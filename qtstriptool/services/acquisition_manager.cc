#include "services/acquisition_manager.h"

#include <QDateTime>
#include <algorithm>
#include <cstdint>

namespace striptool {

AcquisitionManager::AcquisitionManager(ChannelProvider* provider, QObject* parent)
    : QObject(parent), provider_(provider) {
  qRegisterMetaType<ConnectionState>();
  qRegisterMetaType<ChannelMetadata>();
  qRegisterMetaType<Sample>();
  qRegisterMetaType<ChannelId>("striptool::ChannelId");
  connect(provider_, &ChannelProvider::sampleReceived, this,
          [this](ChannelId id, const Sample& sample) {
            if (auto state = channels_.value(id)) {
              state->buffer.append(sample);
              state->lastReceived = std::chrono::steady_clock::now();
              state->metadata.lastUpdate = sample.timestamp;
              if (state->metadata.connection == ConnectionState::Stale) {
                state->metadata.connection = ConnectionState::Connected;
                state->metadata.statusMessage = "Connected";
                emit channelMetadataChanged(id, state->metadata);
              }
            }
          });
  connect(provider_, &ChannelProvider::connectionChanged, this,
          [this](ChannelId id, ConnectionState connection, const QString& message) {
            if (auto state = channels_.value(id)) {
              if ((state->metadata.connection == ConnectionState::Connected ||
                   state->metadata.connection == ConnectionState::Stale) &&
                  connection != ConnectionState::Connected &&
                  connection != ConnectionState::Stale &&
                  !state->buffer.empty())
                state->buffer.append({std::chrono::system_clock::now(), 0.0,
                                      0, 0, false});
              state->metadata.connection = connection;
              state->metadata.statusMessage = message.toStdString();
              if (connection == ConnectionState::Disconnected ||
                  connection == ConnectionState::Connecting ||
                  connection == ConnectionState::Error)
              emit channelMetadataChanged(id, state->metadata);
            }
          });
  connect(provider_, &ChannelProvider::metadataChanged, this,
          [this](ChannelId id, const ChannelMetadata& metadata) {
            if (auto state = channels_.value(id)) {
              const auto previous = state->metadata;
              state->metadata = metadata;
              state->metadata.description = state->metadata.description.empty()
                                                ? state->description
                                                : state->metadata.description;
              if (!state->metadata.lastUpdate)
                state->metadata.lastUpdate = previous.lastUpdate;
              if (state->metadata.statusMessage.empty())
                state->metadata.statusMessage = previous.statusMessage;
              emit channelMetadataChanged(id, state->metadata);
            }
          });
  connect(provider_, &ChannelProvider::descriptionReceived, this,
          [this](ChannelId id, const QString& description) {
            if (auto state = channels_.value(id)) {
              state->description = description.toStdString();
              state->metadata.description = state->description;
              emit channelMetadataChanged(id, state->metadata);
            }
          });
  staleTimer_.setInterval(1000);
  connect(&staleTimer_, &QTimer::timeout, this, &AcquisitionManager::checkStaleNow);
  staleTimer_.start();
  connect(&refreshTimer_, &QTimer::timeout, this,
          &AcquisitionManager::displayRefreshRequested);
}

AcquisitionManager::~AcquisitionManager() {
  const auto ids = channels_.keys();
  channels_.clear();
  for (ChannelId id : ids) provider_->disconnectChannel(id);
}

ChannelId AcquisitionManager::addChannel(const QString& name, std::size_t capacity) {
  const ChannelId id = provider_->connectChannel(name);
  channels_.insert(id, std::make_shared<ChannelState>(capacity));
  channels_[id]->metadata.connection = ConnectionState::Connecting;
  return id;
}

void AcquisitionManager::removeChannel(ChannelId id) {
  provider_->disconnectChannel(id);
  channels_.remove(id);
}

void AcquisitionManager::setRefreshInterval(std::chrono::milliseconds interval) {
  refreshTimer_.start(static_cast<int>(interval.count()));
}

std::chrono::milliseconds AcquisitionManager::refreshInterval() const {
  return std::chrono::milliseconds(refreshTimer_.interval());
}

const SampleBuffer* AcquisitionManager::buffer(ChannelId id) const {
  const auto state = channels_.value(id);
  return state ? &state->buffer : nullptr;
}

ChannelMetadata AcquisitionManager::metadata(ChannelId id) const {
  const auto state = channels_.value(id);
  return state ? state->metadata : ChannelMetadata{};
}

void AcquisitionManager::setStaleAfter(std::chrono::milliseconds interval) {
  staleAfter_ = interval;
  staleTimer_.setInterval(static_cast<int>(std::clamp<std::int64_t>(
      interval.count(), 1, 1000)));
}

void AcquisitionManager::checkStaleNow() {
  for (auto it = channels_.begin(); it != channels_.end(); ++it) {
    auto& state = *it.value();
    if (state.lastReceived == std::chrono::steady_clock::time_point{} ||
        (state.metadata.connection != ConnectionState::Connected &&
         state.metadata.connection != ConnectionState::Stale)) continue;
    if (std::chrono::steady_clock::now() - state.lastReceived > staleAfter_) {
      if (state.metadata.connection == ConnectionState::Stale) continue;
      state.metadata.connection = ConnectionState::Stale;
      state.metadata.statusMessage = "No recent samples";
      emit channelMetadataChanged(it.key(), state.metadata);
    }
  }
}

void AcquisitionManager::clearSamples() {
  for (auto& state : channels_) state->buffer.clear();
  emit displayRefreshRequested();
}

void AcquisitionManager::setBufferCapacity(std::size_t capacity) {
  for (auto& state : channels_) state->buffer.setCapacity(capacity);
}

}  // namespace striptool
