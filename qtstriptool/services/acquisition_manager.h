#pragma once

#include "core/sample_buffer.h"
#include "services/channel_provider.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <memory>

namespace striptool {

class AcquisitionManager final : public QObject {
  Q_OBJECT
public:
  explicit AcquisitionManager(ChannelProvider* provider, QObject* parent = nullptr);
  ~AcquisitionManager() override;

  ChannelId addChannel(const QString& name, std::size_t sampleCapacity = 7200);
  void removeChannel(ChannelId id);
  void setSampleInterval(std::chrono::milliseconds interval);
  void setRefreshInterval(std::chrono::milliseconds interval);
  void setStaleAfter(std::chrono::milliseconds interval) { staleAfter_ = interval; }
  std::chrono::milliseconds sampleInterval() const;
  std::chrono::milliseconds refreshInterval() const;
  const SampleBuffer* buffer(ChannelId id) const;
  ChannelMetadata metadata(ChannelId id) const;
  void sampleNow();
  void retryDisconnected() { provider_->retryDisconnected(); }

signals:
  void displayRefreshRequested();
  void channelMetadataChanged(striptool::ChannelId id,
                              const striptool::ChannelMetadata& metadata);

private:
  struct ChannelState {
    explicit ChannelState(std::size_t capacity) : buffer(capacity) {}
    SampleBuffer buffer;
    std::optional<Sample> latest;
    ChannelMetadata metadata;
  };

  ChannelProvider* provider_;
  QHash<ChannelId, std::shared_ptr<ChannelState>> channels_;
  QTimer sampleTimer_;
  QTimer refreshTimer_;
  std::chrono::milliseconds staleAfter_{5000};
};

}  // namespace striptool
