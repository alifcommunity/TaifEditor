#include "TerminalView.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>
#include <QWheelEvent>

#include <utility>

// ============================================================================
//  Construction
// ============================================================================

TerminalView::TerminalView(QWidget* const parent)
    : QAbstractScrollArea(parent)
    , m_screen(80, 9)
    , m_parser(m_screen)
    , m_resizeDebounce(this)
    , m_updateTimer(this)
    , m_cursorBlinkTimer(this)
    , m_autoScrollTimer(this)
{
    setObjectName(QStringLiteral("NativeTerminalView"));
    setAccessibleName(QStringLiteral("Terminal View"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setMouseTracking(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    viewport()->setCursor(Qt::IBeamCursor);

    // -- Font -----------------------------------------------------------------
    m_terminalFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_baseFontPixelSize = m_terminalFont.pixelSize();
    if (m_baseFontPixelSize <= 0) {
        m_baseFontPixelSize = 15;
    }
    m_terminalFont.setPixelSize(m_baseFontPixelSize);
    m_terminalFont.setStyleStrategy(QFont::PreferAntialias);
    setFont(m_terminalFont);
    const QFontMetrics metrics(m_terminalFont);
    m_cellSize = QSize(qMax(7, metrics.horizontalAdvance(QLatin1Char('M'))),
                       qMax(14, metrics.height()));

    // -- Grid / resize debounce ----------------------------------------------
    m_resizeDebounce.setSingleShot(true);
    m_resizeDebounce.setInterval(80);
    connect(&m_resizeDebounce, &QTimer::timeout, this, [this]() {
        emit gridSizeChanged(gridSize());
    });

    // -- Batched repaint -----------------------------------------------------
    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(16);
    connect(&m_updateTimer, &QTimer::timeout, this, [this]() {
        m_updatePending = false;
        viewport()->update();
    });

    // -- Cursor blink --------------------------------------------------------
    m_cursorBlinkTimer.setInterval(530);
    connect(&m_cursorBlinkTimer, &QTimer::timeout, this, [this]() {
        m_cursorOn = !m_cursorOn;
        viewport()->update();
    });

    // -- Auto-scroll while dragging past viewport edge -----------------------
    m_autoScrollTimer.setInterval(30);
    connect(&m_autoScrollTimer, &QTimer::timeout, this, [this]() {
        if (m_autoScrollDelta == 0) return;
        const int maximum = verticalScrollBar()->maximum();
        const int newValue = qBound(0, m_scrollOffset + m_autoScrollDelta, maximum);
        if (newValue == m_scrollOffset) return;
        verticalScrollBar()->setValue(newValue);
        const int h = viewport()->height();
        const int clampedY = qBound(0, m_lastDragPos.y(), h - 1);
        const int clampedX = qBound(0, m_lastDragPos.x(), viewport()->width() - 1);
        updateSelection(cellAt(QPoint(clampedX, clampedY)), true);
    });

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](const int value) {
        m_scrollOffset = value;
        scheduleViewportUpdate();
    });

    recalculateGrid();
}

// ============================================================================
//  Public API
// ============================================================================

void TerminalView::appendOutput(const QByteArray& bytes)
{
    const QString previousTitle = m_screen.title();
    const bool followTail =
        verticalScrollBar()->value() == verticalScrollBar()->maximum();
    m_parser.feed(bytes);
    updateScrollBar(followTail);
    if (previousTitle != m_screen.title()) {
        emit terminalTitleChanged(m_screen.title());
    }
    scheduleViewportUpdate();
}

void TerminalView::clearTerminal()
{
    m_screen.clearAll();
    clearSelection();
    m_inputBuffer.clear();
    updateScrollBar(true);
    scheduleViewportUpdate();
}

TerminalScreenModel& TerminalView::screen() { return m_screen; }

QSize TerminalView::gridSize() const
{
    return QSize(m_screen.columns(), m_screen.rows());
}

void TerminalView::setMaxScrollback(const int rows)
{
    m_maxScrollback = qMax(10, rows);
    const bool atBottom =
        verticalScrollBar()->value() == verticalScrollBar()->maximum();
    updateScrollBar(atBottom);
    scheduleViewportUpdate();
}

int TerminalView::maxScrollback() const { return m_maxScrollback; }

QString TerminalView::selectedText() const
{
    if (!hasSelection()) return {};

    const int columns = m_screen.columns();
    const int begin = qMin(m_selectionAnchor.row * columns + m_selectionAnchor.column,
                           m_selectionExtent.row * columns + m_selectionExtent.column);
    const int end   = qMax(m_selectionAnchor.row * columns + m_selectionAnchor.column,
                         m_selectionExtent.row * columns + m_selectionExtent.column);
    const int totalRows = totalRowCount();
    const int lastRow = end / columns;

    QString result;
    result.reserve((end - begin + 1) * 2);

    for (int index = begin; index <= end; ++index) {
        const int row = index / columns;
        const int column = index % columns;
        if (row < 0 || row >= totalRows) continue;
        const auto& cells = rowAt(row);
        if (column >= cells.size()) continue;
        const auto& cell = cells[column];
        cell.text.isEmpty() ? result += QLatin1Char(' ') : result += cell.text;
        if (column == columns - 1 && row != lastRow) {
            result += QLatin1Char('\n');
        }
    }
    return result;
}

// ============================================================================
//  Public actions
// ============================================================================

void TerminalView::copySelection()
{
    const QString text = selectedText();
    if (text.isEmpty()) return;
    QApplication::clipboard()->setText(text, QClipboard::Clipboard);
    // Also mirror to primary selection on platforms that support it.
    if (QApplication::clipboard()->supportsSelection()) {
        QApplication::clipboard()->setText(text, QClipboard::Selection);
    }
}

void TerminalView::pasteClipboard()
{
    const QString text = QApplication::clipboard()->text(QClipboard::Clipboard);
    if (text.isEmpty()) return;
    handleTextInput(text);
}

void TerminalView::selectAll()
{
    if (totalRowCount() <= 0) return;
    m_selectionAnchor = {0, 0};
    m_selectionExtent = {totalRowCount() - 1, m_screen.columns() - 1};
    scheduleViewportUpdate();
}

void TerminalView::clearScrollback()
{
    clearTerminal();
}

// ============================================================================
//  Event dispatch (ShortcutOverride, keys, context menu)
// ============================================================================

bool TerminalView::event(QEvent* const event)
{
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        // Claim our shortcuts so a menu/toolbar action with the same binding
        // doesn't swallow the key before it reaches keyPressEvent().
        auto* ke = static_cast<QKeyEvent*>(event);
        if (isTerminalShortcut(ke)) {
            ke->accept();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QAbstractScrollArea::event(event);
}

bool TerminalView::isTerminalShortcut(const QKeyEvent* const event) const
{
    const auto mods = event->modifiers();

    // --- Explicit clipboard keys (any modifier combination) ---------------
    if (event->key() == Qt::Key_Insert) return true;           // Ins / Shift+Ins / Ctrl+Ins
    if (event->key() == Qt::Key_Delete
        && (mods & Qt::ShiftModifier)) return true;            // Shift+Del

    // --- Every Ctrl / Alt / Meta combination ------------------------------
    // A terminal is a keyboard sink: any of these could be a control code,
    // a Meta-prefixed character, or an emulator shortcut. Never let the
    // host application's menu actions see them while we have focus.
    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return true;
    }

    // --- Non-text keys the terminal always handles ------------------------
    switch (event->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Backspace:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_F1:  case Qt::Key_F2:  case Qt::Key_F3:  case Qt::Key_F4:
    case Qt::Key_F5:  case Qt::Key_F6:  case Qt::Key_F7:  case Qt::Key_F8:
    case Qt::Key_F9:  case Qt::Key_F10: case Qt::Key_F11: case Qt::Key_F12:
        return true;
    default:
        break;
    }

    return false;
}

void TerminalView::contextMenuEvent(QContextMenuEvent* const event)
{
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(R"(
        QMenu {
            background: #0B1428;
            color: #DEE8FF;
            border: 1px solid #223052;
        }
        QMenu::item:selected { background: #294b78; }
        QMenu::item:disabled { color: #556688; }
    )"));

    QAction* copyAction = menu.addAction(tr("نسخ"));
    copyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    copyAction->setEnabled(hasSelection());
    connect(copyAction, &QAction::triggered, this, &TerminalView::copySelection);

    QAction* pasteAction = menu.addAction(tr("لصق"));
    pasteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    pasteAction->setEnabled(
        !QApplication::clipboard()->text(QClipboard::Clipboard).isEmpty());
    connect(pasteAction, &QAction::triggered, this, &TerminalView::pasteClipboard);

    menu.addSeparator();

    QAction* selectAllAction = menu.addAction(tr("تحديد الكل"));
    selectAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    connect(selectAllAction, &QAction::triggered, this, &TerminalView::selectAll);

    QAction* clearAction = menu.addAction(tr("مسح"));
    connect(clearAction, &QAction::triggered, this, &TerminalView::clearTerminal);

    menu.exec(event->globalPos());
    event->accept();
}

// ============================================================================
//  Painting
// ============================================================================

void TerminalView::paintEvent(QPaintEvent* const event)
{
    Q_UNUSED(event);

    QPainter painter(viewport());
    const QColor bgDefault = defaultBackground();
    const QColor fgDefault = defaultForeground();

    painter.fillRect(viewport()->rect(), bgDefault);
    painter.setFont(m_terminalFont);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int columns    = m_screen.columns();
    const int cellWidth  = m_cellSize.width();
    const int cellHeight = m_cellSize.height();
    const int visibleRows = visibleRowCount();
    const int totalVis    = visualRowCount();
    const int maximum     = qMax(0, totalVis - visibleRows);
    const int firstVisual = qBound(0, m_scrollOffset, maximum);
    const int lastVisual  = qMin(totalVis, firstVisual + visibleRows);
    const int skip        = scrollbackSkip();

    const bool hasSel = hasSelection();
    const int selStart = hasSel
                             ? qMin(m_selectionAnchor.row * columns + m_selectionAnchor.column,
                                    m_selectionExtent.row * columns + m_selectionExtent.column)
                             : -1;
    const int selEnd = hasSel
                           ? qMax(m_selectionAnchor.row * columns + m_selectionAnchor.column,
                                  m_selectionExtent.row * columns + m_selectionExtent.column)
                           : -1;

    const bool atBottom = (m_scrollOffset >= maximum);
    const bool cursorVisible = m_hasFocus && m_screen.cursor().visible
                               && m_cursorOn && atBottom;
    const int cursorActualRow =
        static_cast<int>(m_screen.scrollback().size()) + m_screen.cursor().row;
    const int cursorVisualRow = cursorVisible ? (cursorActualRow - skip) : -1;
    const int cursorColumn    = cursorVisible ? m_screen.cursor().column : -1;

    QTextOption rowOption;
    rowOption.setWrapMode(QTextOption::NoWrap);
    rowOption.setTextDirection(Qt::LeftToRight);
    rowOption.setAlignment(Qt::AlignLeft);
    rowOption.setUseDesignMetrics(true);

    const QColor selectionColor(QStringLiteral("#294b78"));
    const QColor cursorBackground(QStringLiteral("#DEE8FF"));

    struct CellStyle { QColor fg; QColor bg; bool bold; bool underline; };
    QVector<CellStyle> styles(columns);

    QString rowText;
    rowText.reserve(columns * 2);

    QVector<QTextLayout::FormatRange> formats;
    formats.reserve(columns);

    for (int visualRow = firstVisual; visualRow < lastVisual; ++visualRow) {
        const int actualRow = visualRow + skip;
        const auto& cells = rowAt(actualRow);
        const TerminalScreenModel::Cell blankCell;
        const qreal rowY = (visualRow - firstVisual) * cellHeight;

        // Resolve every cell's effective style up front.
        for (int column = 0; column < columns; ++column) {
            const auto& cell = column < cells.size() ? cells[column] : blankCell;
            QColor fg = cell.attributes.foreground.isValid()
                            ? cell.attributes.foreground : fgDefault;
            QColor bg = cell.attributes.background.isValid()
                            ? cell.attributes.background : bgDefault;
            if (cell.attributes.inverse) std::swap(fg, bg);

            const int linear = actualRow * columns + column;
            const bool selected = hasSel && linear >= selStart && linear <= selEnd;

            if (selected) bg = selectionColor;
            styles[column] = { fg, bg,
                              cell.attributes.bold,
                              cell.attributes.underline };
        }

        // Backgrounds: run-length merged rectangles.
        int runStart = 0;
        QColor runColor = styles[0].bg;
        for (int column = 1; column < columns; ++column) {
            const QColor bg = styles[column].bg;
            if (bg != runColor) {
                if (runColor != bgDefault) {
                    painter.fillRect(QRectF(runStart * cellWidth, rowY,
                                            (column - runStart) * cellWidth,
                                            cellHeight), runColor);
                }
                runStart = column;
                runColor = bg;
            }
        }
        if (runColor != bgDefault) {
            painter.fillRect(QRectF(runStart * cellWidth, rowY,
                                    (columns - runStart) * cellWidth, cellHeight),
                             runColor);
        }

        // Build the row text and merged format ranges.
        rowText.clear();
        formats.clear();

        QTextCharFormat currentFormat;
        int currentFormatStart = 0;
        int currentFormatLength = 0;
        bool hasFormat = false;

        for (int column = 0; column < columns; ++column) {
            const auto& cell = cells[column];
            const int start = rowText.size();
            cell.text.isEmpty() ? rowText += QLatin1Char(' ') : rowText += cell.text;
            const int length = rowText.size() - start;
            if (length <= 0) continue;

            QTextCharFormat format;
            format.setForeground(styles[column].fg);
            if (styles[column].bold)      format.setFontWeight(QFont::Bold);
            if (styles[column].underline) format.setFontUnderline(true);

            if (!hasFormat) {
                currentFormat = format;
                currentFormatStart = start;
                currentFormatLength = length;
                hasFormat = true;
            } else if (format == currentFormat) {
                currentFormatLength += length;
            } else {
                formats.append({currentFormatStart, currentFormatLength, currentFormat});
                currentFormat = format;
                currentFormatStart = start;
                currentFormatLength = length;
            }
        }
        if (hasFormat) {
            formats.append({currentFormatStart, currentFormatLength, currentFormat});
        }

        if (rowText.isEmpty()) continue;

        QTextLayout layout(rowText, m_terminalFont);
        layout.setTextOption(rowOption);
        layout.beginLayout();
        QTextLine line = layout.createLine();
        if (line.isValid()) {
            line.setLineWidth(columns * cellWidth);
            const qreal padY = qMax<qreal>(0.0, (cellHeight - line.height()) / 2.0);
            line.setPosition(QPointF(0, rowY + padY));

            if (visualRow == cursorVisualRow && cursorColumn >= 0) {
                const int cursorPosition = qBound(0, cursorColumn, rowText.size());
                const qreal cursorX = line.cursorToX(cursorPosition);
                painter.fillRect(QRectF(cursorX, rowY,
                                        cellWidth, cellHeight), cursorBackground);
            }
        }
        layout.endLayout();
        layout.draw(&painter, QPointF(0, 0), formats);
    }
}

// ============================================================================
//  Layout / coordinates
// ============================================================================

void TerminalView::resizeEvent(QResizeEvent* const event)
{
    QAbstractScrollArea::resizeEvent(event);
    recalculateGrid();
}

void TerminalView::recalculateGrid()
{
    const int columns = qMax(2, viewport()->width()  / m_cellSize.width());
    const int rows    = qMax(1, viewport()->height() / m_cellSize.height());

    if (columns != m_screen.columns()) {
        const bool followTail =
            verticalScrollBar()->value() == verticalScrollBar()->maximum();
        // Resize columns only.  A vertical widget resize must not resize the
        // terminal model: TerminalScreenModel::resize() moves rows into
        // scrollback or appends blank rows, which makes the viewport churn.
        // Changing the width is still required so subsequent terminal output
        // wraps at the new right edge.
        m_screen.resize(columns, m_screen.rows());
        updateScrollBar(followTail);
        m_resizeDebounce.start();
        scheduleViewportUpdate();
    } else if (rows != m_screen.rows()) {
        // The viewport height changed, but the terminal rows remain stable.
        // Only update the view's scrollbar/page geometry; do not notify the
        // PTY or regenerate screen rows.
        const bool followTail =
            verticalScrollBar()->value() == verticalScrollBar()->maximum();
        updateScrollBar(followTail);
        scheduleViewportUpdate();
    }
}

void TerminalView::updateScrollBar(const bool followTail)
{
    const int maximum  = qMax(0, visualRowCount() - visibleRowCount());
    const int pageStep = visibleRowCount();

    if (verticalScrollBar()->pageStep() != pageStep) {
        verticalScrollBar()->setPageStep(pageStep);
    }
    if (verticalScrollBar()->minimum() != 0
        || verticalScrollBar()->maximum() != maximum) {
        verticalScrollBar()->setRange(0, maximum);
    }

    if (followTail) {
        m_scrollOffset = maximum;
        verticalScrollBar()->setValue(maximum);
    } else {
        m_scrollOffset = qBound(0, m_scrollOffset, maximum);
        verticalScrollBar()->setValue(m_scrollOffset);
    }
}

int TerminalView::visibleRowCount() const
{
    return qMax(1, viewport()->height() / m_cellSize.height());
}

int TerminalView::totalRowCount() const
{
    return static_cast<int>(m_screen.scrollback().size()
                            + m_screen.grid().size());
}

int TerminalView::visualRowCount() const
{
    return qMin(totalRowCount(), m_maxScrollback);
}

int TerminalView::scrollbackSkip() const
{
    return qMax(0, totalRowCount() - m_maxScrollback);
}

const QVector<TerminalScreenModel::Cell>&
TerminalView::rowAt(const int actualRow) const
{
    const auto& scrollback = m_screen.scrollback();
    const int scrollbackSize = static_cast<int>(scrollback.size());
    if (actualRow < scrollbackSize) {
        return scrollback.at(actualRow);
    }
    return m_screen.grid().at(actualRow - scrollbackSize);
}

const QVector<TerminalScreenModel::Cell>&
TerminalView::visualRowAt(const int visualRow) const
{
    return rowAt(visualRow + scrollbackSkip());
}

// ============================================================================
//  Keyboard
// ============================================================================

void TerminalView::keyPressEvent(QKeyEvent* const event)
{
    const auto modifiers = event->modifiers();
    const bool ctrl  = modifiers & Qt::ControlModifier;
    const bool shift = modifiers & Qt::ShiftModifier;
    const bool alt   = modifiers & Qt::AltModifier;

    // ---- Ctrl+Tab / Ctrl+Shift+Tab: focus escape (unchanged) -------------
    if (event->key() == Qt::Key_Tab && ctrl) {
        QAbstractScrollArea::focusNextPrevChild(!shift);
        event->accept();
        return;
    }

    // ---- Copy ------------------------------------------------------------
    // Ctrl+Shift+C  |  Ctrl+Insert  |  Shift+Ctrl+Insert
    if ((event->key() == Qt::Key_C && ctrl && shift)
        || (event->key() == Qt::Key_Insert && ctrl)) {
        copySelection();
        event->accept();
        return;
    }

    // ---- Paste -----------------------------------------------------------
    // Ctrl+Shift+V  |  Shift+Insert
    // (Plain Ctrl+V stays as a control code — see below.)
    if ((event->key() == Qt::Key_V && ctrl && shift)
        || (event->key() == Qt::Key_Insert && shift)) {
        pasteClipboard();
        event->accept();
        return;
    }

    // ---- Select all ------------------------------------------------------
    if (event->key() == Qt::Key_A && ctrl && shift) {
        selectAll();
        event->accept();
        return;
    }

    // ---- Ctrl+C: unconditional. Copy if we have a selection, else SIGINT.
    if (event->key() == Qt::Key_C && ctrl && !shift) {
        if (hasSelection()) {
            copySelection();
        } else {
            m_inputBuffer.clear();
            emit terminalInput(QByteArray(1, '\x03'));   // ETX
            restartCursorBlink();
        }
        event->accept();
        return;
    }

    // ---- Ctrl+V: honour the terminal. Paste into the shell (like xterm
    // with bracketed paste disabled), NOT into the host editor.
    if (event->key() == Qt::Key_V && ctrl && !shift && !alt) {
        pasteClipboard();
        event->accept();
        return;
    }

    // ---- Ctrl+X: send XOFF / CAN to the shell (do NOT cut in the editor).
    if (event->key() == Qt::Key_X && ctrl && !shift) {
        emit terminalInput(QByteArray(1, '\x18'));       // CAN
        restartCursorBlink();
        event->accept();
        return;
    }

    // -- Backspace -----------------------------------------------------------
    if (event->key() == Qt::Key_Backspace) {
        handleBackspace(alt, ctrl);
        event->accept();
        return;
    }

    // -- TAB -----------------------------------------------------------------
    if (event->key() == Qt::Key_Tab && !shift && !ctrl && !alt) {
        handleTab();
        event->accept();
        return;
    }

    // -- Enter / Return ------------------------------------------------------
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        handleEnter();
        event->accept();
        return;
    }

    // -- Shift+PageUp/Down/Home/End: scrollback navigation -------------------
    if (shift) {
        const int page = visibleRowCount();
        if (event->key() == Qt::Key_PageUp) {
            verticalScrollBar()->setValue(verticalScrollBar()->value() - page);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_PageDown) {
            verticalScrollBar()->setValue(verticalScrollBar()->value() + page);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Home) {
            verticalScrollBar()->setValue(verticalScrollBar()->minimum());
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_End) {
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
            event->accept();
            return;
        }
    }

    // -- Fallback: encode as VT sequence or text -----------------------------
    const QByteArray encoded = encodeKey(event);
    if (!encoded.isEmpty()) {
        emit terminalInput(encoded);
        restartCursorBlink();
        event->accept();
        return;
    }

    QAbstractScrollArea::keyPressEvent(event);
}

QByteArray TerminalView::encodeKey(const QKeyEvent* const event) const
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool alt  = modifiers & Qt::AltModifier;
    const bool ctrl = modifiers & Qt::ControlModifier;
    const bool meta = modifiers & Qt::MetaModifier;

    QByteArray result;

    if (!event->text().isEmpty() && !(ctrl && !alt)) {
        if (ctrl && event->text().size() == 1) {
            const ushort value = event->text().at(0).toUpper().unicode();
            if (value >= '@' && value <= '_') {
                result = QByteArray(1, static_cast<char>(value - '@'));
            }
        }
        if (result.isEmpty()) {
            result = event->text().toUtf8();
        }
        if (alt || meta) {
            return QByteArray(1, '\x1b') + result;
        }
        return result;
    }

    auto withAlt = [&](const QByteArray& seq) {
        return (alt || meta) ? (QByteArray(1, '\x1b') + seq) : seq;
    };

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:     return withAlt(QByteArrayLiteral("\r"));
    case Qt::Key_Backspace: return withAlt(QByteArray(1, '\x7f'));
    case Qt::Key_Tab:       return withAlt(QByteArrayLiteral("\t"));
    case Qt::Key_Escape:    return QByteArray(1, '\x1b');
    case Qt::Key_Up:        return withAlt(QByteArrayLiteral("\x1b[A"));
    case Qt::Key_Down:      return withAlt(QByteArrayLiteral("\x1b[B"));
    case Qt::Key_Right:     return withAlt(QByteArrayLiteral("\x1b[C"));
    case Qt::Key_Left:      return withAlt(QByteArrayLiteral("\x1b[D"));
    case Qt::Key_Home:      return withAlt(QByteArrayLiteral("\x1b[H"));
    case Qt::Key_End:       return withAlt(QByteArrayLiteral("\x1b[F"));
    case Qt::Key_Insert:    return withAlt(QByteArrayLiteral("\x1b[2~"));
    case Qt::Key_Delete:    return withAlt(QByteArrayLiteral("\x1b[3~"));
    case Qt::Key_PageUp:    return withAlt(QByteArrayLiteral("\x1b[5~"));
    case Qt::Key_PageDown:  return withAlt(QByteArrayLiteral("\x1b[6~"));
    case Qt::Key_F1:        return withAlt(QByteArrayLiteral("\x1bOP"));
    case Qt::Key_F2:        return withAlt(QByteArrayLiteral("\x1bOQ"));
    case Qt::Key_F3:        return withAlt(QByteArrayLiteral("\x1bOR"));
    case Qt::Key_F4:        return withAlt(QByteArrayLiteral("\x1bOS"));
    case Qt::Key_F5:        return withAlt(QByteArrayLiteral("\x1b[15~"));
    case Qt::Key_F6:        return withAlt(QByteArrayLiteral("\x1b[17~"));
    case Qt::Key_F7:        return withAlt(QByteArrayLiteral("\x1b[18~"));
    case Qt::Key_F8:        return withAlt(QByteArrayLiteral("\x1b[19~"));
    case Qt::Key_F9:        return withAlt(QByteArrayLiteral("\x1b[20~"));
    case Qt::Key_F10:       return withAlt(QByteArrayLiteral("\x1b[21~"));
    case Qt::Key_F11:       return withAlt(QByteArrayLiteral("\x1b[23~"));
    case Qt::Key_F12:       return withAlt(QByteArrayLiteral("\x1b[24~"));
    default: return {};
    }
}

