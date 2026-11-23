#include "Taif.h"
#include "TConsole.h"
#include "InlinePromptConsole.h"
#include "DockableConsoleTool.h"
#include "TSearchPanel.h"
#include "TDiagnosticsPanel.h"
#include "TBreadcrumbBar.h"
#include "TStatusBar.h"
#include "TProjectExplorerWidget.h"
#include "ProjectFileOperations.h"
#include "TRecoveryDialog.h"
#include "GitPanelWidget.h"
#include "GitRepositoryService.h"

#include <QDockWidget>
#include <QDir>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QStringConverter>
#include <QShortcut>
#include <QGuiApplication>
#include <QScreen>
#include <QCoreApplication>
#include <QTextStream>
#include <QApplication>
#include <QMessageBox>
#include <QToolBar>
#include <QHeaderView>
#include <QSettings>
#include <QProcess>
#include <QStyleFactory>
#include <QKeyEvent>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QSet>
#include <QSaveFile>
#include <QStatusBar>


Taif::Taif(const QString& filePath, QWidget* const parent,
           const bool createInitialDocument)
    : QMainWindow(parent)
{

    setAttribute(Qt::WA_DeleteOnClose);

    setting = new TSettings(this);
    recoveryCoordinator = new RecoveryCoordinator({}, this);
    recoveryCoordinator->pruneExpiredEntries(30);

    setupUI();
    connectSettingsSignals();
    setupConnections();
    setupStyle();
    setupTimers();

    runController = new AlifRunController(this);
    connect(runController, &AlifRunController::standardOutput,
            alifOutputConsole, &TConsole::appendPlainTextThreadSafe);
    connect(runController, &AlifRunController::standardError,
            alifOutputConsole, &TConsole::appendPlainTextThreadSafe);
    connect(runController, &AlifRunController::launchFailed, this, [this](const QString& message) {
        if (alifOutputConsole != nullptr) {
            alifOutputConsole->appendPlainTextThreadSafe(
                QStringLiteral("تعذر تشغيل ألف: %1\n").arg(message));
        }
    });
    connect(runController, &AlifRunController::finished, this,
            [this](const int exitCode, const QProcess::ExitStatus exitStatus) {
                if (alifOutputConsole == nullptr) {
                    return;
                }
                const QString outcome = exitStatus == QProcess::NormalExit
                                            ? QStringLiteral("اكتمل التنفيذ (رمز الخروج = %1).\n").arg(exitCode)
                                            : QStringLiteral("انتهى التنفيذ بشكل غير طبيعي.\n");
                alifOutputConsole->appendPlainTextThreadSafe(
                    QStringLiteral("\n──────────────────────────────\n%1").arg(outcome));
                if (auto* const inlineConsole = qobject_cast<InlinePromptConsole*>(alifOutputConsole)) {
                    inlineConsole->endInput();
                }
            });
    connect(runController, &AlifRunController::stateChanged, this,
            [this](const AlifRunController::State state) {
                const bool active = state == AlifRunController::State::Starting
                                    || state == AlifRunController::State::Running
                                    || state == AlifRunController::State::Stopping;
                const QString label = active ? QStringLiteral("إيقاف التنفيذ")
                                             : QStringLiteral("تشغيل");
                if (auto* const inlineConsole = qobject_cast<InlinePromptConsole*>(alifOutputConsole)) {
                    if (active) {
                        inlineConsole->beginInput();
                    } else {
                        inlineConsole->endInput();
                    }
                }
                const QIcon icon(active ? QStringLiteral(":/icons/resources/stop.svg")
                                     : QStringLiteral(":/icons/resources/run.svg"));
                if (menuBar != nullptr && menuBar->runAction != nullptr) {
                    menuBar->runAction->setText(label);
                    menuBar->runAction->setIcon(icon);
                }
                if (runToolbarAction != nullptr) {
                    runToolbarAction->setToolTip(label);
                    runToolbarAction->setIcon(icon);
                }
            });
    connect(alifOutputConsole, &TConsole::commandEntered,
            runController, &AlifRunController::sendInput);

    connect(recoveryCoordinator, &RecoveryCoordinator::removalFailed, this,
            [this](const QString&, const QString& errorMessage) {
                statusBar()->showMessage(errorMessage.isEmpty()
                                         ? QStringLiteral("تعذر حذف نسخة الاستعادة.") : errorMessage, 7000);
            });
    connect(recoveryCoordinator, &RecoveryCoordinator::flushCompleted, this,
            [this](const bool allPersisted) {
                if (!recoveryCloseFlushPending) {
                    return;
                }
                recoveryCloseFlushPending = false;
                if (!allPersisted) {
                    setEnabled(true);
                    statusBar()->showMessage(
                        QStringLiteral("تعذر تأكيد حفظ نسخة الاستعادة. أعد المحاولة قبل الإغلاق."),
                        7000);
                    emit closeRejected();
                    return;
                }

                recoveryCloseFlushAcknowledged = true;
                close();
            });

    installEventFilter(this);
    // Importing legacy adjacent backups can involve disk reads and atomic writes.
    // Defer it until the event loop begins so construction and first paint stay responsive.
    QTimer::singleShot(0, this, [this, filePath]() {
        importKnownLegacyRecoveryEntries(filePath);
        presentRecoveryEntries();
    });

    if (!filePath.isEmpty()) {
        QString failureMessage;
        if (!openDocumentFile(filePath, true, true, true, &failureMessage)) {
            QMessageBox::warning(this, QStringLiteral("خطأ"),
                                 failureMessage.isEmpty()
                                     ? QStringLiteral("لا يمكن فتح الملف.") : failureMessage);
        }
    } else if (createInitialDocument && tabWidget->count() == 0) {
        newFile();
    }
}

Taif::~Taif() {
    if (searchBar != nullptr) {
        searchBar->detachFromHost();
    }
    if (runController != nullptr) {
        runController->shutdown();
    }

    if (TEditor* editor = currentEditor()) {
        QSettings settings("Alif", "Taif");
        settings.setValue("editorFontSize", editor->font().pixelSize());
        settings.setValue("editorFontType", editor->font().family());
        settings.setValue("editorCodeTheme", setting->getThemeCombo()->currentIndex());
        settings.sync();
    }
}

void Taif::setupUI() {
    tabWidget = new QTabWidget(this);
    tabWidget->setObjectName("MainTabs");
    tabWidget->setDocumentMode(true);
    tabWidget->setTabsClosable(true);
    tabWidget->setMovable(true);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    menuBar = new TMenuBar(this);
    setMenuBar(menuBar);
    // Initialize the recent-files submenu with the configured limit.
    menuBar->setRecentFilesLimit(PreferencesStore::load().recentFilesLimit);

    mainSplitter = new QSplitter(Qt::Horizontal, this);
    projectExplorer = new ProjectExplorerWidget(this);
    projectExplorer->setVisible(false);

    editorSplitter = new QSplitter(Qt::Vertical, this);
    searchBar = new SearchPanel(this);
    searchBar->hide();

    QScreen* screen = QGuiApplication::primaryScreen();
    QRect screenGeo = screen->availableGeometry();
    int margin = 90;
    int widthFixedNum = 5;
    int x = screenGeo.right() - screenGeo.size().width() + margin * widthFixedNum / 2;
    int y = screenGeo.top() + 30 + margin / 2; // 30 is top system bar height
    int width = screenGeo.size().width() - margin * widthFixedNum;
    int height = screenGeo.size().height() - margin;
    this->setGeometry(x, y, width, height);

    QToolBar *mainToolBar = new QToolBar("Main Toolbar", this);
    mainToolBar->setObjectName("mainToolBar");
    mainToolBar->setMovable(false);
    mainToolBar->setIconSize(QSize(25, 25));
    this->addToolBar(Qt::RightToolBarArea, mainToolBar);

    toggleSidebarAction = new QAction(QIcon(":/icons/resources/folder-tree.svg"), "متصفح الملفات", this);
    toggleSidebarAction->setCheckable(true);
    toggleSidebarAction->setChecked(false);
    mainToolBar->addAction(toggleSidebarAction);

    runToolbarAction = new QAction(QIcon(":/icons/resources/run.svg"),
                                 QStringLiteral("تشغيل الملف الحالي"), this);

    mainToolBar->addAction(runToolbarAction);
    connect(runToolbarAction, &QAction::triggered, this, &Taif::runAlif);

    auto* const editorPane = new QWidget(editorSplitter);
    auto* const editorPaneLayout = new QVBoxLayout(editorPane);
    editorPaneLayout->setContentsMargins(0, 0, 0, 0);
    editorPaneLayout->setSpacing(0);
    breadcrumbBar = new TBreadcrumbBar(editorPane);
    editorPaneLayout->addWidget(breadcrumbBar);
    editorPaneLayout->addWidget(tabWidget, 1);
    editorSplitter->addWidget(editorPane);
    editorSplitter->addWidget(searchBar);
    editorSplitter->setSizes({1000, 45});

    mainSplitter->addWidget(projectExplorer);
    mainSplitter->addWidget(editorSplitter);
    mainSplitter->setSizes({200, 700});
    this->setCentralWidget(mainSplitter);

    diagnosticsDock = new QDockWidget(QStringLiteral("المشكلات"), this);
    diagnosticsDock->setObjectName(QStringLiteral("DiagnosticsDock"));
    // diagnosticsDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea | Qt::LeftDockWidgetArea);
    diagnosticsDock->setFeatures(QDockWidget::DockWidgetMovable
                                 | QDockWidget::DockWidgetFloatable
                                 | QDockWidget::DockWidgetClosable);
    diagnosticsPanel = new DiagnosticsPanel(diagnosticsDock);
    diagnosticsDock->setWidget(diagnosticsPanel);
    addDockWidget(Qt::BottomDockWidgetArea, diagnosticsDock);
    resizeDocks({diagnosticsDock}, {220}, Qt::Vertical);
    connect(diagnosticsPanel, &DiagnosticsPanel::diagnosticActivated,
            this, [this](const EditorDiagnostic& diagnostic) {
                if (TEditor* editor = currentEditor()) {
                    editor->navigateToDiagnosticRange(diagnostic.range);
                }
            });

    //* Git Section deactivated, active when enable this system
    // gitPanel = new GitPanelWidget(projectExplorer->gitRepositoryService(), this);
    // gitDock = new QDockWidget(QStringLiteral("التحكم بالنسخة"), this);
    // gitDock->setObjectName(QStringLiteral("GitDock"));
    // gitDock->setLayoutDirection(Qt::RightToLeft);
    // gitDock->setWidget(gitPanel);
    // gitDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    // gitDock->setStyleSheet(QStringLiteral(R"(
    //     QDockWidget#GitDock { background:#0f172a; color:#e2e8f0; border:1px solid #1e3a5f; }
    //     QDockWidget#GitDock::title { background:#111d33; color:#dbeafe; padding:7px 9px; border-bottom:1px solid #263a57;}
    //     QDockWidget#GitDock::close-button, QDockWidget#GitDock::float-button { background:transparent; border:none; }
    //     QDockWidget#GitDock::close-button:hover, QDockWidget#GitDock::float-button:hover { background:#1e3a5f; }
    // )"));
    // addDockWidget(Qt::RightDockWidgetArea, gitDock);
    // gitDock->hide();

    // auto* const gitEdgeToolbar = new QToolBar(QStringLiteral("Git"), this);
    // gitEdgeToolbar->setObjectName(QStringLiteral("GitEdgeToolbar"));
    // gitEdgeToolbar->setOrientation(Qt::Vertical);
    // gitEdgeToolbar->setStyleSheet(QStringLiteral(R"(
    //     QToolBar#GitEdgeToolbar { background:#0f172a; border-left:1px solid #1e3a5f; spacing:3px; padding:3px; }
    //     QToolButton { background:#111d33; color:#93c5fd; border:1px solid #263a57; border-radius:4px; padding:6px 4px; }
    //     QToolButton:hover, QToolButton:checked { background:#1e3a5f; color:#ffffff; border-color:#3b82f6; }
    // )"));
    // showGitPanelAction = gitEdgeToolbar->addAction(QStringLiteral("Git"));
    // showGitPanelAction->setCheckable(true);
    // showGitPanelAction->setToolTip(QStringLiteral("إظهار لوحة Git"));
    // addToolBar(Qt::RightToolBarArea, gitEdgeToolbar);
    //* Git Section deactivated, active when enable this system

#if defined(Q_OS_LINUX)
    const QString terminalTitle = QStringLiteral("طرفية النظام (Bash)");
#elif defined(Q_OS_MACOS)
    const QString terminalTitle = QStringLiteral("طرفية النظام (Zsh)");
#else
    const QString terminalTitle = QStringLiteral("طرفية النظام (CMD)");
#endif
    const DockableConsoleTool terminalTool = DockableConsoleToolFactory::create(
        this, terminalTitle, QStringLiteral("TerminalDock"),
        QStringLiteral("SystemTerminalConsole"), true);
    terminalDock = terminalTool.dock;
    systemTerminal = terminalTool.console;

    const DockableConsoleTool outputTool = DockableConsoleToolFactory::create(
        this, QStringLiteral("مخرجات ألف"), QStringLiteral("AlifOutputDock"),
        QStringLiteral("AlifOutputConsole"), false);
    alifOutputDock = outputTool.dock;
    alifOutputConsole = outputTool.console;

    if (terminalDock && alifOutputDock) {
        DockableConsoleToolFactory::ensureTabifiedWith(this, diagnosticsDock, alifOutputDock);
        DockableConsoleToolFactory::ensureTabifiedWith(this, diagnosticsDock, terminalDock);
        DockableConsoleToolFactory::ensureTabifiedWith(this, diagnosticsDock, diagnosticsDock);

        diagnosticsDock->hide();
        alifOutputDock->hide();
        // Use showAndRaiseDock (not .show()) so the native shell is
        // actually started via DockableConsoleToolFactory::startCmd().
        showAndRaiseDock(terminalDock);
    }

    editorStatusBar = new TStatusBar(statusBar());
    statusBar()->addPermanentWidget(editorStatusBar, 1);
    connect(editorStatusBar, &TStatusBar::diagnosticsActivated, this,
            &Taif::showProblemsWithWarning);
}

