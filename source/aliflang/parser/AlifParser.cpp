#include "AlifParser.h"

#include <QtCore/QDebug>

#include <initializer_list>
#include <utility>

namespace {

struct ParsedNode final {
    AstNodeId ast = InvalidAstNodeId;
    SyntaxNodeId syntax = InvalidSyntaxNodeId;
};

QString tokenDisplayName(const TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier: return QStringLiteral("اسم");
    case TokenKind::Newline: return QStringLiteral("سطر جديد");
    case TokenKind::Indent: return QStringLiteral("مسافة بادئة");
    case TokenKind::Dedent: return QStringLiteral("مسافة راجعة");
    case TokenKind::LParen: return QStringLiteral("'('");
    case TokenKind::RParen: return QStringLiteral("')'");
    case TokenKind::LBracket: return QStringLiteral("'['");
    case TokenKind::RBracket: return QStringLiteral("']'");
    case TokenKind::LBrace: return QStringLiteral("'{'");
    case TokenKind::RBrace: return QStringLiteral("'}'");
    case TokenKind::Colon: return QStringLiteral("':'");
    case TokenKind::EndOfFile: return QStringLiteral("نهاية الملف");
    default: return QStringLiteral("رمز متوقع");
    }
}

bool isAssignmentOperator(const TokenKind kind) {
    switch (kind) {
    case TokenKind::Equal:
    case TokenKind::PlusEqual:
    case TokenKind::MinusEqual:
    case TokenKind::StarEqual:
    case TokenKind::SlashEqual:
    case TokenKind::StarSlashEqual:
    case TokenKind::DoubleSlashEqual:
    case TokenKind::PowerEqual:
    case TokenKind::BitwiseAndEqual:
    case TokenKind::BitwiseOrEqual:
    case TokenKind::BitwiseXorEqual:
    case TokenKind::LeftShiftEqual:
    case TokenKind::RightShiftEqual:
        return true;
    default:
        return false;
    }
}

bool isStatementBoundary(const TokenKind kind) {
    switch (kind) {
    case TokenKind::Newline:
    case TokenKind::Semicolon:
    case TokenKind::Dedent:
    case TokenKind::EndOfFile:
        return true;
    default:
        return false;
    }
}

bool isExpressionBoundary(const TokenKind kind) {
    switch (kind) {
    case TokenKind::Comma:
    case TokenKind::Colon:
    case TokenKind::Newline:
    case TokenKind::Semicolon:
    case TokenKind::Dedent:
    case TokenKind::EndOfFile:
    case TokenKind::RParen:
    case TokenKind::RBracket:
    case TokenKind::RBrace:
    case TokenKind::InterpolationEnd:
        return true;
    default:
        return false;
    }
}

int binaryBindingPower(const TokenKind kind) {
    switch (kind) {
    case TokenKind::KwOr:
        return 10;
    case TokenKind::KwAnd:
        return 20;
    case TokenKind::BitwiseOr:
        return 24;
    case TokenKind::BitwiseXor:
        return 25;
    case TokenKind::BitwiseAnd:
        return 26;
    case TokenKind::EqualEqual:
    case TokenKind::NotEqual:
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
    case TokenKind::KwIn:
    case TokenKind::KwIs:
        return 30;
    case TokenKind::LeftShift:
    case TokenKind::RightShift:
        return 35;
    case TokenKind::Plus:
    case TokenKind::Minus:
        return 40;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::StarSlash:
    case TokenKind::DoubleSlash:
    case TokenKind::Percent:
        return 50;
    case TokenKind::Power:
        return 60;
    default:
        return -1;
    }
}

} // namespace

class TaifParserImpl final {
public:
    TaifParserImpl(const QString& source, const LexResult& lexicalResult,
                   const quint64 revision)
        : m_source(source),
          m_lexicalResult(lexicalResult),
          m_revision(revision),
          m_tree(std::make_shared<SyntaxTree>()),
          m_ast(std::make_shared<AstModule>()) {
        m_tree->m_tokens = lexicalResult.tokens;
        for (qsizetype index = 0; index < m_tree->m_tokens.size(); ++index) {
            if (m_tree->m_tokens.at(index).channel == TokenChannel::Main) {
                m_mainTokenIndices.append(index);
            }
        }
        if (m_mainTokenIndices.isEmpty()
            || m_tree->m_tokens.at(m_mainTokenIndices.constLast()).kind != TokenKind::EndOfFile) {
            Token eof;
            eof.kind = TokenKind::EndOfFile;
            eof.channel = TokenChannel::Main;
            if (!m_tree->m_tokens.isEmpty()) {
                eof.range.begin = m_tree->m_tokens.constLast().range.end;
                eof.range.end = eof.range.begin;
            }
            m_tree->m_tokens.append(eof);
            m_mainTokenIndices.append(m_tree->m_tokens.size() - 1);
        }
    }

    [[nodiscard]] ParseResult run() {
        const qsizetype start = m_mainPosition;
        QVector<AstNodeId> statements;
        QVector<SyntaxNodeId> children;

        skipStatementSeparators();
        while (!at(TokenKind::EndOfFile)) {
            const qsizetype before = m_mainPosition;
            const ParsedNode statement = parseStatement();
            if (statement.ast != InvalidAstNodeId) {
                statements.append(statement.ast);
            }
            if (statement.syntax != InvalidSyntaxNodeId) {
                children.append(statement.syntax);
            }
            if (m_mainPosition == before) {
                report(QStringLiteral("عقد099"),
                       QStringLiteral("لم يحرز المحلل اللغوي أي تقدم؛ تم تخطي الرمز غير المتوقع"),
                       current().range);
                consume();
            }
            skipStatementSeparators();
        }

        const qsizetype eofPosition = m_mainPosition;
        if (at(TokenKind::EndOfFile)) {
            consume();
        }
        const SourceRange range = rangeFrom(start, m_mainPosition);
        const SyntaxNodeId rootSyntax = makeSyntax(SyntaxKind::Module, start,
                                                    m_mainPosition, children);
        const AstNodeId rootAst = makeAst(AstNodeKind::Module, range,
                                          QString(), statements, rootSyntax);
        m_tree->m_rootId = rootSyntax;
        m_ast->m_rootId = rootAst;

        ParseResult result;
        result.syntaxTree = m_tree;
        result.ast = m_ast;
        result.lexicalDiagnostics = m_lexicalResult.diagnostics;
        result.parserDiagnostics = m_diagnostics;
        result.documentRevision = m_revision;
        Q_UNUSED(eofPosition)
        return result;
    }

private:
    const QString& m_source;
    const LexResult& m_lexicalResult;
    quint64 m_revision = 0;
    std::shared_ptr<SyntaxTree> m_tree;
    std::shared_ptr<AstModule> m_ast;
    QVector<ParseDiagnostic> m_diagnostics;
    QVector<qsizetype> m_mainTokenIndices;
    qsizetype m_mainPosition = 0;

    [[nodiscard]] const Token& current(const qsizetype lookahead = 0) const {
        const qsizetype position = qMin(m_mainPosition + lookahead,
                                        m_mainTokenIndices.size() - 1);
        return m_tree->m_tokens.at(m_mainTokenIndices.at(position));
    }

    [[nodiscard]] qsizetype currentRawIndex() const {
        return m_mainTokenIndices.at(qMin(m_mainPosition,
                                          m_mainTokenIndices.size() - 1));
    }

    [[nodiscard]] bool at(const TokenKind kind) const {
        return current().kind == kind;
    }

    [[nodiscard]] bool atAny(const std::initializer_list<TokenKind> kinds) const {
        for (const TokenKind kind : kinds) {
            if (at(kind)) {
                return true;
            }
        }
        return false;
    }

    void consume() {
        if (m_mainPosition + 1 < m_mainTokenIndices.size()) {
            ++m_mainPosition;
        }
    }

    bool consumeIf(const TokenKind kind) {
        if (!at(kind)) {
            return false;
        }
        consume();
        return true;
    }

