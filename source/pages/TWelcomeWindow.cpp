#include "TWelcomeWindow.h"
#include "TSessionEditorDialog.h"
#include "TSessionManagerDialog.h"
#include "TaifBootstrap.h"

#include <QWidget>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStackedLayout>
#include <QVBoxLayout>

#include <utility>

constexpr int kSessionIdRole = Qt::UserRole;

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

WelcomeWindow::WelcomeWindow(QWidget* const parent,
                             SessionStore::SettingsScope sessionScope)
    : QMainWindow(parent)
    , sessionStore(std::move(sessionScope))
{
    setAttribute(Qt::WA_DeleteOnClose);

    setupStyle();

    QWidget* const centralWidget = new QWidget(this);
    QVBoxLayout* const mainLayout = new QVBoxLayout(centralWidget);
    setCentralWidget(centralWidget);

    QVBoxLayout* const headerContent = new QVBoxLayout();
    QLabel* const logoLabel = new QLabel(centralWidget);
    logoLabel->setAlignment(Qt::AlignCenter);
    QPixmap logo(QStringLiteral(":/icons/resources/TaifLogo.ico"));
    logoLabel->setPixmap(logo.scaled(
        153, 153,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
        ));
    QVBoxLayout* const textLayout = new QVBoxLayout();
    auto* const titleLabel = new QLabel(QStringLiteral("طــيــف\nمحرر لغة البرمجة العربية ألف نـ5"), centralWidget);
    titleLabel->setObjectName(QStringLiteral("titleLabel"));
    titleLabel->setAlignment(Qt::AlignCenter);

    // ضبط الخط هنا لا يملك تأثير على نافذة الترحيب
    // وذلك لأن الخط والحجم تم ضبطه في setStyleSheet
    // ومع ذلك سيتم الإحتفاظ بهذا الضبط في حال الإستخدام لاحقا
    // او في حال فشل ضبط الخط باستخدام التنسيق styleSheet
    QFont titleFont = titleLabel->font();
    const QString displayArabicFamily = TaifBootstrap::notoKufiFontFamily();
    if (!displayArabicFamily.isEmpty()) {
        titleFont.setFamily(displayArabicFamily);
    }
    titleFont.setPixelSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    textLayout->addWidget(titleLabel);
    headerContent->addWidget(logoLabel);
    headerContent->addSpacing(12);
    headerContent->addLayout(textLayout);

    QVBoxLayout* const mainContentLayout = new QVBoxLayout();
    mainContentLayout->setSpacing(20);

    QHBoxLayout* const filesGroup = new QHBoxLayout();
    QVBoxLayout* const filesButtons = new QVBoxLayout();
    newFileButton = new QPushButton(QStringLiteral("ملف جديد"), centralWidget);
    newFileButton->setObjectName(QStringLiteral("primaryButton"));
    openFileButton = new QPushButton(QStringLiteral("فتح ملف"), centralWidget);
    openFolderButton = new QPushButton(QStringLiteral("فتح مجلد"), centralWidget);
    filesButtons->addWidget(newFileButton);
    filesButtons->addWidget(openFileButton);
    filesButtons->addWidget(openFolderButton);
    filesButtons->addStretch();

    // عند الوراثة من الأب, يحصل خطأ في الشريط التمرير الجانبي حيث تختفي الخلفيات وتصبح شفافة
    recentFilesList = new QListWidget();
    recentFilesList->setObjectName(QStringLiteral("RecentFilesList"));
    QSettings settings(QStringLiteral("Alif"), QStringLiteral("Taif"));
    QStringList recentFiles = settings.value(QStringLiteral("RecentFiles")).toStringList();
    recentFilesList->addItems(recentFiles);
    recentFilesList->setWordWrap(true);
    recentFilesList->setFixedWidth(450);
    filesGroup->addLayout(filesButtons);
    filesGroup->addWidget(recentFilesList);

    QHBoxLayout* const sessionsGroup = new QHBoxLayout();
    QVBoxLayout* const sessionsButtons = new QVBoxLayout();
    newSessionButton = new QPushButton(QStringLiteral("جلسة جديدة"), centralWidget);
    newSessionButton->setObjectName(QStringLiteral("NewSessionButton"));
    manageSessionsButton = new QPushButton(QStringLiteral("إدارة الجلسات"), centralWidget);
    manageSessionsButton->setObjectName(QStringLiteral("ManageSessionsButton"));
    sessionsButtons->addWidget(newSessionButton);
    sessionsButtons->addWidget(manageSessionsButton);
    sessionsButtons->addStretch();

    QWidget* const sessionsContent = new QWidget(centralWidget);
    sessionsContent->setFixedWidth(450);
    sessionsContentLayout = new QStackedLayout(sessionsContent);
    sessionsContentLayout->setContentsMargins(0, 0, 0, 0);
    savedSessionsList = new QListWidget(sessionsContent);
    savedSessionsList->setObjectName(QStringLiteral("SavedSessionsList"));
    savedSessionsList->setWordWrap(true);
    noSessionsLabel = new QLabel(QStringLiteral("لا توجد جلسات محفوظة"), sessionsContent);
    noSessionsLabel->setObjectName(QStringLiteral("sessionsBoxLabel"));
    noSessionsLabel->setAlignment(Qt::AlignCenter);
    noSessionsLabel->setMinimumHeight(100);
    sessionsContentLayout->addWidget(savedSessionsList);
    sessionsContentLayout->addWidget(noSessionsLabel);

    sessionsGroup->addLayout(sessionsButtons);
    sessionsGroup->addWidget(sessionsContent);

    mainContentLayout->addLayout(filesGroup);
    mainContentLayout->addLayout(sessionsGroup);

    mainLayout->addStretch(1);
    QVBoxLayout* const headerCenteringLayout = new QVBoxLayout();
    headerCenteringLayout->addStretch();
    headerCenteringLayout->addLayout(headerContent);
    headerCenteringLayout->addStretch();
    mainLayout->addLayout(headerCenteringLayout);
    mainLayout->addSpacing(70);
    QHBoxLayout* const contentCentering = new QHBoxLayout();
    contentCentering->addStretch();
    contentCentering->addLayout(mainContentLayout);
    contentCentering->addStretch();
    mainLayout->addLayout(contentCentering);
    mainLayout->addStretch(3);

    setWindowTitle(QStringLiteral("صفحة الترحيب — محرر طيف"));
    QScreen* screen = QGuiApplication::primaryScreen();
    QRect screenGeo = screen->availableGeometry();
    int margin = 90;
    int widthFixedNum = 5;
    int x = screenGeo.right() - screenGeo.size().width() + margin * widthFixedNum / 2;
    int y = screenGeo.top() + 30 + margin / 2; // 30 is top system bar height
    int width = screenGeo.size().width() - margin * widthFixedNum;
    int height = screenGeo.size().height() - margin;
    this->setGeometry(x, y, width, height);

    // ربط أزرار الواجهة المركزية
    connect(newFileButton, &QPushButton::clicked, this, &WelcomeWindow::handleNewFileRequest);
    connect(openFileButton, &QPushButton::clicked, this, &WelcomeWindow::handleOpenFileRequest);
    connect(openFolderButton, &QPushButton::clicked, this, &WelcomeWindow::handleOpenFolderRequest);
    connect(recentFilesList, &QListWidget::itemDoubleClicked,
            this, &WelcomeWindow::onRecentFileClicked);
    connect(newSessionButton, &QPushButton::clicked, this, &WelcomeWindow::createSession);
    connect(manageSessionsButton, &QPushButton::clicked, this, &WelcomeWindow::manageSessions);
    connect(savedSessionsList, &QListWidget::itemDoubleClicked,
            this, &WelcomeWindow::openSelectedSession);

    refreshSessions();
}

