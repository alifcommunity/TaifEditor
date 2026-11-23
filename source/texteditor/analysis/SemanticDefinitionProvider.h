#pragma once

#include "AlifAnalysis.h"

#include <QtCore/QString>

#include <optional>

/**
 * Exact in-document destination for semantic navigation. All source ranges use
 * UTF-16 half-open offsets and map directly to QTextCursor positions.
 */
struct DefinitionLocation final {
    SymbolId symbolId = InvalidSymbolId;
    QString name;
    SourceRange sourceRange;
    SourceRange declarationRange;
    qsizetype declarationLine = 0;
};

/**
 * Resolves a local definition target from one immutable, revision-consistent
 * language-analysis snapshot. It has no widget/QTextDocument dependencies.
 */
class SemanticDefinitionProvider final {
public:
    [[nodiscard]] std::optional<DefinitionLocation> definitionAt(
        qsizetype utf16Offset,
        const QString& source,
        const LanguageAnalysisSnapshotPtr& snapshot) const;
};
