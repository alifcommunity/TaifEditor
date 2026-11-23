#include "TEditor.h"
#include "TMinimap.h"
#include "HoverPopup.h"
#include "RecoveryCoordinator.h"
#include "EditorAnalysisBinding.h"
#include "EditorRecoveryBinding.h"
#include "EditorInteractionBinding.h"

#include <QAction>
#include <QCursor>
#include <QFile>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSettings>
#include <QStack>
#include <QStringConverter>
#include <QTextBlock>
#include <QThreadPool>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QScreen>
#include <QFileInfo>



TEditor::TEditor(TSettings *setting, QWidget *parent)
{
    qRegisterMetaType<EditorBreadcrumbContext>("EditorBreadcrumbContext");
    qRegisterMetaType<EditorStatusSnapshot>("StatusSnapshot");
    setAcceptDrops(true);
    this->setStyleSheet(R"(
    QPlainTextEdit {
        background-color: #091021;
        color: #f1f5f9;
        border: none;
    }
)");

    // set tab distance
    UpdateTabStopDistance(font());

    this->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    this->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QTextDocument *editorDocument = this->document();
    QTextOption option = editorDocument->defaultTextOption();
    option.setTextDirection(Qt::RightToLeft);
    option.setAlignment(Qt::AlignRight);
    editorDocument->setDefaultTextOption(option);

    highlighter = new TSyntaxHighlighter(editorDocument);
    analysisBinding = new EditorAnalysisBinding(editorDocument, this);
    analysisController = analysisBinding->controller();
    analysisBinding->setSourceSnapshotProvider([this]() { return toPlainText(); });
    recoveryBinding = new EditorRecoveryBinding(editorDocument, this);
    recoveryBinding->setSnapshotFactory([this](const quint64 documentRevision) {
        const QString sourcePath = property("filePath").toString();
        RecoveryEntry entry;
        entry.id = recoveryBinding != nullptr ? recoveryBinding->documentId() : QString();
        entry.sourcePath = sourcePath;
        entry.displayName = sourcePath.isEmpty() ? QStringLiteral("غير معنون")
                                                 : QFileInfo(sourcePath).fileName();
        entry.documentRevision = documentRevision;
        entry.sourceFingerprint = RecoveryStore::fingerprintForPath(sourcePath);
        entry.untitled = sourcePath.isEmpty();
        return RecoverySnapshot{entry, toPlainText()};
    });
    semanticCompletionProvider = std::make_unique<SemanticCompletionProvider>();

    hoverPopup = new THoverPopup(this);
    interactionBinding = new EditorInteractionBinding(this);
    interactionBinding->initialize([this]() { showPendingHover(); }, 350);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    connect(editorDocument, &QTextDocument::modificationChanged,
            this, [this](const bool) { notifyEditorInformationChanged(); });
    connect(editorDocument, &QTextDocument::contentsChange,
            this, [this](int, int, int) {
                clearActiveCompletionContext();
                m_currentDiagnostics.clear();
                m_diagnosticsRevision = analysisController
                                    ? analysisController->currentRevision() : 0;
                emit diagnosticsChanged(m_currentDiagnostics, m_diagnosticsRevision);
                dismissHover();
                notifyBreadcrumbContextChanged();
                notifyEditorInformationChanged();
                definitionNavigationHistory.clear();
                clearCtrlHoverDefinitionLink();
            });
    connect(analysisController, &EditorAnalysisController::fastPassRequested,
            this, [this](const quint64 revision, const DirtyRange& dirty) {
                if (highlighter && analysisController
                    && revision == analysisController->currentRevision()) {
                    highlighter->runFastPass(revision, dirty);
                }
            });
    connect(analysisController, &EditorAnalysisController::analysisApplied,
            this, [this](LanguageAnalysisSnapshotPtr snapshot) {
                if (!snapshot) {
                    return;
                }
                m_currentDiagnostics = snapshot->diagnostics;
                m_diagnosticsRevision = snapshot->revision;
                emit diagnosticsChanged(m_currentDiagnostics, m_diagnosticsRevision);
                if (highlighter) {
                    highlighter->setSemanticSnapshot(snapshot);
                }
                // A passive Tier 2 result may update an already active popup,
                // but must never reopen one after the user accepted it.
                if (canRefreshActiveCompletion()) {
                    performCompletion();
                }
                notifyBreadcrumbContextChanged();
                notifyEditorInformationChanged();
            });
    connect(analysisController, &EditorAnalysisController::revisionChanged,
            this, &TEditor::notifyEditorInformationChanged);
    connect(analysisBinding, &EditorAnalysisBinding::sourceSnapshotMeasured,
            this, [this](quint64, qsizetype, qint64, bool) { notifyEditorInformationChanged(); });
    connect(analysisBinding, &EditorAnalysisBinding::analysisMetricsAvailable,
            this, [this](AnalysisMetrics) { notifyEditorInformationChanged(); });
    connect(recoveryBinding, &EditorRecoveryBinding::persistenceStateChanged,
            this, &TEditor::notifyEditorInformationChanged);
    connect(recoveryBinding, &EditorRecoveryBinding::payloadCaptured,
            this, [this](qsizetype, qint64) { notifyEditorInformationChanged(); });
    connect(recoveryBinding, &EditorRecoveryBinding::snapshotWriteFinished,
            this, [this](quint64, qint64, bool) { notifyEditorInformationChanged(); });

    lineNumberArea = new LineNumberArea(this);
    minimap = new TMinimap(this, this);

    // تحديث الخريطة عند التمرير أو تعديل النص
    connect(this->verticalScrollBar(), &QScrollBar::valueChanged, minimap, &TMinimap::updateMinimap);
    connect(this->document(), &QTextDocument::contentsChanged, minimap, &TMinimap::updateMinimap);

    // ضبط الإكمال التلقائي
    setupAutoComplete();

    connect(this, &TEditor::blockCountChanged, this, &TEditor::updateLineNumberAreaWidth);
    connect(this, &TEditor::updateRequest, this, &TEditor::updateLineNumberArea);
    connect(this, &TEditor::cursorPositionChanged, this, [this]() {
        viewport()->update(); // Repaint to update the current-line highlight position.
        highlightSelectedWordMatches();
    });
    connect(this, &TEditor::cursorPositionChanged,
            this, &TEditor::notifyBreadcrumbContextChanged);
    connect(this, &TEditor::cursorPositionChanged,
            this, &TEditor::notifyEditorInformationChanged);

    connect(this->document(), &QTextDocument::contentsChanged, this, &TEditor::updateFoldRegions);

    // Set up debounce timer for highlightSelectedWordMatches
    highlightDebounceTimer = new QTimer(this);
    highlightDebounceTimer->setSingleShot(true);
    highlightDebounceTimer->setInterval(50);
    connect(highlightDebounceTimer, &QTimer::timeout, this, [this]()
            {
        if (!highlightSearchInProgress) {
            startAsyncWordHighlight();
        } });

    updateLineNumberAreaWidth();

    applyPreferences(PreferencesStore::load());
    if (setting != nullptr) {
        const QVector<std::shared_ptr<SyntaxTheme>> themes = setting->getAvailableThemes();
        if (!themes.isEmpty()) {
            updateHighlighterTheme(themes.at(qBound(0, preferences.syntaxThemeIndex,
                                                    themes.size() - 1)));
        }
    }

    installEventFilter(this);

    // Service initialization is explicit and ordered after all widget-side
    // presentation connections have been installed.
    analysisBinding->initialize();
    recoveryBinding->initialize();
    notifyEditorInformationChanged();
}

TEditor::~TEditor() {
    // Stop non-visual services before QObject child destruction can tear down
    // their timers/controllers in an unspecified order.
    if (recoveryBinding != nullptr) {
        recoveryBinding->shutdown();
    }
    if (interactionBinding != nullptr) {
        interactionBinding->shutdown();
    }
    if (analysisBinding != nullptr) {
        analysisBinding->shutdown();
    }
}

