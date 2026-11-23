#pragma once

#include <QString>
#include <QVector>
#include <QStringList>

enum CompletionType {
    Keyword,
    Snippet,
    Builtin,
    DynamicWord,
    SemanticSymbol
};

/** UI-only semantic subtype. Keeps the completion UI independent of SymbolTable. */
enum class CompletionSemanticKind : quint8 {
    None,
    Function,
    Class,
    Attribute,
    Parameter,
    Local,
    LoopVariable,
    Import,
    Builtin,
    Unknown
};

struct CompletionItem {
    QString label;
    QString completion;
    QString description;
    CompletionType type = CompletionType::Keyword;
    CompletionSemanticKind semanticKind = CompletionSemanticKind::None;
};

// Abstract Strategy Interface
class ICompletionStrategy {
public:
    virtual ~ICompletionStrategy() = default;
    virtual QVector<CompletionItem> getSuggestions(const QString &prefix, const QString &fullText) = 0;
};

// --- Concrete Strategies ---

class KeywordStrategy : public ICompletionStrategy {
    QStringList keywords{};
public:
    KeywordStrategy();
    QVector<CompletionItem> getSuggestions(const QString &prefix, const QString &text) override;
};

class BuiltinStrategy : public ICompletionStrategy {
    QStringList builtins{};
public:
    BuiltinStrategy();
    QVector<CompletionItem> getSuggestions(const QString &prefix, const QString &text) override;
};

class SnippetStrategy : public ICompletionStrategy {
public:
    QVector<CompletionItem> getSuggestions(const QString &prefix, const QString &text) override;
};

class DynamicWordStrategy : public ICompletionStrategy {
public:
    QVector<CompletionItem> getSuggestions(const QString &prefix, const QString &fullText) override;
};

