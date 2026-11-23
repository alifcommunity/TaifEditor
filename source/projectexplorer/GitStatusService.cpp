#include "GitStatusService.h"

#include <QDir>
#include <QFileInfo>

namespace {

QString normalizedRelativePath(const QString& path)
{
    return QDir::cleanPath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

} // namespace

GitStatusService::GitStatusService(QObject* const parent)
    : QObject(parent)
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(180);
    connect(&m_debounceTimer, &QTimer::timeout, this, &GitStatusService::startRefresh);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &GitStatusService::handleFinished);
}

GitStatusService::~GitStatusService()
{
    m_debounceTimer.stop();
    if (m_process.state() == QProcess::NotRunning) {
        return;
    }
    m_process.terminate();
    if (!m_process.waitForFinished(180)) {
        m_process.kill();
        m_process.waitForFinished(180);
    }
}

void GitStatusService::setProjectRoot(const QString& rootPath)
{
    const QFileInfo info(rootPath);
    const QString canonical = info.canonicalFilePath();
    const QString normalized = QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
    if (m_projectRoot == normalized) {
        return;
    }
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
    }
    m_projectRoot = normalized;
    clearStatus(false, false);
    requestRefresh();
}

QString GitStatusService::projectRoot() const
{
    return m_projectRoot;
}

void GitStatusService::requestRefresh()
{
    if (m_projectRoot.isEmpty()) {
        clearStatus(false, false);
        return;
    }
    m_debounceTimer.start();
}

VersionControlState GitStatusService::statusForRelativePath(const QString& relativePath) const
{
    const auto it = m_statusByPath.constFind(normalizedRelativePath(relativePath));
    return it == m_statusByPath.cend() ? (m_repository ? VersionControlState::Clean
                                                        : VersionControlState::Unavailable)
                                      : it->state;
}

bool GitStatusService::isAvailable() const
{
    return m_available;
}

bool GitStatusService::isRepository() const
{
    return m_repository;
}

QString GitStatusService::statusDetailForRelativePath(const QString& relativePath) const
{
    const auto it = m_statusByPath.constFind(normalizedRelativePath(relativePath));
    return it == m_statusByPath.cend() ? QString() : it->detail;
}

void GitStatusService::startRefresh()
{
    if (m_projectRoot.isEmpty() || m_process.state() != QProcess::NotRunning) {
        return;
    }
    m_process.setWorkingDirectory(m_projectRoot);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start(QStringLiteral("git"), {
        QStringLiteral("status"),
        QStringLiteral("--porcelain=v1"),
        QStringLiteral("-z"),
        QStringLiteral("--ignored=matching"),
        QStringLiteral("--untracked-files=all")
    });
}

void GitStatusService::handleFinished(const int exitCode, const QProcess::ExitStatus exitStatus)
{
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        clearStatus(false, false);
        return;
    }

    const QByteArray output = m_process.readAllStandardOutput();
    const QList<QByteArray> records = output.split('\0');
    QHash<QString, StatusEntry> parsed;
    for (int index = 0; index < records.size(); ++index) {
        const QByteArray& record = records.at(index);
        if (record.size() < 4) {
            continue;
        }
        const QByteArray xy = record.left(2);
        const QString path = normalizedRelativePath(QString::fromUtf8(record.mid(3)));
        if (path.isEmpty() || path == QStringLiteral(".")) {
            continue;
        }

        StatusEntry entry;
        entry.state = stateForPorcelain(xy);
        entry.detail = QString::fromLatin1(xy);
        parsed.insert(path, entry);

        // In -z porcelain rename/copy records, the original path follows the
        // new path. Preserve it too so selection restoration can explain it.
        if ((xy.contains('R') || xy.contains('C')) && index + 1 < records.size()) {
            const QString original = normalizedRelativePath(QString::fromUtf8(records.at(++index)));
            if (!original.isEmpty()) {
                parsed.insert(original, {VersionControlState::Renamed, QString::fromLatin1(xy)});
            }
        }
    }

    const bool availabilityDidChange = !m_available || !m_repository;
    m_statusByPath = std::move(parsed);
    m_available = true;
    m_repository = true;
    if (availabilityDidChange) {
        emit availabilityChanged(true, true);
    }
    emit statusChanged();
}

VersionControlState GitStatusService::stateForPorcelain(const QByteArray& xy)
{
    if (xy.size() < 2) {
        return VersionControlState::Unavailable;
    }
    if (xy == "??") {
        return VersionControlState::Untracked;
    }
    if (xy == "!!") {
        return VersionControlState::Ignored;
    }
    if (xy.contains('U')) {
        return VersionControlState::Conflicted;
    }
    if (xy.contains('R') || xy.contains('C')) {
        return VersionControlState::Renamed;
    }
    if (xy.contains('D')) {
        return VersionControlState::Deleted;
    }
    if (xy.contains('A')) {
        return VersionControlState::Added;
    }
    if (xy.contains('M') || xy.contains('T')) {
        return VersionControlState::Modified;
    }
    return VersionControlState::Clean;
}

void GitStatusService::clearStatus(const bool available, const bool repository)
{
    const bool availabilityDidChange = m_available != available || m_repository != repository;
    const bool hadStatus = !m_statusByPath.isEmpty();
    m_statusByPath.clear();
    m_available = available;
    m_repository = repository;
    if (availabilityDidChange) {
        emit availabilityChanged(available, repository);
    }
    if (availabilityDidChange || hadStatus) {
        emit statusChanged();
    }
}