EditorStatusSnapshot TEditor::informationSnapshot() const {
    EditorStatusSnapshot snapshot;
    snapshot.hasEditor = true;
    snapshot.documentPath = filePath;
    snapshot.documentName = filePath.isEmpty() ? QStringLiteral("بدون عنوان")
                                               : QFileInfo(filePath).fileName();
    snapshot.modified = document()->isModified();

    const QTextCursor cursor = textCursor();
    snapshot.line = cursor.blockNumber() + 1;
    snapshot.column = cursor.positionInBlock() + 1;
    snapshot.documentLines = blockCount();
    snapshot.documentCharacters = qMax<qsizetype>(0, document()->characterCount() - 1);
    snapshot.selectedCharacters = cursor.hasSelection()
                                      ? cursor.selectionEnd() - cursor.selectionStart() : 0;
    if (cursor.hasSelection()) {
        const QTextBlock selectionStart = document()->findBlock(cursor.selectionStart());
        const QTextBlock selectionEnd = document()->findBlock(cursor.selectionEnd());
        snapshot.selectedLines = qMax(1, selectionEnd.blockNumber() - selectionStart.blockNumber() + 1);
    }

    snapshot.encoding = documentEncodingName;
    snapshot.lineEnding = documentLineEnding;
    snapshot.indentationWidth = preferences.tabWidth;
    snapshot.usesSpaces = !cursor.block().text().startsWith(QLatin1Char('\t'));

    for (const EditorDiagnostic& diagnostic : m_currentDiagnostics) {
        if (diagnostic.severity == SemanticDiagnosticSeverity::Error) {
            ++snapshot.errorCount;
        } else if (diagnostic.severity == SemanticDiagnosticSeverity::Warning) {
            ++snapshot.warningCount;
        }
    }

    if (analysisBinding != nullptr) {
        snapshot.analysisSnapshotCharacters = analysisBinding->lastSnapshotCharacterCount();
        const AnalysisMetrics metrics = analysisBinding->lastAnalysisMetrics();
        snapshot.analysisDurationMilliseconds = metrics.totalMilliseconds;
        snapshot.analysisTokenCount = metrics.tokenCount;
    }
    if (analysisController != nullptr) {
        snapshot.analysisRevision = analysisController->currentRevision();
        const LanguageAnalysisSnapshotPtr appliedSnapshot = analysisController->currentSnapshot();
        if (snapshot.analysisSnapshotCharacters >= EditorAnalysisBinding::LargeDocumentCharacterThreshold) {
            snapshot.analysisState = EditorStatusSnapshot::AnalysisState::LargeDocument;
        } else if (appliedSnapshot != nullptr
                   && appliedSnapshot->revision == analysisController->currentRevision()) {
            snapshot.analysisState = EditorStatusSnapshot::AnalysisState::Ready;
        } else {
            snapshot.analysisState = EditorStatusSnapshot::AnalysisState::Pending;
        }
    }

    if (recoveryBinding != nullptr) {
        snapshot.recoveryWriteDurationMilliseconds = recoveryBinding->lastWriteDurationMilliseconds();
        if (recoveryBinding->isRetryScheduled()) {
            snapshot.recoveryState = EditorStatusSnapshot::RecoveryState::RetryScheduled;
        } else if (recoveryBinding->hasPendingPersistence()) {
            snapshot.recoveryState = EditorStatusSnapshot::RecoveryState::PendingPersistence;
        }
    }
    return snapshot;
}

void TEditor::setDocumentLineEnding(const EditorStatusSnapshot::LineEnding lineEnding)
{
    if (documentLineEnding == lineEnding) {
        return;
    }
    documentLineEnding = lineEnding;
    notifyEditorInformationChanged();
}

void TEditor::setDocumentEncoding(const QString& encodingName)
{
    if (encodingName.isEmpty()) {
        return;
    }
    const QByteArray nameBytes = encodingName.toUtf8();
    if (!QStringDecoder(nameBytes).isValid()) {
        return;
    }
    documentEncodingName = encodingName;
}

void TEditor::UpdateTabStopDistance(QFont font)
{
    QFontMetricsF metrics(font);
    const qreal spaceWidth = metrics.horizontalAdvance(' ');
    setTabStopDistance(preferences.tabWidth * spaceWidth);
}

void TEditor::wheelEvent(QWheelEvent *event)
{
    dismissHover();
    if (event->modifiers() & Qt::ControlModifier)
    {
        const int delta = event->angleDelta().y();
        if (delta == 0)
            return;

        QFont font = this->font();
        int currentSize = font.pixelSize();

        int step = 1;

        if (delta > 0)
        {
            currentSize += step;
        }
        else
        {
            currentSize -= step;
        }

        if (currentSize < 12)
            currentSize = 12;
        if (currentSize > 36)
            currentSize = 36;

        font.setPixelSize(currentSize);
        UpdateTabStopDistance(font);
        this->setFont(font);

        if (lineNumberArea)
        {
            QFont lineFont = lineNumberArea->font();
            lineFont.setPixelSize(currentSize);
            lineNumberArea->setFont(lineFont);
        }
        updateLineNumberAreaWidth();

        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

void TEditor::updateFontSize(int size)
{
    if (size < 10)
    {
        size = 18;
    }

    QFont font = this->font();
    font.setPixelSize(size);

    UpdateTabStopDistance(font);

    this->setFont(font);

    QFont fontNums = lineNumberArea->font();
    fontNums.setPixelSize(size);
    lineNumberArea->setFont(fontNums);
}

void TEditor::updateFontType(QString font)
{
    QFont currentFont = this->font();
    currentFont.setFamily(font);

    UpdateTabStopDistance(currentFont);

    this->setFont(currentFont);
}

// 1. دالة تعليق/إلغاء تعليق الأكواد
void TEditor::toggleComment()
{
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock(); // لبدء عملية تراجع (Undo) واحدة

    int startPos = cursor.selectionStart();
    int endPos = cursor.selectionEnd();

    // تحديد بداية ونهاية الأسطر المحددة
    cursor.setPosition(startPos);
    int startBlock = cursor.blockNumber();
    cursor.setPosition(endPos, QTextCursor::KeepAnchor);
    int endBlock = cursor.blockNumber();

    if (cursor.atBlockStart() && endBlock > startBlock)
    {
        endBlock--;
    }

    bool shouldComment = false;

    QTextBlock block = document()->findBlockByNumber(startBlock);
    if (!block.text().trimmed().startsWith("#"))
    {
        shouldComment = true;
    }

    for (int i = startBlock; i <= endBlock; ++i)
    {
        block = document()->findBlockByNumber(i);
        QTextCursor lineCursor(block);

        if (shouldComment)
        {
            lineCursor.movePosition(QTextCursor::StartOfBlock);
            lineCursor.insertText("#");
        }
        else
        {
            QString text = block.text();
            int idx = text.indexOf("#");
            lineCursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, idx);
            lineCursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 1);
            lineCursor.removeSelectedText();
        }
    }

    cursor.endEditBlock();
}

void TEditor::duplicateLine()
{
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();

    QString lineText = cursor.block().text();

    cursor.movePosition(QTextCursor::EndOfBlock);

    cursor.insertText("\n" + lineText);

    cursor.endEditBlock();
}

void TEditor::moveLineUp()
{
    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QTextBlock prevBlock = currentBlock.previous();

    if (!prevBlock.isValid())
        return;

    cursor.beginEditBlock();

    QString currentText = currentBlock.text();
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.deletePreviousChar();

    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.insertText(currentText + "\n");

    cursor.movePosition(QTextCursor::Up);
    setTextCursor(cursor);

    cursor.endEditBlock();
}

void TEditor::moveLineDown()
{
    QTextCursor cursor = textCursor();
    QTextBlock currentBlock = cursor.block();
    QTextBlock nextBlock = currentBlock.next();

    if (!nextBlock.isValid())
        return;

    cursor.beginEditBlock();

    QString currentText = currentBlock.text();
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    if (cursor.atBlockStart())
        cursor.deleteChar();

    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertText("\n" + currentText);

    setTextCursor(cursor);
    cursor.endEditBlock();
}

bool TEditor::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == this and event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

        if (keyEvent->key() == Qt::Key_Return or keyEvent->key() == Qt::Key_Enter)
        {
            if (keyEvent->modifiers() & Qt::ShiftModifier)
            {
                return true;
            }
            curserIndentation();
            return true;
        }
    }
    return QPlainTextEdit::eventFilter(obj, event);
}

void TEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();

    menu->addSeparator();

    const QTextCursor contextCursor = cursorForPosition(event->pos());
    const qsizetype definitionOffset = contextCursor.position();
    QAction *definitionAction = new QAction("اذهب إلى التعريف", this);
    definitionAction->setShortcut(QKeySequence(Qt::Key_F12));
    definitionAction->setEnabled(definitionAt(definitionOffset).has_value());
    connect(definitionAction, &QAction::triggered, this, [this, definitionOffset]() {
        navigateToDefinition(definitionOffset);
    });
    menu->addAction(definitionAction);

    QAction *commentAction = new QAction("تعليق/إلغاء تعليق", this);
    commentAction->setShortcut(QKeySequence("Ctrl+/"));
    connect(commentAction, &QAction::triggered, this, &TEditor::toggleComment);
    menu->addAction(commentAction);

    QAction *duplicateAction = new QAction("تكرار السطر", this);
    duplicateAction->setShortcut(QKeySequence("Ctrl+D"));
    connect(duplicateAction, &QAction::triggered, this, &TEditor::duplicateLine);
    menu->addAction(duplicateAction);

    menu->exec(event->globalPos());

    delete menu;
}

int TEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10)
    {
        max /= 10;
        ++digits;
    }

    int space = 36 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;

    return space;
}

