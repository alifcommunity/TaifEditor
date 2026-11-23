#pragma once

#include "TLexer.h"
#include "TSyntaxThemes.h"
#include "AlifAnalysis.h"

#include <QSyntaxHighlighter>

class TSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit TSyntaxHighlighter(QTextDocument* parent = nullptr);

    // Switch theme
    void setTheme(const std::shared_ptr<SyntaxTheme>& theme);

    /** Tier 1: bounded GUI-thread rehighlight of dirty blocks after 150 ms idle. */
    void runFastPass(quint64 revision, const DirtyRange& dirty);

    /** Tier 2: install current-revision data-only semantic overlays. */
    void setSemanticSnapshot(LanguageAnalysisSnapshotPtr snapshot);
    void clearSemanticSnapshot(quint64 revision);
    void setDiagnosticsVisible(bool visible);
    [[nodiscard]] quint64 semanticRevision() const { return m_semanticRevision; }
    [[nodiscard]] quint64 fastPassRevision() const { return m_fastPassRevision; }

protected:
    void highlightBlock(const QString& text) override;

private:
    void rebuildSemanticBlockIndex();
    void applySemanticSpans(const QString& text);
    [[nodiscard]] QTextCharFormat formatForPresentation(const PresentationSpan& span) const;

    std::unique_ptr<TLexer> lexer{};
    QHash<TokenType, QTextCharFormat> currentThemeFormats{};
    LanguageAnalysisSnapshotPtr m_semanticSnapshot;
    QHash<int, QVector<PresentationSpan>> m_semanticSpansByBlock;
    quint64 m_semanticRevision = 0;
    quint64 m_fastPassRevision = 0;
    bool m_diagnosticsVisible = true;
};
