#pragma once

#include "EditorPreferences.h"
#include "TSyntaxThemes.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

class TSettings : public QWidget {
    Q_OBJECT
public:
    explicit TSettings(QWidget* parent = nullptr);

    [[nodiscard]] QVector<std::shared_ptr<SyntaxTheme>> getAvailableThemes() const;
    [[nodiscard]] QComboBox* getThemeCombo() const;
    [[nodiscard]] EditorPreferences currentPreferences() const;
    void beginEditing();

signals:
    void preferencesPreviewed(EditorPreferences preferences);
    void preferencesApplied(EditorPreferences preferences);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void synchronizeDraftFromControls();
    void applyDraft();
    void cancelDraft();
    void restorePageDefaults();
    void markRecentFilesForClearing();


private:
    void setupLayout();
    void setupStyling();
    void addSettingPage(const QString& name, const QString& description, const QString& iconPath);
    void createAppearancePage(QVBoxLayout* layout);
    void createEditorBehaviorPage(QVBoxLayout* layout);
    void createIntelligencePage(QVBoxLayout* layout);
    void createWorkspacePage(QVBoxLayout* layout);
    void createActionBar(QVBoxLayout* layout);
    void setControlsFromPreferences(const EditorPreferences& preferences);
    void applyDraftPreview();
    void setThemes();

    QStackedWidget* stackedWidget{};
    QListWidget* sidebar{};

    QSpinBox* fontSpin{};
    QComboBox* fontCombo{};
    QComboBox* themeCombo{};
    QSpinBox* tabWidthSpin{};
    // QCheckBox* wordWrapCheck{}; // معلق حتى تصحيح مشكلة إلتفاف النص في المحررات من اليمين إلى اليسار
    QCheckBox* lineNumbersCheck{};
    QCheckBox* minimapCheck{};
    QCheckBox* highlightCurrentLineCheck{};
    QCheckBox* autoSaveCheck{};
    QSpinBox* autoSaveSecondsSpin{};
    QCheckBox* automaticCompletionCheck{};
    QCheckBox* hoverInformationCheck{};
    QSpinBox* hoverDelaySpin{};
    QCheckBox* inlineDiagnosticsCheck{};
    QSpinBox* recentFilesLimitSpin{};
    QPushButton* clearRecentFilesButton{};
    QPushButton* applyButton{};
    QPushButton* cancelButton{};
    QPushButton* resetButton{};

    QVector<std::shared_ptr<SyntaxTheme>> availableThemes{};
    EditorPreferences baselinePreferences{};
    EditorPreferences draftPreferences{};
    bool clearRecentFilesOnApply = false;
    bool synchronizingControls = false;
};
