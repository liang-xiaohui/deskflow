#include "FlexbarManager.h"
#include "common/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace deskflow::gui {

FlexbarManager::FlexbarManager(QObject *parent) : QObject(parent)
{
  m_timer.setInterval(1000);
  connect(&m_timer, &QTimer::timeout, this, &FlexbarManager::tick);
  connect(&m_socket, &QLocalSocket::connected, this, [this] {
    m_lastStatus.start();
    send(m_stopping ? QStringLiteral("stop") : QStringLiteral("start"));
  });
  connect(&m_socket, &QLocalSocket::readyRead, this, [this] { consume(m_socket.readAll()); });
  connect(&m_socket, &QLocalSocket::disconnected, this, &FlexbarManager::finished);
  connect(&m_socket, &QLocalSocket::errorOccurred, this, [this] {
    if (!m_stopping) {
      m_status = {};
      publish(tr("Waiting for FlexDesigner and the managed Flexbar plugin"));
      m_retryTicks = 5;
    }
  });
  connect(&m_process, &QProcess::started, this, [this] {
    m_lastStatus.start();
    if (m_stopping) {
      send(QStringLiteral("stop"));
      m_process.closeWriteChannel();
    }
  });
  connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { consume(m_process.readAllStandardOutput()); });
  connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_process.readAllStandardError(); });
  connect(&m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) { finished(); });
  connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart)
      fail(tr("Cannot start the Flexbar agent; check the runtime and package paths"));
  });
  publish(tr("Disabled"));
}

FlexbarManager::~FlexbarManager()
{
  shutdown();
}

FlexbarManager::Options FlexbarManager::configuredOptions()
{
  Options options;
#ifdef Q_OS_WIN
  options.transport = Options::Transport::ChildProcess;
  options.program = Settings::value(Settings::Flexbar::Runtime).toString();
  if (options.program.isEmpty())
    options.program = QStandardPaths::findExecutable(QStringLiteral("node"));
  if (options.program.isEmpty())
    options.program = qEnvironmentVariable("ProgramFiles") + QStringLiteral("/nodejs/node.exe");
  auto entry = Settings::value(Settings::Flexbar::Agent).toString();
  if (entry.isEmpty())
    entry = QCoreApplication::applicationDirPath() + QStringLiteral("/flexbar/agent.mjs");
  auto data = Settings::value(Settings::Flexbar::DataDirectory).toString();
  if (data.isEmpty())
    data = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/DotFlexbar/windows-agent");
  options.arguments = {entry, QStringLiteral("start"), QStringLiteral("--managed"), QStringLiteral("--data"), data};
#else
  options.socketPath = QDir::homePath() + QStringLiteral("/.dot-flexbar/deskflow.sock");
  options.launchDesigner = true;
#endif
  return options;
}

void FlexbarManager::applySettings()
{
  configure(Settings::value(Settings::Flexbar::Enabled).toBool(), configuredOptions());
}

void FlexbarManager::configure(bool enabled, const Options &options)
{
  if (m_shuttingDown || (m_enabled == enabled && m_options == options))
    return;
  m_enabled = enabled;
  m_failure.clear();
  m_designerLaunched = false;
  m_options = options;
  m_restartPending = enabled;
  m_retryTicks = 0;
  if (attached() || m_stopping)
    stop();
  else if (enabled)
    start();
  else
    publish(tr("Disabled"));
}

bool FlexbarManager::attached() const
{
  return m_socket.state() != QLocalSocket::UnconnectedState || m_process.state() != QProcess::NotRunning;
}

