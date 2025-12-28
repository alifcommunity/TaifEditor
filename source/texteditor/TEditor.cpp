#include "TEditor.h"
#include "THighlighter.h"

#include <QPainter>
#include <QTextBlock>
#include <QScrollBar>
#include <QMimeData>
#include <QSettings>
#include <QPainterPath>
#include <QStack>
#include <QMenu>
#include <QAction>
#include <QFile>


// TMinimap::TMinimap(TEditor *editor) : QWidget(editor), editor(editor)
// {
//     setStyleSheet("background-color: #1e1e1e; border-left: 1px solid #333;");
//     setFixedWidth(100);
// }

// QSize TMinimap::sizeHint() const {
//     return QSize(100, 0);
// }


// void TMinimap::paintEvent(QPaintEvent *event)
// {
//     Q_UNUSED(event);
//     QPainter painter(this);

//     painter.fillRect(rect(), QColor("#1e1e1e"));

//     if (!editor) return;

//     QFont font = editor->font();

//     qreal scale = 0.15;
//     painter.save();
//     painter.scale(scale, scale);

//     painter.setLayoutDirection(Qt::LeftToRight);
//     painter.setPen(QColor("#a0a0a0"));


//     QTextBlock block = editor->document()->firstBlock();
//     qreal currentY = 0;

//     while (block.isValid()) {
//         QString text = block.text();

//         if (!text.isEmpty()) {

//             painter.drawText(QPointF(0, currentY + block.layout()->boundingRect().height()), text);
//         }

//         currentY += block.layout()->boundingRect().height();

//         block = block.next();
//     }

//     painter.restore();

//     int scrollMax = editor->verticalScrollBar()->maximum();
//     int scrollVal = editor->verticalScrollBar()->value();
//     int pageStep  = editor->verticalScrollBar()->pageStep();

//     long long totalScrollableHeight = scrollMax + pageStep;
//     if (totalScrollableHeight <= 0) totalScrollableHeight = 1;

//     double viewRatio = (double)pageStep / totalScrollableHeight;
//     double posRatio = (double)scrollVal / totalScrollableHeight;

//     if (scrollMax == 0) { viewRatio = 1.0; posRatio = 0.0; }

//     int mapHeight = height();
//     int highlightY = mapHeight * posRatio;
//     int highlightH = mapHeight * viewRatio;

//     if (highlightH < 15) highlightH = 15;
//     if (highlightY + highlightH > mapHeight) highlightY = mapHeight - highlightH;

//     // رسم المستطيل
//     painter.setPen(Qt::NoPen);
//     painter.setBrush(QColor(255, 255, 255, 30));
//     painter.drawRect(0, highlightY, width(), highlightH);
// }

// void TMinimap::mousePressEvent(QMouseEvent *event) {
//     scrollEditorTo(event->pos());
// }

// void TMinimap::mouseMoveEvent(QMouseEvent *event) {
//     if (event->buttons() & Qt::LeftButton) {
//         scrollEditorTo(event->pos());
//     }
// }

// void TMinimap::scrollEditorTo(const QPoint &pos) {
//     double ratio = (double)pos.y() / height();
//     int maxVal = editor->verticalScrollBar()->maximum();
//     editor->verticalScrollBar()->setValue(maxVal * ratio);
// }



TEditor::TEditor(QWidget* parent) {
    setAcceptDrops(true);
    this->setStyleSheet("QPlainTextEdit { background-color: #141520; color: #cccccc; }");
    this->setTabStopDistance(32);

    QTextDocument* editorDocument = this->document();
    QTextOption option = editorDocument->defaultTextOption();
    option.setTextDirection(Qt::RightToLeft);
    option.setAlignment(Qt::AlignRight);
    editorDocument->setDefaultTextOption(option);


    highlighter = new THighlighter(editorDocument);
    autoComplete = new AutoComplete(this, parent);
    lineNumberArea = new LineNumberArea(this);

    connect(this, &TEditor::blockCountChanged, this, &TEditor::updateLineNumberAreaWidth);
    connect(this, &TEditor::updateRequest, this, &TEditor::updateLineNumberArea);
    connect(this, &TEditor::cursorPositionChanged, this, &TEditor::highlightCurrentLine);
    connect(this->document(), &QTextDocument::contentsChanged, this, &TEditor::updateFoldRegions);

    // minimap = new TMinimap(this);

    // connect(this->document(), &QTextDocument::contentsChanged, this, &TEditor::updateMinimap);
    // connect(this->verticalScrollBar(), &QScrollBar::valueChanged, this, &TEditor::updateMinimap);

    updateLineNumberAreaWidth();
    highlightCurrentLine();

    QSettings settingsVal("Alif", "Taif");
    int savedSize = settingsVal.value("editorFontSize").toInt();
    updateFontSize(savedSize);

    autoSaveTimer = new QTimer(this);
    autoSaveTimer->setInterval(60000);
    connect(autoSaveTimer, &QTimer::timeout, this, &TEditor::performAutoSave);

    connect(this->document(), &QTextDocument::contentsChanged, this, &TEditor::startAutoSave);

    highlighter = new THighlighter(this->document());
    installEventFilter(this);
}