void Taif::connectSettingsSignals()
{
    if (setting == nullptr) {
        return;
    }

    const auto applyPreferences = [this](const EditorPreferences& preferences) {
        applyEditorPreferences(preferences);
    };
    connect(setting, &TSettings::preferencesPreviewed, this, applyPreferences);
    connect(setting, &TSettings::preferencesApplied, this, [this, applyPreferences](const EditorPreferences& preferences) {
        if (menuBar != nullptr) {
            menuBar->setRecentFilesLimit(preferences.recentFilesLimit);
        }
        applyPreferences(preferences);
    });
}

void Taif::applyEditorPreferences(const EditorPreferences& requestedPreferences)
{
    if (setting == nullptr) {
        return;
    }

    const EditorPreferences preferences = PreferencesStore::normalize(requestedPreferences);
    const QVector<std::shared_ptr<SyntaxTheme>> themes = setting->getAvailableThemes();
    const std::shared_ptr<SyntaxTheme> theme = themes.isEmpty()
                                                   ? std::shared_ptr<SyntaxTheme>()
                                                   : themes.at(qBound(0, preferences.syntaxThemeIndex, themes.size() - 1));

    for (int index = 0; index < tabWidget->count(); ++index) {
        if (auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index))) {
            editor->applyPreferences(preferences);
            if (theme) {
                editor->updateHighlighterTheme(theme);
            }
        }
    }
}

void Taif::setupConnections() {

    connect(projectExplorer, &ProjectExplorerWidget::fileActivationRequested,
            this, &Taif::onProjectFileActivated);
    connect(projectExplorer, &ProjectExplorerWidget::createFileRequested,
            this, &Taif::createProjectFile);
    connect(projectExplorer, &ProjectExplorerWidget::createFolderRequested,
            this, &Taif::createProjectFolder);
    connect(projectExplorer, &ProjectExplorerWidget::renameRequested,
            this, &Taif::renameProjectPath);
    connect(projectExplorer, &ProjectExplorerWidget::deleteRequested,
            this, &Taif::deleteProjectPath);
    connect(projectExplorer, &ProjectExplorerWidget::revealRequested,
            this, &Taif::revealProjectPath);
    connect(tabWidget, &QTabWidget::tabCloseRequested, this, &Taif::closeTab);
    connect(toggleSidebarAction, &QAction::triggered, this, &Taif::toggleSidebar);

    //* Git Section deactivated, active when enable this system
    // connect(showGitPanelAction, &QAction::triggered, this, &Taif::showGitPanel);
    // connect(gitDock, &QDockWidget::visibilityChanged, this,
    //         [this](const bool visible) { showGitPanelAction->setChecked(visible); });
    // connect(gitPanel, &GitPanelWidget::destructiveOperationRequested,
    //         this, &Taif::handleGitDestructiveOperation);
    // connect(gitPanel, &GitPanelWidget::pullRequested, this, &Taif::handleGitPull);
    // connect(gitPanel, &GitPanelWidget::branchSwitchRequested, this, &Taif::handleGitBranchSwitch);
    //* Git Section deactivated, active when enable this system

    connect(menuBar, &TMenuBar::newRequested, this, &Taif::newFile);
    connect(menuBar, &TMenuBar::openFileRequested, this, [this](){this->openFile("");});
    connect(menuBar, &TMenuBar::openRecentFileRequested, this,
            [this](const QString& filePath) { openFile(filePath); });
    connect(menuBar, &TMenuBar::saveRequested, this, &Taif::saveFile);
    connect(menuBar, &TMenuBar::saveAsRequested, this, &Taif::saveFileAs);
    connect(menuBar, &TMenuBar::settingsRequest, this, &Taif::openSettings);
    connect(menuBar, &TMenuBar::exitRequested, this, &Taif::exitApp);
    connect(menuBar, &TMenuBar::runRequested, this, &Taif::runAlif);
    connect(menuBar, &TMenuBar::aboutRequested, this, &Taif::aboutTaif);
    connect(menuBar, &TMenuBar::updateRequested, this, &Taif::checkForUpdates);
    connect(menuBar, &TMenuBar::openFolderRequested, this, &Taif::handleOpenFolderMenu);

    connect(menuBar, &TMenuBar::showAlifOutputRequested, this,
            [this]() { showAndRaiseDock(alifOutputDock); });
    connect(menuBar, &TMenuBar::showTerminalRequested, this,
            [this]() { showAndRaiseDock(terminalDock); });
    connect(menuBar, &TMenuBar::showProblemsRequested, this,
            &Taif::showProblemsWithWarning);

    const auto scheduleBottomToolActionStateSync = [this](const bool) {
        QTimer::singleShot(0, this, &Taif::syncBottomToolActionState);
    };
    connect(diagnosticsDock, &QDockWidget::visibilityChanged,
            this, scheduleBottomToolActionStateSync);
    connect(alifOutputDock, &QDockWidget::visibilityChanged,
            this, scheduleBottomToolActionStateSync);
    connect(terminalDock, &QDockWidget::visibilityChanged,
            this, scheduleBottomToolActionStateSync);
    syncBottomToolActionState();

    connect(menuBar, &TMenuBar::undoRequested, this, [this](){ if (auto e = currentEditor()) e->undo(); });
    connect(menuBar, &TMenuBar::redoRequested, this, [this](){ if (auto e = currentEditor()) e->redo(); });
    connect(menuBar, &TMenuBar::cutRequested, this, [this](){ if (auto e = currentEditor()) e->cut(); });
    connect(menuBar, &TMenuBar::copyRequested, this, [this](){ if (auto e = currentEditor()) e->copy(); });
    connect(menuBar, &TMenuBar::pasteRequested, this, [this](){ if (auto e = currentEditor()) e->paste(); });
    connect(menuBar, &TMenuBar::findRequested, this, &Taif::showFindBar);
    connect(menuBar, &TMenuBar::replaceRequested, this, &Taif::showReplaceBar);
    connect(menuBar, &TMenuBar::goToLineRequested, this, &Taif::goToLine);
    connect(menuBar, &TMenuBar::toggleCommentRequested, this, [this](){ if (auto e = currentEditor()) e->toggleComment(); });
    connect(menuBar, &TMenuBar::duplicateLineRequested, this, [this](){ if (auto e = currentEditor()) e->duplicateLine(); });
    connect(menuBar, &TMenuBar::moveLineUpRequested, this, [this](){ if (auto e = currentEditor()) e->moveLineUp(); });
    connect(menuBar, &TMenuBar::moveLineDownRequested, this, [this](){ if (auto e = currentEditor()) e->moveLineDown(); });

    connect(tabWidget, &QTabWidget::currentChanged, this, &Taif::updateWindowTitle);
    connect(tabWidget, &QTabWidget::currentChanged, this, &Taif::onCurrentTabChanged);
    connect(breadcrumbBar, &TBreadcrumbBar::fileSegmentActivated,
            this, &Taif::revealBreadcrumbPath);
    connect(breadcrumbBar, &TBreadcrumbBar::symbolSegmentActivated, this,
            [this](const SourceRange range) {
                if (TEditor* const editor = currentEditor()) {
                    editor->navigateToDiagnosticRange(range);
                }
            });

    connect(searchBar, &SearchPanel::findText, this, &Taif::findText);
    connect(searchBar, &SearchPanel::findNext, this, &Taif::findNextText);
    connect(searchBar, &SearchPanel::findPrevious, this, &Taif::findPrevText);
    connect(searchBar, &SearchPanel::closed, this, &Taif::hideFindBar);
    connect(searchBar, &SearchPanel::replaceOne, this, &Taif::replaceOne);
    connect(searchBar, &SearchPanel::replaceAll, this, &Taif::replaceAll);
}

