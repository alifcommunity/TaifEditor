#pragma once

#include <QMetaType>
#include <QString>

/**
 * Immutable presentation state for the editor information bar.
 *
 * The editor owns the source state while the status-bar presenter owns all
 * formatting and layout decisions. Keeping this structure widget-free prevents
 * the main window from reaching into analysis or recovery implementation details.
 */
struct EditorStatusSnapshot final {
    enum class AnalysisState : quint8 {
        Unavailable,
        Pending,
        Ready,
        LargeDocument
    };

    enum class RecoveryState : quint8 {
        Clean,
        PendingPersistence,
        RetryScheduled
    };

    enum class LineEnding : quint8 {
        Unknown,
        Lf,
        Crlf,
        Mixed
    };

    bool hasEditor = false;
    QString documentName;
    QString documentPath;
    bool modified = false;

    int line = 0;
    int column = 0;
    qsizetype selectedCharacters = 0;
    qsizetype selectedLines = 0;
    qsizetype documentLines = 0;
    qsizetype documentCharacters = 0;

    QString encoding = QStringLiteral("UTF-8");
    LineEnding lineEnding = LineEnding::Unknown;
    bool usesSpaces = true;
    int indentationWidth = 4;

    int errorCount = 0;
    int warningCount = 0;
    AnalysisState analysisState = AnalysisState::Unavailable;
    qint64 analysisDurationMilliseconds = 0;
    qsizetype analysisSnapshotCharacters = 0;
    qsizetype analysisTokenCount = 0;
    quint64 analysisRevision = 0;

    RecoveryState recoveryState = RecoveryState::Clean;
    qint64 recoveryWriteDurationMilliseconds = 0;
};

Q_DECLARE_METATYPE(EditorStatusSnapshot)
