#include "TLexer.h"



// ==================== Helpers ====================

static QPair<int, QString> checkStringStart(const QString& text, int pos) {
    int idx = pos;
    while (idx < text.length() && idx < pos + 2 && text[idx].isLetter()) {
        idx++;
    }
    for (int end = idx; end >= pos; --end) {
        if (end >= text.length()) continue;
        QChar ch = text[end];
        if (ch == '"' || ch == '\'') {
            QString prefix = text.mid(pos, end - pos).toLower();
            bool valid = true;
            for(QChar c : prefix) if(!QString("م").contains(c)) valid = false;
            if(valid) return {end - pos, QString(ch)};
        }
    }
    return {-1, ""};
}

// ==================== Normal State ====================

TToken NormalState::readToken(const QString& text, int& pos, const LanguageDefinition& langDef) {
    if (pos >= text.length()) return TToken(TokenType::None, pos, 0);

    QChar ch = text[pos];

    // 1. Whitespace
    if (ch.isSpace()) {
        int start = pos;
        while (pos < text.length() && text[pos].isSpace()) pos++;
        return TToken(TokenType::Whitespace, start, pos - start);
    }

    // 2. Comments
    if (ch == '#') {
        int start = pos;
        pos = text.length();
        return TToken(TokenType::Comment, start, pos - start);
    }

    // 3. Strings & F-Strings
    auto strCheck = checkStringStart(text, pos);
    if (strCheck.first != -1) {
        int start = pos;
        int prefixLen = strCheck.first;
        QString quote = strCheck.second;
        QString prefix = text.mid(start, prefixLen).toLower();

        pos += prefixLen;
        bool isTriple = false;
        if (pos + 2 <= text.length() && text.mid(pos, 2) == quote.repeated(2)) {
            if (pos + 3 <= text.length() && text.mid(pos, 3) == quote.repeated(3)) {
                isTriple = true;
            }
        }

        QString delimiter = isTriple ? quote.repeated(3) : quote;
        int delimId;
        if (isTriple) delimId = (quote == "\"") ? StateMasks::TriDouble : StateMasks::TriSingle;
        else delimId = (quote == "\"") ? StateMasks::Double : StateMasks::Single;

        pos += delimiter.length();

        if (prefix.contains(u'م')) {
            pendingState = std::make_unique<FStringState>(delimiter, delimId);
            return TToken(TokenType::FString, start, pos - start);
        } else if (isTriple) {
            pendingState = std::make_unique<TripleStringState>(delimiter, delimId);
            return TToken(TokenType::MultiLineString, start, pos - start);
        } else {
            pendingState = std::make_unique<StringState>(delimiter, delimId);
            return TToken(TokenType::String, start, pos - start);
        }
    }

    // 4. Decorators
    if (ch == '@') {
        int start = pos++;
        while (pos < text.length() && (text[pos].isLetterOrNumber() || text[pos] == '_' || text[pos] == '.')) pos++;
        return TToken(TokenType::Decorator, start, pos - start);
    }

    // 5. Identifiers
    if (ch.isLetter() || ch == '_') {
        int start = pos;
        while (pos < text.length() && (text[pos].isLetterOrNumber() || text[pos] == '_')) pos++;
        QString word = text.mid(start, pos - start);

        if (word == "دالة") {
            pendingState = std::make_unique<FunctionDefState>();
            return TToken(TokenType::Keyword, start, pos - start, word);
        }
        if (word == "صنف") {
            pendingState = std::make_unique<ClassDefState>();
            return TToken(TokenType::Keyword, start, pos - start, word);
        }

        if (word == "هذا") return TToken(TokenType::Self, start, pos - start, word);
        if (langDef.keywordSet.contains(word)) return TToken(TokenType::Keyword, start, pos - start, word);
        if (langDef.magicSet.contains(word)) return TToken(TokenType::MagicMethod, start, pos - start, word);
        if (langDef.builtinSet.contains(word)) return TToken(TokenType::BuiltinFunc, start, pos - start, word);

        // Check for function call pattern 'func('
        int next = pos;
        while(next < text.length() && text[next].isSpace()) next++;
        if (next < text.length() && text[next] == '(') {
            return TToken(TokenType::Function, start, pos - start, word);
        }

        // Simple Heuristic for Class types (PascalCase)
        if (word[0].isUpper()) {
            return TToken(TokenType::ClassDef, start, pos - start, word);
        }

        return TToken(TokenType::Identifier, start, pos - start, word);
    }

    // 6. Numbers
    if (ch.isDigit()) {
        int start = pos;
        if (ch == '0' && pos + 1 < text.length() && text.mid(pos, 2).toLower() == "0x") {
            auto m = langDef.hexPattern.match(text, start, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
            if (m.hasMatch()) { pos += m.capturedLength(); return TToken(TokenType::Number, start, m.capturedLength()); }
        }
        auto m = langDef.numberPattern.match(text, start, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        if (m.hasMatch()) { pos += m.capturedLength(); return TToken(TokenType::Number, start, m.capturedLength()); }
        pos++; return TToken(TokenType::Number, start, 1);
    }

    pos++;
    return TToken(TokenType::Operator, pos - 1, 1, QString(ch));
}

std::unique_ptr<LexerState> NormalState::nextState() const {
    if (pendingState) return std::move(pendingState);
    return std::make_unique<NormalState>();
}

std::unique_ptr<LexerState> NormalState::clone() const {
    auto c = std::make_unique<NormalState>();
    if (pendingState) c->pendingState = pendingState->clone();
    return c;
}

// ==================== Function Definition State ====================
TToken FunctionDefState::readToken(const QString& text, int& pos, const LanguageDefinition& langDef) {
    if (pos >= text.length()) return TToken(TokenType::None, pos, 0);

    // Skip spaces
    if (text[pos].isSpace()) {
        int start = pos;
        while(pos < text.length() && text[pos].isSpace()) pos++;
        return TToken(TokenType::Whitespace, start, pos - start);
    }

    // Capture Name
    if (text[pos].isLetter() || text[pos] == '_') {
        int start = pos;
        while(pos < text.length() && (text[pos].isLetterOrNumber() || text[pos] == '_')) pos++;

        QString name = text.mid(start, pos - start);

        // Done with definition, go back to normal
        pendingState = std::make_unique<NormalState>();
        // Check if this is a magic method (e.g., __تهيئة__)
        if (langDef.magicSet.contains(name))
            return TToken(TokenType::MagicMethod, start, pos - start, name);
        // Otherwise, it's a regular function definition
        return TToken(TokenType::Function, start, pos - start, name);
    }

    // Abort if we hit something unexpected (like a parenthesis immediately?)
    // This handles `دالة (متغير):` syntax errors gracefully by just switching to normal
    pendingState = std::make_unique<NormalState>();
    pos++;
    return TToken(TokenType::Operator, pos-1, 1, QString(text[pos-1]));
}

std::unique_ptr<LexerState> FunctionDefState::nextState() const {
    if(pendingState) return std::move(pendingState);
    return std::make_unique<FunctionDefState>();
}

std::unique_ptr<LexerState> FunctionDefState::clone() const {
    auto c = std::make_unique<FunctionDefState>();
    if(pendingState) c->pendingState = pendingState->clone();
    return c;
}

// ==================== Class Definition State ====================

TToken ClassDefState::readToken(const QString& text, int& pos, const LanguageDefinition&) {
    if (pos >= text.length()) return TToken(TokenType::None, pos, 0);

    if (text[pos].isSpace()) {
        int start = pos;
        while(pos < text.length() && text[pos].isSpace()) pos++;
        return TToken(TokenType::Whitespace, start, pos - start);
    }

    if (text[pos].isLetter() || text[pos] == '_') {
        int start = pos;
        while(pos < text.length() && (text[pos].isLetterOrNumber() || text[pos] == '_')) pos++;
        pendingState = std::make_unique<NormalState>();
        return TToken(TokenType::ClassDef, start, pos - start, text.mid(start, pos - start));
    }

    pendingState = std::make_unique<NormalState>();
    pos++;
    return TToken(TokenType::Operator, pos-1, 1, QString(text[pos-1]));
}

std::unique_ptr<LexerState> ClassDefState::nextState() const {
    if(pendingState) return std::move(pendingState);
    return std::make_unique<ClassDefState>();
}

std::unique_ptr<LexerState> ClassDefState::clone() const {
    auto c = std::make_unique<ClassDefState>();
    if(pendingState) c->pendingState = pendingState->clone();
    return c;
}

// ==================== String States ====================

StringState::StringState(const QString& delim, int id) : delimiter(delim), delimId(id) {}

TToken StringState::readToken(const QString& text, int& pos, const LanguageDefinition&) {
    int start = pos;
    while (pos < text.length()) {
        if (text[pos] == '\\') { pos += 2; continue; }
        if (text.mid(pos).startsWith(delimiter)) {
            pos += delimiter.length();
            return TToken(TokenType::String, start, pos - start);
        }
        pos++;
    }
    return TToken(TokenType::String, start, pos - start);
}

std::unique_ptr<LexerState> StringState::nextState() const {
    return std::make_unique<NormalState>();
}

std::unique_ptr<LexerState> StringState::clone() const {
    return std::make_unique<StringState>(delimiter, delimId);
}

// ==================== Triple String State ====================

TripleStringState::TripleStringState(const QString& delim, int id) : delimiter(delim), delimId(id) {}

TToken TripleStringState::readToken(const QString& text, int& pos, const LanguageDefinition&) {
    int start = pos;

    // Search for the delimiter (e.g., """) starting from current position
    int idx = text.indexOf(delimiter, pos);

    if (idx != -1) {
        // Case 1: Found the closing delimiter on this line
        pos = idx + delimiter.length();

        // We found the end, so the next state must be Normal
        pendingState = std::make_unique<NormalState>();

        return TToken(TokenType::MultiLineString, start, pos - start);
    } else {
        // Case 2: Did not find delimiter, reached end of line
        pos = text.length();

        // Still inside the string, so next state remains TripleStringState
        pendingState = std::make_unique<TripleStringState>(delimiter, delimId);

        return TToken(TokenType::MultiLineString, start, pos - start);
    }
}

std::unique_ptr<LexerState> TripleStringState::nextState() const {
    // Return the state determined by readToken
    if (pendingState) return std::move(pendingState);

    // Fallback (shouldn't happen if readToken is called, but defaults to continue)
    return std::make_unique<TripleStringState>(delimiter, delimId);
}

std::unique_ptr<LexerState> TripleStringState::clone() const {
    auto c = std::make_unique<TripleStringState>(delimiter, delimId);
    if(pendingState) c->pendingState = pendingState->clone();
    return c;
}

// ==================== F-String State ====================

FStringState::FStringState(const QString& delim, int id) : delimiter(delim), delimId(id) {}

TToken FStringState::readToken(const QString& text, int& pos, const LanguageDefinition&) {
    int start = pos;
    while (pos < text.length()) {
        // 1. Check for End Delimiter
        if (text.mid(pos).startsWith(delimiter)) {
            // Check for escape backslashes
            int slashes = 0;
            for(int i = pos - 1; i >= start && text[i] == '\\'; --i) slashes++;

            if (slashes % 2 == 0) {
                // Found the end of the string
                pos += delimiter.length();

                // Explicitly transition to NormalState
                pendingState = std::make_unique<NormalState>();
                return TToken(TokenType::FString, start, pos - start);
            }
        }

        // 2. Check for Interpolation Start '{'
        if (text[pos] == '{') {
            // Check for double curly braces '{{' (escaped brace)
            if (pos + 1 < text.length() && text[pos+1] == '{') {
                pos += 2;
                continue;
            }

            // Check for backslash escape
            int slashes = 0;
            for(int i = pos - 1; i >= start && text[i] == '\\'; --i) slashes++;

            if (slashes % 2 == 0) {
                // If we have accumulated f-string text before this brace, return it first
                if (pos > start) {
                    // We stay in FStringState to handle the brace next time
                    return TToken(TokenType::FString, start, pos - start);
                }

                // Found the brace, switch to Interpolation
                pos++;
                pendingState = std::make_unique<InterpolationState>(delimiter, delimId);
                return TToken(TokenType::Operator, start, 1, "{");
            }
        }
        pos++;
    }

    // Hit end of line without closing string (for single-line f-strings, this is usually an error or just end of block)
    // But for safety in Lexer, we just return what we have.
    return TToken(TokenType::FString, start, pos - start);
}

std::unique_ptr<LexerState> FStringState::nextState() const {
    // If readToken set a pending state (Normal or Interpolation), use it.
    if(pendingState) return std::move(pendingState);

    // Otherwise, default to CONTINUING the F-String (do not go to Normal)
    return std::make_unique<FStringState>(delimiter, delimId);
}

std::unique_ptr<LexerState> FStringState::clone() const {
    auto c = std::make_unique<FStringState>(delimiter, delimId);
    if(pendingState) c->pendingState = pendingState->clone();
    return c;
}

// ==================== Interpolation State ====================

InterpolationState::InterpolationState(const QString& pDelim, int pId, int bal, std::unique_ptr<LexerState> inner)
    : parentDelimiter(pDelim), parentDelimId(pId), braceBalance(bal) {
    if (inner) innerState = std::move(inner);
    else innerState = std::make_unique<NormalState>();
}

TToken InterpolationState::readToken(const QString& text, int& pos, const LanguageDefinition& langDef) {
    if (pos >= text.length()) return TToken(TokenType::None, pos, 0);

    // Only interact with braces if the inner state is Normal
    if (dynamic_cast<NormalState*>(innerState.get())) {
        if (text[pos] == '}') {
            int start = pos++;
            braceBalance--;
            if (braceBalance <= 0) {
                // Done with interpolation, return to parent FString
                pendingState = std::make_unique<FStringState>(parentDelimiter, parentDelimId);
            }
            return TToken(TokenType::Operator, start, 1, "}");
        }

        if (text[pos] == '{') {
            int start = pos++;
            braceBalance++;
            return TToken(TokenType::Operator, start, 1, "{");
        }
    }

    // Delegate reading to the inner state (handles strings, functions, etc.)
    TToken t = innerState->readToken(text, pos, langDef);

    // Update inner state based on transition
    // We must check if innerState has a pending state or if we should just call nextState()
    // Since readToken might have set a pendingState on innerState:

    // We retrieve the next state from the inner state
    auto nextInner = innerState->nextState();

    // We must update our inner state to this new state
    innerState = std::move(nextInner);

    return t;
}

std::unique_ptr<LexerState> InterpolationState::nextState() const {
    if(pendingState) return std::move(pendingState);
    // Persist the current inner state to the next step
    return std::make_unique<InterpolationState>(parentDelimiter, parentDelimId, braceBalance, innerState->clone());
}

std::unique_ptr<LexerState> InterpolationState::clone() const {
    auto c = std::make_unique<InterpolationState>(parentDelimiter, parentDelimId, braceBalance, innerState->clone());
    if(pendingState) c->pendingState = pendingState->clone();
    return c;
}

// ==================== Lexer ====================

TLexer::TLexer() { finalState = StateMasks::Normal; }

QVector<TToken> TLexer::tokenize(const QString& text, int initialState) {
    QVector<TToken> tokens;
    int pos = 0;

    std::unique_ptr<LexerState> currentState;

    int type = initialState & StateMasks::TypeMask;
    int dType = initialState & StateMasks::DelimMask;

    QString delim = "\"";
    if (dType == StateMasks::Single) delim = "'";
    else if (dType == StateMasks::TriDouble) delim = "\"\"\"";
    else if (dType == StateMasks::TriSingle) delim = "'''";

    switch(type) {
    case StateMasks::String: currentState = std::make_unique<StringState>(delim, dType); break;
    case StateMasks::FString: currentState = std::make_unique<FStringState>(delim, dType); break;
    case StateMasks::TripleString: currentState = std::make_unique<TripleStringState>(delim, dType); break;
    case StateMasks::Interpolation: currentState = std::make_unique<InterpolationState>(delim, dType); break;
    case StateMasks::FunctionDef: currentState = std::make_unique<FunctionDefState>(); break;
    case StateMasks::ClassDef: currentState = std::make_unique<ClassDefState>(); break;
    default: currentState = std::make_unique<NormalState>(); break;
    }

    while (pos < text.length()) {
        TToken token = currentState->readToken(text, pos, langDef);

        if (token.length > 0) tokens.append(token);
        else if (pos < text.length()) pos++;

        currentState = currentState->nextState();
    }

    finalState = currentState->getStateId();
    return tokens;
}