void FlexbarManager::start()
{
  if (!m_enabled || m_shuttingDown || attached() || m_stopping)
    return;
  m_restartPending = false;
  m_buffer.clear();
  m_failure.clear();
  m_status = {};
  m_lastStatus.start();
  m_timer.start();
  publish(tr("Starting"));
  if (m_options.transport == Options::Transport::ChildProcess) {
    const auto dataIndex = m_options.arguments.indexOf(QStringLiteral("--data"));
    if (!QFileInfo(m_options.program).isAbsolute() || !QFileInfo(m_options.program).isExecutable() ||
        m_options.arguments.isEmpty() || !QFileInfo(m_options.arguments.first()).isAbsolute() ||
        !QFileInfo(m_options.arguments.first()).isFile() ||
        (dataIndex >= 0 && (dataIndex + 1 >= m_options.arguments.size() ||
                            !QDir::isAbsolutePath(m_options.arguments.at(dataIndex + 1))))) {
      fail(tr("Flexbar agent is not installed; select its runtime and package in Advanced"));
      return;
    }
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start(m_options.program, m_options.arguments);
    return;
  }
#ifdef Q_OS_MACOS
  if (m_options.launchDesigner && !m_designerLaunched && !QFileInfo::exists(m_options.socketPath)) {
    m_designerLaunched = true;
    QProcess::startDetached(
        QStringLiteral("/usr/bin/open"),
        {QStringLiteral("-g"), QStringLiteral("-a"), QStringLiteral("/Applications/FlexDesigner.app")}
    );
  }
#endif
#ifdef Q_OS_UNIX
  struct stat directoryInfo{}, socketInfo{};
  const auto directory = QFile::encodeName(QFileInfo(m_options.socketPath).absolutePath());
  const auto socket = QFile::encodeName(m_options.socketPath);
  if (::lstat(directory.constData(), &directoryInfo) == 0 &&
      (!S_ISDIR(directoryInfo.st_mode) || directoryInfo.st_uid != ::getuid() ||
       (directoryInfo.st_mode & 0777) != 0700)) {
    fail(tr("Unsafe Flexbar local socket permissions"));
    return;
  }
  if (::lstat(socket.constData(), &socketInfo) == 0 &&
      (!S_ISSOCK(socketInfo.st_mode) || socketInfo.st_uid != ::getuid() || (socketInfo.st_mode & 0777) != 0600)) {
    fail(tr("Unsafe Flexbar local socket permissions"));
    return;
  }
#endif
  m_socket.connectToServer(m_options.socketPath);
}

void FlexbarManager::send(const QString &type)
{
  const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("type"), type}}).toJson(QJsonDocument::Compact) + '\n';
  if (m_process.state() == QProcess::Running)
    m_process.write(bytes);
  else if (m_socket.state() == QLocalSocket::ConnectedState)
    m_socket.write(bytes);
}

void FlexbarManager::stop()
{
  if (m_stopping)
    return;
  m_stopping = true;
  m_stoppingSince.start();
  m_timer.start();
  publish(tr("Stopping"));
  send(QStringLiteral("stop"));
  if (m_process.state() == QProcess::Running)
    m_process.closeWriteChannel();
  if (!attached())
    finished();
}

void FlexbarManager::finished()
{
  const bool wasStopping = m_stopping;
  m_stopping = false;
  m_buffer.clear();
  m_status = {};
  if (!m_failure.isEmpty()) {
    m_timer.stop();
    publish(m_failure);
    return;
  }
  if (m_restartPending && m_enabled && !m_shuttingDown) {
    QTimer::singleShot(0, this, &FlexbarManager::start);
    return;
  }
  if (!m_enabled || m_shuttingDown) {
    m_timer.stop();
    publish(tr("Disabled"));
  } else if (m_options.transport == Options::Transport::LocalBridge && !wasStopping) {
    m_retryTicks = 5;
    publish(tr("Waiting for FlexDesigner and the managed Flexbar plugin"));
  } else {
    m_timer.stop();
    publish(tr("Agent stopped; check configuration or an already running manual instance, then restart"));
  }
}

