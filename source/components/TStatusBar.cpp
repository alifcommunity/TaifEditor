#include "TStatusBar.h"

#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QResizeEvent>
#include <QStyle>
#include <QToolButton>

namespace {

constexpr int kCompactWidth = 780;
constexpr int kDenseWidth = 1040;

QString localizedNumber(const qsizetype value)
{
    return QString::number(value);
}

QString decodeProcessOutput(const QByteArray& bytes)
{
#if defined(Q_OS_WIN)
    return QString::fromLocal8Bit(bytes);
#else
    return QString::fromUtf8(bytes);
#endif
}

} // namespace

TStatusBar::TStatusBar(QWidget* const parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("StatusBar"));
    setAccessibleName(QStringLiteral("شريط معلومات المحرر"));
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    setLayoutDirection(Qt::LeftToRight);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(10, 2, 10, 2);
    m_layout->setSpacing(12);

    m_interpreterSegment = createSegment(QStringLiteral("InfoInterpreterSegment"), m_interpreterLabel);
    m_diagnosticsSegment = new QWidget(this);
    m_diagnosticsSegment->setObjectName(QStringLiteral("InfoDiagnosticsSegment"));
    auto* const diagnosticsLayout = new QHBoxLayout(m_diagnosticsSegment);
    diagnosticsLayout->setContentsMargins(2, 1, 2, 1);
    diagnosticsLayout->setSpacing(0);
    m_diagnosticsButton = new QToolButton(m_diagnosticsSegment);
    m_diagnosticsButton->setObjectName(QStringLiteral("InformationDiagnosticsButton"));
    m_diagnosticsButton->setAccessibleName(QStringLiteral("ملخص المشكلات"));
    m_diagnosticsButton->setCursor(Qt::PointingHandCursor);
    diagnosticsLayout->addWidget(m_diagnosticsButton);
    connect(m_diagnosticsButton, &QToolButton::clicked, this, &TStatusBar::diagnosticsActivated);

    m_analysisSegment = createSegment(QStringLiteral("InfoAnalysisSegment"), m_analysisLabel);
    m_recoverySegment = createSegment(QStringLiteral("InfoRecoverySegment"), m_recoveryLabel);
    m_selectionSegment = createSegment(QStringLiteral("InfoSelectionSegment"), m_selectionLabel);
    m_cursorSegment = createSegment(QStringLiteral("InfoCursorSegment"), m_cursorLabel);
    m_formatSegment = createSegment(QStringLiteral("InfoFormatSegment"), m_formatLabel);

    m_layout->addWidget(m_interpreterSegment);
    m_layout->addWidget(m_diagnosticsSegment);
    m_layout->addWidget(m_analysisSegment);
    m_layout->addWidget(m_recoverySegment);
    m_layout->addWidget(m_selectionSegment);
    m_layout->addWidget(m_cursorSegment);
    m_layout->addWidget(m_formatSegment);
    m_layout->addStretch(1);

    setStyleSheet(QStringLiteral(R"(
        QWidget#StatusBar,
        QWidget#StatusBar QLabel,
        QWidget#StatusBar QToolButton {
            font-family: "Tajawal", "Noto Kufi Arabic";
            font-size: 13px;
        }
        QWidget#StatusBar {
            background-color: #0f172a;
            border-top: 1px solid #1e293b;
            color: #94a3b8;
        }
        QWidget#InfoInterpreterSegment, QWidget#InfoDiagnosticsSegment,
        QWidget#InfoAnalysisSegment, QWidget#InfoRecoverySegment,
        QWidget#InfoSelectionSegment, QWidget#InfoCursorSegment,
        QWidget#InfoFormatSegment {
            background: transparent;
            border: none;
        }
        QLabel {
            background: transparent;
            color: #cbd5e1;
            padding: 1px 2px;
        }
        QLabel#InfoAnalysisSegmentLabel[attention="true"],
        QLabel#InfoRecoverySegmentLabel[attention="true"] {
            color: #fbbf24;
        }
        QToolButton#InformationDiagnosticsButton {
            border: none;
            background: transparent;
            color: #93c5fd;
            padding: 1px 2px;
        }
        QToolButton#InformationDiagnosticsButton:hover {
            background-color: #172554;
            color: #e0f2fe;
            border-radius: 3px;
        }
        QToolButton#InformationDiagnosticsButton[hasErrors="true"] {
            color: #fca5a5;
        }
        QToolButton#InformationDiagnosticsButton[hasWarnings="true"] {
            color: #fcd34d;
        }
    )"));

    refreshPresentation();
    requestInterpreterInfo();
}

