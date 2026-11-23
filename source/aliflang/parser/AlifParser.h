#pragma once

#include "AlifLexer.h"

#include <QtCore/QString>
#include <QtCore/QVector>

#include <memory>

using SyntaxNodeId = qsizetype;
using AstNodeId = qsizetype;

inline constexpr SyntaxNodeId InvalidSyntaxNodeId = -1;
inline constexpr AstNodeId InvalidAstNodeId = -1;

enum class ParseDiagnosticSeverity : quint8 {
    Error,
    Warning
};

struct ParseDiagnostic final {
    QString code;
    QString message;
    SourceRange range;
    ParseDiagnosticSeverity severity = ParseDiagnosticSeverity::Error;
};

/**
 * Concrete-syntax categories. SyntaxTree retains every lexer token, including
 * trivia, and syntax nodes reference contiguous token spans into that snapshot.
 */
enum class SyntaxKind : quint16 {
    Module,
    StatementList,
    Suite,
    FunctionDeclaration,
    ClassDeclaration,
    IfStatement,
    ElseIfClause,
    ElseClause,
    ForStatement,
    WhileStatement,
    TryStatement,
    ExceptClause,
    FinallyClause,
    ReturnStatement,
    YieldStatement,
    ImportStatement,
    FromImportStatement,
    DeleteStatement,
    BreakStatement,
    ContinueStatement,
    PassStatement,
    AssignmentStatement,
    ExpressionStatement,
    ParameterList,
    ArgumentList,
    KeywordArgument,
    StarArgument,
    DoubleStarArgument,
    ComprehensionExpression,
    NamePattern,
    NameExpression,
    LiteralExpression,
    UnaryExpression,
    BinaryExpression,
    CallExpression,
    MemberExpression,
    IndexExpression,
    SliceExpression,
    ListExpression,
    TupleExpression,
    MapExpression,
    SetExpression,
    LambdaExpression,
    FormattedStringExpression,
    FormattedStringPart,
    ErrorNode,
    MissingToken,
    ImportWildcard,
    WithStatement,
    GlobalStatement
};

struct SyntaxNode final {
    SyntaxKind kind = SyntaxKind::ErrorNode;
    SourceRange range;
    qsizetype firstToken = 0;
    qsizetype endToken = 0; // Exclusive index into SyntaxTree::tokens.
    QVector<SyntaxNodeId> children;
    TokenKind expectedToken = TokenKind::Invalid; // Set only for MissingToken.
};

/**
 * Immutable-after-construction, lossless parser snapshot. The complete lexer
 * token stream is retained so comments and whitespace remain available for
 * editor features even though grammar lookahead skips trivia.
 */
class SyntaxTree final {
public:
    [[nodiscard]] const QVector<Token>& tokens() const { return m_tokens; }
    [[nodiscard]] const QVector<SyntaxNode>& nodes() const { return m_nodes; }
    [[nodiscard]] SyntaxNodeId rootId() const { return m_rootId; }
    [[nodiscard]] const SyntaxNode& root() const;

private:
    friend class TaifParser;
    friend class TaifParserImpl;

    QVector<Token> m_tokens;
    QVector<SyntaxNode> m_nodes;
    SyntaxNodeId m_rootId = InvalidSyntaxNodeId;
};

enum class AstNodeKind : quint16 {
    Module,
    Suite,
    FunctionDeclaration,
    ClassDeclaration,
    IfStatement,
    ElseIfClause,
    ElseClause,
    ForStatement,
    WhileStatement,
    TryStatement,
    ExceptClause,
    FinallyClause,
    ReturnStatement,
    YieldStatement,
    ImportStatement,
    FromImportStatement,
    DeleteStatement,
    BreakStatement,
    ContinueStatement,
    PassStatement,
    GlobalStatement,
    AssignmentStatement,
    ExpressionStatement,
    Parameter,
    KeywordArgument,
    StarArgument,
    DoubleStarArgument,
    ComprehensionExpression,
    NamePattern,
    NameExpression,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    BooleanLiteral,
    NullLiteral,
    UnaryExpression,
    BinaryExpression,
    CallExpression,
    MemberExpression,
    IndexExpression,
    SliceExpression,
    ListExpression,
    TupleExpression,
    MapExpression,
    SetExpression,
    LambdaExpression,
    FormattedStringExpression,
    FormattedStringText,
    FormattedStringInterpolation,
    FormattedStringFormat,
    ErrorExpression,
    ErrorStatement,
    ImportWildcard, // '*' in a from-import; not an identifier or binding.
    RawStringLiteral,
    BinaryStringLiteral,
    WithStatement
};

