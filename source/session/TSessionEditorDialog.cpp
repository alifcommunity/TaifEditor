#include "TSessionEditorDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr auto kSessionFileFilter = "ملفات ألف (*.alif *.aliflib *.txt);;كل الملفات (*)";

void applySessionDialogStyle(QDialog* const dialog) {
    dialog->setStyleSheet(QStringLiteral(R"(
        QDialog {
            background-color: #0f172a;
            color: #e2e8f0;
            font-family: "Tajawal", "Noto Kufi Arabic";
        }
        QLabel { color: #cbd5e1; }
        QLabel#SessionValidationLabel { color: #fca5a5; }
        QLineEdit, QListWidget {
            background-color: #111f37;
            color: #f1f5f9;
            border: 1px solid #35577c;
            border-radius: 6px;
            padding: 6px;
            selection-background-color: #2563eb;
        }
        QLineEdit:focus, QListWidget:focus { border-color: #60a5fa; }
        QListWidget::item { padding: 6px; border-radius: 4px; }
        QListWidget::item:selected { background-color: #1d4ed8; }
        QPushButton {
            min-width: 30px;
            max-width: 60px;
            background-color: #1e3a5f;
            color: #e2e8f0;
            border: 1px solid #35577c;
            border-radius: 6px;
            padding: 7px 7px;
        }
        QPushButton:hover { background-color: #2563eb; border-color: #60a5fa; }
        QPushButton:disabled { color: #64748b; background-color: #18263b; }
    )"));
}

} // namespace

SessionEditorDialog::SessionEditorDialog(QWidget* const parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("SessionEditorDialog"));
    setWindowTitle(QStringLiteral("إضافة جلسة"));
    setModal(true);
    resize(650, 480);
    applySessionDialogStyle(this);

    auto* const rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(10);

    QLabel* const nameLabel = new QLabel(QStringLiteral("اسم الجلسة"), this);
    nameInput = new QLineEdit(this);
    nameInput->setObjectName(QStringLiteral("SessionNameInput"));
    nameInput->setPlaceholderText(QStringLiteral("مثال: مشروع التحكم بالنظام"));

    QLabel* const filesLabel = new QLabel(QStringLiteral("ملفات الجلسة"), this);
    filesList = new QListWidget(this);
    filesList->setObjectName(QStringLiteral("SessionFilesList"));
    filesList->setSelectionMode(QAbstractItemView::SingleSelection);
    filesList->setMinimumHeight(250);

    QHBoxLayout* const controlsLayout = new QHBoxLayout();
    QPushButton* const addButton = new QPushButton(QStringLiteral("＋"), this);
    addButton->setObjectName(QStringLiteral("AddSessionFilesButton"));
    removeButton = new QPushButton(QIcon(":/icons/resources/trash.svg"), QStringLiteral(""), this);
    QIcon trashIcon{};
    trashIcon.addFile(":/icons/resources/trash.svg", {}, QIcon::Normal);
    trashIcon.addFile(":/icons/resources/trash-disabled.svg", {}, QIcon::Disabled);
    removeButton->setIcon(trashIcon);
    removeButton->setObjectName(QStringLiteral("RemoveSessionFileButton"));
    moveUpButton = new QPushButton(QStringLiteral("↑"), this);
    moveUpButton->setObjectName(QStringLiteral("MoveSessionFileUpButton"));
    moveDownButton = new QPushButton(QStringLiteral("↓"), this);
    moveDownButton->setObjectName(QStringLiteral("MoveSessionFileDownButton"));
    controlsLayout->addWidget(addButton);
    controlsLayout->addWidget(removeButton);
    controlsLayout->addWidget(moveUpButton);
    controlsLayout->addWidget(moveDownButton);
    controlsLayout->addStretch();

    validationLabel = new QLabel(this);
    validationLabel->setObjectName(QStringLiteral("SessionValidationLabel"));
    validationLabel->setWordWrap(true);
    validationLabel->hide();

    QDialogButtonBox* const buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("حفظ"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("إلغاء"));

    rootLayout->addWidget(nameLabel);
    rootLayout->addWidget(nameInput);
    rootLayout->addWidget(filesLabel);
    rootLayout->addWidget(filesList, 1);
    rootLayout->addLayout(controlsLayout);
    rootLayout->addWidget(validationLabel);
    rootLayout->addWidget(buttons);

    connect(addButton, &QPushButton::clicked, this, &SessionEditorDialog::addFiles);
    connect(removeButton, &QPushButton::clicked, this, &SessionEditorDialog::removeSelectedFiles);
    connect(moveUpButton, &QPushButton::clicked, this, &SessionEditorDialog::moveSelectedFileUp);
    connect(moveDownButton, &QPushButton::clicked, this, &SessionEditorDialog::moveSelectedFileDown);
    connect(filesList, &QListWidget::currentRowChanged, this, &SessionEditorDialog::refreshFileState);
    connect(nameInput, &QLineEdit::textChanged, this, [this]() { setValidationMessage({}); });
    connect(buttons, &QDialogButtonBox::accepted, this, &SessionEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshFileState();
}

void SessionEditorDialog::setSession(const SavedSession& session)
{
    editedSession = SessionStore::normalize(session);
    setWindowTitle(editedSession.id.isEmpty()
        ? QStringLiteral("إضافة جلسة")
        : QStringLiteral("تعديل الجلسة"));
    nameInput->setText(editedSession.displayName);
    filesList->clear();
    for (const QString& path : editedSession.filePaths) {
        appendPath(path);
    }
    refreshFileState();
}

SavedSession SessionEditorDialog::session() const
{
    SavedSession result = editedSession;
    result.displayName = nameInput->text().trimmed();
    result.filePaths.clear();
    for (int index = 0; index < filesList->count(); ++index) {
        result.filePaths.append(filesList->item(index)->data(Qt::UserRole).toString());
    }
    return SessionStore::normalize(std::move(result));
}

void SessionEditorDialog::accept()
{
    if (nameInput->text().trimmed().isEmpty()) {
        setValidationMessage(QStringLiteral("اكتب اسمًا للجلسة قبل الحفظ."));
        nameInput->setFocus();
        return;
    }
    QDialog::accept();
}

void SessionEditorDialog::addFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("إضافة ملفات إلى الجلسة"), {}, QString::fromUtf8(kSessionFileFilter));
    for (const QString& path : paths) {
        appendPath(path);
    }
    refreshFileState();
}

void SessionEditorDialog::removeSelectedFiles()
{
    delete filesList->takeItem(filesList->currentRow());
    refreshFileState();
}

void SessionEditorDialog::moveSelectedFileUp()
{
    const int row = filesList->currentRow();
    if (row <= 0) {
        return;
    }
    QListWidgetItem* const item = filesList->takeItem(row);
    filesList->insertItem(row - 1, item);
    filesList->setCurrentRow(row - 1);
    refreshFileState();
}

void SessionEditorDialog::moveSelectedFileDown()
{
    const int row = filesList->currentRow();
    if (row < 0 || row >= filesList->count() - 1) {
        return;
    }
    QListWidgetItem* const item = filesList->takeItem(row);
    filesList->insertItem(row + 1, item);
    filesList->setCurrentRow(row + 1);
    refreshFileState();
}

void SessionEditorDialog::refreshFileState()
{
    const int row = filesList->currentRow();
    removeButton->setEnabled(row >= 0);
    moveUpButton->setEnabled(row > 0);
    moveDownButton->setEnabled(row >= 0 && row < filesList->count() - 1);
}

void SessionEditorDialog::appendPath(const QString& path)
{
    const QString normalizedPath = SessionStore::normalizePath(path);
    if (normalizedPath.isEmpty()) {
        return;
    }
    for (int index = 0; index < filesList->count(); ++index) {
        if (filesList->item(index)->data(Qt::UserRole).toString().compare(
                normalizedPath, Qt::CaseInsensitive) == 0) {
            return;
        }
    }

    auto* const item = new QListWidgetItem(QFileInfo(normalizedPath).fileName(), filesList);
    item->setData(Qt::UserRole, normalizedPath);
    item->setToolTip(normalizedPath);
    if (!QFileInfo::exists(normalizedPath)) {
        item->setText(QStringLiteral("%1 — ملف غير موجود").arg(QFileInfo(normalizedPath).fileName()));
        item->setForeground(QColor(QStringLiteral("#fca5a5")));
    }
}

void SessionEditorDialog::setValidationMessage(const QString& message)
{
    validationLabel->setText(message);
    validationLabel->setVisible(!message.isEmpty());
}
