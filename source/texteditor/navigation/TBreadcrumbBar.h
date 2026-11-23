#pragma once

#include "BreadcrumbTypes.h"

#include <QFrame>
#include <QVector>

class QHBoxLayout;

/**
 * Presentation-only breadcrumb bar for one active editor. It receives normalized
 * file and semantic context from the main window/editor and emits exact local
 * navigation requests; it never reads QTextDocument or runs language analysis.
 */
class TBreadcrumbBar final : public QFrame {
    Q_OBJECT

public:
    explicit TBreadcrumbBar(QWidget* parent = nullptr);

    void setFileContext(const QString& filePath);
    void setSemanticContext(const EditorBreadcrumbContext& context);
    void clearSemanticContext();

    [[nodiscard]] QString currentFilePath() const { return m_filePath; }
    [[nodiscard]] const EditorBreadcrumbContext& semanticContext() const {
        return m_semanticContext;
    }

signals:
    void fileSegmentActivated(const QString& path);
    void symbolSegmentActivated(SourceRange declarationRange);

private:
    struct FileSegment final {
        QString label;
        QString path;
        bool isDirectory = false;
    };

    void rebuild();
    void clearLayout();
    void addSeparator();
    void addFileSegment(const FileSegment& segment, int index);
    void addSymbolSegment(const SemanticBreadcrumb& segment, int index);
    [[nodiscard]] QVector<FileSegment> fileSegments() const;

    QHBoxLayout* m_layout = nullptr;
    QString m_filePath;
    EditorBreadcrumbContext m_semanticContext;
};
