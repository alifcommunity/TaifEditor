#pragma once

#include "EditorPreferences.h"
#include "RecoveryCoordinator.h"
#include "RecoveryStore.h"

#include <QElapsedTimer>
#include <QObject>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <functional>

class QTextDocument;

/**
 * Non-visual recovery service for one editor document.
 *
 * It owns autosave/retry timers and the acknowledged-persistence state machine.
 * Snapshot text/metadata remains supplied by TEditor on the GUI thread, so the
 * binding never requires a widget pointer from worker-thread callbacks.
 */
class EditorRecoveryBinding final : public QObject
{
    Q_OBJECT

public:
    using SnapshotFactory = std::function<RecoverySnapshot(quint64 documentRevision)>;

    explicit EditorRecoveryBinding(QTextDocument* document, QObject* parent = nullptr);
    ~EditorRecoveryBinding() override;

    void initialize();
    void shutdown();

    void setSnapshotFactory(SnapshotFactory factory);
    void setCoordinator(RecoveryCoordinator* coordinator);
    void setConfiguration(const EditorPreferences& preferences);
    void adoptRecoveryEntry(const RecoveryEntry& entry);

    void startAutoSave();
    void stopAutoSave();
    void flushSnapshot();
    void clearSnapshot();

    [[nodiscard]] QString documentId() const;
    [[nodiscard]] bool hasPendingPersistence() const;
    [[nodiscard]] bool isRetryScheduled() const;
    [[nodiscard]] quint64 lastRequestedRevision() const;
    [[nodiscard]] quint64 lastPersistedRevision() const;
    [[nodiscard]] quint64 currentDirtyRevision() const;
    [[nodiscard]] qsizetype lastPayloadCharacterCount() const;
    [[nodiscard]] qint64 lastPayloadCaptureDurationMilliseconds() const;
    [[nodiscard]] qint64 lastWriteDurationMilliseconds() const;

signals:
    void persistenceStateChanged();
    void payloadCaptured(qsizetype characterCount, qint64 durationMilliseconds);
    void snapshotWriteFinished(quint64 revision, qint64 durationMilliseconds, bool succeeded);

private slots:
    void onDocumentChanged();
    void onSnapshotPersisted(RecoveryWriteResult result);
    void onAutoSaveTimeout();

private:
    void scheduleRetry();
    void emitStateChanged();

    QPointer<QTextDocument> m_document;
    QPointer<RecoveryCoordinator> m_coordinator;
    QMetaObject::Connection m_documentChangeConnection;
    QMetaObject::Connection m_persistedConnection;
    QTimer m_autoSaveTimer;
    QTimer m_maximumTimer;
    QTimer m_retryTimer;
    QElapsedTimer m_writeTimer;
    SnapshotFactory m_snapshotFactory;
    EditorPreferences m_preferences{};
    QString m_documentId;
    quint64 m_currentDirtyRevision = 0;
    quint64 m_lastRequestedRevision = 0;
    quint64 m_lastPersistedRevision = 0;
    qsizetype m_lastPayloadCharacterCount = 0;
    qint64 m_lastPayloadCaptureDurationMilliseconds = 0;
    qint64 m_lastWriteDurationMilliseconds = 0;
    int m_retryCount = 0;
    bool m_snapshotAwaitingAcknowledgement = false;
    bool m_dirty = false;
    bool m_initialized = false;
    bool m_shutdown = false;
};
