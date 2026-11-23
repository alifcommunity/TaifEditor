#include "SemanticDefinitionProvider.h"

namespace {

bool containsOffset(const SourceRange& range, const qsizetype offset) {
    return offset >= range.begin.offset && offset < range.end.offset;
}

qsizetype rangeWidth(const SourceRange& range) {
    return range.end.offset - range.begin.offset;
}

bool isIdentifierTokenAt(const LexResult& lex, const qsizetype offset) {
    for (const Token& token : lex.tokens) {
        if (containsOffset(token.range, offset)) {
            return token.channel == TokenChannel::Main && token.kind == TokenKind::Identifier;
        }
    }
    return false;
}

bool isValidDeclarationRange(const SourceRange& range, const QString& source,
                             const LexResult& lex) {
    return range.begin.offset >= 0
        && range.end.offset > range.begin.offset
        && range.end.offset <= source.size()
        && isIdentifierTokenAt(lex, range.begin.offset);
}

std::optional<SymbolId> resolvedSymbolAt(const SemanticModel& semantic,
                                         const qsizetype offset,
                                         SourceRange* sourceRange) {
    if (const NameReference* reference = semantic.referenceAt(offset)) {
        if (reference->state == ResolutionState::Resolved
            && reference->resolvedSymbol != InvalidSymbolId) {
            if (sourceRange != nullptr) {
                *sourceRange = reference->range;
            }
            return reference->resolvedSymbol;
        }
        return std::nullopt;
    }

    const Symbol* best = nullptr;
    for (const Symbol& candidate : semantic.symbols()) {
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
    if (sourceRange != nullptr) {
        *sourceRange = best->declarationRange;
    }
    return best->id;
}

bool supportsLocalNavigation(const Symbol& symbol) {
    if (symbol.kind == SymbolKind::Error || symbol.kind == SymbolKind::External
        || symbol.kind == SymbolKind::Builtin) {
        return false;
    }
    const bool isImportBinding = symbol.kind == SymbolKind::ImportModule
        || symbol.kind == SymbolKind::ImportMember;
    return !symbol.isRecoverable || isImportBinding;
}

} // namespace

std::optional<DefinitionLocation> SemanticDefinitionProvider::definitionAt(
    const qsizetype utf16Offset, const QString& source,
    const LanguageAnalysisSnapshotPtr& snapshot) const {
    if (!snapshot || !snapshot->semantic || !snapshot->parse.ast
        || snapshot->revision != snapshot->semantic->documentRevision()
        || snapshot->revision != snapshot->parse.documentRevision
        || !isIdentifierTokenAt(snapshot->lex, utf16Offset)) {
        return std::nullopt;
    }

    SourceRange sourceRange;
    const std::optional<SymbolId> symbolId = resolvedSymbolAt(
        *snapshot->semantic, utf16Offset, &sourceRange);
    if (!symbolId.has_value()) {
        return std::nullopt;
    }
    const Symbol* symbol = snapshot->semantic->symbol(*symbolId);
    if (symbol == nullptr || !supportsLocalNavigation(*symbol)
        || !isValidDeclarationRange(symbol->declarationRange, source, snapshot->lex)) {
        return std::nullopt;
    }

    DefinitionLocation location;
    location.symbolId = symbol->id;
    location.name = symbol->name;
    location.sourceRange = sourceRange;
    location.declarationRange = symbol->declarationRange;
    location.declarationLine = symbol->declarationRange.begin.line;
    return location;
}
