#pragma once

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTimer>

class ITerminalBackend;

/** GUI-thread-owned lifecycle controller for one System Terminal session. */
class TerminalSessionController final : public QObject {
    Q_OBJECT
public:
    enum class State : quint8 { Idle, Starting, Running, Stopping, Finished, Failed };
    Q_ENUM(State)

    struct Request final {
        QString program;
        QStringList arguments;
        QString workingDirectory;
        QSize initialGrid{80, 24};
    };

    explicit TerminalSessionController(QObject* parent = nullptr);
    ~TerminalSessionController() override;

    [[nodiscard]] State state() const;
    [[nodiscard]] bool isActive() const;
    bool start(const Request& request, QString* errorMessage = nullptr);

public slots:
    void sendInput(const QByteArray& bytes);
    void resizeGrid(const QSize& grid);
    void cancel();
    void shutdown(int timeoutMilliseconds = 1500);

signals:
    void stateChanged(TerminalSessionController::State state);
    void outputReady(const QByteArray& bytes);
    void terminalError(const QString& message);
    void finished(int exitCode, bool normalExit);

private slots:
    void handleBackendOutput(const QByteArray& bytes);
    void handleBackendError(const QString& message);
    void handleBackendFinished(int exitCode, bool normalExit);

private:
    void setState(State state);
    void deliverFinished(int exitCode, bool normalExit);

    ITerminalBackend* m_backend = nullptr;
    QTimer m_cancelEscalation;
    State m_state = State::Idle;
    QSize m_grid{80, 24};
    bool m_finishDelivered = false;
};

Q_DECLARE_METATYPE(TerminalSessionController::State)
