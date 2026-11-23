#pragma once

#include "TerminalScreenModel.h"
#include "VtStreamParser.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QTimer>

class QContextMenuEvent;
class QKeyEvent;

/**
 * Native-feeling LTR terminal viewport.
 *
 * Features:
 *   • Copy / Paste (Ctrl+Shift+C / Ctrl+Shift+V, Ctrl+Insert / Shift+Insert,
 *     middle-click primary paste, right-click context menu).
 *   • Ctrl+C copies a live selection, otherwise sends SIGINT (\x03).
 *   • Local TAB filename completion (single-directory).
 *   • Configurable scrollback cap (default 2000 rows).
 *   • One-character-per-press Backspace; Alt/Ctrl variants send ESC-prefixed
 *     / Ctrl+W respectively.
 *   • Cursor blink, word/line select, drag auto-scroll, Ctrl+Wheel zoom.
 */
class TerminalView final : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit TerminalView(QWidget* parent = nullptr);

    void appendOutput(const QByteArray& bytes);
    void clearTerminal();
    [[nodiscard]] TerminalScreenModel& screen();
    [[nodiscard]] const TerminalScreenModel& screen() const;
    [[nodiscard]] QSize gridSize() const;
    [[nodiscard]] QString selectedText() const;

    // --- Public actions (also used by the context menu) ---------------------
public slots:
    void copySelection();
    void pasteClipboard();
    void selectAll();
    void clearScrollback();

public:
    // --- Configuration ------------------------------------------------------
    void setMaxScrollback(int rows);
    [[nodiscard]] int maxScrollback() const;
    void setAutocompleteEnabled(bool enabled);
    [[nodiscard]] bool isAutocompleteEnabled() const;

signals:
    void terminalInput(const QByteArray& bytes);
    void gridSizeChanged(const QSize& cells);
    void terminalTitleChanged(const QString& title);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    struct CellPoint final { int row = -1; int column = -1; };

    // --- Layout / coordinates ----------------------------------------------
    void recalculateGrid();
    void updateScrollBar(bool followTail);
    [[nodiscard]] int visibleRowCount() const;
    [[nodiscard]] int totalRowCount() const;      // scrollback + grid
    [[nodiscard]] int visualRowCount() const;     // capped at m_maxScrollback
    [[nodiscard]] int scrollbackSkip() const;     // rows trimmed off the front
    [[nodiscard]] const QVector<TerminalScreenModel::Cell>& rowAt(int actualRow) const;
    [[nodiscard]] const QVector<TerminalScreenModel::Cell>& visualRowAt(int visualRow) const;

    // --- Selection ----------------------------------------------------------
    void updateSelection(CellPoint point, bool extend);
    [[nodiscard]] CellPoint cellAt(const QPoint& point) const;
    void selectWordAt(CellPoint point);
    void selectLineAt(CellPoint point);
    [[nodiscard]] bool hasSelection() const;
    void clearSelection();
    [[nodiscard]] static bool isWordChar(QChar ch);

    // --- Input --------------------------------------------------------------
    [[nodiscard]] bool isTerminalShortcut(const QKeyEvent* event) const;
    [[nodiscard]] QByteArray encodeKey(const QKeyEvent* event) const;
    void handleTextInput(const QString& text);
    void handleBackspace(bool alt, bool ctrl);
    void handleTab();
    void handleEnter();
    [[nodiscard]] QStringList completeFilenamePrefix(const QString& prefix,
                                                     QString& outCompletion) const;

    // --- Rendering / scheduling ---------------------------------------------
    void scheduleViewportUpdate();
    void restartCursorBlink();
    void applyFontZoom(int delta);

    [[nodiscard]] static const QColor& defaultForeground();
    [[nodiscard]] static const QColor& defaultBackground();

    // --- Core model ---------------------------------------------------------
    TerminalScreenModel m_screen;
    VtStreamParser m_parser;
    QFont m_terminalFont;
    QSize m_cellSize;
    int m_scrollOffset = 0;
    int m_maxScrollback = 2000;
    bool m_autocompleteEnabled = true;

    // --- Selection state (row indices are ABSOLUTE: scrollback + grid space) -
    CellPoint m_selectionAnchor;
    CellPoint m_selectionExtent;
    bool m_selecting = false;
    bool m_hasFocus = false;

    // --- Timers -------------------------------------------------------------
    QTimer m_resizeDebounce;
    QTimer m_updateTimer;
    bool m_updatePending = false;

    QTimer m_cursorBlinkTimer;
    bool m_cursorOn = true;

    QTimer m_autoScrollTimer;
    int m_autoScrollDelta = 0;
    QPoint m_lastDragPos;

    QElapsedTimer m_clickTimer;
    QPoint m_lastClickPos;
    int m_clickCount = 0;

    int m_baseFontPixelSize = 15;

    // --- Local line buffer (used by TAB completion) ------------------------
    QString m_inputBuffer;
};