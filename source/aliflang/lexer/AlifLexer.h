#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QVector>

/**
 * The language-core lexer uses UTF-16 coordinates because QString and the Qt
 * text document APIs use UTF-16 code units. Every range is half-open.
 */
struct SourceLocation final {
    qsizetype offset = 0;
    qsizetype line = 1;
    qsizetype column = 1;
};

struct SourceRange final {
    SourceLocation begin;
    SourceLocation end;
};

enum class TokenChannel : quint8 {
    Main,
    Trivia
};

/**
 * Parser terminals. Presentation-specific categories deliberately do not
 * appear here: declarations and calls are parsed from Identifier tokens.
 */
enum class TokenKind : quint16 {
    EndOfFile,
    Invalid,

    Identifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    Decorator,
    Whitespace,
    Comment,

    Newline, Indent, Dedent,

    KwFunction,
    KwClass,
    KwIf,
    KwElseIf,
    KwElse,
    KwFor,
    KwIn,
    KwWhile,
    KwTry,
    KwExcept,
    KwFinally,
    KwReturn,
    KwYield,
    KwImport,
    KwAs,
    KwThen,
    KwIs,
    KwFrom,
    KwDelete,
    KwBreak,
    KwContinue,
    KwAnd,
    KwOr,
    KwNot,
    KwLambda,
    KwTrue,
    KwFalse,
    KwNull,
    KwPass,
    KwWith,
    KwGlobal,

    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    Comma,
    Dot,
    Colon,
    Semicolon,

    Plus,
    Minus,
    Star,
    DoubleStar,
    Slash,
    StarSlash,
    DoubleSlash,
    Power,
    Percent,
    Equal,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    StarSlashEqual,
    DoubleSlashEqual,
    PowerEqual,
    EqualEqual,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,

    FStringStart,
    FStringText,
    FStringFormat,
    InterpolationStart,
    InterpolationEnd,
    FStringEnd,
    RawStringLiteral,
    BinaryStringLiteral,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    BitwiseNot,
    LeftShift,
    RightShift,
    BitwiseAndEqual,
    BitwiseOrEqual,
    BitwiseXorEqual,
    LeftShiftEqual,
    RightShiftEqual
};

struct Token final {
    TokenKind kind = TokenKind::Invalid;
    TokenChannel channel = TokenChannel::Main;
    SourceRange range;
    QString lexeme;
};

struct LexDiagnostic final {
    QString code;
    QString message;
    SourceRange range;
};

struct LexResult final {
    QVector<Token> tokens;
    QVector<LexDiagnostic> diagnostics;
};

/**
 * Stateless parser-facing lexer. Each call owns its own cursor, layout stack,
 * and literal state, making full-document lexing deterministic and isolated.
 */
class TaifLexer final {
public:
    [[nodiscard]] LexResult lex(const QString& source) const;
};

Q_DECLARE_METATYPE(TokenKind)
Q_DECLARE_METATYPE(TokenChannel)
Q_DECLARE_METATYPE(Token)
Q_DECLARE_METATYPE(LexDiagnostic)
