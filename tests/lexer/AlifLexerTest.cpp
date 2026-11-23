#include "AlifLexer.h"

#include <QtTest/QTest>
#include <QtCore/QFile>

namespace {

QVector<TokenKind> mainKinds(const LexResult& result) {
    QVector<TokenKind> kinds;
    for (const Token& token : result.tokens) {
        if (token.channel == TokenChannel::Main) {
            kinds.append(token.kind);
        }
    }
    return kinds;
}

void compareKinds(const LexResult& result, const QVector<TokenKind>& expected) {
    const QVector<TokenKind> actual = mainKinds(result);
    QCOMPARE(actual.size(), expected.size());
    for (qsizetype index = 0; index < actual.size(); ++index) {
        QCOMPARE(actual.at(index), expected.at(index));
    }
}

} // namespace

class TaifLexerTest final : public QObject {
    Q_OBJECT

private slots:
    void emptyFileProducesEndOfFile();
    void statementSeparatorsAndKeywordAliases();
    void globalKeywordIsLexed();
    void decoratorsAreLexedWithoutDiagnostics();
    void layoutTriviaAndSourceRanges();
    void literalsAndLongestMatchOperators();
    void binaryOctalAndHexadecimalPrefixes_data();
    void binaryOctalAndHexadecimalPrefixes();
    void bitwiseOperatorsUseLongestMatch();
    void invalidRadixDigitsAreDiagnosed();
    void formattedStringsHaveStructuredTokens();
    void formattedStringFormatsAreStructured();
    void rawAndBinaryStringsHaveLosslessTokens_data();
    void rawAndBinaryStringsHaveLosslessTokens();
    void prefixLettersRemainIdentifiersWithoutAdjacentQuotes();
    void malformedPrefixedStringsRetainDiagnostics();
    void malformedLiteralReportsDiagnostic();
    void statusCorpusLexesToEndOfFile();
};

void TaifLexerTest::emptyFileProducesEndOfFile() {
    const LexResult result = TaifLexer().lex(QString());

    QCOMPARE(result.diagnostics.size(), 0);
    QCOMPARE(result.tokens.size(), 1);
    QCOMPARE(result.tokens.constFirst().kind, TokenKind::EndOfFile);
    QCOMPARE(result.tokens.constFirst().range.begin.offset, qsizetype(0));
    QCOMPARE(result.tokens.constFirst().range.end.offset, qsizetype(0));
}

void TaifLexerTest::statementSeparatorsAndKeywordAliases() {
    const LexResult statement = TaifLexer().lex(QStringLiteral("س = 5؛ ص = 9\n"));
    QCOMPARE(statement.diagnostics.size(), 0);
    compareKinds(statement, {
        TokenKind::Identifier, TokenKind::Equal, TokenKind::IntegerLiteral,
        TokenKind::Semicolon, TokenKind::Identifier, TokenKind::Equal,
        TokenKind::IntegerLiteral, TokenKind::Newline, TokenKind::EndOfFile
    });

    const LexResult aliases = TaifLexer().lex(QStringLiteral("إذا صح و ليس عدم عند هي هو\n"));
    QCOMPARE(aliases.diagnostics.size(), 0);
    compareKinds(aliases, {
        TokenKind::KwIf, TokenKind::KwTrue, TokenKind::KwAnd,
        TokenKind::KwNot, TokenKind::KwNull, TokenKind::KwWith,
        TokenKind::KwIs, TokenKind::KwIs, TokenKind::Newline,
        TokenKind::EndOfFile
    });
    const LexResult loopElse = TaifLexer().lex(QStringLiteral("اذن:\n"));
    QCOMPARE(loopElse.diagnostics.size(), 0);
    compareKinds(loopElse, {TokenKind::KwThen, TokenKind::Colon,
                            TokenKind::Newline, TokenKind::EndOfFile});
}

void TaifLexerTest::globalKeywordIsLexed() {
    const LexResult result = TaifLexer().lex(QStringLiteral("عام متغير\n"));
    QVERIFY(result.diagnostics.isEmpty());
    compareKinds(result, {TokenKind::KwGlobal, TokenKind::Identifier,
                          TokenKind::Newline, TokenKind::EndOfFile});
}

