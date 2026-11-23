#include "SemanticHoverProvider.h"

namespace {

bool containsOffset(const SourceRange& range, const qsizetype offset) {
    return offset >= range.begin.offset && offset < range.end.offset;
}

qsizetype rangeWidth(const SourceRange& range) {
    return range.end.offset - range.begin.offset;
}

QString typeLabelForSymbol(const SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Builtin: return QStringLiteral("ضمني");
    case SymbolKind::Module: return QStringLiteral("وحدة");
    case SymbolKind::Function: return QStringLiteral("دالة");
    case SymbolKind::Class: return QStringLiteral("صنف");
    case SymbolKind::Attribute: return QStringLiteral("صفة");
    case SymbolKind::Parameter: return QStringLiteral("معامل");
    case SymbolKind::Local: return QStringLiteral("متغير محلي");
    case SymbolKind::ImportModule: return QStringLiteral("وحدة مستوردة");
    case SymbolKind::ImportMember: return QStringLiteral("اسم مستورد");
    case SymbolKind::LoopVariable: return QStringLiteral("متغير حلقة");
    case SymbolKind::ComprehensionVariable: return QStringLiteral("متغير ضمني");
    case SymbolKind::External: return QStringLiteral("رمز خارجي");
    case SymbolKind::Error: return QStringLiteral("رمز غير صالح");
    }
    return QStringLiteral("رمز");
}

AstNodeId childWithRole(const AstNode& node, const AstChildRole role) {
    const qsizetype count = qMin(node.children.size(), node.childRoles.size());
    for (qsizetype index = 0; index < count; ++index) {
        if (node.childRoles.at(index) == role) {
            return node.children.at(index);
        }
    }
    return InvalidAstNodeId;
}

QString sourceText(const QString& source, const SourceRange& range) {
    const qsizetype begin = range.begin.offset;
    const qsizetype length = range.end.offset - begin;
    if (begin < 0 || length < 0 || begin + length > source.size()) {
        return {};
    }
    return source.mid(begin, length);
}

QString unquoteDocumentation(QString literal) {
    literal = literal.trimmed();
    static const QStringList tripleDelimiters {
        QStringLiteral("\"\"\""), QStringLiteral("'''")
    };
    for (const QString& delimiter : tripleDelimiters) {
        if (literal.startsWith(delimiter) && literal.endsWith(delimiter)
            && literal.size() >= delimiter.size() * 2) {
            return literal.mid(delimiter.size(), literal.size() - delimiter.size() * 2).trimmed();
        }
    }
    if (literal.size() >= 2 && ((literal.front() == u'\'' && literal.back() == u'\'')
        || (literal.front() == u'\"' && literal.back() == u'\"'))) {
        return literal.mid(1, literal.size() - 2).trimmed();
    }
    return literal;
}

QString truncateDocumentation(QString text) {
    constexpr qsizetype maximumLength = 800;
    constexpr qsizetype maximumLines = 12;
    QStringList lines = text.split(u'\n');
    if (lines.size() > maximumLines) {
        lines = lines.mid(0, maximumLines);
        text = lines.join(u'\n').trimmed() + QStringLiteral("…");
    }
    if (text.size() > maximumLength) {
        text = text.left(maximumLength - 1).trimmed() + QStringLiteral("…");
    }
    return text;
}

QString docstringForSymbol(const Symbol& symbol, const QString& source,
                           const ParseResult& parse) {
    if ((symbol.kind != SymbolKind::Function && symbol.kind != SymbolKind::Class)
        || !parse.ast || symbol.declarationNode == InvalidAstNodeId) {
        return {};
    }
    const QVector<AstNode>& nodes = parse.ast->nodes();
    if (symbol.declarationNode < 0 || symbol.declarationNode >= nodes.size()) {
        return {};
    }
    const AstNode& declaration = nodes.at(symbol.declarationNode);
    const AstNodeId bodyId = childWithRole(declaration, AstChildRole::Body);
    if (bodyId == InvalidAstNodeId || bodyId < 0 || bodyId >= nodes.size()) {
        return {};
    }
    const AstNode& body = nodes.at(bodyId);
    for (const AstNodeId statementId : body.children) {
        if (statementId < 0 || statementId >= nodes.size()) {
            continue;
        }
        const AstNode& statement = nodes.at(statementId);
        if (statement.kind != AstNodeKind::ExpressionStatement) {
            break;
        }
        for (const AstNodeId expressionId : statement.children) {
            if (expressionId < 0 || expressionId >= nodes.size()) {
                continue;
            }
            const AstNode& expression = nodes.at(expressionId);
            if (expression.kind == AstNodeKind::StringLiteral) {
                return unquoteDocumentation(sourceText(source, expression.range));
            }
        }
        break;
    }
    return {};
}

