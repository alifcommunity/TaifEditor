#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

struct RecoverySourceFingerprint final {
    bool exists = false;
    qint64 size = -1;
    QDateTime lastModifiedUtc{};
};

struct RecoveryEntry final {
    QString id;
    QString sourcePath;
    QString displayName;
    QString snapshotFileName;
    quint64 documentRevision = 0;
    QDateTime capturedAtUtc{};
    RecoverySourceFingerprint sourceFingerprint{};
    bool untitled = false;
    bool legacyAdjacentBackup = false;
};

struct RecoverySnapshot final {
    RecoveryEntry entry;
    QString text;
};

/**
 * Persistent crash-recovery store. All files live under the application-data
 * recovery root and are written atomically. It never writes beside user files.
 */
class RecoveryStore final {
public:
    explicit RecoveryStore(QString recoveryRoot = {});

    [[nodiscard]] QString recoveryRoot() const;
    [[nodiscard]] static QString defaultRecoveryRoot();
    [[nodiscard]] static RecoverySourceFingerprint fingerprintForPath(const QString& sourcePath);

    [[nodiscard]] bool writeSnapshot(RecoveryEntry entry, const QString& text,
                                     QString* errorMessage = nullptr) const;
    [[nodiscard]] QVector<RecoveryEntry> entries(QStringList* warnings = nullptr) const;
    [[nodiscard]] bool readSnapshot(const RecoveryEntry& entry, QString* text,
                                    QString* errorMessage = nullptr) const;
    [[nodiscard]] bool removeEntry(const QString& entryId,
                                   QString* errorMessage = nullptr) const;
    void prune(const QDateTime& minimumCaptureTimeUtc, QStringList* warnings = nullptr) const;

private:
    [[nodiscard]] QString metadataPathForId(const QString& entryId) const;
    [[nodiscard]] QString snapshotPathForFileName(const QString& snapshotFileName) const;
    [[nodiscard]] bool ensureRoot(QString* errorMessage) const;
    [[nodiscard]] bool isSafeEntryId(const QString& entryId) const;
    [[nodiscard]] bool isSafeSnapshotFileName(const QString& snapshotFileName) const;

    QString m_recoveryRoot;
};

Q_DECLARE_METATYPE(RecoveryEntry)
Q_DECLARE_METATYPE(RecoverySnapshot)
