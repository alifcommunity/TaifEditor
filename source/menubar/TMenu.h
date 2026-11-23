#pragma once

#include <QAction>
#include <QList>
#include <QString>
#include <QMenuBar>
#include <QFileSystemModel>
#include <QTreeView>

class QMenu;



class TMenuBar : public QMenuBar {

	Q_OBJECT
public:
    TMenuBar(QWidget* parent = nullptr);

    /** Updates View-menu checks to match the open/closed state of each dock. */
    void setOpenViewToolActions(bool alifOutputOpen,
                                bool terminalOpen,
                                bool problemsOpen);

    /** Sets how many recent-file entries the submenu may hold (0 disables). */
    void setRecentFilesLimit(int limit);
    void addRecentFiles(const int rfaSize = 0);

    QAction* newAction;
    QAction* openFileAction;
    QAction* openFolderAction;
    QAction* saveAction;
    QAction* saveAsAction;
    QAction* SettingsAction;
    QAction* exitAction;
    QAction* settingsAction;
    QAction* aboutAction;

    QAction* runAction;

    QAction* alifOutputAction;
    QAction* terminalAction;
    QAction* problemsAction;

    QAction* undoAction;
    QAction* redoAction;
    QAction* cutAction;
    QAction* copyAction;
    QAction* pasteAction;
    QAction* findAction;
    QAction* replaceAction;
    QAction* goToLineAction;
    QAction* toggleCommentAction;
    QAction* duplicateLineAction;
    QAction* moveLineUpAction;
    QAction* moveLineDownAction;

    QAction* updateAction;

signals:
    void newRequested();
    void openFileRequested();
    void openFolderRequested();
    void saveRequested();
    void saveAsRequested();
    void settingsRequest();
    void exitRequested();
    void runRequested();
    void aboutRequested();
    void updateRequested();

    /** Emitted when the user selects a recent file from the menu. */
    void openRecentFileRequested(const QString& filePath);

    void showAlifOutputRequested();
    void showTerminalRequested();
    void showProblemsRequested();

    void undoRequested();
    void redoRequested();
    void cutRequested();
    void copyRequested();
    void pasteRequested();
    void findRequested();
    void replaceRequested();
    void goToLineRequested();
    void toggleCommentRequested();
    void duplicateLineRequested();
    void moveLineUpRequested();
    void moveLineDownRequested();

private slots:
    void refreshRecentFilesMenu();

private:
    QMenu* fileMenu{};
    QMenu* editMenu{};
    QMenu* viewMenu{};
    QMenu* runMenu{};
    QMenu* helpMenu{};

    QMenu* recentFilesMenu = nullptr;
    QList<QAction*> recentFileActions;
    QStringList recentFilePaths;
    int maxRecentFiles = 10;
};