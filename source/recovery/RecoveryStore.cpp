#include "RecoveryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {

constexpr int kRecoverySchemaVersion = 1;
constexpr auto kMetadataSuffix = ".json";
constexpr auto kSnapshotSuffix = ".snapshot";

QJsonObject fingerprintToJson(const RecoverySourceFingerprint& fingerprint)
{
    QJsonObject object;
    object.insert(QStringLiteral("exists"), fingerprint.exists);
    object.insert(QStringLiteral("size"), QString::number(fingerprint.size));
    object.insert(QStringLiteral("lastModifiedUtc"),
                  fingerprint.lastModifiedUtc.toUTC().toString(Qt::ISODateWithMs));
    return object;
}

bool fingerprintFromJson(const QJsonObject& object, RecoverySourceFingerprint* fingerprint)
{
    if (fingerprint == nullptr || !object.contains(QStringLiteral("exists"))
        || !object.contains(QStringLiteral("size"))) {
        return false;
    }
    bool sizeOk = false;
    const qint64 size = object.value(QStringLiteral("size")).toString().toLongLong(&sizeOk);
    if (!sizeOk) {
        return false;
    }
    fingerprint->exists = object.value(QStringLiteral("exists")).toBool();
    fingerprint->size = size;
    const QString timestamp = object.value(QStringLiteral("lastModifiedUtc")).toString();
    fingerprint->lastModifiedUtc = timestamp.isEmpty()
        ? QDateTime()
        : QDateTime::fromString(timestamp, Qt::ISODateWithMs).toUTC();
    return timestamp.isEmpty() || fingerprint->lastModifiedUtc.isValid();
}

QJsonObject entryToJson(const RecoveryEntry& entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), kRecoverySchemaVersion);
    object.insert(QStringLiteral("id"), entry.id);
    object.insert(QStringLiteral("sourcePath"), entry.sourcePath);
    object.insert(QStringLiteral("displayName"), entry.displayName);
    object.insert(QStringLiteral("snapshotFileName"), entry.snapshotFileName);
    object.insert(QStringLiteral("documentRevision"), QString::number(entry.documentRevision));
    object.insert(QStringLiteral("capturedAtUtc"),
                  entry.capturedAtUtc.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("sourceFingerprint"), fingerprintToJson(entry.sourceFingerprint));
    object.insert(QStringLiteral("untitled"), entry.untitled);
    object.insert(QStringLiteral("legacyAdjacentBackup"), entry.legacyAdjacentBackup);
    return object;
}

bool entryFromJson(const QJsonObject& object, RecoveryEntry* entry)
{
    if (entry == nullptr || object.value(QStringLiteral("schemaVersion")).toInt() != kRecoverySchemaVersion) {
        return false;
    }
    bool revisionOk = false;
    const quint64 revision = object.value(QStringLiteral("documentRevision"))
                                 .toString().toULongLong(&revisionOk);
    const QDateTime capturedAt = QDateTime::fromString(
        object.value(QStringLiteral("capturedAtUtc")).toString(), Qt::ISODateWithMs).toUTC();
    if (!revisionOk || !capturedAt.isValid()
        || !fingerprintFromJson(object.value(QStringLiteral("sourceFingerprint")).toObject(),
                                &entry->sourceFingerprint)) {
        return false;
    }
    entry->id = object.value(QStringLiteral("id")).toString();
    entry->sourcePath = object.value(QStringLiteral("sourcePath")).toString();
    entry->displayName = object.value(QStringLiteral("displayName")).toString();
    entry->snapshotFileName = object.value(QStringLiteral("snapshotFileName")).toString();
    entry->documentRevision = revision;
    entry->capturedAtUtc = capturedAt;
    entry->untitled = object.value(QStringLiteral("untitled")).toBool();
    entry->legacyAdjacentBackup = object.value(QStringLiteral("legacyAdjacentBackup")).toBool();
    return !entry->id.isEmpty() && !entry->snapshotFileName.isEmpty();
}

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

} // namespace

