#include "DockableConsoleTool.h"
#include "InlinePromptConsole.h"
#include "TConsole.h"

#include <QDockWidget>
#include <QMainWindow>
#include <QTimer>

DockableConsoleTool DockableConsoleToolFactory::create(QMainWindow* const host,
                                                        const QString& title,
                                                        const QString& dockObjectName,
                                                        const QString& consoleObjectName,
                                                        const bool startSystemShell)
{
    if (host == nullptr) {
        return {};
    }

    auto* dock = new QDockWidget(title, host);
    dock->setObjectName(dockObjectName);
    // dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable
                      | QDockWidget::DockWidgetFloatable
                      | QDockWidget::DockWidgetClosable);

    TConsole* console = startSystemShell
                            ? new TConsole(dock)
                            : static_cast<TConsole*>(new InlinePromptConsole(dock));
    console->setObjectName(consoleObjectName);
    if (startSystemShell) {
        console->enableNativeTerminal();
    } else {
        console->setConsoleRTL();
    }
    dock->setWidget(console);
    host->addDockWidget(Qt::BottomDockWidgetArea, dock);

    // Native shells are started on first activation, after dock layout has
    // produced a real terminal grid and the caller can supply project context.
    // Starting while this initially hidden dock is 2x1 creates a tiny ConPTY
    // buffer and an output surface dominated by padded blank rows.

    return {dock, console};
}

void DockableConsoleToolFactory::ensureTabifiedWith(QMainWindow* const host,
                                                    QDockWidget* const anchor,
                                                    QDockWidget* const dock)
{
    if (host == nullptr || anchor == nullptr || dock == nullptr || anchor == dock) {
        return;
    }

    if (anchor->isFloating() || dock->isFloating()
        || host->dockWidgetArea(anchor) != Qt::BottomDockWidgetArea
        || host->dockWidgetArea(dock) != Qt::BottomDockWidgetArea) {
        return;
    }

    if (!host->tabifiedDockWidgets(anchor).contains(dock)) {
        host->tabifyDockWidget(anchor, dock);
    }
}

bool DockableConsoleToolFactory::isRenderedTab(const QDockWidget* const dock)
{
    return dock != nullptr && dock->isVisible() && !dock->visibleRegion().isEmpty();
}

void DockableConsoleToolFactory::showAndActivate(QDockWidget* const dock)
{
    if (dock == nullptr) {
        return;
    }

    const auto activateDock = [dock]() {
        if (!dock->isVisible()) {
            return;
        }
        dock->raise();
        if (QWidget* const tool = dock->widget()) {
            if (auto* const console = qobject_cast<TConsole*>(tool);
                console != nullptr && console->isNativeTerminal()) {
                console->focusNativeTerminal(Qt::OtherFocusReason);
            } else {
                tool->setFocus(Qt::OtherFocusReason);
            }
        }
    };

    dock->show();
    activateDock();
    QTimer::singleShot(0, dock, [dock, activateDock]() {
        activateDock();
        if (auto* const console = qobject_cast<TConsole*>(dock->widget());
            console != nullptr && console->isNativeTerminal()) {
            console->startCmd();
        }
    });
}
