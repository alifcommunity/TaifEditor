#include "SessionStore.h"

#include <algorithm>
#include <utility>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>

namespace {

constexpr int kLegacySchemaVersion = 1;
constexpr int kRepositorySchemaVersion = 1;
constexpr auto kSessionsKey = "SavedSessions";
constexpr auto kSchemaVersionKey = "schemaVersion";
constexpr auto kEntriesKey = "entries";
constexpr auto kRepositoryVersionKey = "version";
constexpr auto kRepositoryEntriesKey = "sessions";
constexpr auto kRepositoryFileName = "saved-sessions.json";

QRecursiveMutex& sessionRepositoryMutex() {
    static QRecursiveMutex mutex;
    return mutex;
}

QString normalizedName(const QString& name) {
    return name.trimmed();
}

QString nameKey(const QString& name) {
    return normalizedName(name).toCaseFolded();
}

void assignError(QString* const errorMessage, const QString& message) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

QJsonObject sessionToJson(const SavedSession& session) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), session.id);
    object.insert(QStringLiteral("displayName"), session.displayName);
    object.insert(QStringLiteral("filePaths"), QJsonArray::fromStringList(session.filePaths));
    object.insert(QStringLiteral("activeFilePath"), session.activeFilePath);
    object.insert(QStringLiteral("updatedAt"), session.updatedAt.toString(Qt::ISODateWithMs));
    return object;
}

SavedSession sessionFromJson(const QJsonObject& object) {
    SavedSession session;
    session.id = object.value(QStringLiteral("id")).toString();
    session.displayName = object.value(QStringLiteral("displayName")).toString();
    const QJsonArray paths = object.value(QStringLiteral("filePaths")).toArray();
    session.filePaths.reserve(paths.size());
    for (const QJsonValue& path : paths) {
        if (path.isString()) {
            session.filePaths.append(path.toString());
        }
    }
    session.activeFilePath = object.value(QStringLiteral("activeFilePath")).toString();
    session.updatedAt = QDateTime::fromString(
        object.value(QStringLiteral("updatedAt")).toString(), Qt::ISODateWithMs);
    return session;
}

} // namespace

SessionStore::SessionStore(SettingsScope settingsScope)
    : scope(std::move(settingsScope)) {}

QVector<SavedSession> SessionStore::loadAll(QString* const errorMessage) const {
    QMutexLocker locker(&sessionRepositoryMutex());
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QString primaryPath = repositoryFilePath();
    const QString backupPath = backupRepositoryFilePath();
    QVector<SavedSession> sessions;
    QString primaryError;

    if (QFileInfo::exists(primaryPath)) {
        if (readRepository(primaryPath, &sessions, &primaryError)) {
            return sessions;
        }

        QString backupError;
        if (readRepository(backupPath, &sessions, &backupError)) {
            return sessions;
        }

        assignError(errorMessage,
                    QStringLiteral("تعذر قراءة مستودع الجلسات أو نسخته الاحتياطية: %1")
                        .arg(primaryError.isEmpty() ? backupError : primaryError));
        return {};
    }

    // A backup may be the only durable file after an interrupted first write.
    if (QFileInfo::exists(backupPath)) {
        QString backupError;
        if (readRepository(backupPath, &sessions, &backupError)) {
            return sessions;
        }
        assignError(errorMessage,
                    QStringLiteral("تعذر قراءة النسخة الاحتياطية للجلسات: %1").arg(backupError));
        return {};
    }

    sessions = loadLegacySessions();
    if (sessions.isEmpty()) {
        return sessions;
    }

    QString migrationError;
    if (!writeRepositoryAtomically(sessions, &migrationError)) {
        // Legacy data remains available because migration never removes it.
        assignError(errorMessage,
                    QStringLiteral("تمت قراءة الجلسات القديمة، لكن تعذر دمجها: %1")
                        .arg(migrationError));
    }
    return sessions;
}

bool SessionStore::saveAll(const QVector<SavedSession>& sessions,
                           QString* const errorMessage) const {
    QMutexLocker locker(&sessionRepositoryMutex());
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QVector<SavedSession> normalizedSessions;
    if (!validateAndNormalize(sessions, &normalizedSessions, errorMessage)) {
        return false;
    }
    return writeRepositoryAtomically(normalizedSessions, errorMessage);
}

QString SessionStore::repositoryFilePath() const
{
    if (!scope.fileName.isEmpty()) {
        const QFileInfo settingsFile(scope.fileName);
        return QDir(settingsFile.absolutePath()).filePath(
            settingsFile.completeBaseName() + QStringLiteral(".sessions.json"));
    }

    QString root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (root.isEmpty()) {
        root = QDir::homePath();
    }
    return QDir(root).filePath(QString::fromLatin1(kRepositoryFileName));
}

QString SessionStore::backupRepositoryFilePath() const
{
    return repositoryFilePath() + QStringLiteral(".bak");
}

