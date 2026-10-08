#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace deskflow::gui {
class FlexbarManager;

class FlexbarSettingsWidget : public QWidget
{
  Q_OBJECT
public:
  explicit FlexbarSettingsWidget(FlexbarManager *manager, QWidget *parent = nullptr);
  void load(bool defaults = false);
  void save();
  bool isModified(bool defaults = false) const;
  void updateControls();

Q_SIGNALS:
  void edited();

private:
  FlexbarManager *m_manager;
  QCheckBox *m_enabled;
  QLineEdit *m_runtime;
  QLineEdit *m_agent;
  QLineEdit *m_data;
  QLabel *m_status;
  QPushButton *m_check;
  QPushButton *m_restart;
};
} // namespace deskflow::gui
