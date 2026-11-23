QT += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++23

TARGET = Taif

RESOURCES += \
    resources.qrc


# Include directories
INCLUDEPATH +=  ../source/texteditor \
                ../source/texteditor/highlighter \
                ../source/texteditor/autocomplete \
                ../source/texteditor/analysis \
                ../source/texteditor/hover \
                ../source/texteditor/diagnostics  \
                ../source/texteditor/navigation  \
                ../source/texteditor/services  \
                ../source/console \
                ../source/console/terminal \
                ../source/components \
                ../source/menubar   \
                ../source/pages \
                ../source/settings  \
                ../source/session  \
                ../source/recovery  \
                ../source/run  \
                ../source/projectexplorer  \
                ../source/versioncontrol  \
                ../source/aliflang/lexer  \
                ../source/aliflang/parser  \
                ../source/aliflang/semantic  \
                ../source/aliflang/presentation  \

SOURCES += \
    ../source/components/TStatusBar.cpp \
    ../source/console/InlinePromptConsole.cpp \
    ../source/console/OutputBuffer.cpp \
    ../source/console/terminal/PosixPtyBackend.cpp \
    ../source/console/terminal/TerminalScreenModel.cpp \
    ../source/console/terminal/TerminalSessionController.cpp \
    ../source/console/terminal/TerminalView.cpp \
    ../source/console/terminal/VtStreamParser.cpp \
    ../source/console/terminal/WindowsConPtyBackend.cpp \
    ../source/projectexplorer/GitStatusService.cpp \
    ../source/projectexplorer/ProjectFileOperations.cpp \
    ../source/projectexplorer/ProjectFileProxyModel.cpp \
    ../source/projectexplorer/TProjectExplorerWidget.cpp \
    ../source/recovery/RecoveryCoordinator.cpp \
    ../source/recovery/RecoveryStore.cpp \
    ../source/recovery/TRecoveryDialog.cpp \
    ../source/run/AlifRunController.cpp \
    ../source/session/TSessionManagerDialog.cpp \
    ../source/settings/EditorPreferences.cpp \
    ../source/texteditor/services/EditorAnalysisBinding.cpp \
    ../source/texteditor/services/EditorInteractionBinding.cpp \
    ../source/texteditor/services/EditorRecoveryBinding.cpp \
    ../source/versioncontrol/GitPanelWidget.cpp \
    ../source/versioncontrol/GitRepositoryService.cpp \
    TaifWindowController.cpp \
    main.cpp \
    Taif.cpp \
    TaifBootstrap.cpp \
    ../source/aliflang/presentation/AlifDiagnosticPresentationAdapter.cpp \
    ../source/aliflang/presentation/AlifSemanticPresentationAdapter.cpp \
    ../source/console/DockableConsoleTool.cpp \
    ../source/session/SessionStore.cpp \
    ../source/session/TSessionEditorDialog.cpp \
    ../source/texteditor/analysis/EditorAnalysisController.cpp \
    ../source/texteditor/analysis/SemanticCompletionProvider.cpp \
    ../source/texteditor/analysis/SemanticDefinitionProvider.cpp \
    ../source/texteditor/analysis/SemanticHoverProvider.cpp \
    ../source/texteditor/diagnostics/TDiagnosticsPanel.cpp \
    ../source/texteditor/hover/HoverPopup.cpp \
    ../source/texteditor/navigation/TBreadcrumbBar.cpp \
    ../source/texteditor/autocomplete/AutoComplete.cpp \
    ../source/texteditor/autocomplete/AutoCompleteUI.cpp \
    ../source/texteditor/highlighter/TLexer.cpp \
    ../source/texteditor/highlighter/TSyntaxDefinition.cpp \
    ../source/texteditor/highlighter/TSyntaxHighlighter.cpp \
    ../source/texteditor/TEditor.cpp \
    ../source/components/TMinimap.cpp \
    ../source/components/TSearchPanel.cpp \
    ../source/console/TConsole.cpp \
    ../source/console/ProcessWorker.cpp \
    ../source/menubar/TMenu.cpp    \
    ../source/pages/TWelcomeWindow.cpp  \
    ../source/settings/TSettings.cpp   \
    ../source/aliflang/lexer/AlifLexer.cpp \
    ../source/aliflang/parser/AlifParser.cpp \
    ../source/aliflang/semantic/AlifSymbolTable.cpp \