void TEditor::updateMinimapPosition()
{
    if (minimap == nullptr || !preferences.minimapVisible) {
        return;
    }

    int mapX = 0;
    // بسبب الاتجاه من اليمين لليسار قد يكون شريط التمرير على يمين الخريطة المصغرة
    if (verticalScrollBar()->isVisible() && verticalScrollBar()->x() < width() / 2)
    {
        mapX = verticalScrollBar()->width();
    }
    // تم تنقيص 3 من الاعلى لكي لا يقوم بالتغطية على حواف المحرر
    minimap->move(mapX, 3);
}

void TEditor::updateLineNumberAreaWidth()
{
    const int numsWidth = preferences.lineNumbersVisible ? lineNumberAreaWidth() : 0;
    const int mapWidth = preferences.minimapVisible ? 100 : 0;

    setViewportMargins(mapWidth, 0, numsWidth, 0);
}

inline void TEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void TEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);

    QRect cr = contentsRect();
    int numsWidth = lineNumberAreaWidth();

    lineNumberArea->setGeometry(this->width() - numsWidth, cr.top(), numsWidth, cr.height());

    if (minimap)
    {
        // تم تنقيص 3 من الاسفل لكي لا يقوم بالتغطية على حواف المحرر
        minimap->setGeometry(cr.left(), cr.top(), 100, cr.height() - 3);
        updateMinimapPosition();
    }
}

void TEditor::showEvent(QShowEvent *event)
{
    QPlainTextEdit::showEvent(event);

    if (minimap)
    {
        QRect cr = contentsRect();
        // تم تنقيص 3 من الاسفل لكي لا يقوم بالتغطية على حواف المحرر
        minimap->setGeometry(cr.left(), cr.top(), 100, cr.height() - 3);
        updateMinimapPosition();
    }
}

void TEditor::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton
        && event->modifiers().testFlag(Qt::ControlModifier)) {
        const QTextCursor clickedCursor = cursorForPosition(event->position().toPoint());
        if (navigateToDefinition(clickedCursor.position())) {
            event->accept();
            return;
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}
void TEditor::mouseMoveEvent(QMouseEvent* event) {
    const QPoint viewportPosition = event->position().toPoint();
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        // While Ctrl is held the editor shows a definition link underline; suppress
        // the semantic hover popup so it never overlaps with that interaction.
        updateCtrlHoverDefinitionLink(viewportPosition);
        dismissHover();
    } else {
        clearCtrlHoverDefinitionLink();
        scheduleHover(viewportPosition);
    }
    QPlainTextEdit::mouseMoveEvent(event);
}

void TEditor::leaveEvent(QEvent* event) {
    dismissHover();
    clearCtrlHoverDefinitionLink();
    QPlainTextEdit::leaveEvent(event);
}

void TEditor::scheduleHover(const QPoint& viewportPosition) {
    if (!preferences.hoverInformationEnabled || !analysisController || !hasFocus()
        || (c && c->popup()->isVisible())) {
        return;
    }
    const QTextCursor cursor = cursorForPosition(viewportPosition);
    const qsizetype offset = cursor.position();
    const LanguageAnalysisSnapshotPtr snapshot = analysisController->currentSnapshot();
    const quint64 revision = analysisController->currentRevision();
    if (!snapshot || snapshot->revision != revision || offset < 0
        || offset >= document()->characterCount() - 1) {
        dismissHover();
        return;
    }
    if (hoverPopup && hoverPopup->isVisible() && interactionBinding != nullptr
        && interactionBinding->matches(offset, revision)) {
        return;
    }
    if (interactionBinding != nullptr) {
        interactionBinding->scheduleHover(viewportPosition, offset, revision);
    }
}

void TEditor::showPendingHover() {
    if (!analysisController || !hoverPopup || !hasFocus()
        || (c && c->popup()->isVisible())) {
        dismissHover();
        return;
    }
    const quint64 revision = analysisController->currentRevision();
    const LanguageAnalysisSnapshotPtr snapshot = analysisController->currentSnapshot();
    if (interactionBinding == nullptr || !snapshot
        || interactionBinding->pendingOffset() < 0
        || interactionBinding->pendingRevision() != revision
        || snapshot->revision != revision) {
        dismissHover();
        return;
    }
    const QTextCursor currentCursor =
        cursorForPosition(interactionBinding->pendingViewportPosition());
    if (currentCursor.position() != interactionBinding->pendingOffset()) {
        dismissHover();
        return;
    }
    const std::optional<HoverInfo> info = semanticHoverProvider.infoAt(
        interactionBinding->pendingOffset(), toPlainText(), snapshot);
    if (!info.has_value()) {
        dismissHover();
        return;
    }

    hoverPopup->setHoverInfo(*info);
    hoverPopup->adjustSize();
    const QSize popupSize = hoverPopup->size();
    constexpr int pointerGap = 12;
    const QPoint pointerPosition =
        viewport()->mapToGlobal(interactionBinding->pendingViewportPosition());
    QScreen* screen = QGuiApplication::screenAt(pointerPosition);
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect available = screen ? screen->availableGeometry() : QRect();
    // Prefer the left side of the pointer so the tooltip does not cover the
    // token being inspected. If the left edge cannot accommodate it, use the
    // right side rather than overlapping the pointer.
    QPoint popupPosition(pointerPosition.x() - popupSize.width() - pointerGap,
                         pointerPosition.y() + pointerGap);
    if (available.isValid()) {
        if (popupPosition.x() < available.left()) {
            popupPosition.setX(pointerPosition.x() + pointerGap);
        }
        popupPosition.setX(qBound(available.left(), popupPosition.x(),
                                  available.right() - popupSize.width() + 1));
        if (popupPosition.y() + popupSize.height() > available.bottom() + 1) {
            popupPosition.setY(pointerPosition.y() - popupSize.height() - pointerGap);
        }
        popupPosition.setY(qBound(available.top(), popupPosition.y(),
                                  available.bottom() - popupSize.height() + 1));
    }
    hoverPopup->move(popupPosition);
    hoverPopup->show();
}

void TEditor::dismissHover() {
    if (interactionBinding != nullptr) {
        interactionBinding->dismissHover();
    }
    if (hoverPopup) {
        hoverPopup->hide();
    }
}

void TEditor::updateCtrlHoverDefinitionLink(const QPoint& viewportPosition) {
    const QTextCursor cursor = cursorForPosition(viewportPosition);
    const std::optional<DefinitionLocation> location = definitionAt(cursor.position());
    const std::optional<SourceRange> nextRange = location.has_value()
                                                     ? std::optional<SourceRange>(location->sourceRange)
                                                     : std::nullopt;

    if (location.has_value()) {
        viewport()->setCursor(Qt::PointingHandCursor);
    }
    const bool rangeIsUnchanged = ctrlHoverDefinitionRange.has_value()
                                      == nextRange.has_value()
                                  && (!nextRange.has_value()
                                      || (ctrlHoverDefinitionRange->begin.offset == nextRange->begin.offset
                                          && ctrlHoverDefinitionRange->end.offset == nextRange->end.offset));
    if (rangeIsUnchanged) {
        return;
    }
    ctrlHoverDefinitionRange = nextRange;
    highlightSelectedMatches(QList<MatchRange>{}, QString(), -1);
}

void TEditor::clearCtrlHoverDefinitionLink() {
    viewport()->setCursor(Qt::IBeamCursor);
    if (!ctrlHoverDefinitionRange.has_value()) {
        return;
    }
    ctrlHoverDefinitionRange.reset();
    highlightSelectedMatches(QList<MatchRange>{}, QString(), -1);
}

std::optional<DefinitionLocation> TEditor::definitionAt(const qsizetype offset) const {
    if (!analysisController || offset < 0 || offset >= document()->characterCount() - 1) {
        return std::nullopt;
    }
    const LanguageAnalysisSnapshotPtr snapshot = analysisController->currentSnapshot();
    if (!snapshot || snapshot->revision != analysisController->currentRevision()) {
        return std::nullopt;
    }
    return semanticDefinitionProvider.definitionAt(offset, toPlainText(), snapshot);
}

bool TEditor::navigateToDefinition(const qsizetype offset) {
    const std::optional<DefinitionLocation> location = definitionAt(offset);
    if (!location.has_value()) {
        return false;
    }
    const qsizetype documentLength = document()->characterCount() - 1;
    if (location->declarationRange.begin.offset < 0
        || location->declarationRange.end.offset > documentLength
        || location->declarationRange.end.offset <= location->declarationRange.begin.offset) {
        return false;
    }

    QTextCursor destination(document());
    destination.setPosition(location->declarationRange.begin.offset);
    destination.setPosition(location->declarationRange.end.offset, QTextCursor::KeepAnchor);
    const QTextCursor current = textCursor();
    if (current.anchor() == destination.anchor() && current.position() == destination.position()) {
        return true;
    }

    constexpr qsizetype maximumHistoryEntries = 64;
    if (definitionNavigationHistory.size() >= maximumHistoryEntries) {
        definitionNavigationHistory.removeFirst();
    }
    definitionNavigationHistory.append({current.anchor(), current.position()});
    dismissCompletionPopup();
    dismissHover();
    clearCtrlHoverDefinitionLink();
    setTextCursor(destination);
    ensureCursorVisible();
    setFocus(Qt::OtherFocusReason);
    return true;
}

