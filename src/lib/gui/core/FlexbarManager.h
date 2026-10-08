#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace deskflow::gui {

class FlexbarManager : public QObject
{
  Q_OBJECT

public:
  struct Options
  {
    enum class Transport
    {
      LocalBridge,
      ChildProcess
    };
    Transport transport = Transport::LocalBridge;
    QString socketPath;
    QString program;
    QStringList arguments;
    bool launchDesigner = false;
    bool operator==(const Options &) const = default;
  };

  explicit FlexbarManager(QObject *parent = nullptr);
  ~FlexbarManager() override;
  static Options configuredOptions();
  void applySettings();
  void configure(bool enabled, const Options &options);
  void restart();
  void check();
  void shutdown();
  QString statusText() const;
  bool enabled() const
  {
    return m_enabled;
  }
  QJsonObject status() const
  {
    return m_status;
  }

Q_SIGNALS:
  void statusChanged();

private:
  void start();
  void stop();
  void finished();
  void tick();
  void consume(const QByteArray &data);
  void send(const QString &type);
  void fail(const QString &message);
  void publish(const QString &message);
  bool attached() const;

  Options m_options;
  QLocalSocket m_socket;
  QProcess m_process;
  QTimer m_timer;
  QElapsedTimer m_lastStatus;
  QElapsedTimer m_stoppingSince;
  QByteArray m_buffer;
  QJsonObject m_status;
  QString m_message;
  QString m_failure;
  bool m_enabled = false;
  bool m_stopping = false;
  bool m_restartPending = false;
  bool m_shuttingDown = false;
  bool m_designerLaunched = false;
  int m_retryTicks = 0;
};

} // namespace deskflow::gui
