#include "services/history_provider.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace striptool {

namespace {
const int registeredHistoryTypes = [] {
  qRegisterMetaType<std::vector<striptool::Sample>>();
  qRegisterMetaType<striptool::HistoryRequestId>();
  return 0;
}();
}

std::vector<Sample> parseArchiverJson(const QByteArray& content, QString* error) {
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(content, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
    if (error)
      *error = parseError.error == QJsonParseError::NoError
                   ? QObject::tr("response root is not an array")
                   : parseError.errorString();
    return {};
  }
  std::vector<Sample> samples;
  for (const auto& entryValue : document.array()) {
    if (!entryValue.isObject()) continue;
    const QJsonArray data = entryValue.toObject().value(QStringLiteral("data")).toArray();
    samples.reserve(samples.size() + static_cast<std::size_t>(data.size()));
    for (const auto& sampleValue : data) {
      if (!sampleValue.isObject()) continue;
      const QJsonObject item = sampleValue.toObject();
      const QJsonValue value = item.value(QStringLiteral("val"));
      const QJsonValue secsValue = item.value(QStringLiteral("secs"));
      const QJsonValue nanosValue = item.value(QStringLiteral("nanos"));
      if (!value.isDouble() || !secsValue.isDouble() || !nanosValue.isDouble())
        continue;  // qtstriptool supports scalar numeric PVs.
      const qint64 secs = secsValue.toVariant().toLongLong();
      const qint64 nanos = nanosValue.toVariant().toLongLong();
      const double number = value.toDouble();
      if (!std::isfinite(number) || nanos < 0 || nanos >= 1000000000) continue;
      Sample sample;
      sample.timestamp = std::chrono::system_clock::time_point{
          std::chrono::seconds(secs) + std::chrono::nanoseconds(nanos)};
      sample.value = number;
      sample.status = static_cast<std::uint16_t>(std::clamp(
          item.value(QStringLiteral("status")).toInt(), 0, 65535));
      sample.severity = static_cast<std::uint16_t>(std::clamp(
          item.value(QStringLiteral("severity")).toInt(), 0, 65535));
      samples.push_back(sample);
    }
  }
  std::stable_sort(samples.begin(), samples.end(),
                   [](const Sample& left, const Sample& right) {
                     return left.timestamp < right.timestamp;
                   });
  if (error) error->clear();
  return samples;
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

QString ArchiverHistoryProvider::defaultRetrievalRoot() {
  return QStringLiteral("http://asddtn03.aps4.anl.gov:17668/retrieval");
}

QString ArchiverHistoryProvider::configuredRetrievalRoot() {
  const QString configured = qEnvironmentVariable("QTSTRIPTOOL_ARCHIVER_URL").trimmed();
  return configured.isEmpty() ? defaultRetrievalRoot() : configured;
}

ArchiverHistoryProvider::ArchiverHistoryProvider(QString retrievalRoot,
                                                 QObject* parent)
    : HistoryProvider(parent),
      network_(new QNetworkAccessManager(this)),
      retrievalRoot_(retrievalRoot.trimmed().isEmpty()
                         ? configuredRetrievalRoot()
                         : retrievalRoot.trimmed()) {
  while (retrievalRoot_.endsWith(QLatin1Char('/'))) retrievalRoot_.chop(1);
}

QUrl ArchiverHistoryProvider::requestUrl(const QString& retrievalRoot,
                                         const QString& channel,
                                         TimeRange range) {
  const double seconds = std::chrono::duration<double>(range.end - range.start).count();
  const int rawInterval = std::max(1, static_cast<int>(std::ceil(seconds / 10000.0)));
  const double exponent = std::pow(10.0, std::floor(std::log10(rawInterval)));
  int interval = rawInterval;
  for (const int multiplier : {1, 2, 5, 10}) {
    const int candidate = static_cast<int>(multiplier * exponent);
    if (candidate >= rawInterval) { interval = candidate; break; }
  }

  QString endpoint = retrievalRoot;
  while (endpoint.endsWith(QLatin1Char('/'))) endpoint.chop(1);
  if (!endpoint.endsWith(QStringLiteral("/data/getData.json")))
    endpoint += QStringLiteral("/data/getData.json");
  QUrl url(endpoint);
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("pv"),
                     QStringLiteral("lastSample_%1(%2)").arg(interval).arg(channel));
  query.addQueryItem(QStringLiteral("from"),
                     QDateTime::fromMSecsSinceEpoch(
                         std::chrono::duration_cast<std::chrono::milliseconds>(
                             range.start.time_since_epoch()).count(), Qt::UTC)
                         .toString(Qt::ISODateWithMs));
  query.addQueryItem(QStringLiteral("to"),
                     QDateTime::fromMSecsSinceEpoch(
                         std::chrono::duration_cast<std::chrono::milliseconds>(
                             range.end.time_since_epoch()).count(), Qt::UTC)
                         .toString(Qt::ISODateWithMs));
  query.addQueryItem(QStringLiteral("donotchunk"), QStringLiteral("true"));
  url.setQuery(query);
  return url;
}

HistoryRequestId ArchiverHistoryProvider::request(const QString& channel,
                                                  TimeRange range) {
  const HistoryRequestId id = nextId_++;
  if (channel.trimmed().isEmpty() || !range.isValid() || range.start == range.end) {
    QMetaObject::invokeMethod(this, [this, id] {
      emit requestFailed(id, tr("The Archiver request has an invalid PV or time range."));
    }, Qt::QueuedConnection);
    return id;
  }

  const QUrl url = requestUrl(retrievalRoot_, channel, range);
  if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
    QMetaObject::invokeMethod(this, [this, id] {
      emit requestFailed(id, tr("The EPICS Archiver URL is invalid."));
    }, Qt::QueuedConnection);
    return id;
  }

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("qtstriptool/0.1"));
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  request.setTransferTimeout(120000);
#endif
  auto* reply = network_->get(request);
  pending_.insert(id, reply);
  connect(reply, &QNetworkReply::finished, this, [this, id, channel, reply] {
    if (!pending_.remove(id)) { reply->deleteLater(); return; }
    if (reply->error() != QNetworkReply::NoError) {
      emit requestFailed(id, tr("EPICS Archiver request for %1 failed: %2")
                                 .arg(channel, reply->errorString()));
      reply->deleteLater();
      return;
    }
    QString parseError;
    auto samples = parseArchiverJson(reply->readAll(), &parseError);
    if (!parseError.isEmpty()) {
      emit requestFailed(id, tr("EPICS Archiver returned invalid JSON for %1: %2")
                                 .arg(channel, parseError));
      reply->deleteLater();
      return;
    }
    emit resultReady(id, channel, std::move(samples));
    reply->deleteLater();
  });
  return id;
}

void ArchiverHistoryProvider::cancel(HistoryRequestId id) {
  auto reply = pending_.take(id);
  if (!reply) return;
  reply->abort();
  reply->deleteLater();
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