bool TEditor::navigateBackFromDefinition() {
    if (definitionNavigationHistory.isEmpty()) {
        return false;
    }
    const NavigationHistoryEntry entry = definitionNavigationHistory.takeLast();
    const qsizetype documentLength = document()->characterCount() - 1;
    if (entry.anchor < 0 || entry.position < 0 || entry.anchor > documentLength
        || entry.position > documentLength) {
        return false;
    }
    QTextCursor destination(document());
    destination.setPosition(entry.anchor);
    destination.setPosition(entry.position, QTextCursor::KeepAnchor);
    dismissCompletionPopup();
    dismissHover();
    clearCtrlHoverDefinitionLink();
    setTextCursor(destination);
    ensureCursorVisible();
    setFocus(Qt::OtherFocusReason);
    return true;
}

void TEditor::navigateToDiagnosticRange(const SourceRange range) {
    const qsizetype documentLength = document()->characterCount() - 1;
    if (range.begin.offset < 0 || range.end.offset <= range.begin.offset
        || range.end.offset > documentLength) {
        return;
    }
    QTextCursor destination(document());
    destination.setPosition(range.begin.offset);
    destination.setPosition(range.end.offset, QTextCursor::KeepAnchor);
    dismissCompletionPopup();
    clearCtrlHoverDefinitionLink();
    dismissHover();
    setTextCursor(destination);
    ensureCursorVisible();
    setFocus(Qt::OtherFocusReason);
}

void TEditor::lineNumberAreaPaintEvent(QPaintEvent *event) {

    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), Qt::transparent);
    painter.setRenderHint(QPainter::Antialiasing);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    // Arrow pen
    QPen arrowPen(QColor(37, 70, 99));
    arrowPen.setWidth(3);
    arrowPen.setJoinStyle(Qt::RoundJoin);
    arrowPen.setCapStyle(Qt::RoundCap);

    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible() && bottom >= event->rect().top())
        {
            QString number = QString::number(blockNumber + 1);

            painter.setPen(QColor(200, 200, 200));
            painter.drawText(12, top, lineNumberArea->width(), fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignVCenter, number);

            for (const auto &region : foldRegions)
            {
                if (region.startBlockNumber == blockNumber)
                {
                    painter.setPen(arrowPen);
                    painter.setBrush(QColor(37, 70, 99));

                    int midY = top + fontMetrics().height() / 2;
                    int rightEdge = lineNumberArea->width() - 6;
                    int leftEdge = rightEdge - 8;

                    QPolygonF arrow;
                    if (region.folded)
                    {
                        // Left-pointing Triangle
                        arrow << QPoint(rightEdge, midY - 4)
                              << QPoint(leftEdge, midY)
                              << QPoint(rightEdge, midY + 4);
                    }
                    else
                    {
                        // Down-pointing Triangle
                        arrow << QPoint(leftEdge, midY - 3)
                              << QPoint(rightEdge, midY - 3)
                              << QPoint((leftEdge + rightEdge) / 2.0, midY + 4);
                    }

                    painter.drawPolygon(arrow);
                }
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
        ++blockNumber;
    }
}


void TEditor::updateFoldRegions()
{

    // we use static array for zero memory allocation per call
    static const QStringView foldTriggers[] = {
        u"صنف", u"دالة", u"اذا", u"إذا", u"والا", u"وإلا",
        u"اواذا", u"أوإذا", u"بينما", u"لكل", u"حاول", u"خلل", u"نهاية"};

    // Preserve previous fold states
    QHash<int, bool> previousFoldStates;
    previousFoldStates.reserve(foldRegions.size());
    for (const FoldRegion &region : foldRegions)
    {
        previousFoldStates.insert(region.startBlockNumber, region.folded);
    }
    foldRegions.clear();

    // here we use single-pass O(N) fold detection using a Stack
    struct ActiveFold
    {
        int startBlock;
        int indent;
    };
    QStack<ActiveFold> stack;
    int lastValidBlockNumber = -1;

    QTextBlock block = document()->firstBlock();
    while (block.isValid())
    {
        QString text = block.text();
        // using QStringView here to avoids making a copy of the string just to trim it
        QStringView trimmed = QStringView(text).trimmed();

        if (trimmed.isEmpty())
        {
            block = block.next();
            continue;
        }

        // Fast inline indent calculation
        int indent = 0;
        for (QChar c : text)
        {
            if (c == u'\t')
                indent += 8;
            else if (c == u' ')
                indent += 1;
            else
                break;
        }

        // Close fold regions where indentation drops back to or below the parent
        while (!stack.isEmpty() && indent <= stack.top().indent)
        {
            ActiveFold af = stack.pop();
            if (lastValidBlockNumber > af.startBlock)
            {
                FoldRegion region{};
                region.startBlockNumber = af.startBlock;
                region.endBlockNumber = lastValidBlockNumber;
                region.folded = previousFoldStates.value(af.startBlock, false);
                foldRegions.append(region);
            }
        }

        // now check if the current line starts with any trigger word
        bool isTrigger = false;
        for (const QStringView &trigger : foldTriggers)
        {
            if (trimmed.startsWith(trigger))
            {
                isTrigger = true;
                break;
            }
        }

        if (isTrigger)
        {
            stack.push({block.blockNumber(), indent});
        }

        lastValidBlockNumber = block.blockNumber();
        block = block.next();
    }

    // at the end close any unclosed folds remaining at the end of the document
    while (!stack.isEmpty())
    {
        ActiveFold af = stack.pop();
        if (lastValidBlockNumber > af.startBlock)
        {
            FoldRegion region;
            region.startBlockNumber = af.startBlock;
            region.endBlockNumber = lastValidBlockNumber;
            region.folded = previousFoldStates.value(af.startBlock, false);
            foldRegions.append(region);
        }
    }

    if (lineNumberArea)
    {
        lineNumberArea->update();
    }

    // Safely apply visibility using merged intervals (Sweep-Line logic)
    // to prevents inner child folds from unhiding blocks that belong to a folded parent.
    std::vector<std::pair<int, int>> hiddenIntervals;
    hiddenIntervals.reserve(foldRegions.size());
    for (const FoldRegion &r : foldRegions)
    {
        if (r.folded)
        {
            hiddenIntervals.push_back({r.startBlockNumber + 1, r.endBlockNumber});
        }
    }

    // Sort and merge overlapping hidden intervals
    std::sort(hiddenIntervals.begin(), hiddenIntervals.end());
    std::vector<std::pair<int, int>> mergedHidden;
    mergedHidden.reserve(hiddenIntervals.size());
    for (const auto &interval : hiddenIntervals)
    {
        if (mergedHidden.empty() || mergedHidden.back().second < interval.first)
        {
            mergedHidden.push_back(interval);
        }
        else
        {
            mergedHidden.back().second = std::max(mergedHidden.back().second, interval.second);
        }
    }

    // Single pass to apply block visibility changes (Here massive UI performance boost :)
    block = document()->firstBlock();
    auto intervalIt = mergedHidden.begin();
    while (block.isValid())
    {
        int bNum = block.blockNumber();

        while (intervalIt != mergedHidden.end() && intervalIt->second < bNum)
        {
            ++intervalIt;
        }

        bool shouldBeHidden = (intervalIt != mergedHidden.end() && bNum >= intervalIt->first && bNum <= intervalIt->second);

        // Only trigger a state change if necessary to avoid unnecessary Qt paint events
        if (block.isVisible() == shouldBeHidden)
        {
            block.setVisible(!shouldBeHidden);
        }

        block = block.next();
    }

    document()->markContentsDirty(0, document()->characterCount());
    viewport()->update();
}

void TEditor::toggleFold(int blockNumber)
{
    for (FoldRegion &region : foldRegions)
    {
        if (region.startBlockNumber == blockNumber)
        {
            region.folded = !region.folded;

            QTextBlock block = document()->findBlockByNumber(region.startBlockNumber + 1);
            while (block.isValid() && block.blockNumber() <= region.endBlockNumber)
            {
                block.setVisible(!region.folded);
                block = block.next();
            }

            if (!region.folded)
            {
                for (FoldRegion &subRegion : foldRegions)
                {
                    if (subRegion.startBlockNumber > region.startBlockNumber &&
                        subRegion.endBlockNumber <= region.endBlockNumber)
                    {
                        QTextBlock subBlock = document()->findBlockByNumber(subRegion.startBlockNumber + 1);
                        bool allVisible = true;
                        while (subBlock.isValid() && subBlock.blockNumber() <= subRegion.endBlockNumber)
                        {
                            if (!subBlock.isVisible())
                            {
                                allVisible = false;
                                break;
                            }
                            subBlock = subBlock.next();
                        }
                        subRegion.folded = !allVisible;
                    }
                }
            }

            document()->markContentsDirty(0, document()->characterCount());
            viewport()->update();
            break;
        }
    }
}

