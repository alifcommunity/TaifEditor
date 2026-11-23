#include "TSettings.h"
#include "TaifBootstrap.h"

#include <QMessageBox>
#include <QStyledItemDelegate>

TSettings::TSettings(QWidget* const parent)
    : QWidget(parent)
{
    qRegisterMetaType<EditorPreferences>("EditorPreferences");
    setWindowTitle(QStringLiteral("الإعدادات"));
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint);
    setMinimumSize(800, 600);
    setLayoutDirection(Qt::RightToLeft);

    setupLayout();
    setupStyling();
    beginEditing();
}

EditorPreferences TSettings::currentPreferences() const
{
    return draftPreferences;
}

void TSettings::beginEditing() {
    baselinePreferences = PreferencesStore::load();
    draftPreferences = baselinePreferences;
    clearRecentFilesOnApply = false;
    setControlsFromPreferences(draftPreferences);
    if (clearRecentFilesButton != nullptr) {
        clearRecentFilesButton->setEnabled(true);
        clearRecentFilesButton->setText(QStringLiteral("مسح سجل الملفات الأخيرة"));
    }
}

void TSettings::setupLayout() {
    QHBoxLayout* const mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Sidebar
    sidebar = new QListWidget(this);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(260);
    sidebar->setSpacing(5);
    sidebar->setFrameShape(QFrame::NoFrame);

    // Content Area
    stackedWidget = new QStackedWidget(this);
    stackedWidget->setObjectName(QStringLiteral("contentArea"));
    connect(sidebar, &QListWidget::currentRowChanged,
            stackedWidget, &QStackedWidget::setCurrentIndex);

    addSettingPage(QStringLiteral("المحرر"),
                   QStringLiteral("مظهر الشيفرة وسلوك المحرر"),
                   QStringLiteral(":/icons/resources/pencil-ruler.svg"));
    addSettingPage(QStringLiteral("متقدم"),
                   QStringLiteral("الإكمال والمعلومات والتنبيهات"),
                   QStringLiteral(":/icons/resources/settings-2.svg"));
    addSettingPage(QStringLiteral("الملفات"),
                   QStringLiteral("سجل الملفات الأخيرة وسلوك مساحة العمل"),
                   QStringLiteral(":/icons/resources/folder-cog.svg"));

    auto* const contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(stackedWidget, 1);
    createActionBar(contentLayout);

    mainLayout->addWidget(sidebar);
    mainLayout->addLayout(contentLayout, 1);
    sidebar->setCurrentRow(0);
}

void TSettings::addSettingPage(const QString& name, const QString& description,
                               const QString& iconPath) {
    auto* const item = new QListWidgetItem(sidebar);
    item->setText(name);
    item->setIcon(QIcon(iconPath));
    item->setSizeHint(QSize(0, 50));
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* const page = new QWidget(stackedWidget);
    auto* const pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(30, 30, 30, 30);
    pageLayout->setAlignment(Qt::AlignTop);
    pageLayout->setSpacing(18);

    auto* const descriptionLabel = new QLabel(description, page);
    descriptionLabel->setObjectName(QStringLiteral("descLabel"));
    descriptionLabel->setWordWrap(true);
    pageLayout->addWidget(descriptionLabel);

    if (name == QStringLiteral("المحرر")) {
        createAppearancePage(pageLayout);
        createEditorBehaviorPage(pageLayout);
    } else if (name == QStringLiteral("متقدم")) {
        createIntelligencePage(pageLayout);
    } else {
        createWorkspacePage(pageLayout);
    }

    pageLayout->addStretch(1);
    stackedWidget->addWidget(page);
}

