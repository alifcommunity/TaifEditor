#pragma once

#include "ProjectExplorerTypes.h"

#include <QDateTime>
#include <QStringList>
#include <QVector>

#include <optional>

enum class GitOperation : quint8 {
    Stage,
    Unstage,
    Discard,
    Commit,
    Fetch,
    Pull,
    Push,
    SwitchBranch,
    CreateBranch,
    Diff,
    History
};

struct GitFileStatus final {
    QString relativePath;
    QString originalRelativePath;
    VersionControlState state = VersionControlState::Clean;
    QChar indexStatus;
    QChar workTreeStatus;

    [[nodiscard]] bool isStaged() const { return indexStatus != QLatin1Char(' ') && indexStatus != QLatin1Char('?'); }
    [[nodiscard]] bool isConflict() const { return state == VersionControlState::Conflicted; }
};

struct GitHistoryEntry final {
    QString hash;
    QString author;
    QDateTime date;
    QString subject;
    QString decorations;
};

struct GitRepositorySnapshot final {
    bool gitAvailable = false;
    bool repository = false;
    bool busy = false;
    QString projectRoot;
    QString branch;
    QString upstream;
    QStringList remotes;
    int ahead = 0;
    int behind = 0;
    QVector<GitFileStatus> files;
    QString lastError;

    [[nodiscard]] bool isDirty() const { return !files.isEmpty(); }
    [[nodiscard]] int stagedCount() const {
        int count = 0;
        for (const GitFileStatus& status : files) if (status.isStaged()) ++count;
        return count;
    }
};

struct GitCommandResult final {
    GitOperation operation = GitOperation::Stage;
    bool succeeded = false;
    QString standardOutput;
    QString standardError;
    QString userMessage;
};

Q_DECLARE_METATYPE(GitOperation)
Q_DECLARE_METATYPE(GitFileStatus)
Q_DECLARE_METATYPE(GitRepositorySnapshot)
Q_DECLARE_METATYPE(GitCommandResult)
