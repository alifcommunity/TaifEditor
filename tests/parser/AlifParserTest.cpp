#include "AlifParser.h"

#include <QtCore/QFile>
#include <QtTest/QTest>

namespace {

bool hasAstKind(const AstModule& module, const AstNodeKind kind) {
    for (const AstNode& node : module.nodes()) {
        if (node.kind == kind) {
            return true;
        }
    }
    return false;
}

AstNodeId childWithRole(const AstNode& node, const AstChildRole role) {
    for (qsizetype index = 0; index < node.childRoles.size(); ++index) {
        if (node.childRoles.at(index) == role) {
            return node.children.at(index);
        }
    }
    return InvalidAstNodeId;
}

bool hasDiagnostic(const ParseResult& result, const QString& code) {
    for (const ParseDiagnostic& diagnostic : result.parserDiagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

class TaifParserTest final : public QObject {
    Q_OBJECT

private slots:
    void emptySourceCreatesCompleteSnapshots();
    void parserNormalizesMissingEndOfFile();
    void declarationsAndSuitesCreateSemanticNodes();
    void bareStarSeparatesKeywordOnlyParameters();
    void globalStatementParsesNameList();
    void exceptionClauseBindsCaughtError();
    void decoratorsPrefixDeclarationsWithoutDiagnostics();
    void forHeaderTreatsInAsAStructuralDelimiter();
    void loopElseClauseParses_data();
    void loopElseClauseParses();
    void contextManagerStatementParses_data();
    void contextManagerStatementParses();
    void fromImportAcceptsWildcard_data();
    void fromImportAcceptsWildcard();
    void fromImportPreservesNamedImports();
    void importAliasesPreserveBindings();
    void tupleComprehensionPreservesElementsAndTargets_data();
    void tupleComprehensionPreservesElementsAndTargets();
    void ordinaryTupleKeepsFlatElementsAndBinaryPrecedence();
    void comprehensionFiltersParseWithoutElse_data();
    void comprehensionFiltersParseWithoutElse();
    void inlineConditionalsStillRequireElse();
    void chainedComprehensionsPreserveClauseOrder();
    void expressionsUsePrattPostfixAndPrecedenceParsing();
    void identityComparisonsParseInsideCalls_data();
    void identityComparisonsParseInsideCalls();
    void radixLiteralsAndBitwiseExpressions_data();
    void radixLiteralsAndBitwiseExpressions();
    void bitwiseOperatorsRespectPrecedence();
    void formattedStringCreatesStructuredAst();
    void prefixedStringsAreSingleExpressions_data();
    void prefixedStringsAreSingleExpressions();
    void recoveryPreservesLaterDeclarations();
    void lexerDiagnosticsAreRetained();
    void reparseFallsBackToAnEquivalentFreshSnapshot();
    void passStatementIsParsedWithoutError();
    void statusCorpusProducesAFiniteTree();
};

void TaifParserTest::emptySourceCreatesCompleteSnapshots() {
    const ParseResult result = TaifParser().parse(QString());

    QVERIFY(result.syntaxTree != nullptr);
    QVERIFY(result.ast != nullptr);
    QCOMPARE(result.syntaxTree->root().kind, SyntaxKind::Module);
    QCOMPARE(result.ast->root().kind, AstNodeKind::Module);
    QVERIFY(result.parserDiagnostics.isEmpty());
    QCOMPARE(result.syntaxTree->tokens().constLast().kind, TokenKind::EndOfFile);
}

void TaifParserTest::parserNormalizesMissingEndOfFile() {
    LexResult lexicalResult;
    Token identifier;
    identifier.kind = TokenKind::Identifier;
    identifier.channel = TokenChannel::Main;
    identifier.lexeme = QStringLiteral("س");
    identifier.range = {{0, 1, 1}, {1, 1, 2}};
    lexicalResult.tokens.append(identifier);

    const ParseResult result = TaifParser().parse(QStringLiteral("س"), lexicalResult);
    QVERIFY(result.syntaxTree != nullptr);
    QCOMPARE(result.syntaxTree->tokens().constLast().kind, TokenKind::EndOfFile);
    QVERIFY(result.ast != nullptr);
}

void TaifParserTest::declarationsAndSuitesCreateSemanticNodes() {
    const QString source = QStringLiteral(
        "دالة جمع(س, ص = 1):\n"
        "\tارجع س + ص\n"
        "صنف مثال:\n"
        "\tدالة __تهيئة__(هذا):\n"
        "\t\tهذا.س = 1\n");
    const ParseResult result = TaifParser().parse(source);

    QVERIFY(result.syntaxTree != nullptr);
    QVERIFY(result.ast != nullptr);
    QVERIFY(result.parserDiagnostics.isEmpty());
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::FunctionDeclaration));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::ClassDeclaration));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::Parameter));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::AssignmentStatement));
}

