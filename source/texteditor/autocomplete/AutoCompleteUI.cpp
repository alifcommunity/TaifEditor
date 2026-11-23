#include "AutoCompleteUI.h"

#include <QPainter>
#include <QScrollBar>

namespace {

CompletionVisual semanticVisual(const CompletionSemanticKind kind) {
    switch (kind) {
    case CompletionSemanticKind::Function:
        return {QColor(97, 175, 239), QStringLiteral("()"), QStringLiteral("دالة")};
    case CompletionSemanticKind::Class:
        return {QColor(229, 192, 123), QStringLiteral("[]"), QStringLiteral("صنف")};
    case CompletionSemanticKind::Attribute:
        return {QColor(86, 182, 194), QStringLiteral("::"), QStringLiteral("صفة")};
    case CompletionSemanticKind::Parameter:
        return {QColor(198, 120, 221), QStringLiteral("@"), QStringLiteral("معامل")};
    case CompletionSemanticKind::Local:
        return {QColor(171, 178, 191), QStringLiteral("أب"), QStringLiteral("متغير")};
    case CompletionSemanticKind::LoopVariable:
        return {QColor(152, 195, 121), QStringLiteral("#"), QStringLiteral("متغير حلقة")};
    case CompletionSemanticKind::Import:
        return {QColor(209, 154, 102), QStringLiteral("->"), QStringLiteral("اسم مستورد")};
    case CompletionSemanticKind::Builtin:
        return {QColor(130, 212, 72), QStringLiteral("()"), QStringLiteral("ضمنية")};
    case CompletionSemanticKind::None:
    case CompletionSemanticKind::Unknown:
        return {QColor(171, 178, 191), QStringLiteral("أب"), QStringLiteral("رمز")};
    }
    return {QColor(171, 178, 191), QStringLiteral("?"), QStringLiteral("رمز")};
}

} // namespace

CompletionVisual completionVisual(const CompletionType type,
                                  const CompletionSemanticKind semanticKind) {
    switch (type) {
    case CompletionType::Keyword:
        return {QColor(198, 120, 221), QStringLiteral("{}"), QStringLiteral("محجوزة")};
    case CompletionType::Snippet:
        return {QColor(224, 108, 117), QStringLiteral("<>"), QStringLiteral("كتلة")};
    case CompletionType::Builtin:
        return {QColor(130, 212, 72), QStringLiteral("()"), QStringLiteral("ضمنية")};
    case CompletionType::DynamicWord:
        return {QColor(97, 175, 239), QStringLiteral("أب"), QStringLiteral("نص")};
    case CompletionType::SemanticSymbol:
        return semanticVisual(semanticKind);
    }
    return {QColor(171, 178, 191), QStringLiteral("?"), QStringLiteral("اقتراح")};
}

// --- CompletionModel ---

CompletionModel::CompletionModel(QObject* parent) : QAbstractListModel(parent) {}

void CompletionModel::updateData(const std::vector<CompletionItem>& items) {
    beginResetModel();
    m_data.clear();
    m_data.reserve(items.size());
    for (const CompletionItem& item : items) {
        m_data.push_back({item.label, item.completion, item.description,
                          item.type, item.semanticKind});
    }
    endResetModel();
}

int CompletionModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(m_data.size());
}

QVariant CompletionModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_data.size())) {
        return QVariant();
    }
    const ModelItem& item = m_data.at(index.row());
    switch (role) {
    case Qt::DisplayRole: return item.label;
    case Qt::EditRole: return item.completion;
    case Qt::UserRole + 1: return item.description;
    case Qt::UserRole + 2: return static_cast<int>(item.type);
    case Qt::UserRole + 3: return static_cast<int>(item.semanticKind);
    default: return QVariant();
    }
}

// --- Rich popup ---