void TSettings::setupStyling() {
    setStyleSheet(QStringLiteral(R"(
        /*  Theme Palette
            Background: #0f172a | Surface: #1e293b
            Accent: #3b82f6 | Text: #f1f5f9 | Muted: #94a3b8
        */

        /* Main Container */
        QWidget {
            background-color: #0f172a;
            color: #f1f5f9;
            font-family: "Tajawal", "Noto Kufi Arabic", "Segoe UI";
        }

        /* Sidebar Styling */
        QListWidget#sidebar {
            background-color: #1e293b;
            border-left: 3px double #334155;
            padding-top: 10px;
            outline: none;
            font-size: 18px;
        }

        /* The "Buttons" (List Items) */
        QListWidget#sidebar::item {
            color: #94a3b8;
            padding-right: 5px;
            border-right: 4px solid transparent;
            margin: 2px 0px;
        }

        QListWidget#sidebar::item:hover {
            background-color: #334155;
            color: #f1f5f9;
            border-radius: 8px;
        }

        /* The Active/Selected State */
        QListWidget#sidebar::item:selected {
            color: #3b82f6;
            font-weight: bold;
            border-right: 4px solid #3b82f6;
            border-radius: 0px;
        }

        /* Content Area Background */
        QStackedWidget#contentArea {
            background-color: #0f172a;
        }

        QLabel#descLabel {
            color: #64748b;
            font-size: 21px;
            margin-bottom: 15px;
        }

        /* Group Boxes */
        QGroupBox {
            background-color: #1e293b;
            border: 1px solid #334155;
            border-radius: 8px;
            margin-top: 25px;
            padding-top: 15px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 20px;
            color: #3b82f6;
        }

        /* --- Inputs --- */
        QGroupBox QLabel {
            background-color: #1e293b;
        }

        QLineEdit, QComboBox, QSpinBox{
            border: 1px solid #334155;
            border-radius: 6px;
            padding: 5px 10px;
            background-color: #1e293b;
            selection-background-color: #339af0;
            color: #f1f5f9;
            font-size: 13px;
        }

        /* The dropdown menu list */
        QComboBox QAbstractItemView {
            border: 1px solid #339af0;
            border-radius: 6px;
            background-color: #1e293b;
            outline: none;
            padding: 4px;
        }
        QComboBox QAbstractItemView::item {
            color: #f1f5f9;
            background-color: transparent;
            padding: 8px 10px;
            min-height: 24px;
            border-radius: 6px;
            margin-bottom: 2px;
        }
        QComboBox QAbstractItemView::item:hover,
        QComboBox QAbstractItemView::item:selected {
            background-color: #334155;
            color: #3b82f6;
        }

        /* Hover effect */
        QLineEdit:hover, QComboBox:hover, QSpinBox:hover {
            border: 1px solid #339af0;
        }

        /* Focus effect (Modern blue outline) */
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
            border: 1px solid #339af0;
        }

        /* --- QComboBox Specifics --- */
        QComboBox::drop-down {
            background-color: #1e293b;
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 45px;
            border-left-width: 0px; /* Removes the internal line */
            border-radius: 6px;
        }

        QComboBox::down-arrow {
            image: url(:/icons/resources/chevron-down.svg);
            border-radius: 6px;
            width: 18px;
            height: 18px;
        }

        /* QSpinBox Specifics */
        QSpinBox {
            min-width: 45px;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            width: 20px;
            border-radius: 3px;
            background-color: #1e293b;
        }

        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background-color: #1c7ed6;
        }

        QSpinBox::up-arrow {
            image: url(:/icons/resources/chevron-up.svg);
            width: 16px;
        }
        QSpinBox::down-arrow {
            image: url(:/icons/resources/chevron-down.svg);
            width: 16px;
        }

        QCheckBox {
            background-color: #1e293b;
            color: #e2e8f0;
            spacing: 9px;
            padding: 4px 1px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border: 1px solid #64748b;
            border-radius: 4px;
            background: #0f172a;
        }
        QCheckBox::indicator:checked {
            border-color: #3b82f6;
            background: #3b82f6;
            image: url(:/icons/resources/check.svg);
        }
        QWidget#settingsActionBar {
            background-color: #1e293b;
            border-top: 1px solid #334155;
        }
        QPushButton {
            background-color: #334155;
            color: #f1f5f9;
            border: none;
            border-radius: 6px;
            padding: 8px 18px;
            min-width: 92px;
        }
        QPushButton:hover { background-color: #475569; }
        QPushButton#SettingsApplyButton { background-color: #2563eb; }
        QPushButton#SettingsApplyButton:hover { background-color: #3b82f6; }
    )"));

}

