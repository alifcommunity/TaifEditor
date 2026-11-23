#include "TBreadcrumbBar.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QToolButton>

namespace {

struct SymbolPrefix final {
    QIcon icon{};
    QString prefix{};
};

SymbolPrefix symbolPrefix(const SymbolKind kind)
{
    switch (kind) {
    case SymbolKind::Class:
        return SymbolPrefix{QIcon(":/icons/resources/brackets.svg"), QStringLiteral(" صنف ")};
    case SymbolKind::Function:
        return SymbolPrefix{QIcon(":/icons/resources/parentheses.svg"), QStringLiteral(" دالة ")};
    default:
        return {};
    }
}

QString normalizedPath(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

} // namespace

TBreadcrumbBar::TBreadcrumbBar(QWidget* const parent)
    : QFrame(parent)
    , m_layout(new QHBoxLayout(this))
{
    setObjectName(QStringLiteral("BreadcrumbBar"));
    setFrameShape(QFrame::NoFrame);
    setMinimumHeight(34);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(QStringLiteral(R"(
        QFrame#BreadcrumbBar {
            background-color: #0f172a;
        }
        QToolButton {
            color: #cbd5e1;
            background: transparent;
            border: 1px solid transparent;
            border-radius: 5px;
            padding: 3px 7px;
            font-family: "Noto Kufi Arabic", "Tajawal";
            font-size: 12px;
        }
        QToolButton:hover, QToolButton:focus {
            color: #f8fafc;
            background-color: #16345b;
            border-color: #315e91;
        }
        QLabel#BreadcrumbSeparator {
            color: #5f7898;
            padding: 0 1px;
        }
        QToolButton#BreadcrumbSemanticSegment {
            color: #cbd5ef;
        }
        QToolButton#BreadcrumbSemanticSegment:hover, QToolButton#BreadcrumbSemanticSegment:focus {
            color: #dbeafe;
            background-color: #1e3a5f;
        }
    )"));

    m_layout->setContentsMargins(8, 2, 8, 2);
    m_layout->setSpacing(1);
    rebuild();
}

void TBreadcrumbBar::setFileContext(const QString& filePath)
{
    const QString normalizedFilePath = normalizedPath(filePath);
    if (m_filePath == normalizedFilePath) {
        return;
    }
    m_filePath = normalizedFilePath;
    rebuild();
}

void TBreadcrumbBar::setSemanticContext(const EditorBreadcrumbContext& context)
{
    if (m_semanticContext.revision == context.revision
        && m_semanticContext.cursorOffset == context.cursorOffset
        && m_semanticContext.symbolPath.size() == context.symbolPath.size()) {
        bool equal = true;
        for (int index = 0; index < context.symbolPath.size(); ++index) {
            if (m_semanticContext.symbolPath.at(index).symbol != context.symbolPath.at(index).symbol) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return;
        }
    }
    m_semanticContext = context;
    rebuild();
}

void TBreadcrumbBar::clearSemanticContext()
{
    if (m_semanticContext.symbolPath.isEmpty()) {
        return;
    }
    m_semanticContext.symbolPath.clear();
    rebuild();
}

void TBreadcrumbBar::rebuild()
{
    clearLayout();

    const QVector<FileSegment> filePathSegments = fileSegments();
    bool needsSeparator = false;
    for (int index = 0; index < filePathSegments.size(); ++index) {
        if (needsSeparator) {
            addSeparator();
        }
        addFileSegment(filePathSegments.at(index), index);
        needsSeparator = true;
    }

    for (int index = 0; index < m_semanticContext.symbolPath.size(); ++index) {
        if (needsSeparator) {
            addSeparator();
        }
        addSymbolSegment(m_semanticContext.symbolPath.at(index), index);
        needsSeparator = true;
    }

    m_layout->addStretch(1);
}

void TBreadcrumbBar::clearLayout()
{
    while (QLayoutItem* const item = m_layout->takeAt(0)) {
        if (QWidget* const widget = item->widget()) {
            delete widget;
        }
        delete item;
    }
}

void TBreadcrumbBar::addSeparator()
{
    auto* const separator = new QLabel(QStringLiteral("‹"), this);
    separator->setObjectName(QStringLiteral("BreadcrumbSeparator"));
    separator->setAccessibleName(QStringLiteral("فاصل مسار التنقل"));
    m_layout->addWidget(separator);
}

void TBreadcrumbBar::addFileSegment(const FileSegment& segment, const int index)
{
    auto* const button = new QToolButton(this);
    button->setObjectName(QStringLiteral("BreadcrumbFileSegment%1").arg(index));
    button->setIcon(segment.isDirectory ?
                        QIcon(":/icons/resources/folder.svg")
                        : QIcon(":/icons/resources/file.svg"));
    button->setIconSize(QSize(14, 14));
    button->setText(QStringLiteral(" %1").arg(segment.label));
    button->setToolTip(segment.path.isEmpty() ? segment.label : segment.path);
    button->setAccessibleName(segment.isDirectory
        ? QStringLiteral("مجلد: %1").arg(segment.label)
        : QStringLiteral("ملف: %1").arg(segment.label));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(true);
    button->setMaximumWidth(180);
    connect(button, &QToolButton::clicked, this, [this, path = segment.path]() {
        if (!path.isEmpty()) {
            emit fileSegmentActivated(path);
        }
    });
    m_layout->addWidget(button);
}

void TBreadcrumbBar::addSymbolSegment(const SemanticBreadcrumb& segment, const int index)
{
    auto* const button = new QToolButton(this);
    button->setObjectName(QStringLiteral("BreadcrumbSemanticSegment"));
    button->setProperty("breadcrumbIndex", index);
    button->setIcon(symbolPrefix(segment.kind).icon);
    button->setIconSize(QSize(14, 14));
    button->setText(symbolPrefix(segment.kind).prefix + segment.name);
    button->setToolTip(button->text());
    button->setAccessibleName(QStringLiteral("رمز: %1").arg(button->text()));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(true);
    button->setMaximumWidth(220);
    connect(button, &QToolButton::clicked, this, [this, range = segment.declarationRange]() {
        emit symbolSegmentActivated(range);
    });
    m_layout->addWidget(button);
}

QVector<TBreadcrumbBar::FileSegment> TBreadcrumbBar::fileSegments() const
{
    if (m_filePath.isEmpty()) {
        return {{QStringLiteral("بدون عنوان"), {}, false}};
    }

    const QFileInfo fileInfo(m_filePath);
    const QString parentPath = fileInfo.absolutePath();
    const QString parentLabel = QFileInfo(parentPath).fileName();
    const QString fileLabel = fileInfo.fileName();

    QVector<FileSegment> segments;
    if (!parentLabel.isEmpty()) {
        segments.append({parentLabel, parentPath, true});
    }
    segments.append({fileLabel.isEmpty() ? m_filePath : fileLabel, m_filePath, false});
    return segments;
}