void FlexbarManager::consume(const QByteArray &data)
{
  m_buffer += data;
  if (m_buffer.size() > 16384) {
    fail(tr("Invalid Flexbar management response"));
    return;
  }
  while (m_buffer.contains('\n')) {
    const auto boundary = m_buffer.indexOf('\n');
    const auto line = m_buffer.left(boundary);
    m_buffer.remove(0, boundary + 1);
    const auto document = QJsonDocument::fromJson(line);
    const auto status = document.object();
    const auto phase = status.value(QStringLiteral("phase")).toString();
    const auto owner = status.value(QStringLiteral("owner")).toString();
    static const QRegularExpression code(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    const auto issue = status.value(QStringLiteral("lastIssue"));
    const auto pending = status.value(QStringLiteral("pending"));
    if (line.size() > 4096 || !document.isObject() || status.value("type") != "status" ||
        status.value("version") != 1 || !status.value("running").isBool() || !status.value("connected").isBool() ||
        !status.value("pageActive").isBool() || !status.value("actionsGranted").isBool() || !pending.isDouble() ||
        pending.toDouble() < 0 || pending.toDouble() > 1000000 || pending.toDouble() != pending.toInt() ||
        !code.match(phase).hasMatch() || !QStringList{"mac", "windows", "unknown"}.contains(owner) ||
        (!issue.isNull() && (!issue.isString() || !code.match(issue.toString()).hasMatch()))) {
      fail(tr("Invalid Flexbar management response"));
      return;
    }
    m_status = status;
    m_lastStatus.restart();
    if (m_stopping && !status.value("running").toBool()) {
      if (m_socket.state() == QLocalSocket::ConnectedState)
        m_socket.disconnectFromServer();
      continue;
    }
    if (!m_stopping) {
      if (!issue.isNull())
        publish(tr("Flexbar needs attention: %1").arg(issue.toString()));
      else if (!status.value("running").toBool())
        publish(tr("Flexbar is not running"));
      else
        publish(tr("Running · %1 · %2 · Target: %3")
                    .arg(
                        status.value("pageActive").toBool() ? tr("Page ready") : tr("Enter the Flexbar workspace"),
                        status.value("connected").toBool() ? tr("Peer connected") : tr("Peer offline"), owner
                    ));
    }
  }
}

void FlexbarManager::tick()
{
  if (m_stopping) {
    if (m_stoppingSince.elapsed() > 20000) {
      if (m_process.state() != QProcess::NotRunning)
        m_process.kill();
      if (m_socket.state() != QLocalSocket::UnconnectedState)
        m_socket.abort();
      if (!attached())
        finished();
    }
    return;
  }
  if (!m_enabled)
    return;
  if (!attached()) {
    if (m_options.transport == Options::Transport::LocalBridge && --m_retryTicks <= 0)
      start();
    return;
  }
  if (m_lastStatus.isValid() && m_lastStatus.elapsed() > 6000) {
    fail(tr("Flexbar stopped responding; stopping controls"));
    return;
  }
  send(QStringLiteral("status"));
}

void FlexbarManager::fail(const QString &message)
{
  m_failure = message;
  m_restartPending = false;
  m_timer.stop();
  m_status = {};
  if (attached())
    stop();
  publish(message);
}

void FlexbarManager::publish(const QString &message)
{
  m_message = message;
  Q_EMIT statusChanged();
}

QString FlexbarManager::statusText() const
{
  return m_message;
}

void FlexbarManager::restart()
{
  if (!m_enabled || m_shuttingDown)
    return;
  m_restartPending = true;
  m_failure.clear();
  if (attached() || m_stopping)
    stop();
  else
    start();
}

void FlexbarManager::check()
{
  if (attached())
    send(QStringLiteral("status"));
  else
    restart();
}

void FlexbarManager::shutdown()
{
  if (m_shuttingDown)
    return;
  m_shuttingDown = true;
  m_enabled = false;
  m_restartPending = false;
  m_timer.stop();
  if (attached()) {
    send(QStringLiteral("stop"));
    if (m_socket.state() == QLocalSocket::ConnectedState) {
      m_socket.flush();
      m_socket.disconnectFromServer();
      if (m_socket.state() != QLocalSocket::UnconnectedState)
        m_socket.waitForDisconnected(500);
    }
    if (m_process.state() != QProcess::NotRunning) {
      m_process.closeWriteChannel();
      if (!m_process.waitForFinished(20000)) {
        m_process.kill();
        m_process.waitForFinished(1000);
      }
    }
  }
  m_socket.abort();
}

} // namespace deskflow::gui
