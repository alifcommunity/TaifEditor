#include "SemanticCompletionProvider.h"

namespace {

CompletionSemanticKind completionKindForSymbol(const SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Function: return CompletionSemanticKind::Function;
    case SymbolKind::Class: return CompletionSemanticKind::Class;
    case SymbolKind::Attribute: return CompletionSemanticKind::Attribute;
    case SymbolKind::Parameter: return CompletionSemanticKind::Parameter;
    case SymbolKind::LoopVariable:
    case SymbolKind::ComprehensionVariable: return CompletionSemanticKind::LoopVariable;
    case SymbolKind::ImportModule:
    case SymbolKind::ImportMember: return CompletionSemanticKind::Import;
    case SymbolKind::Builtin: return CompletionSemanticKind::Builtin;
    case SymbolKind::Local: return CompletionSemanticKind::Local;
    default: return CompletionSemanticKind::Unknown;
    }
}

QString descriptionForSymbol(const SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Function: return QStringLiteral("دالة معرفة");
    case SymbolKind::Class: return QStringLiteral("صنف معرف");
    case SymbolKind::Attribute: return QStringLiteral("صفة معرفة");
    case SymbolKind::Parameter: return QStringLiteral("معامل");
    case SymbolKind::ImportModule: return QStringLiteral("وحدة مستوردة");
    case SymbolKind::ImportMember: return QStringLiteral("اسم مستورد من وحدة");
    case SymbolKind::Builtin: return QStringLiteral("دالة ضمن لغة ألف");
    case SymbolKind::LoopVariable: return QStringLiteral("متغير حلقة");
    case SymbolKind::ComprehensionVariable: return QStringLiteral("متغير ضمني");
    default: return QStringLiteral("متغير محلي");
    }
}

} // namespace

QVector<CompletionItem> SemanticCompletionProvider::suggestions(
    const QString& prefix,
    const qsizetype cursorOffset,
    const std::shared_ptr<const SemanticModel>& semantic,
    const bool moduleAndPreludeOnly) const {
    QVector<CompletionItem> items;
    if (!semantic) {
        return items;
    }

    const QVector<SymbolId> visible = semantic->visibleSymbolsAt(cursorOffset);
    const qsizetype currentTokenBegin = cursorOffset - prefix.size();
    items.reserve(visible.size());
    for (const SymbolId id : visible) {
        const Symbol* symbol = semantic->symbol(id);
        if (symbol == nullptr || !symbol->name.startsWith(prefix, Qt::CaseInsensitive)) {
            continue;
        }
        // A declaration currently being typed is already visible in the
        // incremental semantic snapshot. Do not offer that same incomplete
        // token as a completion until the cursor has crossed its delimiter.
        if (symbol->declarationRange.begin.offset == currentTokenBegin
            && symbol->declarationRange.end.offset == cursorOffset) {
            continue;
        }
        if (moduleAndPreludeOnly && symbol->declaringScope != semantic->moduleScope()
            && symbol->declaringScope != semantic->preludeScope()) {
            continue;
        }
        CompletionItem item;
        item.label = symbol->name;
        item.completion = symbol->name;
        item.description = descriptionForSymbol(symbol->kind);
        item.type = CompletionType::SemanticSymbol;
        item.semanticKind = completionKindForSymbol(symbol->kind);
        items.append(std::move(item));
    }
    return items;
}

QVector<CompletionItem> SemanticCompletionProvider::memberSuggestions(
    const QString& receiverName,
    const QString& prefix,
    const qsizetype receiverOffset,
    const qsizetype cursorOffset,
    const std::shared_ptr<const SemanticModel>& semantic,
    const bool moduleAndPreludeOnly) const {
    QVector<CompletionItem> items;
    if (!semantic || receiverName.isEmpty()) {
        return items;
    }

    SymbolId receiver = InvalidSymbolId;
    // The receiver offset is inside the identifier itself, unlike cursorOffset
    // which may be after a newly typed dot and outside an older snapshot range.
    const QVector<SymbolId> visible = semantic->visibleSymbolsAt(receiverOffset);
    for (const SymbolId candidateId : visible) {
        const Symbol* candidate = semantic->symbol(candidateId);
        if (candidate == nullptr || candidate->name != receiverName) {
            continue;
        }
        const bool isGlobal = candidate->declaringScope == semantic->moduleScope()
            || candidate->declaringScope == semantic->preludeScope();
        const bool isVerifiedSelf = candidate->kind == SymbolKind::Parameter
            && candidate->name == QStringLiteral("هذا")
            && candidate->instanceClass != InvalidSymbolId;
        if (moduleAndPreludeOnly && !isGlobal && !isVerifiedSelf) {
            continue;
        }
        receiver = candidateId;
        break;
    }
    Q_UNUSED(cursorOffset)
    if (receiver == InvalidSymbolId) {
        return items;
    }

    const QVector<SymbolId> members = semantic->membersOfReceiver(receiver);
    items.reserve(members.size());
    for (const SymbolId memberId : members) {
        const Symbol* member = semantic->symbol(memberId);
        if (member == nullptr || !member->name.startsWith(prefix, Qt::CaseInsensitive)) {
            continue;
        }
        CompletionItem item;
        item.label = member->name;
        item.completion = member->name;
        item.description = descriptionForSymbol(member->kind);
        item.type = CompletionType::SemanticSymbol;
        item.semanticKind = completionKindForSymbol(member->kind);
        items.append(std::move(item));
    }
    return items;
}
