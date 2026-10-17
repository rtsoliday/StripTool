#pragma once

#include "core/model.h"
#include "core/sample_buffer.h"

#include <QByteArray>
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QTimer>
#include <future>
#include <memory>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

Q_DECLARE_METATYPE(std::vector<striptool::Sample>)

namespace striptool {

using HistoryRequestId = quint64;
inline constexpr qint64 kMaximumHistoryResponseBytes = 8 * 1024 * 1024;
inline constexpr std::size_t kMaximumHistoryResponseSamples = 100000;

std::vector<Sample> parseArchiverJson(const QByteArray& content,
                                      QString* error = nullptr,
                                      std::size_t maximumSamples = kMaximumHistoryResponseSamples);

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

class ArchiverHistoryProvider final : public HistoryProvider {
  Q_OBJECT
public:
  static QString defaultRetrievalRoot();
  static QString configuredRetrievalRoot();
  static QUrl requestUrl(const QString& retrievalRoot, const QString& channel,
                         TimeRange range);

  explicit ArchiverHistoryProvider(QString retrievalRoot = {},
                                   QObject* parent = nullptr);
  ~ArchiverHistoryProvider() override;
  HistoryRequestId request(const QString& channel, TimeRange range) override;
  void cancel(HistoryRequestId id) override;
  QString retrievalRoot() const { return retrievalRoot_; }
  // Bounds include downloads waiting for or undergoing parsing. Parsing uses
  // one worker, so full JSON documents cannot multiply with the curve count.
  qint64 maximumResponseBytes = kMaximumHistoryResponseBytes;
  qint64 maximumBufferedBytes = 32 * 1024 * 1024;
  std::size_t maximumSamples = kMaximumHistoryResponseSamples;
  int maximumRequests = 10, requestTimeoutMs = 120000;
  qint64 bufferedBytes() const { return bufferedBytes_; }
  int pendingRequestCount() const { return pending_.size(); }

private:
  QNetworkAccessManager* network_ = nullptr;
  QString retrievalRoot_;
  HistoryRequestId nextId_ = 1;
  struct Request {
    QPointer<QNetworkReply> reply;
    QString channel;
    QByteArray content;
    qint64 bytes = 0;
    bool downloaded = false, cancelled = false;
  };
  struct Parsed { std::vector<Sample> samples; QString error; };
  QHash<HistoryRequestId, std::shared_ptr<Request>> pending_;
  qint64 bufferedBytes_ = 0;
  std::future<Parsed> parsing_;
  HistoryRequestId parsingId_ = 0;
  std::shared_ptr<Request> parsingRequest_;
  QTimer parseTimer_;
  bool pumping_ = false;
  bool readReply(HistoryRequestId);
  void release(const std::shared_ptr<Request>&);
  void fail(HistoryRequestId, const QString&);
  void pumpParser();
  void pumpParserNow();
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
