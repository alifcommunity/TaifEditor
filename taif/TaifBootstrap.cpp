#include "TaifBootstrap.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QLoggingCategory>

namespace {

struct FontCatalog final {
    QString tajawalFont = QStringLiteral("Tajawal");
    QString notoKufiFont = QStringLiteral("Noto Kufi Arabic");
    QString monospaceFont = QStringLiteral("Monospace");
    QStringList editorFamilies;
    bool initialized = false;
};

FontCatalog& fontCatalog() {
    static FontCatalog catalog;
    return catalog;
}

QString registerFont(const QString& resourcePath, const QString& fallbackFamily) {
    const int fontId = QFontDatabase::addApplicationFont(resourcePath);
    if (fontId < 0) {
        qWarning().noquote() << QStringLiteral("تعذر تحميل خط التطبيق: %1").arg(resourcePath);
        return fallbackFamily;
    }

    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    if (families.isEmpty()) {
        qWarning().noquote() << QStringLiteral("لم ينتج عن الخط عائلة صالحة: %1").arg(resourcePath);
        return fallbackFamily;
    }
    return families.constFirst();
}

void appendUnique(QStringList& families, const QString& family) {
    if (!family.isEmpty() && !families.contains(family, Qt::CaseInsensitive)) {
        families.append(family);
    }
}

QString globalScrollBarStyle() {
    // تخصيص شريط التمرير العمودي في كامل المحرر
    return QStringLiteral(R"(
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
            background: #3b82f6;
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
}

} // namespace

void TaifBootstrap::initialize(QApplication& application) {
    FontCatalog& catalog = fontCatalog();
    if (catalog.initialized) {
        return;
    }

    QCoreApplication::setOrganizationName(QStringLiteral("Alif"));
    QCoreApplication::setApplicationName(QStringLiteral("Taif"));
    application.setLayoutDirection(Qt::RightToLeft);

    const QString tajawal = registerFont(
        QStringLiteral(":/fonts/resources/fonts/Tajawal/Tajawal-Regular.ttf"),
        QStringLiteral("Tajawal"));
    const QString kawkabMono = registerFont(
        QStringLiteral(":/fonts/resources/fonts/KawkabMono-Regular.ttf"),
        QStringLiteral("Monospace"));
    const QString notoKufi = registerFont(
        QStringLiteral(":/fonts/resources/fonts/NotoKufiArabic-Regular.ttf"),
        QStringLiteral("Noto Kufi Arabic"));
    const QString alaqHalab = registerFont(
        QStringLiteral(":/fonts/resources/fonts/Alaq-Halab.otf"),
        QStringLiteral("Noto Kufi Arabic"));
    const QString hasubiMono = registerFont(
        QStringLiteral(":/fonts/resources/fonts/Hasubi-Mono.ttf"),
        QStringLiteral("Monospace"));

    catalog.tajawalFont = tajawal;
    catalog.notoKufiFont = notoKufi;
    catalog.monospaceFont = kawkabMono;
    appendUnique(catalog.editorFamilies, tajawal);
    appendUnique(catalog.editorFamilies, notoKufi);
    appendUnique(catalog.editorFamilies, kawkabMono);
    appendUnique(catalog.editorFamilies, alaqHalab);
    appendUnique(catalog.editorFamilies, hasubiMono);
    appendUnique(catalog.editorFamilies, QStringLiteral("Monospace"));

    QFont font;
    font.setFamilies(catalog.editorFamilies);
    font.setPixelSize(14);
    font.setWeight(QFont::Normal);
    font.setFixedPitch(true);
    application.setFont(font);
    application.setStyleSheet(globalScrollBarStyle());

    catalog.initialized = true;
}

QString TaifBootstrap::tajawalFontFamily() {
    return fontCatalog().tajawalFont;
}

QString TaifBootstrap::notoKufiFontFamily() {
    return fontCatalog().notoKufiFont;
}

QString TaifBootstrap::monospaceFontFamily() {
    return fontCatalog().monospaceFont;
}

QStringList TaifBootstrap::availableEditorFontFamilies() {
    return fontCatalog().editorFamilies;
}

TaifLaunchRequest TaifBootstrap::parseLaunchRequest(const QStringList& arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("محرر طيف للغة ألف"));
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("ملف ألف المراد فتحه."));

    TaifLaunchRequest request;
    if (!parser.parse(arguments)) {
        request.errorMessage = QStringLiteral("تعذر تحليل معاملات التشغيل: %1")
            .arg(parser.errorText());
        return request;
    }

    const QStringList positionalArguments = parser.positionalArguments();
    if (positionalArguments.size() > 1) {
        request.errorMessage = QStringLiteral("لا يمكن تمرير أكثر من ملف واحد عند التشغيل.");
        return request;
    }
    if (!positionalArguments.isEmpty()) {
        request.filePath = positionalArguments.constFirst();
    }
    return request;
}
