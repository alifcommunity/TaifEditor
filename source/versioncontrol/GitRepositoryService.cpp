#include "GitRepositoryService.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

QString cleanRelative(QString value)
{
    value = QDir::cleanPath(value).replace(QLatin1Char('\\'), QLatin1Char('/'));
    return value == QStringLiteral(".") ? QString() : value;
}

VersionControlState stateFor(const QChar index, const QChar workTree)
{
    const QString pair = QString(index) + workTree;
    if (pair == QStringLiteral("??")) return VersionControlState::Untracked;
    if (pair == QStringLiteral("!!")) return VersionControlState::Ignored;
    if (index == QLatin1Char('U') || workTree == QLatin1Char('U')) return VersionControlState::Conflicted;
    if (index == QLatin1Char('R') || workTree == QLatin1Char('R')
        || index == QLatin1Char('C') || workTree == QLatin1Char('C')) return VersionControlState::Renamed;
    if (index == QLatin1Char('D') || workTree == QLatin1Char('D')) return VersionControlState::Deleted;
    if (index == QLatin1Char('A')) return VersionControlState::Added;
    if (index == QLatin1Char('M') || workTree == QLatin1Char('M')
        || index == QLatin1Char('T') || workTree == QLatin1Char('T')) return VersionControlState::Modified;
    return VersionControlState::Clean;
}

} // namespace

GitRepositoryService::GitRepositoryService(QObject* const parent)
    : QObject(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(160);
    connect(&m_refreshTimer, &QTimer::timeout, this, &GitRepositoryService::startRefresh);
    connect(&m_queryProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &GitRepositoryService::handleQueryFinished);
    connect(&m_commandProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &GitRepositoryService::handleCommandFinished);
    connect(&m_queryProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_queryProcess.state() == QProcess::NotRunning && !m_snapshot.projectRoot.isEmpty()) {
            m_snapshot.gitAvailable = false;
            m_snapshot.repository = false;
            m_snapshot.lastError = QStringLiteral("Git غير متاح أو تعذر بدء عملية الفحص.");
            publishSnapshot();
        }
    });
}

GitRepositoryService::~GitRepositoryService()
{
    m_shuttingDown = true;
    m_refreshPending = false;
    m_refreshTimer.stop();
    stopProcess(m_queryProcess);
    stopProcess(m_commandProcess);
}

void GitRepositoryService::setProjectRoot(const QString& rootPath)
{
    const QString root = normalizedRoot(rootPath);
    if (m_snapshot.projectRoot == root) return;
    ++m_generation;
    stopProcess(m_queryProcess);
    stopProcess(m_commandProcess);
    m_commandBusy = false;
    m_refreshPending = false;
    m_snapshot = {};
    m_snapshot.projectRoot = root;
    publishSnapshot();
    refresh(true);
}

const GitRepositorySnapshot& GitRepositoryService::snapshot() const { return m_snapshot; }

VersionControlState GitRepositoryService::statusForRelativePath(const QString& relativePath) const
{
    const QString normalized = cleanRelative(relativePath);
    for (const GitFileStatus& entry : m_snapshot.files) {
        if (entry.relativePath == normalized) return entry.state;
    }
    return m_snapshot.repository ? VersionControlState::Clean : VersionControlState::Unavailable;
}

QString GitRepositoryService::statusDetailForRelativePath(const QString& relativePath) const
{
    const QString normalized = cleanRelative(relativePath);
    for (const GitFileStatus& entry : m_snapshot.files) {
        if (entry.relativePath == normalized) return QString(entry.indexStatus) + entry.workTreeStatus;
    }
    return {};
}

void GitRepositoryService::refresh(const bool immediate)
{
    if (m_snapshot.projectRoot.isEmpty()) return;
    if (immediate) {
        m_refreshTimer.stop();
        startRefresh();
    } else {
        m_refreshTimer.start();
    }
}

void GitRepositoryService::cancelActiveOperation()
{
    if (m_commandBusy) stopProcess(m_commandProcess);
}

void GitRepositoryService::stage(const QStringList& relativePaths)
{
    bool valid = false;
    const QStringList paths = validatedPaths(relativePaths, &valid);
    if (!valid || paths.isEmpty()) return finishWithValidationError(GitOperation::Stage, QStringLiteral("اختر ملفات صالحة لإضافتها إلى منطقة التجهيز."));
    QStringList arguments {QStringLiteral("add"), QStringLiteral("--")};
    arguments.append(paths);
    startCommand(GitOperation::Stage, arguments, QStringLiteral("تم تجهيز الملفات."));
}