void TStatusBar::setSnapshot(const EditorStatusSnapshot& snapshot) {
    m_snapshot = snapshot;
    refreshPresentation();
}

EditorStatusSnapshot TStatusBar::snapshot() const {
    return m_snapshot;
}

void TStatusBar::resizeEvent(QResizeEvent* const event) {
    QWidget::resizeEvent(event);
    applyResponsiveVisibility();
}

QWidget* TStatusBar::createSegment(const QString& objectName, QLabel*& label) {
    auto* const segment = new QWidget(this);
    segment->setObjectName(objectName);
    auto* const layout = new QHBoxLayout(segment);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setSpacing(0);
    label = new QLabel(segment);
    label->setObjectName(objectName + QStringLiteral("Label"));
    layout->addWidget(label);
    return segment;
}

void TStatusBar::requestInterpreterInfo() {
    // QProcess inherits the editor's launch directory, which is commonly the
    // user's home directory when a desktop entry starts the editor. Resolve
    // the bundled interpreter from the installed application directory instead
    // of relying on that unrelated current working directory.
#if defined(Q_OS_WIN)
    const QString interpreterName = QStringLiteral("alif/alif.exe");
#else
    const QString interpreterName = QStringLiteral("alif/alif");
#endif
    const QString program = QDir(QCoreApplication::applicationDirPath())
                                .filePath(interpreterName);
    const QString command = QStringLiteral("%1 -ن").arg(program);
    auto* const process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(process, &QProcess::errorOccurred, this,
            [this, process, command](const QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) {
                    return;
                }
                m_interpreterInfo = QStringLiteral("مفسر ألف: غير متاح");
                m_interpreterToolTip = QStringLiteral("تعذر تشغيل %1: %2")
                                           .arg(command, process->errorString());
                refreshPresentation();
                process->deleteLater();
            });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, command](const int exitCode, const QProcess::ExitStatus exitStatus) {
                const QString output = decodeProcessOutput(process->readAllStandardOutput()).simplified();
                const QString error = decodeProcessOutput(process->readAllStandardError()).simplified();
                if (exitStatus == QProcess::NormalExit && exitCode == 0 && !output.isEmpty()) {
                    m_interpreterInfo = output;
                    m_interpreterToolTip = output;
                } else {
                    m_interpreterInfo = QStringLiteral("مفسر ألف: غير متاح");
                    m_interpreterToolTip = error.isEmpty()
                                               ? QStringLiteral("فشل %1 برمز الخروج %2.").arg(command).arg(exitCode)
                                               : error;
                }
                refreshPresentation();
                process->deleteLater();
            });
    process->start(program, {QStringLiteral("-ن")});
}


