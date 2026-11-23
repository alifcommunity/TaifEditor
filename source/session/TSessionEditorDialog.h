#pragma once

#include "SessionStore.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class SessionEditorDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SessionEditorDialog(QWidget* parent = nullptr);

    void setSession(const SavedSession& session);
    [[nodiscard]] SavedSession session() const;

protected:
    void accept() override;

private slots:
    void addFiles();
    void removeSelectedFiles();
    void moveSelectedFileUp();
    void moveSelectedFileDown();
    void refreshFileState();

private:
    void appendPath(const QString& path);
    void setValidationMessage(const QString& message);

    SavedSession editedSession{};
    QLineEdit* nameInput = nullptr;
    QListWidget* filesList = nullptr;
    QLabel* validationLabel = nullptr;
    QPushButton* removeButton = nullptr;
    QPushButton* moveUpButton = nullptr;
    QPushButton* moveDownButton = nullptr;
};