void GitRepositoryService::unstage(const QStringList& relativePaths)
{
    bool valid = false;
    const QStringList paths = validatedPaths(relativePaths, &valid);
    if (!valid || paths.isEmpty()) return finishWithValidationError(GitOperation::Unstage, QStringLiteral("اختر ملفات مجهزة صالحة."));
    QStringList arguments {QStringLiteral("restore"), QStringLiteral("--staged"), QStringLiteral("--")};
    arguments.append(paths);
    startCommand(GitOperation::Unstage, arguments, QStringLiteral("تمت إزالة الملفات من منطقة التجهيز."));
}

void GitRepositoryService::discard(const QStringList& relativePaths)
{
    bool valid = false;
    const QStringList paths = validatedPaths(relativePaths, &valid);
    if (!valid || paths.isEmpty()) return finishWithValidationError(GitOperation::Discard, QStringLiteral("اختر ملفات متتبعة صالحة."));
    QStringList arguments {QStringLiteral("restore"), QStringLiteral("--worktree"), QStringLiteral("--")};
    arguments.append(paths);
    startCommand(GitOperation::Discard, arguments, QStringLiteral("تم تجاهل التعديلات المحلية."));
}

void GitRepositoryService::commit(const QString& message)
{
    const QString normalized = message.trimmed();
    if (normalized.isEmpty() || normalized.size() > 4000) {
        return finishWithValidationError(GitOperation::Commit, QStringLiteral("اكتب رسالة إيداع صالحة."));
    }
    if (m_snapshot.stagedCount() == 0) {
        return finishWithValidationError(GitOperation::Commit, QStringLiteral("جهّز ملفاً واحداً على الأقل قبل الإيداع."));
    }
    startCommand(GitOperation::Commit, {QStringLiteral("commit"), QStringLiteral("-m"), normalized},
                 QStringLiteral("تم إنشاء الإيداع."));
}

void GitRepositoryService::fetch(const QString& remote)
{
    const QString target = remote.trimmed();
    if (!isValidRefName(target)) return finishWithValidationError(GitOperation::Fetch, QStringLiteral("المستودع البعيد غير صالح."));
    startCommand(GitOperation::Fetch, {QStringLiteral("fetch"), target}, QStringLiteral("اكتمل الجلب من المستودع البعيد."));
}

void GitRepositoryService::pull()
{
    if (m_snapshot.upstream.isEmpty()) return finishWithValidationError(GitOperation::Pull, QStringLiteral("لا يوجد فرع تتبع للسحب منه."));
    startCommand(GitOperation::Pull, {QStringLiteral("pull"), QStringLiteral("--ff-only")},
                 QStringLiteral("اكتمل السحب السريع."));
}

void GitRepositoryService::push()
{
    if (m_snapshot.upstream.isEmpty()) return finishWithValidationError(GitOperation::Push, QStringLiteral("اضبط فرع تتبع قبل الدفع."));
    startCommand(GitOperation::Push, {QStringLiteral("push")}, QStringLiteral("اكتمل الدفع إلى المستودع البعيد."));
}

void GitRepositoryService::switchBranch(const QString& branch)
{
    if (!isValidRefName(branch)) return finishWithValidationError(GitOperation::SwitchBranch, QStringLiteral("اسم الفرع غير صالح."));
    startCommand(GitOperation::SwitchBranch, {QStringLiteral("switch"), branch.trimmed()}, QStringLiteral("تم تبديل الفرع."));
}

void GitRepositoryService::createBranch(const QString& branch)
{
    if (!isValidRefName(branch)) return finishWithValidationError(GitOperation::CreateBranch, QStringLiteral("اسم الفرع غير صالح."));
    startCommand(GitOperation::CreateBranch, {QStringLiteral("switch"), QStringLiteral("-c"), branch.trimmed()},
                 QStringLiteral("تم إنشاء الفرع وتبديله."));
}

void GitRepositoryService::requestDiff(const QString& relativePath, const bool staged)
{
    if (!relativePath.isEmpty() && !isValidRelativePath(relativePath)) {
        emit diffReady({}, QStringLiteral("مسار الفرق غير صالح."));
        return;
    }
    QStringList args {QStringLiteral("diff"), QStringLiteral("--no-ext-diff"), QStringLiteral("--unified=3")};
    if (staged) args << QStringLiteral("--staged");
    if (!relativePath.isEmpty()) args << QStringLiteral("--") << relativePath;
    startCommand(GitOperation::Diff, args, QString());
}