void TerminalView::handleTextInput(const QString& text)
{
    if (text.isEmpty()) return;

    // Track the local line buffer used by TAB completion.
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            m_inputBuffer.clear();
        } else if (ch == QLatin1Char('\b') || ch == QChar(0x7f)) {
            if (!m_inputBuffer.isEmpty()) m_inputBuffer.chop(1);
        } else if (!ch.isNull()) {
            m_inputBuffer += ch;
        }
    }
    emit terminalInput(text.toUtf8());
    restartCursorBlink();
}

void TerminalView::handleBackspace(const bool alt, const bool ctrl)
{
    QByteArray seq;
    if (ctrl) {
        // Ctrl+Backspace → Ctrl+W (backward-kill-word).
        seq = QByteArray(1, '\x17');
        while (!m_inputBuffer.isEmpty() && m_inputBuffer.back().isSpace())
            m_inputBuffer.chop(1);
        while (!m_inputBuffer.isEmpty() && !m_inputBuffer.back().isSpace())
            m_inputBuffer.chop(1);
    } else {
        // Plain backspace → DEL (0x7f); one character at a time.
        seq = QByteArray(1, '\x7f');
        if (!m_inputBuffer.isEmpty()) m_inputBuffer.chop(1);
    }
    if (alt) seq.prepend('\x1b');
    emit terminalInput(seq);
    restartCursorBlink();
}

