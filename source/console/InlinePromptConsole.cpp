#include "InlinePromptConsole.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMimeData>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QThread>

namespace {

// هنا يمكن تعيين سابقة تظهر عندما يطلب البرنامج مدخلات من المستخدم
const QString kPrompt = QString(); // QStringLiteral("ألف › ");
const QString kPendingTruncationNotice =
    QStringLiteral("\n[تم اختصار جزء من المخرجات بسبب حد المخزن المؤقت]\n");
const QString kRenderedTruncationNotice =
    QStringLiteral("[تم حذف مخرجات أقدم للحفاظ على أداء المخرجات]\n");

} // namespace

InlinePromptConsole::InlinePromptConsole(QWidget* const parent)
    : TConsole(parent)
    , m_flushTimer(this)
{
    setObjectName(QStringLiteral("InlinePromptConsole"));
    setAccessibleName(QStringLiteral("مخرجات ألف مع إدخال مباشر"));
    setReadOnly(false);
    setUndoRedoEnabled(false);
    setWordWrapMode(QTextOption::WordWrap);
    setTextInteractionFlags(Qt::TextEditorInteraction);
    document()->setMaximumBlockCount(kMaximumRenderedLines);

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(15);
    setFont(font);
    setStyleSheet(QStringLiteral("QPlainTextEdit { background: #03091A; color: #DEE8FF; border: none; }"));

    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(16);
    connect(&m_flushTimer, &QTimer::timeout, this, &InlinePromptConsole::flushPending);
}

void InlinePromptConsole::appendPlainTextThreadSafe(const QString& text)
{
    m_pendingOutput.append(text);
    scheduleFlush();
}

void InlinePromptConsole::beginInput()
{
    if (m_inputEnabled) {
        ensurePromptVisible();
        return;
    }
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    if (document()->characterCount() > 1 && !toPlainText().endsWith(QLatin1Char('\n'))) {
        cursor.insertBlock();
    }
    m_promptStart = cursor.position();
    cursor.insertText(kPrompt);
    m_promptTextStart = cursor.position();
    m_inputEnabled = true;
    m_historyIndex = -1;
    setTextCursor(cursor);
    ensurePromptVisible();
}

void InlinePromptConsole::endInput(const QString& completionMessage)
{
    if (m_inputEnabled) {
        QTextCursor cursor(document());
        cursor.movePosition(QTextCursor::End);
        if (!toPlainText().endsWith(QLatin1Char('\n'))) {
            cursor.insertBlock();
        }
    }
    m_inputEnabled = false;
    m_promptStart = document()->characterCount() - 1;
    m_promptTextStart = m_promptStart;
    if (!completionMessage.isEmpty()) {
        appendOutputBeforePrompt(completionMessage + QLatin1Char('\n'));
    }
}

void InlinePromptConsole::clearConsole()
{
    m_flushTimer.stop();
    m_pendingOutput.clear();
    m_carriageReturnPending = false;
    m_truncationNoticeVisible = false;
    QPlainTextEdit::clear();
    m_promptStart = 0;
    m_promptTextStart = 0;
    m_historyIndex = -1;
    if (m_inputEnabled) {
        beginInput();
    }
}

void InlinePromptConsole::setConsoleRTL()
{
    setLayoutDirection(Qt::RightToLeft);
    QTextOption option = document()->defaultTextOption();
    option.setTextDirection(Qt::RightToLeft);
    option.setAlignment(Qt::AlignRight);
    document()->setDefaultTextOption(option);
}

bool InlinePromptConsole::acceptsInput() const { return m_inputEnabled; }
qsizetype InlinePromptConsole::pendingOutputBytes() const { return m_pendingOutput.pendingBytes(); }
int InlinePromptConsole::renderedLineCount() const { return document()->blockCount(); }
qsizetype InlinePromptConsole::renderedCharacterCount() const { return document()->characterCount() - 1; }

