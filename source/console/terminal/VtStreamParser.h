#pragma once

#include "TerminalScreenModel.h"

#include <QStringDecoder>

/** Incrementally applies a UTF-8 VT stream to TerminalScreenModel. */
class VtStreamParser final {
public:
    explicit VtStreamParser(TerminalScreenModel& screen);

    void feed(const QByteArray& bytes);
    void feedText(const QString& text);
    void reset();

    [[nodiscard]] int ignoredSequenceCount() const;

private:
    enum class State : quint8 { Ground, Escape, Csi, Osc, OscEscape };

    void consume(QChar character);
    void executeCsi(QChar final);
    void executeOsc();
    void applySgr(const QList<int>& parameters);
    [[nodiscard]] QList<int> parameters() const;
    [[nodiscard]] int parameterOrDefault(int index, int defaultValue) const;
    void ignoreSequence();

    TerminalScreenModel& m_screen;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
    State m_state = State::Ground;
    QString m_sequence;
    QString m_osc;
    int m_ignoredSequenceCount = 0;
};
