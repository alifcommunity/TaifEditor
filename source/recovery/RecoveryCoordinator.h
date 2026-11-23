#pragma once

#include "RecoveryStore.h"

#include <QHash>
#include <QObject>
#include <QThread>
#include <QTimer>

struct RecoveryWriteResult final {
    QString entryId;
    quint64 documentRevision = 0;
    bool succeeded = false;
    QString errorMessage;
};

class RecoveryWriter final : public QObject {
    Q_OBJECT
public:
    explicit RecoveryWriter(RecoveryStore store);

public slots:
    void writeSnapshot(RecoverySnapshot snapshot);
    void removeEntry(QString entryId);

signals:
    void writeFinished(RecoveryWriteResult result);
    void removalFinished(QString entryId, bool succeeded, QString errorMessage);

private:
    RecoveryStore m_store;
};

/**
 * GUI-thread coordinator for background recovery writes. It owns one worker,
 * keeps at most one in-flight and one newest pending request per document, and
 * never delivers callbacks through editor pointers. requestFlush() is deliberately
 * asynchronous: it never pumps the GUI event loop and completes when queued work
 * becomes idle or its bounded deadline expires.
 */
class RecoveryCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit RecoveryCoordinator(QString recoveryRoot = {}, QObject* parent = nullptr);
    ~RecoveryCoordinator() override;

    [[nodiscard]] QString createDocumentId() const;
    [[nodiscard]] RecoveryStore store() const;
    [[nodiscard]] QVector<RecoveryEntry> entries(QStringList* warnings = nullptr) const;
    [[nodiscard]] bool readSnapshot(const RecoveryEntry& entry, QString* text,
                                    QString* errorMessage = nullptr) const;
    [[nodiscard]] bool importLegacyAdjacentBackup(const QString& sourcePath,
                                                   QString* errorMessage = nullptr);
    void pruneExpiredEntries(int retentionDays, QStringList* warnings = nullptr) const;

    void submitSnapshot(RecoverySnapshot snapshot);
    void removeEntry(QString entryId);
    void requestFlush(int deadlineMilliseconds = 1200);
    void shutdown();

signals:
    void snapshotPersisted(RecoveryWriteResult result);
    void removalFailed(QString entryId, QString errorMessage);
    void flushCompleted(bool allPersisted);

private slots:
    void onWriteFinished(RecoveryWriteResult result);
    void onRemovalFinished(QString entryId, bool succeeded, QString errorMessage);
    void onFlushDeadline();

private:
    void dispatchSnapshot(const RecoverySnapshot& snapshot);
    void dispatchRemoval(const QString& entryId);
    void dispatchPendingOrRemoval(const QString& entryId);
    void completeFlush(bool allPersisted);
    [[nodiscard]] bool isIdle() const;

    RecoveryStore m_store;
    QThread m_workerThread;
    RecoveryWriter* m_writer = nullptr;
    QHash<QString, RecoverySnapshot> m_pendingSnapshots;
    QHash<QString, quint64> m_inFlightRevisions;
    QHash<QString, bool> m_removalRequested;
    QHash<QString, bool> m_removalsInFlight;
    QTimer m_flushDeadlineTimer;
    bool m_flushRequested = false;
    bool m_flushSawFailure = false;
    bool m_shutdown = false;
};

Q_DECLARE_METATYPE(RecoveryWriteResult)