RecoveryStore::RecoveryStore(QString recoveryRoot)
    : m_recoveryRoot(recoveryRoot.isEmpty() ? defaultRecoveryRoot() : std::move(recoveryRoot))
{
}

QString RecoveryStore::recoveryRoot() const
{
    return m_recoveryRoot;
}

QString RecoveryStore::defaultRecoveryRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("recovery"));
}

RecoverySourceFingerprint RecoveryStore::fingerprintForPath(const QString& sourcePath)
{
    const QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile()) {
        return {};
    }
    return {true, info.size(), info.lastModified().toUTC()};
}

bool RecoveryStore::ensureRoot(QString* errorMessage) const
{
    QDir root;
    if (root.mkpath(m_recoveryRoot)) {
        return true;
    }
    setError(errorMessage, QStringLiteral("تعذر إنشاء مجلد الاستعادة."));
    return false;
}

bool RecoveryStore::isSafeEntryId(const QString& entryId) const
{
    if (entryId.size() < 8 || entryId.size() > 64) {
        return false;
    }
    for (const QChar character : entryId) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-')) {
            return false;
        }
    }
    return true;
}

bool RecoveryStore::isSafeSnapshotFileName(const QString& snapshotFileName) const
{
    return snapshotFileName == QFileInfo(snapshotFileName).fileName()
        && snapshotFileName.endsWith(QLatin1String(kSnapshotSuffix))
        && !snapshotFileName.contains(QStringLiteral(".."));
}

QString RecoveryStore::metadataPathForId(const QString& entryId) const
{
    return QDir(m_recoveryRoot).filePath(entryId + QLatin1String(kMetadataSuffix));
}

QString RecoveryStore::snapshotPathForFileName(const QString& snapshotFileName) const
{
    return QDir(m_recoveryRoot).filePath(snapshotFileName);
}

