#include "AlifLexer.h"

#include <QtCore/QHash>

namespace {

class Scanner final {
public:
    explicit Scanner(const QString& source)
        : m_source(source) {
        m_indentStack.append(0);
    }

    [[nodiscard]] LexResult scan() {
        while (!atEnd()) {
            if (m_atLineStart && m_groupingDepth == 0 && scanLineStart()) {
                continue;
            }
            scanToken();
        }

        while (m_indentStack.size() > 1) {
            m_indentStack.removeLast();
            emitZero(TokenKind::Dedent);
        }
        emitZero(TokenKind::EndOfFile);
        return m_result;
    }

private:
    struct Operator final {
        const char* spelling;
        TokenKind kind;
    };

    const QString& m_source;
    LexResult m_result;
    qsizetype m_pos = 0;
    qsizetype m_line = 1;
    qsizetype m_column = 1;
    bool m_atLineStart = true;
    int m_groupingDepth = 0;
    QVector<int> m_indentStack;

    [[nodiscard]] bool atEnd() const {
        return m_pos >= m_source.size();
    }

    [[nodiscard]] QChar peek(const qsizetype lookahead = 0) const {
        const qsizetype index = m_pos + lookahead;
        return index < m_source.size() ? m_source.at(index) : QChar();
    }

    [[nodiscard]] SourceLocation location() const {
        return {m_pos, m_line, m_column};
    }

    [[nodiscard]] bool startsWith(const QString& text) const {
        return m_source.mid(m_pos, text.size()) == text;
    }

    [[nodiscard]] static bool isNewline(const QChar character) {
        return character == QChar(u'\n') || character == QChar(u'\r');
    }

    [[nodiscard]] static bool isHorizontalWhitespace(const QChar character) {
        return character == QChar(u' ') || character == QChar(u'\t')
            || character == QChar(u'\f') || character == QChar(u'\v');
    }

    [[nodiscard]] static bool isIdentifierStart(const QChar character) {
        return character == QChar(u'_') || character.isLetter();
    }

    [[nodiscard]] static bool isIdentifierContinue(const QChar character) {
        return character == QChar(u'_') || character.isLetterOrNumber();
    }

    void advance() {
        if (atEnd()) {
            return;
        }

        const QChar character = m_source.at(m_pos++);
        if (character == QChar(u'\r')) {
            if (!atEnd() && peek() == QChar(u'\n')) {
                ++m_pos;
            }
            ++m_line;
            m_column = 1;
            m_atLineStart = true;
            return;
        }
        if (character == QChar(u'\n')) {
            ++m_line;
            m_column = 1;
            m_atLineStart = true;
            return;
        }
        ++m_column;
    }

    bool match(const QChar expected) {
        if (peek() != expected) {
            return false;
        }
        advance();
        return true;
    }

    void appendToken(const TokenKind kind, const TokenChannel channel,
              const qsizetype startOffset, const SourceLocation& startLocation) {
        Token token;
        token.kind = kind;
        token.channel = channel;
        token.range = {startLocation, location()};
        token.lexeme = m_source.mid(startOffset, m_pos - startOffset);
        m_result.tokens.append(std::move(token));
    }

    void emitZero(const TokenKind kind) {
        Token token;
        token.kind = kind;
        token.channel = TokenChannel::Main;
        token.range = {location(), location()};
        m_result.tokens.append(std::move(token));
    }

    void diagnostic(const QString& code, const QString& message,
                    const SourceLocation& start) {
        LexDiagnostic issue;
        issue.code = code;
        issue.message = message;
        issue.range = {start, location()};
        m_result.diagnostics.append(std::move(issue));
    }

    void consumeNewline() {
        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();
        advance();
        if (m_groupingDepth == 0) {
            appendToken(TokenKind::Newline, TokenChannel::Main, start, startLocation);
        }
    }

