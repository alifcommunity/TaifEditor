#include "OutputBuffer.h"

#include <QMutexLocker>

OutputBuffer::OutputBuffer(const Limits limits)
    : m_limits(limits)
{
    m_limits.maximumPendingBytes = qMax<qsizetype>(2, m_limits.maximumPendingBytes);
    m_limits.maximumChunkBytes = qBound<qsizetype>(2, m_limits.maximumChunkBytes,
                                                    m_limits.maximumPendingBytes);
}

void OutputBuffer::append(QString chunk)
{
    if (chunk.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    if (byteCount(chunk) > m_limits.maximumChunkBytes) {
        const qsizetype originalBytes = byteCount(chunk);
        chunk = truncateToBytes(chunk, m_limits.maximumChunkBytes);
        m_droppedBytes += originalBytes - byteCount(chunk);
        m_truncationPending = true;
    }

    while (!m_chunks.isEmpty() && m_pendingBytes + byteCount(chunk) > m_limits.maximumPendingBytes) {
        const QString removed = m_chunks.takeFirst();
        const qsizetype removedBytes = byteCount(removed);
        m_pendingBytes -= removedBytes;
        m_droppedBytes += removedBytes;
        m_truncationPending = true;
    }

    if (m_pendingBytes + byteCount(chunk) > m_limits.maximumPendingBytes) {
        const qsizetype acceptedLimit = m_limits.maximumPendingBytes - m_pendingBytes;
        const qsizetype originalBytes = byteCount(chunk);
        chunk = truncateToBytes(chunk, acceptedLimit);
        m_droppedBytes += originalBytes - byteCount(chunk);
        m_truncationPending = true;
    }

    if (!chunk.isEmpty()) {
        m_pendingBytes += byteCount(chunk);
        m_chunks.append(std::move(chunk));
    }
}

OutputBuffer::DrainResult OutputBuffer::drain()
{
    QMutexLocker locker(&m_mutex);
    DrainResult result;
    result.text = m_chunks.join(QString());
    result.truncated = m_truncationPending;
    result.acceptedBytes = m_pendingBytes;
    result.droppedBytes = m_droppedBytes;
    m_chunks.clear();
    m_pendingBytes = 0;
    m_droppedBytes = 0;
    m_truncationPending = false;
    return result;
}

void OutputBuffer::clear()
{
    QMutexLocker locker(&m_mutex);
    m_chunks.clear();
    m_pendingBytes = 0;
    m_droppedBytes = 0;
    m_truncationPending = false;
}

qsizetype OutputBuffer::pendingBytes() const
{
    QMutexLocker locker(&m_mutex);
    return m_pendingBytes;
}

OutputBuffer::Limits OutputBuffer::limits() const
{
    return m_limits;
}

qsizetype OutputBuffer::byteCount(const QString& text)
{
    return text.size() * static_cast<qsizetype>(sizeof(QChar));
}

QString OutputBuffer::truncateToBytes(const QString& text, const qsizetype byteLimit)
{
    const qsizetype characters = qMax<qsizetype>(0, byteLimit / sizeof(QChar));
    return text.left(characters);
}
