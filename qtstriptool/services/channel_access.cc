#include "services/channel_access.h"

#include <db_access.h>
#include <QMetaObject>
#include <QPointer>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>
#include <algorithm>
#include <deque>
#include <mutex>
#include <optional>

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

struct ChannelAccessProvider::Update {
  enum Kind { Connection, Metadata, Value, Description } kind;
  ChannelId id;
  Update(Kind k, ChannelId channel) : kind(k), id(channel) {}
  ConnectionState connection = ConnectionState::Disconnected;
  QString text;
  ChannelMetadata metadata;
  Sample sample;
  std::chrono::system_clock::time_point time = std::chrono::system_clock::now();
  std::size_t bytes() const {
    return sizeof(Update) + std::size_t(text.capacity()) * 2 + metadata.units.capacity() +
        metadata.description.capacity() + metadata.statusMessage.capacity();
  }
};

struct ChannelAccessProvider::Inbox {
  struct Latest {
    std::optional<ConnectionState> connection;
    QString message;
    std::optional<ChannelMetadata> metadata;
    std::optional<QString> description;
    std::optional<Sample> sample;
    bool lost = false, sampleLost = false;
    std::chrono::system_clock::time_point gap;
  };
  std::mutex mutex;
  std::deque<Update> updates;
  QHash<ChannelId, Latest> latest; // One snapshot per active channel, not per event.
  std::size_t bytes = 0;
  bool scheduled = false, overflow = false, closing = false;
  void lost(const Update& u) {
    auto it = latest.find(u.id);
    if (it == latest.end()) return;
    const auto when = u.kind == Update::Value ? u.sample.timestamp : u.time;
    if (!it->lost || when < it->gap) it->gap = when;
    it->lost = true;
    it->sampleLost |= u.kind == Update::Value;
  }
};

ChannelAccessProvider::ChannelAccessProvider(QObject* parent)
    : ChannelProvider(parent), inbox_(std::make_unique<Inbox>()) {
  contextReady_ = ca_context_create(ca_enable_preemptive_callback) == ECA_NORMAL;
}

ChannelAccessProvider::~ChannelAccessProvider() {
  {
    std::lock_guard<std::mutex> guard(inbox_->mutex);
    inbox_->closing = true;
  }
  for (auto& state : states_) closeState(state.get());
  if (contextReady_) ca_context_destroy();
}