TCompletionPopup::TCompletionPopup(QWidget* parent)
    : QListView(parent), footerHeight(70) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setLayoutDirection(Qt::RightToLeft);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setUniformItemSizes(true);
    setStyleSheet(R"(
        QListView {
            background-color: #1e202e;
            border: 1px solid #4b5263;
            color: #abb2bf;
            outline: none;
        }
        QListView::item:selected {
            background-color: #3e4451;
        }
        )");

    infoLabel = new QLabel(this);
    infoLabel->setStyleSheet(R"(
        QLabel {
            background-color: #2c313a;
            border-top: 1px solid #4793FF;
            border-top-left-radius: 8px;
            border-top-right-radius: 8px;
            color: #9da5b4;
            padding: 8px;
            font-family: 'Tajawal', 'Noto Kufi Arabic';
        }
        )");
    infoLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    infoLabel->setWordWrap(true);
    infoLabel->setLayoutDirection(Qt::RightToLeft);
    setViewportMargins(0, 0, 0, footerHeight);
}

void TCompletionPopup::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    const QRect bounds = contentsRect();
    infoLabel->setGeometry(bounds.left(), bounds.bottom() - footerHeight + 1,
                           bounds.width(), footerHeight);
}

void TCompletionPopup::currentChanged(const QModelIndex& current,
                                      const QModelIndex& previous) {
    QListView::currentChanged(current, previous);
    if (!current.isValid()) {
        infoLabel->clear();
        return;
    }

    const QString description = current.data(Qt::UserRole + 1).toString();
    const CompletionType type = static_cast<CompletionType>(
        current.data(Qt::UserRole + 2).toInt());
    const CompletionSemanticKind semanticKind = static_cast<CompletionSemanticKind>(
        current.data(Qt::UserRole + 3).toInt());
    const CompletionVisual visual = completionVisual(type, semanticKind);
    const QString summary = description.isEmpty() ? visual.category : description;
    const QString html = QStringLiteral(R"(
        <div dir='rtl'><span style='font-weight:bold; color:%1; font-size:14px;'>%2</span><br>
        <span style='font-family: Tajawal,'Noto Kufi Arabic'; font-size:12px; color:#dcdfe4;'>%3</span></div>)") // لا تقم بفصل الأسطر عن بعضها لأن ذلك يسبب مشكلة في عرض %2
        .arg(visual.color.name(), visual.category,
            summary.toHtmlEscaped().replace(QChar(u'\n'), QStringLiteral("<br>")));
            infoLabel->setText(html);
}

// --- Row delegate ---

TModernCompletionDelegate::TModernCompletionDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

QSize TModernCompletionDelegate::sizeHint(const QStyleOptionViewItem& option,
                                          const QModelIndex&) const {
    return {option.rect.width(), 32};
}

void TModernCompletionDelegate::paint(QPainter* painter,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // Data
    const QString label = index.data(Qt::DisplayRole).toString();
    const CompletionType type = static_cast<CompletionType>(
        index.data(Qt::UserRole + 2).toInt());
    const CompletionSemanticKind semanticKind = static_cast<CompletionSemanticKind>(
        index.data(Qt::UserRole + 3).toInt());

    // Colors
    const CompletionVisual visual = completionVisual(type, semanticKind);
    const bool selected = option.state.testFlag(QStyle::State_Selected);

    // Draw Background
    // Note: We don't draw rounded rect here because the container handles the main border.
    // Just fill rect.
    painter->fillRect(option.rect, selected ? QColor(62, 68, 81) : QColor(30, 32, 46));

    // Draw Icon (Right Side)
    // ---------------------
    constexpr int iconWidth = 35;
    const QRect iconRect(option.rect.right() - iconWidth + 1, option.rect.top(),
                         iconWidth, option.rect.height());

    // Icon text
    painter->setPen(visual.color);
    painter->setFont(QFont(QStringLiteral("Consolas"), 9, QFont::Bold));
    painter->drawText(iconRect, Qt::AlignCenter, visual.icon);

    // Draw Label (Main Text)
    // ----------------------
    const QRect textRect = option.rect.adjusted(10, 0, -iconWidth, 0);
    painter->setPen(selected ? Qt::white : QColor(171, 178, 191));
    painter->setFont(QFont(QStringLiteral("Tajawal"), 10));

    // Draw text vertically centered
    painter->drawText(textRect, Qt::AlignVCenter,
                      label);

    if (selected) {
        painter->fillRect(option.rect.right() - 2, option.rect.top(), 2,
                          option.rect.height(), visual.color);
    }
    painter->restore();
}
