#pragma once

#include <QPointer>
#include <QWidget>

class QEvent;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QToolButton;
class QVBoxLayout;

class SearchPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit SearchPanel(QWidget *parent = nullptr);

    QString searchText() const;
    bool isCaseSensitive() const;
    bool isWholeWord() const;
    bool isRegex() const;

    QString replaceText() const;

    void setFocusToInput();
    void showReplaceRow(bool visible);

    void setMatchInfo(int current, int total);
    void setNoMatchesFound(bool noMatches);

    // Reparents the panel as a child of host and places it at host's top-left.
    // The panel remains non-modal and is clipped to the editor viewport.
    void showIn(QWidget *host);
    // Restores ownership to the original parent before the current editor is
    // destroyed or replaced.
    void detachFromHost();

signals:
    void findText();
    void findNext();
    void findPrevious();
    void closed();
    void replaceOne();
    void replaceAll();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void applyStyles();
    void updateSearchErrorState();
    void updateMatchLabelStyle();
    void updateFloatingGeometry();
    QToolButton *createToggleButton(const QString &text, const QString &toolTip);

    QLineEdit *searchInput = nullptr;
    QLabel *matchLabel = nullptr;
    QToolButton *btnPrev = nullptr;
    QToolButton *btnNext = nullptr;
    QToolButton *btnCase = nullptr;
    QToolButton *btnWord = nullptr;
    QToolButton *btnRegex = nullptr;
    QToolButton *btnToggleReplace = nullptr;
    QToolButton *btnClose = nullptr;

    QWidget *replaceRow = nullptr;
    QLineEdit *replaceInput = nullptr;
    QToolButton *btnReplace = nullptr;
    QToolButton *btnReplaceAll = nullptr;

    QVBoxLayout *mainLayout = nullptr;
    QHBoxLayout *searchRowLayout = nullptr;
    QHBoxLayout *replaceRowLayout = nullptr;
    QTimer *debounceTimer = nullptr;

    QPointer<QWidget> floatingHost;
    QPointer<QWidget> ownerWidget;
    bool replaceRowVisible = false;
    bool lastNoMatches = false;
};
