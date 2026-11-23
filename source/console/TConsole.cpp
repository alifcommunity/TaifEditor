#include "TConsole.h"

#include "terminal/TerminalSessionController.h"
#include "terminal/TerminalView.h"

#include <QDir>
#include <QTextOption>
#include <QScrollBar>

TConsole::TConsole(QWidget* const parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("ConsoleOutput"));
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setWordWrapMode(QTextOption::WordWrap);
    setStyleSheet(QStringLiteral("QPlainTextEdit { background: #03091A; color: #DEE8FF; border: none; }"));
}

TConsole::~TConsole()
{
    stopCmd();
}

void TConsole::enableNativeTerminal()
{
    if (m_terminalView != nullptr) {
        return;
    }
    setReadOnly(true);
    setTextInteractionFlags(Qt::NoTextInteraction);
    verticalScrollBar()->hide();
    horizontalScrollBar()->hide();
    m_terminalView = new TerminalView(viewport());
    m_terminalView->setGeometry(viewport()->rect());
    m_terminalView->show();
    m_terminalController = new TerminalSessionController(this);
    connect(m_terminalView, &TerminalView::terminalInput, m_terminalController,
            &TerminalSessionController::sendInput);
    connect(m_terminalView, &TerminalView::gridSizeChanged, m_terminalController,
            &TerminalSessionController::resizeGrid);
    connect(m_terminalController, &TerminalSessionController::outputReady, m_terminalView,
            &TerminalView::appendOutput);
    connect(m_terminalController, &TerminalSessionController::terminalError, this,
            [this](const QString& message) {
                if (m_terminalView != nullptr) {
                    m_terminalView->appendOutput(
                        QStringLiteral("\r\n[خطأ في الطرفية: %1]\r\n").arg(message).toUtf8());
                }
                emit terminalError(message);
            });
    connect(m_terminalController, &TerminalSessionController::finished, this,
            [this](const int exitCode, const bool normalExit) {
                if (m_terminalView != nullptr) {
                    m_terminalView->appendOutput(
                        QStringLiteral("\r\n[انتهت الطرفية %1 (الرمز %2)]\r\n")
                            .arg(normalExit ? QStringLiteral("بشكل طبيعي")
                                            : QStringLiteral("بشكل غير طبيعي"))
                            .arg(exitCode).toUtf8());
                }
            });
    setAccessibleName(QStringLiteral("الطرفية الأصلية للنظام"));
}

bool TConsole::isNativeTerminal() const
{
    return m_terminalView != nullptr;
}

void TConsole::focusNativeTerminal(const Qt::FocusReason reason)
{
    if (m_terminalView != nullptr && m_terminalView->isVisible()) {
        m_terminalView->setFocus(reason);
    }
}

void TConsole::setTerminalWorkingDirectory(const QString& directory)
{
    if (m_terminalController != nullptr && m_terminalController->isActive()) {
        return;
    }
    const QDir candidate(directory);
    m_terminalWorkingDirectory = candidate.exists() ? candidate.absolutePath() : QString();
}

QString TConsole::terminalWorkingDirectory() const
{
    return m_terminalWorkingDirectory;
}

void TConsole::startCmd()
{
    if (m_terminalController == nullptr || m_terminalView == nullptr
        || m_terminalController->isActive()) {
        return;
    }
    TerminalSessionController::Request request;
#if defined(Q_OS_WIN)
    request.program = qEnvironmentVariable("COMSPEC", QStringLiteral("C:\\Windows\\System32\\cmd.exe"));
#elif defined(Q_OS_MACOS)
    request.program = QStringLiteral("/bin/zsh");
    request.arguments = {QStringLiteral("-i")};
#else
    request.program = QStringLiteral("/bin/bash");
    request.arguments = {QStringLiteral("-i")};
#endif
    request.initialGrid = m_terminalView->gridSize();
    request.workingDirectory = m_terminalWorkingDirectory;
    QString error;
    if (!m_terminalController->start(request, &error) && !error.isEmpty()) {
        m_terminalView->appendOutput(QStringLiteral("[تعذر بدء الطرفية: %1]\r\n").arg(error).toUtf8());
        emit terminalError(error);
    }
}

void TConsole::stopCmd()
{
    if (m_terminalController != nullptr) {
        m_terminalController->shutdown();
    }
}

void TConsole::clear()
{
    if (m_terminalView != nullptr) {
        m_terminalView->clearTerminal();
        return;
    }
    QPlainTextEdit::clear();
}

void TConsole::setConsoleRTL() {
    setLayoutDirection(Qt::RightToLeft);
    QTextOption option = document()->defaultTextOption();
    option.setTextDirection(Qt::RightToLeft);
    option.setAlignment(Qt::AlignRight);
    document()->setDefaultTextOption(option);
}

void TConsole::appendPlainTextThreadSafe(const QString& text)
{
    if (m_terminalView != nullptr) {
        m_terminalView->appendOutput(text.toUtf8());
        return;
    }
    appendPlainText(text);
}

qsizetype TConsole::pendingOutputBytes() const
{
    return 0;
}

int TConsole::renderedLineCount() const
{
    return m_terminalView == nullptr ? document()->blockCount() : m_terminalView->screen().rows();
}

qsizetype TConsole::renderedCharacterCount() const
{
    return m_terminalView == nullptr ? document()->characterCount() - 1
                                     : m_terminalView->screen().text().size();
}

void TConsole::resizeEvent(QResizeEvent* const event)
{
    QPlainTextEdit::resizeEvent(event);
    if (m_terminalView != nullptr) {
        m_terminalView->setGeometry(viewport()->rect());
    }
}
