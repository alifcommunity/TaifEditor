#include "TConsole.h"
#include <QVBoxLayout>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QTextBlockFormat>
#include <QApplication>
#include <QTextBlock>


TConsole::TConsole(QWidget *parent)
    : QWidget(parent),
    m_output(new QPlainTextEdit(this)),
    m_input(new QLineEdit(this)),
    m_process(new QProcess(this)),
    m_flushTimer(new QTimer(this)),
    m_historyIndex(-1),
    m_autoscroll(true)
{
    // UI
    m_output->setReadOnly(true);
    m_output->setUndoRedoEnabled(false);
    m_output->setWordWrapMode(QTextOption::WordWrap);
    // simple monospace font
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPixelSize(15);
    m_output->setFont(f);
    m_input->setFont(f);

    setStyleSheet("QWidget {background-color: #03091A; color: #DEE8FF;}");

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    lay->addWidget(m_output);
    lay->addWidget(m_input);
    setLayout(lay);

    setLayoutDirection(Qt::RightToLeft);
    m_input->setLayoutDirection(Qt::RightToLeft);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &TConsole::processStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &TConsole::processStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TConsole::processFinished);

    connect(m_input, &QLineEdit::returnPressed, this, &TConsole::onInputReturn);

    m_input->installEventFilter(this);

    m_flushTimer->setInterval(25);
    connect(m_flushTimer, &QTimer::timeout, this, &TConsole::flushPending);
    m_flushTimer->start();
}

TConsole::~TConsole()
{
    stopCmd();
}

void TConsole::startCmd()
{
    if (m_process->state() != QProcess::NotRunning) return;

#if defined(Q_OS_WIN)
    m_process->start("cmd.exe");

#elif defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    QStringList args;
    args << "-c" << "import pty; pty.spawn('/bin/bash')";

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "xterm-256color");
    m_process->setProcessEnvironment(env);
    m_process->start("python3", args);
#endif
}

void TConsole::stopCmd()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(500)) {
            m_process->kill();
            m_process->waitForFinished(200);
        }
    }
}

void TConsole::clear()
{
    m_output->clear();
    m_pending.clear();
    m_buffer.clear();
}

void TConsole::setConsoleRTL()
{
    setLayoutDirection(Qt::RightToLeft);

    QTextOption opt = m_output->document()->defaultTextOption();
    opt.setTextDirection(Qt::RightToLeft);
    opt.setAlignment(Qt::AlignRight);
    m_output->document()->setDefaultTextOption(opt);
}

void TConsole::appendPlainTextThreadSafe(const QString &text)
{
    QMutexLocker locker(&m_pendingMutex);
    QStringList parts = text.split('\n');
    for (QString s : parts) {
        if (s.endsWith('\r')) s.chop(1);
        m_pending.append(s);
    }
    locker.unlock();
}

void TConsole::processStdout()
{
    QByteArray d = m_process->readAllStandardOutput();
#if defined(Q_OS_WIN)
    QString s = QString::fromLocal8Bit(d);
#else
    QString s = QString::fromUtf8(d); // لينكس يستخدم UTF-8
#endif
    appendPlainTextThreadSafe(s);

}

void TConsole::processStderr()
{
    QByteArray d = m_process->readAllStandardError();
    QString s = QString::fromLocal8Bit(d);
    appendPlainTextThreadSafe(s);
}

void TConsole::processFinished(int code, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    appendPlainTextThreadSafe(QString("\n[Process finished with code %1]\n").arg(code));
}

void TConsole::onInputReturn()
{
    QString cmd = m_input->text();
    if (cmd.isEmpty()) {
        if (m_process->state() != QProcess::NotRunning) {
#if defined(Q_OS_WIN)
            m_process->write("\r\n");
#else
            m_process->write("\n");
#endif
        }
        return;
    }

    if (m_history.isEmpty() || m_history.last() != cmd) {
        m_history.append(cmd);
    }
    m_historyIndex = -1;

// echo the command locally (like terminal)
// appendPlainTextThreadSafe(cmd + "\n");
#if defined(Q_OS_WIN)
    appendPlainTextThreadSafe(cmd + "\n");
#endif

    // send to process (CRLF on Windows)
#if defined(Q_OS_WIN)
    if (m_process->state() != QProcess::NotRunning) {
        m_process->write((cmd + "\r\n").toLocal8Bit());
    }
#else
    if (m_process->state() != QProcess::NotRunning) {
        m_process->write((cmd + "\n").toLocal8Bit());
    }
#endif

    emit commandEntered(cmd);
    m_input->clear();
}

