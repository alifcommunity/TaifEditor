#pragma once

#include "AlifAnalysis.h"
#include "AutoComplete.h"

/**
 * Converts current-revision SemanticModel visibility into completion items.
 * It never parses or scans editor text and is safe to invoke on the GUI thread.
 */
class SemanticCompletionProvider final {
public:
    [[nodiscard]] QVector<CompletionItem> suggestions(
        const QString& prefix,
        qsizetype cursorOffset,
        const std::shared_ptr<const SemanticModel>& semantic,
        bool moduleAndPreludeOnly = false) const;

    [[nodiscard]] QVector<CompletionItem> memberSuggestions(
        const QString& receiverName,
        const QString& prefix,
        qsizetype receiverOffset,
        qsizetype cursorOffset,
        const std::shared_ptr<const SemanticModel>& semantic,
        bool moduleAndPreludeOnly = false) const;
};
