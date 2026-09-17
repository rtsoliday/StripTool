#pragma once

#include "core/model.h"
#include "core/sample_buffer.h"

#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <vector>

Q_DECLARE_METATYPE(std::vector<striptool::Sample>)

namespace striptool {

using HistoryRequestId = quint64;

class HistoryProvider : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual ~HistoryProvider() = default;
  virtual HistoryRequestId request(const QString& channel, TimeRange range) = 0;
  virtual void cancel(HistoryRequestId id) = 0;

signals:
  void resultReady(striptool::HistoryRequestId id, QString channel,
                   std::vector<striptool::Sample> samples);
  void requestFailed(striptool::HistoryRequestId id, QString message);
  void requestCancelled(striptool::HistoryRequestId id);
};

class NoHistoryProvider final : public HistoryProvider {
  Q_OBJECT
public:
  using HistoryProvider::HistoryProvider;
  HistoryRequestId request(const QString& channel, TimeRange range) override;
  void cancel(HistoryRequestId id) override;

private:
  HistoryRequestId nextId_ = 1;
  QSet<HistoryRequestId> pending_;
};

// Deterministic provider for UI/service tests and offline demonstrations.
class TestHistoryProvider final : public HistoryProvider {
  Q_OBJECT
public:
  using HistoryProvider::HistoryProvider;
  void setSamples(const QString& channel, std::vector<Sample> samples);
  HistoryRequestId request(const QString& channel, TimeRange range) override;
  void cancel(HistoryRequestId id) override;

private:
  HistoryRequestId nextId_ = 1;
  QHash<QString, std::vector<Sample>> samples_;
  QSet<HistoryRequestId> pending_;
};

}  // namespace striptool

Q_DECLARE_METATYPE(striptool::HistoryRequestId)
