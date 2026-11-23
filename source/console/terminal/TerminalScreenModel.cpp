#include "TerminalScreenModel.h"

#include <QtGlobal>

namespace {

constexpr int kMinimumColumns = 2;
constexpr int kMinimumRows = 1;

} // namespace

TerminalScreenModel::TerminalScreenModel(const int columns, const int rows, const int scrollbackLimit)
    : m_columns(qMax(kMinimumColumns, columns))
    , m_rows(qMax(kMinimumRows, rows))
    , m_scrollbackLimit(qMax(0, scrollbackLimit))
    , m_scrollBottom(m_rows - 1)
{
    reset();
}

void TerminalScreenModel::resize(const int columns, const int rows)
{
    const int newColumns = qMax(kMinimumColumns, columns);
    const int newRows = qMax(kMinimumRows, rows);
    if (newColumns == m_columns && newRows == m_rows) {
        return;
    }

    m_columns = newColumns;
    m_rows = newRows;
    const auto reshape = [this](QVector<QVector<Cell>>& target) {
        while (target.size() > m_rows) {
            const QVector<Cell> removed = target.takeFirst();
            if (m_hasContent) {
                appendScrollback(removed);
            }
        }
        while (target.size() < m_rows) {
            target.append(blankRow());
        }
        for (QVector<Cell>& row : target) {
            row.resize(m_columns);
            for (Cell& cell : row) {
                if (cell.text.isEmpty()) {
                    cell.text = QStringLiteral(" ");
                }
            }
        }
    };
    reshape(m_grid);
    if (!m_primaryGrid.isEmpty()) {
        reshape(m_primaryGrid);
    }
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    clampCursor();
}

void TerminalScreenModel::reset()
{
    m_grid.clear();
    ensureGrid();
    m_scrollback.clear();
    m_primaryGrid.clear();
    m_cursor = {};
    m_savedCursor = {};
    m_primaryCursor = {};
    m_attributes = {};
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_title.clear();
    m_usingAlternateScreen = false;
    m_hasContent = false;
}

void TerminalScreenModel::clearAll()
{
    for (QVector<Cell>& row : m_grid) {
        row = blankRow();
    }
    m_cursor = {};
}

void TerminalScreenModel::eraseDisplay(const int mode)
{
    ensureGrid();
    if (mode == 2 || mode == 3) {
        clearAll();
        if (mode == 3) {
            m_scrollback.clear();
        }
        return;
    }
    if (mode == 0) {
        eraseLine(0);
        for (int row = m_cursor.row + 1; row < m_rows; ++row) {
            m_grid[row] = blankRow();
        }
    } else if (mode == 1) {
        eraseLine(1);
        for (int row = 0; row < m_cursor.row; ++row) {
            m_grid[row] = blankRow();
        }
    }
}

void TerminalScreenModel::eraseLine(const int mode)
{
    ensureGrid();
    QVector<Cell>& row = m_grid[m_cursor.row];
    const int first = mode == 1 ? 0 : m_cursor.column;
    const int last = mode == 0 ? m_columns - 1 : m_columns - 1;
    if (mode == 2) {
        row = blankRow();
        return;
    }
    for (int column = qBound(0, first, m_columns - 1); column <= last; ++column) {
        row[column] = Cell{};
    }
}

void TerminalScreenModel::insertLines(int count)
{
    count = qBound(1, count, m_scrollBottom - m_cursor.row + 1);
    if (m_cursor.row < m_scrollTop || m_cursor.row > m_scrollBottom) {
        return;
    }
    while (count-- > 0) {
        m_grid.insert(m_cursor.row, blankRow());
        m_grid.removeAt(m_scrollBottom + 1);
    }
}

void TerminalScreenModel::deleteLines(int count)
{
    count = qBound(1, count, m_scrollBottom - m_cursor.row + 1);
    if (m_cursor.row < m_scrollTop || m_cursor.row > m_scrollBottom) {
        return;
    }
    while (count-- > 0) {
        m_grid.removeAt(m_cursor.row);
        m_grid.insert(m_scrollBottom, blankRow());
    }
}

void TerminalScreenModel::deleteCharacters(int count)
{
    QVector<Cell>& row = m_grid[m_cursor.row];
    count = qBound(1, count, m_columns - m_cursor.column);
    while (count-- > 0) {
        row.removeAt(m_cursor.column);
        row.append(Cell{});
    }
}

void TerminalScreenModel::eraseCharacters(int count)
{
    count = qBound(1, count, m_columns - m_cursor.column);
    for (int column = m_cursor.column; column < m_cursor.column + count; ++column) {
        m_grid[m_cursor.row][column] = Cell{};
    }
}

void TerminalScreenModel::put(const QString& text)
{
    if (!text.isEmpty()) {
        m_hasContent = true;
    }
    for (const QChar character : text) {
        if (m_cursor.column >= m_columns) {
            carriageReturn();
            lineFeed();
        }
        Cell& cell = m_grid[m_cursor.row][m_cursor.column];
        cell.text = QString(character);
        cell.attributes = m_attributes;
        ++m_cursor.column;
    }
}

void TerminalScreenModel::carriageReturn()
{
    m_cursor.column = 0;
}

void TerminalScreenModel::lineFeed()
{
    m_hasContent = true;
    if (m_cursor.row == m_scrollBottom) {
        scrollRegionUp(1);
    } else {
        ++m_cursor.row;
    }
}

