#pragma once

#include "core/model.h"

#include <QMainWindow>
#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QSpinBox;

namespace striptool {

class ControlsWindow final : public QMainWindow {
  Q_OBJECT
public:
  explicit ControlsWindow(StripToolModel* model, QWidget* parent = nullptr);

  void reloadFromModel();
  void updateTitle();
  void refreshCurveMetadata(std::size_t curve);
  void setChannelMetadata(std::size_t curve, const ChannelMetadata& metadata);

signals:
  void modelChanged();
  void acquisitionConfigurationChanged();
  void openRequested();
  void saveRequested();
  void saveAsRequested();
  void showGraphRequested();

private:
  struct CurveRow {
    QLineEdit* name = nullptr;
    QPushButton* color = nullptr;
    QCheckBox* plotted = nullptr;
    QComboBox* scale = nullptr;
    QSpinBox* precision = nullptr;
    QLineEdit* minimum = nullptr;
    QLineEdit* maximum = nullptr;
    QPushButton* modify = nullptr;
    QPushButton* remove = nullptr;
    QLabel* status = nullptr;
    bool precisionEdited = false;
    bool minimumEdited = false;
    bool maximumEdited = false;
  };

  QWidget* createCurvePage();
  QWidget* createTimingPage();
  QWidget* createAppearancePage();
  void connectEnteredPv();
  void applyCurve(std::size_t index);
  void removeCurve(std::size_t index);
  void chooseColor(Rgba16& color, QPushButton* button);
  void updateColorButton(QPushButton* button, const Rgba16& color);

  StripToolModel* model_;
  QLineEdit* pvEntry_ = nullptr;
  std::array<CurveRow, kMaximumCurves> curveRows_;
  QSpinBox* timespan_ = nullptr;
  QSpinBox* sampleCount_ = nullptr;
  QDoubleSpinBox* sampleInterval_ = nullptr;
  QDoubleSpinBox* refreshInterval_ = nullptr;
  QComboBox* xGrid_ = nullptr;
  QComboBox* yGrid_ = nullptr;
  QCheckBox* coloredAxes_ = nullptr;
  QSpinBox* lineWidth_ = nullptr;
  QPushButton* foreground_ = nullptr;
  QPushButton* background_ = nullptr;
  QPushButton* gridColor_ = nullptr;
  bool loading_ = false;
};

}  // namespace striptool