void TaifParserTest::bareStarSeparatesKeywordOnlyParameters() {
    const ParseResult result = TaifParser().parse(QStringLiteral(
        "دالة مثال(هذا, الاسم, المحمل, *, الاساس=عدم, حالة_المحمل=عدم, هل_حزمة=عدم):\n"
        "\tمرر\n"));

    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    const AstNode* function = nullptr;
    for (const AstNode& node : result.ast->nodes()) {
        if (node.kind == AstNodeKind::FunctionDeclaration) {
            function = &node;
            break;
        }
    }
    QVERIFY(function != nullptr);
    const AstNodeId parameterList = childWithRole(*function, AstChildRole::ParameterList);
    QVERIFY(parameterList != InvalidAstNodeId);
    const AstNode& parameters = result.ast->node(parameterList);
    QCOMPARE(parameters.children.size(), qsizetype(6));
    const QStringList expectedNames {
        QStringLiteral("هذا"), QStringLiteral("الاسم"), QStringLiteral("المحمل"),
        QStringLiteral("الاساس"), QStringLiteral("حالة_المحمل"), QStringLiteral("هل_حزمة")
    };
    for (qsizetype index = 0; index < expectedNames.size(); ++index) {
        QCOMPARE(result.ast->node(parameters.children.at(index)).text,
                 expectedNames.at(index));
    }
}

void TaifParserTest::globalStatementParsesNameList() {
    const ParseResult result = TaifParser().parse(
        QStringLiteral("دالة عدل():\n\tعام متغير, آخر\n\tمتغير = 1\n"));
    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::GlobalStatement));
    const AstNode* statement = nullptr;
    for (const AstNode& node : result.ast->nodes()) {
        if (node.kind == AstNodeKind::GlobalStatement) {
            statement = &node;
            break;
        }
    }
    QVERIFY(statement != nullptr);
    QCOMPARE(statement->children.size(), qsizetype(2));
    QCOMPARE(result.ast->node(statement->children.at(0)).text, QStringLiteral("متغير"));
    QCOMPARE(result.ast->node(statement->children.at(1)).text, QStringLiteral("آخر"));
    QCOMPARE(result.syntaxTree->nodes().at(statement->syntaxNode).kind,
             SyntaxKind::GlobalStatement);
}

void TaifParserTest::exceptionClauseBindsCaughtError() {
    const ParseResult result = TaifParser().parse(QStringLiteral(
        "حاول:\n\tمرر\n"
        "خلل خطأ_استيراد ك خ:\n\tمرر\n"));

    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    const AstNode* clause = nullptr;
    for (const AstNode& node : result.ast->nodes()) {
        if (node.kind == AstNodeKind::ExceptClause) {
            clause = &node;
            break;
        }
    }
    QVERIFY(clause != nullptr);
    const AstNodeId type = childWithRole(*clause, AstChildRole::ExceptionType);
    const AstNodeId target = childWithRole(*clause, AstChildRole::ExceptionTarget);
    QVERIFY(type != InvalidAstNodeId);
    QVERIFY(target != InvalidAstNodeId);
    QCOMPARE(result.ast->node(type).text, QStringLiteral("خطأ_استيراد"));
    QCOMPARE(result.ast->node(target).text, QStringLiteral("خ"));
}

void TaifParserTest::decoratorsPrefixDeclarationsWithoutDiagnostics() {
    const ParseResult result = TaifParser().parse(
        QStringLiteral("@مميز\nدالة جمع(س):\n\tارجع س\n"));

    QVERIFY(result.syntaxTree != nullptr);
    QVERIFY(result.ast != nullptr);
    QVERIFY(result.parserDiagnostics.isEmpty());
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::FunctionDeclaration));
}

void TaifParserTest::forHeaderTreatsInAsAStructuralDelimiter() {
    const QString source = QStringLiteral(
        "لكل ب في مدى(5):\n"
        "\tاطبع(ب)\n");
    const ParseResult result = TaifParser().parse(source);

    QVERIFY(result.parserDiagnostics.isEmpty());
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::ForStatement));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::CallExpression));
}

void TaifParserTest::loopElseClauseParses_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<bool>("hasElse");
    QTest::newRow("for-else") << QStringLiteral(
        "لكل س في متغير:\n\tمرر\nاذن:\n\tمرر\n") << true;
    QTest::newRow("while-else") << QStringLiteral(
        "بينما صح:\n\tمرر\nاذن:\n\tمرر\n") << true;
    QTest::newRow("for-without-else") << QStringLiteral(
        "لكل س في متغير:\n\tمرر\n") << false;
    QTest::newRow("while-without-else") << QStringLiteral(
        "بينما صح:\n\tمرر\n") << false;
}

void TaifParserTest::loopElseClauseParses() {
    QFETCH(QString, source);
    QFETCH(bool, hasElse);
    const ParseResult result = TaifParser().parse(source);
    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    QCOMPARE(result.ast->root().children.size(), qsizetype(1));
    const AstNode& loop = result.ast->node(result.ast->root().children.constFirst());
    QCOMPARE(loop.kind, source.contains(QStringLiteral("بينما"))
                            ? AstNodeKind::WhileStatement : AstNodeKind::ForStatement);
    const AstNodeId elseClauseId = childWithRole(loop, AstChildRole::ElseBody);
    QCOMPARE(elseClauseId != InvalidAstNodeId, hasElse);
    if (hasElse) {
        const AstNode& elseClause = result.ast->node(elseClauseId);
        QCOMPARE(elseClause.kind, AstNodeKind::ElseClause);
        QCOMPARE(result.syntaxTree->nodes().at(elseClause.syntaxNode).kind,
                 SyntaxKind::ElseClause);
        const AstNodeId body = childWithRole(elseClause, AstChildRole::Body);
        QVERIFY(body != InvalidAstNodeId);
        QCOMPARE(result.ast->node(body).kind, AstNodeKind::Suite);
        QVERIFY(hasAstKind(*result.ast, AstNodeKind::PassStatement));
    }
}

