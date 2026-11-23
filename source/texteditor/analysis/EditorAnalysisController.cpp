#include "EditorAnalysisController.h"
#include "AlifSemanticPresentationAdapter.h"
#include "AlifDiagnosticPresentationAdapter.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QMetaObject>

#include <limits>
#include <utility>

AnalysisWorker::AnalysisWorker(std::shared_ptr<std::atomic<quint64>> latestRevision,
                               QObject* parent)
    : QObject(parent),
      m_latestRevision(std::move(latestRevision)) {
}

void AnalysisWorker::process(AnalysisRequest request) {
    if (!m_latestRevision || m_latestRevision->load() != request.revision) {
        return;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    auto snapshot = std::make_shared<LanguageAnalysisSnapshot>();
    snapshot->revision = request.revision;

    QElapsedTimer stageTimer;
    stageTimer.start();
    snapshot->lex = TaifLexer().lex(request.source);
    snapshot->metrics.lexMilliseconds = stageTimer.elapsed();
    snapshot->metrics.tokenCount = snapshot->lex.tokens.size();
    if (m_latestRevision->load() != request.revision) {
        return;
    }

    stageTimer.restart();
    snapshot->parse = TaifParser().parse(request.source, snapshot->lex, request.revision);
    snapshot->metrics.parseMilliseconds = stageTimer.elapsed();
    if (m_latestRevision->load() != request.revision) {
        return;
    }

    stageTimer.restart();
    const SymbolTableInput input {*snapshot->parse.ast,
                                  snapshot->parse.parserDiagnostics,
                                  request.revision};
    snapshot->semantic = SymbolTableBuilder().build(input);
    snapshot->metrics.semanticMilliseconds = stageTimer.elapsed();
    if (m_latestRevision->load() != request.revision) {
        return;
    }

    stageTimer.restart();
    snapshot->diagnostics = DiagnosticPresentationAdapter().collect(
        snapshot->lex, snapshot->parse, snapshot->semantic);
    snapshot->spans = SemanticPresentationAdapter().classify(
        snapshot->lex, snapshot->parse, snapshot->semantic, snapshot->diagnostics);
    snapshot->metrics.presentationMilliseconds = stageTimer.elapsed();
    snapshot->metrics.spanCount = snapshot->spans.size();
    snapshot->metrics.totalMilliseconds = totalTimer.elapsed();

    if (m_latestRevision->load() == request.revision) {
        emit analysisReady(snapshot);
    }
}

EditorAnalysisController::EditorAnalysisController(QObject* parent)
    : QObject(parent),
      m_latestRevision(std::make_shared<std::atomic<quint64>>(0)) {
    qRegisterMetaType<AnalysisRequest>("AnalysisRequest");
    qRegisterMetaType<LanguageAnalysisSnapshotPtr>("LanguageAnalysisSnapshotPtr");

    m_fastTimer.setSingleShot(true);
    m_fastTimer.setInterval(150);
    m_semanticTimer.setSingleShot(true);
    m_semanticTimer.setInterval(300);
    connect(&m_fastTimer, &QTimer::timeout,
            this, &EditorAnalysisController::startFastPass);
    connect(&m_semanticTimer, &QTimer::timeout,
            this, &EditorAnalysisController::requestSemanticSnapshot);

    m_worker = new AnalysisWorker(m_latestRevision);
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &AnalysisWorker::analysisReady,
            this, &EditorAnalysisController::acceptWorkerResult,
            Qt::QueuedConnection);
    m_workerThread.start();
}

EditorAnalysisController::~EditorAnalysisController() {
    shutdown();
}

void EditorAnalysisController::documentChanged(const int position,
                                               const int charsRemoved,
                                               const int charsAdded) {
    if (m_shutdown) {
        return;
    }
    ++m_revision;
    m_latestRevision->store(m_revision);
    mergeDirtyRange(position, charsRemoved, charsAdded);
    m_fastTimer.start();
    m_semanticTimer.start();
    emit revisionChanged(m_revision);
}

void EditorAnalysisController::submitSourceSnapshot(const quint64 revision,
                                                     QString source) {
    if (m_shutdown || revision != m_revision || source.isNull()
        || m_dispatchedRevision == revision) {
        return;
    }
    m_dispatchedRevision = revision;
    AnalysisRequest request;
    request.revision = revision;
    request.source = std::move(source);
    request.dirty = m_dirty;
    QMetaObject::invokeMethod(m_worker, "process", Qt::QueuedConnection,
                              Q_ARG(AnalysisRequest, request));
}

void EditorAnalysisController::requestCompletionAnalysis(const quint64 revision,
                                                          QString source) {
    // Completion is user initiated; it bypasses only the debounce timer and
    // still runs asynchronously through the normal revision gate.
    submitSourceSnapshot(revision, std::move(source));
}

void EditorAnalysisController::shutdown() {
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
    m_fastTimer.stop();
    m_semanticTimer.stop();
    if (m_latestRevision) {
        m_latestRevision->store(std::numeric_limits<quint64>::max());
    }
    if (m_workerThread.isRunning()) {
        m_workerThread.quit();
        m_workerThread.wait();
    }
    m_worker = nullptr;
    m_snapshot.reset();
}

void EditorAnalysisController::startFastPass() {
    if (!m_shutdown) {
        emit fastPassRequested(m_revision, m_dirty);
    }
}

void EditorAnalysisController::requestSemanticSnapshot() {
    if (!m_shutdown) {
        emit semanticSnapshotRequested(m_revision);
    }
}

void EditorAnalysisController::acceptWorkerResult(LanguageAnalysisSnapshotPtr snapshot) {
    if (m_shutdown || !snapshot || snapshot->revision != m_revision) {
        ++m_staleResultCount;
        emit staleAnalysisDiscarded(snapshot ? snapshot->revision : 0);
        return;
    }
    m_snapshot = std::move(snapshot);
    m_dirty = {};
    emit analysisApplied(m_snapshot);
}

void EditorAnalysisController::mergeDirtyRange(const int position,
                                                const int charsRemoved,
                                                const int charsAdded) {
    const qsizetype begin = qMax(0, position);
    const qsizetype end = begin + qMax(charsRemoved, charsAdded);
    if (!m_dirty.isValid() || (m_dirty.beginOffset == 0 && m_dirty.endOffset == 0)) {
        m_dirty.beginOffset = begin;
        m_dirty.endOffset = end;
        return;
    }
    m_dirty.beginOffset = qMin(m_dirty.beginOffset, begin);
    m_dirty.endOffset = qMax(m_dirty.endOffset, end);
}
