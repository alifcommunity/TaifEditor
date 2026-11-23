#pragma once

#include "OutputBuffer.h"
#include "TConsole.h"

#include <QPlainTextEdit>
#include <QTimer>
#include <QVector>

/**
 * RTL transcript console with one protected inline input range at the bottom.
 * Only the current prompt text is editable; output is inserted before it.
 */
class InlinePromptConsole final : public TConsole {
    Q_OBJECT
public:
    static constexpr int kMaximumRenderedLines = 2000;
    static constexpr qsizetype kMaximumRenderedCharacters = 512 * 1024;

    explicit InlinePromptConsole(QWidget* parent = nullptr);

    void appendPlainTextThreadSafe(const QString& text) override;
    void beginInput();
    void endInput(const QString& completionMessage = {});
    void clearConsole();
    void clear() override { clearConsole(); }
    void setConsoleRTL() override;
    [[nodiscard]] bool acceptsInput() const;
    [[nodiscard]] qsizetype pendingOutputBytes() const override;
    [[nodiscard]] int renderedLineCount() const override;
    [[nodiscard]] qsizetype renderedCharacterCount() const override;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void flushPending();

private:
    void scheduleFlush();
    void appendOutputBeforePrompt(const QString& text);
    void appendOutputChunkBeforePrompt(const QString& text);
    void eraseLastOutputLineBeforePrompt();
    void submitCurrentInput();
    void replaceCurrentInput(const QString& text);
    [[nodiscard]] QTextCursor promptCursor() const;
    [[nodiscard]] QString currentInput() const;
    [[nodiscard]] bool cursorInsidePrompt(const QTextCursor& cursor) const;
    void ensurePromptVisible();
    void trimTranscript();

    OutputBuffer m_pendingOutput;
    QTimer m_flushTimer;
    int m_promptStart = 0;
    int m_promptTextStart = 0;
    bool m_inputEnabled = false;
    bool m_carriageReturnPending = false;
    bool m_truncationNoticeVisible = false;
    QVector<QString> m_history;
    int m_historyIndex = -1;
};
