#include "TDiagnosticsPanel.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString severityLabel(const SemanticDiagnosticSeverity severity) {
    switch (severity) {
    case SemanticDiagnosticSeverity::Error: return QStringLiteral("● خطأ");
    case SemanticDiagnosticSeverity::Warning: return QStringLiteral("▲ تحذير");
    case SemanticDiagnosticSeverity::Information: return QStringLiteral("معلومة");
    }
    return {};
}

QColor severityColor(const SemanticDiagnosticSeverity severity) {
    switch (severity) {
    case SemanticDiagnosticSeverity::Error: return QColor(240, 100, 100);
    case SemanticDiagnosticSeverity::Warning: return QColor(255, 180, 70);
    case SemanticDiagnosticSeverity::Information: return QColor(148, 163, 184);
    }
    return QColor(148, 163, 184);
}

QString locationLabel(const SourceRange& range) {
    return QStringLiteral("السطر %1، العمود %2")
        .arg(range.begin.line)
        .arg(range.begin.column);
}

} // namespace

DiagnosticsModel::DiagnosticsModel(QObject* parent) : QAbstractTableModel(parent) {
}

void DiagnosticsModel::setDiagnostics(QVector<EditorDiagnostic> diagnostics) {
    beginResetModel();
    m_diagnostics = std::move(diagnostics);
    rebuildVisibleRows();
    endResetModel();
}

void DiagnosticsModel::setSeverityVisibility(const bool showErrors, const bool showWarnings) {
    if (m_showErrors == showErrors && m_showWarnings == showWarnings) {
        return;
    }
    beginResetModel();
    m_showErrors = showErrors;
    m_showWarnings = showWarnings;
    rebuildVisibleRows();
    endResetModel();
}

const EditorDiagnostic* DiagnosticsModel::diagnosticAt(const QModelIndex& index) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size()) {
        return nullptr;
    }
    const qsizetype sourceRow = m_visibleRows.at(index.row());
    return sourceRow >= 0 && sourceRow < m_diagnostics.size()
        ? &m_diagnostics.at(sourceRow) : nullptr;
}

int DiagnosticsModel::errorCount() const {
    return std::count_if(m_diagnostics.cbegin(), m_diagnostics.cend(),
                         [](const EditorDiagnostic& diagnostic) {
                             return diagnostic.severity == SemanticDiagnosticSeverity::Error;
                         });
}

int DiagnosticsModel::warningCount() const {
    return std::count_if(m_diagnostics.cbegin(), m_diagnostics.cend(),
                         [](const EditorDiagnostic& diagnostic) {
                             return diagnostic.severity == SemanticDiagnosticSeverity::Warning;
                         });
}

int DiagnosticsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_visibleRows.size();
}

int DiagnosticsModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DiagnosticsModel::data(const QModelIndex& index, const int role) const {
    const EditorDiagnostic* diagnostic = diagnosticAt(index);
    if (diagnostic == nullptr) {
        return {};
    }
    if (role == Qt::ForegroundRole) {
        if (index.column() == SeverityColumn) {
            return severityColor(diagnostic->severity);
        }
        if (index.column() == CodeColumn) {
            return QColor(148, 163, 184);
        }
        return QColor(226, 232, 240);
    }
    if (role == Qt::TextAlignmentRole) {
        return index.column() == SeverityColumn || index.column() == CodeColumn
            ? QVariant::fromValue(Qt::AlignCenter)
            : QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (role == Qt::ToolTipRole) {
        return QStringLiteral("%1\n%2\n%3")
            .arg(diagnostic->code, diagnostic->message, locationLabel(diagnostic->range));
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case SeverityColumn: return severityLabel(diagnostic->severity);
    case MessageColumn: return diagnostic->message;
    case LocationColumn: return locationLabel(diagnostic->range);
    case CodeColumn: return diagnostic->code;
    default: return {};
    }
}

QVariant DiagnosticsModel::headerData(const int section, const Qt::Orientation orientation,
                                      const int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case SeverityColumn: return QStringLiteral("النوع");
    case MessageColumn: return QStringLiteral("الوصف");
    case LocationColumn: return QStringLiteral("الموقع");
    case CodeColumn: return QStringLiteral("الرمز");
    default: return {};
    }
}

void DiagnosticsModel::rebuildVisibleRows() {
    m_visibleRows.clear();
    m_visibleRows.reserve(m_diagnostics.size());
    for (qsizetype index = 0; index < m_diagnostics.size(); ++index) {
        const SemanticDiagnosticSeverity severity = m_diagnostics.at(index).severity;
        if ((severity == SemanticDiagnosticSeverity::Error && m_showErrors)
            || (severity == SemanticDiagnosticSeverity::Warning && m_showWarnings)) {
            m_visibleRows.append(index);
        }
    }
}

