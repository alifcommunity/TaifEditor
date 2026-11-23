#include "ProjectFileProxyModel.h"

#include <QCollator>
#include <QFileInfo>
#include <QFileSystemModel>

ProjectFileProxyModel::ProjectFileProxyModel(QObject* const parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setRecursiveFilteringEnabled(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void ProjectFileProxyModel::setProjectRoot(const QString& rootPath)
{
    const QString normalized = normalizedPath(rootPath);
    if (m_projectRoot == normalized) {
        return;
    }
    beginFilterChange();
    m_projectRoot = normalized;
    endFilterChange(Direction::Rows);
}

QString ProjectFileProxyModel::projectRoot() const
{
    return m_projectRoot;
}

void ProjectFileProxyModel::setFilterText(const QString& text)
{
    const QString normalized = text.trimmed();
    if (m_filterText == normalized) {
        return;
    }
    beginFilterChange();
    m_filterText = normalized;
    endFilterChange(Direction::Rows);
}

QString ProjectFileProxyModel::filterText() const
{
    return m_filterText;
}

void ProjectFileProxyModel::setShowHiddenFiles(const bool show)
{
    if (m_showHiddenFiles == show) {
        return;
    }
    beginFilterChange();
    m_showHiddenFiles = show;
    endFilterChange(Direction::Rows);
}

bool ProjectFileProxyModel::showHiddenFiles() const
{
    return m_showHiddenFiles;
}

bool ProjectFileProxyModel::isPathInsideProject(const QString& path) const
{
    if (m_projectRoot.isEmpty()) {
        return false;
    }
    const QString normalized = normalizedPath(path);
    if (normalized.isEmpty()) {
        return false;
    }
    if (normalized == m_projectRoot) {
        return true;
    }
    const QString rootPrefix = m_projectRoot.endsWith(QLatin1Char('/'))
        ? m_projectRoot : m_projectRoot + QLatin1Char('/');
#if defined(Q_OS_WIN)
    return normalized.startsWith(rootPrefix, Qt::CaseInsensitive);
#else
    return normalized.startsWith(rootPrefix);
#endif
}

QString ProjectFileProxyModel::normalizedPath(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool ProjectFileProxyModel::filterAcceptsRow(const int sourceRow,
                                              const QModelIndex& sourceParent) const
{
    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!sourceIndex.isValid()) {
        return false;
    }
    const auto* const fileModel = qobject_cast<const QFileSystemModel*>(sourceModel());
    if (fileModel == nullptr) {
        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }

    const QFileInfo info = fileModel->fileInfo(sourceIndex);
    if (!isPathInsideProject(info.absoluteFilePath()) || isExcluded(info)) {
        return false;
    }
    // Directories stay visible so a matching descendant remains discoverable,
    // even before QFileSystemModel has lazily enumerated that directory.
    return info.isDir() || matchesFilter(info);
}

bool ProjectFileProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const auto* const fileModel = qobject_cast<const QFileSystemModel*>(sourceModel());
    if (fileModel == nullptr) {
        return QSortFilterProxyModel::lessThan(left, right);
    }
    const QFileInfo leftInfo = fileModel->fileInfo(left);
    const QFileInfo rightInfo = fileModel->fileInfo(right);
    if (leftInfo.isDir() != rightInfo.isDir()) {
        return leftInfo.isDir();
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    return collator.compare(leftInfo.fileName(), rightInfo.fileName()) < 0;
}

bool ProjectFileProxyModel::isExcluded(const QFileInfo& fileInfo) const
{
    const QString name = fileInfo.fileName();
    if (!m_showHiddenFiles && (fileInfo.isHidden() || name.startsWith(QLatin1Char('.')))) {
        return true;
    }
    if (name.endsWith(QStringLiteral(".~")) || name.endsWith(QLatin1Char('~'))) {
        return true;
    }
    return name == QStringLiteral(".taif-recovery");
}

bool ProjectFileProxyModel::matchesFilter(const QFileInfo& fileInfo) const
{
    if (m_filterText.isEmpty()) {
        return true;
    }
    const QString relative = QDir(m_projectRoot).relativeFilePath(normalizedPath(fileInfo.absoluteFilePath()));
    return fileInfo.fileName().contains(m_filterText, Qt::CaseInsensitive)
        || relative.contains(m_filterText, Qt::CaseInsensitive);
}