void TEditor::paintEvent(QPaintEvent *event)
{
    // Draw the current-line highlight background BEFORE text rendering.
    // Drawing before the base paint ensures no artifacts: the highlight is painted
    // as a pure background fill within Qt's dirty-region clip, then text renders
    // on top of it. This is completely independent of ExtraSelection, so word-match
    // highlights and Ctrl-hover links are never affected by cursor movement.
    {
        const QTextCursor cursor = textCursor();
        if (preferences.highlightCurrentLine && !cursor.isNull() && !isReadOnly()) {
            QPainter painter(viewport());
            painter.setClipRect(event->rect(), Qt::IntersectClip);
            const QRectF lineRect = blockBoundingGeometry(cursor.block()).translated(contentOffset());
            const qreal width = viewport()->width();
            // Use the exact line bounding rect height to avoid overdraw between lines.
            const QRectF fillRect(0, lineRect.top(), width, lineRect.height());
            painter.fillRect(fillRect, QColor(16, 23, 48));
        }
    }

    // Let the editor draw the actual text on top of the highlight.
    QPlainTextEdit::paintEvent(event);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen linePen(QColor(79, 144, 170, 125)); // Light blue, semi-transparent
    linePen.setWidth(1);
    linePen.setCapStyle(Qt::FlatCap);
    painter.setPen(linePen);

    qreal tabStopDistance = this->tabStopDistance();
    qreal viewWidth = viewport()->width();

    // Calculate the right edge offset (accounting for margins and horizontal scrolling)
    qreal rightOffset = document()->documentMargin() - contentOffset().x();

    QTextBlock block = firstVisibleBlock();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + static_cast<int>(blockBoundingRect(block).height());

    // lambda func to calculate indent level (8 spaces = 1 tab)
    auto getIndentLevel = [](const QString &text) -> int
    {
        int indent = 0;
        int spaces = 0;
        for (QChar c : text)
        {
            if (c == '\t')
            {
                indent++;
                spaces = 0;
            }
            else if (c == ' ')
            {
                spaces++;
                if (spaces == 8)
                {
                    indent++;
                    spaces = 0;
                }
            }
            else
            {
                break;
            }
        }
        return indent;
    };

    // Iterate through all visible blocks
    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible())
        {
            QString text = block.text();
            int indentLevel = 0;

            // Handle empty lines (continue scope lines across them)
            if (text.trimmed().isEmpty())
            {
                int prevIndent = 0;
                int nextIndent = 0;

                // Look back for the previous non-empty line
                QTextBlock prev = block.previous();
                while (prev.isValid() && prev.text().trimmed().isEmpty())
                {
                    prev = prev.previous();
                }
                if (prev.isValid())
                    prevIndent = getIndentLevel(prev.text());

                // Look ahead for the next non-empty line
                QTextBlock next = block.next();
                while (next.isValid() && next.text().trimmed().isEmpty())
                {
                    next = next.next();
                }
                if (next.isValid())
                    nextIndent = getIndentLevel(next.text());

                // Use the minimum of surrounding indents to safely connect/close scopes
                indentLevel = qMin(prevIndent, nextIndent);
            }
            else
            {
                indentLevel = getIndentLevel(text);
            }

            // Draw vertical lines from Right to Left
            // Starting from i = 0 to places the line Under the parent keyword
            for (int i = 0; i < indentLevel; ++i)
            {
                // Calculate X from the right edge, shifting left based on the scope depth
                qreal x = viewWidth - rightOffset - (i * tabStopDistance);

                // Draw the scope line for the current block height
                painter.drawLine(QLineF(x, top, x, bottom));
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
    }
}

/* ---------------------------------- Drag and Drop ---------------------------------- */

