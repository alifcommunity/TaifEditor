#pragma once

#include "EditorStatusSnapshot.h"

#include <QWidget>

class QLabel;
class QToolButton;
class QHBoxLayout;

/**
 * Compact RTL presenter for the active editor's immutable information snapshot.
 * Technical values retain LTR directionality inside the Arabic status-bar shell.
 */
class TStatusBar final : public QWidget {
    Q_OBJECT
public:
    explicit TStatusBar(QWidget* parent = nullptr);

    void setSnapshot(const EditorStatusSnapshot& snapshot);
    [[nodiscard]] EditorStatusSnapshot snapshot() const;

signals:
    void diagnosticsActivated();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    [[nodiscard]] QWidget* createSegment(const QString& objectName, QLabel*& label);
    void requestInterpreterInfo();
    void refreshPresentation();
    void applyResponsiveVisibility();
    [[nodiscard]] static QString compactCount(qsizetype value);
    [[nodiscard]] static QString lineEndingText(EditorStatusSnapshot::LineEnding lineEnding);
    [[nodiscard]] static QString analysisText(const EditorStatusSnapshot& snapshot);
    [[nodiscard]] static QString recoveryText(const EditorStatusSnapshot& snapshot);

    EditorStatusSnapshot m_snapshot;
    QHBoxLayout* m_layout = nullptr;
    QWidget* m_interpreterSegment = nullptr;
    QWidget* m_diagnosticsSegment = nullptr;
    QWidget* m_analysisSegment = nullptr;
    QWidget* m_recoverySegment = nullptr;
    QWidget* m_selectionSegment = nullptr;
    QWidget* m_cursorSegment = nullptr;
    QWidget* m_formatSegment = nullptr;
    QLabel* m_interpreterLabel = nullptr;
    QToolButton* m_diagnosticsButton = nullptr;
    QLabel* m_analysisLabel = nullptr;
    QLabel* m_recoveryLabel = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QLabel* m_cursorLabel = nullptr;
    QLabel* m_formatLabel = nullptr;
    QString m_interpreterInfo = QStringLiteral("مفسر ألف: يتم البحث");
    QString m_interpreterToolTip;
};