void GitRepositoryService::requestHistory(const int limit)
{
    const int boundedLimit = qBound(1, limit, 100);
    startCommand(GitOperation::History, {QStringLiteral("log"),
        QStringLiteral("--max-count=%1").arg(boundedLimit),
        QStringLiteral("--date=iso-strict"),
        QStringLiteral("--pretty=format:%H%x1f%an%x1f%aI%x1f%D%x1f%s%x1e")}, QString());
}

bool GitRepositoryService::isValidRelativePath(const QString& path)
{
    const QString clean = cleanRelative(path);
    return !clean.isEmpty() && !clean.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(clean)
        && !clean.contains(QChar::Null);
}

bool GitRepositoryService::isValidRefName(const QString& ref)
{
    const QString value = ref.trimmed();
    if (value.isEmpty() || value.size() > 255 || value.startsWith(QLatin1Char('-'))
        || value.contains(QStringLiteral("..")) || value.endsWith(QLatin1Char('.'))
        || value.contains(QStringLiteral("@{"))) return false;
    static const QRegularExpression invalid(QStringLiteral(R"([ ~^:?*\[\\\x00-\x1f\x7f])"));
    return !invalid.match(value).hasMatch();
}

void GitRepositoryService::startRefresh()
{
    if (m_shuttingDown || m_snapshot.projectRoot.isEmpty()) return;
    if (m_queryProcess.state() != QProcess::NotRunning) {
        m_refreshPending = true;
        return;
    }
    m_snapshot.busy = true;
    m_snapshot.lastError.clear();
    publishSnapshot();
    m_queryStage = QueryStage::Status;
    m_queryProcess.setWorkingDirectory(m_snapshot.projectRoot);
    m_queryProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_queryProcess.start(QStringLiteral("git"), {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
        QStringLiteral("-z"), QStringLiteral("-b"), QStringLiteral("--ignored=matching"), QStringLiteral("--untracked-files=all")});
}

void GitRepositoryService::handleQueryFinished(const int exitCode, const QProcess::ExitStatus exitStatus)
{
    if (m_shuttingDown) return;
    const QByteArray output = m_queryProcess.readAllStandardOutput();
    const QByteArray error = m_queryProcess.readAllStandardError();
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        m_snapshot.busy = false;
        m_snapshot.gitAvailable = !m_snapshot.projectRoot.isEmpty();
        m_snapshot.repository = false;
        m_snapshot.files.clear();
        m_snapshot.lastError = QString::fromLocal8Bit(error).trimmed();
        if (m_snapshot.lastError.isEmpty()) m_snapshot.lastError = QStringLiteral("المجلد ليس مستودع Git أو Git غير متاح.");
        publishSnapshot();
        if (m_refreshPending) { m_refreshPending = false; refresh(true); }
        return;
    }
    if (m_queryStage == QueryStage::Status) {
        parseStatus(output);
        if (m_refreshPending) { m_refreshPending = false; startRefresh(); return; }
        m_snapshot.gitAvailable = true;
        m_snapshot.repository = true;
        m_queryStage = QueryStage::Remotes;
        m_queryProcess.start(QStringLiteral("git"), {QStringLiteral("remote")});
        return;
    }
    parseRemotes(output);
    m_snapshot.busy = false;
    publishSnapshot();
    if (m_refreshPending) { m_refreshPending = false; refresh(true); }
}

void GitRepositoryService::handleCommandFinished(const int exitCode, const QProcess::ExitStatus exitStatus)
{
    if (m_shuttingDown) return;
    const QString output = QString::fromLocal8Bit(m_commandProcess.readAllStandardOutput());
    const QString error = QString::fromLocal8Bit(m_commandProcess.readAllStandardError());
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
    const GitOperation operation = m_activeOperation;
    m_commandBusy = false;
    if (operation == GitOperation::Diff) {
        emit diffReady(success ? output.left(300000) : QString(), success ? QString() : error);
    } else if (operation == GitOperation::History) {
        QVector<GitHistoryEntry> entries;
        if (success) {
            for (const QByteArray& record : output.toUtf8().split('\x1e')) {
                const QList<QByteArray> parts = record.split('\x1f');
                if (parts.size() < 5 || parts.at(0).isEmpty()) continue;
                GitHistoryEntry entry;
                entry.hash = QString::fromUtf8(parts.at(0)); entry.author = QString::fromUtf8(parts.at(1));
                entry.date = QDateTime::fromString(QString::fromUtf8(parts.at(2)), Qt::ISODate);
                entry.decorations = QString::fromUtf8(parts.at(3)); entry.subject = QString::fromUtf8(parts.at(4)).trimmed();
                entries.push_back(entry);
            }
        }
        emit historyReady(entries, success ? QString() : error);
    }
    emit operationFinished({operation, success, output.left(300000), error.left(300000),
                            success ? m_activeMessage : QStringLiteral("تعذرت عملية Git المطلوبة.")});
    if (operation != GitOperation::Diff && operation != GitOperation::History) refresh(true);
}

