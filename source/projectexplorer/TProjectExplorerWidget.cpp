#include "TProjectExplorerWidget.h"
#include "GitStatusService.h"
#include "ProjectFileProxyModel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace {

class BuiltinFileIconProvider final : public IFileIconProvider {
public:
    [[nodiscard]] int priority() const override { return -100; }

    [[nodiscard]] QIcon iconFor(const FileIconContext& context) const override
    {
        if (context.isDirectory) {
            return QApplication::style()->standardIcon(QStyle::SP_DirIcon);
        }
        const QString suffix = context.suffix.toLower();
        if (suffix == QStringLiteral("alif") || suffix == QStringLiteral("aliflib")) {
            return QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView);
        }
        if (suffix == QStringLiteral("json") || suffix == QStringLiteral("yaml")
            || suffix == QStringLiteral("yml") || suffix == QStringLiteral("ini")) {
            return QApplication::style()->standardIcon(QStyle::SP_FileDialogContentsView);
        }
        return QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    }
};

class GitDecorationProvider final : public IFileDecorationProvider {
public:
    explicit GitDecorationProvider(GitStatusService* const service) : m_service(service) {}
    [[nodiscard]] int priority() const override { return 100; }

    [[nodiscard]] FileDecoration decorationFor(const FileIconContext& context) const override
    {
        if (m_service == nullptr) {
            return {};
        }
        const VersionControlState state = m_service->statusForRelativePath(context.rootRelativePath);
        FileDecoration decoration;
        decoration.versionControlState = state;
        switch (state) {
        case VersionControlState::Modified:
            decoration.foreground = QColor(QStringLiteral("#f59e0b"));
            decoration.tooltip = QStringLiteral("معدّل في Git");
            decoration.accessibleDescription = decoration.tooltip;
            break;
        case VersionControlState::Added:
            decoration.foreground = QColor(QStringLiteral("#4ade80"));
            decoration.tooltip = QStringLiteral("مضاف أو جاهز للإيداع في Git");
            decoration.accessibleDescription = decoration.tooltip;
            break;
        case VersionControlState::Untracked:
            decoration.foreground = QColor(QStringLiteral("#22d3ee"));
            decoration.tooltip = QStringLiteral("غير متتبّع في Git");
            decoration.accessibleDescription = decoration.tooltip;
            break;
        case VersionControlState::Deleted:
            decoration.foreground = QColor(QStringLiteral("#f87171"));
            decoration.tooltip = QStringLiteral("محذوف في Git");
            decoration.accessibleDescription = decoration.tooltip;
            break;
        case VersionControlState::Renamed:
            decoration.foreground = QColor(QStringLiteral("#c084fc"));
            decoration.tooltip = QStringLiteral("أُعيدت تسميته في Git");
            decoration.accessibleDescription = decoration.tooltip;
            break;
        case VersionControlState::Conflicted:
            decoration.foreground = QColor(QStringLiteral("#f43f5e"));
            decoration.tooltip = QStringLiteral("تعارض Git يتطلب المعالجة");
            decoration.accessibleDescription = decoration.tooltip;
            break;
        case VersionControlState::Ignored:
            decoration.foreground = QColor(QStringLiteral("#64748b"));
            decoration.tooltip = QStringLiteral("متجاهل في Git");
            decoration.accessibleDescription = decoration.tooltip;
            break;
        case VersionControlState::Clean:
        case VersionControlState::Unavailable:
            break;
        }
        return decoration;
    }

private:
    GitStatusService* m_service = nullptr;
};

QColor statusMarkerColor(const VersionControlState state)
{
    switch (state) {
    case VersionControlState::Modified: return QColor(QStringLiteral("#f59e0b"));
    case VersionControlState::Added: return QColor(QStringLiteral("#4ade80"));
    case VersionControlState::Untracked: return QColor(QStringLiteral("#22d3ee"));
    case VersionControlState::Deleted: return QColor(QStringLiteral("#f87171"));
    case VersionControlState::Renamed: return QColor(QStringLiteral("#c084fc"));
    case VersionControlState::Conflicted: return QColor(QStringLiteral("#f43f5e"));
    case VersionControlState::Ignored: return QColor(QStringLiteral("#64748b"));
    case VersionControlState::Clean:
    case VersionControlState::Unavailable:
        return {};
    }
    return {};
}

} // namespace

