#include "WindowsConPtyBackend.h"

#ifdef Q_OS_WIN

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QScopeGuard>

#include <cstddef>
#include <vector>

namespace {

QString quoteWindowsArgument(const QString& argument)
{
    if (!argument.contains(QRegularExpression(QStringLiteral("[\\s\"]")))) {
        return argument;
    }
    QString quoted = QStringLiteral("\"");
    int slashes = 0;
    for (const QChar character : argument) {
        if (character == QLatin1Char('\\')) {
            ++slashes;
        } else if (character == QLatin1Char('"')) {
            quoted += QString(slashes * 2 + 1, QLatin1Char('\\'));
            quoted += character;
            slashes = 0;
        } else {
            quoted += QString(slashes, QLatin1Char('\\'));
            quoted += character;
            slashes = 0;
        }
    }
    quoted += QString(slashes * 2, QLatin1Char('\\'));
    quoted += QLatin1Char('"');
    return quoted;
}

} // namespace

WindowsConPtyBackend::WindowsConPtyBackend(QObject* const parent)
    : ITerminalBackend(parent)
{
}

WindowsConPtyBackend::~WindowsConPtyBackend()
{
    shutdown(1200);
}

bool WindowsConPtyBackend::start(const StartRequest& request, QString* const errorMessage)
{
    if (m_running.load()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("الطرفية تعمل بالفعل.");
        }
        return false;
    }

    const QString program = request.program.isEmpty()
        ? qEnvironmentVariable("COMSPEC", QStringLiteral("C:\\Windows\\System32\\cmd.exe"))
        : request.program;
    if (!QFileInfo::exists(program)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("تعذر العثور على مشغل الطرفية: %1").arg(program);
        }
        return false;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE inputRead = INVALID_HANDLE_VALUE;
    HANDLE inputWrite = INVALID_HANDLE_VALUE;
    HANDLE outputRead = INVALID_HANDLE_VALUE;
    HANDLE outputWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&inputRead, &inputWrite, &security, 0)
        || !CreatePipe(&outputRead, &outputWrite, &security, 0)) {
        if (errorMessage) {
            *errorMessage = windowsError(QStringLiteral("تعذر إنشاء أنابيب الطرفية"));
        }
        closeHandle(inputRead);
        closeHandle(inputWrite);
        closeHandle(outputRead);
        closeHandle(outputWrite);
        return false;
    }
    auto closeLocalPipes = qScopeGuard([&]() {
        closeHandle(inputRead);
        closeHandle(inputWrite);
        closeHandle(outputRead);
        closeHandle(outputWrite);
    });

    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    const COORD size{static_cast<SHORT>(qBound(2, request.initialGrid.width(), 32767)),
                     static_cast<SHORT>(qBound(1, request.initialGrid.height(), 32767))};
    HRESULT result = CreatePseudoConsole(size, inputRead, outputWrite, 0, &m_pseudoconsole);
    if (FAILED(result)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("تعذر إنشاء جلسة الطرفية الأصلية (0x%1).")
                                .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
        }
        return false;
    }
    auto closePseudoconsoleOnFailure = qScopeGuard([this]() { closePseudoconsole(); });

    // CreatePseudoConsole duplicates its communication endpoints into ConHost.
    // Keeping host-side copies of those PTY ends open can prevent the transport
    // from observing the intended channel state, so release them before the
    // hosted process is created.
    closeHandle(inputRead);
    closeHandle(outputWrite);

    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<std::byte> attributeStorage(attributeBytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes)
        || !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                      m_pseudoconsole, sizeof(m_pseudoconsole), nullptr, nullptr)) {
        if (errorMessage) {
            *errorMessage = windowsError(QStringLiteral("تعذر إعداد خصائص عملية الطرفية"));
        }
        if (attributes) {
            DeleteProcThreadAttributeList(attributes);
        }
        return false;
    }
    auto deleteAttributes = qScopeGuard([attributes]() { DeleteProcThreadAttributeList(attributes); });

    QStringList commandArguments;
    commandArguments.append(quoteWindowsArgument(program));
    for (const QString& argument : request.arguments) {
        commandArguments.append(quoteWindowsArgument(argument));
    }
    const QString commandLine = commandArguments.join(QLatin1Char(' '));
    const std::wstring wideCommandLine = commandLine.toStdWString();
    std::vector<wchar_t> mutableCommand(wideCommandLine.begin(), wideCommandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    // Console applications otherwise receive duplicated standard handles from
    // the host process on some Windows configurations.  Explicitly selecting
    // the null standard-handle set prevents cmd.exe from bypassing ConPTY and
    // writing its banner, prompt, and command output to the parent console.
    startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    const std::wstring workingDirectory = request.workingDirectory.isEmpty()
        ? QDir::currentPath().toStdWString()
        : request.workingDirectory.toStdWString();
    const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, flags,
                        nullptr, workingDirectory.c_str(), &startup.StartupInfo, &process)) {
        if (errorMessage) {
            *errorMessage = windowsError(QStringLiteral("تعذر تشغيل عملية الطرفية"));
        }
        return false;
    }

    m_inputWrite = inputWrite;
    inputWrite = INVALID_HANDLE_VALUE;
    m_outputRead = outputRead;
    outputRead = INVALID_HANDLE_VALUE;
    m_process = process.hProcess;
    m_thread = process.hThread;
    m_stopRequested.store(false);
    m_running.store(true);
    closePseudoconsoleOnFailure.dismiss();
    startReadThread();
    return true;
}