void Taif::setupStyle() {
    QString styleSheet = R"(
        QMainWindow {
            background-color: #0f172a;
            font-size: 13px;
        }

        /* --- تصميم شريط القوائم --- */
        QMenuBar {
            font-family: "Tajawal", "Noto Kufi Arabic";
            background-color: #0f172a;
            color: #f1f5f9;
            padding: 3px;
            font-size: 14px;
        }
        QMenuBar::item {
            background-color: transparent;
            padding: 6px 12px;
            border-radius: 4px;
        }
        QMenuBar::item:selected {
            background-color: #1e293b;
            color: #3b82f6;
        }

        /* --- قوائم شريط القوائم --- */
        QMenu {
            font-family: "Tajawal", "Noto Kufi Arabic";
            background-color: #1e293b;
            border: 1px solid #334155;
            color: #f1f5f9;
            border-radius: 6px;
            padding: 4px;
        }
        QMenu::item {
            background-color: transparent;
            border-radius: 4px;
            padding: 6px 21px 6px 18px;
        }
        QMenu::item:selected {
            background-color: #3b82f6;
            color: #ffffff;
        }
        QMenu::separator {
            height: 1px;
            background: #334155;
            margin: 4px 8px;
        }

        /* --- تصميم شريط الأدوات --- */
        QToolBar {
            background-color: #0f172a;
            border: none;
            padding: 6px;
            spacing: 8px; /* مسافة بين كل زر والآخر */
        }

        /* تصميم أزرار شريط الأدوات */
        QToolBar QToolButton {
            background-color: transparent;
            color: #f1f5f9;
            border: none;
            border-radius: 6px;

            /*  أهم جزء: تحديد حجم مربع الزر ليكون كبيراً ومربعاً */
            min-width: 40px;
            max-width: 40px;
            min-height: 40px;
            max-height: 40px;
        }

        QToolBar QToolButton:hover {
            background-color: #1e293b;
        }

        QToolBar QToolButton:pressed {
            background-color: #334155;
        }

        QToolBar QToolButton:checked {
            background-color: #3b82f6; /* اللون الأزرق */
        }

        /* --- تصميم شجرة المسارات--- */
        QTreeView {
            background-color: #0f172a;
            border: none;
            color: #94a3b8;
            font-size: 14px;
            outline: none;
        }
        QTreeView::item {
            padding: 4px 2px;
        }
        QTreeView::item:hover {
            background-color: #1e293b;
            color: #f1f5f9;
        }
        QTreeView::item:selected {
            background-color: #1e293b;
            color: #3b82f6;
        }

        /* --- تصميم الفاصل --- */
        QSplitter::handle {
            background-color: #094771;
            width: 1px;
        }

        /* --- تصميم التبويبات --- */
        QTabWidget::pane {
            border: none;
            background-color: #0f172a;
        }
        QTabBar { /* شريط التبويبات */
            background-color: #0f172a;
            qproperty-drawBase: 0;
        }
        QTabBar::tab {
            background: #0f172a;
            color: #94a3b8;
            padding: 3px 9px;
            min-width: 100px;
        }
        QTabBar::tab:selected {
            background: #0f172a;
            color: #3b82f6;
            border-bottom: 2px solid #3b82f6;
        }
        QTabBar::tab:hover:!selected {
            background: #334466;
            color: #f1f5f9;
        }
        QTabBar::close-button {
            image: url(:/icons/resources/close.svg);
            background: transparent;
            border-radius: 2px;
            padding: 1px;
            margin: 0px;
        }
        QTabBar::close-button:hover {
            background: #ef4444;
        }

        /* --- شريط الحالة --- */
        QStatusBar {
            background-color: #0f172a;
            color: #64748b;
            border-top: 1px solid #1e293b;
            font-size: 11px;
            padding-left: 10px;
        }
        QStatusBar::item {
            border: none;
        }

        /* --- نوافذ قابلة للدكن --- */
        QDockWidget {
            background-color: #0f172a;
            color: #e2e8f0;
            font-family: "Tajawal", "Noto Kufi Arabic";
            border-top: 1px solid #334155;
            padding: 1px;
        }
        QDockWidget::title {
            background-color: #1e293b;
            color: #e2e8f0;
            text-align: left;
            padding: 9px;
            border-bottom: 1px solid #334155;
        }
        QDockWidget::close-button,
        QDockWidget::float-button {
            background: transparent;
            border: none;
            border-radius: 3px;
            padding: 1px;
            margin: 8px;
            icon-size: 0px;
            width: 0px;
            height: 0px;
            subcontrol-position: top right;
            position: absolute;
        }
        QDockWidget::close-button {
            image: url(:/icons/resources/close.svg);
            top: 9px; right: 7px; bottom: 9px;
        }
        QDockWidget::float-button {
            image: url(:/icons/resources/picture-in-picture.svg);
            top: 9px; right: 35px; bottom: 9px;
        }
        QDockWidget::close-button:hover {
            background-color: #ef4444;
        }
        QDockWidget::float-button:hover {
            background-color: #334466;
        }
    )";
    setStyleSheet(styleSheet);
}

void Taif::setupTimers() {
    // file watcher timer
    saveSuppressTimer = new QTimer(this);
    saveSuppressTimer->setSingleShot(true);
    connect(saveSuppressTimer, &QTimer::timeout, this, [this]{ savingFromApp = false; });
}

void Taif::closeEvent(QCloseEvent* const event)
{
    if (recoveryCloseFlushPending) {
        event->ignore();
        return;
    }

    if (!recoveryCloseFlushAcknowledged) {
        for (int index = 0; index < tabWidget->count(); ++index) {
            if (auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index))) {
                if (!prepareEditorForClose(editor)) {
                    emit closeRejected();
                    event->ignore();
                    return;
                }
            }
        }

        if (recoveryCoordinator != nullptr) {
            recoveryCloseFlushPending = true;
            // Keep the document immutable until the flush result is known; an
            // edit made during the close grace period would otherwise race the
            // acknowledged final snapshot.
            setEnabled(false);
            flushRecoverySnapshots();
            event->ignore();
            return;
        }
    }

    event->accept();
}

bool Taif::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_F6) {
            toggleConsole();
            return true;
        }
    }
    return QMainWindow::eventFilter(object, event);
}

void Taif::goToLine()
{
    TEditor *editor = currentEditor();
    if (!editor) return;

    // أقصى رقم هو عدد أسطر الملف الحالي
    const int maxLine = editor->blockCount();

    QDialog dlg(this);
    dlg.setWindowTitle("الذهاب إلى سطر");
    dlg.setFixedWidth(300);
    dlg.setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    QLabel *label = new QLabel(QString("أدخل رقم السطر (1 - %1):").arg(maxLine), &dlg);

    QSpinBox *spin = new QSpinBox(&dlg);
    spin->setRange(1, maxLine);
    spin->setValue(editor->textCursor().blockNumber() + 1);
    spin->setKeyboardTracking(true);
    spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    spin->setFocus();

    QDialogButtonBox *buttonBox = new QDialogButtonBox(&dlg);
    QPushButton *okButton = buttonBox->addButton("إذهب", QDialogButtonBox::AcceptRole);
    QPushButton *cancelButton = buttonBox->addButton("إلغاء", QDialogButtonBox::RejectRole);
    okButton->setDefault(true);

    connect(buttonBox, &QDialogButtonBox::accepted,
            &dlg, &QDialog::accept);

    connect(buttonBox, &QDialogButtonBox::rejected,
            &dlg, &QDialog::reject);

    layout->addWidget(label);
    layout->addWidget(spin);
    layout->addWidget(buttonBox);

    // نمط مطابق لنمط المحرر (داكن + خط عربي)
    dlg.setStyleSheet(R"(
        QDialog {
            background-color: #0f172a;
            font-family: "Tajawal", "Noto Kufi Arabic";
        }
        QLabel {
            color: #f1f5f9;
            font-size: 13px;
        }
        QSpinBox {
            background-color: #1e293b;
            color: #f1f5f9;
            border: 1px solid #334155;
            border-radius: 4px;
            padding: 6px 10px;
            font-size: 13px;
            selection-background-color: #3b82f6;
        }
        QSpinBox:focus {
            border: 1px solid #3b82f6;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            width: 20px;
            border-radius: 3px;
            background-color: #1e293b;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background-color: #1c7ed6;
        }
        QSpinBox::up-arrow {
            image: url(:/icons/resources/chevron-up.svg);
            width: 16px;
        }
        QSpinBox::down-arrow {
            image: url(:/icons/resources/chevron-down.svg);
            width: 16px;
        }
        QDialogButtonBox {
            spacing: 10px;
        }
        QDialogButtonBox QPushButton {
            background-color: #1e293b;
            color: #f1f5f9;
            border: 1px solid #334155;
            border-radius: 4px;
            padding: 6px 20px;
            font-family: "Tajawal", "Noto Kufi Arabic";
            font-size: 13px;
        }
        QDialogButtonBox QPushButton:hover {
            background-color: #334155;
        }
        QDialogButtonBox QPushButton:pressed {
            background-color: #0f172a;
        }
        QDialogButtonBox QPushButton:focus {
            border: 1px solid #3b82f6;
        }
    )");

    // زر الموافق بلون مميز (الأزرق)
    okButton->setStyleSheet(
        "QPushButton{background-color:#3b82f6; border:none; color:#ffffff;}"
        "QPushButton:hover{background-color:#2563eb;}"
        "QPushButton:pressed{background-color:#1d4ed8;}");

    if (dlg.exec() == QDialog::Accepted) {
        const int lineNumber = spin->value();
        QTextBlock block = editor->document()->findBlockByNumber(lineNumber - 1);

        if (block.isValid()) {
            QTextCursor cursor = editor->textCursor();

            // تحريك المؤشر الى الكتلة المختارة
            cursor.setPosition(block.position());

            // تحديث المحرر
            editor->setTextCursor(cursor);
            editor->centerCursor();
            editor->setFocus();
        }
    }
}

void Taif::showFindBar()
{
    if (TEditor *editor = currentEditor()) {
        searchBar->showIn(editor);
        searchBar->showReplaceRow(false);
        searchBar->setFocusToInput();
        // Keep the query when changing files, but always recompute its matches
        // against the newly active document as soon as the panel is reopened.
        performSearch(true, false);
        searchBar->setFocusToInput();
    }
}

void Taif::showReplaceBar()
{
    if (TEditor *editor = currentEditor()) {
        searchBar->showIn(editor);
        searchBar->showReplaceRow(true);
        searchBar->setFocusToInput();
        // The replace panel shares the same query and must refresh its search
        // results for the newly active document as well.
        performSearch(true, false);
        searchBar->setFocusToInput();
    }
}

void Taif::hideFindBar()
{
    if (searchBar != nullptr) {
        searchBar->detachFromHost();
    }
    searchBar->hide();
    clearSearchHighlights();
    if (TEditor *editor = currentEditor())
        editor->setFocus(Qt::OtherFocusReason);
}

void Taif::clearSearchHighlights() {
    if (TEditor* editor = currentEditor()) {
        editor->setExtraSelections({});
    }
}

static bool isWordCharacter(const QChar ch)
{
    return ch.isLetterOrNumber() || ch == u'_';
}

static bool isWholeWordMatch(const QString &text, const int start, const int length)
{
    const int end = start + length;
    const bool leftWord = start > 0 && isWordCharacter(text.at(start - 1));
    const bool rightWord = end < text.size() && isWordCharacter(text.at(end));
    return !leftWord && !rightWord;
}

static QList<QPair<int, int>> collectMatches(
    TEditor *editor, const QString &pattern, const bool caseSensitive,
    const bool wholeWord, const bool useRegex)
{
    QList<QPair<int, int>> matches;
    if (!editor || pattern.isEmpty())
        return matches;

    const QString text = editor->document()->toPlainText();
    if (useRegex) {
        QRegularExpression expression(
            pattern, caseSensitive ? QRegularExpression::NoPatternOption
                          : QRegularExpression::CaseInsensitiveOption);
        if (!expression.isValid())
            return matches;

        auto iterator = expression.globalMatch(text);
        while (iterator.hasNext()) {
            const QRegularExpressionMatch match = iterator.next();
            const int start = match.capturedStart();
            const int length = match.capturedLength();
            if (length == 0)
                continue;
            if (!wholeWord || isWholeWordMatch(text, start, length))
                matches.append({start, length});
        }
        return matches;
    }

    const Qt::CaseSensitivity sensitivity = caseSensitive
                                                ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int position = 0;
    while ((position = text.indexOf(pattern, position, sensitivity)) >= 0) {
        if (!wholeWord || isWholeWordMatch(text, position, pattern.size()))
            matches.append({position, pattern.size()});
        position += qMax(1, pattern.size());
    }
    return matches;
}

