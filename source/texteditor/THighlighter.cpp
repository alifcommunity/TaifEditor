#include "THighlighter.h"
#include <QDebug>
#include <QFont>
#include <QStringList>
#include <QJsonParseError>
#include <QJsonDocument>
#include <QFile>
#include <QJsonObject>

THighlighter::THighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    loadSyntaxDefinition(":/json/resources/highlight-defination/alif.json");
}

void THighlighter::loadSyntaxDefinition(const QString &filePath)
{
    qDebug() << "[THighlighter] trying path:" << filePath;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[THighlighter] QFile open failed:" << f.errorString();
        return;
    }

    QByteArray data = f.readAll();
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);

    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[THighlighter] JSON parse error:" << err.errorString();
        return;
    }

    if (!doc.isObject()) {
        qWarning() << "[THighlighter] JSON root is not an object";
        return;
    }

    if (m_definition.loadFromJson(doc.object())) {
        rehighlight();
        qDebug() << "[THighlighter] تم تحميل تعريف الصيغة بنجاح من JSON";
    } else {
        qWarning() << "[THighlighter] فشل تحميل تعريف الصيغة من JSON";
    }
}



void THighlighter::highlightBlock(const QString &text)
{
    QVector<Token> tokens = m_lexer.tokenize(text, 0);
    for (const auto& token : tokens) {
        QString styleName;

        switch (token.type) {
        case TokenType::Keyword:
            styleName = "Definition Keyword";
            break;
        case TokenType::BiltinK:
            styleName = "Builtin Function";
            break;
        case TokenType::SpecialK:
            styleName = "Special Variable";
            break;
        case TokenType::Number:
            styleName = "Float";
            break;
        case TokenType::Comment:
            styleName = "Comment";
            break;
        case TokenType::String:
            styleName = "String";
            break;
        case TokenType::Operator:
            styleName = "Operator";
            break;
        case TokenType::Identifier: {
            QString tokenText = token.text;
            if (isFunctionName(text, token.startPos + token.len)) {
                styleName = "Overloaders";
            } else {
                QStringList builtins = {"اطبع", "ادخل", "مدى", "صحيح", "عشري", "منطق", "طول", "نوع"};
                if (builtins.contains(tokenText)) styleName = "Builtin Function";
                else styleName = "Normal Text";
            }
        } break;
        default:
            styleName = "";
            break;
        }

        if (!styleName.isEmpty()) {
            QTextCharFormat format = m_definition.getStyleFormat(styleName);
            if (format.isValid()) setFormat(token.startPos, token.len, format);
            else qWarning() << "[THighlighter] نمط غير موجود في JSON:" << styleName;
        }
    }

}

bool THighlighter::isFunctionName(const QString& blockText, int idEndPos) {
    int pos = idEndPos;
    while (pos < blockText.length() && blockText[pos].isSpace()) {
        pos++;
    }
    return (pos < blockText.length() && blockText[pos] == '(');
}