void TaifLexerTest::decoratorsAreLexedWithoutDiagnostics() {
    const LexResult result = TaifLexer().lex(QStringLiteral("@مميز\n"));

    QCOMPARE(result.diagnostics.size(), 0);
    compareKinds(result, {
        TokenKind::Decorator, TokenKind::Newline, TokenKind::EndOfFile
    });
    QCOMPARE(result.tokens.constFirst().lexeme, QStringLiteral("@مميز"));
}

void TaifLexerTest::layoutTriviaAndSourceRanges() {
    const QString source = QStringLiteral("# تعليق\nاذا س:\n\tاطبع(س)\nوالا:\n\tاطبع(خطأ)\n");
    const LexResult result = TaifLexer().lex(source);

    QCOMPARE(result.diagnostics.size(), 0);
    compareKinds(result, {
        TokenKind::Newline,
        TokenKind::KwIf, TokenKind::Identifier, TokenKind::Colon, TokenKind::Newline,
        TokenKind::Indent, TokenKind::Identifier, TokenKind::LParen,
        TokenKind::Identifier, TokenKind::RParen, TokenKind::Newline,
        TokenKind::Dedent, TokenKind::KwElse, TokenKind::Colon, TokenKind::Newline,
        TokenKind::Indent, TokenKind::Identifier, TokenKind::LParen,
        TokenKind::KwFalse, TokenKind::RParen, TokenKind::Newline,
        TokenKind::Dedent, TokenKind::EndOfFile
    });

    QVERIFY(!result.tokens.isEmpty());
    for (const Token& token : result.tokens) {
        QVERIFY(token.range.begin.offset <= token.range.end.offset);
        QVERIFY(token.range.begin.line >= 1);
        QVERIFY(token.range.begin.column >= 1);
    }

    bool foundComment = false;
    bool foundIndentTrivia = false;
    for (const Token& token : result.tokens) {
        if (token.kind == TokenKind::Comment && token.channel == TokenChannel::Trivia) {
            foundComment = true;
        }
        if (token.kind == TokenKind::Whitespace && token.channel == TokenChannel::Trivia
            && token.lexeme == QStringLiteral("\t")) {
            foundIndentTrivia = true;
        }
    }
    QVERIFY(foundComment);
    QVERIFY(foundIndentTrivia);
}

void TaifLexerTest::literalsAndLongestMatchOperators() {
    const LexResult result = TaifLexer().lex(QStringLiteral("0x2A \\*= 3 \\\\= 5 ^ 2\n"));

    QCOMPARE(result.diagnostics.size(), 0);
    compareKinds(result, {
        TokenKind::IntegerLiteral, TokenKind::StarSlashEqual,
        TokenKind::IntegerLiteral, TokenKind::DoubleSlashEqual,
        TokenKind::IntegerLiteral, TokenKind::Power,
        TokenKind::IntegerLiteral, TokenKind::Newline, TokenKind::EndOfFile
    });
}

void TaifLexerTest::binaryOctalAndHexadecimalPrefixes_data() {
    QTest::addColumn<QString>("literal");
    QTest::addColumn<QString>("expectedLexeme");
    const QVector<QStringList> pairs {
        {QStringLiteral("0b00000011"), QStringLiteral("0ث00000011")},
        {QStringLiteral("0o00000071"), QStringLiteral("0م00000071")},
        {QStringLiteral("0x0000001c"), QStringLiteral("0س0000001ت")}
    };
    for (const QStringList& pair : pairs) {
        QTest::newRow(qPrintable(pair.at(0))) << pair.at(0) << pair.at(0);
        QTest::newRow(qPrintable(pair.at(1))) << pair.at(1) << pair.at(1);
    }
}

void TaifLexerTest::binaryOctalAndHexadecimalPrefixes() {
    QFETCH(QString, literal);
    QFETCH(QString, expectedLexeme);
    const LexResult result = TaifLexer().lex(literal);
    QVERIFY(result.diagnostics.isEmpty());
    compareKinds(result, {TokenKind::IntegerLiteral, TokenKind::EndOfFile});
    QCOMPARE(result.tokens.constFirst().lexeme, expectedLexeme);
    QCOMPARE(result.tokens.constFirst().range.end.offset, literal.size());
}