void WelcomeWindow::setupStyle() {
    // Palette:
    // Background: #0f172a (Deep Navy)
    // Surface:    #1e293b (Slate Blue)
    // Accent:     #3b82f6 (Electric Blue)
    // Text:       #f1f5f9 (Off White)
    // Muted:      #94a3b8 (Slate Grey)
    setStyleSheet(QStringLiteral(R"(
        QWidget {
            background-color: #0f172a;
            color: #f1f5f9;
            font-family: "Tajawal", "Noto Kufi Arabic", Roboto, Helvetica, Arial;
        }

        /* very important for QScrollBar background */
        QListWidget QWidget {
            background-color: #1e293b;
        }

        /* General Labels */
        QLabel {
            color: #94a3b8;
            font-size: 13px;
        }
        /* The Title Label - Large and Bold */
        QLabel#titleLabel {
            color: #ffffff;
            font-size: 24px;
            font-weight: bold;
            margin-bottom: 10px;
        }


        /* --- Modern Buttons --- */
        QPushButton {
            min-width: 90px;
            background-color: #1e293b;
            color: #f1f5f9;
            border: 1px solid #334155;
            padding: 8px 16px;
            border-radius: 6px;
            font-weight: 500;
        }
        QPushButton:hover {
            background-color: #334155;
            border-color: #3b82f6;
            color: #ffffff;
        }
        QPushButton:pressed {
            background-color: #0f172a;
            color: #3b82f6;
        }

        /* Special Primary Button */
        QPushButton#primaryButton {
            background-color: #3b82f6;
            color: white;
            border: none;
        }
        QPushButton#primaryButton:hover {
            background-color: #2563eb;
        }

        /* --- Sessions List --- */
        QListWidget, QLabel#sessionsBoxLabel {
            background-color: #1e293b;
            border: 1px solid #334155;
            border-radius: 12px;
            color: #f1f5f9;
            outline: none;
            padding: 5px;
            font-size: 14px;
        }

        /* Styling the items inside the list to look like cards */
        QListWidget::item {
            background-color: transparent;
            color: #94a3b8;
            padding: 8px;
            border-radius: 8px;
            margin: 2px 0px;
        }
        QListWidget::item:hover {
            background-color: #334155;
            color: #ffffff;
        }
        QListWidget::item:selected {
            background-color: #3b82f6;
            color: #ffffff;
            border: 1px solid #60a5fa;
        }
    )"));
}

