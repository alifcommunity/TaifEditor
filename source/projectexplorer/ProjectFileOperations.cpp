#include "ProjectFileOperations.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace {

ProjectFileOperationResult success(const QString& source, const QString& destination,
                                   const QString& message)
{
    return {true, source, destination, message, {}};
}

ProjectFileOperationResult failure(const QString& source, const QString& message,
                                   const QString& detail = {})
{
    return {false, source, {}, message, detail};
}

} // namespace

QString ProjectFileOperations::normalizedPath(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool ProjectFileOperations::isInsideRoot(const QString& rootPath, const QString& path)
{
    const QString root = normalizedPath(rootPath);
    const QString candidate = normalizedPath(path);
    if (root.isEmpty() || candidate.isEmpty()) {
        return false;
    }
    if (root == candidate) {
        return true;
    }
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
#if defined(Q_OS_WIN)
    return candidate.startsWith(prefix, Qt::CaseInsensitive);
#else
    return candidate.startsWith(prefix);
#endif
}

bool ProjectFileOperations::isValidChildName(const QString& name, QString* const reason)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral(".") || trimmed == QStringLiteral("..")) {
        if (reason != nullptr) *reason = QStringLiteral("اكتب اسماً صالحاً.");
        return false;
    }
    if (trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\'))
        || trimmed.contains(QChar::Null)) {
        if (reason != nullptr) *reason = QStringLiteral("لا يمكن أن يحتوي الاسم على مسار.");
        return false;
    }
#if defined(Q_OS_WIN)
    static const QRegularExpression invalidWindowsCharacters(QStringLiteral(R"([<>:"/\\|?*])"));
    static const QRegularExpression reservedName(QStringLiteral(
        R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (invalidWindowsCharacters.match(trimmed).hasMatch() || reservedName.match(trimmed).hasMatch()) {
        if (reason != nullptr) *reason = QStringLiteral("الاسم غير صالح في نظام Windows.");
        return false;
    }
#endif
    if (trimmed.endsWith(QLatin1Char('.')) || trimmed.endsWith(QLatin1Char(' '))) {
        if (reason != nullptr) *reason = QStringLiteral("لا يمكن أن ينتهي الاسم بنقطة أو مسافة.");
        return false;
    }
    return true;
}

ProjectFileOperationResult ProjectFileOperations::createFile(const QString& rootPath,
                                                              const QString& directoryPath,
                                                              const QString& name)
{
    QString reason;
    if (!isValidChildName(name, &reason) || !isInsideRoot(rootPath, directoryPath)) {
        return invalidPathResult(ProjectFileOperationKind::CreateFile, directoryPath,
                                 reason.isEmpty() ? QStringLiteral("المجلد المحدد خارج المشروع.") : reason);
    }
    const QString destination = QDir(directoryPath).filePath(name.trimmed());
    if (!isInsideRoot(rootPath, destination) || QFileInfo::exists(destination)) {
        return failure(destination, QStringLiteral("يوجد ملف أو مجلد بالاسم نفسه."));
    }
    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        return failure(destination, QStringLiteral("تعذر إنشاء الملف."), file.errorString());
    }
    file.close();
    return success({}, destination, QStringLiteral("تم إنشاء الملف."));
}

ProjectFileOperationResult ProjectFileOperations::createFolder(const QString& rootPath,
                                                                const QString& directoryPath,
                                                                const QString& name)
{
    QString reason;
    if (!isValidChildName(name, &reason) || !isInsideRoot(rootPath, directoryPath)) {
        return invalidPathResult(ProjectFileOperationKind::CreateFolder, directoryPath,
                                 reason.isEmpty() ? QStringLiteral("المجلد المحدد خارج المشروع.") : reason);
    }
    const QString destination = QDir(directoryPath).filePath(name.trimmed());
    if (!isInsideRoot(rootPath, destination) || QFileInfo::exists(destination)) {
        return failure(destination, QStringLiteral("يوجد ملف أو مجلد بالاسم نفسه."));
    }
    if (!QDir().mkdir(destination)) {
        return failure(destination, QStringLiteral("تعذر إنشاء المجلد."));
    }
    return success({}, destination, QStringLiteral("تم إنشاء المجلد."));
}

