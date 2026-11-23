#pragma once

#include "ProjectExplorerTypes.h"

#include <QWidget>

#include <memory>

class QFileSystemModel;
class QLineEdit;
class QLabel;
class QTimer;
class QToolButton;
class QTreeView;
class ProjectFileProxyModel;
class GitStatusService;
class ProjectExplorerItemDelegate;

/**
 * A project-scoped Explorer surface. It owns presentation and user intent only;
 * Taif remains responsible for document/session lifecycle around mutations.
 */
class ProjectExplorerWidget final : public QWidget {
    Q_OBJECT
public:
    explicit ProjectExplorerWidget(QWidget* parent = nullptr);

    void setProjectRoot(const QString& rootPath);
    [[nodiscard]] QString projectRoot() const;

    void setShowHiddenFiles(bool show);
    [[nodiscard]] bool showHiddenFiles() const;

    void addIconProvider(std::shared_ptr<IFileIconProvider> provider);
    void addDecorationProvider(std::shared_ptr<IFileDecorationProvider> provider);

    [[nodiscard]] QTreeView* treeView() const;
    [[nodiscard]] ProjectFileProxyModel* proxyModel() const;
    [[nodiscard]] GitStatusService* gitStatusService() const;
    void selectPath(const QString& absolutePath);
    void refresh();

signals:
    void fileActivationRequested(const QString& absolutePath);
    void createFileRequested(const QString& directoryPath, const QString& name);
    void createFolderRequested(const QString& directoryPath, const QString& name);
    void renameRequested(const QString& sourcePath, const QString& newName);
    void deleteRequested(const QString& sourcePath);
    void revealRequested(const QString& sourcePath);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void requestContextMenu(const QPoint& position);
    void handleActivation(const QModelIndex& proxyIndex);
    void applyFilter();
    void updateRootPresentation();

private:
    friend class ProjectExplorerItemDelegate;

    [[nodiscard]] QString selectedPath() const;
    [[nodiscard]] QString selectedDirectoryPath() const;
    [[nodiscard]] FileIconContext contextForProxyIndex(const QModelIndex& proxyIndex) const;
    [[nodiscard]] QIcon resolvedIcon(const FileIconContext& context) const;
    [[nodiscard]] FileDecoration resolvedDecoration(const FileIconContext& context) const;
    void showCreateDialog(bool createFolder);
    void showRenameDialog();
    void showDeleteConfirmation();
    void restoreSettings();
    void persistSettings() const;

    QFileSystemModel* m_fileSystemModel = nullptr;
    ProjectFileProxyModel* m_proxyModel = nullptr;
    GitStatusService* m_gitStatusService = nullptr;
    QLabel* m_rootLabel = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QToolButton* m_showHiddenButton = nullptr;
    QToolButton* m_refreshButton = nullptr;
    QTreeView* m_treeView = nullptr;
    QTimer* m_filterTimer = nullptr;
    QString m_projectRoot;
    std::vector<std::shared_ptr<IFileIconProvider>> m_iconProviders;
    std::vector<std::shared_ptr<IFileDecorationProvider>> m_decorationProviders;
};
