#pragma once

#include "AlifAnalysis.h"

#include <QAbstractTableModel>
#include <QWidget>

class QLabel;
class QModelIndex;
class QTableView;
class QToolButton;

class DiagnosticsModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column : int {
        CodeColumn,
        LocationColumn,
        MessageColumn,
        SeverityColumn,
        ColumnCount
    };

    explicit DiagnosticsModel(QObject* parent = nullptr);

    void setDiagnostics(QVector<EditorDiagnostic> diagnostics);
    void setSeverityVisibility(bool showErrors, bool showWarnings);
    [[nodiscard]] const EditorDiagnostic* diagnosticAt(const QModelIndex& index) const;
    [[nodiscard]] int errorCount() const;
    [[nodiscard]] int warningCount() const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override;

private:
    void rebuildVisibleRows();

    QVector<EditorDiagnostic> m_diagnostics;
    QVector<qsizetype> m_visibleRows;
    bool m_showErrors = true;
    bool m_showWarnings = true;
};

/**
 * Compact RTL diagnostics surface for the active Taif editor. The model remains
 * UI-neutral and activation emits a source-range value for exact editor routing.
 */
class DiagnosticsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPanel(QWidget* parent = nullptr);

    void setDiagnostics(QVector<EditorDiagnostic> diagnostics);
    void clearDiagnostics();

signals:
    void diagnosticActivated(EditorDiagnostic diagnostic);

private slots:
    void refreshSummary();
    void activateIndex(const QModelIndex& index);

private:
    DiagnosticsModel* m_model{};
    QTableView* m_table{};
    QLabel* m_summary{};
    QLabel* m_emptyState{};
    QToolButton* m_errorFilter{};
    QToolButton* m_warningFilter{};
};