    bool scanLineStart() {
        const qsizetype whitespaceStart = m_pos;
        const SourceLocation whitespaceLocation = location();
        int indentationWidth = 0;

        while (isHorizontalWhitespace(peek())) {
            if (peek() == QChar(u'\t')) {
                indentationWidth = ((indentationWidth / 4) + 1) * 4;
            } else {
                ++indentationWidth;
            }
            advance();
        }
        if (m_pos > whitespaceStart) {
            appendToken(TokenKind::Whitespace, TokenChannel::Trivia,
                 whitespaceStart, whitespaceLocation);
        }

        if (atEnd()) {
            return true;
        }
        if (isNewline(peek())) {
            consumeNewline();
            return true;
        }
        if (peek() == QChar(u'#')) {
            scanComment();
            if (!atEnd() && isNewline(peek())) {
                consumeNewline();
            }
            return true;
        }

        const int currentIndent = m_indentStack.constLast();
        if (indentationWidth > currentIndent) {
            m_indentStack.append(indentationWidth);
            emitZero(TokenKind::Indent);
        } else if (indentationWidth < currentIndent) {
            while (m_indentStack.size() > 1
                   && indentationWidth < m_indentStack.constLast()) {
                m_indentStack.removeLast();
                emitZero(TokenKind::Dedent);
            }
            if (indentationWidth != m_indentStack.constLast()) {
                const SourceLocation start = location();
                diagnostic(QStringLiteral("نسق004"),
                           QStringLiteral("مسافة بادئة غير صحيحة"), start);
                m_indentStack.append(indentationWidth);
                emitZero(TokenKind::Indent);
            }
        }

        m_atLineStart = false;
        return false;
    }

    void scanWhitespace() {
        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();
        while (isHorizontalWhitespace(peek())) {
            advance();
        }
        appendToken(TokenKind::Whitespace, TokenChannel::Trivia, start, startLocation);
    }

    void scanComment() {
        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();
        while (!atEnd() && !isNewline(peek())) {
            advance();
        }
        appendToken(TokenKind::Comment, TokenChannel::Trivia, start, startLocation);
    }

    [[nodiscard]] static const QHash<QString, TokenKind>& keywords() {
        static const QHash<QString, TokenKind> table = {
            {QStringLiteral("دالة"), TokenKind::KwFunction},
            {QStringLiteral("صنف"), TokenKind::KwClass},
            {QStringLiteral("اذا"), TokenKind::KwIf},
            {QStringLiteral("إذا"), TokenKind::KwIf},
            {QStringLiteral("اواذا"), TokenKind::KwElseIf},
            {QStringLiteral("أوإذا"), TokenKind::KwElseIf},
            {QStringLiteral("والا"), TokenKind::KwElse},
            {QStringLiteral("وإلا"), TokenKind::KwElse},
            {QStringLiteral("لكل"), TokenKind::KwFor},
            {QStringLiteral("في"), TokenKind::KwIn},
            {QStringLiteral("اذن"), TokenKind::KwThen},
            {QStringLiteral("بينما"), TokenKind::KwWhile},
            {QStringLiteral("حاول"), TokenKind::KwTry},
            {QStringLiteral("خلل"), TokenKind::KwExcept},
            {QStringLiteral("نهاية"), TokenKind::KwFinally},
            {QStringLiteral("ارجع"), TokenKind::KwReturn},
            {QStringLiteral("استورد"), TokenKind::KwImport},
            {QStringLiteral("انتج"), TokenKind::KwYield},
            {QStringLiteral("ك"), TokenKind::KwAs},
            {QStringLiteral("هو"), TokenKind::KwIs},
            {QStringLiteral("هي"), TokenKind::KwIs},
            {QStringLiteral("من"), TokenKind::KwFrom},
            {QStringLiteral("احذف"), TokenKind::KwDelete},
            {QStringLiteral("توقف"), TokenKind::KwBreak},
            {QStringLiteral("استمر"), TokenKind::KwContinue},
            {QStringLiteral("و"), TokenKind::KwAnd},
            {QStringLiteral("او"), TokenKind::KwOr},
            {QStringLiteral("أو"), TokenKind::KwOr},
            {QStringLiteral("ليس"), TokenKind::KwNot},
            {QStringLiteral("خطية"), TokenKind::KwLambda},
            {QStringLiteral("صح"), TokenKind::KwTrue},
            {QStringLiteral("خطأ"), TokenKind::KwFalse},
            {QStringLiteral("خطا"), TokenKind::KwFalse},
            {QStringLiteral("عدم"), TokenKind::KwNull},
            {QStringLiteral("مرر"), TokenKind::KwPass},
            {QStringLiteral("عند"), TokenKind::KwWith},
            {QStringLiteral("عام"), TokenKind::KwGlobal},
        };
        return table;
    }

