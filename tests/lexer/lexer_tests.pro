QT += core testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = TLexerTests

INCLUDEPATH += ../../source/aliflang/lexer

SOURCES += \
    AlifLexerTest.cpp \
    ../../source/aliflang/lexer/AlifLexer.cpp

HEADERS += \
    ../../source/aliflang/lexer/AlifLexer.h
