#include "GitPanelWidget.h"

#include "GitRepositoryService.h"

#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

QLabel* sectionTitle(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("GitPanelSectionTitle"));
    return label;
}

QListWidget* changeList(const QString& name, QWidget* parent)
{
    auto* list = new QListWidget(parent);
    list->setObjectName(name);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setMinimumHeight(62);
    list->setMaximumHeight(150);
    list->setAccessibleName(name);
    return list;
}

QString stateName(const VersionControlState state)
{
    switch (state) {
    case VersionControlState::Modified: return QStringLiteral("معدّل");
    case VersionControlState::Added: return QStringLiteral("مضاف");
    case VersionControlState::Untracked: return QStringLiteral("غير متتبّع");
    case VersionControlState::Deleted: return QStringLiteral("محذوف");
    case VersionControlState::Renamed: return QStringLiteral("أُعيدت تسميته");
    case VersionControlState::Conflicted: return QStringLiteral("تعارض");
    case VersionControlState::Ignored: return QStringLiteral("متجاهل");
    default: return QStringLiteral("نظيف");
    }
}

} // namespace

GitPanelWidget::GitPanelWidget(GitRepositoryService* const service, QWidget* const parent)
    : QWidget(parent), m_service(service)
{
    setObjectName(QStringLiteral("GitPanelWidget"));
    setAccessibleName(QStringLiteral("لوحة Git"));
    setLayoutDirection(Qt::RightToLeft);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(7);

    auto* header = new QHBoxLayout;
    m_repositoryLabel = new QLabel(QStringLiteral("لا يوجد مشروع Git"), this);
    m_repositoryLabel->setObjectName(QStringLiteral("GitRepositoryLabel"));
    m_branchLabel = new QLabel(QStringLiteral("—"), this);
    m_branchLabel->setObjectName(QStringLiteral("GitBranchLabel"));
    m_refreshButton = new QPushButton(QStringLiteral("تحديث"), this);
    m_refreshButton->setObjectName(QStringLiteral("GitRefreshButton"));
    header->addWidget(m_repositoryLabel, 1);
    header->addWidget(m_branchLabel);
    header->addWidget(m_refreshButton);
    outer->addLayout(header);

    m_syncLabel = new QLabel(this); m_syncLabel->setObjectName(QStringLiteral("GitSyncLabel"));
    m_stateLabel = new QLabel(this); m_stateLabel->setObjectName(QStringLiteral("GitStateLabel"));
    m_stateLabel->setWordWrap(true);
    outer->addWidget(m_syncLabel); outer->addWidget(m_stateLabel);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("GitPanelScrollArea"));
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(true);
    auto* content = new QWidget(scroll); content->setObjectName(QStringLiteral("GitPanelScrollContent"));
    content->setAutoFillBackground(true); auto* body = new QVBoxLayout(content);
    body->setContentsMargins(0, 0, 0, 0); body->setSpacing(5);

    body->addWidget(sectionTitle(QStringLiteral("تعارضات"), content));
    m_conflictsList = changeList(QStringLiteral("GitConflictsList"), content); body->addWidget(m_conflictsList);
    body->addWidget(sectionTitle(QStringLiteral("مجهّز للإيداع"), content));
    m_stagedList = changeList(QStringLiteral("GitStagedList"), content); body->addWidget(m_stagedList);
    body->addWidget(sectionTitle(QStringLiteral("تغييرات"), content));
    m_changesList = changeList(QStringLiteral("GitChangesList"), content); body->addWidget(m_changesList);
    body->addWidget(sectionTitle(QStringLiteral("غير متتبّع"), content));
    m_untrackedList = changeList(QStringLiteral("GitUntrackedList"), content); body->addWidget(m_untrackedList);

    auto* changeActions = new QHBoxLayout;
    m_stageButton = new QPushButton(QStringLiteral("تجهيز"), content);
    m_unstageButton = new QPushButton(QStringLiteral("إلغاء التجهيز"), content);
    m_discardButton = new QPushButton(QStringLiteral("تجاهل التغييرات"), content);
    m_stageButton->setObjectName(QStringLiteral("GitStageButton"));
    m_unstageButton->setObjectName(QStringLiteral("GitUnstageButton"));
    m_discardButton->setObjectName(QStringLiteral("GitDiscardButton"));
    changeActions->addWidget(m_stageButton); changeActions->addWidget(m_unstageButton); changeActions->addWidget(m_discardButton);
    body->addLayout(changeActions);

    body->addWidget(sectionTitle(QStringLiteral("فرق الملف المحدد"), content));
    m_diffView = new QPlainTextEdit(content); m_diffView->setObjectName(QStringLiteral("GitDiffView"));
    m_diffView->setReadOnly(true); m_diffView->setLayoutDirection(Qt::LeftToRight); m_diffView->setMinimumHeight(140); body->addWidget(m_diffView);

    body->addWidget(sectionTitle(QStringLiteral("إيداع"), content));
    m_commitMessage = new QTextEdit(content); m_commitMessage->setObjectName(QStringLiteral("GitCommitMessage"));
    m_commitMessage->setPlaceholderText(QStringLiteral("رسالة الإيداع…")); m_commitMessage->setMaximumHeight(75); body->addWidget(m_commitMessage);
    m_commitButton = new QPushButton(QStringLiteral("إيداع التغييرات المجهّزة"), content); m_commitButton->setObjectName(QStringLiteral("GitCommitButton")); body->addWidget(m_commitButton);

    body->addWidget(sectionTitle(QStringLiteral("المزامنة"), content));
    auto* syncActions = new QHBoxLayout;
    m_fetchButton = new QPushButton(QStringLiteral("جلب"), content); m_fetchButton->setObjectName(QStringLiteral("GitFetchButton"));
    m_pullButton = new QPushButton(QStringLiteral("سحب سريع"), content); m_pullButton->setObjectName(QStringLiteral("GitPullButton"));
    m_pushButton = new QPushButton(QStringLiteral("دفع"), content); m_pushButton->setObjectName(QStringLiteral("GitPushButton"));
    syncActions->addWidget(m_fetchButton); syncActions->addWidget(m_pullButton); syncActions->addWidget(m_pushButton); body->addLayout(syncActions);

    body->addWidget(sectionTitle(QStringLiteral("الفروع"), content));
    auto* branchActions = new QHBoxLayout;
    m_branchEdit = new QLineEdit(content); m_branchEdit->setObjectName(QStringLiteral("GitBranchEdit"));
    m_branchEdit->setPlaceholderText(QStringLiteral("اسم فرع")); m_branchEdit->setLayoutDirection(Qt::LeftToRight);
    auto* createBranch = new QPushButton(QStringLiteral("إنشاء"), content); createBranch->setObjectName(QStringLiteral("GitCreateBranchButton"));
    auto* switchBranch = new QPushButton(QStringLiteral("تبديل"), content); switchBranch->setObjectName(QStringLiteral("GitSwitchBranchButton"));
    branchActions->addWidget(m_branchEdit, 1); branchActions->addWidget(createBranch); branchActions->addWidget(switchBranch); body->addLayout(branchActions);

    body->addWidget(sectionTitle(QStringLiteral("الإيداعات الأخيرة"), content));
    m_historyList = new QListWidget(content); m_historyList->setObjectName(QStringLiteral("GitHistoryList"));
    m_historyList->setMinimumHeight(110); body->addWidget(m_historyList);

    body->addWidget(sectionTitle(QStringLiteral("نتيجة العملية"), content));
    m_outputView = new QPlainTextEdit(content); m_outputView->setObjectName(QStringLiteral("GitOutputView"));
    m_outputView->setReadOnly(true); m_outputView->setLayoutDirection(Qt::LeftToRight); m_outputView->setMaximumHeight(110); body->addWidget(m_outputView);
    body->addStretch(1); scroll->setWidget(content); outer->addWidget(scroll, 1);

    setStyleSheet(QStringLiteral(R"(
        QWidget#GitPanelWidget { background:#0f172a; color:#cbd5e1; }
        QScrollArea#GitPanelScrollArea, QScrollArea#GitPanelScrollArea > QWidget,
        QWidget#GitPanelScrollContent { background:#0f172a; border:none; }
        QScrollArea#GitPanelScrollArea > QWidget > QWidget { background:#0f172a; }
        QScrollBar:vertical { background:#0b1220; width:10px; margin:2px; }
        QScrollBar::handle:vertical { background:#263a57; border-radius:4px; min-height:28px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QLabel#GitRepositoryLabel { color:#e2e8f0; font-weight:500; }
        QLabel#GitBranchLabel, QLabel#GitSyncLabel { color:#93c5fd; }
        QLabel#GitStateLabel { color:#94a3b8; }
        QLabel#GitPanelSectionTitle { color:#60a5fa; font-weight:700; padding-top:5px; }
        QListWidget, QPlainTextEdit, QTextEdit, QLineEdit { background:#111d33; color:#dbeafe; border:1px solid #263a57; border-radius:5px; padding:4px; }
        QListWidget::item:selected { background:#1e40af; }
        QPushButton { background:#17345c; color:#e0f2fe; border:1px solid #28548a; border-radius:5px; padding:5px 8px; }
        QPushButton:hover { background:#1e4d82; } QPushButton:disabled { color:#64748b; background:#111827; border-color:#1f2937; }
    )"));

    connect(m_service, &GitRepositoryService::snapshotChanged, this, &GitPanelWidget::applySnapshot);
    connect(m_service, &GitRepositoryService::operationFinished, this, &GitPanelWidget::applyOperationResult);
    connect(m_service, &GitRepositoryService::diffReady, this, &GitPanelWidget::applyDiff);
    connect(m_service, &GitRepositoryService::historyReady, this, &GitPanelWidget::applyHistory);
    connect(m_refreshButton, &QPushButton::clicked, this, &GitPanelWidget::refresh);
    connect(m_stageButton, &QPushButton::clicked, this, &GitPanelWidget::requestStage);
    connect(m_unstageButton, &QPushButton::clicked, this, &GitPanelWidget::requestUnstage);
    connect(m_discardButton, &QPushButton::clicked, this, &GitPanelWidget::requestDiscard);
    connect(m_commitButton, &QPushButton::clicked, this, &GitPanelWidget::requestCommit);
    connect(m_fetchButton, &QPushButton::clicked, this, &GitPanelWidget::requestFetch);
    connect(m_pullButton, &QPushButton::clicked, this, &GitPanelWidget::requestPull);
    connect(m_pushButton, &QPushButton::clicked, this, &GitPanelWidget::requestPush);
    connect(createBranch, &QPushButton::clicked, this, &GitPanelWidget::requestCreateBranch);
    connect(switchBranch, &QPushButton::clicked, this, &GitPanelWidget::requestSwitchBranch);
    for (QListWidget* list : {m_conflictsList, m_stagedList, m_changesList, m_untrackedList})
        connect(list, &QListWidget::itemSelectionChanged, this, &GitPanelWidget::showDiffForSelection);
}

void GitPanelWidget::setProjectRoot(const QString& rootPath) { m_projectRoot = rootPath; m_service->setProjectRoot(rootPath); }
QString GitPanelWidget::projectRoot() const { return m_projectRoot; }
void GitPanelWidget::refresh() { m_service->refresh(true); m_service->requestHistory(); }

void GitPanelWidget::applySnapshot(const GitRepositorySnapshot& snapshot)
{
    m_snapshot = snapshot; setBusy(snapshot.busy);
    m_repositoryLabel->setText(snapshot.repository ? QFileInfo(snapshot.projectRoot).fileName() : QStringLiteral("لا يوجد مستودع Git"));
    m_repositoryLabel->setToolTip(snapshot.projectRoot);
    m_branchLabel->setText(snapshot.branch.isEmpty() ? QStringLiteral("—") : snapshot.branch);
    m_syncLabel->setText(snapshot.upstream.isEmpty() ? QStringLiteral("لا يوجد فرع تتبع")
        : QStringLiteral("%1  ↑%2  ↓%3").arg(snapshot.upstream).arg(snapshot.ahead).arg(snapshot.behind));
    m_stateLabel->setText(snapshot.lastError.isEmpty() ? (snapshot.isDirty() ? QStringLiteral("توجد تغييرات محلية.") : QStringLiteral("شجرة العمل نظيفة.")) : snapshot.lastError);
    rebuildChangeLists(snapshot);
    m_commitButton->setEnabled(snapshot.repository && snapshot.stagedCount() > 0 && !snapshot.busy);
}

void GitPanelWidget::applyOperationResult(const GitCommandResult& result)
{
    const QString text = result.succeeded ? result.userMessage : result.userMessage + QStringLiteral("\n") + result.standardError;
    setTechnicalText(m_outputView, text + (result.standardOutput.isEmpty() ? QString() : QStringLiteral("\n") + result.standardOutput));
}

void GitPanelWidget::showDiffForSelection()
{
    auto* list = qobject_cast<QListWidget*>(sender()); if (list == nullptr) return;
    const QStringList paths = selectedPaths(list); if (paths.isEmpty()) return;
    const bool staged = list == m_stagedList;
    m_service->requestDiff(paths.first(), staged);
}

void GitPanelWidget::requestStage()
{
    QStringList paths = selectedPaths(m_changesList); paths.append(selectedPaths(m_untrackedList)); m_service->stage(paths);
}
void GitPanelWidget::requestUnstage() { m_service->unstage(selectedPaths(m_stagedList)); }
void GitPanelWidget::requestDiscard() { emit destructiveOperationRequested(GitOperation::Discard, selectedPaths(m_changesList)); }
void GitPanelWidget::requestCommit() { m_service->commit(m_commitMessage->toPlainText()); }
void GitPanelWidget::requestFetch() { if (!m_snapshot.remotes.isEmpty()) m_service->fetch(m_snapshot.remotes.first()); else setTechnicalText(m_outputView, QStringLiteral("لا يوجد مستودع بعيد للجلب.")); }
void GitPanelWidget::requestPull() { emit pullRequested(); }
void GitPanelWidget::requestPush() { m_service->push(); }
void GitPanelWidget::requestCreateBranch() { m_service->createBranch(m_branchEdit->text()); }
void GitPanelWidget::requestSwitchBranch() { emit branchSwitchRequested(m_branchEdit->text()); }
void GitPanelWidget::applyDiff(const QString& text, const QString& error) { setTechnicalText(m_diffView, error.isEmpty() ? text : error); }

void GitPanelWidget::applyHistory(const QVector<GitHistoryEntry>& entries, const QString& error)
{
    m_historyList->clear(); if (!error.isEmpty()) { m_historyList->addItem(error); return; }
    for (const GitHistoryEntry& entry : entries) {
        auto* item = new QListWidgetItem(QStringLiteral("%1  %2\n%3").arg(entry.hash.left(8), entry.subject, entry.author));
        item->setToolTip(entry.hash + QStringLiteral("\n") + entry.date.toString(Qt::ISODate) + QStringLiteral("\n") + entry.decorations);
        m_historyList->addItem(item);
    }
}

QStringList GitPanelWidget::selectedPaths(QListWidget* const list) const
{
    QStringList result; for (QListWidgetItem* item : list->selectedItems()) result << item->data(Qt::UserRole).toString(); return result;
}

void GitPanelWidget::rebuildChangeLists(const GitRepositorySnapshot& snapshot)
{
    for (QListWidget* list : {m_conflictsList, m_stagedList, m_changesList, m_untrackedList}) list->clear();
    for (const GitFileStatus& status : snapshot.files) {
        QListWidget* target = status.isConflict() ? m_conflictsList : (status.isStaged() ? m_stagedList : (status.state == VersionControlState::Untracked ? m_untrackedList : m_changesList));
        auto* item = new QListWidgetItem(QStringLiteral("%1 — %2").arg(stateName(status.state), status.relativePath));
        item->setData(Qt::UserRole, status.relativePath);
        item->setToolTip(status.originalRelativePath.isEmpty() ? status.relativePath : status.relativePath + QStringLiteral(" ← ") + status.originalRelativePath);
        target->addItem(item);
    }
}

void GitPanelWidget::setBusy(const bool busy)
{
    for (QPushButton* button : {m_stageButton, m_unstageButton, m_discardButton, m_fetchButton, m_pullButton, m_pushButton, m_refreshButton}) button->setEnabled(!busy && m_snapshot.repository);
}
void GitPanelWidget::setTechnicalText(QPlainTextEdit* const view, const QString& text) { view->setPlainText(text.left(300000)); }
