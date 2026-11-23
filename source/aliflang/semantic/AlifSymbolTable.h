#pragma once

#include "AlifParser.h"

#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <memory>

using ScopeId = qsizetype;
using SymbolId = qsizetype;
using ReferenceId = qsizetype;

inline constexpr ScopeId InvalidScopeId = -1;
inline constexpr SymbolId InvalidSymbolId = -1;
inline constexpr ReferenceId InvalidReferenceId = -1;

enum class ScopeKind : quint8 {
    Prelude,
    Module,
    Function,
    Class,
    Lambda,
    Comprehension
};

enum class SymbolKind : quint8 {
    Builtin,
    Module,
    Function,
    Class,
    Attribute,
    Parameter,
    Local,
    ImportModule,
    ImportMember,
    LoopVariable,
    ComprehensionVariable,
    External,
    Error
};

enum class ReferenceKind : quint8 {
    Read,
    Write,
    Delete,
    Call,
    BaseType,
    ImportPath,
    Member
};

enum class ResolutionState : quint8 {
    Resolved,
    Unresolved,
    Ambiguous,
    External,
    Invalid
};

enum class SemanticDiagnosticSeverity : quint8 {
    Error,
    Warning,
    Information
};

struct Scope final {
    ScopeId id = InvalidScopeId;
    ScopeKind kind = ScopeKind::Module;
    ScopeId parent = InvalidScopeId;
    AstNodeId ownerNode = InvalidAstNodeId;
    SourceRange range;
    QHash<QString, QVector<SymbolId>> declarations;
    QVector<ScopeId> children;
};

struct Symbol final {
    SymbolId id = InvalidSymbolId;
    SymbolKind kind = SymbolKind::Local;
    QString name;
    SourceRange declarationRange;
    SourceRange fullRange;
    AstNodeId declarationNode = InvalidAstNodeId;
    ScopeId declaringScope = InvalidScopeId;
    SymbolId instanceClass = InvalidSymbolId;
    bool isRecoverable = false;
};

struct NameReference final {
    ReferenceId id = InvalidReferenceId;
    AstNodeId node = InvalidAstNodeId;
    QString name;
    SourceRange range;
    ScopeId containingScope = InvalidScopeId;
    ReferenceKind kind = ReferenceKind::Read;
    ResolutionState state = ResolutionState::Unresolved;
    QVector<SymbolId> candidates;
    SymbolId resolvedSymbol = InvalidSymbolId;
};

struct SemanticBreadcrumb final {
    SymbolId symbol = InvalidSymbolId;
    SymbolKind kind = SymbolKind::Error;
    QString name;
    SourceRange declarationRange;
    SourceRange fullRange;
};

struct SemanticDiagnostic final {
    QString code;
    QString message;
    SourceRange range;
    SemanticDiagnosticSeverity severity = SemanticDiagnosticSeverity::Warning;
};

/**
 * Immutable semantic snapshot for one parser document revision. All offsets are
 * UTF-16 source offsets, matching QString/QTextDocument and the lexer/parser.
 */
class SemanticModel final {
public:
    [[nodiscard]] const QVector<Scope>& scopes() const { return m_scopes; }
    [[nodiscard]] const QVector<Symbol>& symbols() const { return m_symbols; }
    [[nodiscard]] const QVector<NameReference>& references() const { return m_references; }
    [[nodiscard]] const QVector<SemanticDiagnostic>& diagnostics() const { return m_diagnostics; }
    [[nodiscard]] ScopeId preludeScope() const { return m_preludeScope; }
    [[nodiscard]] ScopeId moduleScope() const { return m_moduleScope; }
    [[nodiscard]] quint64 documentRevision() const { return m_documentRevision; }

    [[nodiscard]] const Scope* scope(ScopeId id) const;
    [[nodiscard]] const Symbol* symbol(SymbolId id) const;
    [[nodiscard]] const NameReference* reference(ReferenceId id) const;
    [[nodiscard]] const NameReference* referenceAt(qsizetype utf16Offset) const;
    [[nodiscard]] QVector<SymbolId> visibleSymbolsAt(qsizetype utf16Offset) const;
    [[nodiscard]] QVector<SymbolId> referencesOf(SymbolId symbol) const;
    [[nodiscard]] QVector<SymbolId> documentSymbols() const;
    [[nodiscard]] QVector<SemanticBreadcrumb> enclosingSymbolPathAt(qsizetype utf16Offset) const;
    [[nodiscard]] QVector<SymbolId> baseClassesOf(SymbolId classSymbol) const;
    [[nodiscard]] QVector<SymbolId> membersOfClass(SymbolId classSymbol) const;
    [[nodiscard]] QVector<SymbolId> membersOfReceiver(SymbolId receiverSymbol) const;
    [[nodiscard]] SymbolId classOfReceiver(SymbolId receiverSymbol) const;

private:
    friend class SymbolTableBuilder;
    friend class SymbolTableBuilderImpl;

    [[nodiscard]] void collectMembers(SymbolId classSymbol, QVector<SymbolId>& out,
                                      QSet<SymbolId>& seen) const;

    QVector<Scope> m_scopes;
    QVector<Symbol> m_symbols;
    QVector<NameReference> m_references;
    QVector<SemanticDiagnostic> m_diagnostics;
    QHash<SymbolId, QVector<ReferenceId>> m_referencesBySymbol;
    QHash<SymbolId, ScopeId> m_classScopesBySymbol;
    QHash<ScopeId, SymbolId> m_scopeOwnerSymbols;
    QHash<SymbolId, QVector<SymbolId>> m_baseClassesBySymbol;
    ScopeId m_preludeScope = InvalidScopeId;
    ScopeId m_moduleScope = InvalidScopeId;
    quint64 m_documentRevision = 0;
};

/**
 * Stateless two-pass lexical-scope builder. It only depends on the parser AST
 * contract and is intentionally independent of text widgets and highlighters.
 */
class SymbolTableBuilder final {
public:
    [[nodiscard]] std::shared_ptr<const SemanticModel> build(
        const SymbolTableInput& input,
        const std::shared_ptr<const SemanticModel>& previous = nullptr) const;
};

Q_DECLARE_METATYPE(ScopeKind)
Q_DECLARE_METATYPE(SymbolKind)
Q_DECLARE_METATYPE(ReferenceKind)
Q_DECLARE_METATYPE(ResolutionState)
Q_DECLARE_METATYPE(SemanticBreadcrumb)
Q_DECLARE_METATYPE(SemanticDiagnostic)
