#include "TMenu.h"
#include "EditorPreferences.h"

#include <QAction>
#include <QMenu>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QSignalBlocker>


QAction* createEditAction(QObject* const parent,
                          const QString& text,
                          const QString& objectName,
                          const QKeySequence& shortcut,
                          const bool isCheckable=false)
{
    auto* const action = new QAction(text, parent);
    action->setObjectName(objectName);
    action->setShortcut(shortcut);
    action->setCheckable(isCheckable);
    return action;
}


TMenuBar::TMenuBar(QWidget* parent) {
    fileMenu = addMenu(QStringLiteral("ملف"));
    editMenu = addMenu(QStringLiteral("تحرير"));
    viewMenu = addMenu(QStringLiteral("عرض"));
    runMenu = addMenu(QStringLiteral("تشغيل"));
    helpMenu = addMenu(QStringLiteral("مساعدة"));

    fileMenu->setMinimumWidth(200);
    editMenu->setMinimumWidth(200);
    viewMenu->setMinimumWidth(200);
    runMenu->setMinimumWidth(200);
    helpMenu->setMinimumWidth(200);

    newAction = createEditAction(parent, QStringLiteral("جديد"),
                                 QStringLiteral("NewAction"), QKeySequence::New);
    openFileAction = createEditAction(parent, QStringLiteral("فتح ملف"),
                                      QStringLiteral("OpenFileAction"), QKeySequence::Open);
    openFolderAction = createEditAction(parent, QStringLiteral("فتح مجلد"),
                                        QStringLiteral("OpenFolderAction"), QKeySequence());
    saveAction = createEditAction(parent, QStringLiteral("حفظ"),
                                  QStringLiteral("SaveAction"), QKeySequence::Save);
    saveAsAction = createEditAction(parent, QStringLiteral("حفظ باسم"),
                               QStringLiteral("SaveAsAction"), QKeySequence::SaveAs);
    settingsAction = createEditAction(parent, QStringLiteral("الإعدادات"),
                                      QStringLiteral("SettingsAction"), QKeySequence());
    exitAction = createEditAction(parent, QStringLiteral("خروج"),
                                  QStringLiteral("ExitAction"), QStringLiteral());


    undoAction = createEditAction(parent, QStringLiteral("تراجع"),
                                  QStringLiteral("UndoAction"), QKeySequence::Undo);
    redoAction = createEditAction(parent, QStringLiteral("إعادة"),
                                  QStringLiteral("RedoAction"), QKeySequence::Redo);
    cutAction = createEditAction(parent, QStringLiteral("قص"),
                                 QStringLiteral("CutAction"), QKeySequence::Cut);
    copyAction = createEditAction(parent, QStringLiteral("نسخ"),
                                  QStringLiteral("CopyAction"), QKeySequence::Copy);
    pasteAction = createEditAction(parent, QStringLiteral("لصق"),
                                   QStringLiteral("PasteAction"), QKeySequence::Paste);
    findAction = createEditAction(parent, QStringLiteral("بحث"),
                                  QStringLiteral("FindAction"), QKeySequence::Find);
    replaceAction = createEditAction(parent, QStringLiteral("بحث واستبدال"),
                                     QStringLiteral("ReplaceAction"), QKeySequence::Replace);
    goToLineAction = createEditAction(parent, QStringLiteral("الذهاب إلى سطر"),
                                      QStringLiteral("GoToLineAction"), QKeySequence::fromString("Ctrl+G"));
    toggleCommentAction = createEditAction(parent, QStringLiteral("تعليق سطر"),
                                           QStringLiteral("ToggleCommentAction"), QKeySequence::fromString("Ctrl+J"));
    duplicateLineAction = createEditAction(parent, QStringLiteral("تكرار السطر"),
                                           QStringLiteral("duplicateLineAction"), QKeySequence::fromString("Ctrl+D"));
    moveLineUpAction = createEditAction(parent, QStringLiteral("نقل السطر لأعلى"),
                                        QStringLiteral("moveLineUpAction"), QKeySequence::fromString("Alt+Up"));
    moveLineDownAction = createEditAction(parent, QStringLiteral("نقل السطر لأسفل"),
                                          QStringLiteral("moveLineDownAction"), QKeySequence::fromString("Alt+Down"));


    alifOutputAction = createEditAction(parent, QStringLiteral("مخرجات ألف"),
                                        QStringLiteral("ShowAlifOutputAction"), QKeySequence(), true);
    terminalAction = createEditAction(parent, QStringLiteral("الطرفية"),
                                      QStringLiteral("ShowTerminalAction"), QKeySequence(), true);
    problemsAction = createEditAction(parent, QStringLiteral("الأخطاء"),
                                      QStringLiteral("ShowProblemsAction"), QKeySequence(), true);


    runAction = createEditAction(parent, QStringLiteral("تشغيل"),
                                 QStringLiteral("RunAction"), QKeySequence::fromString("Ctrl+R"));


    aboutAction = createEditAction(parent, QStringLiteral("عن المحرر"),
                                   QStringLiteral("AboutAction"), QKeySequence());
    updateAction = createEditAction(parent, QStringLiteral("البحث عن تحديثات"),
                                    QStringLiteral("UpdateAction"), QKeySequence());




    // --- Recent Files submenu (الملفات الأخيرة) ---
    recentFilesMenu = new QMenu(QStringLiteral("الملفات الأخيرة"), this);
    recentFilesMenu->setMinimumWidth(200);
    connect(recentFilesMenu, &QMenu::aboutToShow, this, &TMenuBar::refreshRecentFilesMenu);
    addRecentFiles();


    fileMenu->addAction(newAction);
    fileMenu->addAction(openFileAction);
    fileMenu->addAction(openFolderAction);
    fileMenu->addMenu(recentFilesMenu);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(settingsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    viewMenu->addAction(alifOutputAction);
    viewMenu->addAction(terminalAction);
    viewMenu->addAction(problemsAction);

    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    editMenu->addAction(cutAction);
    editMenu->addAction(copyAction);
    editMenu->addAction(pasteAction);
    editMenu->addSeparator();
    editMenu->addAction(findAction);
    editMenu->addAction(replaceAction);
    editMenu->addAction(goToLineAction);
    editMenu->addSeparator();
    editMenu->addAction(toggleCommentAction);
    editMenu->addAction(duplicateLineAction);
    editMenu->addAction(moveLineUpAction);
    editMenu->addAction(moveLineDownAction);

    runMenu->addAction(runAction);

    helpMenu->addAction(aboutAction);
    helpMenu->addAction(updateAction);



    connect(newAction, &QAction::triggered, this, &TMenuBar::newRequested);
    connect(openFileAction, &QAction::triggered, this, &TMenuBar::openFileRequested);
    connect(openFolderAction, &QAction::triggered, this, &TMenuBar::openFolderRequested);
    connect(saveAction, &QAction::triggered, this, &TMenuBar::saveRequested);
    connect(saveAsAction, &QAction::triggered, this, &TMenuBar::saveAsRequested);
    connect(settingsAction, &QAction::triggered, this, &TMenuBar::settingsRequest);
    connect(exitAction, &QAction::triggered, this, &TMenuBar::exitRequested);

    connect(alifOutputAction, &QAction::triggered, this, &TMenuBar::showAlifOutputRequested);
    connect(terminalAction, &QAction::triggered, this, &TMenuBar::showTerminalRequested);
    connect(problemsAction, &QAction::triggered, this, &TMenuBar::showProblemsRequested);

    connect(undoAction, &QAction::triggered, this, &TMenuBar::undoRequested);
    connect(redoAction, &QAction::triggered, this, &TMenuBar::redoRequested);
    connect(cutAction, &QAction::triggered, this, &TMenuBar::cutRequested);
    connect(copyAction, &QAction::triggered, this, &TMenuBar::copyRequested);
    connect(pasteAction, &QAction::triggered, this, &TMenuBar::pasteRequested);
    connect(findAction, &QAction::triggered, this, &TMenuBar::findRequested);
    connect(replaceAction, &QAction::triggered, this, &TMenuBar::replaceRequested);
    connect(goToLineAction, &QAction::triggered, this, &TMenuBar::goToLineRequested);
    connect(toggleCommentAction, &QAction::triggered, this, &TMenuBar::toggleCommentRequested);
    connect(duplicateLineAction, &QAction::triggered, this, &TMenuBar::duplicateLineRequested);
    connect(moveLineUpAction, &QAction::triggered, this, &TMenuBar::moveLineUpRequested);
    connect(moveLineDownAction, &QAction::triggered, this, &TMenuBar::moveLineDownRequested);

    connect(runAction, &QAction::triggered, this, &TMenuBar::runRequested);

    connect(aboutAction, &QAction::triggered, this, &TMenuBar::aboutRequested);
    connect(updateAction, &QAction::triggered, this, &TMenuBar::updateRequested);
}


void TMenuBar::setOpenViewToolActions(const bool alifOutputOpen,
                                      const bool terminalOpen,
                                      const bool problemsOpen)
{
    const QSignalBlocker alifOutputActionBlocker(alifOutputAction);
    const QSignalBlocker terminalActionBlocker(terminalAction);
    const QSignalBlocker problemsActionBlocker(problemsAction);

    if (alifOutputAction != nullptr) {
        alifOutputAction->setChecked(alifOutputOpen);
    }
    if (terminalAction != nullptr) {
        terminalAction->setChecked(terminalOpen);
    }
    if (problemsAction != nullptr) {
        problemsAction->setChecked(problemsOpen);
    }
}

void TMenuBar::addRecentFiles(const int rfaSize) {
    // Pre-create placeholder actions up to the configured maximum. Visibility
    // and text are toggled dynamically in refreshRecentFilesMenu().
    for (int i = rfaSize; i < maxRecentFiles; ++i) {
        QAction* const action = new QAction(this);
        action->setVisible(false);
        recentFilesMenu->addAction(action);
        connect(action, &QAction::triggered, this, [this, i]() {
            if (i >= 0 && i < static_cast<int>(recentFileActions.size())) {
                emit openRecentFileRequested(recentFilePaths[i]);
            }
        });
        recentFileActions.append(action);
    }
}

void TMenuBar::setRecentFilesLimit(const int limit) {
    maxRecentFiles = qBound(0, limit, 30);

    // If the limit shrank below the number of pre-created actions,
    // trim the excess; if it grew, create additional placeholder actions.
    while (recentFileActions.size() > maxRecentFiles) {
        delete recentFileActions.takeLast();
    }
    addRecentFiles(recentFileActions.size());

    refreshRecentFilesMenu();
}

void TMenuBar::refreshRecentFilesMenu() {
    // Read the persisted list directly from QSettings (same source used by Taif::openDocumentFile).
    QSettings settings(QStringLiteral("Alif"), QStringLiteral("Taif"));
    const int limit = PreferencesStore::load().recentFilesLimit;

    if (limit <= 0 || recentFileActions.isEmpty()) {
        for (QAction* action : std::as_const(recentFileActions)) {
            action->setVisible(false);
            action->setText(QString());
            action->setData(QString());
        }
        // Hide the submenu entry entirely when there are no recent files.
        QList<QAction*> menuActions = fileMenu->actions();
        for (QAction* const menuAction : menuActions) {
            if (menuAction->menu() == recentFilesMenu) {
                menuAction->setVisible(false);
                break;
            }
        }
        return;
    }

    QStringList files = settings.value(QStringLiteral("RecentFiles")).toStringList();
    // Keep only existing files and deduplicate while preserving order.
    QStringList validFiles;
    QSet<QString> seen;
    for (const QString& path : files) {
        if (!path.isEmpty() && !seen.contains(path)) {
            const QFileInfo info(path);
            if (info.exists()) {
                validFiles.append(path);
                seen.insert(path);
            }
        }
    }

    recentFilePaths = validFiles; // Keep the full list so indices stay correct.

    for (int i = 0; i < static_cast<int>(recentFileActions.size()); ++i) {
        QAction* const action = recentFileActions[i];
        if (i < validFiles.size()) {
            const QString& path = validFiles[i];
            action->setVisible(true);
            // Use the base name as display text; show full path on hover.
            action->setText(QFileInfo(path).fileName());
            action->setToolTip(path);
            action->setData(path);
        }
    }

    // Show or hide the submenu entry in the File menu based on whether we have entries.
    QList<QAction*> menuActions = fileMenu->actions();
    for (QAction* const menuAction : menuActions) {
        if (menuAction->menu() == recentFilesMenu) {
            menuAction->setVisible(!validFiles.isEmpty());
            break;
        }
    }
}