    void scanIdentifier() {
        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();
        while (isIdentifierContinue(peek())) {
            advance();
        }
        const QString spelling = m_source.mid(start, m_pos - start);
        const auto iterator = keywords().constFind(spelling);
        const TokenKind kind = iterator == keywords().constEnd()
            ? TokenKind::Identifier : iterator.value();
        appendToken(kind, TokenChannel::Main, start, startLocation);
    }

    void scanNumber() {
        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();

        if (peek() == QChar(u'0')) {
            const QChar prefix = peek(1);
            int base = 0;
            if (prefix == QChar(u'b') || prefix == QChar(u'B') || prefix == QChar(u'ث')) {
                base = 2;
            } else if (prefix == QChar(u'o') || prefix == QChar(u'O') || prefix == QChar(u'م')) {
                base = 8;
            } else if (prefix == QChar(u'x') || prefix == QChar(u'X') || prefix == QChar(u'س')) {
                base = 16;
            }
            if (base != 0) {
                advance();
                advance();
                const qsizetype digitsStart = m_pos;
                const auto digitValue = [base](const QChar digit) {
                    if (digit >= QChar(u'0') && digit <= QChar(u'9')) {
                        return digit.unicode() - QChar(u'0').unicode();
                    }
                    if (base == 16) {
                        const QChar lower = digit.toLower();
                        if (lower >= QChar(u'a') && lower <= QChar(u'f')) {
                            return 10 + lower.unicode() - QChar(u'a').unicode();
                        }
                        switch (digit.unicode()) {
                        case 0x0627: // ا: hexadecimal 10
                        case 0x0623: // أ
                            return 10;
                        case 0x0628: return 11; // ب
                        case 0x062A: return 12; // ت
                        case 0x062B: return 13; // ث
                        case 0x062C: return 14; // ج
                        case 0x062D: return 15; // ح
                        default: break;
                        }
                    }
                    return -1;
                };
                bool invalidDigit = false;
                while (digitValue(peek()) >= 0 && digitValue(peek()) < base) {
                    advance();
                }
                if (isIdentifierContinue(peek())) {
                    invalidDigit = true;
                    while (isIdentifierContinue(peek())) {
                        advance();
                    }
                }
                if (m_pos == digitsStart || invalidDigit) {
                    diagnostic(QStringLiteral("نسق005"),
                               QStringLiteral("رقم صحيح غير صالح للأساس %1").arg(base),
                               startLocation);
                    appendToken(TokenKind::Invalid, TokenChannel::Main, start, startLocation);
                } else {
                    appendToken(TokenKind::IntegerLiteral, TokenChannel::Main, start, startLocation);
                }
                return;
            }
        }

        while (peek().isDigit()) {
            advance();
        }

        bool isFloat = false;
        if (peek() == QChar(u'.') && peek(1).isDigit()) {
            isFloat = true;
            advance();
            while (peek().isDigit()) {
                advance();
            }
        }
        if (peek() == QChar(u'e') || peek() == QChar(u'E')) {
            const qsizetype exponentStart = m_pos;
            advance();
            if (peek() == QChar(u'+') || peek() == QChar(u'-')) {
                advance();
            }
            const qsizetype digitsStart = m_pos;
            while (peek().isDigit()) {
                advance();
            }
            if (m_pos == digitsStart) {
                m_pos = exponentStart;
                m_column -= (digitsStart - exponentStart);
            } else {
                isFloat = true;
            }
        }
        appendToken(isFloat ? TokenKind::FloatLiteral : TokenKind::IntegerLiteral,
             TokenChannel::Main, start, startLocation);
    }

    [[nodiscard]] bool isTripleDelimiter(const QChar quote) const {
        return peek() == quote && peek(1) == quote && peek(2) == quote;
    }

