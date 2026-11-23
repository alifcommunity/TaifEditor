#pragma once

#include "RecoveryStore.h"

#include <QDialog>
#include <QVector>

class QTreeWidget;

class TRecoveryDialog final : public QDialog {
    Q_OBJECT
public:
    enum class Decision : quint8 {
        Defer,
        Restore,
        Discard
    };

    explicit TRecoveryDialog(QVector<RecoveryEntry> entries, QWidget* parent = nullptr);

    [[nodiscard]] Decision decision() const;
    [[nodiscard]] QVector<RecoveryEntry> selectedEntries() const;

private:
    [[nodiscard]] static QString sourceStateText(const RecoveryEntry& entry);
    void chooseRestore();
    void chooseDiscard();

    QVector<RecoveryEntry> m_entries;
    QTreeWidget* m_entryList{};
    Decision m_decision = Decision::Defer;
};