// Highlight every match in the document and optionally select the current one.
static void applyMatchHighlights(TEditor* editor, const QList<QPair<int, int>>& matches, int currentIdx)
{
    QList<QTextEdit::ExtraSelection> selections;
    const QColor matchColor(245, 158, 11, 70);     // dim amber for all matches
    const QColor currentColor(245, 158, 11, 160);   // bright amber for current

    for (int i = 0; i < matches.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        QTextCursor c = editor->textCursor();
        c.setPosition(matches[i].first);
        c.setPosition(matches[i].first + matches[i].second, QTextCursor::KeepAnchor);
        sel.cursor = c;
        sel.format.setBackground(i == currentIdx ? currentColor : matchColor);
        selections.append(sel);
    }
    editor->setExtraSelections(selections);
}


void Taif::performSearch(bool forward, bool next) {
    TEditor* editor = currentEditor();
    QString text = searchBar->searchText();
    if (!editor || text.isEmpty()) return;

    bool cs = searchBar->isCaseSensitive();
    bool ww = searchBar->isWholeWord();
    bool rx = searchBar->isRegex();

    // Validate regex before searching
    if (rx) {
        QRegularExpression re(text, cs ? QRegularExpression::NoPatternOption
                                      : QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) {
            searchBar->setMatchInfo(0, 0);
            searchBar->setNoMatchesFound(true);
            return;
        }
    }

    auto matches = collectMatches(editor, text, cs, ww, rx);

    if (matches.isEmpty()) {
        clearSearchHighlights();
        searchBar->setMatchInfo(0, 0);
        searchBar->setNoMatchesFound(true);
        return;
    }

    const QTextCursor editorCursor = editor->textCursor();
    const int cursorStart = editorCursor.selectionStart();
    const int cursorEnd = editorCursor.selectionEnd();

    int currentIdx = 0;
    if (!next) {
        // A new search or changed option must not advance an already selected match.
        // QTextCursor::position() is the selection end, so using it directly would
        // incorrectly move to the following match when Aa/كلمة is clicked.
        const int selectionStart = editorCursor.selectionStart();
        const int selectionEnd = editorCursor.selectionEnd();
        bool selectedMatchFound = false;
        for (int i = 0; i < matches.size(); ++i) {
            if (matches.at(i).first == selectionStart &&
                matches.at(i).first + matches.at(i).second == selectionEnd) {
                currentIdx = i;
                selectedMatchFound = true;
                break;
            }
        }

        if (!selectedMatchFound) {
            // Initial search: select the first match at or after the caret.
            const int anchor = selectionStart;
            currentIdx = 0;
            for (int i = 0; i < matches.size(); ++i) {
                if (matches.at(i).first >= anchor) {
                    currentIdx = i;
                    break;
                }
                currentIdx = i;
            }
        }
    } else if (forward) {
        // Next: move strictly after the currently selected match.
        currentIdx = 0;
        bool foundNext = false;
        for (int i = 0; i < matches.size(); ++i) {
            if (matches.at(i).first >= cursorEnd) {
                currentIdx = i;
                foundNext = true;
                break;
            }
        }
        if (!foundNext)
            currentIdx = 0; // wrap to the first match
    } else {
        // Previous: move strictly before the currently selected match.
        currentIdx = matches.size() - 1;
        for (int i = matches.size() - 1; i >= 0; --i) {
            if (matches.at(i).first < cursorStart) {
                currentIdx = i;
                break;
            }
        }
    }

    applyMatchHighlights(editor, matches, currentIdx);
    QTextCursor matchCursor = editor->textCursor();
    matchCursor.setPosition(matches.at(currentIdx).first);
    matchCursor.setPosition(matches.at(currentIdx).first + matches.at(currentIdx).second,
                            QTextCursor::KeepAnchor);
    editor->setTextCursor(matchCursor);
    editor->ensureCursorVisible();
    searchBar->setMatchInfo(currentIdx + 1, matches.size());
    searchBar->setNoMatchesFound(false);
}

void Taif::findText()       { performSearch(true,  false); }
void Taif::findNextText()   { performSearch(true,  true); }
void Taif::findPrevText()   { performSearch(false, true); }

void Taif::replaceOne() {
    TEditor* editor = currentEditor();
    QString search = searchBar->searchText();
    QString replace = searchBar->replaceText();
    if (!editor || search.isEmpty()) return;

    bool cs = searchBar->isCaseSensitive();
    bool ww = searchBar->isWholeWord();
    bool rx = searchBar->isRegex();

    if (rx) {
        QRegularExpression re(search, cs ? QRegularExpression::NoPatternOption
                                        : QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) return;
    }

    auto matches = collectMatches(editor, search, cs, ww, rx);
    if (matches.isEmpty()) return;

    // Find the match that contains the cursor or starts at the selection.
    int cursorPos = editor->textCursor().selectionStart();
    int targetIdx = -1;
    for (int i = 0; i < matches.size(); ++i) {
        const int matchStart = matches[i].first;
        const int matchEnd = matchStart + matches[i].second;
        if (matchStart <= cursorPos && cursorPos < matchEnd) {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx == -1) targetIdx = 0;

    // Perform the replacement
    QString replacement;
    if (rx) {
        QRegularExpression re(search, cs ? QRegularExpression::NoPatternOption
                                        : QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = re.match(editor->document()->toPlainText().mid(matches[targetIdx].first, matches[targetIdx].second));
        replacement = match.hasMatch() ? match.captured(0).replace(re, replace) : replace;
    } else {
        replacement = replace;
    }

    QTextCursor c = editor->textCursor();
    c.setPosition(matches[targetIdx].first);
    c.setPosition(matches[targetIdx].first + matches[targetIdx].second, QTextCursor::KeepAnchor);
    c.insertText(replacement);

    // Re-search to update highlights and move to next match
    performSearch(true, true);
}

void Taif::replaceAll() {
    TEditor* editor = currentEditor();
    QString search = searchBar->searchText();
    QString replace = searchBar->replaceText();
    if (!editor || search.isEmpty()) return;

    bool cs = searchBar->isCaseSensitive();
    bool ww = searchBar->isWholeWord();
    bool rx = searchBar->isRegex();

    if (rx) {
        QRegularExpression re(search, cs ? QRegularExpression::NoPatternOption
                                        : QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) return;
    }

    auto matches = collectMatches(editor, search, cs, ww, rx);
    if (matches.isEmpty()) return;

    // Group the complete Replace All operation into one undo action.
    QTextCursor editCursor(editor->document());
    editCursor.beginEditBlock();

    if (rx) {
        QRegularExpression re(search, cs ? QRegularExpression::NoPatternOption
                                        : QRegularExpression::CaseInsensitiveOption);
        const QString text = editor->toPlainText();

        // Replace only the matches collected above so whole-word filtering is respected.
        // Process from end to start to keep document positions valid.
        for (int i = matches.size() - 1; i >= 0; --i) {
            const QString matchedText = text.mid(matches[i].first, matches[i].second);
            const QRegularExpressionMatch match = re.match(matchedText);
            const QString replacement = match.hasMatch()
                                            ? match.captured(0).replace(re, replace)
                                            : replace;

            QTextCursor c = editor->textCursor();
            c.setPosition(matches[i].first);
            c.setPosition(matches[i].first + matches[i].second, QTextCursor::KeepAnchor);
            c.insertText(replacement);
        }
    } else {
        // Replace from end to start to keep positions valid
        for (int i = matches.size() - 1; i >= 0; --i) {
            QTextCursor c = editor->textCursor();
            c.setPosition(matches[i].first);
            c.setPosition(matches[i].first + matches[i].second, QTextCursor::KeepAnchor);
            c.insertText(replace);
        }
    }

    editCursor.endEditBlock();

    // Update highlights after replacement
    auto newMatches = collectMatches(editor, search, cs, ww, rx);
    if (newMatches.isEmpty()) {
        clearSearchHighlights();
        searchBar->setMatchInfo(0, 0);
        searchBar->setNoMatchesFound(true);
    } else {
        applyMatchHighlights(editor, newMatches, 0);
        QTextCursor c = editor->textCursor();
        c.setPosition(newMatches[0].first);
        c.setPosition(newMatches[0].first + newMatches[0].second, QTextCursor::KeepAnchor);
        editor->setTextCursor(c);
        editor->ensureCursorVisible();
        searchBar->setMatchInfo(1, newMatches.size());
        searchBar->setNoMatchesFound(false);
    }
}

void Taif::syncBottomToolActionState()
{
    if (menuBar == nullptr) {
        return;
    }

    menuBar->setOpenViewToolActions(
        alifOutputDock != nullptr && alifOutputDock->isVisible(),
        terminalDock != nullptr && terminalDock->isVisible(),
        diagnosticsDock != nullptr && diagnosticsDock->isVisible());
}

void Taif::showProblemsWithWarning() { //* review
    // The problems window is still under development and may report incorrect
    // or misleading errors. Warn the user before opening it.
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(QStringLiteral("تحذير"));
    msgBox.setText(QStringLiteral(" هذا النظام تحت التطوير وهو غير مكتمل بعد \n"
                                  "وقد يظهر رسائل خطأ غير صحيحة او مضللة لذلك "
                                  "يرجى إستخدامه بحذر"));
    msgBox.addButton(QStringLiteral("موافق"), QMessageBox::AcceptRole);
    msgBox.setStyleSheet(
        QStringLiteral("QMessageBox {background: #0f172a; color: #f1f5f9;} "
                       "QLabel {min-height: 90px; color: #f1f5f9;}"));
    msgBox.exec();

    showAndRaiseDock(diagnosticsDock);
}

void Taif::showAndRaiseDock(QDockWidget* const dock) {
    if (dock == nullptr) {
        return;
    }

    if (dock == terminalDock || dock == alifOutputDock) {
        DockableConsoleToolFactory::ensureTabifiedWith(this, diagnosticsDock, dock);
    }
    if (dock == terminalDock && systemTerminal != nullptr) {
        QString workingDirectory = folderPath;
        if (!QDir(workingDirectory).exists()) {
            if (TEditor* const editor = currentEditor(); editor != nullptr && !editor->filePath.isEmpty()) {
                workingDirectory = QFileInfo(editor->filePath).absolutePath();
            }
        }
        if (!QDir(workingDirectory).exists()) {
            // A Linux desktop entry normally launches the editor with the
            // user's home directory as cwd. Keep the embedded terminal beside
            // the installed editor when no project or document directory is
            // available immediately after installation.
            workingDirectory = QCoreApplication::applicationDirPath();
        }
        systemTerminal->setTerminalWorkingDirectory(workingDirectory);
    }
    DockableConsoleToolFactory::showAndActivate(dock);
    QTimer::singleShot(0, this, &Taif::syncBottomToolActionState);
}

void Taif::toggleConsole() {
    if (terminalDock == nullptr) {
        return;
    }
    if (DockableConsoleToolFactory::isRenderedTab(terminalDock)) {
        terminalDock->hide();
        syncBottomToolActionState();
        if (TEditor* editor = currentEditor()) {
            editor->setFocus();
        }
        return;
    }
    showAndRaiseDock(terminalDock);
}

/* ----------------------------------- File Menu Button ----------------------------------- */

Taif::SaveDecision Taif::requestSaveDecision(TEditor* const editor) const {
    if (editor == nullptr || !editor->document()->isModified()) {
        return SaveDecision::Discard;
    }

    QMessageBox messageBox;
    messageBox.setWindowTitle(QStringLiteral("طيف"));
    messageBox.setText(QStringLiteral("تم التعديل على الملف.\nهل تريد حفظ التغييرات؟"));
    auto* const saveButton = messageBox.addButton(QStringLiteral("حفظ"), QMessageBox::AcceptRole);
    auto* const discardButton = messageBox.addButton(QStringLiteral("تجاهل"), QMessageBox::DestructiveRole);
    auto* const cancelButton = messageBox.addButton(QStringLiteral("إلغاء"), QMessageBox::RejectRole);
    messageBox.setDefaultButton(cancelButton);

    // نمط مطابق لنمط المحرر (داكن + خط عربي)
    messageBox.setStyleSheet(R"(
        QMessageBox {
            background-color: #0f172a;
            font-family: "Tajawal", "Noto Kufi Arabic";
        }
        QLabel {
            color: #f1f5f9;
            font-size: 13px;
        }
        QDialogButtonBox {
            spacing: 10px;
        }
        QDialogButtonBox QPushButton {
            background-color: #1e293b;
            color: #f1f5f9;
            border: 1px solid #334155;
            border-radius: 4px;
            padding: 6px 20px;
            font-family: "Tajawal", "Noto Kufi Arabic";
            font-size: 13px;
        }
        QDialogButtonBox QPushButton:hover {
            background-color: #334155;
        }
        QDialogButtonBox QPushButton:pressed {
            background-color: #0f172a;
        }
        QDialogButtonBox QPushButton:focus {
            border: 1px solid #3b82f6;
        }
    )");

    QFont messageFont = font();
    messageFont.setPointSize(10);
    saveButton->setFont(messageFont);
    discardButton->setFont(messageFont);
    cancelButton->setFont(messageFont);
    messageBox.exec();

    if (messageBox.clickedButton() == saveButton) {
        return SaveDecision::Save;
    }
    if (messageBox.clickedButton() == discardButton) {
        return SaveDecision::Discard;
    }
    return SaveDecision::Cancel;
}

bool Taif::prepareEditorForClose(TEditor* const editor)
{
    switch (requestSaveDecision(editor)) {
    case SaveDecision::Save:
        return saveEditor(editor);
    case SaveDecision::Discard:
        editor->removeBackupFile();
        return true;
    case SaveDecision::Cancel:
        return false;
    }
    return false;
}

void Taif::newFile() {
    if (TEditor* const editor = currentEditor()) {
        if (!prepareEditorForClose(editor)) {
            return;
        }
    }

    TEditor *newEditor = new TEditor(setting, this);
    registerEditorRecovery(newEditor);
    tabWidget->addTab(newEditor, "غير معنون");
    tabWidget->setCurrentWidget(newEditor);

    connect(newEditor, &TEditor::openRequest, this, [this](QString filePath){this->openFile(filePath);});
    connectEditorDiagnostics(newEditor);
    connect(newEditor->document(), &QTextDocument::modificationChanged, this,
            [this, newEditor](const bool modified) {
                onEditorModificationChanged(newEditor, modified);
            });
    updateWindowTitle();
}

void Taif::addWatch(const QString &filePath)
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) return;

    if (!fileWatcher) {
        fileWatcher = new QFileSystemWatcher(this);
        connect(fileWatcher, &QFileSystemWatcher::fileChanged, this, &Taif::onFileChanged);
    }

    const QString watchPath = QFileInfo(filePath).absoluteFilePath();
    if (!fileWatcher->files().contains(watchPath)) {
        fileWatcher->addPath(watchPath);
    }
}

