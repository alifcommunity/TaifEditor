#pragma once

#include "SessionStore.h"

#include <QDialog>
#include <optional>

class QListWidget;
class QListWidgetItem;
class QPushButton;

class SessionManagerDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SessionManagerDialog(SessionStore& sessionStore, QWidget* parent = nullptr);

    [[nodiscard]] std::optional<SavedSession> sessionToOpen() const;

signals:
    void sessionsChanged();

private:
    void refreshSessions();
    void refreshButtonState();
    void createSession();
    void editSelectedSession();
    void deleteSelectedSession();
    void acceptSelectedSession();
    [[nodiscard]] std::optional<SavedSession> selectedSession() const;
    [[nodiscard]] bool editSession(SavedSession session, bool isNew);

    SessionStore& m_sessionStore;
    QListWidget* m_sessionsList = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_createButton = nullptr;
    std::optional<SavedSession> m_sessionToOpen;
};