class ProjectExplorerItemDelegate final : public QStyledItemDelegate {
public:
    explicit ProjectExplorerItemDelegate(ProjectExplorerWidget* const explorer)
        : QStyledItemDelegate(explorer), m_explorer(explorer) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem styledOption(option);
        initStyleOption(&styledOption, index);
        const FileIconContext context = m_explorer->contextForProxyIndex(index);
        const FileDecoration decoration = m_explorer->resolvedDecoration(context);
        styledOption.icon = m_explorer->resolvedIcon(context);
        if (decoration.foreground.isValid()
            && !(styledOption.state & QStyle::State_Selected)) {
            styledOption.palette.setColor(QPalette::Text, decoration.foreground);
        }
        QStyledItemDelegate::paint(painter, styledOption, index);

        const QColor marker = statusMarkerColor(decoration.versionControlState);
        if (marker.isValid()) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(marker);
            const QRect markerRect = option.direction == Qt::RightToLeft
                ? QRect(option.rect.left() + 3, option.rect.center().y() - 3, 6, 6)
                : QRect(option.rect.right() - 9, option.rect.center().y() - 3, 6, 6);
            painter->drawEllipse(markerRect);
            painter->restore();
        }
    }

    bool helpEvent(QHelpEvent* event, QAbstractItemView* view,
                   const QStyleOptionViewItem& option, const QModelIndex& index) override
    {
        const FileIconContext context = m_explorer->contextForProxyIndex(index);
        const FileDecoration decoration = m_explorer->resolvedDecoration(context);
        if (!decoration.tooltip.isEmpty()) {
            const QString baseToolTip = index.data(Qt::ToolTipRole).toString();
            QToolTip::showText(event->globalPos(), baseToolTip.isEmpty()
                ? decoration.tooltip : baseToolTip + QStringLiteral("\n") + decoration.tooltip, view);
            return true;
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

private:
    ProjectExplorerWidget* m_explorer = nullptr;
};

ProjectExplorerWidget::ProjectExplorerWidget(QWidget* const parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ProjectExplorerWidget"));
    setAccessibleName(QStringLiteral("مستكشف ملفات المشروع"));
    setMinimumWidth(180);

    m_fileSystemModel = new QFileSystemModel(this);
    m_fileSystemModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Drives);
    m_proxyModel = new ProjectFileProxyModel(this);
    m_proxyModel->setSourceModel(m_fileSystemModel);
    m_gitStatusService = new GitStatusService(this);
    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(140);

    m_iconProviders.push_back(std::make_shared<BuiltinFileIconProvider>());
    m_decorationProviders.push_back(std::make_shared<GitDecorationProvider>(m_gitStatusService));

    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(7, 7, 7, 7);
    layout->setSpacing(6);

    auto* const headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(2, 0, 2, 0);
    m_rootLabel = new QLabel(QStringLiteral("لا يوجد مشروع مفتوح"), this);
    m_rootLabel->setObjectName(QStringLiteral("ProjectExplorerRootLabel"));
    m_rootLabel->setAccessibleName(QStringLiteral("جذر المشروع"));
    m_rootLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_rootLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_refreshButton = new QToolButton(this);
    m_refreshButton->setObjectName(QStringLiteral("ProjectExplorerRefreshButton"));
    m_refreshButton->setIcon(QIcon(":/icons/resources/folder-sync.svg"));
    m_refreshButton->setToolTip(QStringLiteral("تحديث الملفات وحالة Git"));
    m_refreshButton->setAccessibleName(m_refreshButton->toolTip());
    m_showHiddenButton = new QToolButton(this);
    m_showHiddenButton->setObjectName(QStringLiteral("ProjectExplorerHiddenButton"));
    m_showHiddenButton->setIcon(QIcon(":/icons/resources/eye.svg"));
    m_showHiddenButton->setCheckable(true);
    m_showHiddenButton->setToolTip(QStringLiteral("إظهار الملفات المخفية"));
    m_showHiddenButton->setAccessibleName(m_showHiddenButton->toolTip());
    headerLayout->addWidget(m_rootLabel, 1);
    headerLayout->addWidget(m_refreshButton);
    headerLayout->addWidget(m_showHiddenButton);
    layout->addLayout(headerLayout);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName(QStringLiteral("ProjectExplorerFilterEdit"));
    m_filterEdit->setPlaceholderText(QStringLiteral("تصفية ملفات المشروع…"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setAccessibleName(QStringLiteral("تصفية ملفات المشروع"));
    layout->addWidget(m_filterEdit);

    m_emptyLabel = new QLabel(QStringLiteral("اختر مجلداً لعرض ملفات المشروع."), this);
    m_emptyLabel->setObjectName(QStringLiteral("ProjectExplorerEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    layout->addWidget(m_emptyLabel, 1);

    m_treeView = new QTreeView(this);
    m_treeView->setObjectName(QStringLiteral("ProjectExplorerTreeView"));
    m_treeView->setAccessibleName(QStringLiteral("ملفات المشروع"));
    m_treeView->setModel(m_proxyModel);
    m_treeView->setHeaderHidden(true);
    m_treeView->setRootIsDecorated(true);
    m_treeView->setItemsExpandable(true);
    m_treeView->setAnimated(true);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setSortingEnabled(true);
    m_treeView->sortByColumn(0, Qt::AscendingOrder);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setLayoutDirection(Qt::RightToLeft);
    m_treeView->setItemDelegate(new ProjectExplorerItemDelegate(this));
    for (int column = 1; column < 4; ++column) {
        m_treeView->hideColumn(column);
    }
    layout->addWidget(m_treeView, 1);

    setStyleSheet(QStringLiteral(R"(
        QWidget#ProjectExplorerWidget {
            background: #0f172a;
            color: #cbd5e1;
        }
        QLabel#ProjectExplorerRootLabel {
            color: #e2e8f0;
            font-weight: 500;
            padding: 3px 4px;
        }
        QLabel#ProjectExplorerEmptyLabel {
            color: #64748b;
            padding: 20px;
        }
        QLineEdit#ProjectExplorerFilterEdit {
            background: #111d33;
            color: #e2e8f0;
            border: 1px solid #263a57;
            border-radius: 5px;
            padding: 5px 8px;
        }
        QLineEdit#ProjectExplorerFilterEdit:focus {
            border-color: #3b82f6;
        }
        QToolButton#ProjectExplorerRefreshButton,
        QToolButton#ProjectExplorerHiddenButton {
            color: #93c5fd;
            background: transparent;
            border: none;
            border-radius: 4px;
            padding: 4px;
        }
        QToolButton#ProjectExplorerRefreshButton:hover,
        QToolButton#ProjectExplorerHiddenButton:hover,
        QToolButton#ProjectExplorerHiddenButton:checked {
            background: #1e3a5f;
            color: #ffffff;
        }
        QTreeView#ProjectExplorerTreeView {
            background: transparent;
            border: none;
            color: #cbd5e1;
            outline: none;
        }
        QTreeView#ProjectExplorerTreeView::item {
            min-height: 24px;
            padding: 2px 4px;
        }
        QTreeView#ProjectExplorerTreeView::item:hover {
            background: #172b4d;
        }
        QTreeView#ProjectExplorerTreeView::item:selected {
            background: #1e40af;
            color: #ffffff;
        }
    )"));

    connect(m_filterEdit, &QLineEdit::textChanged, m_filterTimer, qOverload<>(&QTimer::start));
    connect(m_filterTimer, &QTimer::timeout, this, &ProjectExplorerWidget::applyFilter);
    connect(m_showHiddenButton, &QToolButton::toggled, this, &ProjectExplorerWidget::setShowHiddenFiles);
    connect(m_refreshButton, &QToolButton::clicked, this, &ProjectExplorerWidget::refresh);
    connect(m_treeView, &QTreeView::activated, this, &ProjectExplorerWidget::handleActivation);
    connect(m_treeView, &QTreeView::customContextMenuRequested,
            this, &ProjectExplorerWidget::requestContextMenu);
    connect(m_treeView, &QTreeView::expanded, this, [this](const QModelIndex&) { persistSettings(); });
    connect(m_treeView, &QTreeView::collapsed, this, [this](const QModelIndex&) { persistSettings(); });
    connect(m_gitStatusService, &GitStatusService::statusChanged,
            m_treeView->viewport(), qOverload<>(&QWidget::update));
    connect(m_fileSystemModel, &QFileSystemModel::directoryLoaded,
            this, [this](const QString&) { m_gitStatusService->requestRefresh(); });
    connect(m_fileSystemModel, &QFileSystemModel::rowsInserted,
            this, [this](const QModelIndex&, int, int) { m_gitStatusService->requestRefresh(); });

    updateRootPresentation();
}