ProjectFileOperationResult ProjectFileOperations::renamePath(const QString& rootPath,
                                                              const QString& sourcePath,
                                                              const QString& newName)
{
    QString reason;
    if (!isValidChildName(newName, &reason) || !isInsideRoot(rootPath, sourcePath)
        || normalizedPath(rootPath) == normalizedPath(sourcePath)) {
        return invalidPathResult(ProjectFileOperationKind::Rename, sourcePath,
                                 reason.isEmpty() ? QStringLiteral("لا يمكن إعادة تسمية جذر المشروع.") : reason);
    }
    const QFileInfo source(sourcePath);
    if (!source.exists()) {
        return failure(sourcePath, QStringLiteral("العنصر لم يعد موجوداً."));
    }
    const QString destination = source.dir().filePath(newName.trimmed());
    if (!isInsideRoot(rootPath, destination) || QFileInfo::exists(destination)) {
        return failure(sourcePath, QStringLiteral("يوجد عنصر بالاسم الجديد."));
    }
    bool renamed = false;
    if (source.isDir()) {
        renamed = QDir().rename(source.absoluteFilePath(), destination);
    } else {
        renamed = QFile::rename(source.absoluteFilePath(), destination);
    }
    return renamed ? success(sourcePath, destination, QStringLiteral("تمت إعادة التسمية."))
                   : failure(sourcePath, QStringLiteral("تعذرت إعادة التسمية."));
}

ProjectFileOperationResult ProjectFileOperations::moveToTrash(const QString& rootPath,
                                                               const QString& sourcePath)
{
    if (!isInsideRoot(rootPath, sourcePath) || normalizedPath(rootPath) == normalizedPath(sourcePath)) {
        return invalidPathResult(ProjectFileOperationKind::MoveToTrash, sourcePath,
                                 QStringLiteral("لا يمكن حذف هذا المسار."));
    }
    QFile file(sourcePath);
    if (!file.moveToTrash()) {
        return failure(sourcePath, QStringLiteral("تعذر نقل العنصر إلى سلة المحذوفات."), file.errorString());
    }
    return success(sourcePath, {}, QStringLiteral("تم نقل العنصر إلى سلة المحذوفات."));
}

ProjectFileOperationResult ProjectFileOperations::permanentlyDelete(const QString& rootPath,
                                                                     const QString& sourcePath)
{
    if (!isInsideRoot(rootPath, sourcePath) || normalizedPath(rootPath) == normalizedPath(sourcePath)) {
        return invalidPathResult(ProjectFileOperationKind::PermanentlyDelete, sourcePath,
                                 QStringLiteral("لا يمكن حذف هذا المسار."));
    }
    const QFileInfo source(sourcePath);
    const bool removed = source.isDir() ? QDir(sourcePath).removeRecursively() : QFile::remove(sourcePath);
    return removed ? success(sourcePath, {}, QStringLiteral("تم حذف العنصر نهائياً."))
                   : failure(sourcePath, QStringLiteral("تعذر حذف العنصر."));
}

ProjectFileOperationResult ProjectFileOperations::reveal(const QString& rootPath,
                                                          const QString& sourcePath)
{
    if (!isInsideRoot(rootPath, sourcePath)) {
        return invalidPathResult(ProjectFileOperationKind::Reveal, sourcePath,
                                 QStringLiteral("المسار خارج المشروع."));
    }
    const QFileInfo source(sourcePath);
    const QString target = source.isDir() ? source.absoluteFilePath() : source.absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(target))) {
        return failure(sourcePath, QStringLiteral("تعذر فتح مدير الملفات."));
    }
    return success(sourcePath, target, QStringLiteral("تم فتح موقع العنصر."));
}

ProjectFileOperationResult ProjectFileOperations::invalidPathResult(const ProjectFileOperationKind,
                                                                     const QString& sourcePath,
                                                                     const QString& message)
{
    return failure(sourcePath, message);
}
