#pragma once

#include "services/channel_provider.h"

#include <QSet>
#include <QTimer>
#include <chrono>
#include <ctime>

namespace striptool {

class CpuUsageProvider final : public ChannelProvider {
public:
  explicit CpuUsageProvider(QObject* parent = nullptr);

  ChannelId connectChannel(const QString& name) override;
  void disconnectChannel(ChannelId id) override;
  void setSampleInterval(std::chrono::milliseconds interval);
  std::chrono::milliseconds sampleInterval() const;
  void sampleNow();

private:
  ChannelId nextId_ = 1;
  QSet<ChannelId> active_;
  QTimer timer_;
#ifdef _WIN32
  double lastCpuSeconds_ = 0.0;
#else
  std::clock_t lastCpu_ = std::clock();
#endif
  std::chrono::steady_clock::time_point lastWall_ = std::chrono::steady_clock::now();
};

}  // namespace striptool