    void scanString(const TokenKind kind = TokenKind::StringLiteral,
                    const bool hasPrefix = false) {
        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();
        if (hasPrefix) {
            advance();
        }
        const QChar quote = peek();
        const bool triple = isTripleDelimiter(quote);
        const QString delimiter = triple ? QString(3, quote) : QString(1, quote);

        for (int index = 0; index < delimiter.size(); ++index) {
            advance();
        }

        while (!atEnd()) {
            if (startsWith(delimiter)) {
                for (int index = 0; index < delimiter.size(); ++index) {
                    advance();
                }
                appendToken(kind, TokenChannel::Main, start, startLocation);
                return;
            }
            if (!triple && isNewline(peek())) {
                diagnostic(QStringLiteral("نسق002"),
                           QStringLiteral("نص أحادي السطر غير منتهي"), startLocation);
                appendToken(TokenKind::Invalid, TokenChannel::Main, start, startLocation);
                return;
            }
            if (peek() == QChar(u'\\')) {
                advance();
                if (!atEnd() && !isNewline(peek())) {
                    advance();
                }
                continue;
            }
            advance();
        }

        diagnostic(QStringLiteral("نسق003"),
                   QStringLiteral("نص متعدد الأسطر غير منتهي"), startLocation);
        appendToken(TokenKind::Invalid, TokenChannel::Main, start, startLocation);
    }

    void scanDecorator() {
        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();
        advance(); // '@'

        // Keep the decorator marker and its qualified name as one lexical
        // token, matching the legacy highlighter and allowing Arabic names.
        while (isIdentifierContinue(peek()) || peek() == QChar(u'.')) {
            advance();
        }
        appendToken(TokenKind::Decorator, TokenChannel::Main, start, startLocation);
    }

    void scanFStringText(const qsizetype start, const SourceLocation& startLocation) {
        if (m_pos > start) {
            appendToken(TokenKind::FStringText, TokenChannel::Main, start, startLocation);
        }
    }

    bool scanFStringFormat() {
        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();
        int nestedBraceDepth = 0;

        while (!atEnd()) {
            if (peek() == QChar(u'{')) {
                ++nestedBraceDepth;
                advance();
                continue;
            }
            if (peek() == QChar(u'}')) {
                if (nestedBraceDepth > 0) {
                    --nestedBraceDepth;
                    advance();
                    continue;
                }
                if (m_pos > start) {
                    appendToken(TokenKind::FStringFormat, TokenChannel::Main,
                         start, startLocation);
                }
                const qsizetype closingStart = m_pos;
                const SourceLocation closingLocation = location();
                advance();
                appendToken(TokenKind::InterpolationEnd, TokenChannel::Main,
                     closingStart, closingLocation);
                return true;
            }
            advance();
        }

        diagnostic(QStringLiteral("نسق006"),
                   QStringLiteral("نص منسق غير مغلق"), startLocation);
        emitZero(TokenKind::Invalid);
        return false;
    }

    bool scanInterpolation() {
        int braceDepth = 1;
        while (!atEnd()) {
            if (braceDepth == 1 && peek() == QChar(u':')) {
                const qsizetype start = m_pos;
                const SourceLocation startLocation = location();
                advance();
                appendToken(TokenKind::Colon, TokenChannel::Main, start, startLocation);
                return scanFStringFormat();
            }
            if (peek() == QChar(u'}')) {
                const qsizetype start = m_pos;
                const SourceLocation startLocation = location();
                advance();
                if (braceDepth == 1) {
                    appendToken(TokenKind::InterpolationEnd, TokenChannel::Main,
                         start, startLocation);
                    return true;
                }
                --braceDepth;
                if (m_groupingDepth > 0) {
                    --m_groupingDepth;
                }
                appendToken(TokenKind::RBrace, TokenChannel::Main, start, startLocation);
                continue;
            }
            if (peek() == QChar(u'{')) {
                const qsizetype start = m_pos;
                const SourceLocation startLocation = location();
                advance();
                ++braceDepth;
                ++m_groupingDepth;
                appendToken(TokenKind::LBrace, TokenChannel::Main, start, startLocation);
                continue;
            }
            scanToken();
        }

        diagnostic(QStringLiteral("نسق006"),
                   QStringLiteral("نص منسق غير منتهي"), location());
        emitZero(TokenKind::Invalid);
        return false;
    }

