#include "PosixPtyBackend.h"

#ifdef Q_OS_UNIX

#include <QElapsedTimer>
#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef Q_OS_MACOS
#include <util.h>
#else
#include <pty.h>
#endif
#include <unistd.h>

namespace {

QString posixError(const QString& context)
{
    return QStringLiteral("%1: %2").arg(context, QString::fromLocal8Bit(std::strerror(errno)));
}

winsize makeWindowSize(const QSize& grid)
{
    winsize size{};
    size.ws_col = static_cast<unsigned short>(qBound(2, grid.width(), 65535));
    size.ws_row = static_cast<unsigned short>(qBound(1, grid.height(), 65535));
    return size;
}

} // namespace

PosixPtyBackend::PosixPtyBackend(QObject* const parent)
    : ITerminalBackend(parent)
{
}

PosixPtyBackend::~PosixPtyBackend()
{
    shutdown(500);
}

bool PosixPtyBackend::start(const StartRequest& request, QString* const errorMessage)
{
    if (m_running.load()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("جلسة الطرفية نشطة بالفعل.");
        }
        return false;
    }
    if (request.program.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("لم يتم تحديد برنامج الطرفية.");
        }
        return false;
    }

    QVector<QByteArray> arguments;
    arguments.reserve(request.arguments.size() + 1);
    arguments.append(request.program.toLocal8Bit());
    for (const QString& argument : request.arguments) {
        arguments.append(argument.toLocal8Bit());
    }
    QVector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (QByteArray& argument : arguments) {
        argv.append(argument.data());
    }
    argv.append(nullptr);
    const QByteArray workingDirectory = request.workingDirectory.toLocal8Bit();

    int masterFd = -1;
    winsize size = makeWindowSize(request.initialGrid);
    const pid_t child = ::forkpty(&masterFd, nullptr, nullptr, &size);
    if (child < 0) {
        if (errorMessage) {
            *errorMessage = posixError(QStringLiteral("تعذر إنشاء PTY للطرفية"));
        }
        return false;
    }
    if (child == 0) {
        if (!workingDirectory.isEmpty() && ::chdir(workingDirectory.constData()) != 0) {
            _exit(126);
        }
        ::execvp(argv.constFirst(), argv.data());
        _exit(127);
    }

    {
        QMutexLocker lock(&m_fdMutex);
        m_masterFd = masterFd;
        m_childPid = child;
    }
    m_stopRequested.store(false);
    m_finishedDelivered.store(false);
    m_running.store(true);
    startReader();
    return true;
}

void PosixPtyBackend::writeInput(const QByteArray& bytes)
{
    if (bytes.isEmpty() || !m_running.load()) {
        return;
    }

    qsizetype offset = 0;
    while (offset < bytes.size()) {
        int fd = -1;
        {
            QMutexLocker lock(&m_fdMutex);
            fd = m_masterFd;
        }
        if (fd < 0) {
            return;
        }

        const ssize_t written = ::write(fd, bytes.constData() + offset,
                                        static_cast<size_t>(bytes.size() - offset));
        if (written > 0) {
            offset += written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd descriptor{fd, POLLOUT, 0};
            ::poll(&descriptor, 1, 100);
            continue;
        }
        return;
    }
}

void PosixPtyBackend::resize(const QSize& grid)
{
    QMutexLocker lock(&m_fdMutex);
    if (m_masterFd < 0) {
        return;
    }
    const winsize size = makeWindowSize(grid);
    ::ioctl(m_masterFd, TIOCSWINSZ, &size);
}

void PosixPtyBackend::requestStop()
{
    m_stopRequested.store(true);
    qint64 childPid = -1;
    {
        QMutexLocker lock(&m_fdMutex);
        childPid = m_childPid;
    }
    if (childPid > 0) {
        ::kill(-static_cast<pid_t>(childPid), SIGINT);
        ::kill(static_cast<pid_t>(childPid), SIGINT);
    }
}

void PosixPtyBackend::shutdown(const int timeoutMilliseconds)
{
    if (!m_running.load() && !m_reader.joinable()) {
        return;
    }

    requestStop();
    const int boundedTimeout = qBound(100, timeoutMilliseconds, 3000);
    QElapsedTimer timer;
    timer.start();
    while (m_running.load() && timer.elapsed() < boundedTimeout) {
        QThread::msleep(10);
    }

    if (m_running.load()) {
        qint64 childPid = -1;
        {
            QMutexLocker lock(&m_fdMutex);
            childPid = m_childPid;
        }
        if (childPid > 0) {
            ::kill(-static_cast<pid_t>(childPid), SIGKILL);
            ::kill(static_cast<pid_t>(childPid), SIGKILL);
        }
    }
    closeMaster();
    if (m_reader.joinable()) {
        m_reader.join();
    }
    m_running.store(false);
}

bool PosixPtyBackend::isRunning() const
{
    return m_running.load();
}

void PosixPtyBackend::startReader()
{
    if (m_reader.joinable()) {
        m_reader.join();
    }
    m_reader = std::thread(&PosixPtyBackend::readLoop, this);
}

void PosixPtyBackend::readLoop()
{
    int waitStatus = 0;
    bool childExited = false;
    QByteArray buffer(8192, Qt::Uninitialized);

    while (true) {
        qint64 childPid = -1;
        int fd = -1;
        {
            QMutexLocker lock(&m_fdMutex);
            childPid = m_childPid;
            fd = m_masterFd;
        }

        if (childPid > 0) {
            const pid_t waited = ::waitpid(static_cast<pid_t>(childPid), &waitStatus, WNOHANG);
            if (waited == static_cast<pid_t>(childPid)) {
                childExited = true;
            }
        }
        if (childExited && fd < 0) {
            break;
        }
        if (fd < 0) {
            break;
        }

        pollfd descriptor{fd, POLLIN | POLLHUP | POLLERR, 0};
        const int pollResult = ::poll(&descriptor, 1, 100);
        if (pollResult > 0 && (descriptor.revents & (POLLIN | POLLHUP | POLLERR))) {
            const ssize_t count = ::read(fd, buffer.data(), static_cast<size_t>(buffer.size()));
            if (count > 0) {
                const QByteArray chunk(buffer.constData(), static_cast<qsizetype>(count));
                QMetaObject::invokeMethod(this, [this, chunk]() { emit outputReady(chunk); },
                                          Qt::QueuedConnection);
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
            if (childExited || (count < 0 && errno == EIO)) {
                break;
            }
        }
        if (childExited) {
            break;
        }
    }

    if (!childExited) {
        qint64 childPid = -1;
        {
            QMutexLocker lock(&m_fdMutex);
            childPid = m_childPid;
        }
        if (childPid > 0) {
            while (::waitpid(static_cast<pid_t>(childPid), &waitStatus, 0) < 0 && errno == EINTR) {
            }
            childExited = true;
        }
    }

    const bool normalExit = childExited && WIFEXITED(waitStatus) && WEXITSTATUS(waitStatus) == 0
        && !m_stopRequested.load();
    const int exitCode = childExited && WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1;
    closeMaster();
    m_running.store(false);
    deliverFinished(exitCode, normalExit);
}

void PosixPtyBackend::deliverFinished(const int exitCode, const bool normalExit)
{
    if (m_finishedDelivered.exchange(true)) {
        return;
    }
    QMetaObject::invokeMethod(this, [this, exitCode, normalExit]() {
        emit finished(exitCode, normalExit);
    }, Qt::QueuedConnection);
}

void PosixPtyBackend::closeMaster()
{
    QMutexLocker lock(&m_fdMutex);
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
}

#endif
