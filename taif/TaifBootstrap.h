#pragma once

#include <QString>
#include <QStringList>

class QApplication;

struct TaifLaunchRequest final {
    QString filePath;
    QString errorMessage;

    [[nodiscard]] bool isValid() const { return errorMessage.isEmpty(); }
};

/**
 * Process-wide startup policy for TaifEditor. This class must be initialized
 * after QApplication construction and before constructing any application UI.
 */
class TaifBootstrap final {
public:
    static void initialize(QApplication& application);

    [[nodiscard]] static QString tajawalFontFamily();
    [[nodiscard]] static QString notoKufiFontFamily();
    [[nodiscard]] static QString monospaceFontFamily();
    [[nodiscard]] static QStringList availableEditorFontFamilies();
    [[nodiscard]] static TaifLaunchRequest parseLaunchRequest(
        const QStringList& arguments);

private:
    TaifBootstrap() = delete;
};
