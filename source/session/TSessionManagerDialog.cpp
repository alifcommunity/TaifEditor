#include "TSessionManagerDialog.h"
#include "TSessionEditorDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int kSessionIdRole = Qt::UserRole;

void applySessionManagerDialogStyle(QDialog* const dialog)
{
    dialog->setStyleSheet(QStringLiteral(R"(
        QDialog {
            background-color: #0f172a;
            color: #e2e8f0;
        }
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

QListWidgetItem* addSessionItem(QListWidget* const list, const SavedSession& session)
{
    const QString fileCount = QString::number(session.filePaths.size());
    auto* const item = new QListWidgetItem(
        QStringLiteral("%1\n%2 ملف").arg(session.displayName, fileCount), list);
    item->setData(kSessionIdRole, session.id);
    item->setToolTip(session.filePaths.isEmpty()
        ? QStringLiteral("جلسة فارغة")
        : session.filePaths.join(u'\n'));
    return item;
}

const SavedSession* sessionForItem(const QVector<SavedSession>& sessions,
                                   const QListWidgetItem* const item)
{
    if (item == nullptr) {
        return nullptr;
    }
    const QString id = item->data(kSessionIdRole).toString();
    for (const SavedSession& session : sessions) {
        if (session.id == id) {
            return &session;
        }
    }
    return nullptr;
}

} // namespace

SessionManagerDialog::SessionManagerDialog(SessionStore& sessionStore, QWidget* const parent)
    : QDialog(parent)
    , m_sessionStore(sessionStore)
{
    setObjectName(QStringLiteral("SessionManagementDialog"));
    setWindowTitle(QStringLiteral("إدارة الجلسات"));
    setModal(true);
    resize(540, 420);
    applySessionManagerDialogStyle(this);

    auto* const layout = new QVBoxLayout(this);
    m_sessionsList = new QListWidget(this);
    m_sessionsList->setObjectName(QStringLiteral("ManagedSessionsList"));
    auto* const controls = new QHBoxLayout();
    m_openButton = new QPushButton(QStringLiteral("فتح"), this);
    m_openButton->setObjectName(QStringLiteral("OpenSessionButton"));
    m_editButton = new QPushButton(QStringLiteral("تعديل"), this);
    m_editButton->setObjectName(QStringLiteral("EditSessionButton"));
    m_deleteButton = new QPushButton(QIcon(QStringLiteral(":/icons/resources/trash.svg")), QString(), this);
    QIcon trashIcon{};
    trashIcon.addFile(QStringLiteral(":/icons/resources/trash.svg"), {}, QIcon::Normal);
    trashIcon.addFile(QStringLiteral(":/icons/resources/trash-disabled.svg"), {}, QIcon::Disabled);
    m_deleteButton->setIcon(trashIcon);
    m_deleteButton->setObjectName(QStringLiteral("DeleteSessionButton"));
    m_createButton = new QPushButton(QStringLiteral("＋"), this);
    m_createButton->setObjectName(QStringLiteral("CreateSessionButton"));
    controls->addWidget(m_openButton);
    controls->addWidget(m_editButton);
    controls->addWidget(m_deleteButton);
    controls->addStretch();
    controls->addWidget(m_createButton);
    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("إغلاق"));

    layout->addWidget(m_sessionsList, 1);
    layout->addLayout(controls);
    layout->addWidget(buttons);

    connect(m_createButton, &QPushButton::clicked, this, &SessionManagerDialog::createSession);
    connect(m_editButton, &QPushButton::clicked, this, &SessionManagerDialog::editSelectedSession);
    connect(m_deleteButton, &QPushButton::clicked, this, &SessionManagerDialog::deleteSelectedSession);
    connect(m_openButton, &QPushButton::clicked, this, &SessionManagerDialog::acceptSelectedSession);
    connect(m_sessionsList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { acceptSelectedSession(); });
    connect(m_sessionsList, &QListWidget::itemSelectionChanged,
            this, &SessionManagerDialog::refreshButtonState);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshSessions();
}

std::optional<SavedSession> SessionManagerDialog::sessionToOpen() const
{
    return m_sessionToOpen;
}

void SessionManagerDialog::refreshSessions()
{
    m_sessionsList->clear();
    for (const SavedSession& session : m_sessionStore.loadAll()) {
        addSessionItem(m_sessionsList, session);
    }
    refreshButtonState();
}

void SessionManagerDialog::refreshButtonState() {
    const bool hasSelection = !m_sessionsList->selectedItems().isEmpty();
    m_openButton->setEnabled(hasSelection);
    m_editButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
}

void SessionManagerDialog::createSession()
{
    if (editSession({}, true)) {
        refreshSessions();
    }
}

void SessionManagerDialog::editSelectedSession()
{
    if (const auto selected = selectedSession(); selected.has_value()
        && editSession(*selected, false)) {
        refreshSessions();
    }
}

void SessionManagerDialog::deleteSelectedSession()
{
    const auto selected = selectedSession();
    if (!selected.has_value()) {
        return;
    }
    QMessageBox confirmation(
        QMessageBox::Question, QStringLiteral("حذف جلسة"),
        QStringLiteral("هل تريد حذف الجلسة «%1»؟\nلن يتم حذف أي ملفات.")
            .arg(selected->displayName),
        QMessageBox::Yes | QMessageBox::No, this);
    confirmation.setButtonText(QMessageBox::Yes, QStringLiteral("نعم"));
    confirmation.setButtonText(QMessageBox::No, QStringLiteral("لا"));
    confirmation.setDefaultButton(QMessageBox::No);
    if (confirmation.exec() != QMessageBox::Yes) {
        return;
    }
    QString errorMessage;
    if (!m_sessionStore.remove(selected->id, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("تعذر حذف الجلسة"), errorMessage);
        return;
    }
    emit sessionsChanged();
    refreshSessions();
}

void SessionManagerDialog::acceptSelectedSession()
{
    if (const auto selected = selectedSession(); selected.has_value()) {
        m_sessionToOpen = *selected;
        accept();
    }
}

std::optional<SavedSession> SessionManagerDialog::selectedSession() const
{
    const QVector<SavedSession> sessions = m_sessionStore.loadAll();
    if (const SavedSession* const selected = sessionForItem(sessions, m_sessionsList->currentItem())) {
        return *selected;
    }
    return std::nullopt;
}

bool SessionManagerDialog::editSession(SavedSession session, const bool isNew)
{
    SessionEditorDialog dialog(this);
    dialog.setSession(std::move(session));
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    QString errorMessage;
    const bool saved = isNew
        ? m_sessionStore.create(dialog.session(), &errorMessage)
        : m_sessionStore.update(dialog.session(), &errorMessage);
    if (!saved) {
        QMessageBox::warning(this, QStringLiteral("تعذر حفظ الجلسة"), errorMessage);
        return false;
    }
    emit sessionsChanged();
    return true;
}
