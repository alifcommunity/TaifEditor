#pragma once

#include <QMetaType>
#include <QString>

/**
 * Typed, user-facing editor preferences. Defaults preserve TaifEditor's
 * established dark/RTL editor behavior while making it configurable.
 */
struct EditorPreferences final {
    QString fontFamily = QStringLiteral("Noto Kufi Arabic");
    int fontSize = 18;
    int syntaxThemeIndex = 0;
    int tabWidth = 8;
    bool wordWrapEnabled = true;
    bool lineNumbersVisible = true;
    bool minimapVisible = true;
    bool highlightCurrentLine = true;
    bool autoSaveEnabled = true;
    int autoSaveIntervalMilliseconds = 60000;
    bool automaticCompletionEnabled = true;
    bool hoverInformationEnabled = true;
    int hoverDelayMilliseconds = 350;
    bool inlineDiagnosticsVisible = false;
    int recentFilesLimit = 10;

    [[nodiscard]] bool operator==(const EditorPreferences& other) const;
    [[nodiscard]] bool operator!=(const EditorPreferences& other) const {
        return !(*this == other);
    }
};

class PreferencesStore final {
public:
    static constexpr int kCurrentSchemaVersion = 1;

    [[nodiscard]] static EditorPreferences defaults();
    [[nodiscard]] static EditorPreferences load();
    [[nodiscard]] static EditorPreferences normalize(EditorPreferences preferences);
    /** Persists normalized preferences and optional draft-local recent-file removal. */
    static bool save(const EditorPreferences& preferences, bool clearRecentFiles = false,
                     QString* errorMessage = nullptr);
    static bool clearRecentFiles(QString* errorMessage = nullptr);

private:
    PreferencesStore() = delete;
};

Q_DECLARE_METATYPE(EditorPreferences)