    void report(const QString& code, const QString& message,
                const SourceRange& range) {
        constexpr int maximumDiagnostics = 256;
        if (m_diagnostics.size() >= maximumDiagnostics) {
            return;
        }
        m_diagnostics.append({code, message, range, ParseDiagnosticSeverity::Error});
    }

    [[nodiscard]] SourceRange rangeFrom(const qsizetype start,
                                        const qsizetype end) const {
        const qsizetype safeStart = qBound<qsizetype>(0, start,
                                                       m_mainTokenIndices.size() - 1);
        const qsizetype safeEnd = qBound<qsizetype>(safeStart, end,
                                                     m_mainTokenIndices.size());
        const Token& first = m_tree->m_tokens.at(m_mainTokenIndices.at(safeStart));
        if (safeEnd == safeStart) {
            return {first.range.begin, first.range.begin};
        }
        const Token& last = m_tree->m_tokens.at(
            m_mainTokenIndices.at(qMin(safeEnd - 1, m_mainTokenIndices.size() - 1)));
        return {first.range.begin, last.range.end};
    }

    [[nodiscard]] SyntaxNodeId makeSyntax(const SyntaxKind kind,
                                          const qsizetype start,
                                          const qsizetype end,
                                          const QVector<SyntaxNodeId>& children = {},
                                          const TokenKind expected = TokenKind::Invalid) {
        SyntaxNode node;
        node.kind = kind;
        node.range = rangeFrom(start, end);
        node.firstToken = m_mainTokenIndices.at(qBound<qsizetype>(
            0, start, m_mainTokenIndices.size() - 1));
        node.endToken = end == start ? node.firstToken
            : m_mainTokenIndices.at(qBound<qsizetype>(0, end - 1,
                                                       m_mainTokenIndices.size() - 1)) + 1;
        node.children = children;
        node.expectedToken = expected;
        m_tree->m_nodes.append(std::move(node));
        return m_tree->m_nodes.size() - 1;
    }

    [[nodiscard]] SyntaxNodeId makeMissing(const TokenKind expected) {
        const qsizetype position = m_mainPosition;
        return makeSyntax(SyntaxKind::MissingToken, position, position, {}, expected);
    }

    [[nodiscard]] AstNodeId makeAst(const AstNodeKind kind, const SourceRange& range,
                                    const QString& text,
                                    const QVector<AstNodeId>& children,
                                    const SyntaxNodeId syntaxNode,
                                    QVector<AstChildRole> childRoles = {},
                                    const qsizetype assignmentTargetCount = -1) {
        AstNode node;
        node.id = m_ast->m_nodes.size();
        node.kind = kind;
        node.range = range;
        node.text = text;
        node.children = children;
        if (childRoles.size() != children.size()) {
            childRoles.fill(AstChildRole::Unknown, children.size());
        }
        node.childRoles = std::move(childRoles);
        node.assignmentTargetCount = assignmentTargetCount;
        node.syntaxNode = syntaxNode;
        m_ast->m_nodes.append(std::move(node));
        return m_ast->m_nodes.size() - 1;
    }

    [[nodiscard]] ParsedNode makeParsed(const AstNodeKind astKind,
                                        const SyntaxKind syntaxKind,
                                        const qsizetype start,
                                        const qsizetype end,
                                        const QString& text = {},
                                        const QVector<AstNodeId>& astChildren = {},
                                        const QVector<SyntaxNodeId>& syntaxChildren = {},
                                        QVector<AstChildRole> childRoles = {},
                                        const qsizetype assignmentTargetCount = -1) {        const SyntaxNodeId syntax = makeSyntax(syntaxKind, start, end, syntaxChildren);
        const AstNodeId ast = makeAst(astKind, rangeFrom(start, end), text,
                                      astChildren, syntax, std::move(childRoles),
                                      assignmentTargetCount);
        return {ast, syntax};
    }

    bool expect(const TokenKind kind, const QString& context,
                QVector<SyntaxNodeId>* missingChildren = nullptr) {
        if (consumeIf(kind)) {
            return true;
        }
        report(QStringLiteral("عقد001"),
               QStringLiteral("متوقع %1 %2").arg(tokenDisplayName(kind), context),
               current().range);
        const SyntaxNodeId missing = makeMissing(kind);
        if (missingChildren != nullptr) {
            missingChildren->append(missing);
        }
        return false;
    }

    void skipStatementSeparators() {
        while (at(TokenKind::Newline) || at(TokenKind::Semicolon)) {
            consume();
        }
    }

    void synchronizeStatement() {
        while (!at(TokenKind::EndOfFile) && !isStatementBoundary(current().kind)) {
            consume();
        }
    }

    [[nodiscard]] ParsedNode parseDecoratedStatement() {
        const qsizetype start = m_mainPosition;

        // Decorators are lexical prefixes. They are intentionally kept out of
        // the AST for now; the declaration/expression that follows remains the
        // semantic node while the lossless syntax tree still retains the
        // decorator token emitted by the lexer.
        while (at(TokenKind::Decorator)) {
            consume();
            skipStatementSeparators();
        }

        if (at(TokenKind::EndOfFile)) {
            return makeParsed(AstNodeKind::ErrorStatement, SyntaxKind::ErrorNode,
                              start, m_mainPosition);
        }
        return parseStatement();
    }

    [[nodiscard]] ParsedNode parseStatement() {
        switch (current().kind) {
        case TokenKind::Decorator: return parseDecoratedStatement();
        case TokenKind::KwFunction: return parseFunctionDeclaration();
        case TokenKind::KwClass: return parseClassDeclaration();
        case TokenKind::KwIf: return parseIfStatement();
        case TokenKind::KwFor: return parseForStatement();
        case TokenKind::KwWith: return parseWithStatement();
        case TokenKind::KwWhile: return parseWhileStatement();
        case TokenKind::KwTry: return parseTryStatement();
        case TokenKind::KwImport: return parseImportStatement();
        case TokenKind::KwFrom: return parseFromImportStatement();
        case TokenKind::KwDelete: return parseDeleteStatement();
        case TokenKind::KwReturn: return parseReturnStatement();
        case TokenKind::KwYield: return parseYieldStatement();
        case TokenKind::KwBreak: return parseControlStatement(AstNodeKind::BreakStatement,
                                                               SyntaxKind::BreakStatement);
        case TokenKind::KwContinue: return parseControlStatement(AstNodeKind::ContinueStatement,
                                                                  SyntaxKind::ContinueStatement);
        case TokenKind::KwPass: return parseControlStatement(AstNodeKind::PassStatement,
                                                              SyntaxKind::PassStatement);
        case TokenKind::KwGlobal: return parseGlobalStatement();
        // case TokenKind::KwWith: return parseWithStatement();
        case TokenKind::Invalid:
            return parseInvalidStatement();
        default:
            return parseAssignmentOrExpressionStatement();
        }
    }

    [[nodiscard]] ParsedNode parseInvalidStatement() {
        const qsizetype start = m_mainPosition;
        report(QStringLiteral("عقد002"), QStringLiteral("رمز غير صحيح في حالة"),
               current().range);
        consume();
        synchronizeStatement();
        return makeParsed(AstNodeKind::ErrorStatement, SyntaxKind::ErrorNode,
                          start, m_mainPosition);
    }

    [[nodiscard]] ParsedNode parseControlStatement(const AstNodeKind astKind,
                                                    const SyntaxKind syntaxKind) {
        const qsizetype start = m_mainPosition;
        consume();
        return makeParsed(astKind, syntaxKind, start, m_mainPosition);
    }

    [[nodiscard]] ParsedNode parseGlobalStatement() {
        const qsizetype start = m_mainPosition;
        consume();
        QVector<AstNodeId> names;
        QVector<SyntaxNodeId> syntaxChildren;
        QVector<AstChildRole> roles;
        do {
            const ParsedNode name = parseNameExpression();
            names.append(name.ast);
            syntaxChildren.append(name.syntax);
            roles.append(AstChildRole::GlobalName);
        } while (consumeIf(TokenKind::Comma));
        return makeParsed(AstNodeKind::GlobalStatement, SyntaxKind::GlobalStatement,
                          start, m_mainPosition, {}, names, syntaxChildren, roles);
    }

