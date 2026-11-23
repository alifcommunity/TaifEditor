#include "TMinimap.h"
#include "TEditor.h"
#include "TaifBootstrap.h"

#include <QPainter>
#include <QTextBlock>
#include <QScrollBar>
#include <QApplication>
#include <QScreen>
#include <algorithm> // std::clamp

TPreviewTooltip::TPreviewTooltip(QWidget* parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground); // to ensure fully rounded rectangle of preview tooltip
    font = QFont();
    font = QFont(TaifBootstrap::notoKufiFontFamily());
    font.insertSubstitution("Arial", "Courier New");
    font.setPixelSize(10);
    setFont(font);
}

void TPreviewTooltip::setContent(const QVector<QPair<int, QString>>& linesContent) {
    lines = linesContent;
    QFontMetrics fm(font);
    lineSpacing = fm.lineSpacing();

    numberWidth = 45;
    textWidth = 300; // we need fixed width of text to prevent the widget from change it's width

    for (const auto& line : lines) {
        // line number width
        int numW = fm.horizontalAdvance(QString::number(line.first)) + 10;
        numberWidth = std::max(numberWidth, numW);
    }

    updateGeometry();
    update();
}

QSize TPreviewTooltip::sizeHint() const {
    if (lines.isEmpty()) return QSize(0, 0);

    int h = lines.size() * lineSpacing + (padding * 2);
    int w = numberWidth + textWidth + (padding * 2);
    return QSize(w, h);
}

void TPreviewTooltip::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRect r = rect();
    // this ensure not clipped at the edges
    QRectF drawingRect = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
    int cornerRadius = 8;
    painter.setBrush(QColor(30, 32, 46));
    painter.setPen(QPen(QColor(0, 122, 204), 1));
    painter.drawRoundedRect(drawingRect, cornerRadius, cornerRadius);

    if (lines.isEmpty()) return;

    painter.setFont(font);
    int y = padding;

    for (auto& line : lines) {
        QRect numRect(width() - padding - numberWidth, y, numberWidth - 10, lineSpacing);
        QRect textRect(padding, y, width() - numberWidth - padding, lineSpacing);

        painter.setPen(QColor(100, 100, 100));
        painter.drawText(numRect, Qt::AlignVCenter, QString::number(line.first));

        painter.setPen(QColor(200, 200, 200));
        painter.drawText(textRect, Qt::AlignVCenter, line.second);

        y += lineSpacing;
    }
}


// =======================================================
// TMinimap Implementation
// =======================================================

TMinimap::TMinimap(TEditor *editor, QWidget *parent)
    : QWidget(parent), editor(editor)
{
    setMouseTracking(true);

    previewTooltip = new TPreviewTooltip(this);
    previewTooltip->hide();

    previewTimer = new QTimer(this);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(7);

    connect(previewTimer, &QTimer::timeout, this, [this]() {
        if (isHovering && !isDragging) {
            showPreviewTooltip(mapFromGlobal(QCursor::pos()));
        }
    });
}

void TMinimap::updateMinimap() {
    update();
}

void TMinimap::computeLayout(int& outVisibleBlockCount, double& outYRatio,
                             double& outSliderHeight, double& outSliderY) const
{
    outVisibleBlockCount = 0;

    // Fast counting of visible blocks
    for (QTextBlock b = editor->document()->firstBlock(); b.isValid(); b = b.next()) {
        if (b.isVisible()) outVisibleBlockCount++;
    }

    if (outVisibleBlockCount == 0) {
        outYRatio = 1.0;
        outSliderHeight = 0.0;
        outSliderY = 0.0;
        return;
    }

    const double theoreticalHeight = outVisibleBlockCount * MINIMAP_LINE_HEIGHT;
    outYRatio = theoreticalHeight > height() ? static_cast<double>(height()) / theoreticalHeight : 1.0;

    int linesInEditor = 1;
    if (editor->fontMetrics().lineSpacing() > 0) {
        linesInEditor = editor->viewport()->height() / editor->fontMetrics().lineSpacing();
    }

    outSliderHeight = std::max(10.0, linesInEditor * MINIMAP_LINE_HEIGHT * outYRatio);

    const double contentHeight = theoreticalHeight * outYRatio;
    const double maxSliderY = std::max(0.0, contentHeight - outSliderHeight);

    const int maxScroll = editor->verticalScrollBar()->maximum();
    const int currentScroll = editor->verticalScrollBar()->value();

    outSliderY = (maxScroll > 0) ? (static_cast<double>(currentScroll) / maxScroll) * maxSliderY : 0.0;
}

