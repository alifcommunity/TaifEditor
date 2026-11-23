#pragma once

#include "ProjectExplorerTypes.h"

/** Safe, local filesystem operations scoped to a canonical project root. */
class ProjectFileOperations final {
public:
    ProjectFileOperations() = delete;

    [[nodiscard]] static QString normalizedPath(const QString& path);
    [[nodiscard]] static bool isInsideRoot(const QString& rootPath, const QString& path);
    [[nodiscard]] static bool isValidChildName(const QString& name, QString* reason = nullptr);

    [[nodiscard]] static ProjectFileOperationResult createFile(const QString& rootPath,
                                                                const QString& directoryPath,
                                                                const QString& name);
    [[nodiscard]] static ProjectFileOperationResult createFolder(const QString& rootPath,
                                                                  const QString& directoryPath,
                                                                  const QString& name);
    [[nodiscard]] static ProjectFileOperationResult renamePath(const QString& rootPath,
                                                                const QString& sourcePath,
                                                                const QString& newName);
    [[nodiscard]] static ProjectFileOperationResult moveToTrash(const QString& rootPath,
                                                                 const QString& sourcePath);
    [[nodiscard]] static ProjectFileOperationResult permanentlyDelete(const QString& rootPath,
                                                                       const QString& sourcePath);
    [[nodiscard]] static ProjectFileOperationResult reveal(const QString& rootPath,
                                                            const QString& sourcePath);

private:
    [[nodiscard]] static ProjectFileOperationResult invalidPathResult(ProjectFileOperationKind kind,
                                                                       const QString& sourcePath,
                                                                       const QString& message);
};
