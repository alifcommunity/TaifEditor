#pragma once

#include <QString>
#include <QSet>

enum class TokenType {
    Keyword,
    BiltinK,
    SpecialK,
    Identifier,
    Number,
    Comment,
    String,
    Operator,
};

class Token {
public:
    TokenType type{};
    int startPos{};
    int len{};
    QString text{};

    Token(TokenType t, int start, int len, QString text) :
        type(t), startPos(start), len(len), text(text) {}
};


class Lexer {
public:
    QVector<Token> tokenize(const QString& text, int oldPos);

private:
    QVector<Token> tokens{};
    int pos = 0;
    int quoteCount = 0;
    int isFString = 0;

    bool isKeyword(const QString& word);
    bool isBiltinK(const QString& word);
    bool isSpecialK(const QString& word);
};