void Taif::removeWatch(const QString &filePath)
{
    if (filePath.isEmpty() || !fileWatcher) return;
    const QString watchPath = QFileInfo(filePath).absoluteFilePath();
    if (fileWatcher->files().contains(watchPath)) {
        fileWatcher->removePath(watchPath);
    }
}

QString Taif::pathForEditor(TEditor *editor) const
{
    if (!editor) return QString();
    return QFileInfo(editor->property("filePath").toString()).absoluteFilePath();
}

void Taif::reloadEditor(TEditor *editor)
{
    const QString filePath = editor->property("filePath").toString();
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // قد يكون الملف قيد الكتابة أو تم حذفه مؤقتًا؛ سنعيد مراقبته
        addWatch(filePath);
        return;
    }
    QTextStream in(&file);
    const QString content = in.readAll();
    file.close();

    const int index = tabWidget->indexOf(editor);
    if (index == -1) return;

    // حفظ موقع المؤشر النسبي قدر الإمكان
    const int cursorPos = editor->textCursor().position();

    editor->document()->setModified(false);
    editor->setPlainText(content);

    // استعادة أقرب موضع للمؤشر داخل النطاق الجديد
    const int newPos = qBound(0, cursorPos, editor->document()->characterCount() - 1);
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(newPos);
    editor->setTextCursor(cursor);

    if (index == tabWidget->currentIndex()) {
        updateCursorPosition();
    }
    updateWindowTitle();

    // QFileSystemWatcher يُزيل المسار تلقائيًا بعد الحدث، لذا نعيده
    addWatch(filePath);
}

void Taif::onFileChanged(const QString &path)
{
    // تجاهل التغيّرات الناتجة عن حفظنا نحن داخل البرنامج
    if (savingFromApp) {
        addWatch(path);
        return;
    }

    const QString watchPath = QFileInfo(path).absoluteFilePath();

    // QFileSystemWatcher يُزيل المسار بعد الحدث، نعيد إضافته للمتابعة
    addWatch(watchPath);

    for (int i = 0; i < tabWidget->count(); ++i) {
        TEditor* editor = qobject_cast<TEditor*>(tabWidget->widget(i));
        if (!editor) continue;
        if (pathForEditor(editor) != watchPath) continue;

        if (editor->document()->isModified()) {
            if (i == tabWidget->currentIndex()) {
                // The editor has unsaved changes. Prompt the user to decide whether
                // to discard their edits and reload from disk, or keep their version.
                QMessageBox msgBox(this);
                msgBox.setWindowTitle("طيف");
                msgBox.setText(QString("<div align='right'>%1"
                                       "<br><br>"
                                       "تم تعديل هذا الملف في برنامج آخر."
                                       "<br>"
                                       "هل تريد إعادة تحميل النسخة المعدلة من القرص؟</div>").arg(watchPath));
                QPushButton *reloadBtn = msgBox.addButton("إعادة تحميل", QMessageBox::AcceptRole);
                /*QPushButton *keepBtn =*/ msgBox.addButton("أبقي نسختي", QMessageBox::RejectRole);
                // نمط مطابق لنمط المحرر (داكن + خط عربي)
                msgBox.setStyleSheet(R"(
                    QMessageBox {
                        background-color: #0f172a;
                        font-family: "Tajawal", "Noto Kufi Arabic";
                    }
                    QLabel {
                        color: #f1f5f9;
                        font-size: 13px;
                    }
                    QDialogButtonBox {
                        spacing: 10px;
                    }
                    QDialogButtonBox QPushButton {
                        background-color: #1e293b;
                        color: #f1f5f9;
                        border: 1px solid #334155;
                        border-radius: 4px;
                        padding: 6px 20px;
                        font-family: "Tajawal", "Noto Kufi Arabic";
                        font-size: 13px;
                    }
                    QDialogButtonBox QPushButton:hover {
                        background-color: #334155;
                    }
                    QDialogButtonBox QPushButton:pressed {
                        background-color: #0f172a;
                    }
                    QDialogButtonBox QPushButton:focus {
                        border: 1px solid #3b82f6;
                    }
                )");

                msgBox.exec();
                if (msgBox.clickedButton() == reloadBtn) {
                    reloadEditor(editor);
                }
            }
        } else {
            reloadEditor(editor);
        }
    }
}

void Taif::openFile(QString filePath)
{
    if (TEditor* const current = currentEditor()) {
        if (!prepareEditorForClose(current)) {
            return;
        }
    }

    if (filePath.isEmpty()) {
        filePath = QFileDialog::getOpenFileName(
            this, QStringLiteral("فتح ملف"), {},
            QStringLiteral("ملف ألف (*.alif *.aliflib);;كل الملفات (*)"));
    }
    if (filePath.isEmpty()) {
        return;
    }

    QString failureMessage;
    if (!openDocumentFile(filePath, true, true, true, &failureMessage)) {
        QMessageBox::warning(this, QStringLiteral("خطأ"),
                             failureMessage.isEmpty()
                                 ? QStringLiteral("لا يمكن فتح الملف.")
                                 : failureMessage);
    }
}

const QByteArray detectFileEncoding(const QByteArray &bytes) {
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return "UTF-8";
    }
    if (bytes.size() >= 2 && (uchar(bytes[0]) == 0xFF || uchar(bytes[0]) == 0xFE)) {
        const bool littleEndian = (uchar(bytes[0]) == 0xFF);
        return littleEndian ? QByteArray("UTF-16LE") : QByteArray("UTF-16BE");
    }
    if (!bytes.isEmpty()) {
        QStringDecoder utf8Dec(QStringEncoder::Utf8);
        const QString decoded = utf8Dec.decode(bytes);
        if (utf8Dec.hasError() == false && !decoded.isEmpty()) {
            return "UTF-8";
        }
    }
    for (const QByteArray& name : {"Windows-1256", "ISO 8859-6", "CP866",
                                   "Windows-1252", "ISO 8859-1"}) {
        QStringDecoder dec(name);
        if (dec.isValid() == false) {
            continue;
        }
        const QString decoded = dec.decode(bytes);
        int replacementRun = 0;
        bool mostlyValid = true;
        for (const QChar ch : decoded) {
            if (ch.unicode() == 0xFFFD) {
                ++replacementRun;
                if (decoded.size() > 0 && replacementRun > decoded.size() * 5 / 100) {
                    mostlyValid = false;
                    break;
                }
            } else {
                replacementRun = 0;
            }
        }
        if (mostlyValid && !decoded.isEmpty()) {
            return name;
        }
    }
    return "UTF-8";
}

bool Taif::openDocumentFile(const QString& requestedPath,
                            const bool promptForBackupRecovery,
                            const bool activateTab,
                            const bool updateRecentFiles,
                            QString* const failureMessage)
{
    const QString filePath = SessionStore::normalizePath(requestedPath);
    if (filePath.isEmpty()) {
        if (failureMessage != nullptr) {
            *failureMessage = QStringLiteral("مسار الملف غير صالح.");
        }
        return false;
    }

    for (int index = 0; index < tabWidget->count(); ++index) {
        auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index));
        if (editor != nullptr
            && SessionStore::normalizePath(editor->property("filePath").toString()) == filePath) {
            if (activateTab) {
                tabWidget->setCurrentIndex(index);
            }
            return true;
        }
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (failureMessage != nullptr) {
            *failureMessage = QStringLiteral("لا يمكن فتح الملف: %1").arg(filePath);
        }
        return false;
    }
    const QByteArray rawBytes = file.readAll();
    file.close();

    const QByteArray detectedEncodingName = detectFileEncoding(rawBytes);
    QStringDecoder decoder(detectedEncodingName);
    const QString content = decoder.decode(rawBytes);

    auto* const newEditor = new TEditor(setting, this);
    registerEditorRecovery(newEditor);
    newEditor->setDocumentEncoding(QString::fromLatin1(detectedEncodingName));
    newEditor->setPlainText(content);
    newEditor->filePath = filePath;
    newEditor->setProperty("filePath", filePath);
