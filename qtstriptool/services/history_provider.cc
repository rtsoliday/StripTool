#include "services/history_provider.h"

#include <QMetaObject>

namespace striptool {

namespace {
const int registeredHistoryTypes = [] {
  qRegisterMetaType<std::vector<striptool::Sample>>();
  qRegisterMetaType<striptool::HistoryRequestId>();
  return 0;
}();
}

HistoryRequestId NoHistoryProvider::request(const QString&, TimeRange) {
  (void)registeredHistoryTypes;
  const HistoryRequestId id = nextId_++;
  pending_.insert(id);
  QMetaObject::invokeMethod(this, [this, id] {
    if (!pending_.remove(id)) return;
    emit requestFailed(id, tr("No archive history provider is configured."));
  }, Qt::QueuedConnection);
  return id;
}

void NoHistoryProvider::cancel(HistoryRequestId id) {
  if (!pending_.remove(id)) return;
  emit requestCancelled(id);
}

void TestHistoryProvider::setSamples(const QString& channel,
                                     std::vector<Sample> samples) {
  samples_.insert(channel, std::move(samples));
}

HistoryRequestId TestHistoryProvider::request(const QString& channel,
                                              TimeRange range) {
  const HistoryRequestId id = nextId_++;
  pending_.insert(id);
  QMetaObject::invokeMethod(this, [this, id, channel, range] {
    if (!pending_.remove(id)) return;
    if (!samples_.contains(channel)) {
      emit requestFailed(id, tr("No test history exists for %1.").arg(channel));
      return;
    }
    std::vector<Sample> selected;
    for (const auto& sample : samples_.value(channel))
      if (sample.timestamp >= range.start && sample.timestamp <= range.end)
        selected.push_back(sample);
    emit resultReady(id, channel, std::move(selected));
  }, Qt::QueuedConnection);
  return id;
}

void TestHistoryProvider::cancel(HistoryRequestId id) {
  if (!pending_.remove(id)) return;
  emit requestCancelled(id);
}

}  // namespace striptool
