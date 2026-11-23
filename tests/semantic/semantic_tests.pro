QT += core testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = TaifSemanticTests

INCLUDEPATH += \
    ../../source/aliflang/lexer \
    ../../source/aliflang/parser \
    ../../source/aliflang/semantic

SOURCES += \
    SymbolTableTest.cpp \
    ../../source/aliflang/lexer/AlifLexer.cpp \
    ../../source/aliflang/parser/AlifParser.cpp \
    ../../source/aliflang/semantic/AlifSymbolTable.cpp

HEADERS += \
    ../../source/aliflang/lexer/AlifLexer.h \
    ../../source/aliflang/parser/AlifParser.h \
    ../../source/aliflang/semantic/AlifSymbolTable.h