void TaifParserTest::contextManagerStatementParses_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("targetName");
    QTest::newRow("file-alias") << QStringLiteral(
        "عند افتح(\"تجربة.الف\", \"ك\") ك ملف:\n\tمرر\n")
        << QStringLiteral("ملف");
    QTest::newRow("without-alias") << QStringLiteral(
        "عند افتح(\"تجربة.الف\", \"ك\"):\n\tمرر\n")
        << QString();
    QTest::newRow("tuple-alias") << QStringLiteral(
        "عند احصل_على_سياق() ك ملف, حالة:\n\tمرر\n")
        << QStringLiteral("ملف");
}

void TaifParserTest::contextManagerStatementParses() {
    QFETCH(QString, source);
    QFETCH(QString, targetName);
    const ParseResult result = TaifParser().parse(source);
    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    QCOMPARE(result.ast->root().children.size(), qsizetype(1));
    const AstNode& statement = result.ast->node(result.ast->root().children.constFirst());
    QCOMPARE(statement.kind, AstNodeKind::WithStatement);
    QCOMPARE(statement.childRoles.at(0), AstChildRole::ContextExpression);
    QCOMPARE(result.ast->node(statement.children.at(0)).kind, AstNodeKind::CallExpression);
    const AstNodeId targetId = childWithRole(statement, AstChildRole::Target);
    if (targetName.isEmpty()) {
        QVERIFY(targetId == InvalidAstNodeId);
    } else {
        QVERIFY(targetId != InvalidAstNodeId);
        const AstNode& target = result.ast->node(targetId);
        QCOMPARE(target.kind, targetName == QStringLiteral("ملف")
                                  && source.contains(QStringLiteral(", حالة"))
                                  ? AstNodeKind::TupleExpression : AstNodeKind::NameExpression);
        if (target.kind == AstNodeKind::NameExpression) {
            QCOMPARE(target.text, targetName);
        } else {
            QCOMPARE(result.ast->node(target.children.constFirst()).text, targetName);
        }
    }
    const AstNodeId bodyId = childWithRole(statement, AstChildRole::Body);
    QVERIFY(bodyId != InvalidAstNodeId);
    QCOMPARE(result.ast->node(bodyId).kind, AstNodeKind::Suite);
    QCOMPARE(result.syntaxTree->nodes().at(statement.syntaxNode).kind,
             SyntaxKind::WithStatement);
}

void TaifParserTest::fromImportAcceptsWildcard_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("path");
    QTest::newRow("exact-report") << QStringLiteral("من مكتبة استورد *")
                                  << QStringLiteral("مكتبة");
    QTest::newRow("newline") << QStringLiteral("من مكتبة استورد *\n")
                             << QStringLiteral("مكتبة");
    QTest::newRow("dotted-path") << QStringLiteral("من مكتبة.فرع استورد *\n")
                                 << QStringLiteral("مكتبة.فرع");
    QTest::newRow("relative-path") << QStringLiteral("من .مكتبة استورد *\n")
                                   << QStringLiteral(".مكتبة");
    QTest::newRow("following-statement") << QStringLiteral("من مكتبة استورد *\nس = 1\n")
                                         << QStringLiteral("مكتبة");
}

void TaifParserTest::fromImportAcceptsWildcard() {
    QFETCH(QString, source);
    QFETCH(QString, path);
    const ParseResult result = TaifParser().parse(source);

    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    QVERIFY(!hasAstKind(*result.ast, AstNodeKind::ErrorExpression));
    const AstNode& statement = result.ast->node(result.ast->root().children.constFirst());
    QCOMPARE(statement.kind, AstNodeKind::FromImportStatement);
    QCOMPARE(statement.children.size(), qsizetype(2));
    QCOMPARE(statement.childRoles.size(), statement.children.size());
    QCOMPARE(statement.childRoles.at(0), AstChildRole::ImportPath);
    QCOMPARE(statement.childRoles.at(1), AstChildRole::ImportName);
    QCOMPARE(result.ast->node(statement.children.at(0)).text, path);
    const AstNode& wildcard = result.ast->node(statement.children.at(1));
    QCOMPARE(wildcard.kind, AstNodeKind::ImportWildcard);
    QCOMPARE(wildcard.text, QStringLiteral("*"));
    QCOMPARE(wildcard.range.begin.offset, source.indexOf(QChar(u'*')));
    QCOMPARE(wildcard.range.end.offset, wildcard.range.begin.offset + 1);
    const SyntaxNode& syntax = result.syntaxTree->nodes().at(wildcard.syntaxNode);
    QCOMPARE(syntax.kind, SyntaxKind::ImportWildcard);
    QCOMPARE(result.syntaxTree->tokens().at(syntax.firstToken).kind, TokenKind::Star);
    QCOMPARE(syntax.endToken, syntax.firstToken + 1);
    if (source.contains(QStringLiteral("س = 1"))) {
        QCOMPARE(result.ast->root().children.size(), qsizetype(2));
        QVERIFY(hasAstKind(*result.ast, AstNodeKind::AssignmentStatement));
    }
}

