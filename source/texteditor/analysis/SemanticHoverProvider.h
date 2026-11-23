#pragma once

#include "AlifAnalysis.h"

#include <QtCore/QString>

#include <optional>

/**
 * Immutable, editor-ready semantic hover data. Ranges use the language core's
 * UTF-16 half-open coordinate convention and therefore map directly to Qt text
 * cursors and document positions.
 */
struct HoverInfo final {
    SymbolId symbolId = InvalidSymbolId;
    SymbolKind symbolKind = SymbolKind::Error;
    QString name;
    QString typeLabel;
    QString signature;
    QString documentation;
    SourceRange targetRange;
    SourceRange declarationRange;
    qsizetype declarationLine = 0;
};

/**
 * Resolves hover information only from a current immutable analysis snapshot.
 * This class has no QWidget/QTextDocument dependency; the editor supplies the
 * source snapshot solely for docstring/comment extraction.
 */
class SemanticHoverProvider final {
public:
    [[nodiscard]] std::optional<HoverInfo> infoAt(
        qsizetype utf16Offset,
        const QString& source,
        const LanguageAnalysisSnapshotPtr& snapshot) const;
};
