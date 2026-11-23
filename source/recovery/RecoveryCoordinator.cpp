#include "RecoveryCoordinator.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <QUuid>

RecoveryWriter::RecoveryWriter(RecoveryStore store)
    : m_store(std::move(store))
{
}

void RecoveryWriter::writeSnapshot(RecoverySnapshot snapshot)
{
    QString errorMessage;
    const bool succeeded = m_store.writeSnapshot(snapshot.entry, snapshot.text, &errorMessage);
    emit writeFinished({snapshot.entry.id, snapshot.entry.documentRevision, succeeded, errorMessage});
}

void RecoveryWriter::removeEntry(QString entryId)
{
    QString errorMessage;
    const bool succeeded = m_store.removeEntry(entryId, &errorMessage);
    emit removalFinished(std::move(entryId), succeeded, errorMessage);
}

RecoveryCoordinator::RecoveryCoordinator(QString recoveryRoot, QObject* const parent)
    : QObject(parent)
    , m_store(std::move(recoveryRoot))
{
    qRegisterMetaType<RecoverySnapshot>("RecoverySnapshot");
    qRegisterMetaType<RecoveryWriteResult>("RecoveryWriteResult");

    m_writer = new RecoveryWriter(m_store);
    m_writer->moveToThread(&m_workerThread);
    connect(m_writer, &RecoveryWriter::writeFinished,
            this, &RecoveryCoordinator::onWriteFinished, Qt::QueuedConnection);
    connect(m_writer, &RecoveryWriter::removalFinished,
            this, &RecoveryCoordinator::onRemovalFinished, Qt::QueuedConnection);
    connect(&m_flushDeadlineTimer, &QTimer::timeout,
            this, &RecoveryCoordinator::onFlushDeadline);
    m_flushDeadlineTimer.setSingleShot(true);
    connect(&m_workerThread, &QThread::finished, m_writer, &QObject::deleteLater);
    m_workerThread.start();
}

RecoveryCoordinator::~RecoveryCoordinator()
{
    shutdown();
}

QString RecoveryCoordinator::createDocumentId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

RecoveryStore RecoveryCoordinator::store() const
{
    return m_store;
}

QVector<RecoveryEntry> RecoveryCoordinator::entries(QStringList* const warnings) const
{
    return m_store.entries(warnings);
}

bool RecoveryCoordinator::readSnapshot(const RecoveryEntry& entry, QString* const text,
                                       QString* const errorMessage) const
{
    return m_store.readSnapshot(entry, text, errorMessage);
}

bool RecoveryCoordinator::importLegacyAdjacentBackup(const QString& requestedSourcePath,
                                                      QString* const errorMessage)
{
    const QFileInfo sourceInfo(requestedSourcePath);
    const QString sourcePath = sourceInfo.absoluteFilePath();
    const QString backupPath = sourcePath + QStringLiteral(".~");
    const QFileInfo backupInfo(backupPath);
    if (!backupInfo.exists() || !backupInfo.isFile()) {
        return false;
    }
    if (sourceInfo.exists() && backupInfo.lastModified() <= sourceInfo.lastModified()) {
        return false;
    }

    QFile legacyBackup(backupPath);
    if (!legacyBackup.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("تعذر قراءة النسخة الاحتياطية السابقة.");
        }
        return false;
    }
    const QString text = QString::fromUtf8(legacyBackup.readAll());
    RecoveryEntry entry;
    entry.id = createDocumentId();
    entry.sourcePath = sourcePath;
    entry.displayName = sourceInfo.fileName();
    entry.sourceFingerprint = RecoveryStore::fingerprintForPath(sourcePath);
    entry.legacyAdjacentBackup = true;

    QString writeError;
    if (!m_store.writeSnapshot(entry, text, &writeError)) {
        if (errorMessage != nullptr) {
            *errorMessage = writeError;
        }
        return false;
    }
    if (!QFile::remove(backupPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("تم نسخ النسخة القديمة، لكن تعذر حذفها.");
        }
    }
    return true;
}

void RecoveryCoordinator::pruneExpiredEntries(const int retentionDays,
                                              QStringList* const warnings) const
{
    const int boundedDays = qBound(1, retentionDays, 365);
    m_store.prune(QDateTime::currentDateTimeUtc().addDays(-boundedDays), warnings);
}

void RecoveryCoordinator::submitSnapshot(RecoverySnapshot snapshot)
{
    if (m_shutdown || snapshot.entry.id.isEmpty()) {
        return;
    }
    m_removalRequested.remove(snapshot.entry.id);
    if (m_inFlightRevisions.contains(snapshot.entry.id)
        || m_removalsInFlight.contains(snapshot.entry.id)) {
        const auto pending = m_pendingSnapshots.constFind(snapshot.entry.id);
        if (pending == m_pendingSnapshots.cend()
            || pending->entry.documentRevision <= snapshot.entry.documentRevision) {
            m_pendingSnapshots.insert(snapshot.entry.id, std::move(snapshot));
        }
        return;
    }
    dispatchSnapshot(snapshot);
}