void GitRepositoryService::startCommand(const GitOperation operation, const QStringList& arguments, const QString& userMessage)
{
    if (!m_snapshot.repository) return finishWithValidationError(operation, QStringLiteral("افتح مستودع Git صالحاً أولاً."));
    if (m_commandBusy) return finishWithValidationError(operation, QStringLiteral("هناك عملية Git قيد التنفيذ."));
    m_commandBusy = true; m_activeOperation = operation; m_activeMessage = userMessage;
    m_commandProcess.setWorkingDirectory(m_snapshot.projectRoot);
    m_commandProcess.setProcessChannelMode(QProcess::SeparateChannels);
    emit operationStarted(operation);
    m_commandProcess.start(QStringLiteral("git"), arguments);
}

void GitRepositoryService::finishWithValidationError(const GitOperation operation, const QString& message)
{
    emit operationFinished({operation, false, {}, {}, message});
}

void GitRepositoryService::publishSnapshot() { emit snapshotChanged(m_snapshot); }

void GitRepositoryService::parseStatus(const QByteArray& output)
{
    m_snapshot.files.clear(); m_snapshot.branch.clear(); m_snapshot.upstream.clear(); m_snapshot.ahead = 0; m_snapshot.behind = 0;
    const QList<QByteArray> records = output.split('\0');
    for (int index = 0; index < records.size(); ++index) {
        const QByteArray& record = records.at(index);
        if (record.startsWith("## ")) {
            const QString header = QString::fromUtf8(record.mid(3));
            const int upstreamSeparator = header.indexOf(QStringLiteral("..."));
            m_snapshot.branch = (upstreamSeparator < 0 ? header : header.left(upstreamSeparator)).section(QLatin1Char(' '), 0, 0);
            if (upstreamSeparator >= 0) {
                const QString tail = header.mid(upstreamSeparator + 3);
                m_snapshot.upstream = tail.section(QLatin1Char(' '), 0, 0);
                static const QRegularExpression ahead(QStringLiteral(R"(ahead (\d+))"));
                static const QRegularExpression behind(QStringLiteral(R"(behind (\d+))"));
                const auto a = ahead.match(header); const auto b = behind.match(header);
                if (a.hasMatch()) m_snapshot.ahead = a.captured(1).toInt();
                if (b.hasMatch()) m_snapshot.behind = b.captured(1).toInt();
            }
            continue;
        }
        if (record.size() < 4) continue;
        GitFileStatus entry;
        entry.indexStatus = QChar::fromLatin1(record.at(0)); entry.workTreeStatus = QChar::fromLatin1(record.at(1));
        entry.relativePath = cleanRelative(QString::fromUtf8(record.mid(3)));
        entry.state = stateFor(entry.indexStatus, entry.workTreeStatus);
        if ((entry.indexStatus == QLatin1Char('R') || entry.indexStatus == QLatin1Char('C')) && index + 1 < records.size())
            entry.originalRelativePath = cleanRelative(QString::fromUtf8(records.at(++index)));
        if (!entry.relativePath.isEmpty()) m_snapshot.files.push_back(entry);
    }
}

void GitRepositoryService::parseRemotes(const QByteArray& output)
{
    m_snapshot.remotes = QString::fromLocal8Bit(output).split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
}

QString GitRepositoryService::normalizedRoot(const QString& path) const
{
    const QFileInfo info(path); const QString canonical = info.canonicalFilePath();
    return path.isEmpty() ? QString() : QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QStringList GitRepositoryService::validatedPaths(const QStringList& paths, bool* const valid) const
{
    QStringList result; *valid = !paths.isEmpty();
    for (const QString& path : paths) { if (!isValidRelativePath(path)) { *valid = false; return {}; } result << cleanRelative(path); }
    result.removeDuplicates(); return result;
}

void GitRepositoryService::stopProcess(QProcess& process)
{
    if (process.state() == QProcess::NotRunning) return;
    process.terminate();
    if (!process.waitForFinished(1000)) {
        process.kill();
        process.waitForFinished(1000);
    }
}