void TSettings::closeEvent(QCloseEvent* const event) {
    draftPreferences = baselinePreferences;
    setControlsFromPreferences(baselinePreferences);
    applyDraftPreview();
    event->accept();
}

void TSettings::createAppearancePage(QVBoxLayout* const layout) {
    auto* const fontGroup = new QGroupBox(QStringLiteral("الخط والمظهر"));
    auto* const fontLayout = new QVBoxLayout(fontGroup);
    auto* const formLayout = new QFormLayout();

    fontSpin = new QSpinBox(fontGroup);
    fontSpin->setObjectName(QStringLiteral("SettingsFontSizeSpin"));
    fontSpin->setRange(12, 36);
    fontSpin->setMinimumHeight(40);
    fontSpin->setMinimumWidth(60);
    fontSpin->setMaximumWidth(100);
    formLayout->addRow(QStringLiteral("حجم الخط:"), fontSpin);

    fontCombo = new QComboBox(fontGroup);
    fontCombo->setObjectName(QStringLiteral("SettingsFontFamilyCombo"));
    fontCombo->setItemDelegate(new QStyledItemDelegate(fontCombo));
    fontCombo->setEditable(false);
    fontCombo->setMinimumHeight(40);
    fontCombo->setMinimumWidth(150);
    fontCombo->setMaximumWidth(250);
    fontCombo->addItems(TaifBootstrap::availableEditorFontFamilies());
    formLayout->addRow(QStringLiteral("نوع الخط:"), fontCombo);

    themeCombo = new QComboBox(fontGroup);
    themeCombo->setObjectName(QStringLiteral("SettingsSyntaxThemeCombo"));
    themeCombo->setItemDelegate(new QStyledItemDelegate(themeCombo));
    themeCombo->setEditable(false);
    themeCombo->setMinimumHeight(40);
    themeCombo->setMaximumWidth(250);
    setThemes();
    for (const auto& theme : availableThemes) {
        themeCombo->addItem(theme->name());
    }
    formLayout->addRow(QStringLiteral("مظهر الشيفرة:"), themeCombo);

    formLayout->setFormAlignment(Qt::AlignLeft);
    fontLayout->addLayout(formLayout);
    layout->addWidget(fontGroup);

    connect(fontSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TSettings::synchronizeDraftFromControls);
    connect(fontCombo, &QComboBox::currentTextChanged,
            this, &TSettings::synchronizeDraftFromControls);
    connect(themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TSettings::synchronizeDraftFromControls);
}