void ProjectExplorerWidget::setProjectRoot(const QString& rootPath)
{
    const QString normalized = ProjectFileProxyModel::normalizedPath(rootPath);
    if (m_projectRoot == normalized) {
        return;
    }
    persistSettings();
    m_projectRoot = normalized;
    m_proxyModel->setProjectRoot(m_projectRoot);
    m_fileSystemModel->setRootPath(m_projectRoot);
    const QModelIndex sourceRoot = m_fileSystemModel->index(m_projectRoot);
    m_treeView->setRootIndex(m_proxyModel->mapFromSource(sourceRoot));
    m_gitStatusService->setProjectRoot(m_projectRoot);
    updateRootPresentation();
    restoreSettings();
}

QString ProjectExplorerWidget::projectRoot() const
{
    return m_projectRoot;
}

void ProjectExplorerWidget::setShowHiddenFiles(const bool show)
{
    m_showHiddenButton->setChecked(show);
    m_proxyModel->setShowHiddenFiles(show);
    persistSettings();
}

bool ProjectExplorerWidget::showHiddenFiles() const
{
    return m_proxyModel->showHiddenFiles();
}

void ProjectExplorerWidget::addIconProvider(std::shared_ptr<IFileIconProvider> provider)
{
    if (provider == nullptr) {
        return;
    }
    m_iconProviders.push_back(std::move(provider));
    std::stable_sort(m_iconProviders.begin(), m_iconProviders.end(),
                     [](const auto& left, const auto& right) { return left->priority() > right->priority(); });
    m_treeView->viewport()->update();
}