void TerminalView::handleTab()
{
    if (!m_autocompleteEnabled || m_inputBuffer.isEmpty()) {
        emit terminalInput(QByteArrayLiteral("\t"));
        return;
    }

    // Extract the "current word" — characters after the last whitespace.
    int start = m_inputBuffer.size();
    while (start > 0 && !m_inputBuffer.at(start - 1).isSpace()) --start;
    const QString prefix = m_inputBuffer.mid(start);

    if (prefix.isEmpty()) {
        emit terminalInput(QByteArrayLiteral("\t"));
        return;
    }

    QString completion;
    const QStringList matches = completeFilenamePrefix(prefix, completion);
    if (completion.isEmpty()) {
        // No unique local match → let the shell do its own completion.
        emit terminalInput(QByteArrayLiteral("\t"));
        return;
    }

    m_inputBuffer += completion;
    emit terminalInput(completion.toUtf8());
    restartCursorBlink();
}

void TerminalView::handleEnter()
{
    m_inputBuffer.clear();
    emit terminalInput(QByteArrayLiteral("\r"));
    restartCursorBlink();
}

QStringList TerminalView::completeFilenamePrefix(const QString& prefix,
                                                 QString& outCompletion) const
{
    outCompletion.clear();
    if (prefix.isEmpty()) return {};

    // Split into directory part and file-name part.
    QString dirPart;
    QString filePart;
    const int lastSlash = prefix.lastIndexOf(QLatin1Char('/'));
    if (lastSlash >= 0) {
        dirPart  = prefix.left(lastSlash + 1);
        filePart = prefix.mid(lastSlash + 1);
    } else {
        filePart = prefix;
    }

    QString searchDir = dirPart.isEmpty() ? QStringLiteral(".") : dirPart;
    if (searchDir.startsWith(QLatin1Char('~'))) {
        searchDir = QDir::homePath() + searchDir.mid(1);
    }

    QDir dir(searchDir);
    if (!dir.exists()) return {};

    const QStringList entries = dir.entryList(
        QStringList{ filePart + QLatin1Char('*') },
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
        QDir::Name | QDir::DirsFirst);

    if (entries.isEmpty()) return {};

    if (entries.size() == 1) {
        const QString& match = entries.first();
        const QFileInfo info(dir, match);
        QString completion = match.mid(filePart.size());
        completion += info.isDir() ? QLatin1Char('/') : QLatin1Char(' ');
        outCompletion = completion;
        return entries;
    }

    // Multiple matches: extend to the longest common prefix if it advances.
    QString common = entries.first();
    for (int i = 1; i < entries.size() && !common.isEmpty(); ++i) {
        const QString& other = entries[i];
        int j = 0;
        while (j < common.size() && j < other.size()
               && common.at(j) == other.at(j)) ++j;
        common.truncate(j);
    }
    if (common.size() > filePart.size()) {
        outCompletion = common.mid(filePart.size());
    }
    return entries;
}

