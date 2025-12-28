#pragma once
// #include <QWidget>
// #include <QAbstractScrollArea>
// #include <QTimer>
// #include <QMutex>
// #include <QPlainTextEdit>


// class QLineEdit;


// // ConsoleView: widget that draws lines from a circular buffer efficiently
// class ConsoleView : public QAbstractScrollArea {
//     Q_OBJECT
// public:
//     explicit ConsoleView(QWidget *parent = nullptr);


//     void appendLines(const QStringList &lines); // append multiple lines
//     void clearBuffer();
//     void setMaxLines(int maxLines);
//     int lineHeight() const { return m_lineHeight; }
//     int maxLines() const { return m_maxLines; }


//     // For compatibility with previous code that inspected document()
//     // We provide a lightweight QStringList access (not a QTextDocument)
//     QStringList lines();


// protected:
//     void paintEvent(QPaintEvent *event) override;
//     void resizeEvent(QResizeEvent *event) override;
//     void wheelEvent(QWheelEvent *event) override;
//     void keyPressEvent(QKeyEvent *event) override;


// private:
//     void updateScrollBar();
//     QStringList m_buffer;
//     int m_maxLines = 10000; // default
//     int m_firstVisible = 0; // index in buffer of the first visible line
//     int m_lineHeight = 16;
//     QFont m_font;
//     QMutex m_mutex; // protect buffer access when appended from other threads
// };

// // TConsole: composite widget with ConsoleView + input QLineEdit
// class TConsole : public QWidget {
//     Q_OBJECT
// public:
//     explicit TConsole(QWidget *parent = nullptr);
//     ~TConsole() override;

// public slots:
//     void appendPlainText(const QString &text);
//     void appendPlainTextThreadSafe(const QString &text);
//     void clear();
//     void setConsoleRTL();
//     void startCmd();

// signals:
//     void commandEntered(const QString &cmd);
//     void appendRequested();

// private slots:
//     void onInputReturn();
//     void flushBufferTimeout();

// private:
//     ConsoleView *m_view;
//     QLineEdit *m_input;

//     QStringList m_stage;
//     QMutex m_stageMutex;
//     QTimer *m_flushTimer;
//     int m_maxLines = 5000;
// };

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QProcess>
#include <QTimer>
#include <QMutex>
#include <QVector>

class TConsole : public QWidget {
    Q_OBJECT
public:
    explicit TConsole(QWidget *parent = nullptr);
    ~TConsole() override;

    // actions
    void startCmd();                    // start cmd.exe (Windows)
    void stopCmd();                     // stop process
    void clear();                       // clear output
    void setConsoleRTL();               // force RTL on widgets
    void appendPlainTextThreadSafe(const QString &text); // thread-safe append

signals:
    void commandEntered(const QString &cmd); // emitted when user enters command

private slots:
    void processStdout();
    void processStderr();
    void processFinished(int code, QProcess::ExitStatus status);
    void onInputReturn();
    void flushPending();

private:
    QPlainTextEdit *m_output{};
    QLineEdit *m_input{};
    QProcess *m_process{};
    QTimer *m_flushTimer{};

    QMutex m_pendingMutex{};
    QStringList m_pending{}; // staging lines

    // history
    QVector<QString> m_history{};
    int m_historyIndex{}; // -1 means not browsing

    // autoscroll
    bool m_autoscroll{};

    // helpers
    void appendOutput(const QString &text); // needs to run in GUI thread
    QString ansiToHtmlFragment(const QString &chunk); // simple ansi -> html/text formatting
    bool eventFilter(QObject *obj, QEvent *ev) override;

    QStringList m_buffer{};          // تخزين جميع الأسطر
    int m_maxLines = 2000;         // آخر 2000 سطر
};

