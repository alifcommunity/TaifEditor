#include "TRecoveryDialog.h"

#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

bool fingerprintsMatch(const RecoverySourceFingerprint& left,
                       const RecoverySourceFingerprint& right)
{
    return left.exists == right.exists && left.size == right.size
        && left.lastModifiedUtc == right.lastModifiedUtc;
}

} // namespace

TRecoveryDialog::TRecoveryDialog(QVector<RecoveryEntry> entries, QWidget* const parent)
    : QDialog(parent)
    , m_entries(std::move(entries))
{
    setWindowTitle(QStringLiteral("استعادة الملفات"));
    setModal(true);
    setMinimumSize(760, 420);
    setLayoutDirection(Qt::RightToLeft);

    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(14);

    auto* const heading = new QLabel(QStringLiteral("تم العثور على نسخ محفوظة للإستعادة"), this);
    heading->setObjectName(QStringLiteral("RecoveryHeading"));
    auto* const description = new QLabel(
        QStringLiteral("إختر الملفات التي تريد إستعادتها من القائمة, مع العلم أن الملف الأصلي سيبقى كما هو."),
        this);
    description->setObjectName(QStringLiteral("RecoveryDescription"));
    description->setWordWrap(true);
    layout->addWidget(heading);
    layout->addWidget(description);

    m_entryList = new QTreeWidget(this);
    m_entryList->setObjectName(QStringLiteral("RecoveryEntryList"));
    m_entryList->setColumnCount(3);
    m_entryList->setHeaderLabels({QStringLiteral("الملف"), QStringLiteral("وقت النسخة"),
                                  QStringLiteral("حالة المصدر")});
    m_entryList->setRootIsDecorated(false);
    m_entryList->setAlternatingRowColors(false);
    m_entryList->setSelectionMode(QAbstractItemView::NoSelection);
    m_entryList->header()->setStretchLastSection(false);
    m_entryList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_entryList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_entryList->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    for (int index = 0; index < m_entries.size(); ++index) {
        const RecoveryEntry& entry = m_entries.at(index);
        auto* const item = new QTreeWidgetItem(m_entryList);
        item->setData(0, Qt::UserRole, index);
        item->setText(0, entry.displayName.isEmpty() ? QStringLiteral("غير معنون")
                                                      : entry.displayName);
        item->setToolTip(0, entry.sourcePath.isEmpty() ? QStringLiteral("مستند غير معنون")
                                                        : entry.sourcePath);
        item->setText(1, entry.capturedAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        item->setText(2, sourceStateText(entry));
        item->setCheckState(0, Qt::Checked);
    }
    layout->addWidget(m_entryList, 1);

    auto* const buttons = new QHBoxLayout();
    auto* const deferButton = new QPushButton(QStringLiteral("تأجيل"), this);
    deferButton->setObjectName(QStringLiteral("RecoveryDeferButton"));
    auto* const discardButton = new QPushButton(QStringLiteral("حذف النسخ المحددة"), this);
    discardButton->setObjectName(QStringLiteral("RecoveryDiscardButton"));
    auto* const restoreButton = new QPushButton(QStringLiteral("استعادة المحدد"), this);
    restoreButton->setObjectName(QStringLiteral("RecoveryRestoreButton"));
    buttons->addWidget(deferButton);
    buttons->addStretch(1);
    buttons->addWidget(discardButton);
    buttons->addWidget(restoreButton);
    layout->addLayout(buttons);

    connect(deferButton, &QPushButton::clicked, this, [this] {
        m_decision = Decision::Defer;
        reject();
    });
    connect(discardButton, &QPushButton::clicked, this, &TRecoveryDialog::chooseDiscard);
    connect(restoreButton, &QPushButton::clicked, this, &TRecoveryDialog::chooseRestore);

    setStyleSheet(QStringLiteral(R"(
        QDialog {
            background-color: #0f172a;
            color: #f1f5f9;
        }
        QLabel#RecoveryHeading {
            color: #f1f5f9;
            font-size: 20px;
            font-weight: 700;
        }
        QLabel#RecoveryDescription {
            color: #94a3b8;
            font-size: 14px;
        }
        QTreeWidget {
            background-color: #1e293b;
            border: 1px solid #334155;
            color: #e2e8f0;
            alternate-background-color: #1e293b;
        }
        QHeaderView {
            background-color: #172036;
            border: none;
            border-bottom: 1px solid #334155;
        }
        QHeaderView::section {
            background-color: #172036;
            color: #94a3b8;
            padding: 9px;
        }
        QTreeWidget::item {
            padding: 7px;
            border-bottom: 1px solid #273449;
        }
        QTreeWidget::item:hover {
            background-color: #263752;
        }
        QPushButton {
            background-color: #334155;
            color: #f1f5f9;
            border: none;
            border-radius: 6px;
            padding: 8px 17px;
            min-width: 100px;
        }
        QPushButton:hover {
            background-color: #475569;
        }
        QPushButton#RecoveryRestoreButton {
            background-color: #2563eb;
        }
        QPushButton#RecoveryRestoreButton:hover {
            background-color: #3b82f6;
        }
        QPushButton#RecoveryDiscardButton:hover {
            background-color: #b91c1c;
        }
    )"));
}

TRecoveryDialog::Decision TRecoveryDialog::decision() const
{
    return m_decision;
}

QVector<RecoveryEntry> TRecoveryDialog::selectedEntries() const
{
    QVector<RecoveryEntry> selected;
    for (int row = 0; row < m_entryList->topLevelItemCount(); ++row) {
        const QTreeWidgetItem* const item = m_entryList->topLevelItem(row);
        if (item == nullptr || item->checkState(0) != Qt::Checked) {
            continue;
        }
        const int entryIndex = item->data(0, Qt::UserRole).toInt();
        if (entryIndex >= 0 && entryIndex < m_entries.size()) {
            selected.append(m_entries.at(entryIndex));
        }
    }
    return selected;
}

QString TRecoveryDialog::sourceStateText(const RecoveryEntry& entry)
{
    if (entry.legacyAdjacentBackup) {
        return QStringLiteral("نسخة احتياطية سابقة");
    }
    if (entry.untitled || entry.sourcePath.isEmpty()) {
        return QStringLiteral("مستند غير معنون");
    }
    const RecoverySourceFingerprint current = RecoveryStore::fingerprintForPath(entry.sourcePath);
    if (!current.exists) {
        return QStringLiteral("الملف الأصلي غير موجود");
    }
    return fingerprintsMatch(current, entry.sourceFingerprint)
        ? QStringLiteral("الملف الأصلي لم يتغير")
        : QStringLiteral("الملف الأصلي تغير خارجيًا");
}

void TRecoveryDialog::chooseRestore()
{
    if (selectedEntries().isEmpty()) {
        return;
    }
    m_decision = Decision::Restore;
    accept();
}

void TRecoveryDialog::chooseDiscard()
{
    if (selectedEntries().isEmpty()) {
        return;
    }
    m_decision = Decision::Discard;
    accept();
}