bool SessionStore::create(SavedSession session, QString* const errorMessage) const
{
    QMutexLocker locker(&sessionRepositoryMutex());
    QVector<SavedSession> sessions = loadAll(errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty()) {
        return false;
    }

    session = normalize(std::move(session));
    if (session.displayName.isEmpty()) {
        assignError(errorMessage, QStringLiteral("اسم الجلسة مطلوب."));
        return false;
    }
    if (!isDisplayNameAvailable(sessions, session.displayName)) {
        assignError(errorMessage, QStringLiteral("توجد جلسة أخرى بهذا الاسم."));
        return false;
    }

    session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.updatedAt = QDateTime::currentDateTimeUtc();
    sessions.append(std::move(session));
    return saveAll(sessions, errorMessage);
}

bool SessionStore::update(SavedSession session, QString* const errorMessage) const
{
    QMutexLocker locker(&sessionRepositoryMutex());
    QVector<SavedSession> sessions = loadAll(errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty()) {
        return false;
    }

    session = normalize(std::move(session));
    if (session.id.isEmpty() || session.displayName.isEmpty()) {
        assignError(errorMessage, QStringLiteral("الجلسة غير صالحة."));
        return false;
    }
    if (!isDisplayNameAvailable(sessions, session.displayName, session.id)) {
        assignError(errorMessage, QStringLiteral("توجد جلسة أخرى بهذا الاسم."));
        return false;
    }

    for (SavedSession& existing : sessions) {
        if (existing.id == session.id) {
            session.updatedAt = QDateTime::currentDateTimeUtc();
            existing = std::move(session);
            return saveAll(sessions, errorMessage);
        }
    }

    assignError(errorMessage, QStringLiteral("تعذر العثور على الجلسة المطلوبة."));
    return false;
}

bool SessionStore::remove(const QString& id, QString* const errorMessage) const
{
    QMutexLocker locker(&sessionRepositoryMutex());
    QVector<SavedSession> sessions = loadAll(errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty()) {
        return false;
    }

    for (qsizetype index = 0; index < sessions.size(); ++index) {
        if (sessions.at(index).id == id) {
            sessions.removeAt(index);
            return saveAll(sessions, errorMessage);
        }
    }

    assignError(errorMessage, QStringLiteral("تعذر العثور على الجلسة المطلوبة."));
    return false;
}

QString SessionStore::normalizePath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QFileInfo fileInfo(trimmed);
    return QDir::cleanPath(fileInfo.absoluteFilePath());
}

SavedSession SessionStore::normalize(SavedSession session)
{
    session.id = session.id.trimmed();
    session.displayName = normalizedName(session.displayName);

    QStringList paths;
    QSet<QString> pathKeys;
    paths.reserve(session.filePaths.size());
    for (const QString& path : std::as_const(session.filePaths)) {
        const QString normalizedPath = normalizePath(path);
        const QString pathKey = normalizedPath.toCaseFolded();
        if (!normalizedPath.isEmpty() && !pathKeys.contains(pathKey)) {
            pathKeys.insert(pathKey);
            paths.append(normalizedPath);
        }
    }
    session.filePaths = std::move(paths);

    session.activeFilePath = normalizePath(session.activeFilePath);
    if (!session.activeFilePath.isEmpty()
        && !pathKeys.contains(session.activeFilePath.toCaseFolded())) {
        session.activeFilePath.clear();
    }
    if (!session.updatedAt.isValid()) {
        session.updatedAt = QDateTime::currentDateTimeUtc();
    }
    return session;
}

bool SessionStore::isDisplayNameAvailable(const QVector<SavedSession>& sessions,
                                          const QString& displayName,
                                          const QString& excludedId)
{
    const QString requestedKey = nameKey(displayName);
    return !requestedKey.isEmpty()
        && std::none_of(sessions.cbegin(), sessions.cend(),
                        [&requestedKey, &excludedId](const SavedSession& session) {
                            return session.id != excludedId
                                && nameKey(session.displayName) == requestedKey;
                        });
}

QVector<SavedSession> SessionStore::loadLegacySessions() const
{
    QSettings settings = makeSettings();
    settings.beginGroup(settingsGroup());
    const QVariantList entries = settings.value(QString::fromLatin1(kEntriesKey)).toList();
    settings.endGroup();

    QVector<SavedSession> source;
    source.reserve(entries.size());
    for (const QVariant& entry : entries) {
        const QVariantMap map = entry.toMap();
        SavedSession session;
        session.id = map.value(QStringLiteral("id")).toString();
        session.displayName = map.value(QStringLiteral("displayName")).toString();
        session.filePaths = map.value(QStringLiteral("filePaths")).toStringList();
        session.activeFilePath = map.value(QStringLiteral("activeFilePath")).toString();
        session.updatedAt = QDateTime::fromString(
            map.value(QStringLiteral("updatedAt")).toString(), Qt::ISODateWithMs);
        source.append(std::move(session));
    }

    QVector<SavedSession> normalized;
    // Legacy stores historically skipped malformed/duplicate entries. Preserve
    // that compatibility during one-way migration instead of rejecting all data.
    QSet<QString> ids;
    QSet<QString> names;
    for (SavedSession session : std::as_const(source)) {
        session = normalize(std::move(session));
        const QString displayNameKey = nameKey(session.displayName);
        if (session.id.isEmpty() || session.displayName.isEmpty()
            || ids.contains(session.id) || names.contains(displayNameKey)) {
            continue;
        }
        ids.insert(session.id);
        names.insert(displayNameKey);
        normalized.append(std::move(session));
    }
    return normalized;
}