HEADERS += \
    ../source/components/TStatusBar.h \
    ../source/console/InlinePromptConsole.h \
    ../source/console/OutputBuffer.h \
    ../source/console/terminal/ITerminalBackend.h \
    ../source/console/terminal/PosixPtyBackend.h \
    ../source/console/terminal/TerminalScreenModel.h \
    ../source/console/terminal/TerminalSessionController.h \
    ../source/console/terminal/TerminalView.h \
    ../source/console/terminal/VtStreamParser.h \
    ../source/console/terminal/WindowsConPtyBackend.h \
    ../source/projectexplorer/GitStatusService.h \
    ../source/projectexplorer/ProjectExplorerTypes.h \
    ../source/projectexplorer/ProjectFileOperations.h \
    ../source/projectexplorer/ProjectFileProxyModel.h \
    ../source/projectexplorer/TProjectExplorerWidget.h \
    ../source/recovery/RecoveryCoordinator.h \
    ../source/recovery/RecoveryStore.h \
    ../source/recovery/TRecoveryDialog.h \
    ../source/run/AlifRunController.h \
    ../source/session/TSessionManagerDialog.h \
    ../source/settings/EditorPreferences.h \
    ../source/texteditor/EditorStatusSnapshot.h \
    ../source/texteditor/services/EditorAnalysisBinding.h \
    ../source/texteditor/services/EditorInteractionBinding.h \
    ../source/texteditor/services/EditorRecoveryBinding.h \
    ../source/versioncontrol/GitPanelWidget.h \
    ../source/versioncontrol/GitRepositoryService.h \
    ../source/versioncontrol/GitTypes.h \
    Taif.h  \
    TaifBootstrap.h \
    ../source/aliflang/presentation/AlifAnalysis.h \
    ../source/aliflang/presentation/AlifDiagnosticPresentationAdapter.h \
    ../source/aliflang/presentation/AlifSemanticPresentationAdapter.h \
    ../source/console/DockableConsoleTool.h \
    ../source/session/SessionStore.h \
    ../source/session/TSessionEditorDialog.h \
    ../source/texteditor/analysis/EditorAnalysisController.h \
    ../source/texteditor/analysis/SemanticCompletionProvider.h \
    ../source/texteditor/analysis/SemanticDefinitionProvider.h \
    ../source/texteditor/analysis/SemanticHoverProvider.h \
    ../source/texteditor/diagnostics/TDiagnosticsPanel.h \
    ../source/texteditor/hover/HoverPopup.h \
    ../source/texteditor/navigation/BreadcrumbTypes.h \
    ../source/texteditor/navigation/TBreadcrumbBar.h \
    ../source/texteditor/autocomplete/AutoComplete.h \
    ../source/texteditor/autocomplete/AutoCompleteUI.h \
    ../source/texteditor/highlighter/TLexer.h \
    ../source/texteditor/highlighter/TSyntaxDefinition.h \
    ../source/texteditor/highlighter/TSyntaxHighlighter.h \
    ../source/texteditor/highlighter/TSyntaxThemes.h \
    ../source/texteditor/highlighter/TToken.h \
    ../source/texteditor/TEditor.h \
    ../source/components/TMinimap.h \
    ../source/components/TSearchPanel.h \
    ../source/console/TConsole.h \
    ../source/console/ProcessWorker.h \
    ../source/menubar/TMenu.h  \
    ../source/pages/TWelcomeWindow.h \
    ../source/settings/TSettings.h \
    ../source/aliflang/lexer/AlifLexer.h \
    ../source/aliflang/parser/AlifParser.h \
    ../source/aliflang/semantic/AlifSymbolTable.h \
    TaifWindowController.h



# Add the application icon (Windows)
win32:RC_ICONS += resources/TaifLogo.ico

# Add the application icon (macOS/Linux)
macx:ICON = resources/TaifLogo.icns
unix:!macx:ICON = resources/TaifLogo.png

# Default rules for deployment.
qnx:target.path = /tmp/$${TARGET}/bin
else:unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target


