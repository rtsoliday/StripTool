#include "services/history_provider.h"

#include "core/application.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>

namespace striptool {

namespace {
const int registeredHistoryTypes = [] {
  qRegisterMetaType<std::vector<striptool::Sample>>();
  qRegisterMetaType<striptool::HistoryRequestId>();
  return 0;
}();
}

std::vector<Sample> parseArchiverJson(const QByteArray& content, QString* error,
                                      std::size_t maximumSamples) {
  maximumSamples = std::min(maximumSamples, kMaximumHistoryResponseSamples);
  if (content.size() > kMaximumHistoryResponseBytes) {
    if (error) *error = QObject::tr("response exceeded the history byte limit");
    return {};
  }
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
  std::size_t entries = 0;
  for (const auto& entryValue : document.array()) {
    if (!entryValue.isObject()) continue;
    const QJsonArray data = entryValue.toObject().value(QStringLiteral("data")).toArray();
    if (std::size_t(data.size()) > maximumSamples - entries) {
      if (error) *error = QObject::tr("response exceeded the history sample limit");
      return {};
    }
    entries += std::size_t(data.size());
    samples.reserve(samples.size() + static_cast<std::size_t>(data.size()));
    for (const auto& sampleValue : data) {
      if (!sampleValue.isObject()) continue;
      const QJsonObject item = sampleValue.toObject();
      const QJsonValue value = item.value(QStringLiteral("val"));
      const QJsonValue secsValue = item.value(QStringLiteral("secs"));
      const QJsonValue nanosValue = item.value(QStringLiteral("nanos"));
      if (!value.isDouble() || !secsValue.isDouble() || !nanosValue.isDouble())
        continue;  // qtstriptool supports scalar numeric PVs.
      const double seconds = secsValue.toDouble();
      const double nanoseconds = nanosValue.toDouble();
      const double number = value.toDouble();
      // Validate before converting or multiplying: QVariant rounds fractional
      // timestamps and chrono's seconds-to-nanoseconds conversion can overflow.
      // Use the common 64-bit nanosecond range even on microsecond-clock hosts.
      constexpr qint64 billion = 1000000000;
      constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
      constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
      constexpr auto minimumSeconds = minimum / billion - 1;
      constexpr auto maximumSeconds = maximum / billion;
      if (!std::isfinite(number) || !std::isfinite(seconds) ||
          std::trunc(seconds) != seconds || seconds < minimumSeconds ||
          seconds > maximumSeconds || !std::isfinite(nanoseconds) ||
          std::trunc(nanoseconds) != nanoseconds || nanoseconds < 0 ||
          nanoseconds >= billion) continue;
      const auto secs = static_cast<qint64>(seconds);
      const auto nanos = static_cast<qint64>(nanoseconds);
      if ((secs == minimumSeconds && nanos < billion + minimum % billion) ||
          (secs == maximumSeconds && nanos > maximum % billion)) continue;
      // For negative seconds, multiply the next second and subtract the
      // remaining fraction so the intermediate product also fits at the limit.
      const auto ticks = secs < 0 ? (secs + 1) * billion + nanos - billion
                                  : secs * billion + nanos;
      Sample sample;
      sample.timestamp = std::chrono::system_clock::time_point{
          std::chrono::duration_cast<std::chrono::system_clock::duration>(
              std::chrono::nanoseconds(ticks))};
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
  parseTimer_.setInterval(10);
  connect(&parseTimer_, &QTimer::timeout, this, [this] { pumpParser(); });
}

ArchiverHistoryProvider::~ArchiverHistoryProvider() {
  parseTimer_.stop();
  for (const auto& request : pending_)
    if (request->reply) {
      request->reply->disconnect(this);
      request->reply->abort();
    }
  // The one bounded parser owns only bytes and plain result values, never this
  // QObject. A close waits for it without exposing destroyed GUI state.
  if (parsing_.valid()) parsing_.wait();
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
  // QUrlQuery leaves '+' and "%XX" as written, but the Archiver form-decodes
  // the query ('+' becomes a space), so encode the whole PV value here.
  const QString pv = QStringLiteral("lastSample_%1(%2)").arg(interval).arg(channel);
  query.addQueryItem(QStringLiteral("pv"), QString::fromLatin1(QUrl::toPercentEncoding(pv)));
  query.addQueryItem(QStringLiteral("from"),
                     QDateTime::fromMSecsSinceEpoch(
                         std::chrono::duration_cast<std::chrono::milliseconds>(
                             range.start.time_since_epoch()).count(), QTimeZone::utc())
                         .toUTC()
                         .toString(Qt::ISODateWithMs));
  query.addQueryItem(QStringLiteral("to"),
                     QDateTime::fromMSecsSinceEpoch(
                         std::chrono::duration_cast<std::chrono::milliseconds>(
                             range.end.time_since_epoch()).count(), QTimeZone::utc())
                         .toUTC()
                         .toString(Qt::ISODateWithMs));
  query.addQueryItem(QStringLiteral("donotchunk"), QStringLiteral("true"));
  url.setQuery(query);
  return url;
}

HistoryRequestId ArchiverHistoryProvider::request(const QString& channel,
                                                  TimeRange range) {
  const HistoryRequestId id = nextId_++;
  if (pending_.size() >= maximumRequests) {
    QMetaObject::invokeMethod(this, [this, id] {
      emit requestFailed(id, tr("The concurrent archive request limit was reached."));
    }, Qt::QueuedConnection);
    return id;
  }
  if (channel.trimmed().isEmpty() || !range.isValid() || range.start == range.end) {
    QMetaObject::invokeMethod(this, [this, id] {
      emit requestFailed(id, tr("The Archiver request has an invalid PV or time range."));
    }, Qt::QueuedConnection);
    return id;
  }

  const QUrl url = requestUrl(retrievalRoot_, channel, range);
  if (!url.isValid() || (url.scheme() != QStringLiteral("https") &&
                         url.scheme() != QStringLiteral("http")) ||
      url.host().isEmpty()) {
    QMetaObject::invokeMethod(this, [this, id] {
      emit requestFailed(id, tr("The EPICS Archiver URL is invalid."));
    }, Qt::QueuedConnection);
    return id;
  }

  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::SameOriginRedirectPolicy);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("qtstriptool/") + applicationVersion());
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  request.setTransferTimeout(120000);
#endif
  auto* reply = network_->get(request);
  reply->setReadBufferSize(64 * 1024);
  auto state = std::make_shared<Request>();
  state->reply = reply;
  state->channel = channel;
  pending_.insert(id, state);
  connect(reply, &QNetworkReply::readyRead, this, [this, id] { readReply(id); });
  connect(reply, &QNetworkReply::metaDataChanged, this, [this, id, reply] {
    if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() >
        std::min(maximumResponseBytes, kMaximumHistoryResponseBytes))
      fail(id, tr("EPICS Archiver response exceeded the history byte limit."));
  });
  auto* timeout = new QTimer(reply);
  timeout->setSingleShot(true);
  connect(timeout, &QTimer::timeout, this, [this, id] {
    fail(id, tr("EPICS Archiver request timed out."));
  });
  timeout->start(std::max(1, requestTimeoutMs));
  connect(reply, &QNetworkReply::finished, this, [this, id, reply] {
    const auto state = pending_.value(id);
    if (!state) { reply->deleteLater(); return; }
    if (!readReply(id)) return;
    if (reply->error() != QNetworkReply::NoError) {
      fail(id, tr("EPICS Archiver request for %1 failed: %2")
                   .arg(state->channel, reply->errorString()));
      return;
    }
    state->downloaded = true;
    state->reply = nullptr;
    reply->deleteLater();
    pumpParser();
  });
  return id;
}

