#pragma once

#include "TSettings.h"
#include "EditorPreferences.h"
#include "TSyntaxHighlighter.h"
#include "AutoComplete.h"
#include "AutoCompleteUI.h"
#include "EditorAnalysisController.h"
#include "SemanticCompletionProvider.h"
#include "SemanticHoverProvider.h"
#include "SemanticDefinitionProvider.h"
#include "CompletionContext.h"
#include "BreadcrumbTypes.h"
#include "EditorStatusSnapshot.h"

#include <QTimer>
#include <QScrollBar>
#include <QPoint>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QCompleter>
#include <memory>


class LineNumberArea;
class TMinimap;
class THoverPopup;
class QEvent;
class QMouseEvent;
class RecoveryCoordinator;
struct RecoveryEntry;
struct RecoveryWriteResult;
class EditorAnalysisBinding;
class EditorRecoveryBinding;
class EditorInteractionBinding;

class TEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    TEditor(TSettings *setting = nullptr, QWidget *parent = nullptr);
    ~TEditor() override;

    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth() const;
    QString filePath{};

    QString getCurrentLineIndentation(const QTextCursor &cursor) const;
    void curserIndentation();

    void setCompleter(QCompleter *completer);

    void startAutoSave();
    void stopAutoSave();
    void removeBackupFile();
    void setRecoveryCoordinator(RecoveryCoordinator* coordinator);
    [[nodiscard]] QString recoveryDocumentId() const;
    [[nodiscard]] bool hasPendingRecoveryPersistence() const;
    [[nodiscard]] bool isRecoveryRetryScheduled() const;
    [[nodiscard]] quint64 lastRequestedRecoveryRevision() const;
    [[nodiscard]] quint64 lastPersistedRecoveryRevision() const;
    [[nodiscard]] quint64 currentDirtyRecoveryRevision() const;
    void flushRecoverySnapshot();
    void adoptRecoveryEntry(const RecoveryEntry& entry);

    [[nodiscard]] const QVector<EditorDiagnostic>& currentDiagnostics() const {
        return m_currentDiagnostics;
    }
    [[nodiscard]] EditorBreadcrumbContext breadcrumbContextAtCursor() const;
    [[nodiscard]] EditorStatusSnapshot informationSnapshot() const;
    void setDocumentLineEnding(EditorStatusSnapshot::LineEnding lineEnding);
    void setDocumentEncoding(const QString& encodingName);

    struct MatchRange {
        int start;
        int length;
    };

public slots:
    void UpdateTabStopDistance(QFont);
    void updateFontSize(int);
    void updateFontType(QString font);
    void toggleComment();
    void duplicateLine();
    void moveLineUp();
    void moveLineDown();
    void performAutoSave();
    void updateHighlighterTheme(std::shared_ptr<SyntaxTheme>);
    void applyPreferences(const EditorPreferences& preferences);
    void navigateToDiagnosticRange(SourceRange range);
    void highlightSelectedWordMatches();
    void startAsyncWordHighlight();
    static QList<MatchRange> searchWordMatches(const QString &searchText, const QString &documentText);
    void highlightSelectedMatches(const QList<MatchRange> &matches, const QString &searchText, int startPos);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

    void paintEvent(QPaintEvent *event) override;

