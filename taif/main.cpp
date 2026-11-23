#include "TaifWindowController.h"
#include "TaifBootstrap.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    TaifBootstrap::initialize(application);

    const TaifLaunchRequest launchRequest =
        TaifBootstrap::parseLaunchRequest(application.arguments());
    if (!launchRequest.isValid()) {
        QMessageBox::warning(nullptr, QStringLiteral("طيف"), launchRequest.errorMessage);
        return EXIT_FAILURE;
    }

    // هذه الدالة قد تسبب إغلاق البرنامج بشكل غير صحيح عند الضغط على خروج في قائمة ملف
    application.setQuitOnLastWindowClosed(true); //* review
    TaifWindowController windowController(&application);
    windowController.showInitial(launchRequest);

    return application.exec();
}