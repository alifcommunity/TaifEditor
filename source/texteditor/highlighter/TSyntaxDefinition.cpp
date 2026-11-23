#include "TSyntaxDefinition.h"

// ==================== Language Definition ====================

LanguageDefinition::LanguageDefinition() {
    QStringList keywords = {
        "و", "او", "ك", "متوقع", "مزامنة", "انتظر", "توقف", "استمر", "أو",
        "احذف", "أوإذا" ,"اواذا", "وإلا", "والا", "خلل", "خطأ" ,"خطا", "نهاية", "لكل",
        "من", "عام", "اذا", "استورد", "في", "هل", "إذا", "عدم",
        "نطاق", "ليس", "مرر", "ارجع", "صح", "حاول",
        "بينما", "عند", "انتج", "خطية"
    };

    QStringList builtins = {
        "", "", "", "منطق", "فهرس", "هل_نوع", "", "عشري",
        "ادخل", "صحيح", "طول", "مصفوفة", "", "اقصى", "ادنى", "افتح", "اطبع",
        "مدى", "مميزة", "نص", "", "اصل", "مترابطة", "نوع", "تحقق_اي", "اجمع",
        "مقرون"
    };

    QStringList magics = {
        "__تهيئة__", "__اس_ع__", "__عرض__", "__استدعاء__", "__اس__", "__سالب__",
        "____", "__اضرب__", "__اطرح__", "__اجمع__", "__اجمع_ع__", "__اطرح_ع__", "__اضرب_ع__", "____"
    };

    keywordSet = QSet<QString>(keywords.begin(), keywords.end());
    builtinSet = QSet<QString>(builtins.begin(), builtins.end());
    magicSet = QSet<QString>(magics.begin(), magics.end());

    hexPattern = QRegularExpression(R"(\b0[xX][0-9a-fA-F]+\b)");
    numberPattern = QRegularExpression(R"(\b\d+(\.\d+)?([eE][+-]?\d+)?\b)");
}
