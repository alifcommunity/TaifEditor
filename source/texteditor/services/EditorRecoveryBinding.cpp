#include "EditorRecoveryBinding.h"

#include "RecoveryCoordinator.h"

#include <QTextDocument>

namespace {

constexpr int kIdleCaptureDelayMilliseconds = 750;
constexpr int kRetryDelayMilliseconds = 30000;
constexpr int kMaximumAutomaticRetries = 3;

} // namespace

EditorRecoveryBinding::EditorRecoveryBinding(QTextDocument* const document, QObject* const parent)
    : QObject(parent)
    , m_document(document)
{
    m_autoSaveTimer.setParent(this);
    m_autoSaveTimer.setSingleShot(true);
    m_autoSaveTimer.setInterval(kIdleCaptureDelayMilliseconds);

    m_maximumTimer.setParent(this);
    m_maximumTimer.setSingleShot(false);

    m_retryTimer.setParent(this);
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(kRetryDelayMilliseconds);
}

EditorRecoveryBinding::~EditorRecoveryBinding()
{
    shutdown();
}

void EditorRecoveryBinding::initialize()
{
    if (m_initialized || m_shutdown || m_document == nullptr) {
        return;
    }

    m_initialized = true;
    m_documentChangeConnection = connect(m_document, &QTextDocument::contentsChanged,
                                         this, &EditorRecoveryBinding::onDocumentChanged);
    connect(&m_autoSaveTimer, &QTimer::timeout,
            this, &EditorRecoveryBinding::onAutoSaveTimeout);
    connect(&m_maximumTimer, &QTimer::timeout,
            this, &EditorRecoveryBinding::onAutoSaveTimeout);
    connect(&m_retryTimer, &QTimer::timeout,
            this, &EditorRecoveryBinding::onAutoSaveTimeout);
}

void EditorRecoveryBinding::shutdown()
{
    if (m_shutdown) {
        return;
    }

    m_shutdown = true;
    stopAutoSave();
    m_retryTimer.stop();
    if (m_documentChangeConnection) {
        disconnect(m_documentChangeConnection);
        m_documentChangeConnection = {};
    }
    if (m_persistedConnection) {
        disconnect(m_persistedConnection);
        m_persistedConnection = {};
    }
    m_coordinator = nullptr;
}

void EditorRecoveryBinding::setSnapshotFactory(SnapshotFactory factory)
{
    m_snapshotFactory = std::move(factory);
}

void EditorRecoveryBinding::setCoordinator(RecoveryCoordinator* const coordinator)
{
    if (m_coordinator == coordinator) {
        return;
    }
    if (m_persistedConnection) {
        disconnect(m_persistedConnection);
        m_persistedConnection = {};
    }

    m_coordinator = coordinator;
    if (m_coordinator != nullptr) {
        m_persistedConnection = connect(m_coordinator, &RecoveryCoordinator::snapshotPersisted,
                                        this, &EditorRecoveryBinding::onSnapshotPersisted);
        if (m_documentId.isEmpty()) {
            m_documentId = m_coordinator->createDocumentId();
        }
    }
    emitStateChanged();
}

void EditorRecoveryBinding::setConfiguration(const EditorPreferences& preferences)
{
    m_preferences = PreferencesStore::normalize(preferences);
    m_maximumTimer.setInterval(m_preferences.autoSaveIntervalMilliseconds);
    if (!m_preferences.autoSaveEnabled) {
        stopAutoSave();
        m_retryTimer.stop();
    }
    emitStateChanged();
}

void EditorRecoveryBinding::adoptRecoveryEntry(const RecoveryEntry& entry)
{
    if (entry.id.isEmpty()) {
        return;
    }

    m_documentId = entry.id;
    m_currentDirtyRevision = entry.documentRevision;
    m_lastRequestedRevision = entry.documentRevision;
    m_lastPersistedRevision = entry.documentRevision;
    m_snapshotAwaitingAcknowledgement = false;
    m_dirty = false;
    m_retryCount = 0;
    emitStateChanged();
}

void EditorRecoveryBinding::startAutoSave()
{
    if (!m_preferences.autoSaveEnabled || m_coordinator == nullptr || m_shutdown) {
        return;
    }
    if (m_currentDirtyRevision == 0) {
        m_currentDirtyRevision = 1;
    }

    m_dirty = true;
    m_autoSaveTimer.start();
    if (!m_maximumTimer.isActive()) {
        m_maximumTimer.start();
    }
    emitStateChanged();
}

void EditorRecoveryBinding::stopAutoSave()
{
    m_autoSaveTimer.stop();
    m_maximumTimer.stop();
}

