#include "SyntaxDefinition.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFont>
#include <QJsonParseError>

bool SyntaxDefinition::load(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "لا يمكن فتح ملف تعريف الصيغة:" << filePath << file.errorString();
        return false;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (doc.isNull() or !doc.isObject()) {
        qWarning() << "JSON غير صالح في ملف تعريف الصيغة:" << filePath;
        qWarning() << "خطأ التحليل:" << parseError.errorString() << "عند الموضع:" << parseError.offset;
        return false;
    }

    QJsonObject root = doc.object();

    qDebug() << "مفاتيح JSON الجذرية:" << root.keys();

    QJsonObject stylesObject;

    if (root.contains("styles") && root["styles"].isObject()) {
        stylesObject = root["styles"].toObject();
    }
    else if (root.contains("highlighting") && root["highlighting"].isObject()) {
        QJsonObject highlighting = root["highlighting"].toObject();
        if (highlighting.contains("styles") && highlighting["styles"].isObject()) {
            stylesObject = highlighting["styles"].toObject();
        }
    }
    else if (root.contains("language") && root["language"].isObject()) {
        QJsonObject language = root["language"].toObject();
        stylesObject = findStylesObject(language);
    }

    if (stylesObject.isEmpty()) {
        qWarning() << "لم يتم العثور على كائن الأنماط في ملف JSON";
        qDebug() << "هيكل JSON الكامل:" << root;
        return false;
    }

    if (!parseStyles(stylesObject)) {
        return false;
    }

    qDebug() << "تم تحميل" << m_styles.size() << "نمط بنجاح";
    return true;
}

QTextCharFormat SyntaxDefinition::createFormatFromKateObject(const QJsonObject &styleObj)
{
    QTextCharFormat format;

    qDebug() << "معالجة كائن النمط، المفاتيح:" << styleObj.keys();

    if (styleObj.contains("color") && styleObj["color"].isString()) {
        QColor color(styleObj["color"].toString());
        if (color.isValid()) {
            format.setForeground(color);
            qDebug() << "  - لون النص:" << color.name();
        }
    }

    if (styleObj.contains("selColor") && styleObj["selColor"].isString()) {
        QColor color(styleObj["selColor"].toString());
        if (color.isValid()) {
            format.setBackground(color);
            qDebug() << "  - لون الخلفية:" << color.name();
        }
    }

    if (styleObj.contains("bold") && styleObj["bold"].isBool()) {
        format.setFontWeight(styleObj["bold"].toBool() ? QFont::Bold : QFont::Normal);
        qDebug() << "  - غامق:" << styleObj["bold"].toBool();
    }

    if (styleObj.contains("italic") && styleObj["italic"].isBool()) {
        format.setFontItalic(styleObj["italic"].toBool());
        qDebug() << "  - مائل:" << styleObj["italic"].toBool();
    }

    if (styleObj.contains("underline") && styleObj["underline"].isBool()) {
        format.setFontUnderline(styleObj["underline"].toBool());
        qDebug() << "  - خط سفلي:" << styleObj["underline"].toBool();
    }

    if (format.isEmpty()) {
        qDebug() << "  - النمط فارغ";
        return QTextCharFormat();
    }

    return format;
}

QJsonObject SyntaxDefinition::findStylesObject(const QJsonObject &obj)
{
    if (obj.contains("styles") && obj["styles"].isObject()) {
        return obj["styles"].toObject();
    }

    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isObject()) {
            QJsonObject result = findStylesObject(it.value().toObject());
            if (!result.isEmpty()) {
                return result;
            }
        }
    }

    return QJsonObject();
}

bool SyntaxDefinition::parseKateStyles(const QJsonObject &stylesObject)
{
    m_styles.clear();

    for (auto it = stylesObject.constBegin(); it != stylesObject.constEnd(); ++it) {
        QString styleName = it.key();

        if (!it.value().isObject()) {
            qDebug() << "النمط" << styleName << "ليس كائن، تخطي";
            continue;
        }

        QJsonObject styleObj = it.value().toObject();
        QTextCharFormat format = createFormatFromKateObject(styleObj);

        if (format.isValid()) {
            m_styles.insert(styleName, format);
            qDebug() << "تم إنشاء النمط:" << styleName;
        } else {
            qDebug() << "فشل إنشاء النمط:" << styleName;
        }
    }

    if (m_styles.isEmpty()) {
        qWarning() << "لم يتم إنشاء أي أنماط من الكائن";
        return false;
    }

    qDebug() << "تم تحميل" << m_styles.size() << "نمط من تنسيق Kate";
    return true;
}