void WelcomeWindow::refreshSessions()
{
    const QVector<SavedSession> sessions = sessionStore.loadAll();
    savedSessionsList->clear();
    for (const SavedSession& session : sessions) {
        addSessionItem(savedSessionsList, session);
    }
    sessionsContentLayout->setCurrentWidget(sessions.isEmpty()
        ? static_cast<QWidget*>(noSessionsLabel)
        : static_cast<QWidget*>(savedSessionsList));
}

bool WelcomeWindow::editSession(SavedSession session, const bool isNew)
{
    SessionEditorDialog dialog(this);
    dialog.setSession(session);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    QString errorMessage;
    const bool saved = isNew
        ? sessionStore.create(dialog.session(), &errorMessage)
        : sessionStore.update(dialog.session(), &errorMessage);
    if (!saved) {
        QMessageBox::warning(this, QStringLiteral("تعذر حفظ الجلسة"), errorMessage);
        return false;
    }
    refreshSessions();
    return true;
}

void WelcomeWindow::createSession() {
    editSession({}, true);
}

void WelcomeWindow::manageSessions() {
    SessionManagerDialog dialog(sessionStore, this);
    connect(&dialog, &SessionManagerDialog::sessionsChanged,
            this, &WelcomeWindow::refreshSessions);
    dialog.exec();
    refreshSessions();
    if (const auto session = dialog.sessionToOpen(); session.has_value()) {
        openSession(*session);
    }
}

void WelcomeWindow::openSelectedSession(QListWidgetItem* const item)
{
    const QVector<SavedSession> sessions = sessionStore.loadAll();
    if (const SavedSession* const selected = sessionForItem(sessions, item)) {
        openSession(*selected);
    }
}

void WelcomeWindow::openSession(const SavedSession& session) {
    emit sessionOpenRequested(session);
}

void WelcomeWindow::onRecentFileClicked(QListWidgetItem* const item)
{
    const QString filePath = item->text();
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        QMessageBox::warning(this, QStringLiteral("ملف غير موجود"),
                             QStringLiteral("تعذر العثور على الملف:\n%1\n\nربما تم نقله أو حذفه.")
                                 .arg(filePath));
        return;
    }

    emit fileOpenRequested(filePath);
}

void WelcomeWindow::handleNewFileRequest() {
    emit newDocumentRequested();
}

void WelcomeWindow::handleOpenFileRequest() {
    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("فتح ملف"), {},
        QStringLiteral("ملف ألف (*.alif *.aliflib);;كل الملفات (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    emit fileOpenRequested(filePath);
}

void WelcomeWindow::handleOpenFolderRequest() {
    const QString folderPath = QFileDialog::getExistingDirectory(this, QStringLiteral("فتح مجلد"));
    if (folderPath.isEmpty()) {
        return;
    }

    emit folderOpenRequested(folderPath);
}

void WelcomeWindow::closeEvent(QCloseEvent* const event) {
    event->accept();
}

WelcomeWindow::~WelcomeWindow() = default;