void TSettings::createEditorBehaviorPage(QVBoxLayout* const layout) {
    auto* const behaviorGroup = new QGroupBox(QStringLiteral("سلوك المحرر"));
    auto* const behaviorLayout = new QVBoxLayout(behaviorGroup);
    auto* const formLayout = new QFormLayout();

    tabWidthSpin = new QSpinBox(behaviorGroup);
    tabWidthSpin->setObjectName(QStringLiteral("SettingsTabWidthSpin"));
    tabWidthSpin->setRange(2, 8);
    tabWidthSpin->setSingleStep(2);
    tabWidthSpin->setMinimumHeight(36);
    formLayout->addRow(QStringLiteral("طول المسافة البادئة:"), tabWidthSpin);

    autoSaveSecondsSpin = new QSpinBox(behaviorGroup);
    autoSaveSecondsSpin->setObjectName(QStringLiteral("SettingsAutoSaveSecondsSpin"));
    autoSaveSecondsSpin->setRange(5, 300);
    autoSaveSecondsSpin->setSuffix(QStringLiteral(" ث"));
    autoSaveSecondsSpin->setMinimumHeight(36);
    formLayout->addRow(QStringLiteral("فترة الحفظ التلقائي:"), autoSaveSecondsSpin);

    // wordWrapCheck = new QCheckBox(QStringLiteral("التفاف الأسطر الطويلة"), behaviorGroup); // معلق حتى تصحيح مشكلة إلتفاف النص في المحررات من اليمين إلى اليسار
    lineNumbersCheck = new QCheckBox(QStringLiteral("إظهار أرقام الأسطر"), behaviorGroup);
    minimapCheck = new QCheckBox(QStringLiteral("إظهار الخريطة المصغرة"), behaviorGroup);
    highlightCurrentLineCheck = new QCheckBox(QStringLiteral("تمييز السطر الحالي"), behaviorGroup);
    autoSaveCheck = new QCheckBox(QStringLiteral("تفعيل الحفظ التلقائي والنسخة الاحتياطية"), behaviorGroup);

    behaviorLayout->addLayout(formLayout);
    // behaviorLayout->addWidget(wordWrapCheck);
    behaviorLayout->addWidget(lineNumbersCheck);
    behaviorLayout->addWidget(minimapCheck);
    behaviorLayout->addWidget(highlightCurrentLineCheck);
    behaviorLayout->addWidget(autoSaveCheck);
    layout->addWidget(behaviorGroup);

    connect(tabWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TSettings::synchronizeDraftFromControls);
    connect(autoSaveSecondsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TSettings::synchronizeDraftFromControls);
    for (QCheckBox* const checkBox : {/*wordWrapCheck,*/ lineNumbersCheck, minimapCheck,
                                      highlightCurrentLineCheck, autoSaveCheck}) {
        connect(checkBox, &QCheckBox::toggled, this, &TSettings::synchronizeDraftFromControls);
    }
}

void TSettings::createIntelligencePage(QVBoxLayout* const layout) {
    auto* const intelligenceGroup = new QGroupBox(QStringLiteral("إعدادات الشيفرة"));
    auto* const intelligenceLayout = new QVBoxLayout(intelligenceGroup);
    auto* const formLayout = new QFormLayout();

    hoverDelaySpin = new QSpinBox(intelligenceGroup);
    hoverDelaySpin->setObjectName(QStringLiteral("SettingsHoverDelaySpin"));
    hoverDelaySpin->setRange(100, 1500);
    hoverDelaySpin->setSingleStep(50);
    hoverDelaySpin->setSuffix(QStringLiteral(" مللي ثانية"));
    hoverDelaySpin->setMinimumHeight(36);
    formLayout->addRow(QStringLiteral("تأخير المعلومات عند المرور:"), hoverDelaySpin);

    automaticCompletionCheck = new QCheckBox(QStringLiteral("إظهار الإكمال التلقائي"), intelligenceGroup);
    hoverInformationCheck = new QCheckBox(QStringLiteral("إظهار معلومات الاسم عند المرور"), intelligenceGroup);
    inlineDiagnosticsCheck = new QCheckBox(QStringLiteral("إظهار تشخيص المشكلات داخل المحرر"), intelligenceGroup);

    intelligenceLayout->addLayout(formLayout);
    intelligenceLayout->addWidget(automaticCompletionCheck);
    intelligenceLayout->addWidget(hoverInformationCheck);
    intelligenceLayout->addWidget(inlineDiagnosticsCheck);
    layout->addWidget(intelligenceGroup);

    connect(hoverDelaySpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TSettings::synchronizeDraftFromControls);
    for (QCheckBox* const checkBox : {automaticCompletionCheck, hoverInformationCheck,
                                      inlineDiagnosticsCheck}) {
        connect(checkBox, &QCheckBox::toggled, this, &TSettings::synchronizeDraftFromControls);
    }
}