bool SyntaxDefinition::loadFromJson(const QJsonObject &root)
{
    qDebug() << "=== تحليل ملف الصيغة ===";

    if (root.contains("language") && root["language"].isObject()) {
        QJsonObject language = root["language"].toObject();

        if (language.contains("highlighting") && language["highlighting"].isObject()) {
            QJsonObject highlighting = language["highlighting"].toObject();
            qDebug() << "مفاتيح highlighting:" << highlighting.keys();

            if (highlighting.contains("itemDatas") && highlighting["itemDatas"].isObject()) {
                QJsonObject itemDatas = highlighting["itemDatas"].toObject();
                qDebug() << "تم العثور على itemDatas!";

                return parseItemDatas(itemDatas);
            } else {
                qWarning() << "لم يتم العثور على itemDatas في highlighting";
            }
        }
    }

    qWarning() << "فشل في العثور على الهيكل المتوقع";
    return false;
}

bool SyntaxDefinition::parseStyles(const QJsonObject &stylesObject)
{
    m_styles.clear();
    for (auto it = stylesObject.constBegin(); it != stylesObject.constEnd(); ++it) {
        if (!it.value().isObject()) {
            qWarning() << "Invalid style format for:" << it.key();
            continue;
        }
        m_styles.insert(it.key(), createFormat(it.value().toObject()));
    }
    return !m_styles.isEmpty();
}

QTextCharFormat SyntaxDefinition::createFormat(const QJsonObject &styleObject)
{
    QTextCharFormat format;
    if (styleObject.contains("foreground") && styleObject["foreground"].isString()) {
        format.setForeground(QColor(styleObject["foreground"].toString()));
    }
    if (styleObject.contains("background") && styleObject["background"].isString()) {
        format.setBackground(QColor(styleObject["background"].toString()));
    }
    if (styleObject.contains("bold") && styleObject["bold"].isBool() && styleObject["bold"].toBool()) {
        format.setFontWeight(QFont::Bold);
    }
    if (styleObject.contains("italic") && styleObject["italic"].isBool() && styleObject["italic"].toBool()) {
        format.setFontItalic(true);
    }
    if (styleObject.contains("underline") && styleObject["underline"].isBool() && styleObject["underline"].toBool()) {
        format.setFontUnderline(true);
    }
    return format;
}

bool SyntaxDefinition::parseItemDatas(const QJsonObject &itemDatas)
{
    m_styles.clear();

    if (itemDatas.contains("itemData") && itemDatas["itemData"].isArray()) {
        QJsonArray itemDataArray = itemDatas["itemData"].toArray();
        qDebug() << "عدد عناصر itemData:" << itemDataArray.size();

        for (const QJsonValue &itemValue : itemDataArray) {
            if (!itemValue.isObject()) continue;

            QJsonObject itemObj = itemValue.toObject();

            if (itemObj.contains("@attributes") && itemObj["@attributes"].isObject()) {
                QJsonObject attributes = itemObj["@attributes"].toObject();

                if (attributes.contains("name") && attributes.contains("defStyleNum")) {
                    QString styleName = attributes["name"].toString();
                    QString defStyleNum = attributes["defStyleNum"].toString();

                    QTextCharFormat format = createFormatFromStyleNum(defStyleNum);
                    if (format.isValid()) {
                        m_styles.insert(styleName, format);
                        qDebug() << "تم إنشاء النمط:" << styleName << "->" << defStyleNum;
                    }
                }
            }
        }
    } else {
        qWarning() << "itemData ليس مصفوفة أو غير موجود";
        return false;
    }

    qDebug() << "تم تحميل" << m_styles.size() << "نمط بنجاح";
    return !m_styles.isEmpty();
}

