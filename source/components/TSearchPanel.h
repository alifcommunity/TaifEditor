#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QHBoxLayout>

class SearchPanel : public QWidget {
    Q_OBJECT

public:
    explicit SearchPanel(QWidget *parent = nullptr);
    QString getText() const;

    bool isCaseSensitive() const;
    bool isWholeWord() const;
    void setFocusToInput();

signals:
    void findText();
    void findNext();
    void findPrevious();
    void closed();

private:
    QLineEdit *searchInput;
    QPushButton *btnNext;
    QPushButton *btnPrev;
    QPushButton *btnClose;
    QCheckBox *checkCase;
    QCheckBox *checkWord;
};
