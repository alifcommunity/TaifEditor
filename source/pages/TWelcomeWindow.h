#pragma once

#include "SessionStore.h"

#include <QMainWindow>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QStackedLayout>


class WelcomeWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit WelcomeWindow(QWidget* parent = nullptr,
                           SessionStore::SettingsScope sessionScope = {});
    ~WelcomeWindow() override;
protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupStyle();
    void refreshSessions();
    bool editSession(SavedSession session, bool isNew);
    void openSession(const SavedSession& session);

    QMenu *fileMenu;
    QMenu *editMenu;
    QPushButton *newFileButton;
    QPushButton *openFileButton;
    QPushButton *openFolderButton;
    QListWidget *recentFilesList;

    QPushButton *newSessionButton;
    QPushButton *manageSessionsButton;
    QListWidget *savedSessionsList;
    QLabel* noSessionsLabel;
    QStackedLayout* sessionsContentLayout;
    SessionStore sessionStore;

private slots:
    void handleNewFileRequest();
    void handleOpenFileRequest();
    void handleOpenFolderRequest();
    void onRecentFileClicked(QListWidgetItem*);
    void createSession();
    void manageSessions();
    void openSelectedSession(QListWidgetItem*);

signals:
    void newDocumentRequested();
    void fileOpenRequested(QString filePath);
    void folderOpenRequested(QString folderPath);
    void sessionOpenRequested(SavedSession session);
};