    [[nodiscard]] ParsedNode parseReturnStatement() {
        const qsizetype start = m_mainPosition;
        consume();
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        if (!isStatementBoundary(current().kind)) {
            const ParsedNode expression = parseExpression();
            children.append(expression.ast);
            syntaxChildren.append(expression.syntax);
        }
        QVector<AstChildRole> roles;
        if (!children.isEmpty()) {
            roles.append(AstChildRole::ReturnValue);
        }
        return makeParsed(AstNodeKind::ReturnStatement, SyntaxKind::ReturnStatement,
                          start, m_mainPosition, {}, children, syntaxChildren, roles);
    }

    [[nodiscard]] ParsedNode parseYieldStatement() {
        const qsizetype start = m_mainPosition;
        consume(); // انتج
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        if (!isStatementBoundary(current().kind)) {
            const ParsedNode expression = parseExpression();
            children.append(expression.ast);
            syntaxChildren.append(expression.syntax);
        }
        QVector<AstChildRole> roles;
        if (!children.isEmpty()) {
            roles.append(AstChildRole::YieldedValue);
        }
        return makeParsed(AstNodeKind::YieldStatement, SyntaxKind::YieldStatement,
                          start, m_mainPosition, {}, children, syntaxChildren, roles);
    }

    [[nodiscard]] ParsedNode parseDeleteStatement() {
        const qsizetype start = m_mainPosition;
        consume();
        const ParsedNode expression = parseExpression();
        return makeParsed(AstNodeKind::DeleteStatement, SyntaxKind::DeleteStatement,
                          start, m_mainPosition, {}, {expression.ast}, {expression.syntax},
                          {AstChildRole::DeletedValue});
    }

    [[nodiscard]] ParsedNode parseImportStatement() {
        const qsizetype start = m_mainPosition;
        consume(); // استورد
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        do {
            const ParsedNode path = parseDottedName();
            children.append(path.ast);
            syntaxChildren.append(path.syntax);
            if (consumeIf(TokenKind::KwAs)) {
                const ParsedNode alias = parseNameExpression();
                children.append(alias.ast);
                syntaxChildren.append(alias.syntax);
            } else {
                // No explicit alias: the bound name is derived from the module
                // path's first segment by the symbol table. We still record a
                // placeholder so that child roles stay aligned.
                children.append(InvalidAstNodeId);
                syntaxChildren.append(makeMissing(TokenKind::Identifier));
            }
        } while (consumeIf(TokenKind::Comma));

        QVector<AstChildRole> roles;
        for (qsizetype index = 0; index < children.size(); ++index) {
            if (index % 2 == 0) {
                roles.append(AstChildRole::ImportPath);
            } else {
                roles.append(AstChildRole::ImportAlias);
            }
        }
        return makeParsed(AstNodeKind::ImportStatement, SyntaxKind::ImportStatement,
                          start, m_mainPosition, {}, children, syntaxChildren, roles);
    }

    [[nodiscard]] ParsedNode parseFromImportStatement() {
        const qsizetype start = m_mainPosition;
        consume();
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        const ParsedNode path = parseDottedName();
        children.append(path.ast);
        syntaxChildren.append(path.syntax);
        expect(TokenKind::KwImport, QStringLiteral("في حالة من-استورد"),
               &syntaxChildren);

        if (at(TokenKind::Star)) {
            const qsizetype wildcardStart = m_mainPosition;
            consume();
            const ParsedNode wildcard = makeParsed(
                AstNodeKind::ImportWildcard, SyntaxKind::ImportWildcard,
                wildcardStart, m_mainPosition, QStringLiteral("*"));
            children.append(wildcard.ast);
            syntaxChildren.append(wildcard.syntax);
        } else {
            do {
                const ParsedNode imported = parseNameExpression();
                children.append(imported.ast);
                syntaxChildren.append(imported.syntax);
                if (consumeIf(TokenKind::KwAs)) {
                    const ParsedNode alias = parseNameExpression();
                    children.append(alias.ast);
                    syntaxChildren.append(alias.syntax);
                }
            } while (consumeIf(TokenKind::Comma));
        }

        QVector<AstChildRole> roles;
        roles.prepend(AstChildRole::ImportPath);
        for (qsizetype index = 1; index < children.size(); ++index) {
            const AstChildRole previousRole = roles.constLast();
            roles.append(previousRole == AstChildRole::ImportName
                             ? AstChildRole::ImportAlias : AstChildRole::ImportName);
        }
        return makeParsed(AstNodeKind::FromImportStatement, SyntaxKind::FromImportStatement,
                          start, m_mainPosition, {}, children, syntaxChildren, roles);
    }

    [[nodiscard]] ParsedNode parseDottedName() {
        const qsizetype start = m_mainPosition;
        QString name;
        while (at(TokenKind::Dot)) {
            name.append(current().lexeme);
            consume();
        }
        if (at(TokenKind::Identifier)) {
            name.append(current().lexeme);
            consume();
        } else {
            report(QStringLiteral("عقد001"), QStringLiteral("متوقع اسم وحدة"),
                   current().range);
            makeMissing(TokenKind::Identifier);
        }
        while (consumeIf(TokenKind::Dot)) {
            name.append(QChar(u'.'));
            if (!at(TokenKind::Identifier)) {
                report(QStringLiteral("عقد001"),
                       QStringLiteral("متوقع اسم وحدة بعد '.'"), current().range);
                makeMissing(TokenKind::Identifier);
                break;
            }
            name.append(current().lexeme);
            consume();
        }
        return makeParsed(AstNodeKind::NameExpression, SyntaxKind::NameExpression,
                          start, m_mainPosition, name);
    }

    [[nodiscard]] ParsedNode parseFunctionDeclaration() {
        const qsizetype start = m_mainPosition;
        consume();
        const ParsedNode name = parseNameExpression();
        QVector<AstNodeId> children {name.ast};
        QVector<SyntaxNodeId> syntaxChildren {name.syntax};
        const ParsedNode parameters = parseParameterList();
        children.append(parameters.ast);
        syntaxChildren.append(parameters.syntax);
        const ParsedNode suite = parseSuite();
        children.append(suite.ast);
        syntaxChildren.append(suite.syntax);
        return makeParsed(AstNodeKind::FunctionDeclaration, SyntaxKind::FunctionDeclaration,
                          start, m_mainPosition, name.ast != InvalidAstNodeId
                              ? m_ast->node(name.ast).text : QString(),
                          children, syntaxChildren,
                          {AstChildRole::DeclarationName, AstChildRole::ParameterList,
                           AstChildRole::Body});
    }

    [[nodiscard]] ParsedNode parseClassDeclaration() {
        const qsizetype start = m_mainPosition;
        consume();
        const ParsedNode name = parseNameExpression();
        QVector<AstNodeId> children {name.ast};
        QVector<SyntaxNodeId> syntaxChildren {name.syntax};

        if (consumeIf(TokenKind::LParen)) {
            while (!at(TokenKind::RParen) && !at(TokenKind::EndOfFile)) {
                const ParsedNode base = parseExpression(0, false);
                children.append(base.ast);
                syntaxChildren.append(base.syntax);
                if (!consumeIf(TokenKind::Comma)) {
                    break;
                }
            }
            expect(TokenKind::RParen, QStringLiteral("بعد قاعدة الصنف"), &syntaxChildren);
        }

        const ParsedNode suite = parseSuite();
        children.append(suite.ast);
        syntaxChildren.append(suite.syntax);
        QVector<AstChildRole> roles;
        roles.append(AstChildRole::DeclarationName);
        while (roles.size() + 1 < children.size()) {
            roles.append(AstChildRole::Base);
        }
        roles.append(AstChildRole::Body);
        return makeParsed(AstNodeKind::ClassDeclaration, SyntaxKind::ClassDeclaration,
                          start, m_mainPosition, m_ast->node(name.ast).text,
                          children, syntaxChildren, roles);
    }

