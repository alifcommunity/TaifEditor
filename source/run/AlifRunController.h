#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

/**
 * Owns one non-interactive Alif compiler/runtime process.
 *
 * The controller is GUI-thread owned. QProcess notifications are asynchronous,
 * while shutdown is the only bounded synchronous operation and is reserved for
 * application teardown.
 */
class AlifRunController final : public QObject
{
    Q_OBJECT

public:
    enum class State : quint8 {
        Idle,
        Starting,
        Running,
        Stopping,
        Finished,
        Failed
    };
    Q_ENUM(State)

    struct Request final {
        QString program;
        QStringList arguments;
        QString workingDirectory;
        QString displayName;
    };

    explicit AlifRunController(QObject* parent = nullptr);
    ~AlifRunController() override;

    [[nodiscard]] State state() const;
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool start(const Request& request, QString* errorMessage = nullptr);

public slots:
    void cancel();
    void sendInput(const QString& text);
    void shutdown(int timeoutMilliseconds = 1500);

signals:
    void stateChanged(AlifRunController::State state);
    void standardOutput(const QString& text);
    void standardError(const QString& text);
    void launchFailed(const QString& message);
    void finished(int exitCode, QProcess::ExitStatus exitStatus);

private slots:
    void handleStarted();
    void handleStandardOutput();
    void handleStandardError();
    void handleError(QProcess::ProcessError error);
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    [[nodiscard]] bool validateRequest(const Request& request, QString* errorMessage) const;
    void flushPendingOutput();
    void setState(State state);
    void completeFailure(const QString& message);
    [[nodiscard]] static QString decodeProcessBytes(const QByteArray& bytes);

    QProcess m_process;
    QTimer m_cancelEscalationTimer;
    State m_state = State::Idle;
    bool m_finishDelivered = false;
    bool m_shuttingDown = false;
};

Q_DECLARE_METATYPE(AlifRunController::State)