void InlinePromptConsole::keyPressEvent(QKeyEvent* const event)
{
    if (event->matches(QKeySequence::Copy)) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_L && (event->modifiers() & Qt::ControlModifier)) {
        clearConsole();
        event->accept();
        return;
    }
    if (!m_inputEnabled) {
        if (event->key() == Qt::Key_End || event->key() == Qt::Key_Down) {
            moveCursor(QTextCursor::End);
        }
        event->ignore();
        return;
    }

    QTextCursor cursor = textCursor();
    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const bool modifies = !event->text().isEmpty()
        || event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete
        || event->matches(QKeySequence::Cut) || event->matches(QKeySequence::Paste);
    if (modifies && (selectionStart < m_promptTextStart || selectionEnd < m_promptTextStart
                     || cursor.position() < m_promptTextStart)) {
        const bool destructiveEdit = event->key() == Qt::Key_Backspace
            || event->key() == Qt::Key_Delete || event->matches(QKeySequence::Cut);
        if (destructiveEdit) {
            event->accept();
            return;
        }
        cursor.clearSelection();
        cursor.setPosition(document()->characterCount() - 1);
        setTextCursor(cursor);
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        submitCurrentInput();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Up) {
        if (!m_history.isEmpty()) {
            m_historyIndex = m_historyIndex < 0 ? m_history.size() - 1 : qMax(0, m_historyIndex - 1);
            replaceCurrentInput(m_history.at(m_historyIndex));
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Down) {
        if (m_historyIndex >= 0) {
            m_historyIndex = qMin(m_history.size(), m_historyIndex + 1);
            replaceCurrentInput(m_historyIndex < m_history.size() ? m_history.at(m_historyIndex) : QString());
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Home) {
        cursor.clearSelection();
        cursor.setPosition(m_promptTextStart);
        setTextCursor(cursor);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Left && cursor.position() <= m_promptTextStart
        && cursor.anchor() <= m_promptTextStart) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Backspace && cursor.position() <= m_promptTextStart
        && !cursor.hasSelection()) {
        event->accept();
        return;
    }

    QPlainTextEdit::keyPressEvent(event);
    ensurePromptVisible();
}

void InlinePromptConsole::insertFromMimeData(const QMimeData* const source)
{
    if (!m_inputEnabled || source == nullptr || !source->hasText()) {
        return;
    }
    QTextCursor cursor = textCursor();
    if (!cursorInsidePrompt(cursor)) {
        cursor.clearSelection();
        cursor.setPosition(document()->characterCount() - 1);
    }
    cursor.insertText(source->text().replace(QStringLiteral("\r\n"), QStringLiteral(" "))
                      .replace(QLatin1Char('\n'), QLatin1Char(' ')));
    setTextCursor(cursor);
}

void InlinePromptConsole::mousePressEvent(QMouseEvent* const event)
{
    QPlainTextEdit::mousePressEvent(event);
    if (!m_inputEnabled) {
        return;
    }
    if (event->button() == Qt::LeftButton && textCursor().position() < m_promptTextStart) {
        QTextCursor cursor(document());
        cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);
    }
}

void InlinePromptConsole::flushPending()
{
    const OutputBuffer::DrainResult result = m_pendingOutput.drain();
    if (result.text.isEmpty() && !result.truncated) {
        return;
    }
    if (result.truncated) {
        emit outputTruncated(result.droppedBytes);
        appendOutputBeforePrompt(kPendingTruncationNotice);
    }
    appendOutputBeforePrompt(result.text);
}

void InlinePromptConsole::scheduleFlush()
{
    if (QThread::currentThread() == thread()) {
        if (!m_flushTimer.isActive()) {
            m_flushTimer.start();
        }
        return;
    }
    QMetaObject::invokeMethod(this, [this]() {
        if (!m_flushTimer.isActive()) {
            m_flushTimer.start();
        }
    }, Qt::QueuedConnection);
}

void InlinePromptConsole::appendOutputBeforePrompt(const QString& text)
{
    QString remaining = text;
    while (!remaining.isEmpty()) {
        if (m_carriageReturnPending) {
            if (remaining.startsWith(QLatin1Char('\n'))) {
                appendOutputChunkBeforePrompt(QStringLiteral("\n"));
                remaining.remove(0, 1);
            } else {
                eraseLastOutputLineBeforePrompt();
            }
            m_carriageReturnPending = false;
            continue;
        }

        const qsizetype carriageReturn = remaining.indexOf(QLatin1Char('\r'));
        if (carriageReturn < 0) {
            appendOutputChunkBeforePrompt(remaining);
            break;
        }
        appendOutputChunkBeforePrompt(remaining.left(carriageReturn));
        remaining.remove(0, carriageReturn + 1);
        m_carriageReturnPending = true;
    }
}