void ProjectExplorerWidget::addDecorationProvider(std::shared_ptr<IFileDecorationProvider> provider)
{
    if (provider == nullptr) {
        return;
    }
    m_decorationProviders.push_back(std::move(provider));
    std::stable_sort(m_decorationProviders.begin(), m_decorationProviders.end(),
                     [](const auto& left, const auto& right) { return left->priority() > right->priority(); });
    m_treeView->viewport()->update();
}

QTreeView* ProjectExplorerWidget::treeView() const { return m_treeView; }
ProjectFileProxyModel* ProjectExplorerWidget::proxyModel() const { return m_proxyModel; }
GitStatusService* ProjectExplorerWidget::gitStatusService() const { return m_gitStatusService; }

void ProjectExplorerWidget::selectPath(const QString& absolutePath)
{
    const QModelIndex sourceIndex = m_fileSystemModel->index(absolutePath);
    const QModelIndex proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
    if (!proxyIndex.isValid()) {
        return;
    }
    QModelIndex parent = proxyIndex.parent();
    while (parent.isValid()) {
        m_treeView->expand(parent);
        parent = parent.parent();
    }
    m_treeView->setCurrentIndex(proxyIndex);
    m_treeView->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
    persistSettings();
}

void ProjectExplorerWidget::refresh()
{
    m_proxyModel->invalidate();
    m_gitStatusService->requestRefresh();
    m_treeView->viewport()->update();
}

bool ProjectExplorerWidget::eventFilter(QObject* const watched, QEvent* const event)
{
    return QWidget::eventFilter(watched, event);
}