#if defined(Q_OS_WIN)
    newEditor->setDocumentLineEnding(EditorStatusSnapshot::LineEnding::Crlf);
#else
    newEditor->setDocumentLineEnding(EditorStatusSnapshot::LineEnding::Lf);
#endif
    newEditor->document()->setModified(false);
    newEditor->removeBackupFile();

    const QString backupPath = filePath + QStringLiteral(".~");
    if (promptForBackupRecovery && QFile::exists(backupPath)) {
        const QMessageBox::StandardButton reply = QMessageBox::warning(
            this, QStringLiteral("استعادة ملف"),
            QStringLiteral("يبدو أن البرنامج أُغلق بشكل غير متوقع.\n"
                           "يوجد نسخة محفوظة تلقائيًا أحدث من الملف الأصلي.\n\n"
                           "هل تريد استعادتها؟"),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QFile backup(backupPath);
            if (backup.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream backupStream(&backup);
                newEditor->setPlainText(backupStream.readAll());
                newEditor->document()->setModified(true);
            }
        } else {
            QFile::remove(backupPath);
        }
    }

    connect(newEditor, &TEditor::openRequest, this,
            [this](const QString& path) { openFile(path); });
    connect(newEditor->document(), &QTextDocument::modificationChanged, this,
            [this, newEditor](const bool modified) {
                onEditorModificationChanged(newEditor, modified);
            });
    connectEditorDiagnostics(newEditor);
    // connectEditorActionState(newEditor); //* review

    const QFileInfo fileInfo(filePath);
    const int tabIndex = tabWidget->addTab(newEditor, fileInfo.fileName());
    tabWidget->setTabToolTip(tabIndex, filePath);
    if (activateTab) {
        tabWidget->setCurrentIndex(tabIndex);
    }

    if (updateRecentFiles) {
        QSettings settings(QStringLiteral("Alif"), QStringLiteral("Taif"));
        QStringList recentFiles = settings.value(QStringLiteral("RecentFiles")).toStringList();
        const int recentFilesLimit = PreferencesStore::load().recentFilesLimit;
        recentFiles.removeAll(filePath);
        if (recentFilesLimit > 0) {
            recentFiles.prepend(filePath);
            while (recentFiles.size() > recentFilesLimit) {
                recentFiles.removeLast();
            }
        } else {
            recentFiles.clear();
        }
        settings.setValue(QStringLiteral("RecentFiles"), recentFiles);
    }

    // Begin watching the file for external changes now that it is loaded.
    addWatch(filePath);

    updateWindowTitle();
    if (activateTab) {
        refreshBreadcrumbs();
    }
    return true;
}

SessionRestoreResult Taif::restoreSession(const SavedSession& savedSession)
{
    const SavedSession session = SessionStore::normalize(savedSession);
    SessionRestoreResult result;
    QSet<QString> openedPathKeys;

    for (const QString& path : session.filePaths) {
        const QString normalizedPath = SessionStore::normalizePath(path);
        const QString pathKey = normalizedPath.toCaseFolded();
        if (normalizedPath.isEmpty() || openedPathKeys.contains(pathKey)) {
            continue;
        }
        openedPathKeys.insert(pathKey);

        QString failureMessage;
        if (openDocumentFile(normalizedPath, false, false, true, &failureMessage)) {
            result.openedFilePaths.append(normalizedPath);
        } else {
            result.unavailableFilePaths.append(normalizedPath);
        }
    }

    const QString activePath = SessionStore::normalizePath(session.activeFilePath);
    int activeIndex = -1;
    for (int index = 0; index < tabWidget->count(); ++index) {
        auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index));
        if (editor != nullptr
            && SessionStore::normalizePath(editor->property("filePath").toString()) == activePath) {
            activeIndex = index;
            break;
        }
    }
    if (activeIndex >= 0) {
        tabWidget->setCurrentIndex(activeIndex);
    } else if (tabWidget->count() > 0) {
        tabWidget->setCurrentIndex(0);
    } else {
        newFile();
    }

    refreshDiagnosticsPanel();
    // updateEditActionState(); //* review
    refreshBreadcrumbs();
    return result;
}

void Taif::loadFolder(const QString& requestedFolderPath) {
    const QString normalizedFolderPath = SessionStore::normalizePath(requestedFolderPath);
    if (!normalizedFolderPath.isEmpty() && QDir(normalizedFolderPath).exists()) {
        folderPath = normalizedFolderPath;
        projectExplorer->setProjectRoot(folderPath);
        // gitPanel->setProjectRoot(folderPath); //* Git Section deactivated, active when enable this system
        projectExplorer->setVisible(true);
        toggleSidebarAction->setChecked(true);
    } else {
        folderPath.clear();
        projectExplorer->setProjectRoot({});
        // gitPanel->setProjectRoot({}); //* Git Section deactivated, active when enable this system
        projectExplorer->setVisible(false);
        toggleSidebarAction->setChecked(false);
    }
    refreshBreadcrumbs();
}

void Taif::handleOpenFolderMenu()
{
    QString folderPath = QFileDialog::getExistingDirectory(this, "اختر مجلد", QDir::homePath());
    if (folderPath.isEmpty()) return;

    loadFolder(folderPath);

}

void Taif::toggleSidebar() {
    const bool shouldBeVisible = !projectExplorer->isVisible();
    projectExplorer->setVisible(shouldBeVisible);
    toggleSidebarAction->setChecked(shouldBeVisible);

    if (shouldBeVisible) {
        projectExplorer->setFocus();
    }
}

void Taif::onProjectFileActivated(const QString& filePath) {
    QString failureMessage;
    if (!openDocumentFile(filePath, true, true, true, &failureMessage)) {
        QMessageBox::warning(this, QStringLiteral("خطأ"),
                             failureMessage.isEmpty() ? QStringLiteral("لا يمكن فتح الملف.")
                                                      : failureMessage);
    }
}

void Taif::createProjectFile(const QString& directoryPath, const QString& name) {
    const ProjectFileOperationResult result = ProjectFileOperations::createFile(folderPath, directoryPath, name);
    presentProjectOperationResult(result);
    if (result.succeeded) {
        projectExplorer->selectPath(result.destinationPath);
        onProjectFileActivated(result.destinationPath);
    }
}

void Taif::createProjectFolder(const QString& directoryPath, const QString& name)
{
    const ProjectFileOperationResult result = ProjectFileOperations::createFolder(folderPath, directoryPath, name);
    presentProjectOperationResult(result);
    if (result.succeeded) {
        projectExplorer->selectPath(result.destinationPath);
    }
}

void Taif::renameProjectPath(const QString& sourcePath, const QString& newName)
{
    if (hasOpenEditorAtOrBelow(sourcePath)) {
        const auto reply = QMessageBox::question(this, QStringLiteral("إعادة تسمية عنصر مفتوح"),
                                                 QStringLiteral("يوجد ملف مفتوح داخل هذا المسار. سيُحدَّث مساره في المحرر بعد نجاح إعادة التسمية. هل تريد المتابعة؟"),
                                                 QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    const ProjectFileOperationResult result = ProjectFileOperations::renamePath(folderPath, sourcePath, newName);
    presentProjectOperationResult(result);
    if (!result.succeeded) {
        return;
    }
    const QString oldPrefix = ProjectFileOperations::normalizedPath(sourcePath);
    const QString newPrefix = ProjectFileOperations::normalizedPath(result.destinationPath);
    for (int index = 0; index < tabWidget->count(); ++index) {
        auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index));
        if (editor == nullptr) continue;
        const QString editorPath = ProjectFileOperations::normalizedPath(editor->property("filePath").toString());
        if (editorPath == oldPrefix || editorPath.startsWith(oldPrefix + QLatin1Char('/'), Qt::CaseInsensitive)) {
            const QString suffix = editorPath.mid(oldPrefix.size());
            const QString updatedPath = newPrefix + suffix;
            editor->filePath = updatedPath;
            editor->setProperty("filePath", updatedPath);
            tabWidget->setTabText(index, QFileInfo(updatedPath).fileName());
            tabWidget->setTabToolTip(index, updatedPath);
        }
    }
    projectExplorer->selectPath(result.destinationPath);
    projectExplorer->refresh();
    refreshBreadcrumbs();
}

void Taif::deleteProjectPath(const QString& sourcePath)
{
    const QString normalizedSource = ProjectFileOperations::normalizedPath(sourcePath);
    for (int index = tabWidget->count() - 1; index >= 0; --index) {
        auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index));
        if (editor == nullptr) continue;
        const QString editorPath = ProjectFileOperations::normalizedPath(editor->property("filePath").toString());
        if (editorPath == normalizedSource || editorPath.startsWith(normalizedSource + QLatin1Char('/'), Qt::CaseInsensitive)) {
            if (!prepareEditorForClose(editor)) {
                return;
            }
        }
    }
    const ProjectFileOperationResult result = ProjectFileOperations::moveToTrash(folderPath, sourcePath);
    presentProjectOperationResult(result);
    if (!result.succeeded) {
        const auto fallback = QMessageBox::warning(this, QStringLiteral("تعذر النقل إلى سلة المحذوفات"),
                                                   QStringLiteral("هل تريد الحذف نهائياً؟ لا يمكن التراجع عن هذا الإجراء."),
                                                   QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (fallback != QMessageBox::Yes) return;
        const ProjectFileOperationResult permanentResult = ProjectFileOperations::permanentlyDelete(folderPath, sourcePath);
        presentProjectOperationResult(permanentResult);
        if (!permanentResult.succeeded) return;
    }
    for (int index = tabWidget->count() - 1; index >= 0; --index) {
        auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index));
        if (editor == nullptr) continue;
        const QString editorPath = ProjectFileOperations::normalizedPath(editor->property("filePath").toString());
        if (editorPath == normalizedSource || editorPath.startsWith(normalizedSource + QLatin1Char('/'), Qt::CaseInsensitive)) {
            tabWidget->removeTab(index);
            editor->deleteLater();
        }
    }
    projectExplorer->refresh();
    refreshBreadcrumbs();
}

void Taif::revealProjectPath(const QString& sourcePath)
{
    presentProjectOperationResult(ProjectFileOperations::reveal(folderPath, sourcePath));
}

//* Git Section deactivated, active when enable this system
// void Taif::showGitPanel()
// {
//     if (gitPanel->projectRoot() != folderPath) {
//         gitPanel->setProjectRoot(folderPath);
//     }
//     gitDock->show();
//     gitDock->raise();
//     gitPanel->refresh();
// }

