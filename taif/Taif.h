#pragma once

#include "TEditor.h"
#include "TMenu.h"
#include "TSearchPanel.h"
#include "AlifRunController.h"
#include "SessionStore.h"
#include "RecoveryCoordinator.h"
#include "GitTypes.h"

#include <QMainWindow>
#include <QStatusBar>
#include <QSplitter>
#include <QFileSystemWatcher>
#include <QTimer>


QT_BEGIN_NAMESPACE
namespace Ui { class Taif; }
QT_END_NAMESPACE

class QDockWidget;
class DiagnosticsPanel;
class TConsole;
class TBreadcrumbBar;
class TStatusBar;
class ProjectExplorerWidget;
struct ProjectFileOperationResult;
class GitPanelWidget;
class GitRepositoryService;

struct SessionRestoreResult final {
    QStringList openedFilePaths;
    QStringList unavailableFilePaths;
};

class Taif : public QMainWindow
{
    Q_OBJECT

public:
    Taif(const QString& filePath = "", QWidget* parent = nullptr,
         bool createInitialDocument = true);
    ~Taif();
    void loadFolder(const QString &folderPath);
    [[nodiscard]] SessionRestoreResult restoreSession(const SavedSession& session);
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
private slots:
    void newFile();
    void openFile(QString);
    void saveFile();
    void saveFileAs();
    void handleOpenFolderMenu();
    void openSettings();
    void exitApp();

    void runAlif();
    void aboutTaif();
    void checkForUpdates();

    void updateWindowTitle();

    void onProjectFileActivated(const QString& filePath);
    void createProjectFile(const QString& directoryPath, const QString& name);
    void createProjectFolder(const QString& directoryPath, const QString& name);
    void renameProjectPath(const QString& sourcePath, const QString& newName);
    void deleteProjectPath(const QString& sourcePath);
    void revealProjectPath(const QString& sourcePath);

    //* Git Section deactivated, active when enable this system
    // void showGitPanel();
    // void handleGitDestructiveOperation(GitOperation operation, const QStringList& relativePaths);
    // void handleGitPull();
    // void handleGitBranchSwitch(const QString& branch);
    //* Git Section deactivated, active when enable this system

    void closeTab(int index);
    void toggleSidebar();

    void toggleConsole();

    void updateCursorPosition();
    void onCurrentTabChanged();
signals:
    void returnToWelcomeRequested();
    void closeRejected();

private slots:
    void onFileChanged(const QString &path);
    void showFindBar();
    void showReplaceBar();
    void hideFindBar();
    void performSearch(bool forward, bool next);
    void findText();
    void findNextText();
    void findPrevText();
    void replaceOne();
    void replaceAll();
    void goToLine();
    void clearSearchHighlights();
    void refreshBreadcrumbs();
    void bindInformationBarToEditor(TEditor* editor);
    void refreshStatusBar();
    void bindBreadcrumbsToEditor(TEditor* editor);
    void revealBreadcrumbPath(const QString& path);

private:
    void setupUI();
    void connectSettingsSignals();
    void setupConnections();
    void applyEditorPreferences(const EditorPreferences& preferences);
    void setupStyle();
    void setupTimers();

    enum class SaveDecision : quint8 {
        Save,
        Discard,
        Cancel
    };

    [[nodiscard]] SaveDecision requestSaveDecision(TEditor* editor) const;
    [[nodiscard]] bool prepareEditorForClose(TEditor* editor);
    [[nodiscard]] bool saveEditor(TEditor* editor);
    [[nodiscard]] bool saveEditorAs(TEditor* editor);
    [[nodiscard]] bool writeEditorContents(TEditor* editor, const QString& filePath);
    void finalizeSavedEditor(TEditor* editor, const QString& filePath);
    void onEditorModificationChanged(TEditor* editor, bool modified);
    void registerEditorRecovery(TEditor* editor);
    void flushRecoverySnapshots();
    void importKnownLegacyRecoveryEntries(const QString& launchFilePath);
    void presentRecoveryEntries();
    void restoreRecoveryEntry(const RecoveryEntry& entry);

    TEditor* currentEditor();
    [[nodiscard]] bool hasOpenEditorAtOrBelow(const QString& path) const;
    void presentProjectOperationResult(const ProjectFileOperationResult& result);

    void connectEditorDiagnostics(TEditor* editor);
    void refreshDiagnosticsPanel();
    void showProblemsWithWarning();
    void showAndRaiseDock(QDockWidget* dock);
    void syncBottomToolActionState();
    void addWatch(const QString &filePath);
    void removeWatch(const QString &filePath);
    void reloadEditor(TEditor *editor);
    QString pathForEditor(TEditor *editor) const;
    bool openDocumentFile(const QString& filePath, bool promptForBackupRecovery,
                          bool activateTab, bool updateRecentFiles,
                          QString* failureMessage = nullptr);

private:
    QTabWidget *tabWidget{};
    TMenuBar* menuBar{};
    TSettings* setting{};
    RecoveryCoordinator* recoveryCoordinator{};
    bool recoveryCloseFlushPending = false;
    bool recoveryCloseFlushAcknowledged = false;
    QAction *toggleSidebarAction{};
    QString folderPath{};
    QAbstractItemModel* model{};

    QSplitter *mainSplitter{};
    ProjectExplorerWidget* projectExplorer{};

    //* Git Section deactivated, active when enable this system
    // GitPanelWidget* gitPanel{};
    // QDockWidget* gitDock{};
    // QAction* showGitPanelAction{};
    //* Git Section deactivated, active when enable this system

    QSplitter *editorSplitter{};
    TBreadcrumbBar* breadcrumbBar{};
    QMetaObject::Connection breadcrumbConnection{};
    QMetaObject::Connection cursorPositionConnection{};
    QMetaObject::Connection editorInformationConnection{};
    QDockWidget* terminalDock{};
    QDockWidget* alifOutputDock{};
    TConsole* systemTerminal{};
    TConsole* alifOutputConsole{};

    AlifRunController* runController{};
    QAction* runToolbarAction{};

    QFileSystemWatcher* fileWatcher{};
    bool savingFromApp{};
    QTimer *saveSuppressTimer{};
    TStatusBar* editorStatusBar{};
    SearchPanel *searchBar{};
    QDockWidget* diagnosticsDock{};
    DiagnosticsPanel* diagnosticsPanel{};
};
