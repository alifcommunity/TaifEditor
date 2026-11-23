#pragma once

#include "ITerminalBackend.h"

#ifdef Q_OS_UNIX

#include <QMutex>

#include <atomic>
#include <thread>

class PosixPtyBackend final : public ITerminalBackend {
    Q_OBJECT
public:
    explicit PosixPtyBackend(QObject* parent = nullptr);
    ~PosixPtyBackend() override;

    bool start(const StartRequest& request, QString* errorMessage) override;
    void writeInput(const QByteArray& bytes) override;
    void resize(const QSize& grid) override;
    void requestStop() override;
    void shutdown(int timeoutMilliseconds) override;
    [[nodiscard]] bool isRunning() const override;

private:
    void startReader();
    void readLoop();
    void deliverFinished(int exitCode, bool normalExit);
    void closeMaster();

    int m_masterFd = -1;
    qint64 m_childPid = -1;
    std::thread m_reader;
    QMutex m_fdMutex;
    std::atomic_bool m_running{false};
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_finishedDelivered{false};
};

#endif
