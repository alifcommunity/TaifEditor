#pragma once

#include <QColor>
#include <QIcon>
#include <QMetaType>
#include <QString>

#include <memory>

/** Read-only source-control state used only for Explorer decoration. */
enum class VersionControlState : quint8 {
    Unavailable,
    Clean,
    Modified,
    Added,
    Untracked,
    Deleted,
    Renamed,
    Conflicted,
    Ignored
};

struct FileIconContext final {
    QString absolutePath;
    QString rootRelativePath;
    QString fileName;
    QString suffix;
    bool isDirectory = false;
    bool isSymbolicLink = false;
};

struct FileDecoration final {
    QColor foreground;
    QString tooltip;
    QString accessibleDescription;
    VersionControlState versionControlState = VersionControlState::Unavailable;

    [[nodiscard]] bool isValid() const { return foreground.isValid() || !tooltip.isEmpty(); }
};

class IFileIconProvider {
public:
    virtual ~IFileIconProvider() = default;
    [[nodiscard]] virtual int priority() const { return 0; }
    [[nodiscard]] virtual QIcon iconFor(const FileIconContext& context) const = 0;
};

class IFileDecorationProvider {
public:
    virtual ~IFileDecorationProvider() = default;
    [[nodiscard]] virtual int priority() const { return 0; }
    [[nodiscard]] virtual FileDecoration decorationFor(const FileIconContext& context) const = 0;
};

enum class ProjectFileOperationKind : quint8 {
    CreateFile,
    CreateFolder,
    Rename,
    MoveToTrash,
    PermanentlyDelete,
    Reveal
};

struct ProjectFileOperationResult final {
    bool succeeded = false;
    QString sourcePath;
    QString destinationPath;
    QString userMessage;
    QString detail;
};

Q_DECLARE_METATYPE(VersionControlState)
Q_DECLARE_METATYPE(FileDecoration)
Q_DECLARE_METATYPE(ProjectFileOperationResult)
