#include "VtStreamParser.h"

#include <QRegularExpression>

namespace {

QColor ansiColor(const int index, const bool bright)
{
    static const QColor normal[] = {
        QColor(QStringLiteral("#0b1018")), QColor(QStringLiteral("#d75f5f")),
        QColor(QStringLiteral("#5faf5f")), QColor(QStringLiteral("#d7af5f")),
        QColor(QStringLiteral("#5f87d7")), QColor(QStringLiteral("#af5fd7")),
        QColor(QStringLiteral("#5fd7d7")), QColor(QStringLiteral("#d7d7d7"))};
    static const QColor intense[] = {
        QColor(QStringLiteral("#5f5f5f")), QColor(QStringLiteral("#ff8787")),
        QColor(QStringLiteral("#87d787")), QColor(QStringLiteral("#ffd787")),
        QColor(QStringLiteral("#87afff")), QColor(QStringLiteral("#d787ff")),
        QColor(QStringLiteral("#87ffff")), QColor(QStringLiteral("#ffffff"))};
    return (bright ? intense : normal)[qBound(0, index, 7)];
}

} // namespace

VtStreamParser::VtStreamParser(TerminalScreenModel& screen)
    : m_screen(screen)
{
}

void VtStreamParser::feed(const QByteArray& bytes)
{
    feedText(m_decoder.decode(bytes));
}

void VtStreamParser::feedText(const QString& text)
{
    for (const QChar character : text) {
        consume(character);
    }
}

void VtStreamParser::reset()
{
    m_decoder.resetState();
    m_state = State::Ground;
    m_sequence.clear();
    m_osc.clear();
    m_ignoredSequenceCount = 0;
}

int VtStreamParser::ignoredSequenceCount() const
{
    return m_ignoredSequenceCount;
}

void VtStreamParser::consume(const QChar character)
{
    switch (m_state) {
    case State::Ground:
        if (character == QChar(0x1b)) {
            m_state = State::Escape;
        } else if (character == QLatin1Char('\r')) {
            m_screen.carriageReturn();
        } else if (character == QLatin1Char('\n')) {
            m_screen.lineFeed();
        } else if (character == QLatin1Char('\b')) {
            m_screen.backspace();
        } else if (character == QLatin1Char('\t')) {
            m_screen.tab();
        } else if (!character.isNull() && character.unicode() >= 0x20 && character != QChar(0x7f)) {
            m_screen.put(QString(character));
        }
        return;

    case State::Escape:
        if (character == QLatin1Char('[')) {
            m_sequence.clear();
            m_state = State::Csi;
        } else if (character == QLatin1Char(']')) {
            m_osc.clear();
            m_state = State::Osc;
        } else if (character == QLatin1Char('7')) {
            m_screen.saveCursor();
            m_state = State::Ground;
        } else if (character == QLatin1Char('8')) {
            m_screen.restoreCursor();
            m_state = State::Ground;
        } else if (character == QLatin1Char('D')) {
            m_screen.lineFeed();
            m_state = State::Ground;
        } else if (character == QLatin1Char('M')) {
            m_screen.scrollDown();
            m_state = State::Ground;
        } else if (character == QLatin1Char('c')) {
            m_screen.reset();
            m_state = State::Ground;
        } else {
            ignoreSequence();
            m_state = State::Ground;
        }
        return;

    case State::Csi:
        if (character.unicode() >= 0x40 && character.unicode() <= 0x7e) {
            executeCsi(character);
            m_state = State::Ground;
        } else if (character.unicode() >= 0x20 && character.unicode() <= 0x3f) {
            m_sequence.append(character);
        } else {
            ignoreSequence();
            m_state = State::Ground;
        }
        return;

    case State::Osc:
        if (character == QChar(0x07)) {
            executeOsc();
            m_state = State::Ground;
        } else if (character == QChar(0x1b)) {
            m_state = State::OscEscape;
        } else {
            m_osc.append(character);
        }
        return;

    case State::OscEscape:
        if (character == QLatin1Char('\\')) {
            executeOsc();
        } else {
            m_osc.append(QChar(0x1b));
            m_osc.append(character);
        }
        m_state = State::Ground;
        return;
    }
}