enum class AstChildRole : quint8 {
    Unknown,
    DeclarationName,
    ParameterList,
    ParameterName,
    DefaultValue,
    Body,
    Base,
    Condition,
    Target,
    Value,
    Iterable,
    Element,
    Callee,
    Argument,
    MemberBase,
    MemberName,
    ImportPath,
    ImportAlias,
    ImportName,
    ReturnValue,
    YieldedValue,
    DeletedValue,
    FormattedPart,
    FormatExpression,
    FormatSpecifier,
    GlobalName,
    ExceptionType,
    ExceptionTarget,
    ContextExpression,
    ElseBody
};

/**
 * Compact semantic node used by the future symbol-table tier. `text` holds an
 * identifier, literal spelling, operator spelling, or declared name according
 * to `kind`; child interpretation is defined by the parser grammar document.
 */
struct AstNode final {
    AstNodeId id = InvalidAstNodeId;
    AstNodeKind kind = AstNodeKind::ErrorExpression;
    SourceRange range;
    QString text;
    QVector<AstNodeId> children;
    QVector<AstChildRole> childRoles;
    qsizetype assignmentTargetCount = -1;
    SyntaxNodeId syntaxNode = InvalidSyntaxNodeId;
};

class AstModule final {
public:
    [[nodiscard]] const QVector<AstNode>& nodes() const { return m_nodes; }
    [[nodiscard]] AstNodeId rootId() const { return m_rootId; }
    [[nodiscard]] const AstNode& root() const;
    [[nodiscard]] const AstNode& node(const AstNodeId id) const;

private:
    friend class TaifParser;
    friend class TaifParserImpl;

    QVector<AstNode> m_nodes;
    AstNodeId m_rootId = InvalidAstNodeId;
};

struct ParseResult final {
    std::shared_ptr<const SyntaxTree> syntaxTree;
    std::shared_ptr<const AstModule> ast;
    QVector<LexDiagnostic> lexicalDiagnostics;
    QVector<ParseDiagnostic> parserDiagnostics;
    quint64 documentRevision = 0;
};

/**
 * Deliberately small handoff envelope. Semantic analysis receives an AST
 * snapshot and parser diagnostics, never a raw source string or token vector.
 */
struct SymbolTableInput final {
    const AstModule& module;
    const QVector<ParseDiagnostic>& parserDiagnostics;
    quint64 documentRevision = 0;
};

struct TextEdit final {
    SourceRange replacedRange;
    QString insertedText;
    quint64 baseRevision = 0;
    quint64 resultRevision = 0;
};

struct IncrementalParseResult final {
    ParseResult result;
    bool usedFullReparseFallback = true;
    qsizetype reusedSyntaxNodeCount = 0;
};

/**
 * Error-tolerant, non-backtracking Taif parser. `parse(source)` is a convenience
 * wrapper around the canonical `parse(source, LexResult)` contract.
 */
class TaifParser final {
public:
    [[nodiscard]] ParseResult parse(const QString& source,
                                    const LexResult& lexicalResult,
                                    quint64 documentRevision = 0) const;
    [[nodiscard]] ParseResult parse(const QString& source,
                                    quint64 documentRevision = 0) const;

    /**
     * Correctness-first incremental boundary. The current lexer has no typed
     * checkpoint/relex API, so this method deliberately performs a full parse
     * and reports that fallback. It preserves the final API contract required
     * for later localized relexing and green-node reuse.
     */
    [[nodiscard]] IncrementalParseResult reparse(const ParseResult& previous,
                                                 const QString& newSource,
                                                 const TextEdit& edit) const;
};

Q_DECLARE_METATYPE(ParseDiagnostic)
Q_DECLARE_METATYPE(SyntaxKind)
Q_DECLARE_METATYPE(AstNodeKind)
Q_DECLARE_METATYPE(AstChildRole)
