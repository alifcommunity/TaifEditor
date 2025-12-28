#include "TSearchPanel.h"
#include <QStyle>

SearchPanel::SearchPanel(QWidget *parent) : QWidget(parent) {
    // 1. إنشاء التخطيط الأفقي
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // 2. إنشاء المكونات
    searchInput = new QLineEdit(this);
    searchInput->setPlaceholderText("بحث...");
    searchInput->setStyleSheet("border: 1px solid #3e3e42; background: #252526; color: #cccccc; padding: 4px;");

    btnNext = new QPushButton("التالي", this);
    btnPrev = new QPushButton("السابق", this);

    // أزرار صغيرة
    QString btnStyle = "QPushButton { background: transparent; border: none; color: #cccccc; padding: 4px; } QPushButton:hover { background: #334466; border-radius: 7px; }";
    btnNext->setStyleSheet(btnStyle);
    btnPrev->setStyleSheet(btnStyle);

    // زر الإغلاق (X)
    btnClose = new QPushButton(this);
    btnClose->setIcon(QIcon(":/icons/resources/close.svg"));
    btnClose->setStyleSheet("QPushButton { background: transparent; border: none; color: white; } QPushButton:hover { background: #334466; border-radius: 7px; }");
    btnClose->setFixedSize(35,35);

    checkCase = new QCheckBox("Aa", this); // Case Sensitive
    checkCase->setToolTip("مطبقة حالة الأحرف");
    checkCase->setStyleSheet("color: #cccccc;");

    // 3. إضافة المكونات للتخطيط
    layout->addWidget(btnClose);
    layout->addWidget(searchInput);
    layout->addWidget(btnNext);
    layout->addWidget(btnPrev);
    layout->addWidget(checkCase);
    layout->addStretch(); // فراغ في النهاية

    // 4. الخلفية واللون
    QString styleSheet = R"("background-color: #1e202e; border-top: 1px solid #3e3e42;")";
    setFixedHeight(45);

    this->setStyleSheet(styleSheet);

    // 5. ربط الإشارات
    connect(btnNext, &QPushButton::clicked, this, &SearchPanel::findNext);
    connect(btnPrev, &QPushButton::clicked, this, &SearchPanel::findPrevious);
    connect(btnClose, &QPushButton::clicked, this, &SearchPanel::closed);

    // البحث عند الضغط على Enter في مربع النص
    connect(searchInput, &QLineEdit::returnPressed, this, &SearchPanel::findNext);
    connect(searchInput, &QLineEdit::textChanged, this, &SearchPanel::findText);
}

QString SearchPanel::getText() const { return searchInput->text(); }
bool SearchPanel::isCaseSensitive() const { return checkCase->isChecked(); }
bool SearchPanel::isWholeWord() const { return false; }
void SearchPanel::setFocusToInput() { searchInput->setFocus(); searchInput->selectAll(); }