void TConsole::flushPending() {
    QStringList items;
    {
        QMutexLocker locker(&m_pendingMutex);
        if (m_pending.isEmpty()) return;
        items = m_pending;
        m_pending.clear();
    }

    for (const QString &line : items) {
        m_buffer.append(line);
    }
    while (m_buffer.size() > m_maxLines)
        m_buffer.pop_front();

    m_output->setPlainText(m_buffer.join("\n"));

    if (m_autoscroll) {
        QScrollBar *sb = m_output->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

void TConsole::appendOutput(const QString &text)
{
    static QRegularExpression re("\x1B\\[([0-9;]+)m");
    int pos = 0;
    QRegularExpressionMatch match;

    QTextCursor cur = m_output->textCursor();
    cur.movePosition(QTextCursor::End);
    m_output->setTextCursor(cur);

    QTextCharFormat curFmt = m_output->currentCharFormat();

    QString s = text;
    int idx = 0;
    while ((match = re.match(s, idx)).hasMatch()) {
        int start = match.capturedStart();
        int end = match.capturedEnd();
        if (start > idx) {
            QString piece = s.mid(idx, start - idx);
            m_output->moveCursor(QTextCursor::End);
            m_output->setCurrentCharFormat(curFmt);
            m_output->insertPlainText(piece);
        }
        QString codeStr = match.captured(1);
        QStringList parts = codeStr.split(';');
        for (const QString &p : parts) {
            bool ok = false;
            int v = p.toInt(&ok);
            if (!ok) continue;
            if (v == 0) {
                curFmt = QTextCharFormat();
            } else if (v == 1) {
                curFmt.setFontWeight(QFont::Bold);
            } else if (v >= 30 && v <= 37) {
                QColor c;
                switch (v) {
                case 30: c = Qt::black; break;
                case 31: c = Qt::red; break;
                case 32: c = Qt::green; break;
                case 33: c = QColor(255, 165, 0); break;
                case 34: c = Qt::blue; break;
                case 35: c = Qt::magenta; break;
                case 36: c = Qt::cyan; break;
                case 37: c = Qt::lightGray; break;
                default: c = Qt::white; break;
                }
                curFmt.setForeground(c);
            } else if (v >= 40 && v <= 47) {
                QColor c;
                switch (v) {
                case 40: c = Qt::black; break;
                case 41: c = Qt::red; break;
                case 42: c = Qt::green; break;
                case 43: c = QColor(255, 165, 0); break;
                case 44: c = Qt::blue; break;
                case 45: c = Qt::magenta; break;
                case 46: c = Qt::cyan; break;
                case 47: c = Qt::lightGray; break;
                default: c = Qt::white; break;
                }
                curFmt.setBackground(c);
            }
        }
        idx = end;
    }
    if (idx < s.length()) {
        QString rest = s.mid(idx);
        m_output->moveCursor(QTextCursor::End);
        m_output->setCurrentCharFormat(curFmt);
        m_output->insertPlainText(rest);
    }

    if (m_autoscroll) {
        QScrollBar *sb = m_output->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

bool TConsole::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_input && ev->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Up) {
            if (m_history.isEmpty()) return true;
            if (m_historyIndex == -1) m_historyIndex = m_history.size() - 1;
            else m_historyIndex = qMax(0, m_historyIndex - 1);
            m_input->setText(m_history[m_historyIndex]);
            return true;
        } else if (ke->key() == Qt::Key_Down) {
            if (m_history.isEmpty()) return true;
            if (m_historyIndex == -1) return true;
            m_historyIndex = qMin(m_history.size() - 1, m_historyIndex + 1);
            if (m_historyIndex >= 0 && m_historyIndex < m_history.size())
                m_input->setText(m_history[m_historyIndex]);
            else
                m_input->clear();
            return true;
        } else if (ke->matches(QKeySequence::Copy)) {
            return QWidget::eventFilter(obj, ev);
        } else if (ke->key() == Qt::Key_C && (ke->modifiers() & Qt::ControlModifier)) {
            return false;
        } else if (ke->key() == Qt::Key_L && (ke->modifiers() & Qt::ControlModifier)) {
            clear();
            return true;
        } else if (ke->key() == Qt::Key_Tab) {
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

