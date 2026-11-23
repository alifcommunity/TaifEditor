#pragma once

#include <QtCore/QString>
#include <QtCore/QtGlobal>

struct CompletionContext final {
    bool isMemberAccess = false;
    QString receiver;
    QString prefix;
    qsizetype receiverBegin = -1;
    qsizetype receiverEnd = -1;
    qsizetype replacementBegin = -1;
    qsizetype replacementEnd = -1;

    [[nodiscard]] bool hasReplacementRange() const {
        return replacementBegin >= 0 && replacementEnd >= replacementBegin;
    }
};

inline bool isCompletionIdentifierCharacter(const QChar character) {
    return character.isLetterOrNumber() || character == QChar(u'_');
}

/**
 * Finds a simple `receiver.memberPrefix` context on one QTextBlock.
 * All returned offsets are absolute UTF-16 document offsets.
 */
inline CompletionContext completionContextAt(const QString& line,
                                             const qsizetype cursorInBlock,
                                             const qsizetype blockBegin) {
    CompletionContext result;
    if (cursorInBlock < 0 || cursorInBlock > line.size()) {
        return result;
    }

    qsizetype memberStart = cursorInBlock;
    while (memberStart > 0 && isCompletionIdentifierCharacter(line.at(memberStart - 1))) {
        --memberStart;
    }
    if (memberStart == 0 || line.at(memberStart - 1) != QChar(u'.')) {
        return result;
    }

    const qsizetype receiverEnd = memberStart - 1;
    qsizetype receiverStart = receiverEnd;
    while (receiverStart > 0
           && isCompletionIdentifierCharacter(line.at(receiverStart - 1))) {
        --receiverStart;
    }
    if (receiverStart == receiverEnd) {
        return result;
    }

    result.isMemberAccess = true;
    result.receiver = line.mid(receiverStart, receiverEnd - receiverStart);
    result.prefix = line.mid(memberStart, cursorInBlock - memberStart);
    result.receiverBegin = blockBegin + receiverStart;
    result.receiverEnd = blockBegin + receiverEnd;
    result.replacementBegin = blockBegin + memberStart;
    result.replacementEnd = blockBegin + cursorInBlock;
    return result;
}
