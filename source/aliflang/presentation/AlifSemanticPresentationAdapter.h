#pragma once

#include "AlifAnalysis.h"

/**
 * Pure classifier for editor presentation. It creates data-only spans and has
 * no dependency on QTextDocument, QSyntaxHighlighter, or QWidget.
 */
class SemanticPresentationAdapter final {
public:
    [[nodiscard]] QVector<PresentationSpan> classify(
        const LexResult& lexicalResult,
        const ParseResult& parseResult,
        const std::shared_ptr<const SemanticModel>& semanticModel,
        const QVector<EditorDiagnostic>& diagnostics = {}) const;
};