void InlinePromptConsole::appendOutputChunkBeforePrompt(const QString& text)
{
    if (text.isEmpty()) {
        return;
    }
    const bool follow = verticalScrollBar()->value() >= verticalScrollBar()->maximum();
    int inputOffset = 0;
    int anchorOffset = 0;
    if (m_inputEnabled) {
        const QTextCursor active = textCursor();
        inputOffset = qMax(0, active.position() - m_promptTextStart);
        anchorOffset = qMax(0, active.anchor() - m_promptTextStart);
    }

    QTextCursor cursor(document());
    const int insertionPoint = m_inputEnabled ? m_promptStart : document()->characterCount() - 1;
    cursor.setPosition(qMax(0, insertionPoint));
    cursor.insertText(text);
    if (m_inputEnabled) {
        m_promptStart += text.size();
        m_promptTextStart += text.size();
        QTextCursor restore(document());
        restore.setPosition(qBound(m_promptTextStart, m_promptTextStart + anchorOffset,
                                   document()->characterCount() - 1));
        restore.setPosition(qBound(m_promptTextStart, m_promptTextStart + inputOffset,
                                   document()->characterCount() - 1), QTextCursor::KeepAnchor);
        setTextCursor(restore);
    }
    trimTranscript();
    if (follow) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
}

void InlinePromptConsole::eraseLastOutputLineBeforePrompt()
{
    const int outputEnd = m_inputEnabled ? m_promptStart : document()->characterCount() - 1;
    const QString transcript = toPlainText();
    const int outputStart = transcript.lastIndexOf(QLatin1Char('\n'), qMax(0, outputEnd - 1)) + 1;
    if (outputStart >= outputEnd) {
        return;
    }

    int inputOffset = 0;
    int anchorOffset = 0;
    if (m_inputEnabled) {
        const QTextCursor active = textCursor();
        inputOffset = qMax(0, active.position() - m_promptTextStart);
        anchorOffset = qMax(0, active.anchor() - m_promptTextStart);
    }

    QTextCursor cursor(document());
    cursor.setPosition(outputStart);
    cursor.setPosition(outputEnd, QTextCursor::KeepAnchor);
    const int removed = cursor.selectionEnd() - cursor.selectionStart();
    cursor.removeSelectedText();
    if (!m_inputEnabled) {
        return;
    }

    m_promptStart -= removed;
    m_promptTextStart -= removed;
    QTextCursor restore(document());
    restore.setPosition(qBound(m_promptTextStart, m_promptTextStart + anchorOffset,
                               document()->characterCount() - 1));
    restore.setPosition(qBound(m_promptTextStart, m_promptTextStart + inputOffset,
                               document()->characterCount() - 1), QTextCursor::KeepAnchor);
    setTextCursor(restore);
}

void InlinePromptConsole::submitCurrentInput()
{
    const QString command = currentInput();
    if (m_history.isEmpty() || m_history.last() != command) {
        m_history.append(command);
    }
    m_historyIndex = -1;
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertBlock();
    m_inputEnabled = false;
    m_promptStart = cursor.position();
    m_promptTextStart = m_promptStart;
    emit commandEntered(command);
    beginInput();
}

void InlinePromptConsole::replaceCurrentInput(const QString& text)
{
    if (!m_inputEnabled) {
        return;
    }
    QTextCursor cursor(document());
    cursor.setPosition(m_promptTextStart);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.insertText(text);
    setTextCursor(cursor);
}

QTextCursor InlinePromptConsole::promptCursor() const
{
    QTextCursor cursor(document());
    cursor.setPosition(m_promptTextStart);
    return cursor;
}

QString InlinePromptConsole::currentInput() const
{
    const QString text = toPlainText();
    return text.mid(qBound(0, m_promptTextStart, text.size()));
}

bool InlinePromptConsole::cursorInsidePrompt(const QTextCursor& cursor) const
{
    return cursor.selectionStart() >= m_promptTextStart && cursor.position() >= m_promptTextStart;
}

void InlinePromptConsole::ensurePromptVisible()
{
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
    ensureCursorVisible();
}

void InlinePromptConsole::trimTranscript()
{
    const qsizetype characters = document()->characterCount() - 1;
    if (characters <= kMaximumRenderedCharacters) {
        return;
    }
    const qsizetype excess = characters - kMaximumRenderedCharacters;
    QTextCursor cursor(document());
    cursor.setPosition(0);
    cursor.setPosition(static_cast<int>(qMin(excess, characters)), QTextCursor::KeepAnchor);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    const int removed = cursor.selectionEnd() - cursor.selectionStart();
    cursor.removeSelectedText();
    m_promptStart = qMax(0, m_promptStart - removed);
    m_promptTextStart = qMax(0, m_promptTextStart - removed);
    if (!m_truncationNoticeVisible) {
        cursor.setPosition(0);
        cursor.insertText(kRenderedTruncationNotice);
        m_promptStart += kRenderedTruncationNotice.size();
        m_promptTextStart += kRenderedTruncationNotice.size();
        m_truncationNoticeVisible = true;
    }
}