void ProjectExplorerWidget::requestContextMenu(const QPoint& position)
{
    const QModelIndex index = m_treeView->indexAt(position);
    if (index.isValid()) {
        m_treeView->setCurrentIndex(index);
    }
    const QString path = selectedPath();
    const QString directory = selectedDirectoryPath();
    QMenu menu(this);
    menu.setLayoutDirection(Qt::RightToLeft);
    QAction* const newFileAction = menu.addAction(QStringLiteral("ملف جديد"));
    QAction* const newFolderAction = menu.addAction(QStringLiteral("مجلد جديد"));
    QAction* const renameAction = menu.addAction(QStringLiteral("إعادة تسمية"));
    QAction* const deleteAction = menu.addAction(QStringLiteral("حذف"));
    menu.addSeparator();
    QAction* const revealAction = menu.addAction(QStringLiteral("فتح في مدير الملفات"));

    const bool hasDirectory = !directory.isEmpty();
    const bool editablePath = !path.isEmpty() && path != m_projectRoot;
    newFileAction->setEnabled(hasDirectory);
    newFolderAction->setEnabled(hasDirectory);
    renameAction->setEnabled(editablePath);
    deleteAction->setEnabled(editablePath);
    revealAction->setEnabled(!path.isEmpty());

    QAction* const selected = menu.exec(m_treeView->viewport()->mapToGlobal(position));
    if (selected == newFileAction) showCreateDialog(false);
    else if (selected == newFolderAction) showCreateDialog(true);
    else if (selected == renameAction) showRenameDialog();
    else if (selected == deleteAction) showDeleteConfirmation();
    else if (selected == revealAction) emit revealRequested(path);
}

void ProjectExplorerWidget::handleActivation(const QModelIndex& proxyIndex)
{
    const FileIconContext context = contextForProxyIndex(proxyIndex);
    if (context.absolutePath.isEmpty()) {
        return;
    }
    if (context.isDirectory) {
        m_treeView->setExpanded(proxyIndex, !m_treeView->isExpanded(proxyIndex));
        persistSettings();
        return;
    }
    emit fileActivationRequested(context.absolutePath);
}

void ProjectExplorerWidget::applyFilter()
{
    m_proxyModel->setFilterText(m_filterEdit->text());
    persistSettings();
}

void ProjectExplorerWidget::updateRootPresentation()
{
    const bool hasRoot = !m_projectRoot.isEmpty() && QDir(m_projectRoot).exists();
    m_rootLabel->setText(hasRoot ? QFileInfo(m_projectRoot).fileName() : QStringLiteral("لا يوجد مشروع مفتوح"));
    m_rootLabel->setToolTip(hasRoot ? m_projectRoot : QString());
    m_treeView->setVisible(hasRoot);
    m_emptyLabel->setVisible(!hasRoot);
    m_filterEdit->setEnabled(hasRoot);
    m_showHiddenButton->setEnabled(hasRoot);
    m_refreshButton->setEnabled(hasRoot);
}

QString ProjectExplorerWidget::selectedPath() const
{
    const FileIconContext context = contextForProxyIndex(m_treeView->currentIndex());
    return context.absolutePath;
}

QString ProjectExplorerWidget::selectedDirectoryPath() const
{
    const FileIconContext context = contextForProxyIndex(m_treeView->currentIndex());
    if (context.absolutePath.isEmpty()) {
        return m_projectRoot;
    }
    return context.isDirectory ? context.absolutePath : QFileInfo(context.absolutePath).absolutePath();
}

FileIconContext ProjectExplorerWidget::contextForProxyIndex(const QModelIndex& proxyIndex) const
{
    if (!proxyIndex.isValid()) {
        return {};
    }
    const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const QFileInfo info = m_fileSystemModel->fileInfo(sourceIndex);
    FileIconContext context;
    context.absolutePath = ProjectFileProxyModel::normalizedPath(info.absoluteFilePath());
    context.rootRelativePath = QDir(m_projectRoot).relativeFilePath(context.absolutePath)
        .replace(QLatin1Char('\\'), QLatin1Char('/'));
    context.fileName = info.fileName();
    context.suffix = info.suffix();
    context.isDirectory = info.isDir();
    context.isSymbolicLink = info.isSymLink();
    return context;
}

