#include "EditorPreferences.h"

#include <QSettings>

namespace {

constexpr auto kSchemaVersionKey = "settingsSchemaVersion";
constexpr auto kFontSizeKey = "editorFontSize";
constexpr auto kFontFamilyKey = "editorFontType";
constexpr auto kSyntaxThemeKey = "editorCodeTheme";
constexpr auto kTabWidthKey = "editorTabWidth";
constexpr auto kWordWrapKey = "editorWordWrap";
constexpr auto kLineNumbersKey = "editorLineNumbersVisible";
constexpr auto kMinimapKey = "editorMinimapVisible";
constexpr auto kHighlightCurrentLineKey = "editorHighlightCurrentLine";
constexpr auto kAutoSaveEnabledKey = "editorAutoSaveEnabled";
constexpr auto kAutoSaveIntervalKey = "editorAutoSaveIntervalMilliseconds";
constexpr auto kAutomaticCompletionKey = "editorAutomaticCompletionEnabled";
constexpr auto kHoverInformationKey = "editorHoverInformationEnabled";
constexpr auto kHoverDelayKey = "editorHoverDelayMilliseconds";
constexpr auto kInlineDiagnosticsKey = "editorInlineDiagnosticsVisible";
constexpr auto kRecentFilesLimitKey = "recentFilesLimit";

bool readBool(const QSettings& settings, const char* key, const bool fallback)
{
    return settings.value(QLatin1String(key), fallback).toBool();
}

int readInt(const QSettings& settings, const char* key, const int fallback)
{
    bool converted = false;
    const int value = settings.value(QLatin1String(key), fallback).toInt(&converted);
    return converted ? value : fallback;
}

void assignError(QString* const errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

} // namespace

bool EditorPreferences::operator==(const EditorPreferences& other) const
{
    return fontFamily == other.fontFamily
        && fontSize == other.fontSize
        && syntaxThemeIndex == other.syntaxThemeIndex
        && tabWidth == other.tabWidth
        && wordWrapEnabled == other.wordWrapEnabled
        && lineNumbersVisible == other.lineNumbersVisible
        && minimapVisible == other.minimapVisible
        && highlightCurrentLine == other.highlightCurrentLine
        && autoSaveEnabled == other.autoSaveEnabled
        && autoSaveIntervalMilliseconds == other.autoSaveIntervalMilliseconds
        && automaticCompletionEnabled == other.automaticCompletionEnabled
        && hoverInformationEnabled == other.hoverInformationEnabled
        && hoverDelayMilliseconds == other.hoverDelayMilliseconds
        && inlineDiagnosticsVisible == other.inlineDiagnosticsVisible
        && recentFilesLimit == other.recentFilesLimit;
}

EditorPreferences PreferencesStore::defaults()
{
    return {};
}

EditorPreferences PreferencesStore::normalize(EditorPreferences preferences)
{
    const EditorPreferences defaultPreferences = defaults();
    if (preferences.fontFamily.trimmed().isEmpty()) {
        preferences.fontFamily = defaultPreferences.fontFamily;
    }
    preferences.fontSize = qBound(12, preferences.fontSize, 36);
    preferences.syntaxThemeIndex = qBound(0, preferences.syntaxThemeIndex, 3);
    preferences.tabWidth = qBound(2, preferences.tabWidth, 8);
    preferences.autoSaveIntervalMilliseconds = qBound(5000,
                                                       preferences.autoSaveIntervalMilliseconds,
                                                       300000);
    preferences.hoverDelayMilliseconds = qBound(100, preferences.hoverDelayMilliseconds, 1500);
    preferences.recentFilesLimit = qBound(0, preferences.recentFilesLimit, 30);
    return preferences;
}

EditorPreferences PreferencesStore::load()
{
    QSettings settings(QStringLiteral("Alif"), QStringLiteral("Taif"));
    const EditorPreferences defaultPreferences = defaults();
    EditorPreferences preferences;

    // These three keys preserve the current pre-schema settings format.
    preferences.fontFamily = settings.value(QLatin1String(kFontFamilyKey),
                                            defaultPreferences.fontFamily).toString();
    preferences.fontSize = readInt(settings, kFontSizeKey, defaultPreferences.fontSize);
    preferences.syntaxThemeIndex = readInt(settings, kSyntaxThemeKey,
                                            defaultPreferences.syntaxThemeIndex);
    preferences.tabWidth = readInt(settings, kTabWidthKey, defaultPreferences.tabWidth);
    preferences.wordWrapEnabled = readBool(settings, kWordWrapKey,
                                           defaultPreferences.wordWrapEnabled);
    preferences.lineNumbersVisible = readBool(settings, kLineNumbersKey,
                                              defaultPreferences.lineNumbersVisible);
    preferences.minimapVisible = readBool(settings, kMinimapKey,
                                          defaultPreferences.minimapVisible);
    preferences.highlightCurrentLine = readBool(settings, kHighlightCurrentLineKey,
                                                defaultPreferences.highlightCurrentLine);
    preferences.autoSaveEnabled = readBool(settings, kAutoSaveEnabledKey,
                                           defaultPreferences.autoSaveEnabled);
    preferences.autoSaveIntervalMilliseconds = readInt(settings, kAutoSaveIntervalKey,
                                                        defaultPreferences.autoSaveIntervalMilliseconds);
    preferences.automaticCompletionEnabled = readBool(settings, kAutomaticCompletionKey,
                                                       defaultPreferences.automaticCompletionEnabled);
    preferences.hoverInformationEnabled = readBool(settings, kHoverInformationKey,
                                                   defaultPreferences.hoverInformationEnabled);
    preferences.hoverDelayMilliseconds = readInt(settings, kHoverDelayKey,
                                                  defaultPreferences.hoverDelayMilliseconds);
    preferences.inlineDiagnosticsVisible = readBool(settings, kInlineDiagnosticsKey,
                                                    defaultPreferences.inlineDiagnosticsVisible);
    preferences.recentFilesLimit = readInt(settings, kRecentFilesLimitKey,
                                           defaultPreferences.recentFilesLimit);

    return normalize(preferences);
}

bool PreferencesStore::save(const EditorPreferences& requestedPreferences,
                            const bool clearRecentFiles,
                            QString* const errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    const EditorPreferences preferences = normalize(requestedPreferences);
    QSettings settings(QStringLiteral("Alif"), QStringLiteral("Taif"));
    settings.setValue(QLatin1String(kSchemaVersionKey), kCurrentSchemaVersion);
    settings.setValue(QLatin1String(kFontFamilyKey), preferences.fontFamily);
    settings.setValue(QLatin1String(kFontSizeKey), preferences.fontSize);
    settings.setValue(QLatin1String(kSyntaxThemeKey), preferences.syntaxThemeIndex);
    settings.setValue(QLatin1String(kTabWidthKey), preferences.tabWidth);
    settings.setValue(QLatin1String(kWordWrapKey), preferences.wordWrapEnabled);
    settings.setValue(QLatin1String(kLineNumbersKey), preferences.lineNumbersVisible);
    settings.setValue(QLatin1String(kMinimapKey), preferences.minimapVisible);
    settings.setValue(QLatin1String(kHighlightCurrentLineKey), preferences.highlightCurrentLine);
    settings.setValue(QLatin1String(kAutoSaveEnabledKey), preferences.autoSaveEnabled);
    settings.setValue(QLatin1String(kAutoSaveIntervalKey), preferences.autoSaveIntervalMilliseconds);
    settings.setValue(QLatin1String(kAutomaticCompletionKey),
                      preferences.automaticCompletionEnabled);
    settings.setValue(QLatin1String(kHoverInformationKey), preferences.hoverInformationEnabled);
    settings.setValue(QLatin1String(kHoverDelayKey), preferences.hoverDelayMilliseconds);
    settings.setValue(QLatin1String(kInlineDiagnosticsKey), preferences.inlineDiagnosticsVisible);
    settings.setValue(QLatin1String(kRecentFilesLimitKey), preferences.recentFilesLimit);
    if (clearRecentFiles) {
        settings.remove(QStringLiteral("RecentFiles"));
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        assignError(errorMessage, QStringLiteral("تعذر حفظ الإعدادات."));
        return false;
    }
    return true;
}

bool PreferencesStore::clearRecentFiles(QString* const errorMessage)
{
    return save(load(), true, errorMessage);
}