void TEditor::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta == 0) return;

        QFont font = this->font();
        qreal currentSize = font.pointSizeF();

        qreal step = 0.5;

        if (delta > 0) {
            currentSize += step;
        } else {
            currentSize -= step;
        }

        if (currentSize < 5.0) currentSize = 5.0;
        if (currentSize > 50) currentSize = 50;

        font.setPointSizeF(currentSize);
        this->setFont(font);

        if (lineNumberArea) {
            QFont lineFont = lineNumberArea->font();
            lineFont.setPointSizeF(currentSize);
            lineNumberArea->setFont(lineFont);
        }

        updateLineNumberAreaWidth();

        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

// void TEditor::updateMinimap() {
//     if (minimap) minimap->update();
// }

void TEditor::updateFontSize(int size) {
    if (size < 10) {
        size = 16;
    }

    QFont font = this->font();
    font.setPointSize(size);
    this->setFont(font);

    QFont fontNums = lineNumberArea->font();
    fontNums.setPointSize(size);
    lineNumberArea->setFont(fontNums);
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

    if (cursor.atBlockStart() && endBlock > startBlock) {
        endBlock--;
    }

    bool shouldComment = false;

    QTextBlock block = document()->findBlockByNumber(startBlock);
    if (!block.text().trimmed().startsWith("#")) {
        shouldComment = true;
    }

    for (int i = startBlock; i <= endBlock; ++i) {
        block = document()->findBlockByNumber(i);
        QTextCursor lineCursor(block);

        if (shouldComment) {
            lineCursor.movePosition(QTextCursor::StartOfBlock);
            lineCursor.insertText("#");
        } else {
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

    if (!prevBlock.isValid()) return;

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

    if (!nextBlock.isValid()) return;

    cursor.beginEditBlock();

    QString currentText = currentBlock.text();
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    if (cursor.atBlockStart()) cursor.deleteChar();

    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertText("\n" + currentText);

    setTextCursor(cursor);
    cursor.endEditBlock();
}

bool TEditor::eventFilter(QObject* obj, QEvent* event) {
    if (obj == this and event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (autoComplete->isPopupVisible()) {
            if (keyEvent->key() == Qt::Key_Return
                or keyEvent->key() == Qt::Key_Enter) {
                return false;
            }
        }
        if (keyEvent->key() == Qt::Key_Return
             or keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
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

    QAction *commentAction = new QAction("تعليق/إلغاء تعليق", this);
    commentAction->setShortcut(QKeySequence("Ctrl+/"));
    connect(commentAction, &QAction::triggered, this, &TEditor::toggleComment);
    menu->addAction(commentAction);

    QAction *duplicateAction = new QAction("تكرار السطر", this);
    duplicateAction->setShortcut(QKeySequence("Ctrl+D"));
    connect(duplicateAction, &QAction::triggered, this, &TEditor::duplicateLine);
    menu->addAction(duplicateAction);


    menu->setStyleSheet(
        "QMenu { background-color: #252526; color: #cccccc; border: 1px solid #454545; }"
        "QMenu::item { padding: 5px 20px; background-color: transparent; }"
        "QMenu::item:selected { background-color: #094771; color: #ffffff; }"
        "QMenu::separator { height: 1px; background: #454545; margin: 5px 0; }"
        );

    menu->exec(event->globalPos());

    delete menu;
}

int TEditor::lineNumberAreaWidth() const {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    int space = 30 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;

    return space;
}


void TEditor::updateLineNumberAreaWidth() {
    int numsWidth = lineNumberAreaWidth();

    int mapWidth = 0;
    // if (minimap && minimap->isVisible()) {
    //     mapWidth = minimap->width();
    // }


    setViewportMargins(mapWidth, 0, numsWidth, 0);
}

inline void TEditor::updateLineNumberArea(const QRect &rect, int dy) {
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void TEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);

    QRect cr = contentsRect();
    int numsWidth = lineNumberAreaWidth();


    lineNumberArea->setGeometry(this->width() - numsWidth, cr.top(), numsWidth, cr.height());

    // if (minimap) {
        // int mapWidth = minimap->width();
        // minimap->setGeometry(25, cr.top(), mapWidth, cr.height());
    // }
}

void TEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {

    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), Qt::transparent);

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);

            painter.setPen(QColor(200, 200, 200));

            painter.drawText(12, top, lineNumberArea->width(), fontMetrics().height(),
                                     Qt::AlignRight | Qt::AlignVCenter, number);

            bool hasFold = false;
            for (const auto& region : foldRegions) {
                if (region.startBlockNumber == blockNumber) {
                    hasFold = true;
                    bool folded = region.folded;

                    QPolygon arrow;
                    int midY = top + fontMetrics().height() / 2;
                    if (folded) {
                        arrow << QPoint(lineNumberArea->width() - 10, midY - 4)
                        << QPoint(lineNumberArea->width() - 2, midY)
                        << QPoint(lineNumberArea->width() - 10, midY + 4);
                    } else {
                        arrow << QPoint(lineNumberArea->width() - 10, midY - 4)
                        << QPoint(lineNumberArea->width() - 2, midY - 4)
                        << QPoint(lineNumberArea->width() - 6, midY + 4);
                    }

                    painter.setBrush(QColor("#10a8f4"));
                    painter.setPen(Qt::NoPen);
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

void TEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;

        QColor lineColor = QColor(23, 24, 36, 240);

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

void TEditor::updateFoldRegions() {

    QHash<int, bool> previousFoldStates;
    for (const FoldRegion& region : foldRegions) {
        previousFoldStates[region.startBlockNumber] = region.folded;
    }

    foldRegions.clear();
    QStack<int> braceStack;

    QTextBlock block = document()->firstBlock();
    while (block.isValid()) {
        QString text = block.text();

        QString trimmed = text.trimmed();
        if (trimmed.startsWith("دالة ") || trimmed.startsWith("صنف ")) {
            int start = block.blockNumber();

            int startIndent = 0;
            for (QChar c : text) {
                if (c == '\t') startIndent += 4;
                else if (c == ' ') startIndent += 1;
                else break;
            }

            QTextBlock next = block.next();
            int end = start;

            while (next.isValid()) {
                QString nextText = next.text();
                QString nextTrim = nextText.trimmed();

                if (nextTrim.isEmpty()) {
                    next = next.next();
                    continue;
                }

                int nextIndent = 0;
                for (QChar c : nextText) {
                    if (c == '\t') nextIndent += 4;
                    else if (c == ' ') nextIndent += 1;
                    else break;
                }

                if (nextTrim.startsWith("دالة ") || nextTrim.startsWith("صنف ")) {
                    if (nextIndent <= startIndent)
                        break;
                }

                if (nextIndent <= startIndent)
                    break;

                end = next.blockNumber();
                next = next.next();
            }

            if (end > start) {
                FoldRegion region{};
                region.startBlockNumber = start;
                region.endBlockNumber = end;
                region.folded = false;
                if (previousFoldStates.contains(region.startBlockNumber))
                    region.folded = previousFoldStates[region.startBlockNumber];
                foldRegions.append(region);
            }
        }
        block = block.next();
    }

    if (lineNumberArea)
        lineNumberArea->update();

    for (const FoldRegion& region : foldRegions) {
        QTextBlock block = document()->findBlockByNumber(region.startBlockNumber + 1);
        while (block.isValid() && block.blockNumber() <= region.endBlockNumber) {
            block.setVisible(!region.folded);
            block = block.next();
        }
    }
    document()->markContentsDirty(0, document()->characterCount());
    viewport()->update();
}

void TEditor::toggleFold(int blockNumber) {
    for (FoldRegion &region : foldRegions) {
        if (region.startBlockNumber == blockNumber) {
            region.folded = !region.folded;

            QTextBlock block = document()->findBlockByNumber(region.startBlockNumber + 1);
            while (block.isValid() && block.blockNumber() <= region.endBlockNumber) {
                block.setVisible(!region.folded);
                block = block.next();
            }

            if (!region.folded) {
                for (FoldRegion &subRegion : foldRegions) {
                    if (subRegion.startBlockNumber > region.startBlockNumber &&
                        subRegion.endBlockNumber <= region.endBlockNumber) {
                        QTextBlock subBlock = document()->findBlockByNumber(subRegion.startBlockNumber + 1);
                        bool allVisible = true;
                        while (subBlock.isValid() && subBlock.blockNumber() <= subRegion.endBlockNumber) {
                            if (!subBlock.isVisible()) {
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


/* ---------------------------------- Drag and Drop ---------------------------------- */

void TEditor::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.fileName().endsWith(".alif", Qt::CaseInsensitive) or
                url.fileName().endsWith(".aliflib", Qt::CaseInsensitive) or
                url.fileName().endsWith(".txt", Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }

    if (event->mimeData()->hasText()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void TEditor::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void TEditor::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.fileName().endsWith(".alif", Qt::CaseInsensitive) or
                url.fileName().endsWith(".aliflib", Qt::CaseInsensitive) or
                url.fileName().endsWith(".txt", Qt::CaseInsensitive)) {

                QString filePath = url.toLocalFile();
                emit openRequest(filePath);

                event->acceptProposedAction();
                return;
            }
        }
    }

    if (event->mimeData()->hasText()) {
        QTextCursor dropCursor = cursorForPosition(event->position().toPoint());
        int dropPosition = dropCursor.position();

        if (dropPosition >= textCursor().selectionStart()
            and dropPosition <= textCursor().selectionEnd()) {
            event->ignore();
            return;
        }

        QString droppedText = event->mimeData()->text();
        QTextCursor originalCursor = textCursor();

        originalCursor.removeSelectedText();

        if (originalCursor.position() < dropPosition) {
            dropPosition -= droppedText.length();
        }

        dropCursor.setPosition(dropPosition);
        dropCursor.insertText(droppedText);

        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

void TEditor::dragLeaveEvent(QDragLeaveEvent* event) {
    event->accept();
}


/* ---------------------------------- Indentation ---------------------------------- */

void TEditor::curserIndentation() {
    QTextCursor cursor = textCursor();
    QString lineText = cursor.block().text();
    int cursorPosInLine = cursor.positionInBlock();
    QString currentIndentation = getCurrentLineIndentation(cursor);

    if (cursorPosInLine > 0) {
        int checkPos = cursorPosInLine - 1;
        while (checkPos >= 0 and lineText.at(checkPos).isSpace()) {
            checkPos--;
        }

        if (checkPos >= 0 and lineText.at(checkPos) == ':') {
            currentIndentation += "\t";
        }
    }

    cursor.beginEditBlock();
    cursor.insertText("\n" + currentIndentation);
    cursor.endEditBlock();
    setTextCursor(cursor);
}

QString TEditor::getCurrentLineIndentation(const QTextCursor &cursor) const {
    QTextBlock block = cursor.block();
    if (!block.isValid()) {
        return QString();
    }

    QString lineText = block.text();
    QString indentation;
    for (const QChar &ch : lineText) {
        if (ch == ' ' or ch == '\t') {
            indentation += ch;
        } else {
            break;
        }
    }
    return indentation;
}




void TEditor::startAutoSave() {
    if (!autoSaveTimer->isActive()) {
        autoSaveTimer->start();
    }
}

void TEditor::stopAutoSave() {
    autoSaveTimer->stop();
}

void TEditor::performAutoSave() {
    QString filePath = this->property("filePath").toString();
    if (filePath.isEmpty() || !this->document()->isModified()) return;

    QString backupPath = filePath + ".~";

    QFile file(backupPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << this->toPlainText();
        file.close();
    }
}

void TEditor::removeBackupFile() {
    QString filePath = this->property("filePath").toString();
    if (filePath.isEmpty()) return;

    QString backupPath = filePath + ".~";
    if (QFile::exists(backupPath)) {
        QFile::remove(backupPath);
    }
    stopAutoSave();
}
