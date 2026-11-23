#pragma once

#include "ITerminalBackend.h"

#include <QMutex>
#include <QThread>

#include <atomic>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class WindowsConPtyBackend final : public ITerminalBackend {
    Q_OBJECT
public:
    explicit WindowsConPtyBackend(QObject* parent = nullptr);
    ~WindowsConPtyBackend() override;

    bool start(const StartRequest& request, QString* errorMessage) override;
    void writeInput(const QByteArray& bytes) override;
    void resize(const QSize& grid) override;
    void requestStop() override;
    void shutdown(int timeoutMilliseconds) override;
    [[nodiscard]] bool isRunning() const override;

private:
    static void closeHandle(HANDLE& handle);
    static QString windowsError(const QString& context, DWORD error = GetLastError());
    void startReadThread();
    void readLoop();
    void finishFromWorker(int exitCode, bool normalExit);
    void closePseudoconsole();

    HPCON m_pseudoconsole = nullptr;
    HANDLE m_inputWrite = INVALID_HANDLE_VALUE;
    HANDLE m_outputRead = INVALID_HANDLE_VALUE;
    HANDLE m_process = INVALID_HANDLE_VALUE;
    HANDLE m_thread = INVALID_HANDLE_VALUE;
    QThread* m_readerThread = nullptr;
    QMutex m_handleMutex;
    std::atomic_bool m_running{false};
    std::atomic_bool m_stopRequested{false};
};
#else
class WindowsConPtyBackend final : public ITerminalBackend {
    Q_OBJECT
public:
    explicit WindowsConPtyBackend(QObject* parent = nullptr) : ITerminalBackend(parent) {}
    bool start(const StartRequest&, QString* errorMessage) override {
        if (errorMessage) *errorMessage = QStringLiteral("ConPTY متاح على Windows فقط.");
        return false;
    }
    void writeInput(const QByteArray&) override {}
    void resize(const QSize&) override {}
    void requestStop() override {}
    void shutdown(int) override {}
    [[nodiscard]] bool isRunning() const override { return false; }
};
#endif