void RecoveryCoordinator::removeEntry(QString entryId)
{
    if (m_shutdown || entryId.isEmpty()) {
        return;
    }
    m_pendingSnapshots.remove(entryId);
    m_removalRequested.insert(entryId, true);
    if (!m_inFlightRevisions.contains(entryId) && !m_removalsInFlight.contains(entryId)) {
        dispatchRemoval(entryId);
    }
}

void RecoveryCoordinator::dispatchSnapshot(const RecoverySnapshot& snapshot)
{
    if (m_shutdown || snapshot.entry.id.isEmpty()) {
        return;
    }
    m_inFlightRevisions.insert(snapshot.entry.id, snapshot.entry.documentRevision);
    QMetaObject::invokeMethod(m_writer, "writeSnapshot", Qt::QueuedConnection,
                              Q_ARG(RecoverySnapshot, snapshot));
}

void RecoveryCoordinator::dispatchRemoval(const QString& entryId)
{
    if (m_shutdown || entryId.isEmpty() || m_removalsInFlight.contains(entryId)) {
        return;
    }
    m_removalsInFlight.insert(entryId, true);
    QMetaObject::invokeMethod(m_writer, "removeEntry", Qt::QueuedConnection,
                              Q_ARG(QString, entryId));
}

void RecoveryCoordinator::dispatchPendingOrRemoval(const QString& entryId)
{
    if (m_shutdown || m_inFlightRevisions.contains(entryId)
        || m_removalsInFlight.contains(entryId)) {
        return;
    }
    if (m_removalRequested.contains(entryId)) {
        dispatchRemoval(entryId);
        return;
    }
    const auto pending = m_pendingSnapshots.find(entryId);
    if (pending == m_pendingSnapshots.end()) {
        return;
    }
    const RecoverySnapshot snapshot = *pending;
    m_pendingSnapshots.erase(pending);
    dispatchSnapshot(snapshot);
}

void RecoveryCoordinator::onWriteFinished(RecoveryWriteResult result)
{
    const auto inFlight = m_inFlightRevisions.find(result.entryId);
    if (inFlight == m_inFlightRevisions.end() || inFlight.value() != result.documentRevision) {
        return;
    }

    m_inFlightRevisions.erase(inFlight);
    if (!result.succeeded) {
        m_flushSawFailure = true;
    }
    emit snapshotPersisted(result);
    dispatchPendingOrRemoval(result.entryId);
    if (isIdle()) {
        completeFlush(!m_flushSawFailure);
    }
}

void RecoveryCoordinator::onRemovalFinished(QString entryId, const bool succeeded,
                                            QString errorMessage)
{
    if (!m_removalsInFlight.remove(entryId)) {
        return;
    }

    m_removalRequested.remove(entryId);
    if (!succeeded) {
        m_flushSawFailure = true;
        emit removalFailed(entryId, std::move(errorMessage));
    }
    dispatchPendingOrRemoval(entryId);
    if (isIdle()) {
        completeFlush(!m_flushSawFailure);
    }
}

void RecoveryCoordinator::requestFlush(const int deadlineMilliseconds)
{
    if (m_shutdown) {
        return;
    }

    m_flushRequested = true;
    m_flushSawFailure = false;
    m_flushDeadlineTimer.start(qBound(1, deadlineMilliseconds, 30000));
    QTimer::singleShot(0, this, [this]() {
        if (isIdle()) {
            completeFlush(!m_flushSawFailure);
        }
    });
}

void RecoveryCoordinator::onFlushDeadline()
{
    completeFlush(false);
}

void RecoveryCoordinator::completeFlush(const bool allPersisted)
{
    if (!m_flushRequested) {
        return;
    }

    m_flushDeadlineTimer.stop();
    m_flushRequested = false;
    emit flushCompleted(allPersisted && isIdle() && !m_flushSawFailure);
}

bool RecoveryCoordinator::isIdle() const
{
    return m_pendingSnapshots.isEmpty() && m_inFlightRevisions.isEmpty()
        && m_removalRequested.isEmpty() && m_removalsInFlight.isEmpty();
}

void RecoveryCoordinator::shutdown()
{
    if (m_shutdown) {
        return;
    }

    // Normal window closure waits through requestFlush() and its signal. The
    // destructor must not pump GUI events; callbacks during teardown would make
    // the close path reentrant. In-flight worker I/O is bounded by its thread
    // shutdown, while work not yet accepted remains recoverably dirty in the UI.
    m_shutdown = true;
    m_flushDeadlineTimer.stop();
    m_pendingSnapshots.clear();
    m_removalRequested.clear();
    m_workerThread.quit();
    m_workerThread.wait(1500);
    m_writer = nullptr;
}