// void Taif::handleGitDestructiveOperation(const GitOperation operation, const QStringList& relativePaths)
// {
//     if (operation != GitOperation::Discard || folderPath.isEmpty() || relativePaths.isEmpty()) {
//         return;
//     }
//     for (const QString& relativePath : relativePaths) {
//         if (hasOpenEditorAtOrBelow(QDir(folderPath).filePath(relativePath))) {
//             QMessageBox::warning(this, QStringLiteral("حماية المحرر"),
//                                  QStringLiteral("أغلق أو احفظ الملف المفتوح قبل تجاهل تعديلاته من Git."));
//             return;
//         }
//     }
//     const auto reply = QMessageBox::warning(this, QStringLiteral("تجاهل تعديلات Git"),
//                                             QStringLiteral("سيجري فقدان التعديلات المحلية في الملفات المحددة نهائياً. هل تريد المتابعة؟"),
//                                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
//     if (reply == QMessageBox::Yes) {
//         projectExplorer->gitRepositoryService()->discard(relativePaths);
//     }
// }

// void Taif::handleGitPull()
// {
//     const GitRepositorySnapshot& snapshot = projectExplorer->gitRepositoryService()->snapshot();
//     if (!snapshot.repository) return;
//     for (int index = 0; index < tabWidget->count(); ++index) {
//         auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index));
//         if (editor != nullptr && editor->document()->isModified()) {
//             QMessageBox::warning(this, QStringLiteral("حماية المحرر"),
//                                  QStringLiteral("احفظ أو أغلق الملفات المعدلة قبل السحب من Git."));
//             return;
//         }
//     }
//     if (snapshot.isDirty()) {
//         const auto reply = QMessageBox::question(this, QStringLiteral("سحب Git"),
//                                                  QStringLiteral("توجد تغييرات محلية. سيُنفذ السحب السريع فقط. هل تريد المتابعة؟"),
//                                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
//         if (reply != QMessageBox::Yes) return;
//     }
//     projectExplorer->gitRepositoryService()->pull();
// }

// void Taif::handleGitBranchSwitch(const QString& branch)
// {
//     const GitRepositorySnapshot& snapshot = projectExplorer->gitRepositoryService()->snapshot();
//     if (!snapshot.repository) return;
//     for (int index = 0; index < tabWidget->count(); ++index) {
//         auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index));
//         if (editor != nullptr && editor->document()->isModified()) {
//             QMessageBox::warning(this, QStringLiteral("حماية المحرر"),
//                                  QStringLiteral("احفظ أو أغلق الملفات المعدلة قبل تبديل فرع Git."));
//             return;
//         }
//     }
//     if (snapshot.isDirty()) {
//         const auto reply = QMessageBox::question(this, QStringLiteral("تبديل فرع Git"),
//                                                  QStringLiteral("توجد تغييرات محلية. هل تريد متابعة تبديل الفرع؟"),
//                                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
//         if (reply != QMessageBox::Yes) return;
//     }
//     projectExplorer->gitRepositoryService()->switchBranch(branch);
// }
//* Git Section deactivated, active when enable this system

void Taif::saveFile()
{
    [[maybe_unused]] const bool saved = saveEditor(currentEditor());
}

void Taif::saveFileAs()
{
    [[maybe_unused]] const bool saved = saveEditorAs(currentEditor());
}

bool Taif::saveEditor(TEditor* const editor)
{
    if (editor == nullptr) {
        return false;
    }

    const QString filePath = SessionStore::normalizePath(editor->property("filePath").toString());
    return filePath.isEmpty() ? saveEditorAs(editor) : writeEditorContents(editor, filePath);
}

bool Taif::saveEditorAs(TEditor* const editor)
{
    if (editor == nullptr) {
        return false;
    }


    const QString currentPath = editor->property("filePath").toString();
    const QString currentName = currentPath.isEmpty()
                                    ? QStringLiteral("ملف جديد.alif")
                                    : QFileInfo(currentPath).fileName();
    const QString filePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("حفظ الملف"), currentName,
        QStringLiteral("ملف ألف (*.alif);;مكتبة ألف (*.aliflib);;كل الملفات (*)"));
    if (filePath.isEmpty()) {
        return false;
    }
    return writeEditorContents(editor, filePath);
}

bool Taif::writeEditorContents(TEditor* const editor, const QString& requestedPath)
{
    if (editor == nullptr) {
        return false;
    }

    const QString filePath = SessionStore::normalizePath(requestedPath);
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("خطأ"), QStringLiteral("مسار الحفظ غير صالح."));
        return false;
    }

    // Suppress external-change detection for this save: the file is about to be
    // rewritten by us, and QFileSystemWatcher would otherwise fire a spurious
    // onFileChanged event (the path may even be temporarily removed/renamed on
    // disk during QSaveFile::commit). The single-shot timer resets the flag so
    // that genuine external changes are detected again shortly after.
    // نتجاهل حدث التغيير الناتج عن حفظنا نحن (يصل بشكل غير متزامن)
    if (fileWatcher) {
        savingFromApp = true;
        saveSuppressTimer->start(1000);
    }

    const QString normalizedFilePath = SessionStore::normalizePath(filePath);
    removeWatch(normalizedFilePath);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        // Restore the watch and suppression state on failure so that external
        // changes are not permanently masked.
        savingFromApp = false;
        saveSuppressTimer->stop();
        addWatch(normalizedFilePath);
        QMessageBox::warning(this, QStringLiteral("خطأ"), QStringLiteral("لا يمكن حفظ الملف."));
        return false;
    }

    QTextStream output(&file);
    output << editor->toPlainText();
    output.flush();
    if (output.status() != QTextStream::Ok || !file.commit()) {
        savingFromApp = false;
        saveSuppressTimer->stop();
        addWatch(normalizedFilePath);
        QMessageBox::warning(this, QStringLiteral("خطأ"), QStringLiteral("تعذر إتمام حفظ الملف بأمان."));
        return false;
    }

    finalizeSavedEditor(editor, filePath);

    // Re-establish the watch after the save completes so that subsequent external
    // modifications are detected. The savingFromApp flag (cleared by the timer)
    // prevents this own-save from being mistaken for an external change.
    addWatch(normalizedFilePath);
    return true;
}

void Taif::registerEditorRecovery(TEditor* const editor)
{
    if (editor != nullptr && recoveryCoordinator != nullptr) {
        editor->setRecoveryCoordinator(recoveryCoordinator);
    }
}

void Taif::flushRecoverySnapshots()
{
    if (recoveryCoordinator == nullptr) {
        return;
    }
    for (int index = 0; index < tabWidget->count(); ++index) {
        if (auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index))) {
            editor->flushRecoverySnapshot();
        }
    }
    recoveryCoordinator->requestFlush(1200);
}

void Taif::importKnownLegacyRecoveryEntries(const QString& launchFilePath)
{
    if (recoveryCoordinator == nullptr) {
        return;
    }

    QSet<QString> candidatePaths;
    const auto addCandidate = [&candidatePaths](const QString& path) {
        const QString normalizedPath = SessionStore::normalizePath(path);
        if (!normalizedPath.isEmpty()) {
            candidatePaths.insert(normalizedPath);
        }
    };
    addCandidate(launchFilePath);

    QSettings settings(QStringLiteral("Alif"), QStringLiteral("Taif"));
    for (const QString& path : settings.value(QStringLiteral("RecentFiles")).toStringList()) {
        addCandidate(path);
    }
    const SessionStore sessionStore;
    for (const SavedSession& session : sessionStore.loadAll()) {
        for (const QString& path : session.filePaths) {
            addCandidate(path);
        }
    }

    for (const QString& path : candidatePaths) {
        [[maybe_unused]] const bool imported = recoveryCoordinator->importLegacyAdjacentBackup(path);
    }
}

void Taif::presentRecoveryEntries() {
    if (recoveryCoordinator == nullptr) {
        return;
    }
    const QVector<RecoveryEntry> availableEntries = recoveryCoordinator->entries();
    if (availableEntries.isEmpty()) {
        return;
    }

    TRecoveryDialog dialog(availableEntries, this);
    dialog.exec();
    const QVector<RecoveryEntry> selectedEntries = dialog.selectedEntries();
    if (dialog.decision() == TRecoveryDialog::Decision::Discard) {
        for (const RecoveryEntry& entry : selectedEntries) {
            recoveryCoordinator->removeEntry(entry.id);
        }
        recoveryCoordinator->requestFlush(1000);
        return;
    }
    if (dialog.decision() == TRecoveryDialog::Decision::Restore) {
        for (const RecoveryEntry& entry : selectedEntries) {
            restoreRecoveryEntry(entry);
        }
    }
}

void Taif::restoreRecoveryEntry(const RecoveryEntry& entry)
{
    if (recoveryCoordinator == nullptr) {
        return;
    }
    QString recoveredText;
    QString errorMessage;
    if (!recoveryCoordinator->readSnapshot(entry, &recoveredText, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("خطأ"),
                             errorMessage.isEmpty() ? QStringLiteral("تعذر استعادة النسخة.")
                                                    : errorMessage);
        return;
    }

    const RecoverySourceFingerprint currentFingerprint =
        RecoveryStore::fingerprintForPath(entry.sourcePath);
    const bool sourceUnchanged = !entry.sourcePath.isEmpty()
                                 && currentFingerprint.exists == entry.sourceFingerprint.exists
                                 && currentFingerprint.size == entry.sourceFingerprint.size
                                 && currentFingerprint.lastModifiedUtc == entry.sourceFingerprint.lastModifiedUtc;

    auto* const editor = new TEditor(setting, this);
    registerEditorRecovery(editor);
    editor->adoptRecoveryEntry(entry);
    editor->setPlainText(recoveredText);
    if (sourceUnchanged) {
        editor->filePath = entry.sourcePath;
        editor->setProperty("filePath", entry.sourcePath);
    }
    editor->document()->setModified(true);

    connect(editor, &TEditor::openRequest, this,
            [this](const QString& path) { openFile(path); });
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor](const bool modified) { onEditorModificationChanged(editor, modified); });
    connectEditorDiagnostics(editor);
    // connectEditorActionState(editor); //* review

    const QString displayName = entry.displayName.isEmpty() ? QStringLiteral("غير معنون")
                                                            : entry.displayName;
    const int tabIndex = tabWidget->addTab(editor, QStringLiteral("استعادة — %1").arg(displayName));
    tabWidget->setTabToolTip(tabIndex, entry.sourcePath.isEmpty()
                                           ? QStringLiteral("نسخة استعادة لمستند غير معنون") : entry.sourcePath);
    tabWidget->setCurrentIndex(tabIndex);
    onEditorModificationChanged(editor, true);
}


void Taif::finalizeSavedEditor(TEditor* const editor, const QString& filePath)
{
    if (editor == nullptr) {
        return;
    }

    editor->filePath = filePath;
    editor->setProperty("filePath", filePath);
#if defined(Q_OS_WIN)
    editor->setDocumentLineEnding(EditorStatusSnapshot::LineEnding::Crlf);
#else
    editor->setDocumentLineEnding(EditorStatusSnapshot::LineEnding::Lf);
#endif
    editor->document()->setModified(false);
    editor->removeBackupFile();
    onEditorModificationChanged(editor, false);
    if (editor == currentEditor()) {
        refreshBreadcrumbs();
    }
}

void Taif::openSettings()
{
    if (setting == nullptr) {
        return;
    }
    if (!setting->isVisible()) {
        setting->beginEditing();
    }
    setting->show();
    setting->raise();
    setting->activateWindow();
}


void Taif::exitApp() {
    emit returnToWelcomeRequested();
}