bool SessionStore::readRepository(const QString& path, QVector<SavedSession>* const sessions,
                                  QString* const errorMessage) const
{
    if (sessions == nullptr) {
        assignError(errorMessage, QStringLiteral("وجهة قراءة الجلسات غير صالحة."));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        assignError(errorMessage, QStringLiteral("تعذر فتح ملف الجلسات."));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        assignError(errorMessage, QStringLiteral("ملف الجلسات غير صالح."));
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QString::fromLatin1(kRepositoryVersionKey)).toInt() != kRepositorySchemaVersion
        || !root.value(QString::fromLatin1(kRepositoryEntriesKey)).isArray()) {
        assignError(errorMessage, QStringLiteral("إصدار ملف الجلسات غير مدعوم."));
        return false;
    }

    QVector<SavedSession> source;
    const QJsonArray entries = root.value(QString::fromLatin1(kRepositoryEntriesKey)).toArray();
    source.reserve(entries.size());
    for (const QJsonValue& value : entries) {
        if (!value.isObject()) {
            assignError(errorMessage, QStringLiteral("يحتوي ملف الجلسات على سجل غير صالح."));
            return false;
        }
        source.append(sessionFromJson(value.toObject()));
    }

    QVector<SavedSession> normalized;
    if (!validateAndNormalize(source, &normalized, errorMessage)) {
        return false;
    }
    *sessions = std::move(normalized);
    return true;
}

bool SessionStore::writeRepositoryAtomically(const QVector<SavedSession>& sessions,
                                             QString* const errorMessage) const
{
    const QString repositoryPath = repositoryFilePath();
    const QFileInfo repositoryInfo(repositoryPath);
    QDir parentDirectory(repositoryInfo.absolutePath());
    if (!parentDirectory.exists() && !QDir().mkpath(parentDirectory.absolutePath())) {
        assignError(errorMessage, QStringLiteral("تعذر إنشاء مجلد مستودع الجلسات."));
        return false;
    }

    const QString backupPath = backupRepositoryFilePath();
    if (QFileInfo::exists(repositoryPath)) {
        QFile::remove(backupPath);
        if (!QFile::copy(repositoryPath, backupPath)) {
            assignError(errorMessage, QStringLiteral("تعذر إنشاء النسخة الاحتياطية للجلسات."));
            return false;
        }
    }

    QJsonArray entries;
    for (const SavedSession& session : sessions) {
        entries.append(sessionToJson(session));
    }
    QJsonObject root;
    root.insert(QString::fromLatin1(kRepositoryVersionKey), kRepositorySchemaVersion);
    root.insert(QString::fromLatin1(kRepositoryEntriesKey), entries);

    QSaveFile file(repositoryPath);
    if (!file.open(QIODevice::WriteOnly)) {
        assignError(errorMessage, QStringLiteral("تعذر فتح مستودع الجلسات للكتابة."));
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0 || !file.commit()) {
        assignError(errorMessage, QStringLiteral("تعذر حفظ مستودع الجلسات بشكل آمن."));
        return false;
    }
    return true;
}

bool SessionStore::validateAndNormalize(const QVector<SavedSession>& source,
                                        QVector<SavedSession>* const normalized,
                                        QString* const errorMessage)
{
    if (normalized == nullptr) {
        assignError(errorMessage, QStringLiteral("وجهة جلسات غير صالحة."));
        return false;
    }

    QVector<SavedSession> result;
    result.reserve(source.size());
    QSet<QString> ids;
    QSet<QString> names;
    for (SavedSession session : source) {
        session = normalize(std::move(session));
        const QString sessionNameKey = nameKey(session.displayName);
        if (session.id.isEmpty() || session.displayName.isEmpty()) {
            assignError(errorMessage, QStringLiteral("يجب أن تحتوي كل جلسة على اسم ومعرف صالحين."));
            return false;
        }
        if (ids.contains(session.id) || names.contains(sessionNameKey)) {
            assignError(errorMessage, QStringLiteral("توجد جلسة أخرى بالاسم نفسه."));
            return false;
        }
        ids.insert(session.id);
        names.insert(sessionNameKey);
        result.append(std::move(session));
    }

    *normalized = std::move(result);
    return true;
}

QString SessionStore::settingsGroup() const
{
    return QString::fromLatin1(kSessionsKey);
}

QSettings SessionStore::makeSettings() const
{
    if (!scope.fileName.isEmpty()) {
        return QSettings(scope.fileName, QSettings::IniFormat);
    }
    return QSettings(scope.organization, scope.application);
}