void TaifParserTest::fromImportPreservesNamedImports() {
    const ParseResult result = TaifParser().parse(QStringLiteral("من مكتبة استورد س, ص\n"));

    QVERIFY(result.parserDiagnostics.isEmpty());
    const AstNode& statement = result.ast->node(result.ast->root().children.constFirst());
    QCOMPARE(statement.kind, AstNodeKind::FromImportStatement);
    QCOMPARE(statement.children.size(), qsizetype(3));
    QCOMPARE(statement.childRoles.size(), statement.children.size());
    QCOMPARE(statement.childRoles.at(0), AstChildRole::ImportPath);
    for (qsizetype index = 1; index < statement.children.size(); ++index) {
        QCOMPARE(statement.childRoles.at(index), AstChildRole::ImportName);
        QCOMPARE(result.ast->node(statement.children.at(index)).kind, AstNodeKind::NameExpression);
    }
    QCOMPARE(result.ast->node(statement.children.at(1)).text, QStringLiteral("س"));
    QCOMPARE(result.ast->node(statement.children.at(2)).text, QStringLiteral("ص"));
}

void TaifParserTest::importAliasesPreserveBindings() {
    const ParseResult result = TaifParser().parse(
        QStringLiteral("استورد الرياضيات ك ريض\n"
                       "من الرياضيات استورد جيب ك _جب, تجيب ك _تجب\n"));
    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    QCOMPARE(result.ast->root().children.size(), qsizetype(2));

    const AstNode& moduleImport = result.ast->node(result.ast->root().children.at(0));
    QCOMPARE(moduleImport.kind, AstNodeKind::ImportStatement);
    QCOMPARE(moduleImport.children.size(), qsizetype(2));
    QCOMPARE(moduleImport.childRoles.at(0), AstChildRole::ImportPath);
    QCOMPARE(moduleImport.childRoles.at(1), AstChildRole::ImportAlias);
    QCOMPARE(result.ast->node(moduleImport.children.at(1)).text, QStringLiteral("ريض"));

    const AstNode& memberImport = result.ast->node(result.ast->root().children.at(1));
    QCOMPARE(memberImport.kind, AstNodeKind::FromImportStatement);
    QCOMPARE(memberImport.children.size(), qsizetype(5));
    QCOMPARE(memberImport.childRoles.at(0), AstChildRole::ImportPath);
    QCOMPARE(memberImport.childRoles.at(1), AstChildRole::ImportName);
    QCOMPARE(memberImport.childRoles.at(2), AstChildRole::ImportAlias);
    QCOMPARE(memberImport.childRoles.at(3), AstChildRole::ImportName);
    QCOMPARE(memberImport.childRoles.at(4), AstChildRole::ImportAlias);
    QCOMPARE(result.ast->node(memberImport.children.at(2)).text, QStringLiteral("_جب"));
    QCOMPARE(result.ast->node(memberImport.children.at(4)).text, QStringLiteral("_تجب"));
}

void TaifParserTest::tupleComprehensionPreservesElementsAndTargets_data() {
    QTest::addColumn<QString>("source");
    QTest::newRow("bare") << QStringLiteral("متغير,متغير2 لكل متغير, متغير2 في تعبير\n");
    QTest::newRow("list") << QStringLiteral("[متغير,متغير2 لكل متغير, متغير2 في تعبير]\n");
    QTest::newRow("parenthesized") << QStringLiteral("(متغير,متغير2 لكل متغير, متغير2 في تعبير)\n");
    QTest::newRow("binary-elements") << QStringLiteral("متغير + 1,متغير2 * 2 لكل متغير, متغير2 في تعبير\n");
    QTest::newRow("unary-elements") << QStringLiteral("-متغير,+متغير2 لكل متغير, متغير2 في تعبير\n");
    QTest::newRow("three-elements") << QStringLiteral("متغير,متغير2,متغير3 لكل متغير, متغير2, متغير3 في تعبير\n");
}

