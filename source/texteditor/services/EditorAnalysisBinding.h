#pragma once

#include "AlifAnalysis.h"

#include <QObject>
#include <QMetaObject>
#include <QPointer>

#include <functional>

class QTextDocument;
class EditorAnalysisController;

/**
 * Non-visual ownership boundary for the editor analysis service.
 *
 * The binding connects only stable QTextDocument change signals to the existing
 * three-tier analysis controller. TEditor remains the widget façade that paints
 * snapshots and presents diagnostics/completion, while this object owns service
 * initialization and the deterministic shutdown order.
 */
class EditorAnalysisBinding final : public QObject
{
    Q_OBJECT

public:
    using SourceSnapshotProvider = std::function<QString()>;

    static constexpr qsizetype LargeDocumentCharacterThreshold = 512 * 1024;

    explicit EditorAnalysisBinding(QTextDocument* document, QObject* parent = nullptr);
    ~EditorAnalysisBinding() override;

    void initialize();
    void shutdown();
    void setSourceSnapshotProvider(SourceSnapshotProvider provider);

    [[nodiscard]] EditorAnalysisController* controller() const;
    [[nodiscard]] qsizetype lastSnapshotCharacterCount() const;
    [[nodiscard]] qint64 lastSnapshotDurationMilliseconds() const;
    [[nodiscard]] AnalysisMetrics lastAnalysisMetrics() const;

signals:
    void sourceSnapshotMeasured(quint64 revision, qsizetype characterCount,
                               qint64 durationMilliseconds, bool largeDocument);
    void analysisMetricsAvailable(AnalysisMetrics metrics);

private:
    void submitSemanticSnapshot(quint64 revision);
    QPointer<QTextDocument> m_document;
    EditorAnalysisController* m_controller = nullptr;
    QMetaObject::Connection m_documentChangeConnection;
    QMetaObject::Connection m_semanticSnapshotConnection;
    QMetaObject::Connection m_analysisMetricsConnection;
    SourceSnapshotProvider m_sourceSnapshotProvider;
    qsizetype m_lastSnapshotCharacterCount = 0;
    qint64 m_lastSnapshotDurationMilliseconds = 0;
    AnalysisMetrics m_lastAnalysisMetrics{};
    bool m_initialized = false;
    bool m_shutdown = false;
};