private:
    /// Returns true for keys that only move the cursor without modifying text.
    static const bool isNavigationKey(const QKeyEvent *e);

    TSyntaxHighlighter* highlighter{};
    EditorAnalysisBinding* analysisBinding{};
    // Non-owning convenience reference; EditorAnalysisBinding owns the controller.
    EditorAnalysisController* analysisController{};

    std::unique_ptr<SemanticCompletionProvider> semanticCompletionProvider{};
    SemanticHoverProvider semanticHoverProvider{};
    SemanticDefinitionProvider semanticDefinitionProvider{};
    THoverPopup* hoverPopup{};
    EditorInteractionBinding* interactionBinding{};
    std::optional<SourceRange> ctrlHoverDefinitionRange{};

    struct NavigationHistoryEntry {
        qsizetype anchor = 0;
        qsizetype position = 0;
    };
    QVector<NavigationHistoryEntry> definitionNavigationHistory{};
    QVector<EditorDiagnostic> m_currentDiagnostics{};
    quint64 m_diagnosticsRevision = 0;

    LineNumberArea *lineNumberArea{};
    TMinimap *minimap{};

    // HighlightSelectedWordMatches enhancements
    QTimer *highlightDebounceTimer{};
    QString lastHighlightedText{};
    int lastHighlightedPosition{-1};
    bool highlightSearchInProgress{false};
    QList<MatchRange> cachedWordMatches{};

    void updateFoldRegions();
    void toggleFold(int blockNum);
    void scheduleRecoveryCapture();
    void clearRecoverySnapshot();

    struct FoldRegion
    {
        int startBlockNumber;
        int endBlockNumber;
        bool folded = false;
    };
    QVector<FoldRegion> foldRegions;

    EditorRecoveryBinding* recoveryBinding{};

    EditorPreferences preferences{};
    EditorStatusSnapshot::LineEnding documentLineEnding = EditorStatusSnapshot::LineEnding::Unknown;
    QString documentEncodingName{QStringLiteral("UTF-8")};

    friend class LineNumberArea;
    friend class TMinimap;

    // Helper function for background word match search
    static QList<QTextEdit::ExtraSelection> searchWordMatches(
        const QString &searchText,
        const QString &documentText,
        int searchStartPosition,
        bool isPartialWord);

    QCompleter *c{};
    CompletionModel *model{};
    std::vector<std::unique_ptr<ICompletionStrategy>> strategies{};
    QStringList snippetTargets{};
    CompletionContext activeCompletionContext{};
    quint64 activeCompletionRevision = 0;
    bool hasActiveCompletionContext = false;
    // Set only by a user-triggered completion pass while semantic analysis is pending.
    bool completionRefreshPending = false;
    [[nodiscard]] bool canRefreshActiveCompletion() const;
    void clearActiveCompletionContext();
    void dismissCompletionPopup();
    void scheduleHover(const QPoint& viewportPosition);
    void showPendingHover();
    void dismissHover();
    void updateCtrlHoverDefinitionLink(const QPoint& viewportPosition);
    void clearCtrlHoverDefinitionLink();
    [[nodiscard]] std::optional<DefinitionLocation> definitionAt(qsizetype offset) const;
    bool navigateToDefinition(qsizetype offset);
    bool navigateBackFromDefinition();
    void notifyBreadcrumbContextChanged();
    void notifyEditorInformationChanged();
    QTextCursor textUnderCursor() const;
    void performCompletion();
    bool processSnippetNavigation();
    void setupAutoComplete();
    void insertWord(const QString &completion, QTextCursor &tc);
    void insertBuiltinFunction(const QString &functionName, QTextCursor &tc);
    void insertSnippet(const QString &snippet, QTextCursor &tc);
    // Bracket auto-completion methods
    bool handleAutoPairing(QKeyEvent *e);
    bool handleBracketCompletion(QChar openingBracket, QChar closingBracket);
    bool handleQuoteCompletion(QChar quoteChar);
    bool handleBracketSkip(QChar typedChar);

private slots:
    void updateMinimapPosition();
    void updateLineNumberAreaWidth();
    inline void updateLineNumberArea(const QRect &rect, int dy);
    void insertCompletion(const QString &completion, CompletionType type);
signals:
    void openRequest(QString filePath);
    void diagnosticsChanged(QVector<EditorDiagnostic> diagnostics, quint64 revision);
    void breadcrumbContextChanged(EditorBreadcrumbContext context);
    void editorInformationChanged(EditorStatusSnapshot snapshot);
};

class LineNumberArea : public QWidget
{
public:
    LineNumberArea(TEditor *editor) : QWidget(editor), tEditor(editor)
    {
        this->setStyleSheet(
            "QWidget {"
            "   border-left: 1px solid #10a8f4;"
            "   border-top-left-radius: 9px;"
            "   border-bottom-left-radius: 9px;"
            "}");
    }

    QSize sizeHint() const override
    {
        return QSize(tEditor->lineNumberAreaWidth(), 0);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        int y = event->position().y();
        QTextBlock block = tEditor->firstVisibleBlock();
        int top = qRound(tEditor->blockBoundingGeometry(block).translated(tEditor->contentOffset()).top());
        int height = qRound(tEditor->blockBoundingRect(block).height());

        while (block.isValid() && top <= y)
        {
            if (y >= top && y < top + height)
            {
                int blockNum = block.blockNumber();
                tEditor->toggleFold(blockNum);
                return;
            }
            block = block.next();
            top += height;
            height = qRound(tEditor->blockBoundingRect(block).height());
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        tEditor->lineNumberAreaPaintEvent(event);
    }

private:
    TEditor *tEditor{};
};