void VtStreamParser::executeCsi(const QChar final)
{
    const QList<int> values = parameters();
    const int first = parameterOrDefault(0, 1);
    const bool privateMode = m_sequence.startsWith(QLatin1Char('?'));

    switch (final.unicode()) {
    case 'A': m_screen.moveCursorRelative(-first, 0); break;
    case 'B': m_screen.moveCursorRelative(first, 0); break;
    case 'C': m_screen.moveCursorRelative(0, first); break;
    case 'D': m_screen.moveCursorRelative(0, -first); break;
    case 'E': m_screen.moveCursorRelative(first, 0); m_screen.carriageReturn(); break;
    case 'F': m_screen.moveCursorRelative(-first, 0); m_screen.carriageReturn(); break;
    case 'G': m_screen.moveCursor(m_screen.cursor().row, parameterOrDefault(0, 1) - 1); break;
    case 'H':
    case 'f':
        m_screen.moveCursor(parameterOrDefault(0, 1) - 1, parameterOrDefault(1, 1) - 1);
        break;
    case 'J': m_screen.eraseDisplay(parameterOrDefault(0, 0)); break;
    case 'K': m_screen.eraseLine(parameterOrDefault(0, 0)); break;
    case 'L': m_screen.insertLines(first); break;
    case 'M': m_screen.deleteLines(first); break;
    case 'P': m_screen.deleteCharacters(first); break;
    case 'S': m_screen.scrollUp(first); break;
    case 'T': m_screen.scrollDown(first); break;
    case 'X': m_screen.eraseCharacters(first); break;
    case 'm': applySgr(values); break;
    case 'r': m_screen.setScrollRegion(parameterOrDefault(0, 1) - 1,
                                        parameterOrDefault(1, m_screen.rows()) - 1); break;
    case 's': m_screen.saveCursor(); break;
    case 'u': m_screen.restoreCursor(); break;
    case 'h':
    case 'l':
        if (privateMode) {
            const bool enabled = final == QLatin1Char('h');
            for (const int value : values) {
                if (value == 25) {
                    m_screen.setCursorVisible(enabled);
                } else if (value == 1049 || value == 47 || value == 1047) {
                    m_screen.setAlternateScreen(enabled);
                }
            }
        }
        break;
    default:
        ignoreSequence();
        break;
    }
}

void VtStreamParser::executeOsc()
{
    const int separator = m_osc.indexOf(QLatin1Char(';'));
    if (separator < 0) {
        return;
    }
    const QString code = m_osc.left(separator);
    if (code == QStringLiteral("0") || code == QStringLiteral("2")) {
        m_screen.setTitle(m_osc.mid(separator + 1));
    }
}

void VtStreamParser::applySgr(const QList<int>& values)
{
    TerminalScreenModel::Attributes attributes = m_screen.attributes();
    const QList<int> parameters = values.isEmpty() ? QList<int>{0} : values;
    for (int index = 0; index < parameters.size(); ++index) {
        const int value = parameters.at(index);
        if (value == 0) {
            attributes = {};
        } else if (value == 1) {
            attributes.bold = true;
        } else if (value == 22) {
            attributes.bold = false;
        } else if (value == 4) {
            attributes.underline = true;
        } else if (value == 24) {
            attributes.underline = false;
        } else if (value == 7) {
            attributes.inverse = true;
        } else if (value == 27) {
            attributes.inverse = false;
        } else if (value >= 30 && value <= 37) {
            attributes.foreground = ansiColor(value - 30, false);
        } else if (value >= 90 && value <= 97) {
            attributes.foreground = ansiColor(value - 90, true);
        } else if (value >= 40 && value <= 47) {
            attributes.background = ansiColor(value - 40, false);
        } else if (value >= 100 && value <= 107) {
            attributes.background = ansiColor(value - 100, true);
        } else if (value == 39) {
            attributes.foreground = {};
        } else if (value == 49) {
            attributes.background = {};
        } else if ((value == 38 || value == 48) && index + 2 < parameters.size()
                   && parameters.at(index + 1) == 5) {
            const int color = qBound(0, parameters.at(index + 2), 255);
            const QColor mapped = color < 16 ? ansiColor(color % 8, color >= 8)
                                              : QColor::fromHsv((color * 47) % 360, 150, 220);
            if (value == 38) {
                attributes.foreground = mapped;
            } else {
                attributes.background = mapped;
            }
            index += 2;
        } else if ((value == 38 || value == 48) && index + 4 < parameters.size()
                   && parameters.at(index + 1) == 2) {
            const QColor color(qBound(0, parameters.at(index + 2), 255),
                                qBound(0, parameters.at(index + 3), 255),
                                qBound(0, parameters.at(index + 4), 255));
            if (value == 38) {
                attributes.foreground = color;
            } else {
                attributes.background = color;
            }
            index += 4;
        }
    }
    m_screen.setAttributes(attributes);
}

QList<int> VtStreamParser::parameters() const
{
    QString payload = m_sequence;
    payload.remove(QLatin1Char('?'));
    if (payload.isEmpty()) {
        return {};
    }
    QList<int> values;
    const QStringList parts = payload.split(QLatin1Char(';'));
    for (const QString& part : parts) {
        values.append(part.isEmpty() ? 0 : part.toInt());
    }
    return values;
}

int VtStreamParser::parameterOrDefault(const int index, const int defaultValue) const
{
    const QList<int> values = parameters();
    if (index >= values.size() || values.at(index) == 0) {
        return defaultValue;
    }
    return values.at(index);
}

void VtStreamParser::ignoreSequence()
{
    ++m_ignoredSequenceCount;
}
