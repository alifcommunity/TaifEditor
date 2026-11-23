#pragma once

#include "GitTypes.h"

#include <QObject>
#include <QProcess>
#include <QTimer>

/** A single-root, serialized, argument-vector Git service shared by the panel and Explorer. */
class GitRepositoryService final : public QObject {
    Q_OBJECT
public:
    explicit GitRepositoryService(QObject* parent = nullptr);
    ~GitRepositoryService() override;

    void setProjectRoot(const QString& rootPath);
    [[nodiscard]] const GitRepositorySnapshot& snapshot() const;
    [[nodiscard]] VersionControlState statusForRelativePath(const QString& relativePath) const;
    [[nodiscard]] QString statusDetailForRelativePath(const QString& relativePath) const;
    void refresh(bool immediate = false);
    void cancelActiveOperation();

    void stage(const QStringList& relativePaths);
    void unstage(const QStringList& relativePaths);
    void discard(const QStringList& relativePaths);
    void commit(const QString& message);
    void fetch(const QString& remote);
    void pull();
    void push();
    void switchBranch(const QString& branch);
    void createBranch(const QString& branch);
    void requestDiff(const QString& relativePath, bool staged);
    void requestHistory(int limit = 50);

    [[nodiscard]] static bool isValidRelativePath(const QString& path);
    [[nodiscard]] static bool isValidRefName(const QString& ref);

signals:
    void snapshotChanged(const GitRepositorySnapshot& snapshot);
    void operationStarted(GitOperation operation);
    void operationFinished(const GitCommandResult& result);
    void diffReady(const QString& text, const QString& error);
    void historyReady(const QVector<GitHistoryEntry>& entries, const QString& error);

private slots:
    void startRefresh();
    void handleQueryFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleCommandFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    enum class QueryStage : quint8 { Status, Remotes };
    void startCommand(GitOperation operation, const QStringList& arguments, const QString& userMessage);
    void finishWithValidationError(GitOperation operation, const QString& message);
    void publishSnapshot();
    void parseStatus(const QByteArray& output);
    void parseRemotes(const QByteArray& output);
    [[nodiscard]] QString normalizedRoot(const QString& path) const;
    [[nodiscard]] QStringList validatedPaths(const QStringList& paths, bool* valid) const;
    void stopProcess(QProcess& process);

    QProcess m_queryProcess;
    QProcess m_commandProcess;
    QTimer m_refreshTimer;
    GitRepositorySnapshot m_snapshot;
    QueryStage m_queryStage = QueryStage::Status;
    GitOperation m_activeOperation = GitOperation::Stage;
    QString m_activeMessage;
    quint64 m_generation = 0;
    bool m_commandBusy = false;
    bool m_refreshPending = false;
    bool m_shuttingDown = false;
};
