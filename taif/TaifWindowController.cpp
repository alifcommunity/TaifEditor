#include "TaifWindowController.h"
#include "Taif.h"
#include "TWelcomeWindow.h"

#include <QMessageBox>

TaifWindowController::TaifWindowController(QObject* const parent)
    : QObject(parent)
{
}

void TaifWindowController::showInitial(const TaifLaunchRequest& launchRequest)
{
    if (!launchRequest.filePath.isEmpty()) {
        showEditorWindow(createEditorWindow(launchRequest.filePath));
        return;
    }
    showWelcome();
}

void TaifWindowController::showWelcome()
{
    if (m_welcomeWindow != nullptr) {
        m_welcomeWindow->show();
        m_welcomeWindow->raise();
        m_welcomeWindow->activateWindow();
        return;
    }

    auto* const welcomeWindow = new WelcomeWindow();
    m_welcomeWindow = welcomeWindow;
    connect(welcomeWindow, &WelcomeWindow::newDocumentRequested,
            this, [this]() { showEditorWindow(createEditorWindow()); });
    connect(welcomeWindow, &WelcomeWindow::fileOpenRequested,
            this, [this](const QString& filePath) {
                showEditorWindow(createEditorWindow(filePath));
            });
    connect(welcomeWindow, &WelcomeWindow::folderOpenRequested,
            this, [this](const QString& folderPath) {
                Taif* const editor = createEditorWindow();
                editor->loadFolder(folderPath);
                showEditorWindow(editor);
            });
    connect(welcomeWindow, &WelcomeWindow::sessionOpenRequested,
            this, &TaifWindowController::restoreSession);
    connect(welcomeWindow, &QObject::destroyed, this,
            [this, welcomeWindow]() {
                if (m_welcomeWindow == welcomeWindow) {
                    m_welcomeWindow = nullptr;
                }
            });

    emit welcomeWindowCreated(welcomeWindow);
    welcomeWindow->show();
}

Taif* TaifWindowController::createEditorWindow(const QString& filePath,
                                                       const bool createInitialDocument)
{
    auto* const editor = new Taif(filePath, nullptr, createInitialDocument);
    m_editorWindows.insert(editor);
    connect(editor, &Taif::returnToWelcomeRequested,
            this, [this, editor]() { requestReturnToWelcome(editor); });
    connect(editor, &Taif::closeRejected, this,
            [this, editor]() { m_returnToWelcomeWindows.remove(editor); });
    connect(editor, &QObject::destroyed, this, [this, editor]() {
        const bool shouldShowWelcome = m_returnToWelcomeWindows.remove(editor);
        m_editorWindows.remove(editor);
        if (shouldShowWelcome) {
            showWelcome();
        }
    });

    emit editorWindowCreated(editor);
    return editor;
}

void TaifWindowController::showEditorWindow(Taif* const editor)
{
    if (editor == nullptr) {
        return;
    }

    editor->show();
    editor->raise();
    editor->activateWindow();
    closeWelcomeWindow();
}

void TaifWindowController::closeWelcomeWindow()
{
    if (m_welcomeWindow != nullptr) {
        m_welcomeWindow->close();
    }
}

void TaifWindowController::restoreSession(const SavedSession& session)
{
    Taif* const editor = createEditorWindow({}, false);
    const SessionRestoreResult restoreResult = editor->restoreSession(session);
    showEditorWindow(editor);

    if (!restoreResult.unavailableFilePaths.isEmpty()) {
        QMessageBox::warning(editor, QStringLiteral("ملفات غير متاحة"),
                             QStringLiteral("تعذر فتح الملفات التالية:\n%1")
                                 .arg(restoreResult.unavailableFilePaths.join(u'\n')));
    }
}

void TaifWindowController::requestReturnToWelcome(Taif* const editor)
{
    if (editor == nullptr || !m_editorWindows.contains(editor)) {
        return;
    }

    m_returnToWelcomeWindows.insert(editor);
    editor->close();
}