void TMinimap::scrollTo(int y) {
    QScrollBar* vBar = editor->verticalScrollBar();
    if (vBar->maximum() <= 0) return;

    int visibleBlockCount;
    double yRatio, sliderHeight, sliderY;
    computeLayout(visibleBlockCount, yRatio, sliderHeight, sliderY);

    if (visibleBlockCount == 0) return;

    double targetY = static_cast<double>(y) - clickOffset;

    const double contentHeight = visibleBlockCount * MINIMAP_LINE_HEIGHT * yRatio;
    const double maxSliderY = std::max(1.0, contentHeight - sliderHeight);

    targetY = std::clamp(targetY, 0.0, maxSliderY);

    const double ratio = targetY / maxSliderY;
    vBar->setValue(static_cast<int>(ratio * vBar->maximum()));
}

void TMinimap::enterEvent(QEnterEvent* event) {
    Q_UNUSED(event);
    isHovering = true;
    setCursor(Qt::PointingHandCursor);
}

void TMinimap::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        setCursor(Qt::ClosedHandCursor);
        previewTimer->stop();
        hidePreviewTooltip();

        int visibleBlockCount;
        double yRatio, sliderHeight, sliderY;
        computeLayout(visibleBlockCount, yRatio, sliderHeight, sliderY);

        const double mouseY = event->pos().y();

        if (mouseY >= sliderY && mouseY <= sliderY + sliderHeight) {
            clickOffset = mouseY - sliderY;
        } else {
            clickOffset = sliderHeight / 2.0;
            scrollTo(mouseY);
        }

        isDragging = true;
    }
}

void TMinimap::mouseMoveEvent(QMouseEvent* event) {
    if (isDragging) {
        scrollTo(event->pos().y());
    } else {
        isHovering = true;
        previewTimer->start();
    }
}

void TMinimap::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        isDragging = false;
        setCursor(Qt::PointingHandCursor);
        if (isHovering) {
            previewTimer->start();
        }
    }
}

void TMinimap::leaveEvent(QEvent* event) {
    Q_UNUSED(event);
    isHovering = false;
    previewTimer->stop();
    hidePreviewTooltip();
    unsetCursor();
}

void TMinimap::wheelEvent(QWheelEvent* event) {
    // Pass wheel scroll events to the editor so user can scroll while hovering over minimap
    QApplication::sendEvent(editor->verticalScrollBar(), event);
}

void TMinimap::showPreviewTooltip(const QPoint& pos) {
    if (pos.y() < 0 || pos.y() >= height()) {
        hidePreviewTooltip();
        return;
    }

    int visibleBlockCount;
    double yRatio, sliderHeight, sliderY;
    computeLayout(visibleBlockCount, yRatio, sliderHeight, sliderY);

    if (visibleBlockCount == 0 || yRatio <= 0.0) return;

    const int targetVisibleIndex = static_cast<int>(pos.y() / (MINIMAP_LINE_HEIGHT * yRatio));

    int currentVisibleIndex = 0;
    int targetBlockNumber = -1;

    for (QTextBlock b = editor->document()->firstBlock(); b.isValid(); b = b.next()) {
        if (b.isVisible()) {
            if (currentVisibleIndex == targetVisibleIndex) {
                targetBlockNumber = b.blockNumber();
                break;
            }
            currentVisibleIndex++;
        }
    }

    if (targetBlockNumber < 0) {
        hidePreviewTooltip();
        return;
    }

    const int CONTEXT_LINES = 2;
    const int startBlock = std::max(0, targetBlockNumber - CONTEXT_LINES);
    const int endBlock = std::min(editor->document()->blockCount() - 1, targetBlockNumber + CONTEXT_LINES);

    QVector<QPair<int, QString>> linesData;
    linesData.reserve(endBlock - startBlock + 1);

    for (int i = startBlock; i <= endBlock; ++i) {
        QTextBlock block = editor->document()->findBlockByNumber(i);
        if (!block.isValid()) continue;

        QString line = block.text();
        line.replace("\t", "    ");
        if (line.length() > 60) {
            line.truncate(57);
            line.append("...");
        }

        linesData.append({i + 1, line});
    }

    previewTooltip->setContent(linesData);
    previewTooltip->adjustSize();

    QPoint globalPos = mapToGlobal(QPoint(width() + 5, pos.y() - previewTooltip->height() / 2));
    const QRect screenRect = QGuiApplication::screenAt(globalPos)
                                 ? QGuiApplication::screenAt(globalPos)->availableGeometry()
                                 : QGuiApplication::primaryScreen()->availableGeometry();

    // Prevent horizontal clipping (if it goes off the right edge of the screen, snap it inside)
    if (globalPos.x() + previewTooltip->width() > screenRect.right()) {
        globalPos.setX(mapToGlobal(QPoint(0, 0)).x() - previewTooltip->width() - 5);
    }

    // Prevent vertical clipping
    globalPos.setY(std::clamp(globalPos.y(), screenRect.top(), screenRect.bottom() - previewTooltip->height()));

    previewTooltip->move(globalPos);
    previewTooltip->show();
}