QString stripCommentMarker(QString comment) {
    comment = comment.trimmed();
    if (comment.startsWith(u'#')) {
        comment.remove(0, 1);
    }
    return comment.trimmed();
}

QString importProvenanceDocumentation(const Symbol& symbol, const QString& source) {
    const QString statement = sourceText(source, symbol.fullRange).simplified();
    if (statement.isEmpty()) {
        return symbol.kind == SymbolKind::ImportModule
            ? QStringLiteral("وحدة مستوردة؛ تفاصيل واجهة الوحدة غير مفهرسة بعد.")
            : QStringLiteral("اسم مستورد؛ تفاصيل واجهة الوحدة غير مفهرسة بعد.");
    }
    return symbol.kind == SymbolKind::ImportModule
        ? QStringLiteral("وحدة مستوردة محليًا من: %1").arg(statement)
        : QStringLiteral("اسم مستورد محليًا من: %1").arg(statement);
}

QString precedingCommentDocumentation(const Symbol& symbol, const LexResult& lex) {
    if (symbol.declarationRange.begin.line <= 1) {
        return {};
    }
    qsizetype expectedLine = symbol.declarationRange.begin.line - 1;
    QStringList lines;
    const QVector<Token>& tokens = lex.tokens;
    for (qsizetype index = tokens.size() - 1; index >= 0; --index) {
        const Token& token = tokens.at(index);
        if (token.kind != TokenKind::Comment || token.range.end.offset > symbol.declarationRange.begin.offset) {
            continue;
        }
        if (token.range.begin.line < expectedLine) {
            break;
        }
        if (token.range.begin.line != expectedLine
            || token.range.begin.column != symbol.declarationRange.begin.column) {
            return {};
        }
        lines.prepend(stripCommentMarker(token.lexeme));
        --expectedLine;
    }
    return lines.join(u'\n').trimmed();
}

QString signatureForSymbol(const Symbol& symbol, const SemanticModel& semantic,
                         const ParseResult& parse) {
    if (symbol.kind == SymbolKind::Class) {
        const QVector<SymbolId> bases = semantic.baseClassesOf(symbol.id);
        QStringList baseNames;
        for (const SymbolId base : bases) {
            const Symbol* baseSymbol = semantic.symbol(base);
            if (baseSymbol != nullptr) {
                baseNames.append(baseSymbol->name);
            }
        }
        if (baseNames.isEmpty()) {
            return symbol.name;
        }
        return QStringLiteral("%1(%2)").arg(symbol.name, baseNames.join(QStringLiteral(", ")));
    }
    if (symbol.kind != SymbolKind::Function || !parse.ast
        || symbol.declarationNode == InvalidAstNodeId) {
        return QStringLiteral("%1").arg(symbol.name);
    }
    const QVector<AstNode>& nodes = parse.ast->nodes();
    if (symbol.declarationNode < 0 || symbol.declarationNode >= nodes.size()) {
        return QStringLiteral("%1").arg(symbol.name);
    }
    const AstNodeId parameterListId = childWithRole(
        nodes.at(symbol.declarationNode), AstChildRole::ParameterList);
    QStringList parameters;
    if (parameterListId >= 0 && parameterListId < nodes.size()) {
        const AstNode& parameterList = nodes.at(parameterListId);
        for (const AstNodeId parameterId : parameterList.children) {
            if (parameterId < 0 || parameterId >= nodes.size()) {
                continue;
            }
            const AstNode& parameter = nodes.at(parameterId);
            const AstNodeId nameId = childWithRole(parameter, AstChildRole::ParameterName);
            if (nameId >= 0 && nameId < nodes.size()) {
                parameters.append(nodes.at(nameId).text);
            }
        }
    }
    return QStringLiteral("%1(%2)").arg(symbol.name, parameters.join(QStringLiteral("، ")));
}