void TStatusBar::refreshPresentation() {
    m_interpreterLabel->setText(m_interpreterInfo);
    m_interpreterLabel->setToolTip(m_interpreterToolTip);
    if (!m_snapshot.hasEditor) {
        m_diagnosticsButton->setText(QStringLiteral("المشكلات: —"));
        m_diagnosticsButton->setToolTip(QStringLiteral("افتح ملفاً لعرض المشكلات."));
        m_analysisLabel->setText(QStringLiteral("التحليل: —"));
        m_recoveryLabel->setText(QStringLiteral("الحفظ: —"));
        m_selectionLabel->setText(QStringLiteral("المستند: —"));
        m_cursorLabel->setText(QStringLiteral("السطر: —"));
        m_formatLabel->setText(QStringLiteral("UTF-8"));
        applyResponsiveVisibility();
        return;
    }

    m_diagnosticsButton->setText(QStringLiteral("أخطاء %1 · تحذيرات %2")
        .arg(m_snapshot.errorCount).arg(m_snapshot.warningCount));
    m_diagnosticsButton->setToolTip(QStringLiteral("%1 خطأ، %2 تحذير. انقر لفتح المشكلات.")
        .arg(m_snapshot.errorCount).arg(m_snapshot.warningCount));
    m_diagnosticsButton->setProperty("hasErrors", m_snapshot.errorCount > 0);
    m_diagnosticsButton->setProperty("hasWarnings", m_snapshot.errorCount == 0 && m_snapshot.warningCount > 0);
    m_diagnosticsButton->style()->unpolish(m_diagnosticsButton);
    m_diagnosticsButton->style()->polish(m_diagnosticsButton);

    m_analysisLabel->setText(analysisText(m_snapshot));
    m_analysisLabel->setToolTip(QStringLiteral("التحليل: %1 مللي ثانية، %2 رمز، لقطة %3 حرف")
        .arg(m_snapshot.analysisDurationMilliseconds)
        .arg(localizedNumber(m_snapshot.analysisTokenCount))
        .arg(localizedNumber(m_snapshot.analysisSnapshotCharacters)));
    const bool analysisNeedsAttention = m_snapshot.analysisState == EditorStatusSnapshot::AnalysisState::Pending
        || m_snapshot.analysisState == EditorStatusSnapshot::AnalysisState::LargeDocument;
    m_analysisLabel->setProperty("attention", analysisNeedsAttention);
    m_analysisLabel->style()->unpolish(m_analysisLabel);
    m_analysisLabel->style()->polish(m_analysisLabel);

    m_recoveryLabel->setText(recoveryText(m_snapshot));
    m_recoveryLabel->setToolTip(QStringLiteral("آخر كتابة للاستعادة: %1 مللي ثانية")
        .arg(m_snapshot.recoveryWriteDurationMilliseconds));
    const bool recoveryNeedsAttention = m_snapshot.recoveryState != EditorStatusSnapshot::RecoveryState::Clean;
    m_recoveryLabel->setProperty("attention", recoveryNeedsAttention);
    m_recoveryLabel->style()->unpolish(m_recoveryLabel);
    m_recoveryLabel->style()->polish(m_recoveryLabel);

    if (m_snapshot.selectedCharacters > 0) {
        m_selectionLabel->setText(QStringLiteral("تحديد: %1 حرف · %2 سطر")
            .arg(compactCount(m_snapshot.selectedCharacters))
            .arg(compactCount(m_snapshot.selectedLines)));
        m_selectionLabel->setToolTip(QStringLiteral("تحديد %1 حرف عبر %2 سطر")
            .arg(localizedNumber(m_snapshot.selectedCharacters))
            .arg(localizedNumber(m_snapshot.selectedLines)));
    } else {
        m_selectionLabel->setText(QStringLiteral("%1 سطر · %2 حرف")
            .arg(compactCount(m_snapshot.documentLines))
            .arg(compactCount(m_snapshot.documentCharacters)));
        m_selectionLabel->setToolTip(QStringLiteral("المستند: %1 سطر، %2 حرف")
            .arg(localizedNumber(m_snapshot.documentLines))
            .arg(localizedNumber(m_snapshot.documentCharacters)));
    }

    m_cursorLabel->setText(QStringLiteral("سطر %1 · عمود %2")
        .arg(m_snapshot.line).arg(m_snapshot.column));
    m_cursorLabel->setToolTip(QStringLiteral("موضع المؤشر: السطر %1، العمود %2")
        .arg(m_snapshot.line).arg(m_snapshot.column));

    m_formatLabel->setText(QStringLiteral("%1 · %2 · %3 %4")
        .arg(m_snapshot.encoding, lineEndingText(m_snapshot.lineEnding),
             m_snapshot.usesSpaces ? QStringLiteral("مسافات") : QStringLiteral("مسافات_طويلة"),
             QString::number(m_snapshot.indentationWidth)));
    m_formatLabel->setToolTip(QStringLiteral("الترميز %1، نهاية السطر %2، الإزاحة %3 بعرض %4")
        .arg(m_snapshot.encoding, lineEndingText(m_snapshot.lineEnding),
             m_snapshot.usesSpaces ? QStringLiteral("مسافات") : QStringLiteral("مسافات_طويلة"),
             QString::number(m_snapshot.indentationWidth)));

    applyResponsiveVisibility();
}

