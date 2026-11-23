#include "AlifSemanticPresentationAdapter.h"

namespace {

bool isKeyword(const TokenKind kind) {
    return kind >= TokenKind::KwFunction && kind <= TokenKind::KwNull;
}

bool isString(const TokenKind kind) {
    switch (kind) {
    case TokenKind::StringLiteral:
    case TokenKind::RawStringLiteral:
    case TokenKind::BinaryStringLiteral:
    case TokenKind::FStringStart:
    case TokenKind::FStringText:
    case TokenKind::FStringFormat:
    case TokenKind::FStringEnd:
        return true;
    default:
        return false;
    }
}

bool isOperator(const TokenKind kind) {
    return (kind >= TokenKind::Plus && kind <= TokenKind::GreaterEqual)
        || (kind >= TokenKind::BitwiseAnd && kind <= TokenKind::RightShiftEqual);
}

bool isPunctuation(const TokenKind kind) {
    return kind >= TokenKind::LParen && kind <= TokenKind::Semicolon;
}

PresentationClass classForSymbol(const Symbol& symbol) {
    if (symbol.kind == SymbolKind::Parameter && symbol.name == QStringLiteral("هذا")
        && symbol.instanceClass != InvalidSymbolId) {
        return PresentationClass::SelfReceiver;
    }
    switch (symbol.kind) {
    case SymbolKind::Function: return PresentationClass::FunctionDeclaration;
    case SymbolKind::Class: return PresentationClass::ClassDeclaration;
    case SymbolKind::Parameter: return PresentationClass::Parameter;
    case SymbolKind::ImportModule:
    case SymbolKind::ImportMember: return PresentationClass::Import;
    case SymbolKind::Builtin: return PresentationClass::Builtin;
    case SymbolKind::Local:
    case SymbolKind::LoopVariable:
    case SymbolKind::ComprehensionVariable: return PresentationClass::Local;
    default: return PresentationClass::Local;
    }
}

PresentationClass classForDiagnostic(const SemanticDiagnostic& diagnostic) {
    if (diagnostic.code == QStringLiteral("يدل001")) {
        return PresentationClass::UnresolvedName;
    }
    return PresentationClass::Error;
}

} // namespace

QVector<PresentationSpan> SemanticPresentationAdapter::classify(
    const LexResult& lexicalResult, const ParseResult& parseResult,
    const std::shared_ptr<const SemanticModel>& semanticModel,
    const QVector<EditorDiagnostic>& diagnostics) const {
    QVector<PresentationSpan> spans;
    spans.reserve(lexicalResult.tokens.size()
                  + (semanticModel ? semanticModel->symbols().size()
                                    + semanticModel->references().size()
                                    + semanticModel->references().size() : 0)
                                    + diagnostics.size());

    for (const Token& token : lexicalResult.tokens) {
        PresentationClass classification = PresentationClass::Error;
        bool shouldEmit = true;
        if (token.kind == TokenKind::Comment) {
            classification = PresentationClass::Comment;
        } else if (token.kind == TokenKind::Decorator) {
            classification = PresentationClass::Decorator;
        } else if (isKeyword(token.kind)) {
            classification = PresentationClass::Keyword;
        } else if (isString(token.kind)) {
            classification = PresentationClass::String;
        } else if (token.kind == TokenKind::IntegerLiteral || token.kind == TokenKind::FloatLiteral) {
            classification = PresentationClass::Number;
        } else if (isOperator(token.kind)) {
            classification = PresentationClass::Operator;
        } else if (isPunctuation(token.kind)) {
            classification = PresentationClass::Punctuation;
        } else if (token.kind == TokenKind::Invalid) {
            classification = PresentationClass::Error;
        } else {
            shouldEmit = false;
        }
        if (shouldEmit && token.range.begin.offset < token.range.end.offset) {
            spans.append({token.range, classification, SemanticDiagnosticSeverity::Information});
        }
    }

    if (semanticModel) {
        for (const Symbol& symbol : semanticModel->symbols()) {
            if (symbol.kind == SymbolKind::Builtin || symbol.declarationNode == InvalidAstNodeId) {
                continue;
            }
            if (symbol.declarationRange.begin.offset >= symbol.declarationRange.end.offset) {
                continue;
            }
            spans.append({symbol.declarationRange, classForSymbol(symbol),
                          SemanticDiagnosticSeverity::Information});
        }

        for (const NameReference& reference : semanticModel->references()) {
            if (reference.range.begin.offset >= reference.range.end.offset) {
                continue;
            }
            if (reference.state == ResolutionState::Resolved && reference.resolvedSymbol != InvalidSymbolId) {
                const Symbol* symbol = semanticModel->symbol(reference.resolvedSymbol);
                if (symbol != nullptr) {
                    spans.append({reference.range, classForSymbol(*symbol),
                                  SemanticDiagnosticSeverity::Information});
                }
            }
        }
    }

    // Diagnostics are emitted after regular semantic spans so severity-specific
    // wave underlines are the final presentation overlay for their ranges.
    for (const EditorDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.range.begin.offset >= diagnostic.range.end.offset) {
            continue;
        }
        SemanticDiagnostic semanticDiagnostic;
        semanticDiagnostic.code = diagnostic.code;
        semanticDiagnostic.message = diagnostic.message;
        semanticDiagnostic.range = diagnostic.range;
        semanticDiagnostic.severity = diagnostic.severity;
        spans.append({diagnostic.range, classForDiagnostic(semanticDiagnostic),
                      diagnostic.severity});
    }

    Q_UNUSED(parseResult)
    return spans;
}
