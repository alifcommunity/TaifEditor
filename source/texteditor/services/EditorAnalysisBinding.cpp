#include "EditorAnalysisBinding.h"

#include "EditorAnalysisController.h"

#include <QElapsedTimer>
#include <QTextDocument>

EditorAnalysisBinding::EditorAnalysisBinding(QTextDocument* const document, QObject* const parent)
    : QObject(parent)
    , m_document(document)
    , m_controller(new EditorAnalysisController(this))
{
}

EditorAnalysisBinding::~EditorAnalysisBinding()
{
    shutdown();
}

void EditorAnalysisBinding::initialize()
{
    if (m_initialized || m_shutdown || m_document == nullptr || m_controller == nullptr) {
        return;
    }

    m_initialized = true;
    m_documentChangeConnection = connect(m_document, &QTextDocument::contentsChange,
                                         m_controller,
                                         &EditorAnalysisController::documentChanged);
    m_semanticSnapshotConnection = connect(m_controller,
                                           &EditorAnalysisController::semanticSnapshotRequested,
                                           this, &EditorAnalysisBinding::submitSemanticSnapshot);
    m_analysisMetricsConnection = connect(m_controller,
                                          &EditorAnalysisController::analysisApplied,
                                          this, [this](LanguageAnalysisSnapshotPtr snapshot) {
                                              if (snapshot == nullptr) {
                                                  return;
                                              }
                                              m_lastAnalysisMetrics = snapshot->metrics;
                                              emit analysisMetricsAvailable(m_lastAnalysisMetrics);
                                          });
    // Preserve the existing initial Tier 0/150/300 ms scheduling behavior.
    m_controller->documentChanged(0, 0, 0);
}

void EditorAnalysisBinding::shutdown()
{
    if (m_shutdown) {
        return;
    }

    m_shutdown = true;
    if (m_documentChangeConnection) {
        disconnect(m_documentChangeConnection);
        m_documentChangeConnection = {};
    }
    if (m_semanticSnapshotConnection) {
        disconnect(m_semanticSnapshotConnection);
        m_semanticSnapshotConnection = {};
    }
    if (m_analysisMetricsConnection) {
        disconnect(m_analysisMetricsConnection);
        m_analysisMetricsConnection = {};
    }
    if (m_controller != nullptr) {
        m_controller->shutdown();
    }
}

void EditorAnalysisBinding::setSourceSnapshotProvider(SourceSnapshotProvider provider)
{
    m_sourceSnapshotProvider = std::move(provider);
}

EditorAnalysisController* EditorAnalysisBinding::controller() const
{
    return m_controller;
}

qsizetype EditorAnalysisBinding::lastSnapshotCharacterCount() const
{
    return m_lastSnapshotCharacterCount;
}

qint64 EditorAnalysisBinding::lastSnapshotDurationMilliseconds() const
{
    return m_lastSnapshotDurationMilliseconds;
}

AnalysisMetrics EditorAnalysisBinding::lastAnalysisMetrics() const
{
    return m_lastAnalysisMetrics;
}

void EditorAnalysisBinding::submitSemanticSnapshot(const quint64 revision)
{
    if (m_shutdown || m_controller == nullptr || !m_sourceSnapshotProvider
        || revision != m_controller->currentRevision()) {
        return;
    }

    QElapsedTimer timer;
    timer.start();
    QString source = m_sourceSnapshotProvider();
    m_lastSnapshotDurationMilliseconds = timer.elapsed();
    m_lastSnapshotCharacterCount = source.size();
    emit sourceSnapshotMeasured(revision, m_lastSnapshotCharacterCount,
                                m_lastSnapshotDurationMilliseconds,
                                m_lastSnapshotCharacterCount
                                    >= LargeDocumentCharacterThreshold);
    m_controller->submitSourceSnapshot(revision, std::move(source));
}
