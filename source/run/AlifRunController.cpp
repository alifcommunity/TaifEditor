#include "AlifRunController.h"

#include <QDir>
#include <QFileInfo>

namespace {

void setError(QString* const errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

} // namespace

AlifRunController::AlifRunController(QObject* const parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_cancelEscalationTimer.setSingleShot(true);
    m_cancelEscalationTimer.setInterval(1200);
    connect(&m_cancelEscalationTimer, &QTimer::timeout, this, [this]() {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
        }
    });

    connect(&m_process, &QProcess::started, this, &AlifRunController::handleStarted);
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &AlifRunController::handleStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &AlifRunController::handleStandardError);
    connect(&m_process, &QProcess::errorOccurred, this, &AlifRunController::handleError);
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AlifRunController::handleFinished);
}

AlifRunController::~AlifRunController()
{
    shutdown();
}

AlifRunController::State AlifRunController::state() const
{
    return m_state;
}

bool AlifRunController::isActive() const
{
    return m_state == State::Starting || m_state == State::Running || m_state == State::Stopping;
}

bool AlifRunController::start(const Request& request, QString* const errorMessage)
{
    if (isActive()) {
        setError(errorMessage, QStringLiteral("يوجد تنفيذ قيد التشغيل بالفعل."));
        return false;
    }
    if (!validateRequest(request, errorMessage)) {
        return false;
    }

    m_finishDelivered = false;
    m_shuttingDown = false;
    m_process.setProgram(request.program);
    m_process.setArguments(request.arguments);
    m_process.setWorkingDirectory(request.workingDirectory);
    setState(State::Starting);
    m_process.start();
    return true;
}

void AlifRunController::cancel()
{
    if (!isActive()) {
        return;
    }
    if (m_process.state() == QProcess::NotRunning) {
        return;
    }

    setState(State::Stopping);
    m_process.terminate();
    m_cancelEscalationTimer.start();
}

void AlifRunController::sendInput(const QString& text)
{
    if (m_process.state() == QProcess::Running) {
        m_process.write((text + u'\n').toLocal8Bit());
    }
}

void AlifRunController::shutdown(const int timeoutMilliseconds)
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;

    m_cancelEscalationTimer.stop();
    if (m_process.state() != QProcess::NotRunning) {
        setState(State::Stopping);
        m_process.terminate();
        const int terminateTimeout = qMax(1, timeoutMilliseconds / 2);
        if (!m_process.waitForFinished(terminateTimeout)) {
            m_process.kill();
            m_process.waitForFinished(qMax(1, timeoutMilliseconds - terminateTimeout));
        }
    }
    flushPendingOutput();
    if (m_state == State::Starting || m_state == State::Running || m_state == State::Stopping) {
        setState(State::Idle);
    }
}

void AlifRunController::handleStarted()
{
    if (m_process.state() == QProcess::Running) {
        setState(State::Running);
    }
}

void AlifRunController::handleStandardOutput()
{
    const QString text = decodeProcessBytes(m_process.readAllStandardOutput());
    if (!text.isEmpty()) {
        emit standardOutput(text);
    }
}

void AlifRunController::handleStandardError()
{
    const QString text = decodeProcessBytes(m_process.readAllStandardError());
    if (!text.isEmpty()) {
        emit standardError(text);
    }
}

void AlifRunController::handleError(const QProcess::ProcessError error)
{
    if (error == QProcess::Crashed) {
        completeFailure(QStringLiteral("تعطلت عملية ألف أثناء التنفيذ."));
        return;
    }
    if (error == QProcess::FailedToStart) {
        completeFailure(QStringLiteral("تعذر بدء مفسر ألف: %1").arg(m_process.errorString()));
        return;
    }
    if (error == QProcess::Timedout) {
        completeFailure(QStringLiteral("انتهت مهلة عملية ألف: %1").arg(m_process.errorString()));
        return;
    }
    if (error == QProcess::WriteError || error == QProcess::ReadError) {
        emit standardError(QStringLiteral("خطأ في اتصال عملية ألف: %1\n").arg(m_process.errorString()));
    }
}

void AlifRunController::handleFinished(const int exitCode, const QProcess::ExitStatus exitStatus)
{
    m_cancelEscalationTimer.stop();
    flushPendingOutput();
    if (m_finishDelivered) {
        return;
    }
    m_finishDelivered = true;

    if (exitStatus == QProcess::CrashExit) {
        completeFailure(QStringLiteral("انتهت عملية ألف بشكل غير طبيعي."));
    } else {
        setState(State::Finished);
    }
    emit finished(exitCode, exitStatus);
}

bool AlifRunController::validateRequest(const Request& request, QString* const errorMessage) const
{
    const QFileInfo executable(request.program);
    if (request.program.trimmed().isEmpty() || !executable.exists() || !executable.isFile()) {
        setError(errorMessage, QStringLiteral("لم يتم العثور على مفسر ألف في المسار المحدد."));
        return false;
    }
    if (!request.workingDirectory.trimmed().isEmpty()
        && !QDir(request.workingDirectory).exists()) {
        setError(errorMessage, QStringLiteral("مجلد تشغيل ملف ألف غير صالح."));
        return false;
    }
    return true;
}

void AlifRunController::flushPendingOutput() {
    // QProcess may already have closed its device during destruction or after a
    // failed start.  Reading then produces spurious QIODevice warnings.
    if (!m_process.isOpen()) {
        return;
    }
    handleStandardOutput();
    handleStandardError();
}

void AlifRunController::setState(const State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
}

void AlifRunController::completeFailure(const QString& message)
{
    if (m_state == State::Failed) {
        return;
    }
    setState(State::Failed);
    emit launchFailed(message);
}

QString AlifRunController::decodeProcessBytes(const QByteArray& bytes)
{
#if defined(Q_OS_WIN)
    return QString::fromLocal8Bit(bytes);
#else
    return QString::fromUtf8(bytes);
#endif
}