QIcon ProjectExplorerWidget::resolvedIcon(const FileIconContext& context) const
{
    for (const auto& provider : m_iconProviders) {
        const QIcon icon = provider->iconFor(context);
        if (!icon.isNull()) {
            return icon;
        }
    }
    return QApplication::style()->standardIcon(QStyle::SP_FileIcon);
}

FileDecoration ProjectExplorerWidget::resolvedDecoration(const FileIconContext& context) const
{
    for (const auto& provider : m_decorationProviders) {
        const FileDecoration decoration = provider->decorationFor(context);
        if (decoration.isValid()) {
            return decoration;
        }
    }
    return {};
}

void ProjectExplorerWidget::showCreateDialog(const bool createFolder)
{
    const QString directory = selectedDirectoryPath();
    bool accepted = false;
    const QString name = QInputDialog::getText(this,
                                                createFolder ? QStringLiteral("مجلد جديد") : QStringLiteral("ملف جديد"),
                                                QStringLiteral("الاسم:"), QLineEdit::Normal, {}, &accepted);
    if (!accepted || name.trimmed().isEmpty()) {
        return;
    }
    if (createFolder) emit createFolderRequested(directory, name.trimmed());
    else emit createFileRequested(directory, name.trimmed());
}

void ProjectExplorerWidget::showRenameDialog()
{
    const QString path = selectedPath();
    if (path.isEmpty() || path == m_projectRoot) {
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("إعادة تسمية"), QStringLiteral("الاسم الجديد:"),
                                                QLineEdit::Normal, QFileInfo(path).fileName(), &accepted);
    if (accepted && !name.trimmed().isEmpty() && name.trimmed() != QFileInfo(path).fileName()) {
        emit renameRequested(path, name.trimmed());
    }
}

void ProjectExplorerWidget::showDeleteConfirmation()
{
    const QString path = selectedPath();
    if (path.isEmpty() || path == m_projectRoot) {
        return;
    }
    const auto reply = QMessageBox::warning(this, QStringLiteral("حذف عنصر"),
                                            QStringLiteral("سيُنقل «%1» إلى سلة المحذوفات إن كانت متاحة. هل تريد المتابعة؟")
                                                .arg(QFileInfo(path).fileName()),
                                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (reply == QMessageBox::Yes) {
        emit deleteRequested(path);
    }
}

void ProjectExplorerWidget::restoreSettings()
{
    if (m_projectRoot.isEmpty()) {
        return;
    }
    QSettings settings(QStringLiteral("Alif"), QStringLiteral("Taif"));
    const QString group = QStringLiteral("ProjectExplorer/%1")
        .arg(QString::number(qHash(m_projectRoot)));
    settings.beginGroup(group);
    const bool showHidden = settings.value(QStringLiteral("showHidden"), false).toBool();
    const QString filter = settings.value(QStringLiteral("filter")).toString();
    const QString selectedRelative = settings.value(QStringLiteral("selectedPath")).toString();
    settings.endGroup();
    setShowHiddenFiles(showHidden);
    m_filterEdit->setText(filter);
    if (!selectedRelative.isEmpty()) {
        selectPath(QDir(m_projectRoot).filePath(selectedRelative));
    }
}

void ProjectExplorerWidget::persistSettings() const
{
    if (m_projectRoot.isEmpty()) {
        return;
    }
    QSettings settings(QStringLiteral("Alif"), QStringLiteral("Taif"));
    const QString group = QStringLiteral("ProjectExplorer/%1")
        .arg(QString::number(qHash(m_projectRoot)));
    settings.beginGroup(group);
    settings.setValue(QStringLiteral("showHidden"), showHiddenFiles());
    settings.setValue(QStringLiteral("filter"), m_filterEdit->text());
    const QString path = selectedPath();
    settings.setValue(QStringLiteral("selectedPath"), path.isEmpty() ? QString()
        : QDir(m_projectRoot).relativeFilePath(path));
    settings.endGroup();
}
