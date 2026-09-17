#pragma once

#include "services/channel_provider.h"

#include <cadef.h>
#include <QHash>
#include <atomic>
#include <memory>
#include <vector>

namespace striptool {

class ChannelAccessProvider final : public ChannelProvider {
public:
  explicit ChannelAccessProvider(QObject* parent = nullptr);
  ~ChannelAccessProvider() override;

  ChannelId connectChannel(const QString& name) override;
  void disconnectChannel(ChannelId id) override;
  void retryDisconnected() override;

private:
  struct State;
  static void connectionCallback(connection_handler_args args);
  static void controlCallback(event_handler_args args);
  static void valueCallback(event_handler_args args);
  void queueConnection(ChannelId id, ConnectionState state, QString message);
  void queueMetadata(ChannelId id, ChannelMetadata metadata);
  void queueSample(ChannelId id, Sample sample);

  ChannelId nextId_ = 1;
  bool contextReady_ = false;
  QHash<ChannelId, State*> active_;
  std::vector<std::unique_ptr<State>> states_;
};

}  // namespace striptool