void WindowsConPtyBackend::writeInput(const QByteArray& bytes)
{
    if (bytes.isEmpty() || !m_running.load()) {
        return;
    }
    QMutexLocker locker(&m_handleMutex);
    if (m_inputWrite == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    if (!WriteFile(m_inputWrite, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr)) {
        const QString message = windowsError(QStringLiteral("تعذر إرسال إدخال للطرفية"));
        QMetaObject::invokeMethod(this, [this, message]() { emit backendError(message); }, Qt::QueuedConnection);
    }
}

void WindowsConPtyBackend::resize(const QSize& grid)
{
    QMutexLocker locker(&m_handleMutex);
    if (m_pseudoconsole == nullptr || grid.width() < 2 || grid.height() < 1) {
        return;
    }
    const COORD size{static_cast<SHORT>(qMin(grid.width(), 32767)),
                     static_cast<SHORT>(qMin(grid.height(), 32767))};
    const HRESULT result = ResizePseudoConsole(m_pseudoconsole, size);
    if (FAILED(result)) {
        const QString message = QStringLiteral("تعذر تغيير حجم الطرفية الأصلية (0x%1).")
                                    .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
        QMetaObject::invokeMethod(this, [this, message]() { emit backendError(message); }, Qt::QueuedConnection);
    }
}

void WindowsConPtyBackend::requestStop()
{
    if (!m_running.load() || m_stopRequested.exchange(true)) {
        return;
    }
    writeInput(QByteArray(1, '\x03'));
}

void WindowsConPtyBackend::shutdown(const int timeoutMilliseconds)
{
    if (!m_running.load()) {
        return;
    }

    // requestStop() must run while the transport is still marked active; it sends
    // Ctrl+C to the foreground process group before forced termination is needed.
    requestStop();
    const DWORD timeout = static_cast<DWORD>(qMax(0, timeoutMilliseconds));
    if (m_process != INVALID_HANDLE_VALUE
        && WaitForSingleObject(m_process, timeout) == WAIT_TIMEOUT) {
        TerminateProcess(m_process, 1);
        WaitForSingleObject(m_process, 250);
    }
    m_running.store(false);

    {
        QMutexLocker locker(&m_handleMutex);
        closeHandle(m_inputWrite);
        closeHandle(m_outputRead);
    }
    if (m_readerThread != nullptr) {
        // readLoop polls the pipe and observes m_running, so this bounded wait
        // cannot leave a synchronous ReadFile blocked during object destruction.
        m_readerThread->wait(qMax(100, timeoutMilliseconds));
        delete m_readerThread;
        m_readerThread = nullptr;
    }
    closeHandle(m_process);
    closeHandle(m_thread);
    closePseudoconsole();
}

bool WindowsConPtyBackend::isRunning() const
{
    return m_running.load();
}

void WindowsConPtyBackend::closeHandle(HANDLE& handle)
{
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
        CloseHandle(handle);
    }
    handle = INVALID_HANDLE_VALUE;
}