    void scanFString() {
        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();
        advance(); // Arabic formatted-string prefix: م
        const QChar quote = peek();
        const bool triple = isTripleDelimiter(quote);
        const QString delimiter = triple ? QString(3, quote) : QString(1, quote);
        for (int index = 0; index < delimiter.size(); ++index) {
            advance();
        }
        appendToken(TokenKind::FStringStart, TokenChannel::Main, start, startLocation);

        qsizetype textStart = m_pos;
        SourceLocation textLocation = location();
        while (!atEnd()) {
            if (startsWith(delimiter)) {
                scanFStringText(textStart, textLocation);
                const qsizetype delimiterStart = m_pos;
                const SourceLocation delimiterLocation = location();
                for (int index = 0; index < delimiter.size(); ++index) {
                    advance();
                }
                appendToken(TokenKind::FStringEnd, TokenChannel::Main,
                     delimiterStart, delimiterLocation);
                return;
            }
            if (!triple && isNewline(peek())) {
                scanFStringText(textStart, textLocation);
                diagnostic(QStringLiteral("نسق002"),
                           QStringLiteral("نص أحادي السطر غير منتهي"), startLocation);
                appendToken(TokenKind::Invalid, TokenChannel::Main, m_pos, location());
                return;
            }
            if (peek() == QChar(u'\\')) {
                advance();
                if (!atEnd() && !isNewline(peek())) {
                    advance();
                }
                continue;
            }
            if (peek() == QChar(u'{')) {
                if (peek(1) == QChar(u'{')) {
                    advance();
                    advance();
                    continue;
                }
                scanFStringText(textStart, textLocation);
                const qsizetype braceStart = m_pos;
                const SourceLocation braceLocation = location();
                advance();
                appendToken(TokenKind::InterpolationStart, TokenChannel::Main,
                     braceStart, braceLocation);
                if (!scanInterpolation()) {
                    return;
                }
                textStart = m_pos;
                textLocation = location();
                continue;
            }
            if (peek() == QChar(u'}') && peek(1) == QChar(u'}')) {
                advance();
                advance();
                continue;
            }
            if (peek() == QChar(u'}')) {
                const SourceLocation errorStart = location();
                advance();
                diagnostic(QStringLiteral("نسق006"),
                           QStringLiteral("قوس إغلاق مفقود ضمن النص المنسق"), errorStart);
                continue;
            }
            advance();
        }

        scanFStringText(textStart, textLocation);
        diagnostic(QStringLiteral("نسق003"),
                   QStringLiteral("نص منسق غير منتهي"), startLocation);
        emitZero(TokenKind::Invalid);
    }

    bool scanOperator() {
        static const Operator operators[] = {
            {"\\\\=", TokenKind::DoubleSlashEqual},
            {"\\*=", TokenKind::StarSlashEqual},
            {"*|=", TokenKind::BitwiseXorEqual},
            {"<<=", TokenKind::LeftShiftEqual},
            {">>=", TokenKind::RightShiftEqual},
            {"&=", TokenKind::BitwiseAndEqual},
            {"|=", TokenKind::BitwiseOrEqual},
            {"**", TokenKind::DoubleStar},
            {"<<", TokenKind::LeftShift},
            {">>", TokenKind::RightShift},
            {"*|", TokenKind::BitwiseXor},
            {"+=", TokenKind::PlusEqual},
            {"-=", TokenKind::MinusEqual},
            {"*=", TokenKind::StarEqual},
            {"\\=", TokenKind::SlashEqual},
            {"^=", TokenKind::PowerEqual},
            {"==", TokenKind::EqualEqual},
            {"!=", TokenKind::NotEqual},
            {"<=", TokenKind::LessEqual},
            {">=", TokenKind::GreaterEqual},
            {"\\\\", TokenKind::DoubleSlash},
            {"\\*", TokenKind::StarSlash},
            {"&", TokenKind::BitwiseAnd},
            {"|", TokenKind::BitwiseOr},
            {"~", TokenKind::BitwiseNot},
            {"+", TokenKind::Plus},
            {"-", TokenKind::Minus},
            {"*", TokenKind::Star},
            {"\\", TokenKind::Slash},
            {"^", TokenKind::Power},
            {"%", TokenKind::Percent},
            {"=", TokenKind::Equal},
            {"<", TokenKind::Less},
            {">", TokenKind::Greater}
        };

        for (const Operator& item : operators) {
            const QString spelling = QString::fromLatin1(item.spelling);
            if (!startsWith(spelling)) {
                continue;
            }
            const qsizetype start = m_pos;
            const SourceLocation startLocation = location();
            for (qsizetype index = 0; index < spelling.size(); ++index) {
                advance();
            }
            appendToken(item.kind, TokenChannel::Main, start, startLocation);
            return true;
        }
        return false;
    }