void TaifParserTest::tupleComprehensionPreservesElementsAndTargets() {
    QFETCH(QString, source);
    const ParseResult result = TaifParser().parse(source);
    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    const AstNode& statement = result.ast->node(result.ast->root().children.constFirst());
    const AstNode* comprehension = &result.ast->node(statement.children.constFirst());
    if (comprehension->kind == AstNodeKind::ListExpression) {
        QCOMPARE(comprehension->children.size(), qsizetype(1));
        comprehension = &result.ast->node(comprehension->children.constFirst());
    }
    QCOMPARE(comprehension->kind, AstNodeKind::ComprehensionExpression);
    QCOMPARE(comprehension->children.size(), qsizetype(3));
    QCOMPARE(comprehension->childRoles.at(0), AstChildRole::Element);
    QCOMPARE(comprehension->childRoles.at(1), AstChildRole::Target);
    QCOMPARE(comprehension->childRoles.at(2), AstChildRole::Iterable);
    const AstNode& element = result.ast->node(comprehension->children.at(0));
    const AstNode& target = result.ast->node(comprehension->children.at(1));
    const qsizetype count = source.contains(QStringLiteral("متغير3")) ? 3 : 2;
    QCOMPARE(element.kind, AstNodeKind::TupleExpression);
    QCOMPARE(element.children.size(), count);
    QCOMPARE(target.kind, AstNodeKind::TupleExpression);
    QCOMPARE(target.children.size(), count);
    for (qsizetype index = 0; index < count; ++index) {
        const QString name = index == 0 ? QStringLiteral("متغير")
                                       : QStringLiteral("متغير") + QString::number(index + 1);
        QCOMPARE(result.ast->node(target.children.at(index)).kind, AstNodeKind::NameExpression);
        QCOMPARE(result.ast->node(target.children.at(index)).text, name);
    }
    QCOMPARE(result.ast->node(comprehension->children.at(2)).text, QStringLiteral("تعبير"));
}

void TaifParserTest::ordinaryTupleKeepsFlatElementsAndBinaryPrecedence() {
    const ParseResult result = TaifParser().parse(QStringLiteral("س + 1, ص * 2, ع\n"));
    QVERIFY(result.parserDiagnostics.isEmpty());
    const AstNode& statement = result.ast->node(result.ast->root().children.constFirst());
    const AstNode& tuple = result.ast->node(statement.children.constFirst());
    QCOMPARE(tuple.kind, AstNodeKind::TupleExpression);
    QCOMPARE(tuple.children.size(), qsizetype(3));
    QCOMPARE(result.ast->node(tuple.children.at(0)).kind, AstNodeKind::BinaryExpression);
    QCOMPARE(result.ast->node(tuple.children.at(0)).text, QStringLiteral("+"));
    QCOMPARE(result.ast->node(tuple.children.at(1)).kind, AstNodeKind::BinaryExpression);
    QCOMPARE(result.ast->node(tuple.children.at(1)).text, QStringLiteral("*"));
    QCOMPARE(result.ast->node(tuple.children.at(2)).text, QStringLiteral("ع"));
}

void TaifParserTest::comprehensionFiltersParseWithoutElse_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<int>("filterCount");
    QTest::newRow("exact-report") << QStringLiteral("[س لكل س, خلية في تعداد(هذا.اللوح) اذا خلية == 0]") << 1;
    QTest::newRow("multiple-filters") << QStringLiteral("[س لكل س, خلية في تعداد(هذا.اللوح) اذا خلية == 0 اذا س > 1]\n") << 2;
    QTest::newRow("boolean-filter") << QStringLiteral("[س لكل س, خلية في تعداد(هذا.اللوح) اذا خلية == 0 و س > 1]\n") << 1;
    QTest::newRow("tuple-element") << QStringLiteral("س,خلية لكل س, خلية في تعداد(هذا.اللوح) اذا خلية == 0\n") << 1;
    QTest::newRow("conditional-iterable") << QStringLiteral("[س لكل س, خلية في (تعداد(هذا.اللوح) اذا صح والا []) اذا خلية == 0]\n") << 1;
    QTest::newRow("conditional-filter") << QStringLiteral("[س لكل س, خلية في تعداد(هذا.اللوح) اذا (خلية == 0 اذا صح والا خطأ)]\n") << 1;
    QTest::newRow("following-statement") << QStringLiteral("[س لكل س, خلية في تعداد(هذا.اللوح) اذا خلية == 0]\nبعد = 1\n") << 1;
}

void TaifParserTest::comprehensionFiltersParseWithoutElse() {
    QFETCH(QString, source);
    QFETCH(int, filterCount);
    const ParseResult result = TaifParser().parse(source);
    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    QVERIFY(!hasAstKind(*result.ast, AstNodeKind::ErrorExpression));
    const AstNode& statement = result.ast->node(result.ast->root().children.constFirst());
    const AstNode* comprehension = &result.ast->node(statement.children.constFirst());
    if (comprehension->kind == AstNodeKind::ListExpression) {
        comprehension = &result.ast->node(comprehension->children.constFirst());
    }
    QCOMPARE(comprehension->kind, AstNodeKind::ComprehensionExpression);
    QCOMPARE(comprehension->children.size(), qsizetype(3 + filterCount));
    QCOMPARE(comprehension->childRoles.size(), comprehension->children.size());
    QCOMPARE(comprehension->childRoles.at(0), AstChildRole::Element);
    QCOMPARE(comprehension->childRoles.at(1), AstChildRole::Target);
    QCOMPARE(comprehension->childRoles.at(2), AstChildRole::Iterable);
    for (qsizetype index = 3; index < comprehension->children.size(); ++index) {
        QCOMPARE(comprehension->childRoles.at(index), AstChildRole::Condition);
        const AstNode& condition = result.ast->node(comprehension->children.at(index));
        QCOMPARE(condition.kind, AstNodeKind::BinaryExpression);
        QVERIFY(condition.range.begin.offset >= source.indexOf(QStringLiteral("اذا")));
    }
    if (source.contains(QStringLiteral("بعد = 1"))) {
        QCOMPARE(result.ast->root().children.size(), qsizetype(2));
        QVERIFY(hasAstKind(*result.ast, AstNodeKind::AssignmentStatement));
    }
}

