#include "TWelcomeWindow.h"
#include "Taif.h"
#include "TMenu.h"
#include <QtWidgets>


WelcomeWindow::WelcomeWindow(QWidget *parent)
    : QMainWindow(parent)
{

    setAttribute(Qt::WA_DeleteOnClose);
    // ===================================================================
    // الجزء الأول: القوائم وشريط الأدوات
    // ===================================================================

    TMenuBar *mainMenuBar = new TMenuBar(this);
    this->setMenuBar(mainMenuBar);

    // ===================================================================
    // الجزء الثاني: بناء الواجهة المركزية
    // ===================================================================

    QWidget *centralWidget = new QWidget(this);
    this->setCentralWidget(centralWidget);
    QVBoxLayout *mainVLayout = new QVBoxLayout(centralWidget);

    QHBoxLayout *headerContent = new QHBoxLayout();
    QLabel *logoLabel = new QLabel();
    logoLabel->setPixmap(QPixmap(":/icons/resources/TaifLogo.ico").scaled(90, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    QVBoxLayout *textVLayout = new QVBoxLayout();
    QLabel *titleLabel = new QLabel("أهلا في محرر طيف");
    QFont titleFont;
    QStringList NotoKufiArabicFont = QFontDatabase::applicationFontFamilies(2);
    titleFont.setFamily(NotoKufiArabicFont.at(0));
    titleFont.setPixelSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    QLabel *subtitleLabel = new QLabel("طيف - محرر لغة ألف");
    textVLayout->addWidget(titleLabel);
    textVLayout->addWidget(subtitleLabel);
    headerContent->addWidget(logoLabel);
    headerContent->addSpacing(15);
    headerContent->addLayout(textVLayout);
    headerContent->addSpacing(10);
    headerContent->addLayout(textVLayout);
    subtitleLabel->setFont(titleFont);

    QVBoxLayout *mainContentLayout = new QVBoxLayout();
    mainContentLayout->setSpacing(20);
    QSettings settings("Alif", "Taif");
    QStringList recentFiles = settings.value("RecentFiles").toStringList();
    QHBoxLayout *filesGroup = new QHBoxLayout();
    QVBoxLayout *filesButtons = new QVBoxLayout();
    newFileButton = new QPushButton("ملف جديد");
    openFileButton = new QPushButton("فتح الملف");
    openFolderButton = new QPushButton("فتح المجلد");
    filesButtons->addWidget(newFileButton);
    filesButtons->addWidget(openFileButton);
    filesButtons->addWidget(openFolderButton);
    filesButtons->addStretch();
    recentFilesList = new QListWidget();
    recentFilesList->addItems(recentFiles);
    recentFilesList->setMinimumWidth(450);
    filesGroup->addLayout(filesButtons);
    filesGroup->addWidget(recentFilesList);

    QHBoxLayout *sessionsGroup = new QHBoxLayout();
    QVBoxLayout *sessionsButtons = new QVBoxLayout();
    newSessionButton = new QPushButton("جلسة جديدة");
    manageSessionsButton = new QPushButton("إدارة الجلسات");
    sessionsButtons->addWidget(newSessionButton);
    sessionsButtons->addWidget(manageSessionsButton);
    sessionsButtons->addStretch();
    QLabel *noSessionsLabel = new QLabel("لا يوجد جلسات محفوظة");
    noSessionsLabel->setAlignment(Qt::AlignCenter);
    noSessionsLabel->setMinimumSize(450, 100);
    noSessionsLabel->setObjectName("sessionsBoxLabel");
    sessionsGroup->addLayout(sessionsButtons);
    sessionsGroup->addWidget(noSessionsLabel);

    // ===================================================================
    //  توحيد أحجام المكونات
    // ===================================================================
    int uniformWidth = 450;
    recentFilesList->setFixedWidth(uniformWidth);
    noSessionsLabel->setFixedWidth(uniformWidth);

    mainContentLayout->addLayout(filesGroup);
    mainContentLayout->addLayout(sessionsGroup);

    showOnStartupCheck = new QCheckBox("إظهار صفحة الترحيب عند بدأ البرنامج");
    showOnStartupCheck->setChecked(true);
    showOnStartupCheck->setDisabled(true);

    // ===================================================================
    // الجزء الثالث: التجميع النهائي والمحاذاة للمنتصف
    // ===================================================================
    mainVLayout->addStretch(1);
    QHBoxLayout *headerCenteringLayout = new QHBoxLayout();
    headerCenteringLayout->addStretch();
    headerCenteringLayout->addLayout(headerContent);
    headerCenteringLayout->addStretch();
    mainVLayout->addLayout(headerCenteringLayout);
    mainVLayout->addSpacing(30);
    QHBoxLayout *contentCenteringLayout = new QHBoxLayout();
    contentCenteringLayout->addStretch();
    contentCenteringLayout->addLayout(mainContentLayout);
    contentCenteringLayout->addStretch();
    mainVLayout->addLayout(contentCenteringLayout);
    mainVLayout->addSpacing(20);
    mainVLayout->addWidget(showOnStartupCheck, 0, Qt::AlignCenter);
    mainVLayout->addStretch(1);

    // ===================================================================
    // الجزء الرابع: تطبيق التصميم QSS (Styling)
    // ===================================================================


    QString styleSheet = R"(
        QWidget {
            background-color: #141520;
            color: #eeeeee;
            border-bottom-left-radius: 8px;
            border-bottom-right-radius: 8px;
        }
        QLabel#titleLabel { color: #ffffff; }
        QLabel { color: #eeeeee; }
        QPushButton {
            min-width: 70px;
            background-color: #3a3d41; border: 1px solid #555555;
            padding: 4px 12px; border-radius: 4px;
        }
        QPushButton:hover { background-color: #4f5357; }
        QPushButton:pressed { background-color: #2a2d31; }
        QListWidget, QLabel#sessionsBoxLabel {
            background-color: #1e202e; border: 1px solid #555555;
            border-radius: 8px; color: #909090;
        }
        TMenuBar{
            background-color: #1e202e;
            spacing: 5px; /* spacing between menu bar items */
        }

        QListWidget::item:hover { background-color: #2f2f3f; }
    )";
    this->setStyleSheet(styleSheet);

    this->setWindowTitle("صفحة الترحيب - محرر طيف");
    QScreen* screen = QGuiApplication::primaryScreen();
    QRect screenGeo = screen->availableGeometry();
    int margin = 100;
    int widthFixedNum = 6;
    int x = screenGeo.right() - screenGeo.size().width() + margin * widthFixedNum / 2;
    int y = screenGeo.top() + 30 + margin / 2; // 30 is top system bar height
    int width = screenGeo.size().width() - margin * widthFixedNum;
    int height = screenGeo.size().height() - margin;
    this->setGeometry(x, y, width, height);

    connect(mainMenuBar, &TMenuBar::newRequested, this, &WelcomeWindow::handleNewFileRequest);
    connect(mainMenuBar, &TMenuBar::openFileRequested, this, &WelcomeWindow::handleOpenFileRequest);
    connect(mainMenuBar, &TMenuBar::exitRequested, this, &QWidget::close);

    // ربط أزرار الواجهة المركزية
    connect(newFileButton, &QPushButton::clicked, this, &WelcomeWindow::handleNewFileRequest);
    connect(openFileButton, &QPushButton::clicked, this, &WelcomeWindow::handleOpenFileRequest);
    connect(openFolderButton, &QPushButton::clicked, this, &WelcomeWindow::handleOpenFolderRequest);
    connect(recentFilesList, &QListWidget::itemDoubleClicked, this, &WelcomeWindow::onRecentFileClicked);
}

void WelcomeWindow::onRecentFileClicked(QListWidgetItem *item)
{
    QString filePath = item->text();
    QFileInfo fileInfo(filePath);

    if (!fileInfo.exists() || !fileInfo.isFile()) {
        QMessageBox::warning(this, "ملف غير موجود",
                             "تعذر العثور على الملف:\n" + filePath + "\n\nربما تم نقله أو حذفه.");
        // delete item;
        return;
    }

    Taif *editor = new Taif(filePath);
    editor->show();
    this->close();
}

void WelcomeWindow::handleNewFileRequest()
{
    Taif *editor = new Taif();
    editor->show();
    this->close();
}

void WelcomeWindow::handleOpenFileRequest()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Open File");

    if (!filePath.isEmpty()) {
        Taif *editor = new Taif(filePath);
        editor->show();
        this->close();
    }
}

void WelcomeWindow::handleOpenFolderRequest()
{
    QString folderPath = QFileDialog::getExistingDirectory(this, "Open Folder");

    if (!folderPath.isEmpty()) {
        Taif *editor = new Taif();
        editor->loadFolder(folderPath);

        editor->show();
        this->close();
    }
}

void WelcomeWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
}

WelcomeWindow::~WelcomeWindow()
{
}