void TStatusBar::applyResponsiveVisibility() {
    const int availableWidth = width();
    const bool compact = availableWidth > 0 && availableWidth < kCompactWidth;
    const bool dense = availableWidth > 0 && availableWidth < kDenseWidth;

    m_interpreterSegment->setVisible(true);
    m_analysisSegment->setVisible(!dense);
    m_recoverySegment->setVisible(!dense);
    m_selectionSegment->setVisible(!compact);
    m_diagnosticsSegment->setVisible(true);
    m_cursorSegment->setVisible(true);
    m_formatSegment->setVisible(true);
}

QString TStatusBar::compactCount(const qsizetype value) {
    if (value >= 1000000) {
        return QString::number(static_cast<double>(value) / 1000000.0, 'f', 1) + QStringLiteral("مليون");
    }
    if (value >= 1000) {
        return QString::number(static_cast<double>(value) / 1000.0, 'f', 1) + QStringLiteral("ألف");
    }
    return localizedNumber(value);
}

QString TStatusBar::lineEndingText(const EditorStatusSnapshot::LineEnding lineEnding) {
    switch (lineEnding) {
    case EditorStatusSnapshot::LineEnding::Lf: return QStringLiteral("LF");
    case EditorStatusSnapshot::LineEnding::Crlf: return QStringLiteral("CRLF");
    case EditorStatusSnapshot::LineEnding::Mixed: return QStringLiteral("Mixed EOL");
    case EditorStatusSnapshot::LineEnding::Unknown: return QStringLiteral("EOL");
    }
    return QStringLiteral("EOL");
}

QString TStatusBar::analysisText(const EditorStatusSnapshot& snapshot)
{
    switch (snapshot.analysisState) {
    case EditorStatusSnapshot::AnalysisState::Pending:
        return QStringLiteral("التحليل: جاري");
    case EditorStatusSnapshot::AnalysisState::Ready:
        return QStringLiteral("تحليل %1ms").arg(snapshot.analysisDurationMilliseconds);
    case EditorStatusSnapshot::AnalysisState::LargeDocument:
        return QStringLiteral("تحليل: مستند كبير");
    case EditorStatusSnapshot::AnalysisState::Unavailable:
        return QStringLiteral("التحليل: —");
    }
    return QStringLiteral("التحليل: —");
}

QString TStatusBar::recoveryText(const EditorStatusSnapshot& snapshot)
{
    switch (snapshot.recoveryState) {
    case EditorStatusSnapshot::RecoveryState::PendingPersistence:
        return QStringLiteral("الحفظ: جارٍ");
    case EditorStatusSnapshot::RecoveryState::RetryScheduled:
        return QStringLiteral("الحفظ: إعادة محاولة");
    case EditorStatusSnapshot::RecoveryState::Clean:
        return QStringLiteral("الحفظ: محفوظ");
    }
    return QStringLiteral("الحفظ: —");
}
