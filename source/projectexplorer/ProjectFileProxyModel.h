#pragma once

#include <QFileInfo>
#include <QSortFilterProxyModel>

/**
 * Restricts a QFileSystemModel to one project root and filters names/relative
 * paths without leaking files outside the active local workspace.
 */
class ProjectFileProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ProjectFileProxyModel(QObject* parent = nullptr);

    void setProjectRoot(const QString& rootPath);
    [[nodiscard]] QString projectRoot() const;

    void setFilterText(const QString& text);
    [[nodiscard]] QString filterText() const;

    void setShowHiddenFiles(bool show);
    [[nodiscard]] bool showHiddenFiles() const;

    [[nodiscard]] bool isPathInsideProject(const QString& path) const;
    [[nodiscard]] static QString normalizedPath(const QString& path);

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const override;
    [[nodiscard]] bool lessThan(const QModelIndex& left,
                                const QModelIndex& right) const override;

private:
    [[nodiscard]] bool isExcluded(const QFileInfo& fileInfo) const;
    [[nodiscard]] bool matchesFilter(const QFileInfo& fileInfo) const;

    QString m_projectRoot;
    QString m_filterText;
    bool m_showHiddenFiles = false;
};