DiagnosticsPanel::DiagnosticsPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("DiagnosticsPanel"));
    setMinimumHeight(150);
    setStyleSheet(R"(
        #DiagnosticsPanel { background-color: #0f172a; color: #e2e8f0; }
        QLabel { font-family: 'Tajawal', 'Noto Kufi Arabic'; }
        QToolButton { background-color: #1e293b; color: #cbd5e1; border: 1px solid #334155;
        border-radius: 5px; padding: 4px 8px; font-family: 'Tajawal', 'Noto Kufi Arabic'; }
        QToolButton:checked { background-color: #253b5f; border-color: #4793ff; color: #f1f5f9; }
        QToolButton:hover { border-color: #4793ff; }
        QTableView { background-color: #111c31; alternate-background-color: #16233b;
        color: #e2e8f0; border: 1px solid #334155; gridline-color: #26354c;
        selection-background-color: #253b5f; selection-color: #f8fafc;
        font-family: 'Tajawal', 'Noto Kufi Arabic'; }
        QHeaderView {
            background-color: #1e293b;
        }
        QHeaderView::section {
            background-color: #1e293b;
            color: #94a3b8;
            border: none;
            border-bottom: 1px solid #334155;
            padding: 6px;
            font-family: 'Tajawal', 'Noto Kufi Arabic';
        }
)");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(7);

    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(7);
    auto* title = new QLabel(QStringLiteral("المشكلات"), this);
    title->setStyleSheet("QLabel { color: #f1f5f9; font-size: 14px; font-weight: 700; }");
    header->addWidget(title);

    m_summary = new QLabel(this);
    m_summary->setStyleSheet("QLabel { color: #94a3b8; }");
    header->addWidget(m_summary);
    header->addStretch(1);

    m_errorFilter = new QToolButton(this);
    m_errorFilter->setCheckable(true);
    m_errorFilter->setChecked(true);
    header->addWidget(m_errorFilter);
    m_warningFilter = new QToolButton(this);
    m_warningFilter->setCheckable(true);
    m_warningFilter->setChecked(true);
    header->addWidget(m_warningFilter);
    layout->addLayout(header);

    m_model = new DiagnosticsModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(DiagnosticsModel::SeverityColumn,
                                                       QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(DiagnosticsModel::MessageColumn,
                                                       QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(DiagnosticsModel::LocationColumn,
                                                       QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(DiagnosticsModel::CodeColumn,
                                                       QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    m_emptyState = new QLabel(QStringLiteral("لا توجد أخطاء أو تحذيرات في المستند الحالي"), this);
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet("QLabel { color: #94a3b8; padding: 14px; }");
    layout->addWidget(m_emptyState);

    connect(m_errorFilter, &QToolButton::toggled, this, [this](const bool) {
        m_model->setSeverityVisibility(m_errorFilter->isChecked(), m_warningFilter->isChecked());
        refreshSummary();
    });
    connect(m_warningFilter, &QToolButton::toggled, this, [this](const bool) {
        m_model->setSeverityVisibility(m_errorFilter->isChecked(), m_warningFilter->isChecked());
        refreshSummary();
    });
    connect(m_table, &QTableView::activated, this, &DiagnosticsPanel::activateIndex);
    connect(m_table, &QTableView::doubleClicked, this, &DiagnosticsPanel::activateIndex);

    refreshSummary();
}

void DiagnosticsPanel::setDiagnostics(QVector<EditorDiagnostic> diagnostics) {
    m_model->setDiagnostics(std::move(diagnostics));
    refreshSummary();
}

void DiagnosticsPanel::clearDiagnostics() {
    setDiagnostics({});
}

void DiagnosticsPanel::refreshSummary() {
    const int errors = m_model->errorCount();
    const int warnings = m_model->warningCount();
    m_errorFilter->setText(QStringLiteral("● أخطاء %1").arg(errors));
    m_warningFilter->setText(QStringLiteral("▲ تحذيرات %1").arg(warnings));
    m_summary->setText(QStringLiteral("%1 مشكلة").arg(errors + warnings));
    const bool hasRows = m_model->rowCount() > 0;
    m_table->setVisible(hasRows);
    m_emptyState->setVisible(!hasRows);
}

void DiagnosticsPanel::activateIndex(const QModelIndex& index) {
    if (const EditorDiagnostic* diagnostic = m_model->diagnosticAt(index)) {
        emit diagnosticActivated(*diagnostic);
    }
}
