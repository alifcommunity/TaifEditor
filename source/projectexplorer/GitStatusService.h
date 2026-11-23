#pragma once

#include "ProjectExplorerTypes.h"

#include <QObject>
#include <QHash>
#include <QProcess>
#include <QTimer>

/**
 * Runs a fixed read-only Git porcelain request asynchronously and exposes a
 * normalized root-relative status map for explorer decoration only.
 */
class GitStatusService final : public QObject {
    Q_OBJECT
public:
    explicit GitStatusService(QObject* parent = nullptr);
    ~GitStatusService() override;

    void setProjectRoot(const QString& rootPath);
    [[nodiscard]] QString projectRoot() const;
    void requestRefresh();

    [[nodiscard]] VersionControlState statusForRelativePath(const QString& relativePath) const;
    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] bool isRepository() const;
    [[nodiscard]] QString statusDetailForRelativePath(const QString& relativePath) const;

signals:
    void statusChanged();
    void availabilityChanged(bool available, bool repository);

private slots:
    void startRefresh();
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    struct StatusEntry final {
        VersionControlState state = VersionControlState::Clean;
        QString detail;
    };

    [[nodiscard]] static VersionControlState stateForPorcelain(const QByteArray& xy);
    void clearStatus(bool available, bool repository);

    QProcess m_process;
    QTimer m_debounceTimer;
    QString m_projectRoot;
    QHash<QString, StatusEntry> m_statusByPath;
    bool m_available = false;
    bool m_repository = false;
};
