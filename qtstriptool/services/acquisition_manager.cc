#include "services/acquisition_manager.h"

#include <QDateTime>

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
              state->latest = sample;
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
              state->metadata.connection = connection;
              state->metadata.statusMessage = message.toStdString();
              emit channelMetadataChanged(id, state->metadata);
            }
          });
  connect(provider_, &ChannelProvider::metadataChanged, this,
          [this](ChannelId id, const ChannelMetadata& metadata) {
            if (auto state = channels_.value(id)) {
              state->metadata = metadata;
              emit channelMetadataChanged(id, state->metadata);
            }
          });
  connect(&sampleTimer_, &QTimer::timeout, this, &AcquisitionManager::sampleNow);
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

void AcquisitionManager::setSampleInterval(std::chrono::milliseconds interval) {
  sampleTimer_.start(static_cast<int>(interval.count()));
}

void AcquisitionManager::setRefreshInterval(std::chrono::milliseconds interval) {
  refreshTimer_.start(static_cast<int>(interval.count()));
}

std::chrono::milliseconds AcquisitionManager::sampleInterval() const {
  return std::chrono::milliseconds(sampleTimer_.interval());
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

void AcquisitionManager::sampleNow() {
  const auto now = std::chrono::system_clock::now();
  for (auto it = channels_.begin(); it != channels_.end(); ++it) {
    auto& state = *it.value();
    if (!state.latest) continue;
    state.buffer.append(*state.latest);
    if (state.metadata.connection == ConnectionState::Connected &&
        now - state.latest->timestamp > staleAfter_) {
      state.metadata.connection = ConnectionState::Stale;
      state.metadata.statusMessage = "No recent samples";
      emit channelMetadataChanged(it.key(), state.metadata);
    }
  }
}

}  // namespace striptool
