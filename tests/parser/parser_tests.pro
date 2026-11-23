QT += core testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = TaifParserTests


INCLUDEPATH += \
    ../../source/aliflang/lexer \
    ../../source/aliflang/parser

SOURCES += \
    AlifParserTest.cpp \
    ../../source/aliflang/lexer/AlifLexer.cpp \
    ../../source/aliflang/parser/AlifParser.cpp

HEADERS += \
    ../../source/aliflang/lexer/AlifLexer.h \
    ../../source/aliflang/parser/AlifParser.h
