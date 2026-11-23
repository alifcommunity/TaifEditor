#include "TSearchPanel.h"
#include <QStyle>

#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr auto kBackground = "#1e202e";
constexpr auto kHoverBackground = "#2d2f3a";
constexpr auto kBorder = "#3e3e42";
constexpr auto kInputBackground = "#16171c";
constexpr auto kText = "#e2e8f0";
constexpr auto kDimText = "#8a8d97";
constexpr auto kAccent = "#3b82f6";
constexpr auto kAccentHover = "#2563eb";
constexpr auto kDanger = "#ef4444";
}

SearchPanel::SearchPanel(QWidget *parent)
    : QWidget(parent)
    , ownerWidget(parent)
{
    setObjectName(QStringLiteral("searchPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus);

    buildUi();
    applyStyles();

    debounceTimer = new QTimer(this);
    debounceTimer->setSingleShot(true);
    debounceTimer->setInterval(180);

    connect(debounceTimer, &QTimer::timeout, this, &SearchPanel::findText);
    connect(searchInput, &QLineEdit::textChanged, this, [this] {
        updateSearchErrorState();
        lastNoMatches = false;
        updateMatchLabelStyle();
        debounceTimer->start();
    });
    connect(searchInput, &QLineEdit::returnPressed, this, &SearchPanel::findNext);
    connect(replaceInput, &QLineEdit::returnPressed, this, &SearchPanel::replaceOne);
    connect(btnPrev, &QToolButton::clicked, this, &SearchPanel::findPrevious);
    connect(btnNext, &QToolButton::clicked, this, &SearchPanel::findNext);
    connect(btnCase, &QToolButton::toggled, this, [this] {
        emit findText();
    });
    connect(btnWord, &QToolButton::toggled, this, [this] {
        emit findText();
    });
    connect(btnRegex, &QToolButton::toggled, this, [this] {
        updateSearchErrorState();
        emit findText();
    });
    connect(btnClose, &QToolButton::clicked, this, &SearchPanel::closed);
    connect(btnToggleReplace, &QToolButton::clicked, this, [this](bool checked) {
        showReplaceRow(checked);
    });
    connect(btnReplace, &QToolButton::clicked, this, &SearchPanel::replaceOne);
    connect(btnReplaceAll, &QToolButton::clicked, this, &SearchPanel::replaceAll);

    searchInput->installEventFilter(this);
    replaceInput->installEventFilter(this);
}

void SearchPanel::buildUi()
{
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(7, 5, 7, 5);
    mainLayout->setSpacing(3);

    searchRowLayout = new QHBoxLayout;
    searchRowLayout->setContentsMargins(0, 0, 0, 0);
    searchRowLayout->setSpacing(3);

    auto *searchIcon = new QLabel(QStringLiteral("⌕"), this);
    searchIcon->setFixedWidth(16);
    searchIcon->setAlignment(Qt::AlignCenter);
    searchIcon->setObjectName(QStringLiteral("mutedIcon"));

    searchInput = new QLineEdit(this);
    searchInput->setPlaceholderText(QString::fromUtf8("بحث في الملف..."));
    searchInput->setClearButtonEnabled(true);
    searchInput->setMinimumWidth(63);
    searchInput->setFixedHeight(34);
    searchInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    matchLabel = new QLabel(this);
    matchLabel->setMinimumWidth(63);
    matchLabel->setAlignment(Qt::AlignCenter);
    matchLabel->setObjectName(QStringLiteral("matchLabel"));

    btnPrev = new QToolButton(this);
    btnPrev->setText(QStringLiteral("↑"));
    // btnPrev->setToolTip(QString::fromUtf8("السابق (Shift+F3)"));
    btnPrev->setFixedSize(25, 25);

    btnNext = new QToolButton(this);
    btnNext->setText(QStringLiteral("↓"));
    // btnNext->setToolTip(QString::fromUtf8("التالي (F3)"));
    btnNext->setFixedSize(25, 25);

    btnCase = createToggleButton(QStringLiteral("Aa"), QString::fromUtf8("حالة الأحرف (Aa)"));
    btnWord = createToggleButton(QString::fromUtf8("كلمة"), QString::fromUtf8("كلمة كاملة"));
    btnRegex = createToggleButton(QStringLiteral(".*"), QString::fromUtf8("تعبير نمطي (Regex)"));

    btnToggleReplace = new QToolButton(this);
    btnToggleReplace->setText(QStringLiteral("⤵"));
    btnToggleReplace->setToolTip(QString::fromUtf8("إظهار/إخفاء الاستبدال"));
    btnToggleReplace->setCheckable(true);
    btnToggleReplace->setFixedSize(25, 25);

    btnClose = new QToolButton(this);
    btnClose->setText(QStringLiteral("×"));
    btnClose->setObjectName("closeBotton");
    btnClose->setToolTip(QString::fromUtf8("إغلاق"));
    btnClose->setFixedSize(25, 25);

    searchRowLayout->addWidget(searchIcon);
    searchRowLayout->addWidget(searchInput, 1);
    searchRowLayout->addWidget(matchLabel);
    searchRowLayout->addWidget(btnPrev);
    searchRowLayout->addWidget(btnNext);
    searchRowLayout->addSpacing(3);
    searchRowLayout->addWidget(btnCase);
    searchRowLayout->addWidget(btnWord);
    searchRowLayout->addWidget(btnRegex);
    searchRowLayout->addWidget(btnToggleReplace);
    searchRowLayout->addStretch();
    searchRowLayout->addWidget(btnClose);
    mainLayout->addLayout(searchRowLayout);

    replaceRow = new QWidget(this);
    replaceRowLayout = new QHBoxLayout(replaceRow);
    replaceRowLayout->setContentsMargins(0, 0, 0, 0);
    replaceRowLayout->setSpacing(3);

    auto *replaceIcon = new QLabel(QStringLiteral("⇄"), replaceRow);
    replaceIcon->setFixedWidth(16);
    replaceIcon->setAlignment(Qt::AlignCenter);
    replaceIcon->setObjectName(QStringLiteral("mutedIcon"));

    replaceInput = new QLineEdit(replaceRow);
    replaceInput->setPlaceholderText(QString::fromUtf8("استبدال بـ..."));
    replaceInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    replaceInput->setFixedHeight(34);

    btnReplace = new QToolButton(replaceRow);
    btnReplace->setText(QString::fromUtf8("استبدال"));
    btnReplace->setToolTip(QString::fromUtf8("استبدال التطابق الحالي"));
    btnReplace->setFixedHeight(34);

    btnReplaceAll = new QToolButton(replaceRow);
    btnReplaceAll->setText(QString::fromUtf8("استبدال الكل"));
    btnReplaceAll->setToolTip(QString::fromUtf8("استبدال جميع التطابقات"));
    btnReplaceAll->setFixedHeight(34);

    replaceRowLayout->addWidget(replaceIcon);
    replaceRowLayout->addWidget(replaceInput, 1);
    replaceRowLayout->addWidget(btnReplace);
    replaceRowLayout->addWidget(btnReplaceAll);
    mainLayout->addWidget(replaceRow);
    replaceRow->setVisible(false);
}

QToolButton *SearchPanel::createToggleButton(const QString &text, const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setCheckable(true);
    button->setFixedHeight(25);
    button->setFixedWidth(text.size() > 2 ? 47 : 33);
    return button;
}

void SearchPanel::applyStyles()
{
    setStyleSheet(QStringLiteral(
                      "QWidget#searchPanel { background: %1; border: 1px solid %2; border-radius: 6px; }"
                      "QLabel#mutedIcon, QLabel#matchLabel { color: %3; background: transparent; border: none; }"
                      "QLabel#matchLabel { font-size: 12px; }"
                      "QLineEdit { background: %4; border: 1px solid %2; border-radius: 6px; color: %5; padding: 5px 10px; }"
                      "QLineEdit:focus { border-color: %6; }"
                      "QLineEdit[invalid=\"true\"] { border-color: %8; }"
                      "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 5px; color: %5; padding: 3px 7px; }"
                      "QToolButton:hover { background: %7; border-color: %2; }"
                      "QToolButton:checked { background: %6; color: white; }"
                      "QToolButton:disabled { color: %3; }"
                      "QToolButton#closeBotton::hover {background: %8;}"
                      ).arg(kBackground, kBorder, kDimText, kInputBackground, kText, kAccent, kHoverBackground, kDanger));

    const QString actionStyle = QStringLiteral(
                                    "QToolButton { background: %1; border: none; color: white; }"
                                    "QToolButton:hover { background: %2; }"
                                    ).arg(kAccent, kAccentHover);
    btnReplace->setStyleSheet(actionStyle);
    btnReplaceAll->setStyleSheet(actionStyle);
}

QString SearchPanel::searchText() const { return searchInput->text(); }
bool SearchPanel::isCaseSensitive() const { return btnCase->isChecked(); }
bool SearchPanel::isWholeWord() const { return btnWord->isChecked(); }
bool SearchPanel::isRegex() const { return btnRegex->isChecked(); }
QString SearchPanel::replaceText() const { return replaceInput->text(); }



void SearchPanel::setFocusToInput()
{
    searchInput->setFocus(Qt::OtherFocusReason);
    searchInput->selectAll();
}

void SearchPanel::showReplaceRow(bool visible)
{
    replaceRowVisible = visible;
    replaceRow->setVisible(visible);
    btnToggleReplace->setChecked(visible);
    btnToggleReplace->setToolTip(visible
                                     ? QString::fromUtf8("إخفاء الاستبدال")
                                     : QString::fromUtf8("إظهار الاستبدال"));
    if (visible)
        replaceInput->setFocus(Qt::OtherFocusReason);
    adjustSize();
    updateFloatingGeometry();
}



void SearchPanel::setMatchInfo(int current, int total)
{
    if (total <= 0) {
        matchLabel->setText(searchInput->text().isEmpty()
                            ? QString()
                            : QString::fromUtf8("لا يوجد تطابق"));
    } else {
        matchLabel->setText(QStringLiteral("%1 من %2").arg(current).arg(total));
    }
    setNoMatchesFound(total == 0);
}

void SearchPanel::setNoMatchesFound(bool noMatches)
{
    lastNoMatches = noMatches;
    if (noMatches && !searchInput->text().isEmpty())
        matchLabel->setText(QString::fromUtf8("لا يوجد تطابق"));
    updateMatchLabelStyle();
}

void SearchPanel::updateSearchErrorState()
{
    const bool invalid = btnRegex->isChecked() && !searchInput->text().isEmpty()
    && !QRegularExpression(searchInput->text()).isValid();
    searchInput->setProperty("invalid", invalid);
    searchInput->style()->unpolish(searchInput);
    searchInput->style()->polish(searchInput);
    searchInput->update();
}

void SearchPanel::updateMatchLabelStyle()
{
    matchLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; border: none; font-size: 12px; }")
                                  .arg(lastNoMatches && !searchInput->text().isEmpty() ? kDanger : kDimText));
}

