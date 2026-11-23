#include "TerminalSessionController.h"

#include "ITerminalBackend.h"
#ifdef Q_OS_WIN
#include "WindowsConPtyBackend.h"
#else
#include "PosixPtyBackend.h"
#endif

TerminalSessionController::TerminalSessionController(QObject* const parent)
    : QObject(parent)
#ifdef Q_OS_WIN
    , m_backend(new WindowsConPtyBackend(this))
#else
    , m_backend(new PosixPtyBackend(this))
#endif
    , m_cancelEscalation(this)
{
    qRegisterMetaType<TerminalSessionController::State>();
    m_cancelEscalation.setSingleShot(true);
    m_cancelEscalation.setInterval(1200);
    connect(m_backend, &ITerminalBackend::outputReady, this,
            &TerminalSessionController::handleBackendOutput);
    connect(m_backend, &ITerminalBackend::backendError, this,
            &TerminalSessionController::handleBackendError);
    connect(m_backend, &ITerminalBackend::finished, this,
            &TerminalSessionController::handleBackendFinished);
    connect(&m_cancelEscalation, &QTimer::timeout, this, [this]() {
        if (m_state == State::Stopping && m_backend != nullptr && m_backend->isRunning()) {
            m_backend->shutdown(200);
        }
    });
}

TerminalSessionController::~TerminalSessionController()
{
    shutdown();
}

TerminalSessionController::State TerminalSessionController::state() const
{
    return m_state;
}

bool TerminalSessionController::isActive() const
{
    return m_state == State::Starting || m_state == State::Running || m_state == State::Stopping;
}

bool TerminalSessionController::start(const Request& request, QString* const errorMessage)
{
    if (isActive()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("جلسة الطرفية نشطة بالفعل.");
        }
        return false;
    }
    if (m_backend == nullptr) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("لم يتم تجهيز محرك الطرفية.");
        }
        setState(State::Failed);
        return false;
    }

    m_grid = request.initialGrid.expandedTo(QSize(2, 1));
    m_finishDelivered = false;
    ITerminalBackend::StartRequest backendRequest;
    backendRequest.program = request.program;
    backendRequest.arguments = request.arguments;
    backendRequest.workingDirectory = request.workingDirectory;
    backendRequest.initialGrid = m_grid;

    setState(State::Starting);
    QString backendError;
    if (!m_backend->start(backendRequest, &backendError)) {
        setState(State::Failed);
        if (errorMessage) {
            *errorMessage = backendError;
        }
        emit terminalError(backendError);
        return false;
    }
    setState(State::Running);
    return true;
}

void TerminalSessionController::sendInput(const QByteArray& bytes)
{
    if (m_state != State::Running || bytes.isEmpty() || m_backend == nullptr) {
        return;
    }
    m_backend->writeInput(bytes);
}

void TerminalSessionController::resizeGrid(const QSize& grid)
{
    const QSize normalized = grid.expandedTo(QSize(2, 1));
    if (normalized == m_grid) {
        return;
    }
    m_grid = normalized;
    if (m_backend != nullptr && isActive()) {
        m_backend->resize(m_grid);
    }
}

void TerminalSessionController::cancel()
{
    if (!isActive() || m_backend == nullptr) {
        return;
    }
    setState(State::Stopping);
    m_backend->requestStop();
    m_cancelEscalation.start();
}

void TerminalSessionController::shutdown(const int timeoutMilliseconds)
{
    m_cancelEscalation.stop();
    if (m_backend != nullptr && m_backend->isRunning()) {
        setState(State::Stopping);
        m_backend->shutdown(qBound(100, timeoutMilliseconds, 3000));
    }
    if (m_state == State::Stopping && !m_finishDelivered) {
        // Backend shutdown suppresses its worker completion signal while tearing
        // down handles.  Complete the controller transition on the GUI thread so
        // callers never retain an indefinitely active terminal session.
        deliverFinished(-1, false);
    }
}

void TerminalSessionController::handleBackendOutput(const QByteArray& bytes)
{
    if (!bytes.isEmpty()) {
        emit outputReady(bytes);
    }
}

void TerminalSessionController::handleBackendError(const QString& message)
{
    emit terminalError(message);
}

void TerminalSessionController::handleBackendFinished(const int exitCode, const bool normalExit)
{
    m_cancelEscalation.stop();
    deliverFinished(exitCode, normalExit);
}

void TerminalSessionController::setState(const State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
}

void TerminalSessionController::deliverFinished(const int exitCode, const bool normalExit)
{
    if (m_finishDelivered) {
        return;
    }
    m_finishDelivered = true;
    setState(normalExit ? State::Finished : State::Failed);
    emit finished(exitCode, normalExit);
}
