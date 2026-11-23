#pragma once

#include "TaifBootstrap.h"

#include <QObject>
#include <QPointer>
#include <QSet>

class Taif;
class WelcomeWindow;
struct SavedSession;

/**
 * Application-owned router for top-level Taif and Welcome windows.
 *
 * WelcomeWindow emits user intent only; it never owns or constructs an editor
 * main window. Taif likewise emits an intent to return to Welcome rather than
 * constructing one during close handling. This controller owns the transition
 * policy and keeps every top-level window’s lifetime observable in one place.
 */
class TaifWindowController final : public QObject
{
    Q_OBJECT

public:
    explicit TaifWindowController(QObject* parent = nullptr);

    void showInitial(const TaifLaunchRequest& launchRequest);
    void showWelcome();

signals:
    void editorWindowCreated(Taif* editor);
    void welcomeWindowCreated(WelcomeWindow* welcomeWindow);

private:
    Taif* createEditorWindow(const QString& filePath = {}, bool createInitialDocument = true);
    void showEditorWindow(Taif* editor);
    void closeWelcomeWindow();
    void restoreSession(const SavedSession& session);
    void requestReturnToWelcome(Taif* editor);

    QPointer<WelcomeWindow> m_welcomeWindow;
    QSet<Taif*> m_editorWindows;
    QSet<Taif*> m_returnToWelcomeWindows;
};
