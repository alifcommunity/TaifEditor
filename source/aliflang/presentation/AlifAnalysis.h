#pragma once

#include "AlifLexer.h"
#include "AlifParser.h"
#include "AlifSymbolTable.h"

#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <memory>

/** A conservative dirty interval used only for scheduling and future relexing. */
struct DirtyRange final {
    qsizetype beginOffset = 0;
    qsizetype endOffset = 0;

    [[nodiscard]] bool isValid() const { return beginOffset >= 0 && endOffset >= beginOffset; }
};

struct AnalysisRequest final {
    quint64 revision = 0;
    QString source;
    DirtyRange dirty;
};

enum class PresentationClass : quint8 {
    Keyword,
    Decorator,
    Comment,
    String,
    Number,
    Operator,
    Punctuation,
    FunctionDeclaration,
    ClassDeclaration,
    Parameter,
    SelfReceiver,
    Local,
    Import,
    Builtin,
    UnresolvedName,
    Error
};

struct PresentationSpan final {
    SourceRange range;
    PresentationClass classification = PresentationClass::Error;
    SemanticDiagnosticSeverity severity = SemanticDiagnosticSeverity::Information;
};

enum class DiagnosticOrigin : quint8 {
    Lexer,
    Parser,
    Semantic
};

/** Immutable, UI-neutral diagnostic normalized from one analysis snapshot. */
struct EditorDiagnostic final {
    QString code;
    QString message;
    SourceRange range;
    SemanticDiagnosticSeverity severity = SemanticDiagnosticSeverity::Information;
    DiagnosticOrigin origin = DiagnosticOrigin::Semantic;
};

struct AnalysisMetrics final {
    qint64 lexMilliseconds = 0;
    qint64 parseMilliseconds = 0;
    qint64 semanticMilliseconds = 0;
    qint64 presentationMilliseconds = 0;
    qint64 totalMilliseconds = 0;
    qsizetype tokenCount = 0;
    qsizetype spanCount = 0;
    qsizetype staleResultCount = 0;
};

/** Immutable language-core result transferred from worker to GUI thread. */
struct LanguageAnalysisSnapshot final {
    quint64 revision = 0;
    LexResult lex;
    ParseResult parse;
    std::shared_ptr<const SemanticModel> semantic;
    QVector<PresentationSpan> spans;
    QVector<EditorDiagnostic> diagnostics;
    AnalysisMetrics metrics;
};

using LanguageAnalysisSnapshotPtr = std::shared_ptr<const LanguageAnalysisSnapshot>;

Q_DECLARE_METATYPE(AnalysisRequest)
Q_DECLARE_METATYPE(PresentationSpan)
Q_DECLARE_METATYPE(EditorDiagnostic)
Q_DECLARE_METATYPE(QVector<EditorDiagnostic>)
Q_DECLARE_METATYPE(LanguageAnalysisSnapshotPtr)