void TSettings::createWorkspacePage(QVBoxLayout* const layout) {
    auto* const workspaceGroup = new QGroupBox(QStringLiteral("الملفات الأخيرة"));
    auto* const workspaceLayout = new QVBoxLayout(workspaceGroup);
    auto* const formLayout = new QFormLayout();

    recentFilesLimitSpin = new QSpinBox(workspaceGroup);
    recentFilesLimitSpin->setObjectName(QStringLiteral("SettingsRecentFilesLimitSpin"));
    recentFilesLimitSpin->setRange(0, 30);
    recentFilesLimitSpin->setMinimumHeight(36);
    formLayout->addRow(QStringLiteral("الحد الأقصى للملفات الأخيرة:"), recentFilesLimitSpin);

    auto* const explanation = new QLabel(
        QStringLiteral("استخدم القيمة 0 لإيقاف حفظ سجل الملفات الأخيرة."), workspaceGroup);
    explanation->setObjectName(QStringLiteral("settingsHint"));
    explanation->setWordWrap(true);

    clearRecentFilesButton = new QPushButton(QStringLiteral("مسح سجل الملفات الأخيرة"), workspaceGroup);
    clearRecentFilesButton->setObjectName(QStringLiteral("SettingsClearRecentFilesButton"));
    workspaceLayout->addLayout(formLayout);
    workspaceLayout->addWidget(explanation);
    workspaceLayout->addWidget(clearRecentFilesButton, 0, Qt::AlignRight);
    layout->addWidget(workspaceGroup);

    connect(recentFilesLimitSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TSettings::synchronizeDraftFromControls);
    connect(clearRecentFilesButton, &QPushButton::clicked,
            this, &TSettings::markRecentFilesForClearing);
}

void TSettings::createActionBar(QVBoxLayout* const layout)
{
    auto* const actionBar = new QWidget(this);
    actionBar->setObjectName(QStringLiteral("settingsActionBar"));
    auto* const actionLayout = new QHBoxLayout(actionBar);
    actionLayout->setContentsMargins(30, 12, 30, 12);

    resetButton = new QPushButton(QStringLiteral("استعادة الافتراضيات"), actionBar);
    resetButton->setObjectName(QStringLiteral("SettingsResetButton"));
    cancelButton = new QPushButton(QStringLiteral("إلغاء"), actionBar);
    cancelButton->setObjectName(QStringLiteral("SettingsCancelButton"));
    applyButton = new QPushButton(QStringLiteral("تطبيق"), actionBar);
    applyButton->setObjectName(QStringLiteral("SettingsApplyButton"));

    actionLayout->addWidget(resetButton);
    actionLayout->addStretch(1);
    actionLayout->addWidget(cancelButton);
    actionLayout->addWidget(applyButton);
    layout->addWidget(actionBar);

    connect(applyButton, &QPushButton::clicked, this, &TSettings::applyDraft);
    connect(cancelButton, &QPushButton::clicked, this, &TSettings::cancelDraft);
    connect(resetButton, &QPushButton::clicked, this, &TSettings::restorePageDefaults);
}

void TSettings::setControlsFromPreferences(const EditorPreferences& requestedPreferences)
{
    const EditorPreferences preferences = PreferencesStore::normalize(requestedPreferences);
    synchronizingControls = true;

    fontSpin->setValue(preferences.fontSize);
    int fontIndex = fontCombo->findText(preferences.fontFamily);
    if (fontIndex < 0) {
        fontIndex = fontCombo->findText(TaifBootstrap::tajawalFontFamily());
    }
    fontCombo->setCurrentIndex(qMax(0, fontIndex));
    themeCombo->setCurrentIndex(qBound(0, preferences.syntaxThemeIndex,
                                       qMax(0, themeCombo->count() - 1)));
    tabWidthSpin->setValue(preferences.tabWidth);
    // wordWrapCheck->setChecked(preferences.wordWrapEnabled);
    lineNumbersCheck->setChecked(preferences.lineNumbersVisible);
    minimapCheck->setChecked(preferences.minimapVisible);
    highlightCurrentLineCheck->setChecked(preferences.highlightCurrentLine);
    autoSaveCheck->setChecked(preferences.autoSaveEnabled);
    autoSaveSecondsSpin->setValue(preferences.autoSaveIntervalMilliseconds / 1000);
    automaticCompletionCheck->setChecked(preferences.automaticCompletionEnabled);
    hoverInformationCheck->setChecked(preferences.hoverInformationEnabled);
    hoverDelaySpin->setValue(preferences.hoverDelayMilliseconds);
    inlineDiagnosticsCheck->setChecked(preferences.inlineDiagnosticsVisible);
    recentFilesLimitSpin->setValue(preferences.recentFilesLimit);
    synchronizingControls = false;
}