void SearchPanel::showIn(QWidget *host)
{
    if (!host)
        return;

    if (floatingHost != host) {
        if (floatingHost)
            floatingHost->removeEventFilter(this);
        floatingHost = host;
        floatingHost->installEventFilter(this);
        setParent(host, Qt::Widget);
    }

    adjustSize();
    updateFloatingGeometry();
    show();
    raise();
}

void SearchPanel::detachFromHost()
{
    if (floatingHost) {
        floatingHost->removeEventFilter(this);
        floatingHost.clear();
    }

    if (parentWidget() != ownerWidget) {
        hide();
        setParent(ownerWidget, Qt::Widget);
    }
}

void SearchPanel::updateFloatingGeometry()
{
    if (!floatingHost)
        return;

    const int leftMargin = 118;
    const int margin = 12;
    const int availableWidth = qMax(0, floatingHost->width() - 2 * margin);
    const int panelWidth = qBound(320, availableWidth, 560);
    setFixedWidth(panelWidth);
    move(leftMargin + margin, margin);
}

bool SearchPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == floatingHost &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest)) {
        QTimer::singleShot(0, this, &SearchPanel::updateFloatingGeometry);
    }

    if ((watched == searchInput || watched == replaceInput) && event->type() == QEvent::KeyPress) {
        const auto *keyEvent = static_cast<const QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            emit closed();
            return true;
        }
        if (watched == searchInput && keyEvent->key() == Qt::Key_Return &&
            keyEvent->modifiers().testFlag(Qt::ShiftModifier)) {
            emit findPrevious();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}
