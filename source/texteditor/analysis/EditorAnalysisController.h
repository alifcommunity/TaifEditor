#pragma once

#include "AlifAnalysis.h"

#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtCore/QTimer>

#include <atomic>
#include <memory>

class AnalysisWorker final : public QObject {
    Q_OBJECT
public:
    explicit AnalysisWorker(std::shared_ptr<std::atomic<quint64>> latestRevision,
                            QObject* parent = nullptr);

public slots:
    void process(AnalysisRequest request);

signals:
    void analysisReady(LanguageAnalysisSnapshotPtr snapshot);

private:
    std::shared_ptr<std::atomic<quint64>> m_latestRevision;
};

/**
 * GUI-thread coordinator for one TEditor document. It owns the debounce timers,
 * revision gate, worker lifetime, and immutable current analysis snapshot.
 */
class EditorAnalysisController final : public QObject {
    Q_OBJECT
public:
    explicit EditorAnalysisController(QObject* parent = nullptr);
    ~EditorAnalysisController() override;

    [[nodiscard]] quint64 currentRevision() const { return m_revision; }
    [[nodiscard]] LanguageAnalysisSnapshotPtr currentSnapshot() const { return m_snapshot; }
    [[nodiscard]] qsizetype staleResultCount() const { return m_staleResultCount; }

    /** Tier 0: called synchronously from QTextDocument::contentsChange. */
    void documentChanged(int position, int charsRemoved, int charsAdded);

    /** GUI thread provides the owned snapshot after Tier 2 debounce fires. */
    void submitSourceSnapshot(quint64 revision, QString source);

    /** User-initiated semantic completion request; bypasses the 300 ms timer only. */
    void requestCompletionAnalysis(quint64 revision, QString source);

    /** Stop timers and worker safely before the owning editor is destroyed. */
    void shutdown();

signals:
    /** Tier 1 trigger. Receiver must run on the GUI thread. */
    void fastPassRequested(quint64 revision, DirtyRange dirty);
    /** Tier 2 trigger. TEditor captures QTextDocument text on the GUI thread. */
    void semanticSnapshotRequested(quint64 revision);
    /** A current revision result is ready for highlighter/completion/UI consumers. */
    void analysisApplied(LanguageAnalysisSnapshotPtr snapshot);
    void staleAnalysisDiscarded(quint64 revision);
    void revisionChanged(quint64 revision);

private slots:
    void startFastPass();
    void requestSemanticSnapshot();
    void acceptWorkerResult(LanguageAnalysisSnapshotPtr snapshot);

private:
    void mergeDirtyRange(int position, int charsRemoved, int charsAdded);

    QTimer m_fastTimer;
    QTimer m_semanticTimer;
    QThread m_workerThread;
    AnalysisWorker* m_worker = nullptr;
    quint64 m_revision = 0;
    quint64 m_dispatchedRevision = 0;
    DirtyRange m_dirty;
    LanguageAnalysisSnapshotPtr m_snapshot;
    std::shared_ptr<std::atomic<quint64>> m_latestRevision;
    qsizetype m_staleResultCount = 0;
    bool m_shutdown = false;
};