void TEditor::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        for (const QUrl &url : event->mimeData()->urls())
        {
            if (url.fileName().endsWith(".alif", Qt::CaseInsensitive) or
                url.fileName().endsWith(".aliflib", Qt::CaseInsensitive) or
                url.fileName().endsWith(".txt", Qt::CaseInsensitive))
            {
                event->acceptProposedAction();
                return;
            }
        }
    }

    if (event->mimeData()->hasText())
    {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void TEditor::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

void TEditor::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        for (const QUrl &url : event->mimeData()->urls())
        {
            if (url.fileName().endsWith(".alif", Qt::CaseInsensitive) or
                url.fileName().endsWith(".aliflib", Qt::CaseInsensitive) or
                url.fileName().endsWith(".txt", Qt::CaseInsensitive))
            {

                QString filePath = url.toLocalFile();
                emit openRequest(filePath);

                event->acceptProposedAction();
                return;
            }
        }
    }

    if (event->mimeData()->hasText())
    {
        QTextCursor dropCursor = cursorForPosition(event->position().toPoint());
        int dropPosition = dropCursor.position();

        if (dropPosition >= textCursor().selectionStart() and dropPosition <= textCursor().selectionEnd())
        {
            event->ignore();
            return;
        }

        QString droppedText = event->mimeData()->text();
        QTextCursor originalCursor = textCursor();

        originalCursor.removeSelectedText();

        if (originalCursor.position() < dropPosition)
        {
            dropPosition -= droppedText.length();
        }

        dropCursor.setPosition(dropPosition);
        dropCursor.insertText(droppedText);

        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

void TEditor::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

/* ---------------------------------- Indentation ---------------------------------- */

void TEditor::curserIndentation()
{
    QTextCursor cursor = textCursor();
    QString lineText = cursor.block().text();
    int cursorPosInLine = cursor.positionInBlock();
    QString currentIndentation = getCurrentLineIndentation(cursor);

    if (cursorPosInLine > 0)
    {
        int checkPos = cursorPosInLine - 1;
        while (checkPos >= 0 and lineText.at(checkPos).isSpace())
        {
            checkPos--;
        }

        if (checkPos >= 0 and lineText.at(checkPos) == ':')
        {
            currentIndentation += "\t";
        }
    }

    cursor.beginEditBlock();
    cursor.insertText("\n" + currentIndentation);
    cursor.endEditBlock();
    setTextCursor(cursor);
}

QString TEditor::getCurrentLineIndentation(const QTextCursor &cursor) const
{
    QTextBlock block = cursor.block();
    if (!block.isValid())
    {
        return QString();
    }

    QString lineText = block.text();
    QString indentation;
    for (const QChar &ch : lineText)
    {
        if (ch == ' ' or ch == '\t')
        {
            indentation += ch;
        }
        else
        {
            break;
        }
    }
    return indentation;
}

void TEditor::setRecoveryCoordinator(RecoveryCoordinator* const coordinator) {
    if (recoveryBinding != nullptr) {
        recoveryBinding->setCoordinator(coordinator);
    }
}

QString TEditor::recoveryDocumentId() const {
    return recoveryBinding != nullptr ? recoveryBinding->documentId() : QString();
}

bool TEditor::hasPendingRecoveryPersistence() const {
    return recoveryBinding != nullptr && recoveryBinding->hasPendingPersistence();
}

bool TEditor::isRecoveryRetryScheduled() const {
    return recoveryBinding != nullptr && recoveryBinding->isRetryScheduled();
}

quint64 TEditor::lastRequestedRecoveryRevision() const {
    return recoveryBinding != nullptr ? recoveryBinding->lastRequestedRevision() : 0;
}

quint64 TEditor::lastPersistedRecoveryRevision() const {
    return recoveryBinding != nullptr ? recoveryBinding->lastPersistedRevision() : 0;
}

quint64 TEditor::currentDirtyRecoveryRevision() const {
    return recoveryBinding != nullptr ? recoveryBinding->currentDirtyRevision() : 0;
}

void TEditor::adoptRecoveryEntry(const RecoveryEntry& entry) {
    if (recoveryBinding != nullptr) {
        recoveryBinding->adoptRecoveryEntry(entry);
    }
}

void TEditor::startAutoSave() {
    if (recoveryBinding != nullptr) {
        recoveryBinding->startAutoSave();
    }
}

void TEditor::stopAutoSave() {
    if (recoveryBinding != nullptr) {
        recoveryBinding->stopAutoSave();
    }
}

void TEditor::scheduleRecoveryCapture() {
    startAutoSave();
}

void TEditor::flushRecoverySnapshot() {
    if (recoveryBinding != nullptr) {
        recoveryBinding->flushSnapshot();
    }
}

void TEditor::performAutoSave() {
    flushRecoverySnapshot();
}

void TEditor::clearRecoverySnapshot() {
    if (recoveryBinding != nullptr) {
        recoveryBinding->clearSnapshot();
    }
}

void TEditor::removeBackupFile() {
    clearRecoverySnapshot();
}

void TEditor::updateHighlighterTheme(std::shared_ptr<SyntaxTheme> theme) {
    if (highlighter != nullptr) {
        highlighter->setTheme(theme);
    }
}

void TEditor::applyPreferences(const EditorPreferences& requestedPreferences) {
    preferences = PreferencesStore::normalize(requestedPreferences);
    updateFontType(preferences.fontFamily);
    updateFontSize(preferences.fontSize);

    setLineWrapMode(preferences.wordWrapEnabled ? QPlainTextEdit::WidgetWidth
                                                : QPlainTextEdit::NoWrap);
    setWordWrapMode(preferences.wordWrapEnabled
                        ? QTextOption::WrapAtWordBoundaryOrAnywhere
                        : QTextOption::NoWrap);
    setHorizontalScrollBarPolicy(preferences.wordWrapEnabled
                                     ? Qt::ScrollBarAlwaysOff
                                     : Qt::ScrollBarAsNeeded);

    if (lineNumberArea != nullptr) {
        lineNumberArea->setVisible(preferences.lineNumbersVisible);
    }
    if (minimap != nullptr) {
        minimap->setVisible(preferences.minimapVisible);
    }
    if (recoveryBinding != nullptr) {
        recoveryBinding->setConfiguration(preferences);
    }
    if (interactionBinding != nullptr) {
        interactionBinding->setHoverDelay(preferences.hoverDelayMilliseconds);
    }
    if (!preferences.hoverInformationEnabled) {
        dismissHover();
    }
    if (!preferences.automaticCompletionEnabled) {
        dismissCompletionPopup();
    }
    if (highlighter != nullptr) {
        highlighter->setDiagnosticsVisible(preferences.inlineDiagnosticsVisible);
    }
    updateLineNumberAreaWidth();
    viewport()->update();
    notifyEditorInformationChanged();
}


// --- autocomplete system ---

void TEditor::setupAutoComplete()
{
    // set autocomplete system
    model = new CompletionModel(this);
    strategies.push_back(std::make_unique<SnippetStrategy>());
    strategies.push_back(std::make_unique<KeywordStrategy>());
    // هذه الأنظمة لا زالت متاحة ولكن تم تعليقها تجهيزا لحذفها
    // او إعادة إستخدامها بحسب ما يقرر لاحقا
    // strategies.push_back(std::make_unique<BuiltinStrategy>());
    // strategies.push_back(std::make_unique<DynamicWordStrategy>());

    QCompleter *completer = new QCompleter(this);
    setCompleter(completer);
}

void TEditor::setCompleter(QCompleter *completer)
{
    if (c)
        disconnect(c, nullptr, this, nullptr);
    c = completer;
    if (!c)
        return;

    c->setWidget(this);
    c->setCompletionMode(QCompleter::PopupCompletion);
    c->setCaseSensitivity(Qt::CaseInsensitive);
    c->setModel(model);

    // Custom Rich Popup ---
    TCompletionPopup *popup = new TCompletionPopup;
    c->setPopup(popup); // QCompleter takes ownership

    popup->setItemDelegate(new TModernCompletionDelegate(popup));

    // set dimensions
    popup->setMinimumWidth(350);
    popup->setMinimumHeight(200); // Taller to fit list + footer

    // To this lambda that captures the type:
    connect(c, QOverload<const QString &>::of(&QCompleter::activated),
            this, [this](const QString &completion)
            {
                // Get the current index from the completer popup
                QModelIndex index = c->popup()->currentIndex();
                if (index.isValid()) {
                    // Get the type from the model
                    CompletionType type = static_cast<CompletionType>(
                        index.data(Qt::UserRole + 2).toInt());
                    // Get the full completion item
                    QString completionText = index.data(Qt::EditRole).toString();
                    insertCompletion(completionText, type);
                } else {
                    // Fallback to just the string without type
                    insertCompletion(completion, CompletionType::DynamicWord);
                } });
}

void TEditor::focusOutEvent(QFocusEvent *e)
{
    dismissCompletionPopup();
    dismissHover();
    clearCtrlHoverDefinitionLink();
    QPlainTextEdit::focusOutEvent(e);
}

void TEditor::keyPressEvent(QKeyEvent *e)
{
    dismissHover();
    if (e->key() == Qt::Key_Control) {
        updateCtrlHoverDefinitionLink(viewport()->mapFromGlobal(QCursor::pos()));
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Alt
        || e->key() == Qt::Key_Meta) {
        // Modifier-only key presses do not change the document and must never
        // trigger automatic completion, especially while search has selected a
        // match in this editor but the search field is being used.
        dismissCompletionPopup();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_F12 && e->modifiers() == Qt::NoModifier) {
        navigateToDefinition(textCursor().position());
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Left && e->modifiers() == Qt::AltModifier) {
        navigateBackFromDefinition();
        e->accept();
        return;
    }

    // handleing Brackets and Quotes
    if (handleAutoPairing(e)) {
        e->accept();
        return;
    }

    // Live-update an already-visible autocomplete popup as the cursor moves.
    if (preferences.automaticCompletionEnabled && c && c->popup()->isVisible()
        && isNavigationKey(e)) {
        // Let the editor move the cursor first
        QPlainTextEdit::keyPressEvent(e);
        // Then immediately trigger completion to update the list based on the new cursor position
        performCompletion();
        return;
    }

    if (c && c->popup()->isVisible())
    {
        switch (e->key())
        {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        case Qt::Key_Escape:
            dismissCompletionPopup();
            e->accept();
            return;
        default:
            break;
        }
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter))
    {
        if (!snippetTargets.isEmpty())
        {
            if (processSnippetNavigation())
            {
                e->accept();
                return;
            }
        }
    }

    if (e->key() == Qt::Key_Tab && !snippetTargets.isEmpty())
    {
        if (processSnippetNavigation())
        {
            e->accept();
            return;
        }
    }

    const bool isShortcut = ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_Space);

    QPlainTextEdit::keyPressEvent(e);

    // Pure cursor-navigation keys never surface or refresh the popup; only text
    // input (or an explicit shortcut) should trigger automatic completion.
    if (isNavigationKey(e)) {
        dismissCompletionPopup();
        return;
    }

    if (!preferences.automaticCompletionEnabled && !isShortcut) {
        dismissCompletionPopup();
        return;
    }

    performCompletion();
}
void TEditor::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Control
        || !e->modifiers().testFlag(Qt::ControlModifier)) {
        clearCtrlHoverDefinitionLink();
    }
    QPlainTextEdit::keyReleaseEvent(e);
}

/// Returns true for keys that only move the cursor without modifying text.
/// Such keys must never trigger or refresh the automatic completion popup;
/// they are handled by the base QPlainTextEdit and dismissed instead.
const bool TEditor::isNavigationKey(const QKeyEvent *e) {
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        return true;
    default:
        return false;
    }
}

