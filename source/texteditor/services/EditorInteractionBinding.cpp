#include "EditorInteractionBinding.h"

EditorInteractionBinding::EditorInteractionBinding(QObject* const parent)
    : QObject(parent)
{
    m_hoverTimer.setParent(this);
    m_hoverTimer.setSingleShot(true);
}

EditorInteractionBinding::~EditorInteractionBinding()
{
    shutdown();
}

void EditorInteractionBinding::initialize(std::function<void()> hoverTimeoutHandler,
                                          const int delayMilliseconds)
{
    if (m_initialized || m_shutdown) {
        return;
    }

    m_initialized = true;
    m_hoverTimer.setInterval(qMax(1, delayMilliseconds));
    connect(&m_hoverTimer, &QTimer::timeout, this,
            [handler = std::move(hoverTimeoutHandler)]() {
                if (handler) {
                    handler();
                }
            });
}

void EditorInteractionBinding::shutdown()
{
    if (m_shutdown) {
        return;
    }

    m_shutdown = true;
    dismissHover();
}

void EditorInteractionBinding::setHoverDelay(const int delayMilliseconds)
{
    m_hoverTimer.setInterval(qMax(1, delayMilliseconds));
}

void EditorInteractionBinding::scheduleHover(const QPoint viewportPosition,
                                             const qsizetype offset,
                                             const quint64 revision)
{
    if (!m_initialized || m_shutdown) {
        return;
    }

    m_pendingViewportPosition = viewportPosition;
    m_pendingOffset = offset;
    m_pendingRevision = revision;
    m_hoverTimer.start();
}

void EditorInteractionBinding::dismissHover()
{
    m_hoverTimer.stop();
    m_pendingOffset = -1;
    m_pendingRevision = 0;
    m_pendingViewportPosition = {};
}

bool EditorInteractionBinding::matches(const qsizetype offset, const quint64 revision) const
{
    return m_pendingOffset == offset && m_pendingRevision == revision;
}

QPoint EditorInteractionBinding::pendingViewportPosition() const
{
    return m_pendingViewportPosition;
}

qsizetype EditorInteractionBinding::pendingOffset() const
{
    return m_pendingOffset;
}

quint64 EditorInteractionBinding::pendingRevision() const
{
    return m_pendingRevision;
}