bool RecoveryStore::writeSnapshot(RecoveryEntry entry, const QString& text,
                                  QString* errorMessage) const
{
    if (!ensureRoot(errorMessage)) {
        return false;
    }
    if (entry.id.isEmpty()) {
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (!isSafeEntryId(entry.id)) {
        setError(errorMessage, QStringLiteral("معرّف الاستعادة غير صالح."));
        return false;
    }
    entry.snapshotFileName = entry.id + QLatin1String(kSnapshotSuffix);
    entry.capturedAtUtc = QDateTime::currentDateTimeUtc();
    if (entry.displayName.trimmed().isEmpty()) {
        entry.displayName = entry.untitled ? QStringLiteral("غير معنون")
                                           : QFileInfo(entry.sourcePath).fileName();
    }
    if (!isSafeSnapshotFileName(entry.snapshotFileName)) {
        setError(errorMessage, QStringLiteral("اسم ملف الاستعادة غير صالح."));
        return false;
    }

    QSaveFile snapshotFile(snapshotPathForFileName(entry.snapshotFileName));
    if (!snapshotFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(errorMessage, QStringLiteral("تعذر كتابة نسخة الاستعادة."));
        return false;
    }
    const QByteArray utf8Text = text.toUtf8();
    if (snapshotFile.write(utf8Text) != utf8Text.size() || !snapshotFile.commit()) {
        setError(errorMessage, QStringLiteral("تعذر إتمام حفظ نسخة الاستعادة بأمان."));
        return false;
    }

    QSaveFile metadataFile(metadataPathForId(entry.id));
    if (!metadataFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(errorMessage, QStringLiteral("تعذر كتابة بيانات نسخة الاستعادة."));
        return false;
    }
    const QByteArray metadata = QJsonDocument(entryToJson(entry)).toJson(QJsonDocument::Compact);
    if (metadataFile.write(metadata) != metadata.size() || !metadataFile.commit()) {
        setError(errorMessage, QStringLiteral("تعذر إتمام حفظ بيانات الاستعادة بأمان."));
        return false;
    }
    return true;
}

QVector<RecoveryEntry> RecoveryStore::entries(QStringList* warnings) const
{
    QVector<RecoveryEntry> result;
    const QDir root(m_recoveryRoot);
    if (!root.exists()) {
        return result;
    }
    const QFileInfoList metadataFiles = root.entryInfoList(
        {QStringLiteral("*") + QLatin1String(kMetadataSuffix)}, QDir::Files, QDir::Time);
    for (const QFileInfo& metadataInfo : metadataFiles) {
        QFile metadataFile(metadataInfo.filePath());
        if (!metadataFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (warnings != nullptr) {
                warnings->append(QStringLiteral("تعذر قراءة بيانات استعادة."));
            }
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll(), &parseError);
        RecoveryEntry entry;
        if (parseError.error != QJsonParseError::NoError || !document.isObject()
            || !entryFromJson(document.object(), &entry) || !isSafeEntryId(entry.id)
            || !isSafeSnapshotFileName(entry.snapshotFileName)
            || !QFileInfo::exists(snapshotPathForFileName(entry.snapshotFileName))) {
            if (warnings != nullptr) {
                warnings->append(QStringLiteral("تم تجاهل نسخة استعادة غير صالحة."));
            }
            continue;
        }
        result.append(entry);
    }
    return result;
}

bool RecoveryStore::readSnapshot(const RecoveryEntry& entry, QString* text,
                                 QString* errorMessage) const
{
    if (text == nullptr || !isSafeEntryId(entry.id)
        || !isSafeSnapshotFileName(entry.snapshotFileName)) {
        setError(errorMessage, QStringLiteral("طلب الاستعادة غير صالح."));
        return false;
    }
    QFile snapshotFile(snapshotPathForFileName(entry.snapshotFileName));
    if (!snapshotFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(errorMessage, QStringLiteral("تعذر قراءة نسخة الاستعادة."));
        return false;
    }
    *text = QString::fromUtf8(snapshotFile.readAll());
    return true;
}

bool RecoveryStore::removeEntry(const QString& entryId, QString* errorMessage) const
{
    if (!isSafeEntryId(entryId)) {
        setError(errorMessage, QStringLiteral("معرّف الاستعادة غير صالح."));
        return false;
    }
    const QString metadataPath = metadataPathForId(entryId);
    QString snapshotFileName = entryId + QLatin1String(kSnapshotSuffix);
    QFile metadataFile(metadataPath);
    if (metadataFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll(), &parseError);
        RecoveryEntry entry;
        if (parseError.error == QJsonParseError::NoError && document.isObject()
            && entryFromJson(document.object(), &entry) && isSafeSnapshotFileName(entry.snapshotFileName)) {
            snapshotFileName = entry.snapshotFileName;
        }
        metadataFile.close();
    }
    if (QFile::exists(metadataPath) && !QFile::remove(metadataPath)) {
        setError(errorMessage, QStringLiteral("تعذر حذف بيانات نسخة الاستعادة."));
        return false;
    }
    const QString snapshotPath = snapshotPathForFileName(snapshotFileName);
    if (QFile::exists(snapshotPath) && !QFile::remove(snapshotPath)) {
        setError(errorMessage, QStringLiteral("تعذر حذف ملف نسخة الاستعادة."));
        return false;
    }
    return true;
}

void RecoveryStore::prune(const QDateTime& minimumCaptureTimeUtc, QStringList* warnings) const
{
    const QVector<RecoveryEntry> currentEntries = entries(warnings);
    for (const RecoveryEntry& entry : currentEntries) {
        if (entry.capturedAtUtc < minimumCaptureTimeUtc && !removeEntry(entry.id)) {
            if (warnings != nullptr) {
                warnings->append(QStringLiteral("تعذر تنظيف نسخة استعادة قديمة."));
            }
        }
    }
}