void TEditor::performCompletion() {
    const QTextCursor editorCursor = textCursor();

    CompletionContext completionContext = completionContextAt(
        editorCursor.block().text(), editorCursor.positionInBlock(),
        editorCursor.block().position());
    if (!completionContext.isMemberAccess) {
        const QTextCursor wordCursor = textUnderCursor();
        completionContext.prefix = wordCursor.selectedText();
        completionContext.replacementBegin = wordCursor.selectionStart();
        completionContext.replacementEnd = wordCursor.selectionEnd();
    }

    const QString textUnder = completionContext.prefix;
    // Empty prefixes are valid immediately after a receiver dot, e.g. `تويوتا.`.
    if (textUnder.isEmpty() && !completionContext.isMemberAccess) {
        dismissCompletionPopup();
        return;
    }

    std::vector<CompletionItem> allSuggestions;
    const quint64 revision = analysisController ? analysisController->currentRevision() : 0;
    const qsizetype cursorOffset = textCursor().position();
    bool hasCurrentSemanticModel = false;
    if (analysisController && semanticCompletionProvider) {
        const LanguageAnalysisSnapshotPtr snapshot = analysisController->currentSnapshot();
        if (snapshot && snapshot->semantic) {
            hasCurrentSemanticModel = snapshot->revision == revision;
            const QVector<CompletionItem> semanticItems = completionContext.isMemberAccess
                                                              ? semanticCompletionProvider->memberSuggestions(
                                                                    completionContext.receiver, textUnder,
                                                                    completionContext.receiverBegin, cursorOffset, snapshot->semantic,
                                                                    !hasCurrentSemanticModel)
                                                              : semanticCompletionProvider->suggestions(
                                                                    textUnder, cursorOffset, snapshot->semantic, !hasCurrentSemanticModel);
            allSuggestions.insert(allSuggestions.end(), semanticItems.begin(), semanticItems.end());
        }
        if (!hasCurrentSemanticModel) {
            // This pass originated from typing or an explicit completion shortcut.
            // Preserve its context until the requested immutable snapshot arrives;
            // that snapshot may be the first result able to open the popup.
            completionRefreshPending = completionContext.hasReplacementRange();
            analysisController->requestCompletionAnalysis(revision, toPlainText());
        } else {
            completionRefreshPending = false;
        }
    }

    for (const auto& strategy : strategies) {
        if (completionContext.isMemberAccess
            || dynamic_cast<DynamicWordStrategy*>(strategy.get()) != nullptr) {
            continue;
        }
        const QString unusedDocumentText;
        auto res = strategy->getSuggestions(textUnder, unusedDocumentText);
        allSuggestions.insert(allSuggestions.end(), res.begin(), res.end());
    }

    model->updateData(allSuggestions);

    if (allSuggestions.empty())
    {
        if (completionRefreshPending && completionContext.hasReplacementRange()) {
            // Semantic completion is still in flight. Keep the user-requested
            // context, but do not leave stale suggestions visible meanwhile.
            activeCompletionContext = completionContext;
            activeCompletionRevision = revision;
            hasActiveCompletionContext = true;
            if (c && c->popup()) {
                c->popup()->hide();
            }
        } else {
            dismissCompletionPopup();
        }
        return;
    }

    activeCompletionContext = completionContext;
    activeCompletionRevision = revision;
    hasActiveCompletionContext = completionContext.hasReplacementRange();
    c->setCompletionPrefix(textUnder);
    QRect cr = cursorRect();

    QPoint widgetPos = this->viewport()->mapTo(this, cr.topRight());
    cr.moveTo(widgetPos);

    // Calculate popup width: Text width + Scrollbar + Padding
    int popupWidth = std::clamp(35 + 150 + c->popup()->verticalScrollBar()->width() + 65, 295, 355);

    // Shift dialog left ---
    cr.moveLeft(cr.right() - popupWidth - 360);

    // set width
    cr.setWidth(popupWidth);

    c->complete(cr);

    // select first item in the popped up list
    QAbstractItemView *popup = c->popup();
    if (popup and popup->model()->rowCount() > 0)
    {
        popup->setCurrentIndex(popup->model()->index(0, 0));
    }
}
void TEditor::clearActiveCompletionContext() {
    activeCompletionContext = {};
    activeCompletionRevision = 0;
    hasActiveCompletionContext = false;
    completionRefreshPending = false;
}
bool TEditor::canRefreshActiveCompletion() const {
    if (!c || !c->popup() || !completionRefreshPending
        || !hasActiveCompletionContext
        || !activeCompletionContext.hasReplacementRange()) {
        return false;
    }
    const quint64 currentRevision = analysisController ? analysisController->currentRevision() : 0;
    if (activeCompletionRevision != currentRevision) {
        return false;
    }
    const qsizetype documentLength = document()->characterCount() - 1;
    const qsizetype cursorOffset = textCursor().position();
    return activeCompletionContext.replacementBegin >= 0
           && activeCompletionContext.replacementEnd <= documentLength
           && cursorOffset >= activeCompletionContext.replacementBegin
           && cursorOffset <= activeCompletionContext.replacementEnd;
}
void TEditor::dismissCompletionPopup() {
    if (c && c->popup()) {
        c->popup()->hide();
    }
    clearActiveCompletionContext();
}

QTextCursor TEditor::textUnderCursor() const
{
    QTextCursor tc = textCursor();
    tc.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
    return tc;
}

void TEditor::insertCompletion(const QString &completion, CompletionType type)
{
    if (c->widget() != this)
        return;

    QTextCursor tc;
    const bool contextIsCurrent = hasActiveCompletionContext
                                  && activeCompletionRevision == (analysisController ? analysisController->currentRevision() : 0)
                                  && activeCompletionContext.hasReplacementRange()
                                  && activeCompletionContext.replacementEnd <= document()->characterCount() - 1;
    if (contextIsCurrent) {
        tc = textCursor();
        tc.setPosition(activeCompletionContext.replacementBegin);
        tc.setPosition(activeCompletionContext.replacementEnd, QTextCursor::KeepAnchor);
    } else {
        // Never fall back to word navigation for a stale member context: it can
        // select the receiver and its dot. A fresh completion pass is required.
        if (hasActiveCompletionContext && activeCompletionContext.isMemberAccess) {
            clearActiveCompletionContext();
            return;
        }
        tc = textUnderCursor();
    }

    switch (type)
    {
    case CompletionType::Builtin:
        insertBuiltinFunction(completion, tc);
        break;
    case CompletionType::Snippet:
        insertSnippet(completion, tc);
        break;
    case CompletionType::Keyword:
        insertWord(completion, tc);
        break;
    case CompletionType::DynamicWord:
    case CompletionType::SemanticSymbol:
    default:
        insertWord(completion, tc);
        break;
    }
    dismissCompletionPopup();
}

void TEditor::insertWord(const QString &completion, QTextCursor &tc)
{
    tc.insertText(completion);
    setTextCursor(tc);
}
void TEditor::insertBuiltinFunction(const QString &functionName, QTextCursor &tc)
{
    // Select everything from cursor to end of current word
    QTextCursor tempCursor = textCursor();
    tempCursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);

    tc.insertText(functionName);
    tc.insertText("()");

    tc.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);

    // Perform the insertion
    setTextCursor(tc);
}
void TEditor::insertSnippet(const QString &snippet, QTextCursor &tc)
{
    QString textToInsert = snippet;

    // Calculate indentation
    // Get the full text of the current line to determine indentation
    QTextBlock block = tc.block();
    QString lineText = block.text();
    QString baseIndentation{};
    for (const QChar &ch : lineText)
    {
        if (ch.isSpace())
            baseIndentation.append(ch);
        else
            break;
    }

    // Apply indentation to multi-line snippets
    if (textToInsert.contains('\n'))
    {
        QStringList lines = textToInsert.split('\n');
        // Start from index 1 because index 0 is appended to the current line
        // (which already has indentation on the left).
        // Subsequent lines need the base indentation explicitly added.
        for (int i = 1; i < lines.size(); ++i)
        {
            lines[i] = baseIndentation + lines[i];
        }
        textToInsert = lines.join('\n');
    }

    // Perform the insertion
    tc.insertText(textToInsert);
    setTextCursor(tc);

    // Reset snippet targets
    snippetTargets.clear();

    // Setup snippet navigation based on snippet content
    if (snippet.startsWith("دالة"))
    {
        QTextCursor finder = textCursor();
        finder.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, textToInsert.length());
        finder = document()->find("اسم", finder);
        if (!finder.isNull())
            setTextCursor(finder);
        snippetTargets << "معاملات" << "مرر";
    }
    else if (snippet.startsWith("صنف"))
    {
        QTextCursor finder = textCursor();
        finder.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, textToInsert.length());
        finder = document()->find("اسم", finder);
        if (!finder.isNull())
            setTextCursor(finder);
        snippetTargets << "مرر";
    }
    else if (snippet.startsWith("اذا"))
    {
        QTextCursor finder = textCursor();
        finder.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, textToInsert.length());
        finder = document()->find("الشرط", finder);
        if (!finder.isNull())
            setTextCursor(finder);
        snippetTargets << "مرر";
    }
    else if (snippet.startsWith("لكل"))
    {
        QTextCursor finder = textCursor();
        finder.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, textToInsert.length());
        finder = document()->find("عنصر", finder);
        if (!finder.isNull())
            setTextCursor(finder);
        snippetTargets << "العناصر" << "مرر";
    }
    else if (snippet.startsWith("بينما"))
    {
        QTextCursor finder = textCursor();
        finder.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, textToInsert.length());
        finder = document()->find("الشرط", finder);
        if (!finder.isNull())
            setTextCursor(finder);
        snippetTargets << "مرر";
    }
    else if (snippet.startsWith("حاول"))
    {
        QTextCursor finder = textCursor();
        finder.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, textToInsert.length());
        finder = document()->find("مرر", finder);
        if (!finder.isNull())
            setTextCursor(finder);
        snippetTargets << "مرر";
    }
    else if (snippet.startsWith("خطية"))
    {
        QTextCursor finder = textCursor();
        finder.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, textToInsert.length());
        finder = document()->find("معاملات", finder);
        if (!finder.isNull())
            setTextCursor(finder);
        snippetTargets << "مرر";
    }
}

bool TEditor::processSnippetNavigation()
{
    if (snippetTargets.isEmpty())
        return false;
    QString nextTarget = snippetTargets.first();
    QTextCursor tc = textCursor();
    QTextCursor found = document()->find(nextTarget, tc);
    if (!found.isNull())
    {
        setTextCursor(found);
        snippetTargets.removeFirst();
        return true;
    }
    snippetTargets.clear();
    return false;
}