void TaifLexerTest::bitwiseOperatorsUseLongestMatch() {
    const QString source = QStringLiteral("& | *| ~ << >> &= |= *|= <<= >>= *| *=");
    const LexResult result = TaifLexer().lex(source);
    QVERIFY(result.diagnostics.isEmpty());
    compareKinds(result, {
        TokenKind::BitwiseAnd, TokenKind::BitwiseOr, TokenKind::BitwiseXor,
        TokenKind::BitwiseNot, TokenKind::LeftShift, TokenKind::RightShift,
        TokenKind::BitwiseAndEqual, TokenKind::BitwiseOrEqual,
        TokenKind::BitwiseXorEqual, TokenKind::LeftShiftEqual,
        TokenKind::RightShiftEqual, TokenKind::BitwiseXor, TokenKind::StarEqual,
        TokenKind::EndOfFile
    });
    QVector<Token> mainTokens;
    for (const Token& token : result.tokens) {
        if (token.channel == TokenChannel::Main) {
            mainTokens.append(token);
        }
    }
    QCOMPARE(mainTokens.at(8).lexeme, QStringLiteral("*|="));
    QCOMPARE(mainTokens.at(9).lexeme, QStringLiteral("<<="));
    QCOMPARE(mainTokens.at(10).lexeme, QStringLiteral(">>="));
}

void TaifLexerTest::invalidRadixDigitsAreDiagnosed() {
    for (const QString& source : {QStringLiteral("0b2"), QStringLiteral("0ث2"),
                                  QStringLiteral("0o8"), QStringLiteral("0م8"),
                                  QStringLiteral("0x1g"), QStringLiteral("0س1ز")}) {
        const LexResult result = TaifLexer().lex(source);
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.diagnostics.constFirst().code, QStringLiteral("نسق005"));
        QCOMPARE(result.tokens.constFirst().kind, TokenKind::Invalid);
    }
}

void TaifLexerTest::formattedStringsHaveStructuredTokens() {
    const LexResult result = TaifLexer().lex(QStringLiteral("س = م\"قيمة {س + 1}\"\n"));

    QCOMPARE(result.diagnostics.size(), 0);
    compareKinds(result, {
        TokenKind::Identifier, TokenKind::Equal,
        TokenKind::FStringStart, TokenKind::FStringText,
        TokenKind::InterpolationStart, TokenKind::Identifier,
        TokenKind::Plus, TokenKind::IntegerLiteral,
        TokenKind::InterpolationEnd, TokenKind::FStringEnd,
        TokenKind::Newline, TokenKind::EndOfFile
    });
}

void TaifLexerTest::formattedStringFormatsAreStructured() {
    const LexResult result = TaifLexer().lex(QStringLiteral("م\"{س:.2ف}%\""));

    QCOMPARE(result.diagnostics.size(), 0);
    compareKinds(result, {
        TokenKind::FStringStart, TokenKind::InterpolationStart,
        TokenKind::Identifier, TokenKind::Colon, TokenKind::FStringFormat,
        TokenKind::InterpolationEnd, TokenKind::FStringText, TokenKind::FStringEnd,
        TokenKind::EndOfFile
    });
}

void TaifLexerTest::rawAndBinaryStringsHaveLosslessTokens_data() {
    QTest::addColumn<QString>("literal");
    for (const QString& prefix : {QStringLiteral("خ"), QStringLiteral("ث")}) {
        for (const QString& delimiter : {QStringLiteral("\""), QStringLiteral("'"), QStringLiteral("\"\"\""), QStringLiteral("'''")}) {
            for (const QString& text : {QString(), QStringLiteral("abc\\n{اسم}")}) {
                const QString literal = prefix + delimiter + text + delimiter;
                QTest::newRow(qPrintable(literal)) << literal;
            }
        }
    }
    QTest::newRow("raw-escaped-quote") << QStringLiteral("خ\"a\\\"b\"");
    QTest::newRow("raw-multiline") << QStringLiteral("خ\"\"\"a\nb\"\"\"");
    QTest::newRow("binary-multiline") << QStringLiteral("ث'''a\nb'''");
}