QTextCharFormat SyntaxDefinition::createFormatFromStyleNum(const QString &defStyleNum)
{
    QTextCharFormat format;

    // تعيين الألوان بناءً على defStyleNum
    if (defStyleNum == "dsNormal") {
        // النص العادي - لون افتراضي
        format.setForeground(QColor(248, 248, 255));
    }
    else if (defStyleNum == "dsKeyword" or defStyleNum == "dsControlFlow") {
        // الكلمات المفتاحية
        format.setForeground(QColor(255, 100, 100));
    }
    else if (defStyleNum == "dsOperator") {
        // المعاملات
        format.setForeground(QColor(40, 240, 240));
    }
    else if (defStyleNum == "dsBuiltIn") {
        // الدوال المدمجة
        format.setForeground(QColor(90, 180, 255));
    }
    else if (defStyleNum == "dsFunction") {
        // الدوال
        format.setForeground(QColor(210, 160, 255));
    }
    else if (defStyleNum == "dsVariable") {
        // كلمات مفتاحية خاصة
        format.setForeground(QColor(255, 110, 255));
    }
    else if (defStyleNum == "dsString" or defStyleNum == "dsVerbatimString" or defStyleNum == "dsSpecialString") {
        // النصوص
        format.setForeground(QColor(80, 240, 80));
    }
    else if (defStyleNum == "dsComment") {
        // التعليقات
        format.setForeground(QColor(140, 150, 190));
    }
    else if (defStyleNum == "dsFloat" or defStyleNum == "dsDecVal" or defStyleNum == "dsBaseN") {
        // الأرقام
        format.setForeground(QColor(255, 220, 80));
    }
    // else if (defStyleNum == "dsImport" or defStyleNum == "dsPreprocessor") {
    //     // الاستيراد والمعالجات
    //     format.setForeground(QColor("#c586c0"));
    // }
    else if (defStyleNum == "dsError") {
        // الأخطاء
        format.setForeground(QColor(210, 15, 57));
    }
    // else if (defStyleNum == "dsAttribute") {
    //     // السمات
    //     format.setForeground(QColor("#9cdcfe"));
    // }
    // else if (defStyleNum == "dsChar") {
    //     // المحارف
    //     format.setForeground(QColor("#ce9178"));
    // }
    // else if (defStyleNum == "dsSpecialChar") {
    //     // المحارف الخاصة
    //     format.setForeground(QColor("#d7ba7d"));
    // }
    else {
        // نمط افتراضي
        format.setForeground(QColor(250, 250, 250));
        qDebug() << "نمط غير معروف:" << defStyleNum << "-> استخدام الافتراضي";
    }

    return format;
}

bool SyntaxDefinition::parseRules(const QJsonArray &rulesArray)
{
    m_rules.clear();
    for (const QJsonValue &value : rulesArray) {
        if (!value.isObject()) continue;
        QJsonObject ruleObject = value.toObject();

        if (!ruleObject.contains("match") or !ruleObject["match"].isString()) continue;

        HighlightingRule rule;
        rule.pattern = QRegularExpression(ruleObject["match"].toString());
        if (!rule.pattern.isValid()) {
            qWarning() << "Invalid regex pattern:" << ruleObject["match"].toString() << rule.pattern.errorString();
            continue;
        }

        if (ruleObject.contains("style") && ruleObject["style"].isString()) {
            QString styleName = ruleObject["style"].toString();
            if (m_styles.contains(styleName)) {
                rule.formats.insert(0, m_styles[styleName]);
            } else {
                qWarning() << "Undefined style name:" << styleName;
            }
        }

        if (ruleObject.contains("captures") && ruleObject["captures"].isObject()) {
            QJsonObject captures = ruleObject["captures"].toObject();
            for (auto it = captures.constBegin(); it != captures.constEnd(); ++it) {
                bool ok;
                int captureIndex = it.key().toInt(&ok);
                if (!ok or captureIndex <= 0) continue;

                if (it.value().isObject()) {
                    QJsonObject captureStyleObj = it.value().toObject();
                    if (captureStyleObj.contains("style") && captureStyleObj["style"].isString()) {
                        QString styleName = captureStyleObj["style"].toString();
                        if (m_styles.contains(styleName)) {
                            rule.formats.insert(captureIndex, m_styles[styleName]);
                        } else {
                            qWarning() << "Undefined style name in capture:" << styleName;
                        }
                    }
                }
            }
        }

        if (!rule.formats.isEmpty()) {
            m_rules.append(rule);
        }
    }
    return !m_rules.isEmpty();
}

const QVector<HighlightingRule>& SyntaxDefinition::rules() const
{
    return m_rules;
}

QTextCharFormat SyntaxDefinition::getStyleFormat(const QString &styleName) const
{
    return m_styles.value(styleName, QTextCharFormat());
}