QString WindowsConPtyBackend::windowsError(const QString& context, const DWORD error)
{
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    const QString detail = buffer == nullptr ? QString::number(error) : QString::fromWCharArray(buffer).trimmed();
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return QStringLiteral("%1: %2").arg(context, detail);
}

void WindowsConPtyBackend::startReadThread()
{
    m_readerThread = QThread::create([this]() { readLoop(); });
    m_readerThread->start();
}

void WindowsConPtyBackend::readLoop()
{
    QByteArray buffer(16 * 1024, Qt::Uninitialized);
    int idlePollsAfterChildExit = 0;
    while (m_running.load()) {
        HANDLE output = INVALID_HANDLE_VALUE;
        {
            QMutexLocker locker(&m_handleMutex);
            output = m_outputRead;
        }
        if (output == INVALID_HANDLE_VALUE) {
            break;
        }

        DWORD available = 0;
        if (!PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr)) {
            break;
        }
        if (available == 0) {
            // ConPTY may flush its final frame after the child process has
            // signalled.  Its output pipe remains owned by the pseudoconsole,
            // so EOF alone is not a usable completion signal.  Keep polling for
            // a short bounded grace interval and drain every late frame first.
            const bool childExited = m_process != INVALID_HANDLE_VALUE
                && WaitForSingleObject(m_process, 0) == WAIT_OBJECT_0;
            if (childExited && ++idlePollsAfterChildExit >= 50) {
                break;
            }
            if (!childExited) {
                idlePollsAfterChildExit = 0;
            }
            QThread::msleep(10);
            continue;
        }

        idlePollsAfterChildExit = 0;
        DWORD read = 0;
        const DWORD requested = qMin<DWORD>(available, static_cast<DWORD>(buffer.size()));
        if (!ReadFile(output, buffer.data(), requested, &read, nullptr) || read == 0) {
            break;
        }
        const QByteArray chunk(buffer.constData(), static_cast<qsizetype>(read));
        QMetaObject::invokeMethod(this, [this, chunk]() { emit outputReady(chunk); }, Qt::QueuedConnection);
    }

    int exitCode = -1;
    bool normalExit = false;
    if (m_process != INVALID_HANDLE_VALUE) {
        DWORD processCode = STILL_ACTIVE;
        if (GetExitCodeProcess(m_process, &processCode) && processCode != STILL_ACTIVE) {
            exitCode = static_cast<int>(processCode);
            normalExit = !m_stopRequested.load();
        }
    }
    QMetaObject::invokeMethod(this, [this, exitCode, normalExit]() {
        finishFromWorker(exitCode, normalExit);
    }, Qt::QueuedConnection);
}

void WindowsConPtyBackend::finishFromWorker(const int exitCode, const bool normalExit)
{
    if (!m_running.exchange(false)) {
        return;
    }
    emit finished(exitCode, normalExit);
}

void WindowsConPtyBackend::closePseudoconsole()
{
    QMutexLocker locker(&m_handleMutex);
    if (m_pseudoconsole != nullptr) {
        ClosePseudoConsole(m_pseudoconsole);
        m_pseudoconsole = nullptr;
    }
}

#endif
