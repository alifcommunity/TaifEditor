#pragma once

#include "TToken.h"

#include <QString>
#include <QTextCharFormat>

// ==================== Themes ====================

// 1. The Strategy Interface
class SyntaxTheme {
public:
    virtual ~SyntaxTheme() = default;
    virtual QString name() const = 0;
    virtual void apply(QHash<TokenType, QTextCharFormat>& formats) const = 0;

protected:
    // Helper to reduce boilerplate in concrete themes
    void setFormat(QHash<TokenType, QTextCharFormat>& formats, TokenType type,
                   QColor color, bool bold = false, bool italic = false) const {
        QTextCharFormat f;
        f.setForeground(color);
        if(bold) f.setFontWeight(QFont::Bold);
        if(italic) f.setFontItalic(true);
        formats[type] = f;
    }
};


// ==================== CONCRETE THEMES ====================

class VSCodeDarkTheme : public SyntaxTheme {
public:
    QString name() const override { return "باهت لطيف"; }
    void apply(QHash<TokenType, QTextCharFormat>& formats) const override {
        setFormat(formats, TokenType::Keyword,      QColor(197, 134, 192), false);
        setFormat(formats, TokenType::BuiltinFunc,  QColor(78, 201, 176));
        setFormat(formats, TokenType::MagicMethod,  QColor(86, 156, 214));
        setFormat(formats, TokenType::Decorator,    QColor(220, 220, 170));
        setFormat(formats, TokenType::Function,     QColor(220, 220, 170));
        setFormat(formats, TokenType::ClassDef,     QColor(78, 201, 176), false);
        setFormat(formats, TokenType::String,       QColor(206, 145, 120));
        setFormat(formats, TokenType::MultiLineString, QColor(206, 145, 120));
        setFormat(formats, TokenType::FString,      QColor(206, 145, 120));
        setFormat(formats, TokenType::Comment,      QColor(106, 153, 85), false, false);
        setFormat(formats, TokenType::Number,       QColor(181, 206, 168));
        setFormat(formats, TokenType::Self,         QColor(86, 156, 214), false);
        setFormat(formats, TokenType::Operator,     QColor(212, 212, 212));
        setFormat(formats, TokenType::Identifier,   QColor(156, 220, 254));
    }
};

class MonokaiTheme : public SyntaxTheme {
public:
    QString name() const override { return "تباين دموي"; }
    void apply(QHash<TokenType, QTextCharFormat>& formats) const override {
        setFormat(formats, TokenType::Keyword,      QColor(249, 38, 114), false); // Pink
        setFormat(formats, TokenType::BuiltinFunc,  QColor(102, 217, 239));       // Light Blue
        setFormat(formats, TokenType::MagicMethod,  QColor(166, 226, 46));       // Green
        setFormat(formats, TokenType::Decorator,    QColor(253, 151, 31));       // Orange
        setFormat(formats, TokenType::Function,     QColor(166, 226, 46));       // Green
        setFormat(formats, TokenType::ClassDef,     QColor(102, 217, 239), false); // Blue
        setFormat(formats, TokenType::String,       QColor(230, 219, 116));       // Yellow
        setFormat(formats, TokenType::MultiLineString, QColor(230, 219, 116));
        setFormat(formats, TokenType::FString,      QColor(230, 219, 116));
        setFormat(formats, TokenType::Comment,      QColor(117, 113, 94), false, false); // Grey
        setFormat(formats, TokenType::Number,       QColor(174, 129, 255));       // Purple
        setFormat(formats, TokenType::Self,         QColor(253, 151, 31), false); // Orange
        setFormat(formats, TokenType::Operator,     QColor(249, 38, 114));       // Pink
        setFormat(formats, TokenType::Identifier,   QColor(248, 248, 242));       // White
    }
};

class OceanicTheme : public SyntaxTheme {
public:
    QString name() const override { return "محيطي عميق"; }
    void apply(QHash<TokenType, QTextCharFormat>& formats) const override {
        setFormat(formats, TokenType::Keyword,      QColor(199, 146, 234), false); // Purple
        setFormat(formats, TokenType::BuiltinFunc,  QColor(130, 170, 255));       // Blue
        setFormat(formats, TokenType::MagicMethod,  QColor(137, 221, 255));       // Cyan
        setFormat(formats, TokenType::Decorator,    QColor(195, 232, 141));       // Green
        setFormat(formats, TokenType::Function,     QColor(130, 170, 255));       // Blue
        setFormat(formats, TokenType::ClassDef,     QColor(255, 203, 107), false); // Yellow
        setFormat(formats, TokenType::String,       QColor(195, 232, 141));       // Green
        setFormat(formats, TokenType::MultiLineString, QColor(195, 232, 141));
        setFormat(formats, TokenType::FString,      QColor(195, 232, 141));
        setFormat(formats, TokenType::Comment,      QColor(84, 110, 122), false, false); // Blue Grey
        setFormat(formats, TokenType::Number,       QColor(247, 140, 108));       // Orange
        setFormat(formats, TokenType::Self,         QColor(255, 83, 112), false); // Red
        setFormat(formats, TokenType::Operator,     QColor(137, 221, 255));       // Cyan
        setFormat(formats, TokenType::Identifier,   QColor(238, 255, 255));       // White
    }
};

class TaifGlowTheme : public SyntaxTheme {
public:
    QString name() const override { return "طيفي متوهج"; }
    void apply(QHash<TokenType, QTextCharFormat>& formats) const override {
        setFormat(formats, TokenType::Keyword,      QColor(255, 100, 100), false); // Purple
        setFormat(formats, TokenType::BuiltinFunc,  QColor(90, 180, 255));       // Blue
        setFormat(formats, TokenType::MagicMethod,  QColor(255, 110, 255));       // Cyan
        setFormat(formats, TokenType::Decorator,    QColor(210, 160, 255));       // Green
        setFormat(formats, TokenType::Function,     QColor(210, 160, 255));       // Blue
        setFormat(formats, TokenType::ClassDef,     QColor(255, 110, 255), false); // Yellow
        setFormat(formats, TokenType::String,       QColor(80, 240, 80));       // Green
        setFormat(formats, TokenType::MultiLineString, QColor(80, 240, 80));
        setFormat(formats, TokenType::FString,      QColor(80, 240, 80));
        setFormat(formats, TokenType::Comment,      QColor(140, 150, 190), false, false); // Blue Grey
        setFormat(formats, TokenType::Number,       QColor(255, 220, 80));       // Orange
        setFormat(formats, TokenType::Self,         QColor(90, 180, 255), false); // Red
        setFormat(formats, TokenType::Operator,     QColor(40, 240, 240));       // Cyan
        setFormat(formats, TokenType::Identifier,   QColor(248, 248, 255));       // White
    }
};

