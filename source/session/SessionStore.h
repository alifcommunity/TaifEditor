#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

class QSettings;

/** A durable, explicit set of files that can be restored into one editor window. */
struct SavedSession final {
    QString id;
    QString displayName;
    QStringList filePaths;
    QString activeFilePath;
    QDateTime updatedAt;
};

class SessionStore final {
public:
    class SettingsScope final {
    public:
        SettingsScope() :
            organization(QStringLiteral("Alif")),
            application(QStringLiteral("Taif")) {};

        QString organization{};
        QString application{};
        /** Optional INI path used by isolated tests; empty selects normal user settings. */
        QString fileName{};
    };

    explicit SessionStore(SettingsScope scope = SettingsScope());

    /**
     * Loads the versioned atomic repository. If it is absent, legacy QSettings
     * sessions are validated, returned, and migrated without deleting the
     * legacy source. A corrupt primary repository falls back to its last-good
     * backup before reporting a read error.
     */
    [[nodiscard]] QVector<SavedSession> loadAll(QString* errorMessage = nullptr) const;
    bool saveAll(const QVector<SavedSession>& sessions, QString* errorMessage = nullptr) const;

    /** Exposed for diagnostics and isolated tests; not a UI-facing setting. */
    [[nodiscard]] QString repositoryFilePath() const;
    [[nodiscard]] QString backupRepositoryFilePath() const;

    bool create(SavedSession session, QString* errorMessage = nullptr) const;
    bool update(SavedSession session, QString* errorMessage = nullptr) const;
    bool remove(const QString& id, QString* errorMessage = nullptr) const;

    [[nodiscard]] static QString normalizePath(const QString& path);
    [[nodiscard]] static SavedSession normalize(SavedSession session);
    [[nodiscard]] static bool isDisplayNameAvailable(const QVector<SavedSession>& sessions,
                                                     const QString& displayName,
                                                     const QString& excludedId = {});

private:
    [[nodiscard]] QVector<SavedSession> loadLegacySessions() const;
    [[nodiscard]] bool readRepository(const QString& path, QVector<SavedSession>* sessions,
                                      QString* errorMessage) const;
    [[nodiscard]] bool writeRepositoryAtomically(const QVector<SavedSession>& sessions,
                                                  QString* errorMessage) const;
    [[nodiscard]] static bool validateAndNormalize(const QVector<SavedSession>& source,
                                                   QVector<SavedSession>* normalized,
                                                   QString* errorMessage);
    [[nodiscard]] QString settingsGroup() const;
    [[nodiscard]] QSettings makeSettings() const;

    SettingsScope scope;
};
