#pragma once

#include "AlifAnalysis.h"

/**
 * Stateless normalizer for diagnostics from all language-analysis stages.
 * Results are deterministic, de-duplicated, sorted, and safe for GUI rendering.
 */
class DiagnosticPresentationAdapter final {
public:
    [[nodiscard]] QVector<EditorDiagnostic> collect(
        const LexResult& lexicalResult,
        const ParseResult& parseResult,
        const std::shared_ptr<const SemanticModel>& semanticModel) const;
};