void TSettings::synchronizeDraftFromControls()
{
    if (synchronizingControls) {
        return;
    }

    draftPreferences.fontSize = fontSpin->value();
    draftPreferences.fontFamily = fontCombo->currentText();
    draftPreferences.syntaxThemeIndex = themeCombo->currentIndex();
    draftPreferences.tabWidth = tabWidthSpin->value();
    // draftPreferences.wordWrapEnabled = wordWrapCheck->isChecked();
    draftPreferences.lineNumbersVisible = lineNumbersCheck->isChecked();
    draftPreferences.minimapVisible = minimapCheck->isChecked();
    draftPreferences.highlightCurrentLine = highlightCurrentLineCheck->isChecked();
    draftPreferences.autoSaveEnabled = autoSaveCheck->isChecked();
    draftPreferences.autoSaveIntervalMilliseconds = autoSaveSecondsSpin->value() * 1000;
    draftPreferences.automaticCompletionEnabled = automaticCompletionCheck->isChecked();
    draftPreferences.hoverInformationEnabled = hoverInformationCheck->isChecked();
    draftPreferences.hoverDelayMilliseconds = hoverDelaySpin->value();
    draftPreferences.inlineDiagnosticsVisible = inlineDiagnosticsCheck->isChecked();
    draftPreferences.recentFilesLimit = recentFilesLimitSpin->value();
    draftPreferences = PreferencesStore::normalize(draftPreferences);
    applyDraftPreview();
}

void TSettings::applyDraftPreview() {
    emit preferencesPreviewed(draftPreferences);
}

void TSettings::applyDraft() {
    draftPreferences = PreferencesStore::normalize(draftPreferences);
    QString errorMessage;
    if (!PreferencesStore::save(draftPreferences, clearRecentFilesOnApply, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("تعذر حفظ الإعدادات"), errorMessage);
        return;
    }
    baselinePreferences = draftPreferences;
    clearRecentFilesOnApply = false;
    emit preferencesApplied(draftPreferences);
    hide();
}

void TSettings::cancelDraft() {
    draftPreferences = baselinePreferences;
    clearRecentFilesOnApply = false;
    setControlsFromPreferences(baselinePreferences);
    if (clearRecentFilesButton != nullptr) {
        clearRecentFilesButton->setEnabled(true);
        clearRecentFilesButton->setText(QStringLiteral("مسح سجل الملفات الأخيرة"));
    }
    applyDraftPreview();
    hide();
}

void TSettings::markRecentFilesForClearing() {
    clearRecentFilesOnApply = true;
    if (clearRecentFilesButton != nullptr) {
        clearRecentFilesButton->setEnabled(false);
        clearRecentFilesButton->setText(QStringLiteral("سيُمسح عند تطبيق الإعدادات"));
    }
}

void TSettings::restorePageDefaults() {
    draftPreferences = PreferencesStore::defaults();
    setControlsFromPreferences(draftPreferences);
    applyDraftPreview();
}


QComboBox *TSettings::getThemeCombo() const {
    return themeCombo;
}

void TSettings::setThemes() {
    availableThemes.clear();
    availableThemes.append(std::make_shared<VSCodeDarkTheme>());
    availableThemes.append(std::make_shared<MonokaiTheme>());
    availableThemes.append(std::make_shared<OceanicTheme>());
    availableThemes.append(std::make_shared<TaifGlowTheme>());
}

QVector<std::shared_ptr<SyntaxTheme>> TSettings::getAvailableThemes() const {
    return availableThemes;
}