void TerminalScreenModel::backspace()
{
    m_cursor.column = qMax(0, m_cursor.column - 1);
}

void TerminalScreenModel::tab()
{
    m_cursor.column = qMin(m_columns - 1, ((m_cursor.column / 8) + 1) * 8);
}

void TerminalScreenModel::moveCursor(const int row, const int column)
{
    m_cursor.row = qBound(0, row, m_rows - 1);
    m_cursor.column = qBound(0, column, m_columns - 1);
}

void TerminalScreenModel::moveCursorRelative(const int rowDelta, const int columnDelta)
{
    moveCursor(m_cursor.row + rowDelta, m_cursor.column + columnDelta);
}

void TerminalScreenModel::saveCursor()
{
    m_savedCursor = m_cursor;
}

void TerminalScreenModel::restoreCursor()
{
    m_cursor = m_savedCursor;
    clampCursor();
}

void TerminalScreenModel::setScrollRegion(int top, int bottom)
{
    top = qBound(0, top, m_rows - 1);
    bottom = qBound(0, bottom, m_rows - 1);
    if (top >= bottom) {
        top = 0;
        bottom = m_rows - 1;
    }
    m_scrollTop = top;
    m_scrollBottom = bottom;
    moveCursor(0, 0);
}

void TerminalScreenModel::scrollUp(const int count)
{
    scrollRegionUp(qMax(1, count));
}

void TerminalScreenModel::scrollDown(const int count)
{
    scrollRegionDown(qMax(1, count));
}

void TerminalScreenModel::setCursorVisible(const bool visible)
{
    m_cursor.visible = visible;
}

void TerminalScreenModel::setAttributes(const Attributes& attributes)
{
    m_attributes = attributes;
}

void TerminalScreenModel::setTitle(QString title)
{
    m_title = std::move(title);
}

void TerminalScreenModel::setAlternateScreen(const bool enabled)
{
    if (enabled == m_usingAlternateScreen) {
        return;
    }
    if (enabled) {
        m_primaryGrid = m_grid;
        m_primaryCursor = m_cursor;
        m_grid.clear();
        ensureGrid();
        m_cursor = {};
    } else {
        if (!m_primaryGrid.isEmpty()) {
            m_grid = m_primaryGrid;
            m_cursor = m_primaryCursor;
        }
        m_primaryGrid.clear();
        clampCursor();
    }
    m_usingAlternateScreen = enabled;
}

int TerminalScreenModel::columns() const { return m_columns; }
int TerminalScreenModel::rows() const { return m_rows; }
int TerminalScreenModel::scrollbackLimit() const { return m_scrollbackLimit; }
TerminalScreenModel::Cursor TerminalScreenModel::cursor() const { return m_cursor; }
QString TerminalScreenModel::title() const { return m_title; }
TerminalScreenModel::Attributes TerminalScreenModel::attributes() const { return m_attributes; }
const QVector<QVector<TerminalScreenModel::Cell>>& TerminalScreenModel::grid() const { return m_grid; }
const QVector<QVector<TerminalScreenModel::Cell>>& TerminalScreenModel::scrollback() const { return m_scrollback; }
bool TerminalScreenModel::usingAlternateScreen() const { return m_usingAlternateScreen; }

QString TerminalScreenModel::text() const
{
    QStringList lines;
    lines.reserve(m_scrollback.size() + m_grid.size());
    const auto appendRows = [&lines](const QVector<QVector<Cell>>& rows) {
        for (const QVector<Cell>& row : rows) {
            QString line;
            for (const Cell& cell : row) {
                line += cell.text;
            }
            lines.append(line);
        }
    };
    appendRows(m_scrollback);
    appendRows(m_grid);
    return lines.join(QLatin1Char('\n'));
}

QVector<TerminalScreenModel::Cell> TerminalScreenModel::blankRow() const
{
    return QVector<Cell>(m_columns, Cell{});
}

void TerminalScreenModel::clampCursor()
{
    m_cursor.row = qBound(0, m_cursor.row, m_rows - 1);
    m_cursor.column = qBound(0, m_cursor.column, m_columns - 1);
}

void TerminalScreenModel::appendScrollback(const QVector<Cell>& row)
{
    if (m_usingAlternateScreen || m_scrollbackLimit <= 0) {
        return;
    }
    m_scrollback.append(row);
    while (m_scrollback.size() > m_scrollbackLimit) {
        m_scrollback.removeFirst();
    }
}

void TerminalScreenModel::ensureGrid()
{
    while (m_grid.size() < m_rows) {
        m_grid.append(blankRow());
    }
    while (m_grid.size() > m_rows) {
        appendScrollback(m_grid.takeFirst());
    }
}

void TerminalScreenModel::scrollRegionUp(int count)
{
    count = qBound(1, count, m_scrollBottom - m_scrollTop + 1);
    while (count-- > 0) {
        const QVector<Cell> removed = m_grid.takeAt(m_scrollTop);
        if (m_scrollTop == 0) {
            appendScrollback(removed);
        }
        m_grid.insert(m_scrollBottom, blankRow());
    }
}

void TerminalScreenModel::scrollRegionDown(int count)
{
    count = qBound(1, count, m_scrollBottom - m_scrollTop + 1);
    while (count-- > 0) {
        m_grid.removeAt(m_scrollBottom);
        m_grid.insert(m_scrollTop, blankRow());
    }
}
