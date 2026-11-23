#pragma once

#include <QObject>
#include <QPoint>
#include <QTimer>

#include <functional>

/**
 * Non-visual interaction state for hover presentation.
 *
 * The binding owns only timer/debounce and revision-associated pointer state.
 * TEditor remains responsible for widget hit testing, semantic lookup, and
 * popup geometry, preserving all existing RTL tooltip behavior.
 */
class EditorInteractionBinding final : public QObject
{
    Q_OBJECT

public:
    explicit EditorInteractionBinding(QObject* parent = nullptr);
    ~EditorInteractionBinding() override;

    void initialize(std::function<void()> hoverTimeoutHandler, int delayMilliseconds);
    void shutdown();
    void setHoverDelay(int delayMilliseconds);

    void scheduleHover(QPoint viewportPosition, qsizetype offset, quint64 revision);
    void dismissHover();

    [[nodiscard]] bool matches(qsizetype offset, quint64 revision) const;
    [[nodiscard]] QPoint pendingViewportPosition() const;
    [[nodiscard]] qsizetype pendingOffset() const;
    [[nodiscard]] quint64 pendingRevision() const;

private:
    QTimer m_hoverTimer;
    QPoint m_pendingViewportPosition;
    qsizetype m_pendingOffset = -1;
    quint64 m_pendingRevision = 0;
    bool m_initialized = false;
    bool m_shutdown = false;
};