void TaifParserTest::inlineConditionalsStillRequireElse() {
    const ParseResult valid = TaifParser().parse(QStringLiteral("س = 1 + 2 اذا صح والا 0\n"));
    QVERIFY(valid.parserDiagnostics.isEmpty());
    const AstNode& assignment = valid.ast->node(valid.ast->root().children.constFirst());
    const AstNode& conditional = valid.ast->node(assignment.children.constLast());
    QCOMPARE(conditional.text, QStringLiteral("اذا/والا"));
    QCOMPARE(valid.ast->node(conditional.children.constFirst()).text, QStringLiteral("+"));
    const ParseResult invalid = TaifParser().parse(QStringLiteral("س = 1 اذا صح\n"));
    QVERIFY(hasDiagnostic(invalid, QStringLiteral("عقد001")));
}

void TaifParserTest::chainedComprehensionsPreserveClauseOrder() {
    const ParseResult result = TaifParser().parse(QStringLiteral(
        "[قيمة لكل خلية في هذا.اللوح اذا خلية != 0 لكل قيمة في ([1, 0, 0] اذا خلية == 0 والا [0, 1, 0] اذا خلية == منظور_اللاعب والا [0, 0, 1]) اذا قيمة > 0]\n"));
    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    const AstNode& statement = result.ast->node(result.ast->root().children.constFirst());
    const AstNode& list = result.ast->node(statement.children.constFirst());
    const AstNode& outer = result.ast->node(list.children.constFirst());
    QCOMPARE(outer.kind, AstNodeKind::ComprehensionExpression);
    QCOMPARE(result.ast->node(outer.children.at(1)).text, QStringLiteral("خلية"));
    QCOMPARE(result.ast->node(outer.children.at(2)).kind, AstNodeKind::MemberExpression);
    QCOMPARE(outer.childRoles.at(3), AstChildRole::Condition);
    QCOMPARE(result.ast->node(outer.children.at(3)).text, QStringLiteral("!="));
    const AstNode& inner = result.ast->node(outer.children.at(0));
    QCOMPARE(inner.kind, AstNodeKind::ComprehensionExpression);
    QCOMPARE(result.ast->node(inner.children.at(1)).text, QStringLiteral("قيمة"));
    QCOMPARE(result.ast->node(inner.children.at(0)).kind, AstNodeKind::NameExpression);
    QCOMPARE(result.ast->node(inner.children.at(0)).text, QStringLiteral("قيمة"));
    const AstNode& iterable = result.ast->node(inner.children.at(2));
    QCOMPARE(iterable.text, QStringLiteral("اذا/والا"));
    QCOMPARE(result.ast->node(iterable.children.at(2)).text, QStringLiteral("اذا/والا"));
    QCOMPARE(inner.childRoles.at(3), AstChildRole::Condition);
    QCOMPARE(result.ast->node(inner.children.at(3)).text, QStringLiteral(">"));
}

void TaifParserTest::expressionsUsePrattPostfixAndPrecedenceParsing() {
    const QString source = QStringLiteral(
        "س = -2 + 3 * 4 ^ 2\n"
        "اطبع(س.قيمة[1:3], اسم = \"نص\", *ض, **ص)\n");
    const ParseResult result = TaifParser().parse(source);

    QVERIFY(result.parserDiagnostics.isEmpty());
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::UnaryExpression));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::BinaryExpression));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::CallExpression));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::MemberExpression));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::SliceExpression));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::KeywordArgument));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::StarArgument));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::DoubleStarArgument));
}

void TaifParserTest::identityComparisonsParseInsideCalls_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("operatorText");
    QTest::newRow("reported-he") << QStringLiteral("اطبع(مصفوفة1 هي مصفوفة2)\n")
                                  << QStringLiteral("هي");
    QTest::newRow("synonym-huwa") << QStringLiteral("اطبع(مصفوفة1 هو مصفوفة2)\n")
                                   << QStringLiteral("هو");
    QTest::newRow("comparison-precedence") << QStringLiteral(
        "اطبع(مصفوفة1 + قيمة هي مصفوفة2)\n") << QStringLiteral("هي");
}

void TaifParserTest::identityComparisonsParseInsideCalls() {
    QFETCH(QString, source);
    QFETCH(QString, operatorText);
    const ParseResult result = TaifParser().parse(source);
    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    bool foundIdentityComparison = false;
    for (const AstNode& node : result.ast->nodes()) {
        if (node.kind != AstNodeKind::BinaryExpression || node.text != operatorText) {
            continue;
        }
        QCOMPARE(node.children.size(), qsizetype(2));
        QCOMPARE(result.ast->node(node.children.at(0)).kind,
                 operatorText == QStringLiteral("هي") && source.contains(QStringLiteral(" + "))
                     ? AstNodeKind::BinaryExpression : AstNodeKind::NameExpression);
        QCOMPARE(result.ast->node(node.children.at(1)).text, QStringLiteral("مصفوفة2"));
        foundIdentityComparison = true;
    }
    QVERIFY(foundIdentityComparison);
}

