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
  static constexpr std::size_t kMaximumPendingUpdates = 4096;
  static constexpr std::size_t kMaximumPendingBytes = 2 * 1024 * 1024;
  std::size_t pendingUpdateCount() const;
  std::size_t pendingUpdateBytes() const;
  std::size_t retainedStateCount() const { return states_.size(); }

private:
  friend struct ChannelAccessProviderTestAccess;
  struct State;
  struct Update;
  struct Inbox;
  void post(Update);
  void drain();
  void closeState(State*);
  static void connectionCallback(connection_handler_args args);
  static void controlCallback(event_handler_args args);
  static void valueCallback(event_handler_args args);
  static void descriptionConnectionCallback(connection_handler_args args);
  static void descriptionCallback(event_handler_args args);
  void queueConnection(ChannelId id, ConnectionState state, QString message);
  void queueMetadata(ChannelId id, ChannelMetadata metadata);
  void queueSample(ChannelId id, Sample sample);
  void queueDescription(ChannelId id, QString description);

  ChannelId nextId_ = 1;
  bool contextReady_ = false;
  QHash<ChannelId, State*> active_;
  std::vector<std::unique_ptr<State>> states_;
  std::unique_ptr<Inbox> inbox_;
};

}  // namespace striptool
