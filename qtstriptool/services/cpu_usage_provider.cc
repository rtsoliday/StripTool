#include "services/cpu_usage_provider.h"

#include <QTimer>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif

namespace striptool {
#ifdef _WIN32
namespace {
double processCpuSeconds() {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    return 0.0;
  const auto seconds = [](FILETIME time) {
    return (double(time.dwHighDateTime) * 4294967296.0 + time.dwLowDateTime) / 1e7;
  };
  return seconds(kernel) + seconds(user);
}
}
#endif

CpuUsageProvider::CpuUsageProvider(QObject* parent) : ChannelProvider(parent) {
#ifdef _WIN32
  // MSVC's clock() measures elapsed wall time, so an idle process would
  // otherwise appear to consume 100% CPU.
  lastCpuSeconds_ = processCpuSeconds();
#endif
  timer_.setInterval(1000);
  connect(&timer_, &QTimer::timeout, this, &CpuUsageProvider::sampleNow);
}

ChannelId CpuUsageProvider::connectChannel(const QString& name) {
  const ChannelId id = nextId_++;
  if (name != QStringLiteral("CPU_Usage")) {
    QTimer::singleShot(0, this, [this, id] {
      emit connectionChanged(id, ConnectionState::Error,
                             QStringLiteral("Local provider only supports CPU_Usage"));
    });
    return id;
  }
  active_.insert(id);
  ChannelMetadata metadata;
  metadata.connection = ConnectionState::Connected;
  metadata.units = "percent";
  metadata.precision = 2;
  metadata.displayMinimum = 0.0;
  metadata.displayMaximum = 100.0;
  QTimer::singleShot(0, this, [this, id, metadata] {
    emit connectionChanged(id, ConnectionState::Connected, QStringLiteral("Connected"));
    emit metadataChanged(id, metadata);
  });
  if (!timer_.isActive()) timer_.start();
  return id;
}

void CpuUsageProvider::disconnectChannel(ChannelId id) {
  active_.remove(id);
  if (active_.isEmpty()) timer_.stop();
  emit connectionChanged(id, ConnectionState::Disconnected,
                         QStringLiteral("Disconnected"));
}

void CpuUsageProvider::setSampleInterval(std::chrono::milliseconds interval) {
  timer_.setInterval(static_cast<int>(interval.count()));
}

std::chrono::milliseconds CpuUsageProvider::sampleInterval() const {
  return std::chrono::milliseconds(timer_.interval());
}

void CpuUsageProvider::sampleNow() {
#ifdef _WIN32
  const auto cpu = processCpuSeconds();
  const double cpuSeconds = cpu - lastCpuSeconds_;
  lastCpuSeconds_ = cpu;
#else
  const auto cpu = std::clock();
  const double cpuSeconds = static_cast<double>(cpu - lastCpu_) / CLOCKS_PER_SEC;
  lastCpu_ = cpu;
#endif
  const auto wall = std::chrono::steady_clock::now();
  const double wallSeconds = std::chrono::duration<double>(wall - lastWall_).count();
  lastWall_ = wall;
  const double percentage = wallSeconds > 0.0
                                ? std::clamp(100.0 * cpuSeconds / wallSeconds, 0.0, 100.0)
                                : 0.0;
  const Sample sample{std::chrono::system_clock::now(), percentage, 0, 0};
  for (ChannelId id : active_) emit sampleReceived(id, sample);
}

}  // namespace striptool
