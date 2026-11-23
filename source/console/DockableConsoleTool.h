#pragma once

#include <QString>

class QDockWidget;
class QMainWindow;
class TConsole;

struct DockableConsoleTool final {
    QDockWidget* dock = nullptr;
    TConsole* console = nullptr;
};

/**
 * Creates one persistent bottom-dock console tool with the standard Taif dock
 * contract. The caller owns layout/tabification policy through QMainWindow and
 * may hide the dock without destroying the console or its process state.
 */
class DockableConsoleToolFactory final {
public:
    [[nodiscard]] static DockableConsoleTool create(QMainWindow* host,
                                                    const QString& title,
                                                    const QString& dockObjectName,
                                                    const QString& consoleObjectName,
                                                    bool startSystemShell);

    /**
     * Ensures a bottom-area console dock shares the anchor's tab group. Floating
     * or deliberately relocated docks are left untouched to preserve user layout.
     */
    static void ensureTabifiedWith(QMainWindow* host,
                                   QDockWidget* anchor,
                                   QDockWidget* dock);

    /** Returns whether the dock is the currently rendered tab in its group. */
    [[nodiscard]] static bool isRenderedTab(const QDockWidget* dock);

    /**
     * Shows a dock and selects it from a tabified group after the pending dock
     * layout update has completed. The owned tool receives keyboard focus.
     */
    static void showAndActivate(QDockWidget* dock);
};