void Taif::onCurrentTabChanged() {
    if (searchBar != nullptr) {
        // SearchPanel is temporarily parented to the editor for clipping. It
        // must be returned to Taif before the old editor can be deleted.
        searchBar->detachFromHost();
        searchBar->hide();
    }
    updateWindowTitle();

    TEditor* const editor = currentEditor();
    bindInformationBarToEditor(editor);
    refreshStatusBar();

    refreshDiagnosticsPanel();
    // updateEditActionState(); //* review
    bindBreadcrumbsToEditor(editor);
    refreshBreadcrumbs();
}

bool Taif::hasOpenEditorAtOrBelow(const QString& path) const {
    const QString normalizedPath = ProjectFileOperations::normalizedPath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }
    for (int index = 0; index < tabWidget->count(); ++index) {
        const auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index));
        if (editor == nullptr) continue;
        const QString editorPath = ProjectFileOperations::normalizedPath(editor->property("filePath").toString());
        if (editorPath == normalizedPath
            || editorPath.startsWith(normalizedPath + QLatin1Char('/'), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

void Taif::presentProjectOperationResult(const ProjectFileOperationResult& result) {
    if (result.succeeded) {
        statusBar()->showMessage(result.userMessage, 3500);
        return;
    }
    QMessageBox::warning(this, QStringLiteral("عملية ملفات المشروع"),
                         result.userMessage.isEmpty() ? QStringLiteral("تعذرت العملية.")
                                                      : result.userMessage);
}

void Taif::connectEditorDiagnostics(TEditor* editor) {
    if (editor == nullptr) {
        return;
    }
    connect(editor, &TEditor::diagnosticsChanged, this,
            [this, editor](QVector<EditorDiagnostic> diagnostics, const quint64) {
                if (editor == currentEditor() && diagnosticsPanel) {
                    diagnosticsPanel->setDiagnostics(std::move(diagnostics));
                }
            });
}

void Taif::refreshDiagnosticsPanel() {
    if (!diagnosticsPanel) {
        return;
    }
    if (TEditor* editor = currentEditor()) {
        diagnosticsPanel->setDiagnostics(editor->currentDiagnostics());
    } else {
        diagnosticsPanel->clearDiagnostics();
    }
}

void Taif::bindInformationBarToEditor(TEditor* const editor) {
    if (editorInformationConnection) {
        disconnect(editorInformationConnection);
    }
    if (editor == nullptr || editorStatusBar == nullptr) {
        return;
    }
    editorInformationConnection = connect(editor, &TEditor::editorInformationChanged, this,
                                          [this, editor](const EditorStatusSnapshot& snapshot) {
                                              if (editor == currentEditor() && editorStatusBar != nullptr) {
                                                  editorStatusBar->setSnapshot(snapshot);
                                              }
                                          });
}

void Taif::refreshStatusBar() {
    if (editorStatusBar == nullptr) {
        return;
    }
    if (TEditor* const editor = currentEditor()) {
        editorStatusBar->setSnapshot(editor->informationSnapshot());
    } else {
        editorStatusBar->setSnapshot({});
    }
}

void Taif::bindBreadcrumbsToEditor(TEditor* const editor)
{
    if (breadcrumbConnection) {
        disconnect(breadcrumbConnection);
    }
    if (editor == nullptr || breadcrumbBar == nullptr) {
        return;
    }
    breadcrumbConnection = connect(editor, &TEditor::breadcrumbContextChanged, this,
                                   [this, editor](const EditorBreadcrumbContext& context) {
                                       if (editor == currentEditor() && breadcrumbBar != nullptr) {
                                           breadcrumbBar->setSemanticContext(context);
                                       }
                                   });
}

void Taif::refreshBreadcrumbs()
{
    if (breadcrumbBar == nullptr) {
        return;
    }
    TEditor* const editor = currentEditor();
    if (editor == nullptr) {
        breadcrumbBar->setFileContext({});
        breadcrumbBar->clearSemanticContext();
        return;
    }

    breadcrumbBar->setFileContext(
        SessionStore::normalizePath(editor->property("filePath").toString()));
    breadcrumbBar->setSemanticContext(editor->breadcrumbContextAtCursor());
}

void Taif::revealBreadcrumbPath(const QString& path)
{
    const QFileInfo pathInfo(path);
    if (!pathInfo.isDir()) {
        if (TEditor* const editor = currentEditor()) {
            editor->setFocus();
        }
        return;
    }

    if (folderPath.isEmpty()) {
        return;
    }
    const QString relativePath = QDir(folderPath).relativeFilePath(pathInfo.absoluteFilePath());
    if (relativePath == QStringLiteral("..")
        || relativePath.startsWith(QStringLiteral("../"))
        || relativePath.startsWith(QStringLiteral("..\\"))) {
        return;
    }
    projectExplorer->setVisible(true);
    toggleSidebarAction->setChecked(true);
    projectExplorer->selectPath(pathInfo.absoluteFilePath());
}

void Taif::updateCursorPosition() {
    refreshStatusBar();
}


/* ----------------------------------- Run Menu Button ----------------------------------- */

void Taif::runAlif() {
    TEditor* const editor = currentEditor();
    TConsole* const console = alifOutputConsole;
    if (editor == nullptr || console == nullptr || alifOutputDock == nullptr
        || runController == nullptr) {
        return;
    }

    showAndRaiseDock(alifOutputDock);
    if (runController->isActive()) {
        console->appendPlainTextThreadSafe(QStringLiteral("\nجار إيقاف التنفيذ...\n"));
        runController->cancel();
        return;
    }

    QString filePath = editor->property("filePath").toString();
    if (filePath.isEmpty() || editor->document()->isModified()) {
        QMessageBox::warning(this, QStringLiteral("تنبيه"),
                             QStringLiteral("يجب حفظ الملف قبل التشغيل."));
        saveFile();
        filePath = editor->property("filePath").toString();
        if (filePath.isEmpty() || editor->document()->isModified()) {
            return;
        }
    }

    const QString applicationDirectory = QCoreApplication::applicationDirPath();
#if defined(Q_OS_WIN)
    const QString program = QDir(applicationDirectory).filePath(QStringLiteral("alif/alif.exe"));
#else
    const QString program = QDir(applicationDirectory).filePath(QStringLiteral("alif/alif"));

#endif
    if (!QFileInfo::exists(program)) {
        console->clear();
        console->appendPlainTextThreadSafe(
            QStringLiteral("تعذر العثور على مترجم ألف.\nالمسار المتوقع: %1\n").arg(program));

#if defined(Q_OS_LINUX)
        console->appendPlainTextThreadSafe(
            QStringLiteral("تأكد من وجود ملف ألف ومن منحه صلاحية التنفيذ.\n"));
#endif
        return;
    }

    console->clear();
    console->appendPlainTextThreadSafe(
        QStringLiteral("بدء تشغيل ملف ألف: %1\n\n").arg(QFileInfo(filePath).fileName()));

    AlifRunController::Request request;
    request.program = program;
    request.arguments = {filePath};
    request.workingDirectory = QFileInfo(filePath).absolutePath();
    request.displayName = QFileInfo(filePath).fileName();

    QString errorMessage;
    if (!runController->start(request, &errorMessage)) {
        console->appendPlainTextThreadSafe(
            QStringLiteral("تعذر تشغيل ألف: %1\n").arg(errorMessage));
    }
}


//----------------

TEditor* Taif::currentEditor() {
    return qobject_cast<TEditor*>(tabWidget->currentWidget());
}

void Taif::closeTab(const int index) {
    if (tabWidget->count() <= 1) {
        // تفعيل هذه الدالة يسمح للمحرر بإغلاق أخر تبويب مفتوح
        // والعودة بعد ذلك إلى نافذة الترحيب
        // exitApp();
        return;
    }

    auto* const editor = qobject_cast<TEditor*>(tabWidget->widget(index));
    if (editor == nullptr || !prepareEditorForClose(editor)) {
        return;
    }

    // Stop watching the file before destroying the editor so that stale
    // entries are not left in QFileSystemWatcher.
    removeWatch(editor->property("filePath").toString());

    tabWidget->removeTab(index);
    editor->deleteLater();
}

/* ----------------------------------- Help Menu Button ----------------------------------- */

void Taif::aboutTaif() {
    QMessageBox messageDialog{};
    messageDialog.setWindowTitle("عن محرر طيف");
    messageDialog.setText(R"(
        محرر طيف (نـ3) 1445-1446

        © الحقوق محفوظة لصالح
        برمجيات ألف - عبدالرحمن ومحمد الخطيب - سوريا

        محرر نصي خاص بلغة ألف نـ5
        يعمل على جميع المنصات "ويندوز - لينكس - ماك"
        ـــــــــــــــــــــــــــــــــــــــــــــــــــــ
        المحرر لا يزال تحت التطوير وقد يحتوي بعض الاخطاء
        نرجو تبليغ مجتمع ألف في حال وجود أي خطأ
        https://t.me/aliflang
        ـــــــــــــــــــــــــــــــــــــــــــــــــــــ
        فريق التطوير لا يمتلك أي ضمانات وغير مسؤول
        عن أي خطأ او خلل قد يحدث بسبب المحرر.

        المحرر يخضع لرخصة برمجيات ألف
        يجب قراءة الرخصة جيداً قبل البدأ بإستخدام المحرر
                                    )"
                          );
    messageDialog.setStyleSheet("background: #1e293b; color: #f1f5f9; font-family: 'Tajawal', 'Noto Kufi Arabic';");

    messageDialog.exec();
}

void Taif::checkForUpdates() {
    #if defined(Q_OS_WIN)
    QString path = QCoreApplication::applicationDirPath() + "/MaintenanceTool.exe";
    #elif defined(Q_OS_LINUX)
    QString path = QCoreApplication::applicationDirPath() + "/maintenancetool";
    #elif defined(Q_OS_MACOS)
    QString path = QCoreApplication::applicationDirPath() + "/../../../maintenancetool.app/contents/MacOS/maintenancetool";
    qDebug() << path;
    #endif
    // Use --updater to skip the "Add/Remove" page and go straight to updates
    QProcess::startDetached(path, {"--su"});
}

/* ----------------------------------- Other Functions ----------------------------------- */

void Taif::updateWindowTitle() {
    TEditor* editor = currentEditor();
    QString title{};

    if (!editor) {
        title = "طيف";
    } else {
        QString filePath = editor->property("filePath").toString();
        // --------------------------------------------------------

        if (filePath.isEmpty()) {
            title = "غير معنون";
        } else {
            title = QFileInfo(filePath).fileName();
        }
        if (editor->document()->isModified()) {
            title += "[*]";
        }
        title += " - طيف";
    }
    setWindowTitle(title);
    setWindowModified(editor && editor->document()->isModified()); // تحديث علامة التعديل للنافذة
}

void Taif::onEditorModificationChanged(TEditor* const editor, const bool modified) {
    if (editor == nullptr) {
        return;
    }

    const int index = tabWidget->indexOf(editor);
    if (index >= 0) {
        QString tabText = tabWidget->tabText(index);
        if (modified && !tabText.endsWith(QStringLiteral("[*]"))) {
            tabWidget->setTabText(index, tabText + QStringLiteral("[*]"));
        } else if (!modified && tabText.endsWith(QStringLiteral("[*]"))) {
            tabWidget->setTabText(index, tabText.left(tabText.length() - 3));
        }
    }

    if (editor == currentEditor()) {
        updateWindowTitle();
    }
}

