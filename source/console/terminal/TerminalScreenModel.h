#pragma once

#include <QColor>
#include <QVector>
#include <QString>

/**
 * Pure terminal grid state. It deliberately owns no QWidget or native process
 * handles so VT parsing and painting remain independently testable.
 */
class TerminalScreenModel final {
public:
    struct Attributes final {
        QColor foreground;
        QColor background;
        bool bold = false;
        bool underline = false;
        bool inverse = false;
    };

    struct Cell final {
        QString text = QStringLiteral(" ");
        Attributes attributes;
    };

    struct Cursor final {
        int row = 0;
        int column = 0;
        bool visible = true;
    };

    explicit TerminalScreenModel(int columns = 80, int rows = 24, int scrollbackLimit = 4000);

    void resize(int columns, int rows);
    void reset();
    void clearAll();
    void eraseDisplay(int mode);
    void eraseLine(int mode);
    void insertLines(int count);
    void deleteLines(int count);
    void deleteCharacters(int count);
    void eraseCharacters(int count);

    void put(const QString& text);
    void carriageReturn();
    void lineFeed();
    void backspace();
    void tab();
    void moveCursor(int row, int column);
    void moveCursorRelative(int rowDelta, int columnDelta);
    void saveCursor();
    void restoreCursor();
    void setScrollRegion(int top, int bottom);
    void scrollUp(int count = 1);
    void scrollDown(int count = 1);
    void setCursorVisible(bool visible);
    void setAttributes(const Attributes& attributes);
    void setTitle(QString title);
    void setAlternateScreen(bool enabled);

    [[nodiscard]] int columns() const;
    [[nodiscard]] int rows() const;
    [[nodiscard]] int scrollbackLimit() const;
    [[nodiscard]] Cursor cursor() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] Attributes attributes() const;
    [[nodiscard]] const QVector<QVector<Cell>>& grid() const;
    [[nodiscard]] const QVector<QVector<Cell>>& scrollback() const;
    [[nodiscard]] bool usingAlternateScreen() const;
    [[nodiscard]] QString text() const;

private:
    [[nodiscard]] QVector<Cell> blankRow() const;
    void clampCursor();
    void appendScrollback(const QVector<Cell>& row);
    void ensureGrid();
    void scrollRegionUp(int count);
    void scrollRegionDown(int count);

    int m_columns = 80;
    int m_rows = 24;
    int m_scrollbackLimit = 4000;
    QVector<QVector<Cell>> m_grid;
    QVector<QVector<Cell>> m_scrollback;
    QVector<QVector<Cell>> m_primaryGrid;
    Cursor m_cursor;
    Cursor m_savedCursor;
    Cursor m_primaryCursor;
    Attributes m_attributes;
    int m_scrollTop = 0;
    int m_scrollBottom = 23;
    QString m_title;
    bool m_usingAlternateScreen = false;
    bool m_hasContent = false;
};