void TaifLexerTest::rawAndBinaryStringsHaveLosslessTokens() {
    QFETCH(QString, literal);
    const LexResult result = TaifLexer().lex(literal);
    QVERIFY(result.diagnostics.isEmpty());
    const TokenKind expected = literal.startsWith(QChar(u'خ'))
        ? TokenKind::RawStringLiteral : TokenKind::BinaryStringLiteral;
    compareKinds(result, {expected, TokenKind::EndOfFile});
    const Token& token = result.tokens.constFirst();
    QCOMPARE(token.lexeme, literal);
    QCOMPARE(token.range.begin.offset, qsizetype(0));
    QCOMPARE(token.range.end.offset, literal.size());
}

void TaifLexerTest::prefixLettersRemainIdentifiersWithoutAdjacentQuotes() {
    const LexResult result = TaifLexer().lex(QStringLiteral("م خ ث خام ثنائي م \"x\""));
    QVERIFY(result.diagnostics.isEmpty());
    compareKinds(result, {TokenKind::Identifier, TokenKind::Identifier, TokenKind::Identifier,
                          TokenKind::Identifier, TokenKind::Identifier, TokenKind::Identifier,
                          TokenKind::StringLiteral, TokenKind::EndOfFile});
}

void TaifLexerTest::malformedPrefixedStringsRetainDiagnostics() {
    for (const QString& prefix : {QStringLiteral("م"), QStringLiteral("خ"), QStringLiteral("ث")}) {
        const LexResult result = TaifLexer().lex(prefix + QStringLiteral("\"abc\n"));
        QVERIFY(!result.diagnostics.isEmpty());
        QCOMPARE(result.diagnostics.constFirst().code, QStringLiteral("نسق002"));
        QCOMPARE(result.diagnostics.constFirst().range.begin.offset, qsizetype(0));
        QCOMPARE(result.tokens.constLast().kind, TokenKind::EndOfFile);
    }
}

void TaifLexerTest::malformedLiteralReportsDiagnostic() {
    const LexResult result = TaifLexer().lex(QStringLiteral("س = \"غير منته\n"));

    QVERIFY(!result.diagnostics.isEmpty());
    QCOMPARE(result.diagnostics.constFirst().code, QStringLiteral("نسق002"));
    QVERIFY(result.tokens.constLast().kind == TokenKind::EndOfFile);
}

void TaifLexerTest::statusCorpusLexesToEndOfFile() {
    QFile file(QFINDTESTDATA("data/Status.alif"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QString corpus = QString::fromUtf8(file.readAll());
    QVERIFY(!corpus.isEmpty());

    const LexResult result = TaifLexer().lex(corpus);
    QVERIFY(!result.tokens.isEmpty());
    QCOMPARE(result.tokens.constLast().kind, TokenKind::EndOfFile);
    QStringList diagnosticSummary;
    for (const LexDiagnostic& diagnostic : result.diagnostics) {
        diagnosticSummary.append(QStringLiteral("%1 at %2:%3: %4")
            .arg(diagnostic.code)
            .arg(diagnostic.range.begin.line)
            .arg(diagnostic.range.begin.column)
            .arg(diagnostic.message));
    }
    QVERIFY2(result.diagnostics.isEmpty(), qPrintable(diagnosticSummary.join(QStringLiteral(" | "))));

    bool foundFString = false;
    bool foundIndent = false;
    bool foundSemicolon = false;
    for (const Token& token : result.tokens) {
        foundFString = foundFString || token.kind == TokenKind::FStringStart;
        foundIndent = foundIndent || token.kind == TokenKind::Indent;
        foundSemicolon = foundSemicolon || token.kind == TokenKind::Semicolon;
    }
    QVERIFY(foundFString);
    QVERIFY(foundIndent);
    QVERIFY(foundSemicolon);
}

QTEST_GUILESS_MAIN(TaifLexerTest)
#include "AlifLexerTest.moc"