void TMinimap::hidePreviewTooltip() {
    if (previewTooltip && previewTooltip->isVisible()) {
        previewTooltip->hide();
    }
}

void TMinimap::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(event->rect(), QColor("transparent"));

    if (editor->document()->blockCount() == 0) return;

    int visibleBlockCount;
    double yRatio, sliderHeight, sliderY;
    computeLayout(visibleBlockCount, yRatio, sliderHeight, sliderY);

    if (visibleBlockCount == 0) return;

    double currentY = 3.0; // offset the minimap content -3px in y
    const int widgetHeight = height();
    const double charWidth = 1.2;
    const QColor defaultColor(200, 200, 200);
    const double fixedIdent = 4.0;

    // Setting Qt::NoPen removes stroke computation overhead for drawing small rects
    painter.setPen(Qt::NoPen);

    QTextBlock block = editor->document()->firstBlock();

    while(block.isValid()) {
        // Early Exit. If we've drawn past the widget bottom, stop processing completely.
        if (currentY > widgetHeight) break;

        if (block.isVisible()) {
            const QString text = block.text();
            const int textLen = text.length();

            // Skip fast if line is empty
            if (textLen > 0) {
                int leadingSpaces = 0;
                while (leadingSpaces < textLen && text.at(leadingSpaces).isSpace()) {
                    leadingSpaces++;
                }

                if (leadingSpaces < textLen) {
                    double indentOffset = leadingSpaces * fixedIdent;
                    double currentX = (width() - 4.0) - indentOffset;

                    QVector<QTextLayout::FormatRange> formats = block.layout()->formats();
                    std::sort(formats.begin(), formats.end(), [](const QTextLayout::FormatRange& a, const QTextLayout::FormatRange& b) {
                        return a.start < b.start;
                    });

                    int stringIdx = leadingSpaces;
                    for (const auto& f : formats) {
                        if (currentX <= 4.0 || stringIdx >= textLen) break;
                        if (f.start + f.length <= stringIdx) continue;

                        if (f.start > stringIdx) {
                            const int gapLen = f.start - stringIdx;
                            double rectW = gapLen * charWidth;
                            rectW = (currentX - rectW < 4.0) ? (currentX - 4.0) : rectW;

                            if (rectW > 0) {
                                painter.fillRect(QRectF(currentX - rectW, currentY, rectW, DOT_HEIGHT), defaultColor);
                                currentX -= rectW;
                            }
                            stringIdx = f.start;
                        }

                        int formatDrawLen = f.length - std::max(0, stringIdx - f.start);
                        if (formatDrawLen > 0) {
                            QColor color = f.format.foreground().color();
                            if (!color.isValid()) color = defaultColor;

                            double rectW = formatDrawLen * charWidth;
                            rectW = (currentX - rectW < 4.0) ? (currentX - 4.0) : rectW;

                            if (rectW > 0) {
                                painter.fillRect(QRectF(currentX - rectW, currentY, rectW, DOT_HEIGHT), color);
                                currentX -= rectW;
                            }
                            stringIdx += formatDrawLen;
                        }
                    }

                    if (currentX > 4.0 && stringIdx < textLen) {
                        const int remain = textLen - stringIdx;
                        double rectW = remain * charWidth;
                        rectW = (currentX - rectW < 4.0) ? (currentX - 4.0) : rectW;

                        if (rectW > 0) {
                            painter.fillRect(QRectF(currentX - rectW, currentY, rectW, DOT_HEIGHT), defaultColor);
                        }
                    }
                }
            }
            currentY += (MINIMAP_LINE_HEIGHT * yRatio);
        }
        block = block.next();
    }

    // Draw the active viewport slider
    painter.fillRect(QRectF(1, sliderY + 1, width() - 2, sliderHeight), QColor(56, 186, 255, 25));
    painter.setPen(QPen(QColor(56, 186, 255, 75), 1));
    painter.drawRect(QRectF(1, sliderY + 1, width() - 2, sliderHeight));
}