#pragma once

#include <QObject>
#include <QSize>

class ITerminalBackend : public QObject {
    Q_OBJECT
public:
    struct StartRequest final {
        QString program;
        QStringList arguments;
        QString workingDirectory;
        QSize initialGrid{80, 24};
    };

    explicit ITerminalBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~ITerminalBackend() override = default;

    virtual bool start(const StartRequest& request, QString* errorMessage) = 0;
    virtual void writeInput(const QByteArray& bytes) = 0;
    virtual void resize(const QSize& grid) = 0;
    virtual void requestStop() = 0;
    virtual void shutdown(int timeoutMilliseconds) = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;

signals:
    void outputReady(const QByteArray& bytes);
    void backendError(const QString& message);
    void finished(int exitCode, bool normalExit);
};