bool isIdentifierTokenAt(const LexResult& lex, const qsizetype offset) {
    for (const Token& token : lex.tokens) {
        if (containsOffset(token.range, offset)) {
            return token.channel == TokenChannel::Main && token.kind == TokenKind::Identifier;
        }
    }
    return false;
}

std::optional<SymbolId> symbolAt(const SemanticModel& semantic, const qsizetype offset,
                                 SourceRange* targetRange) {
    if (const NameReference* reference = semantic.referenceAt(offset)) {
        if (reference->state == ResolutionState::Resolved
            && reference->resolvedSymbol != InvalidSymbolId) {
            if (targetRange != nullptr) {
                *targetRange = reference->range;
            }
            return reference->resolvedSymbol;
        }
        return std::nullopt;
    }

    const Symbol* best = nullptr;
    for (const Symbol& candidate : semantic.symbols()) {
        // Prelude/built-in symbols use the module range as a synthetic
        // declaration range. It is not a real source declaration and must not
        // make an unrelated unresolved identifier show built-in hover info.
        if (candidate.declarationNode == InvalidAstNodeId) {
            continue;
        }
        if (!containsOffset(candidate.declarationRange, offset)) {
            continue;
        }
        if (best == nullptr || rangeWidth(candidate.declarationRange)
            < rangeWidth(best->declarationRange)) {
            best = &candidate;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    if (targetRange != nullptr) {
        *targetRange = best->declarationRange;
    }
    return best->id;
}

} // namespace

std::optional<HoverInfo> SemanticHoverProvider::infoAt(
    const qsizetype utf16Offset, const QString& source,
    const LanguageAnalysisSnapshotPtr& snapshot) const {
    if (!snapshot || !snapshot->semantic || !snapshot->parse.ast
        || snapshot->revision != snapshot->semantic->documentRevision()
        || snapshot->revision != snapshot->parse.documentRevision
        || !isIdentifierTokenAt(snapshot->lex, utf16Offset)) {
        return std::nullopt;
    }

    SourceRange targetRange;
    const std::optional<SymbolId> symbolId = symbolAt(*snapshot->semantic, utf16Offset,
                                                       &targetRange);
    if (!symbolId.has_value()) {
        return std::nullopt;
    }
    const Symbol* symbol = snapshot->semantic->symbol(*symbolId);
    const bool isImportBinding = symbol != nullptr
        && (symbol->kind == SymbolKind::ImportModule
            || symbol->kind == SymbolKind::ImportMember);
    const bool isBuiltin = symbol != nullptr && symbol->kind == SymbolKind::Builtin;
    if (symbol == nullptr || symbol->kind == SymbolKind::Error
        || symbol->kind == SymbolKind::External
        || (symbol->isRecoverable && !isImportBinding && !isBuiltin)) {
        return std::nullopt;
    }

    QString documentation = isImportBinding
        ? importProvenanceDocumentation(*symbol, source)
        : docstringForSymbol(*symbol, source, snapshot->parse);
    if (documentation.isEmpty()) {
        documentation = precedingCommentDocumentation(*symbol, snapshot->lex);
    }
    if (documentation.isEmpty()) {
        documentation = QStringLiteral("لا توجد وثائق متاحة");
    }

    HoverInfo info;
    info.symbolId = symbol->id;
    info.symbolKind = symbol->kind;
    info.name = symbol->name;
    info.typeLabel = typeLabelForSymbol(symbol->kind);
    info.signature = signatureForSymbol(*symbol, *snapshot->semantic, snapshot->parse);
    info.documentation = truncateDocumentation(std::move(documentation));
    info.targetRange = targetRange;
    info.declarationRange = symbol->declarationRange;
    info.declarationLine = symbol->declarationRange.begin.line;
    return info;
}