void TaifParserTest::radixLiteralsAndBitwiseExpressions_data() {
    QTest::addColumn<QString>("source");
    QTest::addRow("standard-radices") << QStringLiteral(
        "ثنائي = 0b00000011\nثماني = 0o00000071\nست_عشري = 0x0000001c\n");
    QTest::addRow("arabic-radices") << QStringLiteral(
        "ثنائي = 0ث00000011\nثماني = 0م00000071\nست_عشري = 0س0000001ت\n");
    QTest::addRow("binary-and-xor") << QStringLiteral(
        "عدد = 5 & 1\nعدد = 5 | 1\nعدد = 5 *| 1\nعدد = ~5\nعدد = 5 << 1\nعدد = 5 >> 1\n");
    QTest::addRow("compound-assignment") << QStringLiteral(
        "عدد &= 2\nعدد |= 4\nعدد *|= 5\nعدد <<= 1\nعدد >>= 1\n");
}

void TaifParserTest::radixLiteralsAndBitwiseExpressions() {
    QFETCH(QString, source);
    const ParseResult result = TaifParser().parse(source);
    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    QCOMPARE(result.ast->root().children.size(),
             source.count(QChar(u'\n')));
    for (const AstNodeId id : result.ast->root().children) {
        const AstNode& statement = result.ast->node(id);
        QVERIFY(statement.kind == AstNodeKind::AssignmentStatement);
    }
    if (source.contains(QStringLiteral("0ث"))) {
        const AstNode& binary = result.ast->node(result.ast->root().children.at(0));
        QCOMPARE(result.ast->node(binary.children.constLast()).text, QStringLiteral("0ث00000011"));
    }
    if (source.contains(QStringLiteral("عدد = 5 & 1"))) {
        const QVector<qsizetype> binaryStatementIndices {0, 1, 2, 4, 5};
        const QStringList binaryOperators {QStringLiteral("&"), QStringLiteral("|"),
                                           QStringLiteral("*|"), QStringLiteral("<<"),
                                           QStringLiteral(">>")};
        for (qsizetype index = 0; index < binaryOperators.size(); ++index) {
            const AstNode& assignment = result.ast->node(
                result.ast->root().children.at(binaryStatementIndices.at(index)));
            QCOMPARE(result.ast->node(assignment.children.constLast()).text,
                     binaryOperators.at(index));
        }
        const AstNode& complementAssignment = result.ast->node(result.ast->root().children.at(3));
        const AstNode& complement = result.ast->node(complementAssignment.children.constLast());
        QCOMPARE(complement.kind, AstNodeKind::UnaryExpression);
        QCOMPARE(complement.text, QStringLiteral("~"));
    }
    if (source.contains(QStringLiteral("عدد &= 2"))) {
        const QStringList operators {QStringLiteral("&="), QStringLiteral("|="),
                                     QStringLiteral("*|="), QStringLiteral("<<="),
                                     QStringLiteral(">>=")};
        for (qsizetype index = 0; index < operators.size(); ++index) {
            QCOMPARE(result.ast->node(result.ast->root().children.at(index)).text,
                     operators.at(index));
        }
    }
}

void TaifParserTest::bitwiseOperatorsRespectPrecedence() {
    const ParseResult result = TaifParser().parse(
        QStringLiteral("نتيجة = 1 | 2 *| 3 & 4 << 1 + 2\n"));
    QVERIFY(result.parserDiagnostics.isEmpty());
    const AstNode& assignment = result.ast->node(result.ast->root().children.constFirst());
    const AstNode& bitwiseOr = result.ast->node(assignment.children.constLast());
    QCOMPARE(bitwiseOr.text, QStringLiteral("|"));
    const AstNode& bitwiseXor = result.ast->node(bitwiseOr.children.at(1));
    QCOMPARE(bitwiseXor.text, QStringLiteral("*|"));
    const AstNode& bitwiseAnd = result.ast->node(bitwiseXor.children.at(1));
    QCOMPARE(bitwiseAnd.text, QStringLiteral("&"));
    const AstNode& shift = result.ast->node(bitwiseAnd.children.at(1));
    QCOMPARE(shift.text, QStringLiteral("<<"));
    const AstNode& addition = result.ast->node(shift.children.at(1));
    QCOMPARE(addition.text, QStringLiteral("+"));
}

void TaifParserTest::formattedStringCreatesStructuredAst() {
    const ParseResult result = TaifParser().parse(QStringLiteral(
        "س = م\"القيمة {س + 1:.2ف}\"\n"));

    QVERIFY(result.parserDiagnostics.isEmpty());
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::FormattedStringExpression));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::FormattedStringText));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::FormattedStringInterpolation));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::FormattedStringFormat));
}