    [[nodiscard]] ParsedNode parseParameterList() {
        const qsizetype start = m_mainPosition;
        QVector<AstNodeId> parameters;
        QVector<SyntaxNodeId> syntaxChildren;
        expect(TokenKind::LParen, QStringLiteral("قبل معامل المصفوفة"), &syntaxChildren);

        while (!at(TokenKind::RParen) && !at(TokenKind::EndOfFile)) {
            const qsizetype parameterStart = m_mainPosition;
            QString prefix;
            if (consumeIf(TokenKind::DoubleStar)) {
                prefix = QStringLiteral("**");
            } else if (consumeIf(TokenKind::Star)) {
                // A bare '*' separates positional parameters from keyword-only
                // parameters; unlike '*name', it is not itself a parameter.
                if (at(TokenKind::Comma)) {
                    consume();
                    continue;
                }
                prefix = QStringLiteral("*");
            }
            const ParsedNode name = parseNameExpression();
            QVector<AstNodeId> children {name.ast};
            QVector<SyntaxNodeId> parameterSyntax {name.syntax};
            if (consumeIf(TokenKind::Equal)) {
                const ParsedNode defaultValue = parseExpression(0, false);
                children.append(defaultValue.ast);
                parameterSyntax.append(defaultValue.syntax);
            }
            QVector<AstChildRole> parameterRoles {AstChildRole::ParameterName};
            if (children.size() > 1) {
                parameterRoles.append(AstChildRole::DefaultValue);
            }
            const ParsedNode parameter = makeParsed(AstNodeKind::Parameter,
                                                     SyntaxKind::NamePattern,
                                                     parameterStart, m_mainPosition,
                                                     prefix + m_ast->node(name.ast).text,
                                                     children, parameterSyntax,
                                                     parameterRoles);
            parameters.append(parameter.ast);
            syntaxChildren.append(parameter.syntax);
            if (!consumeIf(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RParen, QStringLiteral("بعد معامل المصفوفة"), &syntaxChildren);
        return makeParsed(AstNodeKind::TupleExpression, SyntaxKind::ParameterList,
                          start, m_mainPosition, {}, parameters, syntaxChildren);
    }

    [[nodiscard]] ParsedNode parseIfStatement() {
        const qsizetype start = m_mainPosition;
        consume();
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        const ParsedNode condition = parseExpression();
        children.append(condition.ast);
        syntaxChildren.append(condition.syntax);
        const ParsedNode suite = parseSuite();
        children.append(suite.ast);
        syntaxChildren.append(suite.syntax);

        while (at(TokenKind::KwElseIf)) {
            const qsizetype clauseStart = m_mainPosition;
            consume();
            const ParsedNode elseIfCondition = parseExpression();
            const ParsedNode elseIfSuite = parseSuite();
            const ParsedNode clause = makeParsed(AstNodeKind::ElseIfClause,
                                                 SyntaxKind::ElseIfClause,
                                                 clauseStart, m_mainPosition, {},
                                                 {elseIfCondition.ast, elseIfSuite.ast},
                                                 {elseIfCondition.syntax, elseIfSuite.syntax});
            children.append(clause.ast);
            syntaxChildren.append(clause.syntax);
        }
        if (at(TokenKind::KwElse)) {
            const qsizetype clauseStart = m_mainPosition;
            consume();
            const ParsedNode elseSuite = parseSuite();
            const ParsedNode clause = makeParsed(AstNodeKind::ElseClause,
                                                 SyntaxKind::ElseClause,
                                                 clauseStart, m_mainPosition, {},
                                                 {elseSuite.ast}, {elseSuite.syntax});
            children.append(clause.ast);
            syntaxChildren.append(clause.syntax);
        }
        return makeParsed(AstNodeKind::IfStatement, SyntaxKind::IfStatement,
                          start, m_mainPosition, {}, children, syntaxChildren);
    }

    [[nodiscard]] ParsedNode parseBindingTarget() {
        const qsizetype start = m_mainPosition;
        if (!at(TokenKind::Identifier)) {
            report(QStringLiteral("عقد005"),
                   QStringLiteral("متوقع متغير او مترابطة قبل 'في'"), current().range);
            if (!at(TokenKind::KwIn) && !isExpressionBoundary(current().kind)) {
                consume();
            }
            return makeParsed(AstNodeKind::ErrorExpression, SyntaxKind::ErrorNode,
                              start, m_mainPosition);
        }

        const ParsedNode first = parseNameExpression();
        if (!at(TokenKind::Comma)) {
            return first;
        }

        QVector<AstNodeId> children {first.ast};
        QVector<SyntaxNodeId> syntaxChildren {first.syntax};
        while (consumeIf(TokenKind::Comma)) {
            if (at(TokenKind::KwIn) || isExpressionBoundary(current().kind)) {
                break;
            }
            if (!at(TokenKind::Identifier)) {
                report(QStringLiteral("عقد005"),
                       QStringLiteral("متوقع متغير او مترابطة قبل 'في'"),
                       current().range);
                break;
            }
            const ParsedNode item = parseNameExpression();
            children.append(item.ast);
            syntaxChildren.append(item.syntax);
        }
        if (children.size() == 1) {
            return first;
        }
        return makeParsed(AstNodeKind::TupleExpression, SyntaxKind::TupleExpression,
                          start, m_mainPosition, {}, children, syntaxChildren);
        }

    [[nodiscard]] ParsedNode parseForStatement() {
        const qsizetype start = m_mainPosition;
        consume();
        const ParsedNode target = parseBindingTarget();
        expect(TokenKind::KwIn, QStringLiteral("في حالة لكل"));
        const ParsedNode iterable = parseExpression();
        const ParsedNode suite = parseSuite();
        QVector<AstNodeId> children {target.ast, iterable.ast, suite.ast};
        QVector<SyntaxNodeId> syntaxChildren {target.syntax, iterable.syntax, suite.syntax};
        QVector<AstChildRole> roles {
            AstChildRole::Target, AstChildRole::Iterable, AstChildRole::Body};
        if (at(TokenKind::KwThen)) {
            const ParsedNode elseClause = parseLoopElseClause();
            children.append(elseClause.ast);
            syntaxChildren.append(elseClause.syntax);
            roles.append(AstChildRole::ElseBody);
        }
        return makeParsed(AstNodeKind::ForStatement, SyntaxKind::ForStatement,
                          start, m_mainPosition, {}, children, syntaxChildren, roles);
    }

    [[nodiscard]] ParsedNode parseLoopElseClause() {
        const qsizetype start = m_mainPosition;
        consume(); // اذن
        const ParsedNode suite = parseSuite();
        return makeParsed(AstNodeKind::ElseClause, SyntaxKind::ElseClause,
                          start, m_mainPosition, {}, {suite.ast}, {suite.syntax},
                          {AstChildRole::Body});
    }

    [[nodiscard]] ParsedNode parseWithStatement() {
        const qsizetype start = m_mainPosition;
        consume(); // عند
        const ParsedNode context = parseExpression();
        QVector<AstNodeId> children {context.ast};
        QVector<SyntaxNodeId> syntaxChildren {context.syntax};
        QVector<AstChildRole> roles {AstChildRole::ContextExpression};
        if (consumeIf(TokenKind::KwAs)) {
            const ParsedNode target = parseBindingTarget();
            children.append(target.ast);
            syntaxChildren.append(target.syntax);
            roles.append(AstChildRole::Target);
        }
        const ParsedNode suite = parseSuite();
        children.append(suite.ast);
        syntaxChildren.append(suite.syntax);
        roles.append(AstChildRole::Body);
        return makeParsed(AstNodeKind::WithStatement, SyntaxKind::WithStatement,
                          start, m_mainPosition, {}, children, syntaxChildren, roles);
    }

    [[nodiscard]] ParsedNode parseWhileStatement() {
        const qsizetype start = m_mainPosition;
        consume();
        const ParsedNode condition = parseExpression();
        const ParsedNode suite = parseSuite();
        QVector<AstNodeId> children {condition.ast, suite.ast};
        QVector<SyntaxNodeId> syntaxChildren {condition.syntax, suite.syntax};
        QVector<AstChildRole> roles {AstChildRole::Condition, AstChildRole::Body};
        if (at(TokenKind::KwThen)) {
            const ParsedNode elseClause = parseLoopElseClause();
            children.append(elseClause.ast);
            syntaxChildren.append(elseClause.syntax);
            roles.append(AstChildRole::ElseBody);
        }
        return makeParsed(AstNodeKind::WhileStatement, SyntaxKind::WhileStatement,
                          start, m_mainPosition, {}, children, syntaxChildren, roles);
    }

    [[nodiscard]] ParsedNode parseTryStatement() {
        const qsizetype start = m_mainPosition;
        consume();
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        const ParsedNode trySuite = parseSuite();
        children.append(trySuite.ast);
        syntaxChildren.append(trySuite.syntax);

        while (at(TokenKind::KwExcept)) {
            const qsizetype clauseStart = m_mainPosition;
            consume();
            QVector<AstNodeId> clauseChildren;
            QVector<SyntaxNodeId> clauseSyntax;
            QVector<AstChildRole> clauseRoles;
            if (!at(TokenKind::Colon)) {
                const ParsedNode errorType = parseExpression();
                clauseChildren.append(errorType.ast);
                clauseSyntax.append(errorType.syntax);
                clauseRoles.append(AstChildRole::ExceptionType);
                if (consumeIf(TokenKind::KwAs)) {
                    const ParsedNode target = parseBindingTarget();
                    clauseChildren.append(target.ast);
                    clauseSyntax.append(target.syntax);
                    clauseRoles.append(AstChildRole::ExceptionTarget);
                }
            }
            const ParsedNode exceptSuite = parseSuite();
            clauseChildren.append(exceptSuite.ast);
            clauseSyntax.append(exceptSuite.syntax);
            clauseRoles.append(AstChildRole::Body);
            const ParsedNode clause = makeParsed(AstNodeKind::ExceptClause,
                                                 SyntaxKind::ExceptClause,
                                                 clauseStart, m_mainPosition, {},
                                                 clauseChildren, clauseSyntax,
                                                 clauseRoles);
            children.append(clause.ast);
            syntaxChildren.append(clause.syntax);
        }
        if (at(TokenKind::KwElse)) {
            const qsizetype clauseStart = m_mainPosition;
            consume();
            const ParsedNode elseSuite = parseSuite();
            const ParsedNode clause = makeParsed(AstNodeKind::ElseClause,
                                                 SyntaxKind::ElseClause,
                                                 clauseStart, m_mainPosition, {},
                                                 {elseSuite.ast}, {elseSuite.syntax});
            children.append(clause.ast);
            syntaxChildren.append(clause.syntax);
        }
        if (at(TokenKind::KwFinally)) {
            const qsizetype clauseStart = m_mainPosition;
            consume();
            const ParsedNode finallySuite = parseSuite();
            const ParsedNode clause = makeParsed(AstNodeKind::FinallyClause,
                                                 SyntaxKind::FinallyClause,
                                                 clauseStart, m_mainPosition, {},
                                                 {finallySuite.ast}, {finallySuite.syntax});
            children.append(clause.ast);
            syntaxChildren.append(clause.syntax);
        }
        return makeParsed(AstNodeKind::TryStatement, SyntaxKind::TryStatement,
                          start, m_mainPosition, {}, children, syntaxChildren);
    }

    [[nodiscard]] ParsedNode parseSuite() {
        const qsizetype start = m_mainPosition;
        QVector<SyntaxNodeId> syntaxChildren;
        expect(TokenKind::Colon, QStringLiteral("قبل الكتلة"), &syntaxChildren);
        QVector<AstNodeId> statements;

        if (consumeIf(TokenKind::Newline)) {
            // Comment-only and blank lines are represented as additional
            // newlines before the first indentation token. Skip them before
            // requiring the suite's Indent marker.
            skipStatementSeparators();
            expect(TokenKind::Indent, QStringLiteral("لبدأ مسافة بادئة للكتلة"),
                   &syntaxChildren);
            skipStatementSeparators();
            while (!at(TokenKind::Dedent) && !at(TokenKind::EndOfFile)) {
                const qsizetype before = m_mainPosition;
                const ParsedNode statement = parseStatement();
                statements.append(statement.ast);
                syntaxChildren.append(statement.syntax);
                if (m_mainPosition == before) {
                    report(QStringLiteral("عقد099"),
                           QStringLiteral("لم يحرز المحلل اللغوي أي تقدم في الكتلة"),
                           current().range);
                    consume();
                }
                skipStatementSeparators();
            }
            expect(TokenKind::Dedent, QStringLiteral("لإنهاء كتلة متداخلة"),
                   &syntaxChildren);
        } else if (!isStatementBoundary(current().kind)) {
            const ParsedNode statement = parseStatement();
            statements.append(statement.ast);
            syntaxChildren.append(statement.syntax);
        } else {
            report(QStringLiteral("عقد002"), QStringLiteral("متوقع كتلة"),
                   current().range);
        }
        return makeParsed(AstNodeKind::Suite, SyntaxKind::Suite,
                          start, m_mainPosition, {}, statements, syntaxChildren);
    }

    [[nodiscard]] ParsedNode parseAssignmentOrExpressionStatement() {
        const qsizetype start = m_mainPosition;
        const ParsedNode left = parseExpression();

        QVector<AstNodeId> targets;
        QVector<SyntaxNodeId> targetSyntax;
        const bool leftIsTuple = left.ast != InvalidAstNodeId
                                 && m_ast->node(left.ast).kind == AstNodeKind::TupleExpression;
        if (leftIsTuple && isAssignmentOperator(current().kind)) {
            const AstNode& tuple = m_ast->node(left.ast);
            for (const AstNodeId child : tuple.children) {
                targets.append(child);
                targetSyntax.append(m_ast->node(child).syntaxNode);
            }
        } else {
            targets.append(left.ast);
            targetSyntax.append(left.syntax);
        }

        if (isAssignmentOperator(current().kind)) {
            const QString assignmentOperator = current().lexeme;
            consume();
            ParsedNode right = parseExpression();
            // Plain assignments may be chained: س = ص = 1. Each expression
            // before the final value is an additional assignment target.
            while (assignmentOperator == QStringLiteral("=")
                   && at(TokenKind::Equal)) {
                targets.append(right.ast);
                targetSyntax.append(right.syntax);
                consume();
                right = parseExpression();
            }
            targets.append(right.ast);
            targetSyntax.append(right.syntax);
            QVector<AstChildRole> roles;
            roles.fill(AstChildRole::Target, targets.size() - 1);
            roles.append(AstChildRole::Value);
            return makeParsed(AstNodeKind::AssignmentStatement,
                              SyntaxKind::AssignmentStatement,
                              start, m_mainPosition, assignmentOperator,
                              targets, targetSyntax, roles, targets.size() - 1);
        }

        return makeParsed(AstNodeKind::ExpressionStatement,
                          SyntaxKind::ExpressionStatement,
                          start, m_mainPosition, {}, {left.ast}, {left.syntax});
    }

    /// Parses ordered comprehension clauses. Each later clause becomes the
    /// preceding clause's element so its iterable sees earlier bindings and
    /// the final result expression sees every enclosing clause's bindings.
    [[nodiscard]] ParsedNode parseComprehensionClause(const ParsedNode& element) {
        const qsizetype clauseStart = startFor(element);
        consume(); //لكل
        QVector<AstNodeId> children {element.ast};
        QVector<SyntaxNodeId> syntaxChildren {element.syntax};
        const ParsedNode target = parseBindingTarget();
        expect(TokenKind::KwIn, QStringLiteral("في حاوية ضمنية"), &syntaxChildren);
        // The iterable in a comprehension clause is itself an expression that
        // may contain enclosed nested comprehensions. Top-level 'لكل' and
        // 'اذا' start the next clause or filter, not part of this iterable.
        const ParsedNode iterable = parseExpression(0, true, false, false);
        children.append(target.ast);
        children.append(iterable.ast);
        syntaxChildren.append({target.syntax, iterable.syntax});
        QVector<AstChildRole> roles {
            AstChildRole::Element, AstChildRole::Target, AstChildRole::Iterable};
        while (consumeIf(TokenKind::KwIf)) {
            const ParsedNode condition = parseExpression(0, false, false, false);
            children.append(condition.ast);
            syntaxChildren.append(condition.syntax);
            roles.append(AstChildRole::Condition);
        }
        if (at(TokenKind::KwFor)) {
            const ParsedNode nextClause = parseComprehensionClause(element);
            children[0] = nextClause.ast;
            syntaxChildren[0] = nextClause.syntax;
        }
        return makeParsed(AstNodeKind::ComprehensionExpression,
                          SyntaxKind::ComprehensionExpression,
                          clauseStart, m_mainPosition, {}, children, syntaxChildren,
                          roles);
    }

    [[nodiscard]] ParsedNode parseExpression(const int minimumBindingPower = 0,
                                             const bool allowBraceLessTuple = true,
                                             const bool allowComprehension = true,
                                             const bool allowInlineConditional = true) {

        ParsedNode left = parsePrefixExpression();
        while (!isExpressionBoundary(current().kind)) {
            if (const ParsedNode postfix = parsePostfixExpression(left);
                postfix.ast != InvalidAstNodeId) {
                left = postfix;
                continue;
            }

            TokenKind operatorKind = current().kind;
            QString operatorText = current().lexeme;
            int bindingPower = binaryBindingPower(operatorKind);
            if (operatorKind == TokenKind::KwNot && current(1).kind == TokenKind::KwIn) {
                bindingPower = 30;
                operatorText += QChar(u' ') + current(1).lexeme;
            }
            if (bindingPower < minimumBindingPower) {
                break;
            }

            const qsizetype start = m_mainPosition;
            consume();
            if (operatorKind == TokenKind::KwNot && at(TokenKind::KwIn)) {
                consume();
            }
            const int rightBindingPower = operatorKind == TokenKind::Power
                ? bindingPower : bindingPower + 1;
            const ParsedNode right = parseExpression(rightBindingPower, false, false,
                                                     allowInlineConditional);
            left = makeParsed(AstNodeKind::BinaryExpression, SyntaxKind::BinaryExpression,
                              startFor(left), m_mainPosition, operatorText,
                              {left.ast, right.ast}, {left.syntax, right.syntax});
        }

        // A comma-separated element belongs to one comprehension. Do not let
        // the final tuple item (or a binary operand) consume its clause first.
        if (minimumBindingPower == 0 && allowBraceLessTuple && at(TokenKind::Comma)) {
            left = parseTupleSequence(left, allowInlineConditional);
        }
        if (minimumBindingPower == 0 && allowComprehension) {
            while (at(TokenKind::KwFor)) {
                left = parseComprehensionClause(left);
            }
        }

        if (minimumBindingPower == 0 && allowInlineConditional && at(TokenKind::KwIf)) {
            const qsizetype conditionalStart = startFor(left);
            consume();
            const ParsedNode condition = parseExpression(0, false);
            expect(TokenKind::KwElse, QStringLiteral("in an inline conditional expression"));
            const ParsedNode alternative = parseExpression(minimumBindingPower,
                                                           allowBraceLessTuple,
                                                           allowComprehension,
                                                           allowInlineConditional);
            left = makeParsed(AstNodeKind::BinaryExpression, SyntaxKind::BinaryExpression,
                              conditionalStart, m_mainPosition,
                              QStringLiteral("اذا/والا"),
                              {left.ast, condition.ast, alternative.ast},
                              {left.syntax, condition.syntax, alternative.syntax});
        }

        if (allowBraceLessTuple && at(TokenKind::Comma)) {
            left = parseTupleSequence(left, allowInlineConditional);
        }
        return left;
    }

    [[nodiscard]] ParsedNode parseTupleSequence(const ParsedNode& first,
                                                const bool allowInlineConditional = true) {
        const qsizetype start = startFor(first);
        QVector<AstNodeId> children {first.ast};
        QVector<SyntaxNodeId> syntaxChildren {first.syntax};
        while (consumeIf(TokenKind::Comma)) {
            if (at(TokenKind::RParen) || at(TokenKind::RBracket) || at(TokenKind::RBrace)
                || at(TokenKind::Newline) || at(TokenKind::Semicolon)
                || at(TokenKind::Dedent) || at(TokenKind::EndOfFile)) {
                // Trailing comma: the tuple ends here without another element.
                break;
            }
            const ParsedNode item = parseExpression(0, false, false, allowInlineConditional);
            children.append(item.ast);
            syntaxChildren.append(item.syntax);
        }
        if (children.size() <= 1) {
            return first;
        }
        return makeParsed(AstNodeKind::TupleExpression, SyntaxKind::TupleExpression,
                          start, m_mainPosition, {}, children, syntaxChildren);
    }

    [[nodiscard]] qsizetype startFor(const ParsedNode& parsed) const {
        if (parsed.syntax == InvalidSyntaxNodeId) {
            return m_mainPosition;
        }
        const qsizetype rawStart = m_tree->m_nodes.at(parsed.syntax).firstToken;
        for (qsizetype index = 0; index < m_mainTokenIndices.size(); ++index) {
            if (m_mainTokenIndices.at(index) == rawStart) {
                return index;
            }
        }
        return m_mainPosition;
    }

    [[nodiscard]] ParsedNode parsePostfixExpression(const ParsedNode& base) {
        if (at(TokenKind::LParen)) {
            return parseCallExpression(base);
        }
        if (at(TokenKind::Dot)) {
            return parseMemberExpression(base);
        }
        if (at(TokenKind::LBracket)) {
            return parseIndexOrSliceExpression(base);
        }
        return {};
    }

    [[nodiscard]] ParsedNode parseCallArgument() {
        const qsizetype start = m_mainPosition;
        AstNodeKind argumentKind = AstNodeKind::ExpressionStatement;
        SyntaxKind syntaxKind = SyntaxKind::ArgumentList;
        QString name;

        if (consumeIf(TokenKind::DoubleStar)) {
            argumentKind = AstNodeKind::DoubleStarArgument;
        } else if (consumeIf(TokenKind::Star)) {
            argumentKind = AstNodeKind::StarArgument;
        } else if (at(TokenKind::Identifier) && current(1).kind == TokenKind::Equal) {
            argumentKind = AstNodeKind::KeywordArgument;
            name = current().lexeme;
            consume();
            consume();
        }

        const ParsedNode value = parseExpression(0, false);
        if (argumentKind == AstNodeKind::ExpressionStatement) {
            return value;
        }
        return makeParsed(argumentKind, syntaxKind, start, m_mainPosition, name,
                          {value.ast}, {value.syntax});
    }

    [[nodiscard]] ParsedNode parseCallExpression(const ParsedNode& callee) {
        const qsizetype start = startFor(callee);
        QVector<AstNodeId> children {callee.ast};
        QVector<SyntaxNodeId> syntaxChildren {callee.syntax};
        consume();
        while (!at(TokenKind::RParen) && !at(TokenKind::EndOfFile)) {
            const ParsedNode argument = parseCallArgument();
            children.append(argument.ast);
            syntaxChildren.append(argument.syntax);
            if (!consumeIf(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RParen, QStringLiteral("بعد استدعاء الوسيطات"), &syntaxChildren);
        QVector<AstChildRole> roles;
        roles.append(AstChildRole::Callee);
        roles.fill(AstChildRole::Argument, children.size() - 1);
        roles.prepend(AstChildRole::Callee);
        return makeParsed(AstNodeKind::CallExpression, SyntaxKind::CallExpression,
                          start, m_mainPosition, {}, children, syntaxChildren, roles);
    }

    [[nodiscard]] ParsedNode parseMemberExpression(const ParsedNode& base) {
        const qsizetype start = startFor(base);
        consume();
        const ParsedNode member = parseNameExpression();
        return makeParsed(AstNodeKind::MemberExpression, SyntaxKind::MemberExpression,
                          start, m_mainPosition, m_ast->node(member.ast).text,
                          {base.ast, member.ast}, {base.syntax, member.syntax},
                          {AstChildRole::MemberBase, AstChildRole::MemberName});
    }

    [[nodiscard]] ParsedNode parseIndexOrSliceExpression(const ParsedNode& base) {
        const qsizetype start = startFor(base);
        QVector<AstNodeId> children {base.ast};
        QVector<SyntaxNodeId> syntaxChildren {base.syntax};
        consume();
        AstNodeKind astKind = AstNodeKind::IndexExpression;
        SyntaxKind syntaxKind = SyntaxKind::IndexExpression;

        if (!at(TokenKind::Colon) && !at(TokenKind::RBracket)) {
            const ParsedNode first = parseExpression();
            children.append(first.ast);
            syntaxChildren.append(first.syntax);
        }
        if (consumeIf(TokenKind::Colon)) {
            astKind = AstNodeKind::SliceExpression;
            syntaxKind = SyntaxKind::SliceExpression;
            if (!at(TokenKind::RBracket)) {
                const ParsedNode second = parseExpression();
                children.append(second.ast);
                syntaxChildren.append(second.syntax);
            }
            if (consumeIf(TokenKind::Colon) && !at(TokenKind::RBracket)) {
                const ParsedNode step = parseExpression();
                children.append(step.ast);
                syntaxChildren.append(step.syntax);
            }
        }
        expect(TokenKind::RBracket, QStringLiteral("بعد مؤشر او قاطع"), &syntaxChildren);
        return makeParsed(astKind, syntaxKind, start, m_mainPosition, {},
                          children, syntaxChildren);
    }

    [[nodiscard]] ParsedNode parsePrefixExpression() {
        const qsizetype start = m_mainPosition;
        const Token token = current();
        switch (token.kind) {
        case TokenKind::Identifier:
            return parseNameExpression();
        case TokenKind::IntegerLiteral:
            consume();
            return makeParsed(AstNodeKind::IntegerLiteral, SyntaxKind::LiteralExpression,
                              start, m_mainPosition, token.lexeme);
        case TokenKind::FloatLiteral:
            consume();
            return makeParsed(AstNodeKind::FloatLiteral, SyntaxKind::LiteralExpression,
                              start, m_mainPosition, token.lexeme);
        case TokenKind::StringLiteral:
            consume();
            return makeParsed(AstNodeKind::StringLiteral, SyntaxKind::LiteralExpression,
                              start, m_mainPosition, token.lexeme);
        case TokenKind::RawStringLiteral:
        case TokenKind::BinaryStringLiteral:
            consume();
            return makeParsed(token.kind == TokenKind::RawStringLiteral
                                  ? AstNodeKind::RawStringLiteral : AstNodeKind::BinaryStringLiteral,
                              SyntaxKind::LiteralExpression,
                              start, m_mainPosition, token.lexeme);
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
            consume();
            return makeParsed(AstNodeKind::BooleanLiteral, SyntaxKind::LiteralExpression,
                              start, m_mainPosition, token.lexeme);
        case TokenKind::KwNull:
            consume();
            return makeParsed(AstNodeKind::NullLiteral, SyntaxKind::LiteralExpression,
                              start, m_mainPosition, token.lexeme);
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::KwNot:
        case TokenKind::BitwiseNot:
            consume();
            return parseUnaryExpression(start, token.lexeme);
        case TokenKind::LParen:
            return parseParenthesizedOrTuple();
        case TokenKind::LBracket:
            return parseCollection(AstNodeKind::ListExpression, SyntaxKind::ListExpression,
                                   TokenKind::LBracket, TokenKind::RBracket);
        case TokenKind::LBrace:
            return parseBraceCollection();
        case TokenKind::KwLambda:
            return parseLambdaExpression();
        case TokenKind::FStringStart:
            return parseFormattedString();
        default:
            report(QStringLiteral("عقد003"), QStringLiteral("متوقع تعبير"),
                   token.range);
            if (!isExpressionBoundary(token.kind)) {
                consume();
            }
            return makeParsed(AstNodeKind::ErrorExpression, SyntaxKind::ErrorNode,
                              start, m_mainPosition, token.lexeme);
        }
    }

    [[nodiscard]] ParsedNode parseNameExpression() {
        const qsizetype start = m_mainPosition;
        if (!at(TokenKind::Identifier)) {
            report(QStringLiteral("عقد001"), QStringLiteral("متوقع اسم"),
                   current().range);
            const SyntaxNodeId missing = makeMissing(TokenKind::Identifier);
            const AstNodeId ast = makeAst(AstNodeKind::ErrorExpression,
                                          rangeFrom(start, start), QString(), {}, missing);
            return {ast, missing};
        }
        const QString name = current().lexeme;
        consume();
        return makeParsed(AstNodeKind::NameExpression, SyntaxKind::NameExpression,
                          start, m_mainPosition, name);
    }

    [[nodiscard]] ParsedNode parseUnaryExpression(const qsizetype start,
                                                   const QString& operatorText) {
        const ParsedNode operand = parseExpression(55, false, false);
        return makeParsed(AstNodeKind::UnaryExpression, SyntaxKind::UnaryExpression,
                          start, m_mainPosition, operatorText,
                          {operand.ast}, {operand.syntax});
    }

    [[nodiscard]] ParsedNode parseParenthesizedOrTuple() {
        const qsizetype start = m_mainPosition;
        consume();
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        bool isTuple = false;
        if (!at(TokenKind::RParen)) {
            const ParsedNode first = parseExpression(0, true);
            children.append(first.ast);
            syntaxChildren.append(first.syntax);

            if (at(TokenKind::KwFor)) {
                const qsizetype comprehensionStart = startFor(first);
                consume();
                QVector<AstNodeId> comprehensionChildren;
                QVector<SyntaxNodeId> comprehensionSyntax;
                const ParsedNode target = parseBindingTarget();
                expect(TokenKind::KwIn, QStringLiteral("في حاوية توليدية"),
                       &comprehensionSyntax);
                const ParsedNode iterable = parseExpression(0, true);
                comprehensionChildren = {first.ast, target.ast, iterable.ast};
                comprehensionSyntax.append({target.syntax, iterable.syntax});
                expect(TokenKind::RParen, QStringLiteral("لإغلاق تعبير قوسي"),
                       &comprehensionSyntax);
                return makeParsed(AstNodeKind::ComprehensionExpression,
                                  SyntaxKind::ComprehensionExpression,
                                  comprehensionStart, m_mainPosition, {},
                                  comprehensionChildren, comprehensionSyntax,
                                  {AstChildRole::Element, AstChildRole::Target,
                                   AstChildRole::Iterable});
            }

            while (consumeIf(TokenKind::Comma)) {
                isTuple = true;
                if (at(TokenKind::RParen)) {
                    break;
                }
                const ParsedNode item = parseExpression();
                children.append(item.ast);
                syntaxChildren.append(item.syntax);
            }
        } else {
            isTuple = true;
        }
        expect(TokenKind::RParen, QStringLiteral("بعد التعبير بين قوسين"),
               &syntaxChildren);
        if (!isTuple && children.size() == 1) {
            return {children.constFirst(), syntaxChildren.constFirst()};
        }
        return makeParsed(AstNodeKind::TupleExpression, SyntaxKind::TupleExpression,
                          start, m_mainPosition, {}, children, syntaxChildren);
    }

    [[nodiscard]] ParsedNode parseCollection(const AstNodeKind astKind,
                                             const SyntaxKind syntaxKind,
                                             const TokenKind opener,
                                             const TokenKind closer) {
        const qsizetype start = m_mainPosition;
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        expect(opener, QStringLiteral("لبدأ حاوية"), &syntaxChildren);
        while (!at(closer) && !at(TokenKind::EndOfFile)) {
            const ParsedNode item = parseExpression();
            children.append(item.ast);
            syntaxChildren.append(item.syntax);

            if (at(TokenKind::KwFor)) {
                const qsizetype comprehensionStart = startFor(item);
                consume();
                const ParsedNode target = parseBindingTarget();
                expect(TokenKind::KwIn, QStringLiteral("في تعبير توليدي"), &syntaxChildren);
                const ParsedNode iterable = parseExpression();
                const ParsedNode comprehension = makeParsed(
                    AstNodeKind::ComprehensionExpression,
                    SyntaxKind::ComprehensionExpression,
                    comprehensionStart, m_mainPosition, {},
                    {item.ast, target.ast, iterable.ast},
                    {item.syntax, target.syntax, iterable.syntax},
                    {AstChildRole::Element, AstChildRole::Target, AstChildRole::Iterable});                children = {comprehension.ast};
                syntaxChildren.append(comprehension.syntax);
                break;
            }
            if (!consumeIf(TokenKind::Comma)) {
                break;
            }
        }
        expect(closer, QStringLiteral("لإغلاق الحاوية"), &syntaxChildren);
        return makeParsed(astKind, syntaxKind, start, m_mainPosition, {},
                          children, syntaxChildren);
    }

    [[nodiscard]] ParsedNode parseBraceCollection() {
        const qsizetype start = m_mainPosition;
        consume();
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        bool isMap = false;
        while (!at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
            const ParsedNode keyOrValue = parseExpression(0, false);
            children.append(keyOrValue.ast);
            syntaxChildren.append(keyOrValue.syntax);
            if (consumeIf(TokenKind::Colon)) {
                isMap = true;
                const ParsedNode value = parseExpression(0, false);
                children.append(value.ast);
                syntaxChildren.append(value.syntax);
            }
            if (!consumeIf(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RBrace, QStringLiteral("لإغلاق الحاوية"), &syntaxChildren);
        return makeParsed(isMap ? AstNodeKind::MapExpression : AstNodeKind::SetExpression,
                          isMap ? SyntaxKind::MapExpression : SyntaxKind::SetExpression,
                          start, m_mainPosition, {}, children, syntaxChildren);
    }

    [[nodiscard]] ParsedNode parseLambdaExpression() {
        const qsizetype start = m_mainPosition;
        consume();
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        while (!at(TokenKind::Colon) && !at(TokenKind::EndOfFile)) {
            const qsizetype parameterStart = m_mainPosition;
            const ParsedNode name = parseNameExpression();
            QVector<AstNodeId> parameterChildren {name.ast};
            QVector<SyntaxNodeId> parameterSyntax {name.syntax};
            QVector<AstChildRole> parameterRoles {AstChildRole::ParameterName};
            if (consumeIf(TokenKind::Equal)) {
                const ParsedNode defaultValue = parseExpression(0, false);
                parameterChildren.append(defaultValue.ast);
                parameterSyntax.append(defaultValue.syntax);
                parameterRoles.append(AstChildRole::DefaultValue);
            }
            const ParsedNode parameter = makeParsed(
                AstNodeKind::Parameter, SyntaxKind::NamePattern,
                parameterStart, m_mainPosition,
                name.ast != InvalidAstNodeId ? m_ast->node(name.ast).text : QString(),
                parameterChildren, parameterSyntax, parameterRoles);
            children.append(parameter.ast);
            syntaxChildren.append(parameter.syntax);
            if (!consumeIf(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::Colon, QStringLiteral("بعد معامل دالة خطية"), &syntaxChildren);
        const ParsedNode body = parseExpression();
        children.append(body.ast);
        syntaxChildren.append(body.syntax);
        return makeParsed(AstNodeKind::LambdaExpression, SyntaxKind::LambdaExpression,
                          start, m_mainPosition, {}, children, syntaxChildren);
    }

    [[nodiscard]] ParsedNode parseFormattedString() {
        const qsizetype start = m_mainPosition;
        consume();
        QVector<AstNodeId> children;
        QVector<SyntaxNodeId> syntaxChildren;
        while (!at(TokenKind::FStringEnd) && !at(TokenKind::EndOfFile)) {
            const qsizetype partStart = m_mainPosition;
            if (at(TokenKind::FStringText)) {
                const QString text = current().lexeme;
                consume();
                const ParsedNode part = makeParsed(AstNodeKind::FormattedStringText,
                                                   SyntaxKind::FormattedStringPart,
                                                   partStart, m_mainPosition, text);
                children.append(part.ast);
                syntaxChildren.append(part.syntax);
                continue;
            }
            if (consumeIf(TokenKind::InterpolationStart)) {
                QVector<AstNodeId> interpolationChildren;
                QVector<SyntaxNodeId> interpolationSyntax;
                const ParsedNode expression = parseExpression();
                interpolationChildren.append(expression.ast);
                interpolationSyntax.append(expression.syntax);
                if (consumeIf(TokenKind::Colon)) {
                    if (at(TokenKind::FStringFormat)) {
                        const qsizetype formatStart = m_mainPosition;
                        const QString rawFormat = current().lexeme;
                        consume();
                        const ParsedNode format = makeParsed(
                            AstNodeKind::FormattedStringFormat,
                            SyntaxKind::FormattedStringPart,
                            formatStart, m_mainPosition, rawFormat);
                        interpolationChildren.append(format.ast);
                        interpolationSyntax.append(format.syntax);
                    }
                }
                expect(TokenKind::InterpolationEnd,
                       QStringLiteral("لإغلاق النص المنسق"),
                       &interpolationSyntax);
                const ParsedNode interpolation = makeParsed(
                    AstNodeKind::FormattedStringInterpolation,
                    SyntaxKind::FormattedStringPart,
                    partStart, m_mainPosition, {}, interpolationChildren,
                    interpolationSyntax);
                children.append(interpolation.ast);
                syntaxChildren.append(interpolation.syntax);
                continue;
            }
            report(QStringLiteral("عقد004"),
                   QStringLiteral("لإغلاق النص المنسق"), current().range);
            const qsizetype errorStart = m_mainPosition;
            consume();
            const ParsedNode error = makeParsed(AstNodeKind::ErrorExpression,
                                                SyntaxKind::ErrorNode,
                                                errorStart, m_mainPosition);
            children.append(error.ast);
            syntaxChildren.append(error.syntax);
        }
        expect(TokenKind::FStringEnd, QStringLiteral("لإغلاق النص المنسق"),
               &syntaxChildren);
        return makeParsed(AstNodeKind::FormattedStringExpression,
                          SyntaxKind::FormattedStringExpression,
                          start, m_mainPosition, {}, children, syntaxChildren);
    }
};

const SyntaxNode& SyntaxTree::root() const {
    Q_ASSERT(m_rootId >= 0 && m_rootId < m_nodes.size());
    return m_nodes.at(m_rootId);
}

const AstNode& AstModule::root() const {
    Q_ASSERT(m_rootId >= 0 && m_rootId < m_nodes.size());
    return m_nodes.at(m_rootId);
}

const AstNode& AstModule::node(const AstNodeId id) const {
    Q_ASSERT(id >= 0 && id < m_nodes.size());
    return m_nodes.at(id);
}

ParseResult TaifParser::parse(const QString& source,
                              const LexResult& lexicalResult,
                              const quint64 documentRevision) const {
    return TaifParserImpl(source, lexicalResult, documentRevision).run();
}

ParseResult TaifParser::parse(const QString& source,
                              const quint64 documentRevision) const {
    const LexResult lexicalResult = TaifLexer().lex(source);
    return parse(source, lexicalResult, documentRevision);
}

IncrementalParseResult TaifParser::reparse(const ParseResult& previous,
                                           const QString& newSource,
                                           const TextEdit& edit) const {
    IncrementalParseResult incremental;
    const quint64 revision = edit.resultRevision != 0
        ? edit.resultRevision : previous.documentRevision + 1;
    incremental.result = parse(newSource, revision);
    incremental.usedFullReparseFallback = true;
    incremental.reusedSyntaxNodeCount = 0;
    return incremental;
}