// ============================================================================
//  Mouse
// ============================================================================

void TerminalView::mousePressEvent(QMouseEvent* const event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        const QPoint pos = event->position().toPoint();

        if (m_clickTimer.isValid()
            && m_clickTimer.elapsed() < QApplication::doubleClickInterval()
            && (pos - m_lastClickPos).manhattanLength()
                   < QApplication::startDragDistance()) {
            ++m_clickCount;
        } else {
            m_clickCount = 1;
        }
        m_clickTimer.restart();
        m_lastClickPos = pos;

        if (m_clickCount == 2) {
            selectWordAt(cellAt(pos));
        } else if (m_clickCount >= 3) {
            selectLineAt(cellAt(pos));
            m_clickCount = 0;
        } else {
            updateSelection(cellAt(pos), false);
        }

        m_selecting = true;
        m_lastDragPos = pos;
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        const QString text =
            QApplication::clipboard()->text(QClipboard::Selection);
        if (!text.isEmpty()) handleTextInput(text);
        event->accept();
        return;
    }

    QAbstractScrollArea::mousePressEvent(event);
}

void TerminalView::mouseMoveEvent(QMouseEvent* const event)
{
    if (!m_selecting) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    m_lastDragPos = event->position().toPoint();
    const int h = viewport()->height();
    const int w = viewport()->width();

    int newDelta = 0;
    if (m_lastDragPos.y() < 0)       newDelta = -1;
    else if (m_lastDragPos.y() >= h) newDelta =  1;

    if (newDelta != 0) {
        m_autoScrollDelta = newDelta;
        if (!m_autoScrollTimer.isActive()) m_autoScrollTimer.start();
    } else {
        m_autoScrollDelta = 0;
        m_autoScrollTimer.stop();
    }

    const int clampedY = qBound(0, m_lastDragPos.y(), h - 1);
    const int clampedX = qBound(0, m_lastDragPos.x(), w - 1);
    updateSelection(cellAt(QPoint(clampedX, clampedY)), true);

    event->accept();
}