bool ArchiverHistoryProvider::readReply(HistoryRequestId id) {
  const auto state = pending_.value(id);
  if (!state || !state->reply) return false;
  while (state->reply->bytesAvailable() > 0) {
    const auto chunk = state->reply->read(64 * 1024);
    if (chunk.isEmpty()) break;
    if (chunk.size() > std::min(maximumResponseBytes, kMaximumHistoryResponseBytes) - state->bytes ||
        chunk.size() > maximumBufferedBytes - bufferedBytes_) {
      fail(id, tr("EPICS Archiver response exceeded the history byte/buffer limit."));
      return false;
    }
    state->content += chunk;
    state->bytes += chunk.size();
    bufferedBytes_ += chunk.size();
  }
  return true;
}

void ArchiverHistoryProvider::release(const std::shared_ptr<Request>& state) {
  state->cancelled = true;
  if (state != parsingRequest_) {
    bufferedBytes_ -= state->bytes;
    state->bytes = 0;
    state->content.clear();
  }
  if (auto reply = state->reply) {
    state->reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
  }
}

void ArchiverHistoryProvider::fail(HistoryRequestId id, const QString& message) {
  const auto state = pending_.take(id);
  if (!state) return;
  release(state);
  QPointer<ArchiverHistoryProvider> guard(this);
  emit requestFailed(id, message);
  if (guard) pumpParser();
}

void ArchiverHistoryProvider::cancel(HistoryRequestId id) {
  const auto state = pending_.take(id);
  if (!state) return;
  release(state);
  QPointer<ArchiverHistoryProvider> guard(this);
  emit requestCancelled(id);
  if (guard) pumpParser();
}

void ArchiverHistoryProvider::pumpParser() {
  if (pumping_) return;
  pumping_ = true;
  QPointer<ArchiverHistoryProvider> guard(this);
  pumpParserNow();
  if (guard) pumping_ = false;
}

void ArchiverHistoryProvider::pumpParserNow() {
  if (parsing_.valid()) {
    if (parsing_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto parsed = parsing_.get();
    const auto id = parsingId_;
    auto state = std::move(parsingRequest_);
    parsingId_ = 0;
    bufferedBytes_ -= state->bytes;
    state->bytes = 0;
    if (!state->cancelled && pending_.remove(id)) {
      QPointer<ArchiverHistoryProvider> guard(this);
      if (parsed.error.isEmpty()) emit resultReady(id, state->channel, std::move(parsed.samples));
      else emit requestFailed(id, tr("EPICS Archiver returned invalid JSON for %1: %2")
                                     .arg(state->channel, parsed.error));
      if (!guard) return;
    }
  }
  for (auto it = pending_.cbegin(); it != pending_.cend(); ++it) {
    const auto& state = it.value();
    if (!state->downloaded) continue;
    parsingId_ = it.key();
    parsingRequest_ = state;
    auto content = std::move(state->content);
    const auto limit = std::min(maximumSamples, kMaximumHistoryResponseSamples);
    try {
      parsing_ = std::async(std::launch::async, [content = std::move(content), limit] {
        Parsed result;
        try { result.samples = parseArchiverJson(content, &result.error, limit); }
        catch (const std::exception&) { result.error = QStringLiteral("History parsing failed"); }
        return result;
      });
    } catch (const std::exception&) {
      parsingRequest_.reset(); parsingId_ = 0;
      parseTimer_.start();
      fail(it.key(), tr("Unable to start the history parser."));
      return;
    }
    parseTimer_.start();
    return;
  }
  parseTimer_.stop();
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
