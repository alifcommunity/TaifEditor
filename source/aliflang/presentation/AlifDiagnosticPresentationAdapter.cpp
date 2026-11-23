#include "AlifDiagnosticPresentationAdapter.h"

#include <QtCore/QSet>

#include <algorithm>

namespace {

SemanticDiagnosticSeverity severityFor(const ParseDiagnosticSeverity severity) {
    return severity == ParseDiagnosticSeverity::Warning
        ? SemanticDiagnosticSeverity::Warning
        : SemanticDiagnosticSeverity::Error;
}

int severityRank(const SemanticDiagnosticSeverity severity) {
    switch (severity) {
    case SemanticDiagnosticSeverity::Error: return 0;
    case SemanticDiagnosticSeverity::Warning: return 1;
    case SemanticDiagnosticSeverity::Information: return 2;
    }
    return 3;
}

QString deduplicationKey(const EditorDiagnostic& diagnostic) {
    return QString::number(static_cast<int>(diagnostic.origin))
        + QLatin1Char('|') + diagnostic.code
        + QLatin1Char('|') + diagnostic.message
        + QLatin1Char('|') + QString::number(diagnostic.range.begin.offset)
        + QLatin1Char('|') + QString::number(diagnostic.range.end.offset)
        + QLatin1Char('|') + QString::number(static_cast<int>(diagnostic.severity));
}

bool sourceOrder(const EditorDiagnostic& left, const EditorDiagnostic& right) {
    if (left.range.begin.offset != right.range.begin.offset) {
        return left.range.begin.offset < right.range.begin.offset;
    }
    if (left.range.end.offset != right.range.end.offset) {
        return left.range.end.offset < right.range.end.offset;
    }
    if (severityRank(left.severity) != severityRank(right.severity)) {
        return severityRank(left.severity) < severityRank(right.severity);
    }
    if (left.code != right.code) {
        return left.code < right.code;
    }
    return static_cast<int>(left.origin) < static_cast<int>(right.origin);
}

} // namespace

QVector<EditorDiagnostic> DiagnosticPresentationAdapter::collect(
    const LexResult& lexicalResult, const ParseResult& parseResult,
    const std::shared_ptr<const SemanticModel>& semanticModel) const {
    QVector<EditorDiagnostic> diagnostics;
    diagnostics.reserve(lexicalResult.diagnostics.size() + parseResult.parserDiagnostics.size()
                        + (semanticModel ? semanticModel->diagnostics().size() : 0));
    QSet<QString> seen;

    const auto appendUnique = [&diagnostics, &seen](EditorDiagnostic diagnostic) {
        const QString key = deduplicationKey(diagnostic);
        if (!seen.contains(key)) {
            seen.insert(key);
            diagnostics.append(std::move(diagnostic));
        }
    };

    for (const LexDiagnostic& diagnostic : lexicalResult.diagnostics) {
        appendUnique({diagnostic.code, diagnostic.message, diagnostic.range,
                      SemanticDiagnosticSeverity::Error, DiagnosticOrigin::Lexer});
    }

    // ParseResult forwards lexer diagnostics for parser recovery; lexer results
    // above are the single canonical source for them, so only parser-native
    // diagnostics are appended here.
    for (const ParseDiagnostic& diagnostic : parseResult.parserDiagnostics) {
        appendUnique({diagnostic.code, diagnostic.message, diagnostic.range,
                      severityFor(diagnostic.severity), DiagnosticOrigin::Parser});
    }

    if (semanticModel) {
        for (const SemanticDiagnostic& diagnostic : semanticModel->diagnostics()) {
            appendUnique({diagnostic.code, diagnostic.message, diagnostic.range,
                          diagnostic.severity, DiagnosticOrigin::Semantic});
        }
    }

    std::sort(diagnostics.begin(), diagnostics.end(), sourceOrder);
    return diagnostics;
}