    bool scanPunctuation() {
        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();
        const QChar character = peek();
        TokenKind kind = TokenKind::Invalid;
        bool opensGrouping = false;
        bool closesGrouping = false;

        switch (character.unicode()) {
        case u'(':
            kind = TokenKind::LParen;
            opensGrouping = true;
            break;
        case u')':
            kind = TokenKind::RParen;
            closesGrouping = true;
            break;
        case u'[':
            kind = TokenKind::LBracket;
            opensGrouping = true;
            break;
        case u']':
            kind = TokenKind::RBracket;
            closesGrouping = true;
            break;
        case u'{':
            kind = TokenKind::LBrace;
            opensGrouping = true;
            break;
        case u'}':
            kind = TokenKind::RBrace;
            closesGrouping = true;
            break;
        case u',':
        case 0x060C: // Arabic comma: ،
            kind = TokenKind::Comma;
            break;
        case u'.':
            kind = TokenKind::Dot;
            break;
        case u':':
            kind = TokenKind::Colon;
            break;
        case u';':
        case 0x061B: // Arabic semicolon: ؛
            kind = TokenKind::Semicolon;
            break;
        default:
            return false;
        }

        advance();
        if (opensGrouping) {
            ++m_groupingDepth;
        } else if (closesGrouping && m_groupingDepth > 0) {
            --m_groupingDepth;
        }
        appendToken(kind, TokenChannel::Main, start, startLocation);
        return true;
    }

    void scanToken() {
        if (atEnd()) {
            return;
        }
        if (isHorizontalWhitespace(peek())) {
            scanWhitespace();
            return;
        }
        if (isNewline(peek())) {
            consumeNewline();
            return;
        }
        if (peek() == QChar(u'#')) {
            scanComment();
            return;
        }
        if (peek() == QChar(u'@')) {
            scanDecorator();
            return;
        }
        if (peek() == QChar(u'م') && (peek(1) == QChar(u'\'') || peek(1) == QChar(u'"'))) {
            scanFString();
            return;
        }
        if ((peek() == QChar(u'خ') || peek() == QChar(u'ث'))
            && (peek(1) == QChar(u'\'') || peek(1) == QChar(u'"'))) {
            // Keep raw/byte spellings lossless; escape decoding belongs to the
            // runtime. Escaped delimiters still participate in token scanning.
            scanString(peek() == QChar(u'خ') ? TokenKind::RawStringLiteral
                                             : TokenKind::BinaryStringLiteral, true);
            return;
        }
        if (peek() == QChar(u'\'') || peek() == QChar(u'"')) {
            scanString();
            return;
        }
        if (isIdentifierStart(peek())) {
            scanIdentifier();
            return;
        }
        if (peek().isDigit()) {
            scanNumber();
            return;
        }
        if (scanOperator() || scanPunctuation()) {
            return;
        }

        const qsizetype start = m_pos;
        const SourceLocation startLocation = location();
        advance();
        diagnostic(QStringLiteral("نسق001"),
                   QStringLiteral("حرف غير معروف"), startLocation);
        appendToken(TokenKind::Invalid, TokenChannel::Main, start, startLocation);
    }
};

} // namespace

LexResult TaifLexer::lex(const QString& source) const {
    return Scanner(source).scan();
}
