#include "AutoComplete.h"
#include <QRegularExpression>
#include <QSet>

// --- Keyword Strategy ---
KeywordStrategy::KeywordStrategy() {
    keywords = {"احذف", "دالة", "صنف", "استورد",
                "من", "بينما", "لكل", "اذا",
                "اواذا", "والا", "ارجع", "أوإذا",
                "حاول", "خلل", "توقف", "استمر",
                "و", "او", "اصل", "أو",
                "عدم", "صح", "خطا", "خطأ",
                "ك", "في", "عام", "عند",
                "مزامنة", "من", "مرر", "ليس",
                "وإلا", "هل", "نهاية", "نطاق",
                "انتج", "إذا", "هذا", "خطية"
    };
}

QVector<CompletionItem> KeywordStrategy::getSuggestions(const QString &prefix, const QString &) {
    QVector<CompletionItem> items;
    if (prefix.isEmpty()) return items;

    for (const auto &k : keywords) {
        if (k.startsWith(prefix, Qt::CaseInsensitive)) {
            items.push_back({k, k, "كلمة محجوزة في اللغة", CompletionType::Keyword});
        }
    }
    return items;
}

// --- Built-ins Strategy ---
BuiltinStrategy::BuiltinStrategy() {
    builtins = {"تحقق_اي", "افتح", "ادخل", "اطبع",
                "مصفوفة", "مترابطة", "عشري", "صحيح",
                "اقصى", "طول", "ادنى", "منطق",
                "مدى", "هل_نوع", "اجمع", "مقرون"
                };
}

QVector<CompletionItem> BuiltinStrategy::getSuggestions(const QString &prefix, const QString &) {
    QVector<CompletionItem> items{};
    if (prefix.isEmpty()) return items;

    for (const auto &b : builtins) {
        if (b.startsWith(prefix, Qt::CaseInsensitive)) {
            items.push_back({b, b, "دالة ضمن لغة ألف", CompletionType::Builtin});
        }
    }
    return items;
}

// --- Snippet Strategy ---
QVector<CompletionItem> SnippetStrategy::getSuggestions(const QString &prefix, const QString &) {
    QVector<CompletionItem> items;
    QString p = prefix.toLower();

    if (QString("دالة").startsWith(p)) {
        items.push_back({"دالة",
                         "دالة اسم(معاملات):\n\tمرر",
                         "تعريف دالة",
                         CompletionType::Snippet});
    }
    if (QString("صنف").startsWith(p)) {
        items.push_back({"صنف",
                         "صنف اسم:\n\tدالة __تهيئة__(هذا):\n\t\tمرر",
                         "تعريف صنف",
                         CompletionType::Snippet});
    }
    if (QString("اذا").startsWith(p)) {
        items.push_back({"اذا",
                         "اذا الشرط:\n\tمرر",
                         "حالة إذا الشرطية",
                         CompletionType::Snippet});
    }
    if (QString("لكل").startsWith(p)) {
        items.push_back({"لكل",
                         "لكل عنصر في العناصر:\n\tمرر",
                         "حلقة لكل التكرارية",
                         CompletionType::Snippet});
    }
    if (QString("حاول").startsWith(p)) {
        items.push_back({"حاول",
                         "حاول:\n\tمرر\nخلل:\n\tمرر",
                         "كتلة حاول/خلل",
                         CompletionType::Snippet});
    }
    if (QString("بينما").startsWith(p)) {
        items.push_back({"بينما",
                         "بينما الشرط:\n\tمرر",
                         "حلقة بينما التكرارية",
                         CompletionType::Snippet});
    }
    if (QString("خطية").startsWith(p)) {
        items.push_back({"خطية",
                         "خطية معاملات: مرر",
                         "حلقة بينما التكرارية",
                         CompletionType::Snippet});
    }

    return items;
}

// --- Dynamic Word Strategy ---
QVector<CompletionItem> DynamicWordStrategy::getSuggestions(const QString &prefix, const QString &fullText) {
    QVector<CompletionItem> items;
    if (prefix.length() < 2) return items;

    static QRegularExpression re("[a-zA-Z0-9_\u0600-\u06FF_0-9]+");
    QRegularExpressionMatchIterator i = re.globalMatch(fullText);

    QSet<QString> uniqueWords{};
    while (i.hasNext()) {
        QString word = i.next().captured(0);
        // Only add words that match prefix but are not the prefix itself (to avoid duplicates of what we are typing)
        if (word != prefix && word.startsWith(prefix, Qt::CaseInsensitive)) {
            uniqueWords.insert(word);
        }
    }

    for (const auto &w : uniqueWords) {
        items.push_back({w, w, "نص ضمن الملف الحالي", CompletionType::DynamicWord});
    }


    return items;
}