void TerminalView::mouseReleaseEvent(QMouseEvent* const event)
{
    if (event->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        m_autoScrollDelta = 0;
        m_autoScrollTimer.stop();

        const int h = viewport()->height();
        const int w = viewport()->width();
        const int clampedY = qBound(0, event->position().toPoint().y(), h - 1);
        const int clampedX = qBound(0, event->position().toPoint().x(), w - 1);
        updateSelection(cellAt(QPoint(clampedX, clampedY)), true);
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void TerminalView::wheelEvent(QWheelEvent* const event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int dy = event->angleDelta().y();
        if (dy != 0) {
            applyFontZoom(dy > 0 ? 1 : -1);
            event->accept();
            return;
        }
    }
    QAbstractScrollArea::wheelEvent(event);
}

// ============================================================================
//  Focus
// ============================================================================

void TerminalView::focusInEvent(QFocusEvent* const event)
{
    m_hasFocus = true;
    m_cursorOn = true;
    m_cursorBlinkTimer.start();
    QAbstractScrollArea::focusInEvent(event);
    scheduleViewportUpdate();
}

void TerminalView::focusOutEvent(QFocusEvent* const event)
{
    m_hasFocus = false;
    m_cursorBlinkTimer.stop();
    QAbstractScrollArea::focusOutEvent(event);
    scheduleViewportUpdate();
}

bool TerminalView::focusNextPrevChild(bool next)
{
    Q_UNUSED(next);
    // Return false so Qt does NOT walk the focus chain on Tab / Shift+Tab.
    // The key then falls through to keyPressEvent(), where handleTab() runs
    // filename completion (or forwards a raw \t to the shell).
    //
    // Users who still need to leave the terminal can use Ctrl+Tab, which is
    // handled explicitly in keyPressEvent().
    return false;
}

// ============================================================================
//  Selection helpers
// ============================================================================

bool TerminalView::hasSelection() const
{
    if (m_selectionAnchor.row < 0 || m_selectionExtent.row < 0) {
        return false;
    }
    // A plain click leaves anchor == extent on a single cell. That is cursor
    // placement, not copied content — treat it as "no selection" so that Ctrl+C
    // can reach the running program instead of copying an empty range.
    const int columns = m_screen.columns();
    if (m_selectionAnchor.row * columns + m_selectionAnchor.column
        == m_selectionExtent.row * columns + m_selectionExtent.column) {
        return false;
    }
    return true;
}

void TerminalView::clearSelection()
{
    m_selectionAnchor = {};
    m_selectionExtent = {};
}

void TerminalView::updateSelection(const CellPoint point, const bool extend)
{
    if (point.row < 0 || point.column < 0) return;
    if (!extend) m_selectionAnchor = point;
    m_selectionExtent = point;
    scheduleViewportUpdate();
}

TerminalView::CellPoint TerminalView::cellAt(const QPoint& point) const
{
    const int visualRow = m_scrollOffset + (point.y() / m_cellSize.height());
    const int actualRow = visualRow + scrollbackSkip();
    const int column    = point.x() / m_cellSize.width();

    if (actualRow < 0 || actualRow >= totalRowCount()
        || column < 0 || column >= m_screen.columns()) {
        return {};
    }
    return {actualRow, column};
}

bool TerminalView::isWordChar(const QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

void TerminalView::selectWordAt(const CellPoint point)
{
    if (point.row < 0 || point.row >= totalRowCount()) return;
    const auto& row = rowAt(point.row);
    const int columns = m_screen.columns();
    if (point.column >= columns) return;

    auto isWordAt = [&](int col) -> bool {
        if (col < 0 || col >= row.size()) return false;
        const auto& t = row[col].text;
        return !t.isEmpty() && isWordChar(t.at(0));
    };

    int start = point.column;
    int end   = point.column;

    if (isWordAt(point.column)) {
        while (start > 0 && isWordAt(start - 1)) --start;
        while (end < columns - 1 && isWordAt(end + 1)) ++end;
    } else {
        while (start > 0 && !isWordAt(start - 1)
               && !row[start - 1].text.isEmpty()) --start;
        while (end < columns - 1 && !isWordAt(end + 1)
               && !row[end + 1].text.isEmpty()) ++end;
    }

    m_selectionAnchor = {point.row, start};
    m_selectionExtent = {point.row, end};
    scheduleViewportUpdate();
}

void TerminalView::selectLineAt(const CellPoint point)
{
    if (point.row < 0 || point.row >= totalRowCount()) return;
    m_selectionAnchor = {point.row, 0};
    m_selectionExtent = {point.row, m_screen.columns() - 1};
    scheduleViewportUpdate();
}

// ============================================================================
//  Misc
// ============================================================================

void TerminalView::scheduleViewportUpdate()
{
    if (m_updatePending) return;
    m_updatePending = true;
    m_updateTimer.start();
}

void TerminalView::restartCursorBlink()
{
    m_cursorOn = true;
    if (m_hasFocus) m_cursorBlinkTimer.start();
}

void TerminalView::applyFontZoom(const int delta)
{
    const int current = m_terminalFont.pixelSize();
    const int next = qBound(8, current + delta, 48);
    if (next == current) return;

    m_terminalFont.setPixelSize(next);
    setFont(m_terminalFont);
    const QFontMetrics metrics(m_terminalFont);
    m_cellSize = QSize(qMax(7, metrics.horizontalAdvance(QLatin1Char('M'))),
                       qMax(14, metrics.height()));
    recalculateGrid();
    scheduleViewportUpdate();
}

const QColor& TerminalView::defaultForeground()
{
    static const QColor color(QStringLiteral("#DEE8FF"));
    return color;
}

const QColor& TerminalView::defaultBackground()
{
    static const QColor color(QStringLiteral("#03091A"));
    return color;
}
