#pragma once

#include "GitTypes.h"

#include <QWidget>

class GitRepositoryService;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QToolButton;

/** Interactive right-side Git workflow panel. Git execution stays in GitRepositoryService. */
class GitPanelWidget final : public QWidget {
    Q_OBJECT
public:
    explicit GitPanelWidget(GitRepositoryService* service, QWidget* parent = nullptr);

    void setProjectRoot(const QString& rootPath);
    [[nodiscard]] QString projectRoot() const;
    void refresh();

signals:
    void openFileRequested(const QString& absolutePath);
    void destructiveOperationRequested(GitOperation operation, const QStringList& relativePaths);
    void branchSwitchRequested(const QString& branch);
    void pullRequested();

private slots:
    void applySnapshot(const GitRepositorySnapshot& snapshot);
    void applyOperationResult(const GitCommandResult& result);
    void showDiffForSelection();
    void requestStage();
    void requestUnstage();
    void requestDiscard();
    void requestCommit();
    void requestFetch();
    void requestPull();
    void requestPush();
    void requestCreateBranch();
    void requestSwitchBranch();
    void applyDiff(const QString& text, const QString& error);
    void applyHistory(const QVector<GitHistoryEntry>& entries, const QString& error);

private:
    [[nodiscard]] QStringList selectedPaths(QListWidget* list) const;
    void rebuildChangeLists(const GitRepositorySnapshot& snapshot);
    void setBusy(bool busy);
    void setTechnicalText(QPlainTextEdit* view, const QString& text);

    GitRepositoryService* m_service = nullptr;
    QString m_projectRoot;
    GitRepositorySnapshot m_snapshot;
    QLabel* m_repositoryLabel = nullptr;
    QLabel* m_branchLabel = nullptr;
    QLabel* m_syncLabel = nullptr;
    QLabel* m_stateLabel = nullptr;
    QListWidget* m_conflictsList = nullptr;
    QListWidget* m_stagedList = nullptr;
    QListWidget* m_changesList = nullptr;
    QListWidget* m_untrackedList = nullptr;
    QPlainTextEdit* m_diffView = nullptr;
    QPlainTextEdit* m_outputView = nullptr;
    QTextEdit* m_commitMessage = nullptr;
    QLineEdit* m_branchEdit = nullptr;
    QListWidget* m_historyList = nullptr;
    QPushButton* m_stageButton = nullptr;
    QPushButton* m_unstageButton = nullptr;
    QPushButton* m_discardButton = nullptr;
    QPushButton* m_commitButton = nullptr;
    QPushButton* m_fetchButton = nullptr;
    QPushButton* m_pullButton = nullptr;
    QPushButton* m_pushButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
};
