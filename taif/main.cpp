#include "Taif.h"
#include "TWelcomeWindow.h"

#include <QApplication>
#include <QMessageBox>
#include <QDebug>

#include <QFileDialog>
#include <QLockFile>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Alif");
    QCoreApplication::setApplicationName("Taif");
    app.setLayoutDirection(Qt::RightToLeft);


    QString lockPath = QDir::tempPath() + "/taif_editor.lock";
    QLockFile lockFile(lockPath);

    if (!lockFile.tryLock(100)) {
        QMessageBox::warning(nullptr, "طيف",
                             "البرنامج يعمل بالفعل!\nلا يمكن تشغيل أكثر من نسخة في نفس الوقت.");
        return 0;
    }

    int fontId1 = QFontDatabase::addApplicationFont(":/fonts/resources/fonts/Tajawal/Tajawal-Regular.ttf");
    int fontId2 = QFontDatabase::addApplicationFont(":/fonts/resources/fonts/KawkabMono-Regular.ttf");
    int fontId3 = QFontDatabase::addApplicationFont(":/fonts/resources/fonts/NotoKufiArabic-Regular.ttf");
    if(fontId1 == -1 or fontId2 == -1 or fontId3 == -1) {
        qWarning() << "لم يستطع تحميل الخط";
    } else {
        QString tajawal = QFontDatabase::applicationFontFamilies(fontId1).at(0);
        QString kawkabMono = QFontDatabase::applicationFontFamilies(fontId2).at(0);
        QString notoKufi = QFontDatabase::applicationFontFamilies(fontId3).at(0);
        QFont font{};
        QStringList fontFamilies{};
        fontFamilies << notoKufi << tajawal << kawkabMono;
        font.setFamilies(fontFamilies);
        font.setPixelSize(14);
        font.setWeight(QFont::Weight::Normal);
        app.setFont(font);
    }

    // تخصيص شريط التمرير العمودي في كامل المحرر
    app.setStyleSheet(R"(
        QScrollBar:vertical {
            background: transparent;
            width: 20px;
            margin: 18px 6px 18px 6px;
        }
        QScrollBar:horizontal {
            background: transparent;
            height: 20px;
            margin: 6px 18px 6px 18px;
        }

        QScrollBar::handle:vertical {
            background: #254663;
            min-height: 15px;
            border-radius: 4px;
        }
        QScrollBar::handle:horizontal {
            background: #254663;
            min-width: 15px;
            border-radius: 4px;
        }

        QScrollBar::handle:vertical:hover,
        QScrollBar::handle:horizontal:hover {
            background: #325573;
        }

        QScrollBar::add-line:vertical { /* المكان الذي يحمل السهم السفلي */
            background: transparent;
            height: 16px;
            padding: 2px;
            subcontrol-position: bottom;
            subcontrol-origin: margin;
            border-radius: 6px;
        }
        QScrollBar::add-line:horizontal { /* المكان الذي يحمل السهم الايسر */
            background: transparent;
            width: 16px;
            padding: 2px;
            subcontrol-position: right;
            subcontrol-origin: margin;
            border-radius: 6px;
        }

        QScrollBar::sub-line:vertical { /* المكان الذي يحمل السهم العلوي*/
            background: transparent;
            height: 16px;
            padding: 2px;
            subcontrol-position: top;
            subcontrol-origin: margin;
            border-radius: 6px;
        }
        QScrollBar::sub-line:horizontal { /* المكان الذي يحمل السهم الايمن*/
            background: transparent;
            width: 16px;
            padding: 2px;
            subcontrol-position: left;
            subcontrol-origin: margin;
            border-radius: 6px;
        }

        QScrollBar::up-arrow:vertical,
        QScrollBar::down-arrow:vertical,
        QScrollBar::left-arrow:horizontal,
        QScrollBar::right-arrow:horizontal {
            width: 12px;
            background: none;
            color: white;
            border-radius: 6px;
        }

        QScrollBar::up-arrow:vertical {
            image: url(:/icons/resources/up-arrow.png);
        }
        QScrollBar::right-arrow:horizontal {
            image: url(:/icons/resources/left-arrow.png);
        }

        QScrollBar::down-arrow:vertical {
            image: url(:/icons/resources/down-arrow.png);
        }
        QScrollBar::left-arrow:horizontal {
            image: url(:/icons/resources/right-arrow.png);
        }

        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical,
        QScrollBar::add-page:horizontal,
        QScrollBar::sub-page:horizontal {
            background: none;
        }
    )");

    // لتشغيل ملف ألف بإستخدام محرر طيف عند إختيار المحرر ك برنامج للتشغيل
    QString filePath{};
    if (app.arguments().count() > 2) {
        int ret = QMessageBox::warning(nullptr, "طيف",
                                       "لا يمكن تمرير أكثر من معامل واحد",
                                       QMessageBox::Close);
        return ret;
    }

    if (app.arguments().count() == 2) {
        filePath = app.arguments().at(1);
    }

    app.setQuitOnLastWindowClosed(true);

    if (!filePath.isEmpty()) {
        Taif *editor = new Taif(filePath);
        editor->show();
    } else {
        WelcomeWindow *w = new WelcomeWindow();
        w->show();
    }

    return app.exec();
}
