#pragma once

#include <QPlainTextEdit>

class TerminalSessionController;
class TerminalView;

/**
 * Stable dock-facing console widget. In system-terminal mode it hosts a direct
 * ConPTY terminal viewport; InlinePromptConsole derives from it for Alif I/O.
 * Neither role uses a separate QLineEdit.
 */
class TConsole : public QPlainTextEdit {
    Q_OBJECT
public:
    static constexpr int kMaximumRenderedLines = 2000;
    static constexpr qsizetype kMaximumRenderedCharacters = 512 * 1024;

    explicit TConsole(QWidget* parent = nullptr);
    ~TConsole() override;

    void enableNativeTerminal();
    [[nodiscard]] bool isNativeTerminal() const;
    void focusNativeTerminal(Qt::FocusReason reason = Qt::OtherFocusReason);
    void setTerminalWorkingDirectory(const QString& directory);
    [[nodiscard]] QString terminalWorkingDirectory() const;

    virtual void startCmd();
    virtual void stopCmd();
    virtual void clear();
    virtual void setConsoleRTL();
    virtual void appendPlainTextThreadSafe(const QString& text);

    [[nodiscard]] virtual qsizetype pendingOutputBytes() const;
    [[nodiscard]] virtual int renderedLineCount() const;
    [[nodiscard]] virtual qsizetype renderedCharacterCount() const;

signals:
    void commandEntered(const QString& command);
    void outputTruncated(qsizetype droppedPendingBytes);
    void terminalError(const QString& message);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    TerminalView* m_terminalView = nullptr;
    TerminalSessionController* m_terminalController = nullptr;
    QString m_terminalWorkingDirectory;
};