void TaifParserTest::prefixedStringsAreSingleExpressions_data() {
    QTest::addColumn<QString>("literal");
    for (const QString& prefix : {QStringLiteral("م"), QStringLiteral("خ"), QStringLiteral("ث")}) {
        for (const QString& delimiter : {QStringLiteral("\""), QStringLiteral("'"), QStringLiteral("\"\"\""), QStringLiteral("'''")}) {
            for (const QString& text : {QString(), QStringLiteral("abc\\n{س}")}) {
                const QString literal = prefix + delimiter + text + delimiter;
                QTest::newRow(qPrintable(literal)) << literal;
            }
        }
    }
}

void TaifParserTest::prefixedStringsAreSingleExpressions() {
    QFETCH(QString, literal);
    const ParseResult result = TaifParser().parse(QStringLiteral("س = ") + literal + QChar(u'\n'));
    QVERIFY(result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.parserDiagnostics.isEmpty());
    QCOMPARE(result.ast->root().children.size(), qsizetype(1));
    const AstNode& statement = result.ast->node(result.ast->root().children.constFirst());
    QCOMPARE(statement.kind, AstNodeKind::AssignmentStatement);
    const AstNode& value = result.ast->node(statement.children.constLast());
    QCOMPARE(value.range.begin.offset, qsizetype(4));
    QCOMPARE(value.range.end.offset, qsizetype(4) + literal.size());
    const AstNodeKind expected = literal.startsWith(QChar(u'م'))
        ? AstNodeKind::FormattedStringExpression
        : literal.startsWith(QChar(u'خ')) ? AstNodeKind::RawStringLiteral
                                         : AstNodeKind::BinaryStringLiteral;
    QCOMPARE(value.kind, expected);
    if (expected != AstNodeKind::FormattedStringExpression) {
        QCOMPARE(value.text, literal);
    }
}

void TaifParserTest::recoveryPreservesLaterDeclarations() {
    const QString source = QStringLiteral(
        "اذا صح\n"
        "\tاطبع(1)\n"
        "دالة بعد_الخطأ():\n"
        "\tارجع 9\n");
    const ParseResult result = TaifParser().parse(source);

    QVERIFY(!result.parserDiagnostics.isEmpty());
    QVERIFY(hasDiagnostic(result, QStringLiteral("عقد001")));
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::FunctionDeclaration));
    QCOMPARE(result.syntaxTree->tokens().constLast().kind, TokenKind::EndOfFile);
}

void TaifParserTest::lexerDiagnosticsAreRetained() {
    const ParseResult result = TaifParser().parse(QStringLiteral("س = \"غير منته\n"));

    QVERIFY(!result.lexicalDiagnostics.isEmpty());
    QVERIFY(result.syntaxTree != nullptr);
    QVERIFY(result.ast != nullptr);
    QCOMPARE(result.syntaxTree->tokens().constLast().kind, TokenKind::EndOfFile);
}

void TaifParserTest::reparseFallsBackToAnEquivalentFreshSnapshot() {
    const TaifParser parser;
    const ParseResult before = parser.parse(QStringLiteral("س = 1\n"), 4);
    TextEdit edit;
    edit.replacedRange = {{4, 1, 5}, {5, 1, 6}};
    edit.insertedText = QStringLiteral("2");
    edit.baseRevision = 4;
    edit.resultRevision = 5;

    const IncrementalParseResult incremental = parser.reparse(
        before, QStringLiteral("س = 2\n"), edit);
    const ParseResult fresh = parser.parse(QStringLiteral("س = 2\n"), 5);

    QVERIFY(incremental.usedFullReparseFallback);
    QCOMPARE(incremental.reusedSyntaxNodeCount, qsizetype(0));
    QCOMPARE(incremental.result.documentRevision, fresh.documentRevision);
    QCOMPARE(incremental.result.ast->nodes().size(), fresh.ast->nodes().size());
    QCOMPARE(incremental.result.parserDiagnostics.size(), fresh.parserDiagnostics.size());
}

void TaifParserTest::passStatementIsParsedWithoutError() {
    const QString source = QStringLiteral(
        "دالة اختبار():\n"
        "\tاذا صح:\n"
        "\t\tمرر\n"
        "\tوالا:\n"
        "\t\tاطبع(1)\n");
    const ParseResult result = TaifParser().parse(source);

    QVERIFY(result.parserDiagnostics.isEmpty());
    QVERIFY(hasAstKind(*result.ast, AstNodeKind::PassStatement));
}

void TaifParserTest::statusCorpusProducesAFiniteTree() {
    QFile file(QFINDTESTDATA("../lexer/data/Status.alif"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QString corpus = QString::fromUtf8(file.readAll());

    const ParseResult result = TaifParser().parse(corpus, 7);
    QVERIFY(result.syntaxTree != nullptr);
    QVERIFY(result.ast != nullptr);
    QCOMPARE(result.documentRevision, quint64(7));
    QCOMPARE(result.syntaxTree->tokens().constLast().kind, TokenKind::EndOfFile);
    QVERIFY(result.parserDiagnostics.isEmpty());
    QVERIFY(result.ast->nodes().size() > 100);
}

QTEST_GUILESS_MAIN(TaifParserTest)
#include "AlifParserTest.moc"