void ChannelAccessProvider::closeState(State* state) {
  state->active.store(false);
  // ca_clear_channel reclaims subscriptions and waits for this channel's
  // callbacks. Do not hold the inbox mutex while waiting for those callbacks.
  // Reading subscription before that fence would race connectionCallback.
  if (contextReady_ && state->channel) ca_clear_channel(state->channel);
  if (contextReady_ && state->descriptionChannel) ca_clear_channel(state->descriptionChannel);
  state->subscription = nullptr;
  state->channel = state->descriptionChannel = nullptr;
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
  {
    std::lock_guard<std::mutex> guard(inbox_->mutex);
    inbox_->latest.insert(id, {});
  }
  if (!contextReady_) {
    queueConnection(id, ConnectionState::Error,
                    QStringLiteral("Channel Access initialization failed"));
    return id;
  }
  queueConnection(id, ConnectionState::Connecting, QStringLiteral("Connecting"));
  const int status = ca_create_channel(name.toUtf8().constData(), connectionCallback,
                                       raw, CA_PRIORITY_DEFAULT, &raw->channel);
  if (status != ECA_NORMAL) {
    queueConnection(id, ConnectionState::Error,
                    QString::fromLatin1(ca_message(status)));
  } else {
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
  closeState(state);
  {
    std::lock_guard<std::mutex> guard(inbox_->mutex);
    inbox_->latest.remove(id);
    auto& updates = inbox_->updates;
    updates.erase(std::remove_if(updates.begin(), updates.end(), [&](const Update& u) {
      return u.id == id;
    }), updates.end());
    inbox_->bytes = 0;
    for (const auto& u : updates) inbox_->bytes += u.bytes();
  }
  states_.erase(std::find_if(states_.begin(), states_.end(),
                             [state](const auto& p) { return p.get() == state; }));
  if (contextReady_) ca_flush_io();
  emit connectionChanged(id, ConnectionState::Disconnected,
                         QStringLiteral("Disconnected"));
}

void ChannelAccessProvider::connectionCallback(connection_handler_args args) {
  auto* state = static_cast<State*>(ca_puser(args.chid));
  if (!state || !state->active.load()) return;
  if (args.op == CA_OP_CONN_UP) {
    state->owner->queueConnection(state->id, ConnectionState::Connected,
                                  QStringLiteral("Connected"));
    // Units and limits may have changed while the IOC was down, so read them
    // on every connection. CA restores the subscription itself.
    ca_get_callback(DBR_CTRL_DOUBLE, state->channel, controlCallback, state);
    if (!state->subscription)
      ca_create_subscription(DBR_STS_DOUBLE, 1, state->channel,
                             DBE_VALUE | DBE_ALARM, valueCallback, state,
                             &state->subscription);
    ca_flush_io();
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
  // Other CA servers may fill units without a NUL; stay within the field.
  metadata.units.assign(control->units, strnlen(control->units, sizeof control->units));
  metadata.precision = control->precision;
  // A NaN or infinite limit would reach the curve limits, which then cannot
  // be plotted or saved.
  const auto usable = [](double low, double high) {
    return std::isfinite(low) && std::isfinite(high) && low < high;
  };
  double low = control->lower_disp_limit;
  double high = control->upper_disp_limit;
  if (!usable(low, high)) {
    low = control->lower_ctrl_limit;
    high = control->upper_ctrl_limit;
  }
  if (!usable(low, high)) {
    low = control->value - std::abs(control->value / 10.0);
    high = control->value + std::abs(control->value / 10.0);
  }
  if (!usable(low, high)) { low = -100.0; high = 100.0; }
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
  const auto* value = static_cast<const dbr_sts_double*>(args.dbr);
  Sample sample;
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
  // DBR_STRING is a fixed field that need not end in a NUL.
  const auto* text = static_cast<const char*>(args.dbr);
  state->owner->queueDescription(
      state->id, QString::fromUtf8(text, int(qstrnlen(text, MAX_STRING_SIZE))).trimmed());
}

void ChannelAccessProvider::queueConnection(ChannelId id, ConnectionState state,
                                            QString message) {
  Update u{Update::Connection, id};
  u.connection = state; u.text = std::move(message);
  post(std::move(u));
}

void ChannelAccessProvider::queueMetadata(ChannelId id, ChannelMetadata metadata) {
  Update u{Update::Metadata, id}; u.metadata = std::move(metadata);
  post(std::move(u));
}

void ChannelAccessProvider::queueSample(ChannelId id, Sample sample) {
  // Timestamp at the CA callback boundary. This uses the same workstation
  // clock as the live display and excludes callback-to-GUI queue latency.
  sample.timestamp = std::chrono::system_clock::now();
  Update u{Update::Value, id}; u.sample = sample;
  post(std::move(u));
}

void ChannelAccessProvider::queueDescription(ChannelId id, QString description) {
  Update u{Update::Description, id}; u.text = std::move(description);
  post(std::move(u));
}

std::size_t ChannelAccessProvider::pendingUpdateCount() const {
  std::lock_guard<std::mutex> guard(inbox_->mutex);
  return inbox_->updates.size();
}

std::size_t ChannelAccessProvider::pendingUpdateBytes() const {
  std::lock_guard<std::mutex> guard(inbox_->mutex);
  return inbox_->bytes;
}

void ChannelAccessProvider::post(Update u) {
  std::lock_guard<std::mutex> guard(inbox_->mutex);
  auto found = inbox_->latest.find(u.id);
  if (inbox_->closing || found == inbox_->latest.end()) return;
  auto& latest = found.value();
  switch (u.kind) {
  case Update::Connection: latest.connection = u.connection; latest.message = u.text; break;
  case Update::Metadata: latest.metadata = u.metadata; break;
  case Update::Description: latest.description = u.text; break;
  case Update::Value: latest.sample = u.sample; break;
  }
  const auto bytes = u.bytes();
  if (!inbox_->overflow && (inbox_->updates.size() >= kMaximumPendingUpdates ||
                            bytes > kMaximumPendingBytes - inbox_->bytes)) {
    for (const auto& pending : inbox_->updates) inbox_->lost(pending);
    inbox_->updates.clear(); inbox_->bytes = 0;
    inbox_->overflow = true;
  }
  if (inbox_->overflow) inbox_->lost(u);
  else { inbox_->bytes += bytes; inbox_->updates.push_back(std::move(u)); }
  if (!inbox_->scheduled) {
    inbox_->scheduled = true;
    QMetaObject::invokeMethod(this, [this] { drain(); }, Qt::QueuedConnection);
  }
}

void ChannelAccessProvider::drain() {
  QPointer<ChannelAccessProvider> guard(this);
  std::vector<Update> updates;
  QHash<ChannelId, Inbox::Latest> recovery;
  {
    std::lock_guard<std::mutex> guard(inbox_->mutex);
    if (inbox_->overflow) {
      for (auto it = inbox_->latest.begin(); it != inbox_->latest.end(); ++it)
        if (it->lost) {
          recovery.insert(it.key(), it.value());
          it->lost = it->sampleLost = false;
        }
      inbox_->overflow = false;
    }
    for (int n = 0; n < 256 && !inbox_->updates.empty(); ++n) {
      inbox_->bytes -= inbox_->updates.front().bytes();
      updates.push_back(std::move(inbox_->updates.front()));
      inbox_->updates.pop_front();
    }
  }
  for (auto it = recovery.cbegin(); it != recovery.cend(); ++it) {
    const auto id = it.key();
    const auto& state = it.value();
    if (!active_.contains(id)) continue;
    // A bounded queue cannot retain every transition under sustained overload.
    // Break the trace explicitly, report loss, then publish the latest state.
    emit sampleReceived(id, {state.gap, 0.0, 0, 0, false});
    if (!guard) return;
    if (!active_.contains(id)) continue;
    emit dataLoss(id, tr("CA update buffer overflow; observations lost"));
    if (!guard) return;
    if (active_.contains(id) && state.connection) {
      emit connectionChanged(id, *state.connection, state.message);
      if (!guard) return;
    }
    if (active_.contains(id) && state.metadata) {
      auto metadata = *state.metadata;
      if (state.connection) metadata.connection = *state.connection;
      metadata.statusMessage = state.message.toStdString();
      emit metadataChanged(id, metadata);
      if (!guard) return;
    }
    if (active_.contains(id) && state.description) {
      emit descriptionReceived(id, *state.description);
      if (!guard) return;
    }
    if (active_.contains(id) && state.sampleLost && state.sample &&
        state.connection == ConnectionState::Connected)
      emit sampleReceived(id, *state.sample);
    if (!guard) return;
  }
  for (const auto& u : updates) {
    if (!active_.contains(u.id)) continue;
    switch (u.kind) {
    case Update::Connection: emit connectionChanged(u.id, u.connection, u.text); break;
    case Update::Metadata: emit metadataChanged(u.id, u.metadata); break;
    case Update::Description: emit descriptionReceived(u.id, u.text); break;
    case Update::Value: emit sampleReceived(u.id, u.sample); break;
    }
    if (!guard) return;
  }
  {
    std::lock_guard<std::mutex> guard(inbox_->mutex);
    if (inbox_->updates.empty() && !inbox_->overflow) inbox_->scheduled = false;
    else QMetaObject::invokeMethod(this, [this] { drain(); }, Qt::QueuedConnection);
  }
}

}  // namespace striptool
