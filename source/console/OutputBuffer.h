#pragma once

#include <QMutex>
#include <QString>
#include <QStringList>

/**
 * Thread-safe bounded staging buffer for decoded process-output chunks.
 *
 * Producers may enqueue from any thread. The GUI-owned consumer drains a
 * complete batch and renders it append-only. Bounds are measured in UTF-16
 * bytes because QString is the in-process text representation.
 */
class OutputBuffer final {
public:
    class Limits final {
    public:
        Limits() :
            maximumPendingBytes(1024 * 1024),
            maximumChunkBytes(256 * 1024){};

        qsizetype maximumPendingBytes;
        qsizetype maximumChunkBytes;
    };

    struct DrainResult final {
        QString text;
        bool truncated = false;
        qsizetype acceptedBytes = 0;
        qsizetype droppedBytes = 0;
    };

    explicit OutputBuffer(Limits limits = Limits());

    void append(QString chunk);
    [[nodiscard]] DrainResult drain();
    void clear();

    [[nodiscard]] qsizetype pendingBytes() const;
    [[nodiscard]] Limits limits() const;

private:
    [[nodiscard]] static qsizetype byteCount(const QString& text);
    [[nodiscard]] static QString truncateToBytes(const QString& text, qsizetype byteLimit);

    Limits m_limits;
    mutable QMutex m_mutex;
    QStringList m_chunks;
    qsizetype m_pendingBytes = 0;
    qsizetype m_droppedBytes = 0;
    bool m_truncationPending = false;
};
