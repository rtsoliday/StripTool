#include "services/channel_access.h"

#include <db_access.h>
#include <epicsTime.h>
#include <QMetaObject>
#include <cmath>
#include <utility>

namespace striptool {

struct ChannelAccessProvider::State {
  ChannelAccessProvider* owner = nullptr;
  ChannelId id = 0;
  QString name;
  chid channel = nullptr;
  chid descriptionChannel = nullptr;
  evid subscription = nullptr;
  std::atomic_bool active{true};
};

ChannelAccessProvider::ChannelAccessProvider(QObject* parent)
    : ChannelProvider(parent) {
  contextReady_ = ca_context_create(ca_enable_preemptive_callback) == ECA_NORMAL;
}

ChannelAccessProvider::~ChannelAccessProvider() {
  for (auto& state : states_) {
    state->active.store(false);
    if (contextReady_ && state->subscription)
      ca_clear_subscription(state->subscription);
    if (contextReady_ && state->channel) ca_clear_channel(state->channel);
    if (contextReady_ && state->descriptionChannel)
      ca_clear_channel(state->descriptionChannel);
    state->subscription = nullptr;
    state->channel = nullptr;
    state->descriptionChannel = nullptr;
  }
  if (contextReady_) ca_context_destroy();
}

ChannelId ChannelAccessProvider::connectChannel(const QString& name) {
  const ChannelId id = nextId_++;
  auto state = std::make_unique<State>();
  state->owner = this;
  state->id = id;
  state->name = name;
  State* raw = state.get();
  states_.push_back(std::move(state));
  active_.insert(id, raw);
  if (!contextReady_) {
    queueConnection(id, ConnectionState::Error,
                    QStringLiteral("Channel Access initialization failed"));
    return id;
  }
  const int status = ca_create_channel(name.toUtf8().constData(), connectionCallback,
                                       raw, CA_PRIORITY_DEFAULT, &raw->channel);
  if (status != ECA_NORMAL) {
    queueConnection(id, ConnectionState::Error,
                    QString::fromLatin1(ca_message(status)));
  } else {
    queueConnection(id, ConnectionState::Connecting, QStringLiteral("Connecting"));
    const QString descriptionName = name.section(QLatin1Char('.'), 0, 0) +
                                    QStringLiteral(".DESC");
    ca_create_channel(descriptionName.toUtf8().constData(),
                      descriptionConnectionCallback, raw, CA_PRIORITY_DEFAULT,
                      &raw->descriptionChannel);
    ca_flush_io();
  }
  return id;
}

void ChannelAccessProvider::disconnectChannel(ChannelId id) {
  State* state = active_.take(id);
  if (!state) return;
  state->active.store(false);
  if (state->subscription) ca_clear_subscription(state->subscription);
  state->subscription = nullptr;
  if (state->channel) ca_clear_channel(state->channel);
  state->channel = nullptr;
  if (state->descriptionChannel) ca_clear_channel(state->descriptionChannel);
  state->descriptionChannel = nullptr;
  if (contextReady_) ca_flush_io();
  emit connectionChanged(id, ConnectionState::Disconnected,
                         QStringLiteral("Disconnected"));
}

void ChannelAccessProvider::retryDisconnected() {
  if (!contextReady_) return;
  for (State* state : active_) {
    if (state->channel && ca_state(state->channel) != cs_conn) {
      queueConnection(state->id, ConnectionState::Connecting,
                      QStringLiteral("Retrying connection"));
    }
  }
  ca_flush_io();
}

void ChannelAccessProvider::connectionCallback(connection_handler_args args) {
  auto* state = static_cast<State*>(ca_puser(args.chid));
  if (!state || !state->active.load()) return;
  if (args.op == CA_OP_CONN_UP) {
    state->owner->queueConnection(state->id, ConnectionState::Connected,
                                  QStringLiteral("Connected"));
    if (!state->subscription) {
      ca_get_callback(DBR_CTRL_DOUBLE, state->channel, controlCallback, state);
      ca_create_subscription(DBR_TIME_DOUBLE, 1, state->channel,
                             DBE_VALUE | DBE_ALARM, valueCallback, state,
                             &state->subscription);
      ca_flush_io();
    }
  } else {
    state->owner->queueConnection(state->id, ConnectionState::Disconnected,
                                  QStringLiteral("Connection lost"));
  }
}

void ChannelAccessProvider::controlCallback(event_handler_args args) {
  auto* state = static_cast<State*>(args.usr);
  if (!state || !state->active.load()) return;
  if (args.status != ECA_NORMAL || !args.dbr) {
    state->owner->queueConnection(state->id, ConnectionState::Error,
                                  QString::fromLatin1(ca_message(args.status)));
    return;
  }
  const auto* control = static_cast<const dbr_ctrl_double*>(args.dbr);
  ChannelMetadata metadata;
  metadata.connection = ConnectionState::Connected;
  metadata.units = control->units;
  metadata.precision = control->precision;
  double low = control->lower_disp_limit;
  double high = control->upper_disp_limit;
  if (high <= low) {
    low = control->lower_ctrl_limit;
    high = control->upper_ctrl_limit;
  }
  if (high <= low) {
    if (control->value == 0.0) { low = -100.0; high = 100.0; }
    else { low = control->value - std::abs(control->value / 10.0);
           high = control->value + std::abs(control->value / 10.0); }
  }
  metadata.displayMinimum = low;
  metadata.displayMaximum = high;
  state->owner->queueMetadata(state->id, metadata);
}

void ChannelAccessProvider::valueCallback(event_handler_args args) {
  auto* state = static_cast<State*>(args.usr);
  if (!state || !state->active.load()) return;
  if (args.status != ECA_NORMAL || !args.dbr) {
    state->owner->queueConnection(state->id, ConnectionState::Error,
                                  QString::fromLatin1(ca_message(args.status)));
    return;
  }
  const auto* value = static_cast<const dbr_time_double*>(args.dbr);
  timespec timestamp{};
  epicsTimeToTimespec(&timestamp, &value->stamp);
  Sample sample;
  sample.timestamp = std::chrono::time_point_cast<
      std::chrono::system_clock::duration>(
      std::chrono::system_clock::from_time_t(timestamp.tv_sec) +
      std::chrono::nanoseconds(timestamp.tv_nsec));
  sample.value = value->value;
  sample.status = value->status;
  sample.severity = value->severity;
  state->owner->queueSample(state->id, sample);
}

void ChannelAccessProvider::descriptionConnectionCallback(connection_handler_args args) {
  auto* state = static_cast<State*>(ca_puser(args.chid));
  if (!state || !state->active.load() || args.op != CA_OP_CONN_UP) return;
  ca_get_callback(DBR_STRING, args.chid, descriptionCallback, state);
  ca_flush_io();
}

void ChannelAccessProvider::descriptionCallback(event_handler_args args) {
  auto* state = static_cast<State*>(args.usr);
  if (!state || !state->active.load() || args.status != ECA_NORMAL || !args.dbr)
    return;
  state->owner->queueDescription(
      state->id, QString::fromUtf8(static_cast<const char*>(args.dbr)).trimmed());
}

void ChannelAccessProvider::queueConnection(ChannelId id, ConnectionState state,
                                            QString message) {
  QMetaObject::invokeMethod(this, [this, id, state, message = std::move(message)] {
    if (active_.contains(id)) emit connectionChanged(id, state, message);
  }, Qt::QueuedConnection);
}

void ChannelAccessProvider::queueMetadata(ChannelId id, ChannelMetadata metadata) {
  QMetaObject::invokeMethod(this, [this, id, metadata = std::move(metadata)] {
    if (active_.contains(id)) emit metadataChanged(id, metadata);
  }, Qt::QueuedConnection);
}

void ChannelAccessProvider::queueSample(ChannelId id, Sample sample) {
  QMetaObject::invokeMethod(this, [this, id, sample] {
    if (active_.contains(id)) emit sampleReceived(id, sample);
  }, Qt::QueuedConnection);
}

void ChannelAccessProvider::queueDescription(ChannelId id, QString description) {
  QMetaObject::invokeMethod(this, [this, id, description = std::move(description)] {
    if (active_.contains(id)) emit descriptionReceived(id, description);
  }, Qt::QueuedConnection);
}

}  // namespace striptool