void EditorRecoveryBinding::flushSnapshot()
{
    if (!m_preferences.autoSaveEnabled || m_coordinator == nullptr || !m_dirty
        || m_snapshotAwaitingAcknowledgement || !m_snapshotFactory || m_shutdown) {
        return;
    }
    if (m_documentId.isEmpty()) {
        m_documentId = m_coordinator->createDocumentId();
    }

    QElapsedTimer payloadTimer;
    payloadTimer.start();
    RecoverySnapshot snapshot = m_snapshotFactory(m_currentDirtyRevision);
    m_lastPayloadCaptureDurationMilliseconds = payloadTimer.elapsed();
    m_lastPayloadCharacterCount = snapshot.text.size();
    emit payloadCaptured(m_lastPayloadCharacterCount, m_lastPayloadCaptureDurationMilliseconds);
    if (snapshot.entry.id.isEmpty()) {
        snapshot.entry.id = m_documentId;
    }
    snapshot.entry.documentRevision = m_currentDirtyRevision;
    m_documentId = snapshot.entry.id;
    m_lastRequestedRevision = snapshot.entry.documentRevision;
    m_snapshotAwaitingAcknowledgement = true;
    m_writeTimer.start();
    m_coordinator->submitSnapshot(std::move(snapshot));
    emitStateChanged();
}

void EditorRecoveryBinding::clearSnapshot()
{
    stopAutoSave();
    m_retryTimer.stop();
    m_dirty = false;
    m_snapshotAwaitingAcknowledgement = false;
    m_retryCount = 0;
    // A successful normal document save is itself durable. Ignore an older
    // queued recovery acknowledgement and keep externally observed state sound.
    m_lastRequestedRevision = m_currentDirtyRevision;
    m_lastPersistedRevision = m_currentDirtyRevision;
    if (m_coordinator != nullptr && !m_documentId.isEmpty()) {
        m_coordinator->removeEntry(m_documentId);
    }
    emitStateChanged();
}

QString EditorRecoveryBinding::documentId() const
{
    return m_documentId;
}

bool EditorRecoveryBinding::hasPendingPersistence() const
{
    return m_dirty || m_snapshotAwaitingAcknowledgement
        || m_lastPersistedRevision < m_lastRequestedRevision;
}

bool EditorRecoveryBinding::isRetryScheduled() const
{
    return m_retryTimer.isActive();
}

quint64 EditorRecoveryBinding::lastRequestedRevision() const
{
    return m_lastRequestedRevision;
}

quint64 EditorRecoveryBinding::lastPersistedRevision() const
{
    return m_lastPersistedRevision;
}

quint64 EditorRecoveryBinding::currentDirtyRevision() const
{
    return m_currentDirtyRevision;
}

qsizetype EditorRecoveryBinding::lastPayloadCharacterCount() const
{
    return m_lastPayloadCharacterCount;
}

qint64 EditorRecoveryBinding::lastPayloadCaptureDurationMilliseconds() const
{
    return m_lastPayloadCaptureDurationMilliseconds;
}

qint64 EditorRecoveryBinding::lastWriteDurationMilliseconds() const
{
    return m_lastWriteDurationMilliseconds;
}

void EditorRecoveryBinding::onDocumentChanged()
{
    if (!m_preferences.autoSaveEnabled || m_coordinator == nullptr || m_shutdown) {
        return;
    }

    ++m_currentDirtyRevision;
    m_retryCount = 0;
    m_retryTimer.stop();
    startAutoSave();
}

void EditorRecoveryBinding::onSnapshotPersisted(RecoveryWriteResult result)
{
    if (result.entryId != m_documentId || result.documentRevision != m_lastRequestedRevision) {
        return;
    }

    m_snapshotAwaitingAcknowledgement = false;
    m_lastWriteDurationMilliseconds = m_writeTimer.isValid() ? m_writeTimer.elapsed() : 0;
    emit snapshotWriteFinished(result.documentRevision, m_lastWriteDurationMilliseconds,
                               result.succeeded);
    if (!result.succeeded) {
        m_dirty = true;
        scheduleRetry();
        emitStateChanged();
        return;
    }

    m_lastPersistedRevision = result.documentRevision;
    m_retryCount = 0;
    m_retryTimer.stop();
    if (m_currentDirtyRevision == m_lastPersistedRevision) {
        m_dirty = false;
        stopAutoSave();
    } else {
        // The document advanced while this payload was in flight. Preserve the
        // newer dirty revision and use the standard idle/max-age capture policy.
        m_dirty = true;
        startAutoSave();
    }
    emitStateChanged();
}

void EditorRecoveryBinding::onAutoSaveTimeout()
{
    flushSnapshot();
}

void EditorRecoveryBinding::scheduleRetry()
{
    if (m_retryCount >= kMaximumAutomaticRetries) {
        // Keep the document logically dirty. A future edit or explicit close
        // flush remains allowed to request another snapshot.
        stopAutoSave();
        return;
    }

    ++m_retryCount;
    stopAutoSave();
    m_retryTimer.start();
}

void EditorRecoveryBinding::emitStateChanged()
{
    emit persistenceStateChanged();
}
