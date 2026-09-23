#pragma once
#include "core/model.h"
#include <QHash>
#include <QMainWindow>
#include <QString>
#include <array>
#include <memory>
class QMenu;
namespace striptool {
class AcquisitionManager;
class ChannelAccessProvider;
class ControlsWindow;
class CpuUsageProvider;
class HistoryProvider;
class PlotWidget;
class MainWindow final : public QMainWindow {
public:
  explicit MainWindow(QWidget* parent = nullptr);
  explicit MainWindow(StripToolModel model, QWidget* parent = nullptr);
  ~MainWindow() override;

  const StripToolModel& model() const { return model_; }
  PlotWidget* plotWidget() const { return plotWidget_; }
  ControlsWindow* controlsWindow() const { return controlsWindow_.get(); }
  void startAcquisition();
  void stopAcquisition();
  void restartAcquisition();
  void showControls();
  bool openConfiguration(const QString& path, QString* error = nullptr);
  bool saveConfiguration(const QString& path, QString* error = nullptr);
  bool exportData(const QString& path, bool csv, QString* error = nullptr) const;
  bool saveSnapshot(const QString& path, QString* error = nullptr) const;

private:
  void applyModel();
  void updateRecentFiles(const QString& path = {});
  void requestHistory();

  StripToolModel model_;
  PlotWidget* plotWidget_ = nullptr;
  QMenu* recentMenu_ = nullptr;
  std::unique_ptr<ControlsWindow> controlsWindow_;
  std::unique_ptr<ChannelAccessProvider> channelAccess_;
  std::unique_ptr<CpuUsageProvider> cpuUsage_;
  std::unique_ptr<HistoryProvider> historyProvider_;
  std::unique_ptr<AcquisitionManager> channelAcquisition_;
  std::unique_ptr<AcquisitionManager> cpuAcquisition_;
  std::array<quint64, kMaximumCurves> channelIds_{};
  std::array<bool, kMaximumCurves> localChannels_{};
  std::array<std::string, kMaximumCurves> acquiredNames_{};
  QHash<quint64, std::size_t> historyRequests_;
  bool acquisitionRunning_ = false;
};
}
