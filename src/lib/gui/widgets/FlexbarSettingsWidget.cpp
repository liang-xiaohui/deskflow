#include "FlexbarSettingsWidget.h"
#include "common/Settings.h"
#include "gui/core/FlexbarManager.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace deskflow::gui {

FlexbarSettingsWidget::FlexbarSettingsWidget(FlexbarManager *manager, QWidget *parent)
    : QWidget(parent),
      m_manager(manager),
      m_enabled(new QCheckBox(tr("Enable Flexbar"), this)),
      m_runtime(new QLineEdit(this)),
      m_agent(new QLineEdit(this)),
      m_data(new QLineEdit(this)),
      m_status(new QLabel(this)),
      m_check(new QPushButton(tr("Check connection"), this)),
      m_restart(new QPushButton(tr("Restart module"), this))
{
  setObjectName(QStringLiteral("flexbarSettings"));
  m_enabled->setObjectName(QStringLiteral("cbFlexbarEnabled"));
  m_runtime->setObjectName(QStringLiteral("lineFlexbarRuntime"));
  m_agent->setObjectName(QStringLiteral("lineFlexbarAgent"));
  m_data->setObjectName(QStringLiteral("lineFlexbarData"));
  m_status->setObjectName(QStringLiteral("lblFlexbarStatus"));
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(m_enabled);
#ifdef Q_OS_MACOS
  auto *role = new QLabel(tr("This Mac: USB device host · Legion: remote controls and tasks"), this);
#elif defined(Q_OS_WIN)
  auto *role = new QLabel(tr("This Windows PC: remote controls and tasks · Mac: USB device host"), this);
#else
  auto *role = new QLabel(tr("Flexbar integration currently requires macOS or Windows"), this);
#endif
  role->setWordWrap(true);
  layout->addWidget(role);
  auto *description = new QLabel(
      tr("Runs with Deskflow, including in the tray. Disabling or quitting stops this module, not keyboard sharing. "
         "Pairing and the last page are preserved."),
      this
  );
  description->setWordWrap(true);
  layout->addWidget(description);
#ifdef Q_OS_MACOS
  auto *dependency = new QLabel(
      tr("Requires FlexDesigner and the managed Mac host plugin. Enable starts FlexDesigner if needed; disabling does "
         "not quit it."),
      this
  );
  dependency->setWordWrap(true);
  layout->addWidget(dependency);
#endif
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  layout->addWidget(m_status);
  auto *actions = new QHBoxLayout;
  actions->addWidget(m_check);
  actions->addWidget(m_restart);
  actions->addStretch();
  layout->addLayout(actions);
  auto *advanced = new QGroupBox(tr("Advanced — installed Windows agent"), this);
  auto *fields = new QFormLayout(advanced);
  const auto addPath = [this, fields](const QString &label, QLineEdit *edit, bool directory) {
    auto *row = new QWidget(this);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addWidget(edit);
    auto *browse = new QPushButton(tr("Browse…"), row);
    rowLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, edit, directory] {
      const auto value = directory ? QFileDialog::getExistingDirectory(this, tr("Select data directory"), edit->text())
                                   : QFileDialog::getOpenFileName(this, tr("Select installed file"), edit->text());
      if (!value.isEmpty())
        edit->setText(value);
    });
    fields->addRow(label, row);
  };
  addPath(tr("Node runtime"), m_runtime, false);
  addPath(tr("Agent entry (agent.mjs)"), m_agent, false);
  addPath(tr("Private data directory"), m_data, true);
  m_runtime->setPlaceholderText(tr("Automatic"));
  m_agent->setPlaceholderText(tr("Bundled flexbar/agent.mjs"));
  m_data->setPlaceholderText(tr("Existing DotFlexbar/windows-agent data"));
  layout->addWidget(advanced);
#ifndef Q_OS_WIN
  advanced->hide();
#endif
  layout->addStretch();
  connect(m_enabled, &QCheckBox::toggled, this, [this] {
    updateControls();
    Q_EMIT edited();
  });
  for (auto *edit : {m_runtime, m_agent, m_data})
    connect(edit, &QLineEdit::textChanged, this, [this] {
      updateControls();
      Q_EMIT edited();
    });
  if (manager) {
    connect(manager, &FlexbarManager::statusChanged, this, &FlexbarSettingsWidget::updateControls);
    connect(m_check, &QPushButton::clicked, manager, &FlexbarManager::check);
    connect(m_restart, &QPushButton::clicked, manager, &FlexbarManager::restart);
  }
  load();
}

void FlexbarSettingsWidget::load(bool defaults)
{
  const auto value = [defaults](const QString &key) {
    return defaults ? Settings::defaultValue(key) : Settings::value(key);
  };
  m_enabled->setChecked(value(Settings::Flexbar::Enabled).toBool());
  m_runtime->setText(value(Settings::Flexbar::Runtime).toString());
  m_agent->setText(value(Settings::Flexbar::Agent).toString());
  m_data->setText(value(Settings::Flexbar::DataDirectory).toString());
  updateControls();
}

void FlexbarSettingsWidget::save()
{
  Settings::setValue(Settings::Flexbar::Runtime, m_runtime->text().trimmed());
  Settings::setValue(Settings::Flexbar::Agent, m_agent->text().trimmed());
  Settings::setValue(Settings::Flexbar::DataDirectory, m_data->text().trimmed());
  Settings::setValue(Settings::Flexbar::Enabled, m_enabled->isChecked());
}

bool FlexbarSettingsWidget::isModified(bool defaults) const
{
  const auto value = [defaults](const QString &key) {
    return defaults ? Settings::defaultValue(key) : Settings::value(key);
  };
  return m_enabled->isChecked() != value(Settings::Flexbar::Enabled).toBool() ||
         m_runtime->text().trimmed() != value(Settings::Flexbar::Runtime).toString() ||
         m_agent->text().trimmed() != value(Settings::Flexbar::Agent).toString() ||
         m_data->text().trimmed() != value(Settings::Flexbar::DataDirectory).toString();
}

void FlexbarSettingsWidget::updateControls()
{
  bool supported = false;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
  supported = true;
#endif
  m_enabled->setEnabled(supported && Settings::isWritable());
  for (auto *edit : {m_runtime, m_agent, m_data})
    edit->setReadOnly(!Settings::isWritable());
  m_check->setEnabled(m_manager && m_manager->enabled() && !isModified());
  m_restart->setEnabled(m_manager && m_manager->enabled() && !isModified());
  m_status->setText(m_manager ? m_manager->statusText() : tr("Status unavailable"));
}

} // namespace deskflow::gui