bool TEditor::handleAutoPairing(QKeyEvent *e)
{
    QString text = e->text();

    if (!text.isEmpty())
    {
        QChar typedChar = text.at(0);

        // Handle opening brackets
        if (typedChar == '(' || typedChar == '[' || typedChar == '{')
        {
            QChar closingBracket;
            if (typedChar == '(')
                closingBracket = ')';
            else if (typedChar == '[')
                closingBracket = ']';
            else
                closingBracket = '}';

            return handleBracketCompletion(typedChar, closingBracket);
        }
        // Handle quotes
        else if (typedChar == '\'' || typedChar == '"' || typedChar == '`')
        {
            return handleQuoteCompletion(typedChar);
        }
        // Handle closing brackets (skip over existing ones)
        else if (typedChar == ')' || typedChar == ']' || typedChar == '}' ||
                 typedChar == '\'' || typedChar == '"' || typedChar == '`')
        {
            return handleBracketSkip(typedChar);
        }
    }

    return false;
}

bool TEditor::handleBracketCompletion(QChar openingBracket, QChar closingBracket)
{
    QTextCursor cursor = textCursor();

    // Check if there's a selection
    if (cursor.hasSelection())
    {
        // Wrap selection with brackets
        QString selectedText = cursor.selectedText();
        cursor.insertText(openingBracket + selectedText + closingBracket);

        // Move cursor after the opening bracket to select the original text
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, selectedText.length() + 1);
        cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, selectedText.length());
        setTextCursor(cursor);
    }
    else
    {
        // Insert both brackets and place cursor between them
        cursor.insertText(QString(openingBracket) + closingBracket);
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);
        setTextCursor(cursor);
    }

    return true;
}

bool TEditor::handleQuoteCompletion(QChar quoteChar)
{
    QTextCursor cursor = textCursor();
    QTextDocument *doc = document();

    // Get the character at cursor position
    int pos = cursor.position();

    // Check if there's a selection
    if (cursor.hasSelection())
    {
        // Wrap selection with quotes
        QString selectedText = cursor.selectedText();
        cursor.insertText(quoteChar + selectedText + quoteChar);

        // Move cursor after the opening quote to select the original text
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, selectedText.length() + 1);
        cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, selectedText.length());
        setTextCursor(cursor);
        return true;
    }

    // Check if next character is the same quote (should skip)
    QChar nextChar;
    if (pos < doc->characterCount() - 1)
    {
        nextChar = doc->characterAt(pos);
        if (nextChar == quoteChar)
        {
            // Just move cursor over the existing quote
            cursor.movePosition(QTextCursor::Left);
            setTextCursor(cursor);
            return true;
        }
    }

    // Insert the quote pair
    cursor.insertText(QString(quoteChar) + quoteChar);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);
    setTextCursor(cursor);

    return true;
}

bool TEditor::handleBracketSkip(QChar typedChar)
{
    QTextCursor cursor = textCursor();
    QTextDocument *doc = document();
    int pos = cursor.position();

    // Check if the next character matches the typed closing bracket/quote
    if (pos < doc->characterCount() - 1)
    {
        QChar nextChar = doc->characterAt(pos);
        if (nextChar == typedChar)
        {
            // Just move the cursor over the existing bracket/quote
            cursor.movePosition(QTextCursor::Left);
            setTextCursor(cursor);
            return true;
        }
    }

    return false;
}

void TEditor::highlightSelectedWordMatches()
{
    if (highlightDebounceTimer)
    {
        highlightDebounceTimer->start();
    }
}

void TEditor::startAsyncWordHighlight()
{
    if (highlightSearchInProgress)
    {
        return;
    }

    QTextCursor currentCursor = textCursor();
    QString selectedText = currentCursor.selectedText();
    QString trimmedText = selectedText.trimmed();

    // Cache check
    if (trimmedText == lastHighlightedText &&
        currentCursor.position() == lastHighlightedPosition)
    {
        return;
    }

    // If there is no selection, clear the word highlights but KEEP the line highlight!
    if (trimmedText.isEmpty())
    {
        highlightSearchInProgress = true;

        // Safely clear selections on the main thread
        QMetaObject::invokeMethod(this, [this]() {
            highlightSearchInProgress = false;
            lastHighlightedText.clear();
            lastHighlightedPosition = -1;
            cachedWordMatches.clear();
            highlightSelectedMatches(QList<MatchRange>{}, QString(), -1);
        }, Qt::QueuedConnection);
        return;
    }

    highlightSearchInProgress = true;

    // Capture ONLY thread-safe copies for the background task
    QString docText = toPlainText();
    QString searchText = trimmedText;
    int startPos = currentCursor.position();

    // Launch background search utilizing pure string scanning
    // Correct way for fire-and-forget tasks in Qt 6:
    QThreadPool::globalInstance()->start([this, searchText, docText, startPos]()
                                         {
                                             // 1. Run the search in the background thread
                                             QList<MatchRange> matches = searchWordMatches(searchText, docText);

                                             // 2. Dispatch the results back to the GUI thread
                                             QMetaObject::invokeMethod(this, [this, matches, searchText, startPos]() {
                                                 highlightSelectedMatches(matches, searchText, startPos);
                                             }, Qt::QueuedConnection);
                                         });
}

// RUNS ON BACKGROUND THREAD: Pure functional, uses absolutely NO GUI or QObject elements
QList<TEditor::MatchRange> TEditor::searchWordMatches(
    const QString &searchText,
    const QString &documentText)
{
    QList<MatchRange> matches;
    if (searchText.isEmpty() || documentText.isEmpty()) {
        return matches;
    }

    int searchPos = 0;
    // Qt::CaseInsensitive search mimicking your original setup
    while ((searchPos = documentText.indexOf(searchText, searchPos, Qt::CaseInsensitive)) != -1)
    {
        matches.append(MatchRange{searchPos, static_cast<int>(searchText.length())});
        searchPos += searchText.length();
    }

    return matches;
}

// RUNS ON MAIN GUI THREAD: Translates basic indexes back into Qt cursors.
// The current-line highlight is drawn directly in paintEvent, so this method only
// manages word-match highlights and the Ctrl-hover definition-link underline via
// ExtraSelection. When called with empty matches (Ctrl+hover or mouse-move handlers),
// re-applies cached word highlights to prevent them from being wiped out.
void TEditor::highlightSelectedMatches(const QList<MatchRange> &matches,
                                       const QString &searchText, int startPos) {
    // If called with empty matches but we have cached ones (e.g. Ctrl+hover over a
    // definition or mouse moved away from the word), use the cache to preserve highlights.
    const QList<MatchRange>& effectiveMatches = matches.isEmpty() && !cachedWordMatches.isEmpty()
                                                    ? cachedWordMatches : matches;

    QList<QTextEdit::ExtraSelection> extraSelections;

    // Configure the highlight format
    QTextCharFormat highlightFormat;
    highlightFormat.setBackground(QColor(100, 100, 130, 63));

    // Access to the text document and cursor layout is 100% safe here
    QTextDocument *doc = document();

    for (const auto &match : effectiveMatches)
    {
        QTextEdit::ExtraSelection selection;
        selection.format = highlightFormat;

        // Correctly construct the cursor on the main thread
        selection.cursor = QTextCursor(doc);
        selection.cursor.setPosition(match.start);
        selection.cursor.setPosition(match.start + match.length, QTextCursor::KeepAnchor);

        extraSelections.append(selection);
    }

    if (ctrlHoverDefinitionRange.has_value()) {
        const SourceRange& range = *ctrlHoverDefinitionRange;
        QTextEdit::ExtraSelection linkSelection;
        linkSelection.cursor = QTextCursor(document());
        linkSelection.cursor.setPosition(range.begin.offset);
        linkSelection.cursor.setPosition(range.end.offset, QTextCursor::KeepAnchor);
        const QColor linkBlue(71, 147, 255);
        linkSelection.format.setForeground(linkBlue);
        linkSelection.format.setUnderlineColor(linkBlue);
        linkSelection.format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        extraSelections.append(linkSelection);
    }

    setExtraSelections(extraSelections);

    // Cache the matches so future calls with empty/default args can preserve them.
    cachedWordMatches = effectiveMatches;

    highlightSearchInProgress = false;

    // Update Cache safely
    lastHighlightedText = searchText;
    lastHighlightedPosition = startPos;
}

EditorBreadcrumbContext TEditor::breadcrumbContextAtCursor() const {
    EditorBreadcrumbContext context;
    if (analysisController == nullptr) {
        return context;
    }

    context.revision = analysisController->currentRevision();
    context.cursorOffset = textCursor().position();
    const LanguageAnalysisSnapshotPtr snapshot = analysisController->currentSnapshot();
    if (snapshot != nullptr && snapshot->revision == context.revision
        && snapshot->semantic != nullptr) {
        context.symbolPath = snapshot->semantic->enclosingSymbolPathAt(context.cursorOffset);
    }
    return context;
}

void TEditor::notifyBreadcrumbContextChanged() {
    emit breadcrumbContextChanged(breadcrumbContextAtCursor());
}

void TEditor::notifyEditorInformationChanged() {
    emit editorInformationChanged(informationSnapshot());
}
