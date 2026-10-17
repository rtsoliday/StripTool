#include "core/application.h"
#include "core/command_line.h"
#include "core/config.h"
#include "core/model.h"
#include "core/sample_buffer.h"
#include "services/acquisition_manager.h"
#include "services/channel_access.h"
#include "services/cpu_usage_provider.h"
#include "services/export_service.h"
#include "services/file_workflow.h"
#include "services/history_provider.h"
#include "services/runtime_capabilities.h"
#include "ui/about_banner.h"
#include "ui/controls_window.h"
#include "ui/help.h"
#include "ui/history_dialog.h"
#include "ui/main_window.h"
#include "ui/plot_widget.h"
#include "ui/pv_drop.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QProcess>
#include <QDateTimeEdit>
#include <QIcon>
#include <QFileInfo>
#include <QImage>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QPrintPreviewDialog>
#include <QPrintPreviewWidget>
#include <QSignalSpy>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QSettings>
#include <QScopeGuard>
#include <QStyle>
#include <QTabWidget>
#include <QTextBrowser>
#include <QtMath>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QToolButton>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>

namespace striptool {
// Inject protocol callbacks without a network server; production ingress and
// GUI delivery still run through the actual provider.
struct ChannelAccessProviderTestAccess {
  static void sample(ChannelAccessProvider& p, ChannelId id, double value) {
    p.queueSample(id, {std::chrono::system_clock::now(), value, 0, 0});
  }
  static void connection(ChannelAccessProvider& p, ChannelId id, ConnectionState state) {
    p.queueConnection(id, state, QStringLiteral("test"));
  }
  static void metadata(ChannelAccessProvider& p, ChannelId id, ChannelMetadata metadata) {
    p.queueMetadata(id, std::move(metadata));
  }
  // Delivers a DBR_CTRL_DOUBLE reply through the real CA callback.
  static void control(ChannelAccessProvider& p, ChannelId id, const dbr_ctrl_double& value) {
    event_handler_args args{};
    args.usr = p.active_.value(id);
    args.type = DBR_CTRL_DOUBLE;
    args.count = 1;
    args.status = ECA_NORMAL;
    args.dbr = &value;
    ChannelAccessProvider::controlCallback(args);
  }
};
}

namespace {
class HistoryHttpServer final : public QTcpServer {
public:
  std::function<void(QTcpSocket*)> respond;
  HistoryHttpServer() {
    listen(QHostAddress::LocalHost);
    connect(this, &QTcpServer::newConnection, this, [this] {
      while (hasPendingConnections()) {
        auto* socket = nextPendingConnection();
        auto received = std::make_shared<QByteArray>();
        auto sent = std::make_shared<bool>(false);
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, received, sent] {
          *received += socket->readAll();
          if (!*sent && received->contains("\r\n\r\n")) {
            *sent = true;
            if (respond) respond(socket);
          }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      }
    });
  }
  QString root() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }
  static void send(QTcpSocket* socket, const QByteArray& body, bool length = true, bool close = true) {
    QByteArray headers = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n";
    if (length) headers += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    socket->write(headers + "\r\n" + body);
    socket->flush();
    if (close) socket->disconnectFromHost();
  }
};
std::filesystem::path fixture(const char* name) {
  return std::filesystem::path("../tests/fixtures/config") / name;
}

void moveWhileDragging(QWidget* widget, const QPoint& position,
                       Qt::MouseButton heldButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  QMouseEvent move(QEvent::MouseMove, QPointF(position),
                   QPointF(widget->mapToGlobal(position)), Qt::NoButton,
                   heldButton, Qt::NoModifier);
#else
  QMouseEvent move(QEvent::MouseMove, QPointF(position), Qt::NoButton,
                   heldButton, Qt::NoModifier);
#endif
  QApplication::sendEvent(widget, &move);
}

class FakeChannelProvider final : public striptool::ChannelProvider {
public:
  using ChannelProvider::ChannelProvider;
  striptool::ChannelId connectChannel(const QString& name) override {
    names.insert(nextId, name);
    return nextId++;
  }
  void disconnectChannel(striptool::ChannelId id) override {
    names.remove(id);
    emit connectionChanged(id, striptool::ConnectionState::Disconnected,
                           QStringLiteral("Disconnected"));
  }
  void publishConnection(striptool::ChannelId id, striptool::ConnectionState state) {
    emit connectionChanged(id, state, QStringLiteral("test"));
  }
  void publishMetadata(striptool::ChannelId id,
                       const striptool::ChannelMetadata& metadata) {
    emit metadataChanged(id, metadata);
  }
  void publishDescription(striptool::ChannelId id, const QString& description) {
    emit descriptionReceived(id, description);
  }
  void publishSample(striptool::ChannelId id, const striptool::Sample& sample) {
    emit sampleReceived(id, sample);
  }
  QHash<striptool::ChannelId, QString> names;
private:
  striptool::ChannelId nextId = 1;
};

class RecordingHistoryProvider final : public striptool::HistoryProvider {
public:
  using HistoryProvider::HistoryProvider;
  striptool::HistoryRequestId request(const QString& channel,
                                      striptool::TimeRange range) override {
    const auto id = nextId++;
    requests.push_back({id, channel, range});
    return id;
  }
  void cancel(striptool::HistoryRequestId id) override { cancelled.push_back(id); }
  void succeed(striptool::HistoryRequestId id, const QString& channel,
               std::vector<striptool::Sample> samples) {
    emit resultReady(id, channel, std::move(samples));
  }
  void fail(striptool::HistoryRequestId id) {
    emit requestFailed(id, QStringLiteral("deliberate test failure"));
  }
  struct Request {
    striptool::HistoryRequestId id;
    QString channel;
    striptool::TimeRange range;
  };
  std::vector<Request> requests;
  std::vector<striptool::HistoryRequestId> cancelled;
private:
  striptool::HistoryRequestId nextId = 1;
};
}

class SmokeTests final : public QObject {
  Q_OBJECT
private slots:
  void caInboxPreservesOrderedSamples() {
    striptool::ChannelAccessProvider provider;
    striptool::AcquisitionManager acquisition(&provider);
    const auto id = acquisition.addChannel(QStringLiteral("QTST:MEMORY:ORDER"), 2000);
    using Access = striptool::ChannelAccessProviderTestAccess;
    Access::connection(provider, id, striptool::ConnectionState::Connected);
    QTRY_COMPARE(acquisition.metadata(id).connection, striptool::ConnectionState::Connected);
    std::thread producer([&] {
      for (int i = 0; i < 1000; ++i) Access::sample(provider, id, i == 500 ? 10000 : i);
    });
    producer.join();
    QVERIFY(provider.pendingUpdateCount() <= provider.kMaximumPendingUpdates);
    QVERIFY(provider.pendingUpdateBytes() <= provider.kMaximumPendingBytes);
    QTRY_COMPARE(acquisition.buffer(id)->size(), std::size_t(1000));
    const auto samples = acquisition.buffer(id)->samples();
    for (int i = 0; i < 1000; ++i) QCOMPARE(samples[i].value, double(i == 500 ? 10000 : i));
    QCOMPARE(provider.pendingUpdateBytes(), std::size_t(0));
  }
  void caInboxOverflow_data() {
    QTest::addColumn<bool>("disconnected");
    QTest::newRow("connected") << false;
    QTest::newRow("disconnected") << true;
  }
  void caInboxOverflow() {
    QFETCH(bool, disconnected);
    striptool::ChannelAccessProvider provider;
    striptool::AcquisitionManager acquisition(&provider);
    const auto id = acquisition.addChannel(QStringLiteral("QTST:MEMORY:OVERFLOW"), 100);
    using Access = striptool::ChannelAccessProviderTestAccess;
    Access::connection(provider, id, striptool::ConnectionState::Connected);
    QTRY_COMPARE(acquisition.metadata(id).connection, striptool::ConnectionState::Connected);
    QSignalSpy lost(&provider, &striptool::ChannelProvider::dataLoss);
    const int count = int(provider.kMaximumPendingUpdates) * 2;
    std::thread producer([&] {
      for (int i = 0; i < count; ++i) Access::sample(provider, id, i);
      if (disconnected) Access::connection(provider, id, striptool::ConnectionState::Disconnected);
      striptool::ChannelMetadata metadata;
      metadata.connection = striptool::ConnectionState::Connected; // A stale CTRL response.
      Access::metadata(provider, id, metadata);
    });
    producer.join();
    QVERIFY(provider.pendingUpdateCount() <= provider.kMaximumPendingUpdates);
    QVERIFY(provider.pendingUpdateBytes() <= provider.kMaximumPendingBytes);
    QTRY_COMPARE(lost.count(), 1);
    const auto samples = acquisition.buffer(id)->samples();
    QVERIFY(!samples.empty());
    QVERIFY(!samples.front().plotable);
    QCOMPARE(acquisition.metadata(id).connection, disconnected ? striptool::ConnectionState::Disconnected
                                                             : striptool::ConnectionState::Connected);
    if (disconnected) QVERIFY(!samples.back().plotable);
    else { QVERIFY(samples.back().plotable); QCOMPARE(samples.back().value, double(count - 1)); }
    Access::sample(provider, id, 99);
    QTRY_COMPARE(acquisition.buffer(id)->latest()->value, 99.0);
  }
  void caInboxByteLimitAndRetirement() {
    striptool::ChannelAccessProvider provider;
    striptool::AcquisitionManager acquisition(&provider);
    const auto id = acquisition.addChannel(QStringLiteral("QTST:MEMORY:BYTES"));
    QCoreApplication::processEvents();
    QSignalSpy lost(&provider, &striptool::ChannelProvider::dataLoss);
    for (int i = 0; i < 3000; ++i) {
      striptool::ChannelMetadata metadata;
      metadata.description.assign(1024, 'x');
      striptool::ChannelAccessProviderTestAccess::metadata(provider, id, std::move(metadata));
    }
    QVERIFY(provider.pendingUpdateBytes() <= provider.kMaximumPendingBytes);
    QTRY_COMPARE(lost.count(), 1);
    connect(&provider, &striptool::ChannelProvider::dataLoss, &acquisition,
            [&](striptool::ChannelId channel, const QString&) { acquisition.removeChannel(channel); });
    for (std::size_t i = 0; i <= provider.kMaximumPendingUpdates; ++i)
      striptool::ChannelAccessProviderTestAccess::sample(provider, id, double(i));
    QTRY_COMPARE(provider.retainedStateCount(), std::size_t(0));
    QCOMPARE(provider.pendingUpdateBytes(), std::size_t(0));
    for (int i = 0; i < 200; ++i) {
      const auto channel = provider.connectChannel(QStringLiteral("QTST:MEMORY:RETIRED"));
      QCOMPARE(provider.retainedStateCount(), std::size_t(1));
      provider.disconnectChannel(channel);
      QCOMPARE(provider.retainedStateCount(), std::size_t(0));
    }
    QCOMPARE(provider.pendingUpdateCount(), std::size_t(0));
  }
  void sampleStorageShrinksAndReleases() {
    striptool::SampleBuffer buffer(10000);
    const auto start = std::chrono::system_clock::time_point{};
    for (int i = 0; i < 12000; ++i) buffer.append({start + std::chrono::seconds(i), double(i)});
    buffer.setCapacity(3);
    QCOMPARE(buffer.size(), std::size_t(3));
    QCOMPARE(buffer.samples().front().value, 11997.0);
    QCOMPARE(buffer.latest()->value, 11999.0);
    QVERIFY(buffer.reservedBytes() <= 3 * sizeof(striptool::Sample));
    buffer.clear();
    QCOMPARE(buffer.reservedBytes(), std::size_t(0));
    for (int i = 0; i < 8; ++i) buffer.append({start, double(i)});
    QCOMPARE(buffer.size(), std::size_t(3));
    QCOMPARE(buffer.latest()->value, 7.0);
    QVERIFY(buffer.reservedBytes() <= 3 * sizeof(striptool::Sample));
  }
  void boundedArchiveHistoryAndStorageRelease() {
    striptool::MainWindow window;
    auto* plot = window.plotWidget();
    auto* notice = window.findChild<QLabel*>(QStringLiteral("historyReductionNotice"));
    QVERIFY(notice && notice->isHidden());
    const auto maximum = plot->kMaximumHistorySamples;
    const auto start = std::chrono::system_clock::time_point{};
    std::vector<striptool::Sample> history;
    for (std::size_t i = 0; i < 4 * maximum; ++i)
      history.push_back({start + std::chrono::seconds(i), 0.0});
    history[maximum / 2].plotable = false;
    history[maximum].value = 10000;
    history[2 * maximum].value = -10000;
    plot->joinHistoricalSamples(0, history);
    QVERIFY(plot->historyReduced(0));
    QVERIFY(!notice->isHidden());
    const auto& retained = plot->curveSamples(0);
    QVERIFY(retained.size() <= maximum);
    QCOMPARE(retained.front().timestamp, history.front().timestamp);
    QCOMPARE(retained.back().timestamp, history.back().timestamp);
    QVERIFY(std::any_of(retained.begin(), retained.end(), [](const auto& s) { return s.value == 10000; }));
    QVERIFY(std::any_of(retained.begin(), retained.end(), [](const auto& s) { return s.value == -10000; }));
    QVERIFY(std::any_of(retained.begin(), retained.end(), [](const auto& s) { return !s.plotable; }));
    const auto copy = retained;
    for (int i = 0; i < 5; ++i) plot->joinHistoricalSamples(0, copy);
    QVERIFY(plot->retainedSampleBytes(0) <= 2 * maximum * sizeof(striptool::Sample));
    plot->clearCurveSamples(0);
    QCOMPARE(plot->retainedSampleBytes(0), std::size_t(0));
    QVERIFY(!plot->historyReduced(0));
    QVERIFY(notice->isHidden());
    plot->setCurveSamples(0, std::vector<striptool::Sample>(10000));
    plot->setCurveSamples(0, std::vector<striptool::Sample>(2));
    QVERIFY(plot->retainedSampleBytes(0) <= 4 * sizeof(striptool::Sample));
    plot->setCurveSamples(0, {});
    QCOMPARE(plot->retainedSampleBytes(0), std::size_t(0));
  }
  void archiveParserRejectsOversizedResponses() {
    QString error;
    QVERIFY(striptool::parseArchiverJson(
        QByteArray(int(striptool::kMaximumHistoryResponseBytes + 1), ' '), &error).empty());
    QVERIFY(error.contains("byte limit"));
    const QByteArray json = R"([{"data":[{"secs":1,"nanos":0,"val":1},{"secs":2,"nanos":0,"val":2},{}]}])";
    QVERIFY(striptool::parseArchiverJson(json, &error, 2).empty());
    QVERIFY(error.contains("sample limit"));
    QCOMPARE(striptool::parseArchiverJson(json, &error, 3).size(), std::size_t(2));
    QVERIFY(error.isEmpty());
  }
  void archiveDownloadByteLimit_data() {
    QTest::addColumn<bool>("contentLength");
    QTest::newRow("advertised-size") << true;
    QTest::newRow("streamed-size") << false;
  }
  void archiveDownloadByteLimit() {
    QFETCH(bool, contentLength);
    HistoryHttpServer server;
    QVERIFY(server.isListening());
    server.respond = [=](QTcpSocket* socket) {
      HistoryHttpServer::send(socket, QByteArray(4096, 'x'), contentLength);
    };
    striptool::ArchiverHistoryProvider provider(server.root());
    provider.maximumResponseBytes = 1024;
    QSignalSpy failed(&provider, &striptool::HistoryProvider::requestFailed);
    QSignalSpy ready(&provider, &striptool::HistoryProvider::resultReady);
    const auto now = std::chrono::system_clock::now();
    provider.request(QStringLiteral("test"), {now - std::chrono::hours(1), now});
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(failed[0][1].toString().contains("limit"));
    QCOMPARE(ready.count(), 0);
    QCOMPARE(provider.pendingRequestCount(), 0);
    QCOMPARE(provider.bufferedBytes(), qint64(0));
  }
  void archiveAggregateBudgetCancellationAndTimeout() {
    HistoryHttpServer server;
    QVERIFY(server.isListening());
    server.respond = [](QTcpSocket* socket) {
      HistoryHttpServer::send(socket, QByteArray(700, ' '), false, false);
    };
    striptool::ArchiverHistoryProvider provider(server.root());
    provider.maximumResponseBytes = 2048;
    provider.maximumBufferedBytes = 1000;
    provider.maximumRequests = 2;
    QSignalSpy failed(&provider, &striptool::HistoryProvider::requestFailed);
    QSignalSpy cancelled(&provider, &striptool::HistoryProvider::requestCancelled);
    const auto now = std::chrono::system_clock::now();
    const striptool::TimeRange range{now - std::chrono::hours(1), now};
    const auto a = provider.request(QStringLiteral("a"), range);
    const auto b = provider.request(QStringLiteral("b"), range);
    provider.request(QStringLiteral("c"), range);
    QTRY_COMPARE(failed.count(), 2); // Concurrent-request limit, then shared byte budget.
    QVERIFY(provider.bufferedBytes() <= provider.maximumBufferedBytes);
    QCOMPARE(provider.pendingRequestCount(), 1);
    provider.cancel(a); provider.cancel(b);
    QCOMPARE(cancelled.count(), 1);
    QCOMPARE(provider.bufferedBytes(), qint64(0));
    provider.requestTimeoutMs = 30;
    provider.request(QStringLiteral("timeout"), range);
    QTRY_COMPARE(failed.count(), 3);
    QVERIFY(failed.last()[1].toString().contains("timed out"));
    QCOMPARE(provider.pendingRequestCount(), 0);
    QCOMPARE(provider.bufferedBytes(), qint64(0));
  }
  void archiveWorkerParsingAndSampleLimit() {
    HistoryHttpServer server;
    QVERIFY(server.isListening());
    const QByteArray body = R"([{"data":[{"secs":1,"nanos":0,"val":1},{"secs":2,"nanos":0,"val":2}]}])";
    server.respond = [=](QTcpSocket* socket) { HistoryHttpServer::send(socket, body); };
    striptool::ArchiverHistoryProvider provider(server.root());
    QSignalSpy failed(&provider, &striptool::HistoryProvider::requestFailed);
    QSignalSpy ready(&provider, &striptool::HistoryProvider::resultReady);
    const auto now = std::chrono::system_clock::now();
    const striptool::TimeRange range{now - std::chrono::hours(1), now};
    provider.maximumSamples = 1;
    provider.request(QStringLiteral("sample-limit"), range);
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(failed[0][1].toString().contains("sample limit"));
    QCOMPARE(provider.bufferedBytes(), qint64(0));
    provider.maximumSamples = 10;
    provider.request(QStringLiteral("valid"), range);
    QTRY_COMPARE(ready.count(), 1);
    const auto samples = qvariant_cast<std::vector<striptool::Sample>>(ready[0][2]);
    QCOMPARE(samples.size(), std::size_t(2));
    QCOMPARE(samples.back().value, 2.0);
    QCOMPARE(provider.pendingRequestCount(), 0);
    QCOMPARE(provider.bufferedBytes(), qint64(0));
    auto closing = std::make_unique<striptool::ArchiverHistoryProvider>(server.root());
    bool delivered = false;
    connect(closing.get(), &striptool::HistoryProvider::resultReady, &server,
            [&] { delivered = true; closing.reset(); });
    closing->request(QStringLiteral("close-in-result-handler"), range);
    QTRY_VERIFY(delivered);
    QVERIFY(!closing);
  }
  void metadataIsConfigured() {
    QCOMPARE(QApplication::applicationName(), QStringLiteral("qtstriptool"));
    QCOMPARE(QApplication::organizationName(), QStringLiteral("EPICS"));
    QVERIFY(!striptool::qtVersion().isEmpty());
    QVERIFY(!striptool::epicsVersion().isEmpty());
    QVERIFY(striptool::versionText().contains(QStringLiteral("Qt StripTool")));
    QCOMPARE(QApplication::style()->objectName(), QStringLiteral("fusion"));
  }
  void mainWindowHasStableScaffold() {
    striptool::MainWindow window;
    QCOMPARE(window.objectName(), QStringLiteral("mainWindow"));
    QCOMPARE(window.windowTitle(), striptool::applicationName());
    QVERIFY(window.findChild<striptool::PlotWidget*>(QStringLiteral("plotArea")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("exitAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("aboutAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("pauseAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("autoScaleAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("resetAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("showControlsAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("graphOpenAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("graphSaveAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("exportTextAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("exportCsvAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("snapshotAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("printAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("printPreviewAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("historyAction")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("helpAction")));
    QVERIFY(!window.findChild<QAction*>(QStringLiteral("retryConnectionsAction")));
    auto* autoScale = window.findChild<QAction*>(QStringLiteral("autoScaleAction"));
    QVERIFY(autoScale->isCheckable());
    QVERIFY(autoScale->isChecked());
    QVERIFY(window.findChild<QAction*>(QStringLiteral("pauseAction"))->isCheckable());
    QVERIFY(window.findChild<QAction*>(QStringLiteral("graphOpenAction"))->isEnabled());
    QVERIFY(window.findChild<QAction*>(QStringLiteral("exportCsvAction"))->isEnabled());
    QVERIFY(window.controlsWindow());
  }
  void helpTopicsSearchAndWindowReuse() {
    striptool::MainWindow window;
    window.stopAcquisition();
    window.show();
    auto* helpAction = window.findChild<QAction*>(QStringLiteral("helpAction"));
    QVERIFY(helpAction->shortcuts().contains(QKeySequence(Qt::Key_F1)));
    helpAction->trigger();
    auto* help = window.findChild<QDialog*>(QStringLiteral("helpWindow"));
    QVERIFY(help);
    QVERIFY(help->isVisible());
    QVERIFY(!help->isModal());
    auto* topics = help->findChild<QListWidget*>(QStringLiteral("helpTopics"));
    auto* browser = help->findChild<QTextBrowser*>(QStringLiteral("helpBrowser"));
    auto* find = help->findChild<QLineEdit*>(QStringLiteral("helpFind"));
    auto* matches = help->findChild<QLabel*>(QStringLiteral("helpMatches"));
    auto* next = help->findChild<QToolButton*>(QStringLiteral("helpNext"));
    auto* previous = help->findChild<QToolButton*>(QStringLiteral("helpPrevious"));
    QVERIFY(topics && browser && find && matches && next && previous);
    QVERIFY(topics->count() >= 8);
    QVERIFY(browser->toPlainText().contains(QStringLiteral("QTSTRIPTOOL_ARCHIVER_URL")));
    QVERIFY(!next->isEnabled());
    const QString output = qEnvironmentVariable("QTSTRIPTOOL_VISUAL_OUTPUT");
    if (!output.isEmpty()) {
      QApplication::processEvents();
      const auto directory = std::filesystem::path(output.toStdString()).parent_path();
      QVERIFY(help->grab().save(QString::fromStdString((directory / "help-topics.png").string())));
    }
    // Search traverses the complete guide, rather than just the current topic.
    find->setText(QStringLiteral("ARCHIVER"));
    const auto selections = browser->extraSelections();
    QVERIFY(selections.size() > 2);
    QVERIFY(next->isEnabled());
    const auto initial = browser->textCursor().selectionStart();
    next->click();
    QVERIFY(browser->textCursor().selectionStart() != initial);
    previous->click();
    QCOMPARE(browser->textCursor().selectionStart(), initial);
    find->setFocus();
    QTest::keyClick(find, Qt::Key_Return);
    QVERIFY(help->isVisible());
    QVERIFY(browser->textCursor().selectionStart() != initial);
    QTest::keyClick(find, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(browser->textCursor().selectionStart(), initial);
    for (int i = 0; i < selections.size(); ++i) next->click();
    QCOMPARE(browser->textCursor().selectionStart(), initial);
    find->setText(QStringLiteral("archiver"));
    QCOMPARE(browser->extraSelections().size(), selections.size());
    find->setText(QStringLiteral("no-such-help-phrase-123456"));
    QCOMPARE(matches->text(), QStringLiteral("No matches"));
    QVERIFY(browser->extraSelections().isEmpty());
    QVERIFY(!browser->textCursor().hasSelection());
    QVERIFY(!previous->isEnabled());
    find->clear();
    QVERIFY(matches->text().isEmpty());

    window.findChild<QAction*>(QStringLiteral("keyboardShortcutsAction"))->trigger();
    QCOMPARE(window.findChild<QDialog*>(QStringLiteral("helpWindow")), help);
    QCOMPARE(topics->currentItem()->data(Qt::UserRole).toString(), QStringLiteral("keyboard-shortcuts"));
    // A list after a Markdown heading keeps only its lead-in bold.
    const auto mouseText = browser->document()->find(QStringLiteral("drag empty plot space"));
    QVERIFY(!mouseText.isNull());
    QVERIFY(mouseText.charFormat().fontWeight() < QFont::DemiBold);
    topics->setCurrentRow(2);
    QVERIFY(browser->verticalScrollBar()->value() > 0);
    browser->verticalScrollBar()->setValue(0);
    QCOMPARE(topics->currentRow(), 0);
    help->activateWindow();
    QTest::qWait(20);
    QTest::keyClick(browser, Qt::Key_F, Qt::ControlModifier);
    QTRY_VERIFY(find->hasFocus());
    find->setText(QStringLiteral("archiver"));
    const auto shortcutStart = browser->textCursor().position();
    QTest::keyClick(find, Qt::Key_F3);
    QVERIFY(browser->textCursor().position() != shortcutStart);
    QTest::keyClick(find, Qt::Key_F3, Qt::ShiftModifier);
    QCOMPARE(browser->textCursor().position(), shortcutStart);
    QTest::keyClick(find, Qt::Key_Escape);
    QVERIFY(!help->isVisible());
    helpAction->trigger();
    QCOMPARE(window.findChildren<QDialog*>(QStringLiteral("helpWindow")).size(), 1);
    QVERIFY(help->isVisible());
    help->close();

    // The Controls window has the same menu and opens its relevant topic.
    auto* controls = window.controlsWindow();
    controls->findChild<QAction*>(QStringLiteral("controlsHelpAction"))->trigger();
    auto* controlHelp = controls->findChild<QDialog*>(QStringLiteral("helpWindow"));
    QVERIFY(controlHelp && controlHelp->isVisible());
    QCOMPARE(controlHelp->findChild<QListWidget*>(QStringLiteral("helpTopics"))
                 ->currentItem()->data(Qt::UserRole).toString(), QStringLiteral("configure-curves"));
    QVERIFY(controls->findChild<QAction*>(QStringLiteral("controlsKeyboardShortcutsAction")));
    QVERIFY(controls->findChild<QAction*>(QStringLiteral("controlsOnlineManualAction")));
    controlHelp->findChild<QPushButton*>(QStringLiteral("helpClose"))->click();
    QVERIFY(!controlHelp->isVisible());
  }
  void aboutDialogCreditsAndHelpHandoff() {
    striptool::MainWindow window;
    window.stopAcquisition();
    for (QWidget* owner : {static_cast<QWidget*>(&window),
                           static_cast<QWidget*>(window.controlsWindow())}) {
      QString credits, footer, title;
      bool bannerAnimating = false;
      bool lastCreditVisible = false;
      bool creditsFitWithoutGap = false;
      bool imageSaved = true;
      const QString output = qEnvironmentVariable("QTSTRIPTOOL_VISUAL_OUTPUT");
      QTimer::singleShot(0, owner, [&] {
        auto* dialog = owner->findChild<QDialog*>(QStringLiteral("aboutDialog"));
        if (!dialog) return;
        if (auto* browser = dialog->findChild<QTextBrowser*>(QStringLiteral("aboutCredits")))
          credits = browser->toPlainText();
        if (auto* label = dialog->findChild<QLabel*>(QStringLiteral("aboutFooter"))) footer = label->text();
        if (auto* label = dialog->findChild<QLabel*>(QStringLiteral("aboutTitle"))) title = label->text();
        if (auto* timer = dialog->findChild<QTimer*>(QStringLiteral("aboutAnimationTimer")))
          bannerAnimating = timer->isActive();
        if (!output.isEmpty()) {
          const auto directory = std::filesystem::path(output.toStdString()).parent_path();
          imageSaved = dialog->grab().save(QString::fromStdString((directory / "about.png").string()));
        }
        if (auto* browser = dialog->findChild<QTextBrowser*>(QStringLiteral("aboutCredits"))) {
          // The dialog is sized to its credits, not left with a large blank area.
          creditsFitWithoutGap = browser->viewport()->height()
              <= qCeil(browser->document()->size().height()) + 40;
          const auto last = browser->document()->find(QStringLiteral("Vladimir Romano"));
          browser->setTextCursor(last);
          browser->ensureCursorVisible();
          lastCreditVisible = !last.isNull()
              && browser->cursorRect(last).top() >= 0
              && browser->cursorRect(last).bottom() <= browser->viewport()->height();
        }
        if (auto* open = dialog->findChild<QPushButton*>(QStringLiteral("aboutOpenHelp"))) open->click();
        else dialog->reject();
      });
      const QString action = owner == &window ? QStringLiteral("aboutAction")
          : QStringLiteral("controlsAboutAction");
      owner->findChild<QAction*>(action)->trigger();
      QCOMPARE(title, striptool::applicationName());
      for (const auto& name : {"Robert Soliday", "Janet Anderson", "Christopher A. Larrieu",
                               "Albert Kagarmanov", "Kenneth Evans", "Deb Kirstens", "Vladimir Romano"})
        QVERIFY(credits.contains(QLatin1String(name)));
      QVERIFY(footer.contains(striptool::qtVersion()));
      QVERIFY(footer.contains(striptool::epicsVersion()));
      QVERIFY(bannerAnimating);
      QVERIFY(lastCreditVisible);
      QVERIFY(creditsFitWithoutGap);
      QVERIFY(imageSaved);
      auto* help = owner->findChild<QDialog*>(QStringLiteral("helpWindow"));
      QVERIFY(help && help->isVisible());
      QVERIFY(!QApplication::activeModalWidget());
      help->close();
    }
  }
  void aboutBannerClickScrollsMessage() {
    striptool::AboutBanner banner;
    banner.resize(banner.sizeHint());
    banner.show();
    QVERIFY(banner.message().isEmpty());
    QTest::mouseClick(&banner, Qt::LeftButton);
    const QString first = banner.message();
    QCOMPARE(first, QStringLiteral("Happy trending!"));
    // Clicking again does not interrupt a message that is still scrolling.
    QTest::mouseClick(&banner, Qt::LeftButton);
    QCOMPARE(banner.message(), first);
    QTest::qWait(900);
    const QImage image = banner.grab().toImage();
    QVERIFY(!image.isNull());
    const QString output = qEnvironmentVariable("QTSTRIPTOOL_VISUAL_OUTPUT");
    if (!output.isEmpty()) {
      const auto directory = std::filesystem::path(output.toStdString()).parent_path();
      QVERIFY(image.save(QString::fromStdString((directory / "about-banner-message.png").string())));
    }
    QCOMPARE(banner.message(), first);
    // Hiding ends the message; the next click shows the next one.
    banner.hide();
    QVERIFY(banner.message().isEmpty());
    banner.show();
    QTest::mouseClick(&banner, Qt::LeftButton);
    QVERIFY(!banner.message().isEmpty());
    QVERIFY(banner.message() != first);
  }
  void helpManualUsesSiteOverride() {
    const bool wasSet = qEnvironmentVariableIsSet("STRIP_HELP_PATH");
    const QByteArray original = qgetenv("STRIP_HELP_PATH");
    const auto restore = qScopeGuard([&] {
      if (wasSet) qputenv("STRIP_HELP_PATH", original);
      else qunsetenv("STRIP_HELP_PATH");
    });
    qunsetenv("STRIP_HELP_PATH");
    QCOMPARE(striptool::helpManualUrl(), QUrl(QStringLiteral("https://ops.aps.anl.gov/manuals/QtStripTool/")));
    qputenv("STRIP_HELP_PATH", "https://example.org/site-guide#curves");
    QCOMPARE(striptool::helpManualUrl(), QUrl(QStringLiteral("https://example.org/site-guide#curves")));
    qputenv("STRIP_HELP_PATH", "site help/index.html");
    QCOMPARE(striptool::helpManualUrl(), QUrl::fromLocalFile(QFileInfo(QStringLiteral("site help/index.html")).absoluteFilePath()));
    qputenv("STRIP_HELP_PATH", "");
    QVERIFY(striptool::helpManualUrl().scheme() == QStringLiteral("https"));
  }
  void pvDropsAddCurvesOnBothScreens() {
    striptool::MainWindow window;
    window.stopAcquisition();
    window.show();
    window.showControls();
    auto* controls = window.controlsWindow();
    auto* pendingName = controls->findChild<QLineEdit*>(QStringLiteral("curveName5"));
    pendingName->setText(QStringLiteral("pending:edit"));
    auto drop = [](QWidget* target, const QString& text) {
      QMimeData mime;
      mime.setText(text);
      QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime,
                            Qt::MiddleButton, Qt::NoModifier);
      QApplication::sendEvent(target, &enter);
      if (!enter.isAccepted()) return false;
      // Model an external source that releases its selection at mouse-up.
      mime.clear();
      QDropEvent event(QPointF(10, 10), Qt::CopyAction, &mime,
                       Qt::MiddleButton, Qt::NoModifier);
      QApplication::sendEvent(target, &event);
      return event.isAccepted() && event.dropAction() == Qt::CopyAction;
    };
    // Drop over a line editor must add curves instead of inserting PV text.
    QVERIFY(drop(pendingName, QStringLiteral("test:first test:second test:first")));
    QCOMPARE(window.model().curves[0].name, std::string("test:first"));
    QCOMPARE(window.model().curves[1].name, std::string("test:second"));
    QVERIFY(!window.model().curves[2].nameSet);
    QCOMPARE(pendingName->text(), QStringLiteral("pending:edit"));
    QVERIFY(drop(window.plotWidget(), QStringLiteral("test:third\ntest:fourth")));
    QCOMPARE(window.plotWidget()->model().curves[2].name, std::string("test:third"));
    QVERIFY(window.plotWidget()->model().curves[2].plotted);
    QVERIFY(drop(controls, QStringLiteral("test:third")));
    QVERIFY(!window.model().curves[4].nameSet);
    QVERIFY(!drop(controls, QString(64, QLatin1Char('x'))));
    QVERIFY(!drop(controls, QStringLiteral("invalid") + QChar(0)));
    window.stopAcquisition();
  }
  void pvDropCapacityIsTransactionalAndReplotsExistingCurves() {
    auto model = striptool::makeDefaultModel();
    striptool::ControlsWindow controls(&model);
    QSignalSpy acquisition(&controls,
                            &striptool::ControlsWindow::acquisitionConfigurationChanged);
    QStringList names;
    for (int i = 0; i < 9; ++i) names.append(QStringLiteral("test:%1").arg(i));
    QVERIFY(controls.addPvs(names));
    QCOMPARE(acquisition.count(), 1);
    QVERIFY(!controls.addPvs({QStringLiteral("test:9"), QStringLiteral("test:10")}));
    QVERIFY(!model.curves[9].nameSet);
    QCOMPARE(acquisition.count(), 1);
    model.curves[0].plotted = false;
    QVERIFY(controls.addPvs({QStringLiteral("test:0")}));
    QVERIFY(model.curves[0].plotted);
    QCOMPARE(acquisition.count(), 1);
    QVERIFY(controls.addPvs({QStringLiteral("test:9")}));
    QVERIFY(!controls.addPvs({QStringLiteral("test:10")}));
    QCOMPARE(model.curves[9].name, std::string("test:9"));
  }
  void qtedmMiddleButtonDropsReachBothScreens() {
    const QString qtedm = qEnvironmentVariable("QTSTRIPTOOL_TEST_QTEDM");
    const QString driver = qEnvironmentVariable("QTSTRIPTOOL_TEST_DRAG_DRIVER");
    if (qtedm.isEmpty() || driver.isEmpty() ||
        QApplication::platformName() != QStringLiteral("xcb"))
      QSKIP("Run test-qtedm-drop under X11 with QtEDM and the mouse driver.");
    striptool::MainWindow window;
    window.move(400, 200);
    window.controlsWindow()->move(400, 200);
    QProcess source;
    const auto stopSource = qScopeGuard([&source] {
      source.terminate();
      if (!source.waitForFinished(3000)) {
        source.kill();
        source.waitForFinished(3000);
      }
    });
    source.start(qtedm, {QStringLiteral("-x"),
                         QFileInfo(QStringLiteral("tests/pv_drag_source.adl")).absoluteFilePath()});
    QVERIFY(source.waitForStarted());
    for (QWidget* target : {static_cast<QWidget*>(window.controlsWindow()),
                             static_cast<QWidget*>(&window)}) {
      target->show();
      QApplication::processEvents();
      QProcess mouse;
      mouse.start(driver, {QString::number(target->winId())});
      QVERIFY(mouse.waitForStarted());
      QTRY_VERIFY_WITH_TIMEOUT(mouse.state() == QProcess::NotRunning, 10000);
      QCOMPARE(mouse.exitCode(), 0);
      QTRY_VERIFY_WITH_TIMEOUT(window.model().curves[0].nameSet, 3000);
      QCOMPARE(window.model().curves[0].name, std::string("qtedm:drop"));
      QVERIFY(window.plotWidget()->model().curves[0].plotted);
      window.controlsWindow()->findChild<QPushButton*>(QStringLiteral("curveRemove0"))->click();
      target->hide();
    }
    source.terminate();
    QVERIFY(source.waitForFinished());
    window.stopAcquisition();
  }
  void motifPvDropsUseRealMotifSource() {
    const QString source = qEnvironmentVariable("QTSTRIPTOOL_TEST_MOTIF_SOURCE");
    if (source.isEmpty() || QApplication::platformName() != QStringLiteral("xcb"))
      QSKIP("Run test-motif under X11 with the Motif drag source helper.");
    striptool::MainWindow window;
    window.show();
    window.showControls();
    QApplication::processEvents();
    int curve = 0;
    for (QWidget* target : {static_cast<QWidget*>(window.controlsWindow()),
                             static_cast<QWidget*>(&window)}) {
      QProcess sender;
      const QString pv = QStringLiteral("motif:test%1").arg(curve);
      sender.start(source, {QString::number(target->winId()), pv});
      QVERIFY(sender.waitForStarted());
      QTRY_VERIFY_WITH_TIMEOUT(window.model().curves[std::size_t(curve)].nameSet, 10000);
      QCOMPARE(window.model().curves[std::size_t(curve)].name, pv.toStdString());
      QVERIFY(window.plotWidget()->model().curves[std::size_t(curve)].plotted);
      QTRY_VERIFY_WITH_TIMEOUT(sender.state() == QProcess::NotRunning, 10000);
      QCOMPARE(sender.exitCode(), 0);
      ++curve;
    }
    QStringList remaining;
    for (int i = curve; i < int(striptool::kMaximumCurves); ++i)
      remaining.append(QStringLiteral("motif:fill%1").arg(i));
    QVERIFY(window.controlsWindow()->addPvs(remaining));
    QProcess rejected;
    rejected.start(source, {QString::number(window.winId()),
                            QStringLiteral("motif:overflow")});
    QVERIFY(rejected.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(rejected.state() == QProcess::NotRunning, 10000);
    QCOMPARE(rejected.exitCode(), 1);
    QCOMPARE(window.model().curves.back().name, std::string("motif:fill9"));
    window.stopAcquisition();
  }
  void controlsWindowEditsTheSharedModel() {
    auto model = striptool::makeDefaultModel();
    striptool::ControlsWindow controls(&model);
    QCOMPARE(controls.objectName(), QStringLiteral("controlsWindow"));
    QVERIFY(controls.width() >= 1200);
    auto* tabs = controls.findChild<QTabWidget*>(QStringLiteral("controlsTabs"));
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    for (int i = 0; i < int(striptool::kMaximumCurves); ++i) {
      auto* curveName = controls.findChild<QLineEdit*>(QStringLiteral("curveName") +
                                                       QString::number(i));
      QVERIFY(curveName);
      QVERIFY(curveName->minimumWidth() >= 220);
      QVERIFY(controls.findChild<QPushButton*>(QStringLiteral("curveModify") +
                                               QString::number(i)));
      QVERIFY(controls.findChild<QPushButton*>(QStringLiteral("curveRemove") +
                                               QString::number(i)));
    }
    QVERIFY(controls.findChild<QAction*>(QStringLiteral("openAction")));
    QVERIFY(controls.findChild<QAction*>(QStringLiteral("saveAction")));
    QVERIFY(controls.findChild<QAction*>(QStringLiteral("saveAsAction")));
    QVERIFY(controls.findChild<QAction*>(QStringLiteral("showGraphAction")));
    QVERIFY(controls.findChild<QAction*>(QStringLiteral("controlsAboutAction")));

    QSignalSpy changed(&controls, &striptool::ControlsWindow::modelChanged);
    QSignalSpy acquisition(
        &controls, &striptool::ControlsWindow::acquisitionConfigurationChanged);
    auto* entry = controls.findChild<QLineEdit*>(QStringLiteral("pvEntry"));
    entry->setText(QStringLiteral("test:controls:pv"));
    controls.findChild<QPushButton*>(QStringLiteral("connectButton"))->click();
    QVERIFY(model.curves[0].nameSet);
    QCOMPARE(QString::fromStdString(model.curves[0].name),
             QStringLiteral("test:controls:pv"));
    QVERIFY(model.curves[0].plotted);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(acquisition.count(), 1);

    controls.findChild<QLineEdit*>(QStringLiteral("curveMinimum0"))
        ->setText(QStringLiteral("1.25"));
    controls.findChild<QLineEdit*>(QStringLiteral("curveMaximum0"))
        ->setText(QStringLiteral("24.5"));
    controls.findChild<QComboBox*>(QStringLiteral("curveScale0"))->setCurrentIndex(1);
    controls.findChild<QPushButton*>(QStringLiteral("curveModify0"))->click();
    QCOMPARE(model.curves[0].minimum, 1.25);
    QCOMPARE(model.curves[0].maximum, 24.5);
    QCOMPARE(model.curves[0].scale, striptool::ScaleMode::Log10);
    QCOMPARE(acquisition.count(), 1);

    controls.findChild<QSpinBox*>(QStringLiteral("sampleCount"))->setValue(2048);
    QCOMPARE(model.timing.numberOfSamples, 2048);
    QCOMPARE(acquisition.count(), 2);
    controls.findChild<QComboBox*>(QStringLiteral("xGridMode"))->setCurrentIndex(2);
    QCOMPARE(model.graph.xGrid, striptool::GridMode::All);
    QCOMPARE(acquisition.count(), 2);

    controls.findChild<QPushButton*>(QStringLiteral("curveRemove0"))->click();
    QVERIFY(!model.curves[0].nameSet);
    QCOMPARE(acquisition.count(), 3);
  }
  void typedTimingValuesApplyOnceWhenEntered() {
    // Intermediate values such as the "9" of "9000" must not resize buffers.
    auto model = striptool::makeDefaultModel();
    striptool::ControlsWindow controls(&model);
    QSignalSpy acquisition(&controls,
                           &striptool::ControlsWindow::acquisitionConfigurationChanged);
    for (const auto& [name, typed] :
         {std::pair{QStringLiteral("sampleCount"), QStringLiteral("9000")},
          std::pair{QStringLiteral("timespanSeconds"), QStringLiteral("3600")}}) {
      auto* editor = controls.findChild<QSpinBox*>(name);
      QVERIFY(editor);
      acquisition.clear();
      editor->selectAll();
      QTest::keyClicks(editor, typed);
      QCOMPARE(acquisition.count(), 0);
      QTest::keyClick(editor, Qt::Key_Return);
      QCOMPARE(acquisition.count(), 1);
    }
    QCOMPARE(model.timing.numberOfSamples, 9000);
    QCOMPARE(model.timing.timespanSeconds, 3600U);
  }
  void plotRendersModelTitle() {
    auto model = striptool::makeDefaultModel();
    model.title = "Beam Study";
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);

    QImage image(plot.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    plot.render(&image);

    int titlePixels = 0;
    for (int y = 6; y < 30; ++y)
      for (int x = 72; x < 622; ++x) {
        const QColor pixel = image.pixelColor(x, y);
        if (pixel.red() < 120 && pixel.green() < 120 && pixel.blue() < 120)
          ++titlePixels;
      }
    QVERIFY2(titlePixels > 20, "The model title was not rendered above the plot");
  }
  void appearanceTitleEditsAndPersists() {
    auto model = striptool::makeDefaultModel();
    QVERIFY(model.title.empty());
    striptool::ControlsWindow controls(&model);
    auto* title = controls.findChild<QLineEdit*>(QStringLiteral("plotTitle"));
    QVERIFY(title);
    QVERIFY(title->text().isEmpty());
    QSignalSpy changed(&controls, &striptool::ControlsWindow::modelChanged);
    title->setText(QStringLiteral("Beam Study"));
    emit title->editingFinished();
    QCOMPARE(model.title, std::string("Beam Study"));
    QCOMPARE(changed.count(), 1);

    std::ostringstream output;
    QVERIFY(striptool::writeConfiguration(output, model));
    QVERIFY(output.str().find("Strip.Title") != std::string::npos);
    auto roundTripped = striptool::makeDefaultModel();
    std::istringstream input(output.str());
    QVERIFY(striptool::readConfiguration(input, roundTripped).success);
    QCOMPARE(roundTripped.title, model.title);
  }
  void curveLimitEditorsFitLongValues() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].minimum = -1.234567890123456e+100;
    model.curves[0].maximum = 1.234567890123456e+100;
    striptool::ControlsWindow controls(&model);
    auto* minimum = controls.findChild<QLineEdit*>(QStringLiteral("curveMinimum0"));
    auto* maximum = controls.findChild<QLineEdit*>(QStringLiteral("curveMaximum0"));
    QVERIFY(minimum);
    QVERIFY(maximum);
    QCOMPARE(minimum->text(), QString::number(model.curves[0].minimum, 'g', 16));
    QCOMPARE(maximum->text(), QString::number(model.curves[0].maximum, 'g', 16));
    QVERIFY(minimum->minimumWidth() >=
            minimum->fontMetrics().horizontalAdvance(minimum->text()) + 20);
    QVERIFY(maximum->minimumWidth() >=
            maximum->fontMetrics().horizontalAdvance(maximum->text()) + 20);
  }
  void returnInCurveFieldsAppliesModify() {
    auto model = striptool::makeDefaultModel();
    striptool::ControlsWindow controls(&model);
    controls.show();
    auto* name = controls.findChild<QLineEdit*>(QStringLiteral("curveName0"));
    auto* minimum = controls.findChild<QLineEdit*>(QStringLiteral("curveMinimum0"));
    auto* maximum = controls.findChild<QLineEdit*>(QStringLiteral("curveMaximum0"));
    QVERIFY(name);
    QVERIFY(minimum);
    QVERIFY(maximum);
    QSignalSpy changed(&controls, &striptool::ControlsWindow::modelChanged);
    QSignalSpy acquisition(
        &controls, &striptool::ControlsWindow::acquisitionConfigurationChanged);

    name->setText(QStringLiteral("first:pv"));
    QTest::keyClick(name, Qt::Key_Return);
    QCOMPARE(model.curves[0].name, std::string("first:pv"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(acquisition.count(), 1);

    minimum->setText(QStringLiteral("2.5"));
    QTest::keyClick(minimum, Qt::Key_Return);
    QCOMPARE(model.curves[0].minimum, 2.5);
    QVERIFY(model.curves[0].minimumSet);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(acquisition.count(), 1);

    maximum->setText(QStringLiteral("25"));
    QTest::keyClick(maximum, Qt::Key_Return);
    QCOMPARE(model.curves[0].maximum, 25.0);
    QVERIFY(model.curves[0].maximumSet);
    QCOMPARE(changed.count(), 3);
    QCOMPARE(acquisition.count(), 1);

    name->setText(QStringLiteral("replacement:pv"));
    QTest::keyClick(name, Qt::Key_Return);
    QCOMPARE(model.curves[0].name, std::string("replacement:pv"));
    QCOMPARE(acquisition.count(), 2);
  }
  void curveLimitEditorsPreserveScientificValues() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "test:limits";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 1e-50;
    model.curves[0].maximum = 1e50;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    striptool::ControlsWindow controls(&model);
    auto* minimum = controls.findChild<QLineEdit*>(QStringLiteral("curveMinimum0"));
    auto* maximum = controls.findChild<QLineEdit*>(QStringLiteral("curveMaximum0"));
    QCOMPARE(minimum->text().toDouble(), 1e-50);
    QCOMPARE(maximum->text().toDouble(), 1e50);
    controls.findChild<QPushButton*>(QStringLiteral("curveModify0"))->click();
    QCOMPARE(model.curves[0].minimum, 1e-50);
    QCOMPARE(model.curves[0].maximum, 1e50);
    minimum->setText(QStringLiteral("2e-50"));
    controls.findChild<QPushButton*>(QStringLiteral("curveModify0"))->click();
    QCOMPARE(model.curves[0].minimum, 2e-50);
    QCOMPARE(model.curves[0].maximum, 1e50);
  }
  void metadataRefreshPreservesPendingCurveEdits() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "original:pv";
    model.curves[0].nameSet = true;
    striptool::ControlsWindow controls(&model);
    auto* name = controls.findChild<QLineEdit*>(QStringLiteral("curveName0"));
    auto* precision = controls.findChild<QSpinBox*>(QStringLiteral("curvePrecision0"));
    auto* minimum = controls.findChild<QLineEdit*>(QStringLiteral("curveMinimum0"));
    auto* maximum = controls.findChild<QLineEdit*>(QStringLiteral("curveMaximum0"));
    name->setText(QStringLiteral("pending:pv"));
    precision->setValue(6);
    minimum->setText(QStringLiteral("3.5"));
    model.curves[0].precision = 9;
    model.curves[0].minimum = 2.0;
    model.curves[0].maximum = 20.0;
    controls.refreshCurveMetadata(0);
    QCOMPARE(name->text(), QStringLiteral("pending:pv"));
    QCOMPARE(precision->value(), 6);
    QCOMPARE(minimum->text(), QStringLiteral("3.5"));
    QCOMPARE(maximum->text().toDouble(), 20.0);
    controls.findChild<QPushButton*>(QStringLiteral("curveModify0"))->click();
    QCOMPARE(model.curves[0].name, std::string("pending:pv"));
    QCOMPARE(model.curves[0].precision, 6);
    QCOMPARE(model.curves[0].minimum, 3.5);
    QCOMPARE(model.curves[0].maximum, 20.0);
  }
  void reopeningControlsPreservesPendingEdits() {
    striptool::MainWindow window;
    auto* controls = window.controlsWindow();
    auto* name = controls->findChild<QLineEdit*>(QStringLiteral("curveName0"));
    name->setText(QStringLiteral("draft:pv"));
    window.showControls();
    controls->hide();
    window.showControls();
    QCOMPARE(name->text(), QStringLiteral("draft:pv"));
    QVERIFY(!window.model().curves[0].nameSet);
  }
  void removingCurvePreservesOtherPendingEdits() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "remove:pv";
    model.curves[0].nameSet = true;
    model.curves[1].name = "keep:pv";
    model.curves[1].nameSet = true;
    striptool::ControlsWindow controls(&model);
    auto* pendingName = controls.findChild<QLineEdit*>(QStringLiteral("curveName1"));
    auto* pendingMinimum = controls.findChild<QLineEdit*>(QStringLiteral("curveMinimum1"));
    pendingName->setText(QStringLiteral("draft:pv"));
    pendingMinimum->setText(QStringLiteral("2.5"));
    controls.findChild<QPushButton*>(QStringLiteral("curveRemove0"))->click();
    QVERIFY(!model.curves[0].nameSet);
    QCOMPARE(pendingName->text(), QStringLiteral("draft:pv"));
    QCOMPARE(pendingMinimum->text(), QStringLiteral("2.5"));
    controls.findChild<QPushButton*>(QStringLiteral("curveModify1"))->click();
    QCOMPARE(model.curves[1].name, std::string("draft:pv"));
    QCOMPARE(model.curves[1].minimum, 2.5);
  }
  void mainAndControlWindowsShareOneModel() {
    striptool::MainWindow window;
    auto* controls = window.controlsWindow();
    auto* lineWidth = controls->findChild<QSpinBox*>(QStringLiteral("lineWidth"));
    lineWidth->setValue(7);
    QCOMPARE(window.model().graph.lineWidth, 7);
    QCOMPARE(window.plotWidget()->model().graph.lineWidth, 7);
    window.plotWidget()->addAnnotation({std::chrono::system_clock::now(),
                                        std::nullopt, "shared annotation"});
    QCOMPARE(window.model().annotations.size(), std::size_t{1});
    lineWidth->setValue(8);
    QCOMPARE(window.plotWidget()->model().annotations.size(), std::size_t{1});
    window.showControls();
    QVERIFY(controls->isVisible());
    controls->findChild<QAction*>(QStringLiteral("showGraphAction"))->trigger();
    QVERIFY(window.isVisible());
  }
  void localChannelMetadataReachesControlsAndGraph() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "CPU_Usage";
    model.curves[0].nameSet = true;
    striptool::MainWindow window(model);
    window.startAcquisition();
    QTRY_COMPARE(QString::fromStdString(window.model().curves[0].units),
                 QStringLiteral("percent"));
    QCOMPARE(window.plotWidget()->model().curves[0].maximum, 100.0);
    QVERIFY(window.model().curves[0].unitsDiscovered);
    QVERIFY(window.model().curves[0].precisionDiscovered);
    QVERIFY(window.model().curves[0].minimumDiscovered);
    QVERIFY(window.model().curves[0].maximumDiscovered);
    QVERIFY(!window.model().curves[0].minimumSet);
    QVERIFY(!window.model().curves[0].maximumSet);
    QCOMPARE(window.controlsWindow()->findChild<QLabel*>(QStringLiteral("curveStatus0"))->text(),
             QStringLiteral("Live"));
    window.stopAcquisition();
    QCOMPARE(window.controlsWindow()->findChild<QLabel*>(QStringLiteral("curveStatus0"))->text(),
             QStringLiteral("Offline"));
  }
  void connectButtonKeepsUntouchedLimitsAutomatic() {
    striptool::MainWindow window;
    window.startAcquisition();
    auto* controls = window.controlsWindow();
    controls->findChild<QLineEdit*>(QStringLiteral("pvEntry"))
        ->setText(QStringLiteral("CPU_Usage"));
    controls->findChild<QPushButton*>(QStringLiteral("connectButton"))->click();
    QVERIFY(window.model().curves[0].nameSet);
    QVERIFY(!window.model().curves[0].minimumSet);
    QVERIFY(!window.model().curves[0].maximumSet);
    QTRY_COMPARE(QString::fromStdString(window.model().curves[0].units),
                 QStringLiteral("percent"));
    QCOMPARE(window.model().curves[0].minimum, 0.0);
    QCOMPARE(window.model().curves[0].maximum, 100.0);
    controls->findChild<QLineEdit*>(QStringLiteral("curveMinimum0"))
        ->setText(QStringLiteral("10"));
    controls->findChild<QPushButton*>(QStringLiteral("curveModify0"))->click();
    QVERIFY(window.model().curves[0].minimumSet);
    QCOMPARE(window.model().curves[0].minimum, 10.0);
  }
  void addingPvSilentlyBackfillsFiveMinutesFromArchiver() {
    striptool::MainWindow window;
    auto provider = std::make_unique<RecordingHistoryProvider>();
    auto* recording = provider.get();
    window.setHistoryProvider(std::move(provider));
    window.startAcquisition();
    auto* controls = window.controlsWindow();
    controls->findChild<QLineEdit*>(QStringLiteral("pvEntry"))
        ->setText(QStringLiteral("test:automatic-history"));
    const auto before = std::chrono::system_clock::now();
    controls->findChild<QPushButton*>(QStringLiteral("connectButton"))->click();
    const auto after = std::chrono::system_clock::now();
    QCOMPARE(recording->requests.size(), std::size_t{1});
    const auto request = recording->requests.front();
    QCOMPARE(request.channel, QStringLiteral("test:automatic-history"));
    QCOMPARE(request.range.end - request.range.start, std::chrono::minutes(5));
    QVERIFY(request.range.end >= before && request.range.end <= after);

    const auto archived = request.range.end - std::chrono::minutes(1);
    recording->succeed(request.id, request.channel,
                       {{archived, 42.0, 3, 2}});
    QCOMPARE(window.plotWidget()->curveSamples(0).size(), std::size_t{1});
    QCOMPARE(window.plotWidget()->curveSamples(0).front().timestamp, archived);
    QCOMPARE(window.plotWidget()->curveSamples(0).front().value, 42.0);

    controls->findChild<QLineEdit*>(QStringLiteral("pvEntry"))
        ->setText(QStringLiteral("test:automatic-failure"));
    controls->findChild<QPushButton*>(QStringLiteral("connectButton"))->click();
    QCOMPARE(recording->requests.size(), std::size_t{2});
    recording->fail(recording->requests.back().id);
    QVERIFY(window.findChildren<QMessageBox*>().isEmpty());

    controls->findChild<QLineEdit*>(QStringLiteral("pvEntry"))
        ->setText(QStringLiteral("CPU_Usage"));
    controls->findChild<QPushButton*>(QStringLiteral("connectButton"))->click();
    QCOMPARE(recording->requests.size(), std::size_t{2});
  }
  void zoomingOutExtendsArchivedHistory() {
    striptool::MainWindow window;
    auto provider = std::make_unique<RecordingHistoryProvider>();
    auto* recording = provider.get();
    window.setHistoryProvider(std::move(provider));
    window.resize(1000, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.startAcquisition();
    auto* controls = window.controlsWindow();
    auto* entry = controls->findChild<QLineEdit*>(QStringLiteral("pvEntry"));
    auto* connect = controls->findChild<QPushButton*>(QStringLiteral("connectButton"));
    entry->setText(QStringLiteral("test:extend-history"));
    connect->click();
    entry->setText(QStringLiteral("CPU_Usage"));
    connect->click();
    QCOMPARE(recording->requests.size(), std::size_t{1});
    const auto first = recording->requests.front();
    auto* plot = window.plotWidget();

    // A zoom while the startup backfill is pending waits for that request.
    plot->zoom(4.0);
    QVERIFY(plot->autoScroll());
    QTest::qWait(700);
    QCOMPARE(recording->requests.size(), std::size_t{1});
    recording->succeed(first.id, first.channel,
                       {{first.range.start + std::chrono::seconds(1), 1.0, 0, 0}});
    QTRY_COMPARE(recording->requests.size(), std::size_t{2});
    const auto second = recording->requests.back();
    QCOMPARE(second.channel, first.channel);
    QCOMPARE(second.range.end, first.range.start);
    QVERIFY(second.range.start <= plot->visibleTimeRange().start);
    QVERIFY(first.range.start - second.range.start > std::chrono::minutes(14));
    const auto older = second.range.start + std::chrono::seconds(1);
    recording->succeed(second.id, second.channel, {{older, 2.0, 0, 0}});
    QCOMPARE(plot->curveSamples(0).size(), std::size_t{2});
    QCOMPARE(plot->curveSamples(0).front().timestamp, older);

    // A view inside the covered span requests nothing.
    plot->zoom(0.5);
    QTest::qWait(700);
    QCOMPARE(recording->requests.size(), std::size_t{2});

    // Panning further back requests only the uncovered span. Failure stays
    // silent and the same span is not requested again.
    plot->pan(-2.0);
    QTRY_COMPARE(recording->requests.size(), std::size_t{3});
    const auto third = recording->requests.back();
    QCOMPARE(third.range.end, second.range.start);
    QCOMPARE(third.range.start, plot->visibleTimeRange().start);
    recording->fail(third.id);
    QVERIFY(window.findChildren<QMessageBox*>().isEmpty());
    plot->pan(0.5);
    QTest::qWait(700);
    QCOMPARE(recording->requests.size(), std::size_t{3});
  }
  void resetViewUsesProviderLimitsWithoutManualEdits() {
    striptool::MainWindow window;
    window.startAcquisition();
    auto* controls = window.controlsWindow();
    controls->findChild<QLineEdit*>(QStringLiteral("pvEntry"))
        ->setText(QStringLiteral("CPU_Usage"));
    controls->findChild<QPushButton*>(QStringLiteral("connectButton"))->click();
    QTRY_COMPARE(window.model().curves[0].minimum, 0.0);
    QTRY_COMPARE(window.model().curves[0].maximum, 100.0);
    QVERIFY(!window.model().curves[0].minimumSet);
    QVERIFY(!window.model().curves[0].maximumSet);

    const auto now = std::chrono::system_clock::now();
    window.plotWidget()->setCurveSamples(0, {
        {now - std::chrono::seconds(1), 45.0, 0, 0},
        {now, 55.0, 0, 0}});
    window.plotWidget()->autoScale();
    QVERIFY(window.plotWidget()->valueRange(0).minimum > 0.0);
    QVERIFY(window.plotWidget()->valueRange(0).maximum < 100.0);

    auto* autoScaleAction =
        window.findChild<QAction*>(QStringLiteral("autoScaleAction"));
    QVERIFY(autoScaleAction->isChecked());
    window.findChild<QAction*>(QStringLiteral("resetAction"))->trigger();
    QVERIFY(!autoScaleAction->isChecked());
    QCOMPARE(window.plotWidget()->valueRange(0).minimum, 0.0);
    QCOMPARE(window.plotWidget()->valueRange(0).maximum, 100.0);
    autoScaleAction->trigger();
    QVERIFY(autoScaleAction->isChecked());
    QVERIFY(window.plotWidget()->valueRange(0).minimum > 0.0);
    QVERIFY(window.plotWidget()->valueRange(0).maximum < 100.0);
    autoScaleAction->trigger();
    QVERIFY(!autoScaleAction->isChecked());
    QCOMPARE(window.plotWidget()->valueRange(0).minimum, 0.0);
    QCOMPARE(window.plotWidget()->valueRange(0).maximum, 100.0);

    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "first:pv";
    model.curves[0].nameSet = true;
    model.curves[1].name = "second:pv";
    model.curves[1].nameSet = true;
    model.curves[1].minimum = -20.0;
    model.curves[1].maximum = 20.0;
    striptool::PlotWidget secondCurvePlot;
    secondCurvePlot.setModel(model);
    secondCurvePlot.setCurveSamples(1, {
        {now - std::chrono::seconds(1), 4.0, 0, 0},
        {now, 6.0, 0, 0}});
    secondCurvePlot.autoScale();
    QVERIFY(secondCurvePlot.valueRange(1).minimum > -20.0);
    QVERIFY(secondCurvePlot.valueRange(1).maximum < 20.0);
    secondCurvePlot.resetVerticalView();
    QCOMPARE(secondCurvePlot.valueRange(1).minimum, -20.0);
    QCOMPARE(secondCurvePlot.valueRange(1).maximum, 20.0);
  }
  void newCurveInheritsGlobalConfiguredLimitMode() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "first:pv";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0.0;
    model.curves[0].maximum = 100.0;
    striptool::PlotWidget plot;
    plot.setModel(model);
    const auto now = std::chrono::system_clock::now();
    plot.setCurveSamples(0, {{now, 50.0, 0, 0}});
    plot.autoScale();
    QVERIFY(plot.valueRange(0).minimum > 0.0);
    QVERIFY(plot.valueRange(0).maximum < 100.0);
    plot.resetVerticalView();
    QCOMPARE(plot.valueRange(0).minimum, 0.0);
    QCOMPARE(plot.valueRange(0).maximum, 100.0);

    model.curves[1].name = "second:pv";
    model.curves[1].nameSet = true;
    model.curves[1].minimum = -20.0;
    model.curves[1].maximum = 20.0;
    plot.setModel(model);
    plot.setCurveSamples(1, {{now, 5.0, 0, 0}});
    QCOMPARE(plot.valueRange(0).minimum, 0.0);
    QCOMPARE(plot.valueRange(0).maximum, 100.0);
    QCOMPARE(plot.valueRange(1).minimum, -20.0);
    QCOMPARE(plot.valueRange(1).maximum, 20.0);

    plot.autoScale();
    QVERIFY(plot.valueRange(0).minimum > 0.0);
    QVERIFY(plot.valueRange(0).maximum < 100.0);
    QVERIFY(plot.valueRange(1).minimum > -20.0);
    QVERIFY(plot.valueRange(1).maximum < 20.0);
  }
  void addingCurveKeepsExistingAcquisitionHistory() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "CPU_Usage";
    model.curves[0].nameSet = true;
    striptool::MainWindow window(model);
    window.startAcquisition();
    QTRY_VERIFY_WITH_TIMEOUT(!window.plotWidget()->curveSamples(0).empty(), 2500);
    const auto oldest = window.plotWidget()->curveSamples(0).front().timestamp;
    auto* controls = window.controlsWindow();
    controls->findChild<QLineEdit*>(QStringLiteral("pvEntry"))
        ->setText(QStringLiteral("CPU_Usage"));
    controls->findChild<QPushButton*>(QStringLiteral("connectButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(!window.plotWidget()->curveSamples(1).empty(), 2500);
    const auto& original = window.plotWidget()->curveSamples(0);
    QVERIFY(std::any_of(original.begin(), original.end(), [&](const auto& sample) {
      return sample.timestamp == oldest;
    }));
  }
  void openingConfigurationClearsPreviousAcquisitionData() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "CPU_Usage";
    model.curves[0].nameSet = true;
    striptool::MainWindow window(model);
    window.startAcquisition();
    QTRY_VERIFY_WITH_TIMEOUT(!window.plotWidget()->curveSamples(0).empty(), 2500);
    const auto previous = window.plotWidget()->curveSamples(0).front().timestamp;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("same-pv.stp"));
    QString error;
    QVERIFY(window.saveConfiguration(path, &error));
    QVERIFY(window.openConfiguration(path, &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.plotWidget()->curveSamples(0).empty(), 2500);
    QVERIFY(window.plotWidget()->curveSamples(0).front().timestamp > previous);
  }
  void fileWorkflowIsTransactionalAndTracksRecentFiles() {
    const auto root = std::filesystem::temp_directory_path() /
                      "qtstriptool-file-workflow-test";
    std::filesystem::create_directories(root);
    const auto path = root / "saved.stp";
    auto model = striptool::makeDefaultModel();
    model.timing.timespanSeconds = 123;
    model.timing.numberOfSamples = 2048;
    std::string error;
    QVERIFY(striptool::FileWorkflow::save(path, model, &error));
    QCOMPARE(model.filename, path.string());
    model.timing.timespanSeconds = 1;
    QVERIFY(striptool::FileWorkflow::open(path, model).success);
    QCOMPARE(model.timing.timespanSeconds, 123U);
    QCOMPARE(model.timing.numberOfSamples, 2048);
    QCOMPARE(model.filename, path.string());
    QVERIFY(model.title.empty());
    model.timing.sampleIntervalSeconds = -1.0;
    QVERIFY(!striptool::FileWorkflow::save(path, model, &error));
    auto saved = striptool::makeDefaultModel();
    QVERIFY(striptool::FileWorkflow::open(path, saved).success);
    QCOMPARE(saved.timing.timespanSeconds, 123U);
    auto recent = striptool::FileWorkflow::addRecent(
        {"one.stp", path.string(), "two.stp"}, path.string(), 3);
    QCOMPARE(recent.size(), std::size_t{3});
    QCOMPARE(recent.front(), path.string());
    QCOMPARE(recent[1], std::string("one.stp"));
    const auto before = model;
    QVERIFY(!striptool::FileWorkflow::open(root / "missing.stp", model).success);
    QCOMPARE(model.timing.timespanSeconds, before.timing.timespanSeconds);
    striptool::FileWorkflow::restoreDefaults(model);
    QCOMPARE(model.timing.timespanSeconds, 300U);
    std::filesystem::remove_all(root);
  }
  void exportReportsWriteFailures() {
    // /dev/full accepts open() but every write fails with ENOSPC.
    if (!QFileInfo::exists(QStringLiteral("/dev/full"))) QSKIP("/dev/full is unavailable");
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "export:full";
    model.curves[0].nameSet = true;
    striptool::CurveSamples samples;
    samples[0].push_back({std::chrono::system_clock::now(), 1.0, 0, 0});
    std::string error;
    QVERIFY(!striptool::ExportService::writeTextFile("/dev/full", model, samples, &error));
    QVERIFY(!error.empty());
    error.clear();
    QVERIFY(!striptool::ExportService::writeCsvFile("/dev/full", model, samples, &error));
    QVERIFY(!error.empty());
  }
  void exportsTextAndCsvData() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "pv,\"quoted\"";
    model.curves[0].nameSet = true;
    striptool::CurveSamples samples;
    samples[0].push_back({std::chrono::system_clock::from_time_t(0), 1.25, 2, 3});
    std::ostringstream text;
    QVERIFY(striptool::ExportService::writeText(text, model, samples));
    QVERIFY(text.str().find("pv,\"quoted\" 1.25 2 3") != std::string::npos);
    std::ostringstream csv;
    QVERIFY(striptool::ExportService::writeCsv(csv, model, samples));
    QVERIFY(csv.str().find("timestamp,curve,value,status,severity") == 0);
    QVERIFY(csv.str().find("\"pv,\"\"quoted\"\"\"") != std::string::npos);
  }
  void noHistoryProviderReportsFailureAndCancellation() {
    striptool::NoHistoryProvider provider;
    QSignalSpy failed(&provider, &striptool::HistoryProvider::requestFailed);
    QSignalSpy cancelled(&provider, &striptool::HistoryProvider::requestCancelled);
    const auto now = std::chrono::system_clock::now();
    const auto request = provider.request(QStringLiteral("test:pv"),
                                          {now - std::chrono::hours(1), now});
    QVERIFY(failed.wait());
    QCOMPARE(failed.at(0).at(0).toULongLong(), request);
    const auto cancelledRequest = provider.request(QStringLiteral("test:pv"),
                                                   {now, now});
    provider.cancel(cancelledRequest);
    QTRY_COMPARE(cancelled.count(), 1);
    QCOMPARE(cancelled.at(0).at(0).toULongLong(), cancelledRequest);
    QCOMPARE(failed.count(), 1);
  }
  void testHistoryProviderReturnsSelectedOwnedSamples() {
    striptool::TestHistoryProvider provider;
    const auto start = std::chrono::system_clock::from_time_t(1000);
    provider.setSamples(QStringLiteral("test:history"),
                        {{start, 1.0, 0, 0},
                         {start + std::chrono::seconds(10), 2.0, 0, 0},
                         {start + std::chrono::seconds(20), 3.0, 0, 0}});
    QSignalSpy results(&provider, &striptool::HistoryProvider::resultReady);
    const auto id = provider.request(QStringLiteral("test:history"),
                                     {start + std::chrono::seconds(5),
                                      start + std::chrono::seconds(15)});
    QVERIFY(results.wait());
    QCOMPARE(results.at(0).at(0).toULongLong(), id);
    const auto samples = qvariant_cast<std::vector<striptool::Sample>>(
        results.at(0).at(2));
    QCOMPARE(samples.size(), std::size_t{1});
    QCOMPARE(samples.front().value, 2.0);
  }
  void archiverHistoryProviderRetrievesNativeJson() {
    QCOMPARE(striptool::ArchiverHistoryProvider::defaultRetrievalRoot(),
             QStringLiteral("http://asddtn03.aps4.anl.gov:17668/retrieval"));
    const QByteArray response = R"([{"meta":{"name":"test:archive"},"data":[{"secs":1001,"nanos":0,"val":2.5},{"secs":1000,"nanos":123456789,"val":1.25,"status":4,"severity":2},{"secs":1002,"nanos":0,"val":[1,2]},{"secs":1003,"nanos":1000000000,"val":3.5}]}])";
    QString error;
    const auto samples = striptool::parseArchiverJson(response, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(samples.size(), std::size_t{2});
    QCOMPARE(samples[0].value, 1.25);
    QCOMPARE(samples[0].status, std::uint16_t{4});
    QCOMPARE(samples[0].severity, std::uint16_t{2});
    QCOMPARE(std::chrono::duration_cast<std::chrono::nanoseconds>(
                 samples[0].timestamp.time_since_epoch()).count(),
             std::chrono::duration_cast<std::chrono::nanoseconds>(
                 std::chrono::duration_cast<std::chrono::system_clock::duration>(
                     std::chrono::seconds(1000) +
                     std::chrono::nanoseconds(123456789))).count());
    QCOMPARE(samples[1].timestamp,
             std::chrono::system_clock::time_point{std::chrono::seconds(1001)});
    QCOMPARE(samples[1].status, std::uint16_t{0});
    QCOMPARE(samples[1].severity, std::uint16_t{0});
    const auto invalid = striptool::parseArchiverJson(QByteArrayLiteral("{}"), &error);
    QVERIFY(invalid.empty());
    QVERIFY(!error.isEmpty());

    {
      // Restore the caller's URL: unsetting it would send later tests'
      // archive requests to the operational default.
      const bool wasSet = qEnvironmentVariableIsSet("QTSTRIPTOOL_ARCHIVER_URL");
      const QByteArray original = qgetenv("QTSTRIPTOOL_ARCHIVER_URL");
      const auto restore = qScopeGuard([&] {
        if (wasSet) qputenv("QTSTRIPTOOL_ARCHIVER_URL", original);
        else qunsetenv("QTSTRIPTOOL_ARCHIVER_URL");
      });
      qputenv("QTSTRIPTOOL_ARCHIVER_URL", QByteArrayLiteral("http://example.invalid/archive/"));
      QCOMPARE(striptool::ArchiverHistoryProvider::configuredRetrievalRoot(),
               QStringLiteral("http://example.invalid/archive/"));
    }

    const auto start = std::chrono::system_clock::time_point{
        std::chrono::milliseconds(946684800123)};
    const auto url = striptool::ArchiverHistoryProvider::requestUrl(
        QStringLiteral("http://archive.example/retrieval/"),
        QStringLiteral("test:pv name"),
        {start, start + std::chrono::seconds(10001)});
    QCOMPARE(url.path(), QStringLiteral("/retrieval/data/getData.json"));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("pv")),
             QStringLiteral("lastSample_2(test:pv name)"));
    QCOMPARE(query.queryItemValue(QStringLiteral("from")),
             QStringLiteral("2000-01-01T00:00:00.123Z"));
    QCOMPARE(query.queryItemValue(QStringLiteral("to")),
             QStringLiteral("2000-01-01T02:46:41.123Z"));
    QCOMPARE(query.queryItemValue(QStringLiteral("donotchunk")),
             QStringLiteral("true"));
    QCOMPARE(striptool::ArchiverHistoryProvider::requestUrl(
                 QStringLiteral("http://archive.example/retrieval/data/getData.json"),
                 QStringLiteral("test:pv"),
                 {start, start + std::chrono::seconds(1)}).path(),
             QStringLiteral("/retrieval/data/getData.json"));
  }
  void activeHistoryRequestCanBeDestroyedSafely() {
    auto provider = std::make_unique<striptool::TestHistoryProvider>();
    const auto now = std::chrono::system_clock::now();
    provider->setSamples(QStringLiteral("shutdown:test"), {{now, 1.0, 0, 0}});
    provider->request(QStringLiteral("shutdown:test"), {now, now});
    provider.reset();
    QApplication::processEvents();
  }
  void historyDialogRoundTripsRange() {
    striptool::HistoryDialog dialog;
    QVERIFY(dialog.findChild<QDateTimeEdit*>(QStringLiteral("historyFrom")));
    QVERIFY(dialog.findChild<QDateTimeEdit*>(QStringLiteral("historyTo")));
    const auto start = std::chrono::system_clock::from_time_t(1000);
    const auto end = std::chrono::system_clock::from_time_t(2000);
    dialog.setRange({start, end});
    QCOMPARE(dialog.selectedRange().start, start);
    QCOMPARE(dialog.selectedRange().end, end);
  }
  void mainWindowFileExportAndSnapshotEntryPointsWork() {
    const auto root = std::filesystem::temp_directory_path() /
                      "qtstriptool-main-workflow-test";
    std::filesystem::create_directories(root);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QString::fromStdString(root.string()));
    striptool::MainWindow window;
    window.resize(640, 480);
    window.show();
    QString error;
    QVERIFY2(window.saveConfiguration(QString::fromStdString((root / "saved.stp").string()),
                                      &error), qPrintable(error));
    QVERIFY2(window.openConfiguration(QString::fromStdString((root / "saved.stp").string()),
                                      &error), qPrintable(error));
    QVERIFY2(window.exportData(QString::fromStdString((root / "data.csv").string()), true,
                               &error), qPrintable(error));
    QVERIFY2(window.saveSnapshot(QString::fromStdString((root / "plot.png").string()),
                                 &error), qPrintable(error));
    QVERIFY2(window.saveSnapshot(QString::fromStdString((root / "plot-default").string()),
                                 &error), qPrintable(error));
    QVERIFY2(window.saveSnapshot(QString::fromStdString((root / "plot-jpeg").string()),
                                 QStringLiteral("jpg"), &error), qPrintable(error));
    QVERIFY(std::filesystem::file_size(root / "data.csv") > 0);
    QVERIFY(std::filesystem::file_size(root / "plot.png") > 0);
    QVERIFY(std::filesystem::file_size(root / "plot-default.png") > 0);
    QVERIFY(std::filesystem::file_size(root / "plot-jpeg.jpg") > 0);
    std::filesystem::remove_all(root);
  }
  void printPreviewHasUsefulInitialGeometry() {
    striptool::MainWindow window;
    window.resize(900, 620);
    window.show();
    bool inspected = false;
    QSize dialogSize;
    QSize previewSize;
    qreal zoomFactor = 0.0;
    bool imageSaved = false;
    QTimer::singleShot(100, [&] {
      for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        auto* preview = qobject_cast<QPrintPreviewDialog*>(topLevel);
        if (!preview) continue;
        auto* widget = preview->findChild<QPrintPreviewWidget*>();
        if (widget) {
          dialogSize = preview->size();
          previewSize = widget->size();
          zoomFactor = widget->zoomFactor();
          imageSaved = preview->grab().save(
              QStringLiteral("/tmp/qtstriptool-print-preview.png"));
          inspected = true;
        }
        preview->reject();
        break;
      }
    });
    window.findChild<QAction*>(QStringLiteral("printPreviewAction"))->trigger();
    QVERIFY(inspected);
    qInfo() << "print preview geometry" << dialogSize
            << "preview geometry" << previewSize << "zoom" << zoomFactor;
    QVERIFY(dialogSize.width() >= 800);
    QVERIFY(dialogSize.height() >= 600);
    QVERIFY(previewSize.height() >= 500);
    QVERIFY(zoomFactor >= 0.25);
    QVERIFY(imageSaved);
  }
  void mainWindowExportsOnlyVisibleSamples() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "visible:test";
    model.curves[0].nameSet = true;
    striptool::MainWindow window(model);
    const auto base = std::chrono::system_clock::from_time_t(1000);
    window.plotWidget()->setCurveSamples(0,
        {{base, 1.0, 0, 0}, {base + std::chrono::seconds(10), 2.0, 0, 0},
         {base + std::chrono::seconds(20), 3.0, 0, 0}});
    window.plotWidget()->setVisibleTimeRange(
        {base + std::chrono::seconds(5), base + std::chrono::seconds(15)});
    const QString path = temporary.filePath(QStringLiteral("visible.csv"));
    QVERIFY(window.exportData(path, true));
    std::ifstream input(path.toStdString());
    const std::string content((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    QVERIFY(content.find(",2,") != std::string::npos);
    QVERIFY(content.find(",1,") == std::string::npos);
    QVERIFY(content.find(",3,") == std::string::npos);
  }
  void iconResourceIsEmbedded() {
    QVERIFY(!QIcon(QStringLiteral(":/icons/qtstriptool.svg")).isNull());
  }
  void defaultsAreToolkitNeutral() {
    const auto model = striptool::makeDefaultModel();
    QCOMPARE(model.timing.timespanSeconds, 300U);
    QCOMPARE(model.timing.numberOfSamples, 7200);
    QCOMPARE(model.curves.size(), striptool::kMaximumCurves);
    QCOMPARE(QString::fromStdString(model.curves[0].name), QStringLiteral("Curve0"));
    QVERIFY((model.colors.background == striptool::Rgba16{65535, 65535, 65535, 65535}));
    QVERIFY((model.colors.curves[9] == striptool::Rgba16{39578, 52685, 12850, 65535}));
  }
  void commandLineTimingOverridesConfiguration() {
    for (const QStringList& arguments : {
        QStringList{"qtstriptool", "-history=3600", "-sampleCount=7200", "beam.stp"},
        QStringList{"qtstriptool", "--history", "3600", "--sampleCount", "7200", "beam.stp"}}) {
      QCommandLineParser parser;
      striptool::configureCommandLineParser(parser);
      QVERIFY(parser.parse(arguments));
      QCOMPARE(parser.positionalArguments(), QStringList{"beam.stp"});
      auto model = striptool::makeDefaultModel();
      QVERIFY(striptool::readConfigurationFile(fixture("canonical-1.2.stp"), model).success);
      const auto interval = model.timing.sampleIntervalSeconds;
      QVERIFY(striptool::applyCommandLineTiming(parser, model));
      QCOMPARE(model.timing.timespanSeconds, 3600U);
      QCOMPARE(model.timing.numberOfSamples, 7200);
      QCOMPARE(model.timing.sampleIntervalSeconds, interval);
      striptool::MainWindow window(model);
      const auto range = window.plotWidget()->visibleTimeRange();
      QCOMPARE(range.end - range.start, std::chrono::seconds(3600));
      QCOMPARE(window.controlsWindow()->findChild<QSpinBox*>(QStringLiteral("timespanSeconds"))->value(), 3600);
      QCOMPARE(window.controlsWindow()->findChild<QSpinBox*>(QStringLiteral("sampleCount"))->value(), 7200);
    }
  }
  void commandLineRejectsInvalidTiming() {
    for (const QString argument : {"-history=0", "-history=-1", "-history=1.5",
                                   "-history=2147483648", "-history=abc", "-history=",
                                   "-sampleCount=0", "-sampleCount=65537",
                                   "-sampleCount=999999999999999999999999"}) {
      QCommandLineParser parser;
      striptool::configureCommandLineParser(parser);
      QVERIFY(parser.parse({"qtstriptool", argument}));
      auto model = striptool::makeDefaultModel();
      QString error;
      QVERIFY(!striptool::applyCommandLineTiming(parser, model, &error));
      QVERIFY(!error.isEmpty());
      QCOMPARE(model.timing.timespanSeconds, 300U);
      QCOMPARE(model.timing.numberOfSamples, 7200);
    }
  }
  void automaticHistoryUsesConfiguredWindow() {
    auto model = striptool::makeDefaultModel();
    model.timing.timespanSeconds = 3600;
    striptool::MainWindow window(model);
    auto provider = std::make_unique<RecordingHistoryProvider>();
    auto* recording = provider.get();
    window.setHistoryProvider(std::move(provider));
    window.startAcquisition();
    window.controlsWindow()->findChild<QLineEdit*>(QStringLiteral("pvEntry"))
        ->setText(QStringLiteral("test:hour-history"));
    window.controlsWindow()->findChild<QPushButton*>(QStringLiteral("connectButton"))->click();
    QCOMPARE(recording->requests.size(), std::size_t{1});
    const auto range = recording->requests.front().range;
    QCOMPARE(range.end - range.start, std::chrono::hours(1));
  }
  void readsCanonicalFixture() {
    auto model = striptool::makeDefaultModel();
    const auto result = striptool::readConfigurationFile(fixture("canonical-1.2.stp"), model);
    QVERIFY2(result.success, result.diagnostics.empty() ? "parse failed" : result.diagnostics[0].message.c_str());
    QVERIFY(!result.legacyFormat);
    QCOMPARE(model.timing.timespanSeconds, 900U);
    QCOMPARE(model.timing.numberOfSamples, 9000);
    QCOMPARE(model.graph.xGrid, striptool::GridMode::All);
    QCOMPARE(model.graph.lineWidth, 3);
    QCOMPARE(QString::fromStdString(model.curves[9].name), QStringLiteral("test:curve:10"));
    QCOMPARE(model.curves[1].scale, striptool::ScaleMode::Log10);
    QVERIFY(!model.curves[2].plotted);
    QCOMPARE(model.curves[9].precision, 20);
    striptool::MainWindow window(model);
    QCOMPARE(window.model().timing.numberOfSamples, 9000);
    QVERIFY(window.windowTitle().contains(QStringLiteral("canonical-1.2.stp")));
  }
  void derivesSamplesWhenOmitted() {
    auto model = striptool::makeDefaultModel();
    const auto result = striptool::readConfigurationFile(fixture("derived-samples-1.2.stp"), model);
    QVERIFY(result.success);
    QCOMPARE(model.timing.numberOfSamples, 2404);
  }
  void readsLegacyFixture() {
    auto model = striptool::makeDefaultModel();
    const auto result = striptool::readConfigurationFile(fixture("legacy-format.stp"), model);
    QVERIFY(result.success);
    QVERIFY(result.legacyFormat);
    QCOMPARE(model.timing.sampleIntervalSeconds, 0.5);
    QCOMPARE(model.timing.refreshIntervalSeconds, 0.5);
    QCOMPARE(model.timing.numberOfSamples, 1200);
    QCOMPARE(QString::fromStdString(model.curves[1].name), QStringLiteral("test:legacy:two"));
    QCOMPARE(model.curves[0].minimum, -5.0);
    QCOMPARE(model.curves[1].maximum, 100.0);
  }
  void preservesUnknownFieldsAcrossRoundTrip() {
    auto model = striptool::makeDefaultModel();
    QVERIFY(striptool::readConfigurationFile(fixture("unknown-fields-1.2.stp"), model).success);
    QCOMPARE(model.unknownFields.size(), std::size_t{4});
    std::ostringstream output;
    QVERIFY(striptool::writeConfiguration(output, model));
    const auto text = output.str();
    QVERIFY(text.find("Foreign.Namespace.Value") != std::string::npos);
    QVERIFY(text.find("Strip.Curve.0.FutureField") != std::string::npos);
    auto roundTripped = striptool::makeDefaultModel();
    std::istringstream input(text);
    QVERIFY(striptool::readConfiguration(input, roundTripped).success);
    QCOMPARE(QString::fromStdString(roundTripped.curves[0].name),
             QStringLiteral("test:known:curve"));
    QCOMPARE(roundTripped.unknownFields.size(), std::size_t{4});
  }
  void rejectsMalformedInputTransactionally() {
    auto model = striptool::makeDefaultModel();
    model.timing.timespanSeconds = 42;
    std::istringstream bad("StripConfig 1.2\nStrip.Time.Timespan nope\n");
    const auto result = striptool::readConfiguration(bad, model);
    QVERIFY(!result.success);
    QCOMPARE(result.diagnostics[0].line, std::size_t{2});
    QCOMPARE(model.timing.timespanSeconds, 42U);

    std::istringstream badIndex("StripConfig 1.2\nStrip.Curve.10.Name overflow\n");
    QVERIFY(!striptool::readConfiguration(badIndex, model).success);
    std::istringstream badPrecision("StripConfig 1.2\nStrip.Curve.0.Precision -1\n");
    QVERIFY(!striptool::readConfiguration(badPrecision, model).success);
  }
  void clampsLegacyNumericRanges() {
    auto model = striptool::makeDefaultModel();
    std::istringstream input(
        "StripConfig 1.2\n"
        "Strip.Time.Timespan 0\n"
        "Strip.Time.NumSamples 2\n"
        "Strip.Time.SampleInterval 0\n"
        "Strip.Time.RefreshInterval 0\n"
        "Strip.Option.GridXon 99\n"
        "Strip.Option.GraphLineWidth 99\n");
    QVERIFY(striptool::readConfiguration(input, model).success);
    QCOMPARE(model.timing.timespanSeconds, 1U);
    QCOMPARE(model.timing.numberOfSamples, 2);
    QCOMPARE(model.timing.sampleIntervalSeconds, 0.01);
    QCOMPARE(model.timing.refreshIntervalSeconds, 0.1);
    QCOMPARE(model.graph.xGrid, striptool::GridMode::Some);
    QCOMPARE(model.graph.lineWidth, 10);
  }
  void appliesLayersInOrder() {
    const auto temporary = std::filesystem::temp_directory_path() / "qtstriptool-layer-test";
    std::filesystem::create_directories(temporary);
    const auto site = temporary / "site.stp";
    const auto user = temporary / "user.stp";
    const auto explicitFile = temporary / "explicit.stp";
    { std::ofstream out(site); out << "StripConfig 1.2\nStrip.Time.Timespan 100\nStrip.Option.GraphLineWidth 1\n"; }
    { std::ofstream out(user); out << "StripConfig 1.2\nStrip.Time.Timespan 200\n"; }
    { std::ofstream out(explicitFile); out << "StripConfig 1.2\nStrip.Time.Timespan 300\n"; }
    auto model = striptool::makeDefaultModel();
    QVERIFY(striptool::loadConfigurationLayers({site, user, explicitFile}, model).success);
    QCOMPARE(model.timing.timespanSeconds, 300U);
    QCOMPARE(model.graph.lineWidth, 1);
    std::filesystem::remove_all(temporary);
  }
  void writerRejectsInvalidModels() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].nameSet = true;
    model.curves[0].name.assign(striptool::kMaximumCurveNameLength + 1, 'x');
    std::ostringstream output;
    std::string error;
    QVERIFY(!striptool::writeConfiguration(output, model, &error));
    QVERIFY(!error.empty());
    QVERIFY(output.str().empty());
  }
  void configurationRoundTripsFullDoublePrecision() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "precision:pv";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = std::nextafter(1.0, 2.0);
    model.curves[0].minimumSet = true;
    model.curves[0].maximum = std::nextafter(2.0, 3.0);
    model.curves[0].maximumSet = true;
    std::ostringstream output;
    QVERIFY(striptool::writeConfiguration(output, model));
    std::istringstream input(output.str());
    auto loaded = striptool::makeDefaultModel();
    QVERIFY(striptool::readConfiguration(input, loaded).success);
    QCOMPARE(loaded.curves[0].minimum, model.curves[0].minimum);
    QCOMPARE(loaded.curves[0].maximum, model.curves[0].maximum);
  }
  void configurationWritesDiscoveredChannelMetadata() {
    auto model = striptool::makeDefaultModel();
    auto& curve = model.curves[0];
    curve.name = "metadata:pv";
    curve.nameSet = true;
    curve.units = "mA";
    curve.comment = "Discovered channel description";
    curve.precision = 7;
    curve.minimum = -123.456789012345;
    curve.maximum = 987.654321098765;
    curve.unitsDiscovered = true;
    curve.commentDiscovered = true;
    curve.precisionDiscovered = true;
    curve.minimumDiscovered = true;
    curve.maximumDiscovered = true;
    QVERIFY(!curve.unitsSet);
    QVERIFY(!curve.commentSet);
    QVERIFY(!curve.precisionSet);
    QVERIFY(!curve.minimumSet);
    QVERIFY(!curve.maximumSet);

    std::ostringstream output;
    QVERIFY(striptool::writeConfiguration(output, model));
    const auto text = output.str();
    QVERIFY(text.find("Strip.Curve.0.Units") != std::string::npos);
    QVERIFY(text.find("Strip.Curve.0.Comment") != std::string::npos);
    QVERIFY(text.find("Strip.Curve.0.Precision") != std::string::npos);
    QVERIFY(text.find("Strip.Curve.0.Min") != std::string::npos);
    QVERIFY(text.find("Strip.Curve.0.Max") != std::string::npos);

    auto loaded = striptool::makeDefaultModel();
    std::istringstream input(text);
    QVERIFY(striptool::readConfiguration(input, loaded).success);
    QCOMPARE(QString::fromStdString(loaded.curves[0].units), QStringLiteral("mA"));
    QCOMPARE(QString::fromStdString(loaded.curves[0].comment),
             QStringLiteral("Discovered channel description"));
    QCOMPARE(loaded.curves[0].precision, 7);
    QCOMPARE(loaded.curves[0].minimum, curve.minimum);
    QCOMPARE(loaded.curves[0].maximum, curve.maximum);
  }
  void configurationOmitsDiscoveredUnitsThatAreNotOneWord() {
    // An IOC EGU such as "deg C" must not make the saved file unreadable.
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "metadata:temperature";
    model.curves[0].nameSet = true;
    model.curves[0].units = "deg C";
    model.curves[0].unitsDiscovered = true;
    model.curves[1].name = "metadata:current";
    model.curves[1].nameSet = true;
    model.curves[1].units = "mA";
    model.curves[1].unitsDiscovered = true;

    std::ostringstream output;
    QVERIFY(striptool::writeConfiguration(output, model));
    const auto text = output.str();
    QVERIFY(text.find("Strip.Curve.0.Units") == std::string::npos);
    QVERIFY(text.find("Strip.Curve.1.Units") != std::string::npos);

    auto loaded = striptool::makeDefaultModel();
    std::istringstream input(text);
    const auto result = striptool::readConfiguration(input, loaded);
    QVERIFY2(result.success, result.diagnostics.empty() ? ""
                                 : result.diagnostics.front().message.c_str());
    QCOMPARE(QString::fromStdString(loaded.curves[0].name),
             QStringLiteral("metadata:temperature"));
    QVERIFY(!loaded.curves[0].unitsSet);
    QCOMPARE(QString::fromStdString(loaded.curves[1].units), QStringLiteral("mA"));
  }
  void followsLegacySearchOrder() {
    const auto root = std::filesystem::temp_directory_path() / "qtstriptool-search-test";
    const auto current = root / "current";
    const auto search = root / "search";
    std::filesystem::create_directories(current);
    std::filesystem::create_directories(search);
    { std::ofstream out(search / "example.stp"); out << "StripConfig 1.2\n"; }
    QCOMPARE(striptool::findConfigurationFile("example.stp", current, search.string()),
             search / "example.stp");
    { std::ofstream out(current / "example.stp"); out << "StripConfig 1.2\n"; }
    QCOMPARE(striptool::findConfigurationFile("example.stp", current, search.string()),
             current / "example.stp");
    QCOMPARE(striptool::findStartupConfiguration("example.stp", current, search.string()),
             current / "example.stp");
    { std::ofstream out(current / "StripTool.stp"); out << "StripConfig 1.2\n"; }
    QCOMPARE(striptool::findStartupConfiguration("", current, search.string()),
             current / "StripTool.stp");
    QCOMPARE(striptool::findStartupConfiguration("missing.stp", current, search.string()),
             current / "StripTool.stp");
    std::filesystem::remove_all(root);
  }
  void sampleBufferWrapsAndHonorsMemoryLimit() {
    striptool::SampleBuffer buffer(3);
    const auto base = std::chrono::system_clock::time_point{};
    for (int i = 0; i < 5; ++i)
      buffer.append({base + std::chrono::seconds(i), static_cast<double>(i),
                     static_cast<std::uint16_t>(i), 0});
    QCOMPARE(buffer.size(), std::size_t{3});
    const auto samples = buffer.samples();
    QCOMPARE(samples[0].value, 2.0);
    QCOMPARE(samples[2].timestamp, base + std::chrono::seconds(4));
    striptool::SampleBuffer limited(1000, sizeof(striptool::Sample) * 2);
    QCOMPARE(limited.capacity(), std::size_t{2});
  }
  void decimationPreservesChronologyAndExtrema() {
    std::vector<striptool::Sample> samples;
    const auto base = std::chrono::system_clock::time_point{};
    for (int i = 0; i < 20; ++i)
      samples.push_back({base + std::chrono::seconds(i), i == 7 ? 100.0 : double(i), 0, 0});
    const auto reduced = striptool::decimateSamples(samples, 8);
    QVERIFY(reduced.size() <= std::size_t{8});
    QCOMPARE(reduced.front().timestamp, samples.front().timestamp);
    QCOMPARE(reduced.back().timestamp, samples.back().timestamp);
    QVERIFY(std::any_of(reduced.begin(), reduced.end(),
                        [](const auto& sample) { return sample.value == 100.0; }));
    QVERIFY(std::is_sorted(reduced.begin(), reduced.end(), [](const auto& a, const auto& b) {
      return a.timestamp < b.timestamp;
    }));
    for (std::size_t budget = 1; budget <= 24; ++budget)
      QVERIFY(striptool::decimateSamples(samples, budget).size() <= budget);
  }
  void disconnectedIntervalsSurviveDecimation() {
    const auto base = std::chrono::system_clock::time_point{};
    std::vector<striptool::Sample> samples;
    for (int i = 0; i < 100; ++i)
      samples.push_back({base + std::chrono::seconds(i), double(i), 0, 0});
    samples[50].plotable = false;
    samples[50].value = 1e9;
    const auto reduced = striptool::decimateSamples(samples, 12);
    QVERIFY(reduced.size() <= std::size_t{12});
    QVERIFY(std::any_of(reduced.begin(), reduced.end(), [](const auto& sample) {
      return !sample.plotable;
    }));
    const auto range = striptool::sampleValueRange(samples, striptool::ScaleMode::Linear);
    QVERIFY(range.has_value());
    QVERIFY(range->maximum < 1e9);
  }
  void denseDecimationRetainsStepTransitionsAndSpikes() {
    const auto base = std::chrono::system_clock::time_point{};
    std::vector<striptool::Sample> samples;
    for (int i = 0; i < 1000; ++i) {
      double value = 10.0;
      if (i == 401) value = 250.0;
      if (i >= 650) value = 40.0;
      samples.push_back({base + std::chrono::milliseconds(i), value, 0, 0});
    }
    const auto reduced = striptool::decimateSamples(samples, 24);
    QVERIFY(reduced.size() <= std::size_t{24});
    QVERIFY(std::any_of(reduced.begin(), reduced.end(), [](const auto& sample) {
      return sample.value == 250.0;
    }));
    const auto spike = std::find_if(reduced.begin(), reduced.end(), [](const auto& sample) {
      return sample.value == 250.0;
    });
    QVERIFY(spike != reduced.end());
    QVERIFY(std::next(spike) != reduced.end());
    QCOMPARE(std::next(spike)->timestamp, base + std::chrono::milliseconds(402));
    QCOMPARE(std::next(spike)->value, 10.0);
    QVERIFY(std::any_of(reduced.begin(), reduced.end(), [](const auto& sample) {
      return sample.value == 40.0;
    }));
    QVERIFY(std::is_sorted(reduced.begin(), reduced.end(), [](const auto& a, const auto& b) {
      return a.timestamp < b.timestamp;
    }));
  }
  void denseGapsRetainDataWithoutJoiningDisconnectedSegments() {
    const auto base = std::chrono::system_clock::time_point{};
    std::vector<striptool::Sample> samples;
    for (int i = 0; i < 100; ++i)
      samples.push_back({base + std::chrono::seconds(i), double(i), 0, 0,
                         i % 2 == 0});
    const auto reduced = striptool::decimateSamples(samples, 8);
    QVERIFY(reduced.size() <= std::size_t{8});
    QVERIFY(std::any_of(reduced.begin(), reduced.end(),
                        [](const auto& sample) { return sample.plotable; }));
    QVERIFY(std::any_of(reduced.begin(), reduced.end(),
                        [](const auto& sample) { return !sample.plotable; }));
    for (std::size_t i = 1; i < reduced.size(); ++i)
      QVERIFY(!reduced[i - 1].plotable || !reduced[i].plotable);
    QVERIFY(striptool::sampleValueRange(reduced, striptool::ScaleMode::Linear));
    for (int gapPeriod = 2; gapPeriod <= 5; ++gapPeriod) {
      for (auto& sample : samples) {
        const auto offset = std::chrono::duration_cast<std::chrono::seconds>(
            sample.timestamp - base).count();
        sample.plotable = offset % gapPeriod != 1;
      }
      for (std::size_t budget = 3; budget <= 12; ++budget) {
        const auto points = striptool::decimateSamples(samples, budget);
        QVERIFY(points.size() <= budget);
        QVERIFY(std::is_sorted(points.begin(), points.end(),
                               [](const auto& a, const auto& b) {
                                 return a.timestamp < b.timestamp;
                               }));
        for (std::size_t i = 1; i < points.size(); ++i) {
          if (!points[i - 1].plotable || !points[i].plotable) continue;
          const auto first = std::chrono::duration_cast<std::chrono::seconds>(
              points[i - 1].timestamp - base).count();
          const auto last = std::chrono::duration_cast<std::chrono::seconds>(
              points[i].timestamp - base).count();
          for (auto original = first + 1; original < last; ++original)
            QVERIFY(samples[static_cast<std::size_t>(original)].plotable);
        }
      }
    }
  }
  void logDecimationKeepsNonPositiveGap() {
    const auto base = std::chrono::system_clock::time_point{};
    std::vector<striptool::Sample> samples;
    for (int i = 0; i < 20; ++i)
      samples.push_back({base + std::chrono::seconds(i), i == 10 ? 0.0 : 2.0, 0, 0});
    const auto selected = striptool::selectSamples(
        samples, samples.front().timestamp, samples.back().timestamp, 5,
        striptool::ScaleMode::Log10);
    QVERIFY(std::any_of(selected.begin(), selected.end(), [](const auto& sample) {
      return !sample.plotable && sample.value == 0.0;
    }));
    QVERIFY(selected.size() <= std::size_t{5});
  }
  void traceSelectionIsStableAsViewScrolls() {
    using std::chrono::hours;
    using std::chrono::seconds;
    // Ten hours of 1 Hz decay and top-up, drawn 800 pixels wide (45 s each).
    const auto base = std::chrono::system_clock::time_point{} + hours(1000);
    std::vector<striptool::Sample> samples;
    for (int i = 0; i < 36000; ++i)
      samples.push_back({base + seconds(i),
                         200.0 - (i % 120) * 0.01 + ((i * 7919) % 13) * 0.001, 0, 0});
    samples[20000].value = 250.0;
    samples[25000].plotable = false;
    const auto window = hours(10);
    const auto bucket = std::chrono::system_clock::duration(window) / 800;
    const auto between = [](const std::vector<striptool::Sample>& trace,
                            std::chrono::system_clock::time_point from,
                            std::chrono::system_clock::time_point to) {
      std::vector<std::tuple<std::int64_t, double, bool>> points;
      for (const auto& sample : trace)
        if (sample.timestamp >= from && sample.timestamp < to)
          points.emplace_back(sample.timestamp.time_since_epoch().count(),
                              sample.value, sample.plotable);
      return points;
    };
    const auto referenceEnd = base + seconds(35000);
    const auto reference = striptool::selectTraceSamples(
        samples, referenceEnd - window, referenceEnd, bucket);
    QVERIFY(reference.size() <= std::size_t{5 * 802});
    QVERIFY(reference.size() < samples.size() / 4);
    QVERIFY(std::is_sorted(reference.begin(), reference.end(), [](const auto& a, const auto& b) {
      return a.timestamp < b.timestamp;
    }));
    const auto spike = std::find_if(reference.begin(), reference.end(),
                                    [](const auto& sample) { return sample.value == 250.0; });
    QVERIFY(spike != reference.end() && std::next(spike) != reference.end());
    // A spike is held no longer than one pixel column.
    QVERIFY(std::next(spike)->timestamp - spike->timestamp < bucket);
    QVERIFY(std::any_of(reference.begin(), reference.end(),
                        [](const auto& sample) { return !sample.plotable; }));

    for (int shift = 1; shift <= 90; shift += 7) {
      const auto end = referenceEnd + seconds(shift);
      const auto trace = striptool::selectTraceSamples(samples, end - window, end, bucket);
      const auto from = end - window + 2 * bucket;
      const auto to = referenceEnd - 2 * bucket;
      QVERIFY2(between(trace, from, to) == between(reference, from, to),
               "Scrolling the view changed the trace away from its edges");
    }
  }
  void acquisitionTracksReconnectMetadataAndTimestamps() {
    FakeChannelProvider provider;
    striptool::AcquisitionManager acquisition(&provider);
    acquisition.setRefreshInterval(std::chrono::milliseconds(400));
    QCOMPARE(acquisition.refreshInterval(), std::chrono::milliseconds(400));
    acquisition.setStaleAfter(std::chrono::milliseconds(10));
    const auto id = acquisition.addChannel(QStringLiteral("test:pv"), 2);
    QCOMPARE(acquisition.metadata(id).connection, striptool::ConnectionState::Connecting);
    provider.publishConnection(id, striptool::ConnectionState::Connected);
    QCOMPARE(acquisition.metadata(id).connection, striptool::ConnectionState::Connected);
    striptool::ChannelMetadata metadata;
    metadata.connection = striptool::ConnectionState::Connected;
    metadata.units = "A";
    metadata.precision = 3;
    metadata.displayMinimum = -2.0;
    metadata.displayMaximum = 2.0;
    provider.publishMetadata(id, metadata);
    QCOMPARE(QString::fromStdString(acquisition.metadata(id).units), QStringLiteral("A"));
    provider.publishDescription(id, QStringLiteral("Power supply current"));
    QCOMPARE(acquisition.metadata(id).description, std::string("Power supply current"));
    provider.publishMetadata(id, metadata);
    QCOMPARE(acquisition.metadata(id).description, std::string("Power supply current"));

    const auto now = std::chrono::system_clock::now();
    provider.publishSample(id, {now, 1.5, 2, 1});
    QCOMPARE(acquisition.buffer(id)->size(), std::size_t{1});
    QCOMPARE(acquisition.buffer(id)->latest()->timestamp, now);
    QCOMPARE(acquisition.buffer(id)->latest()->value, 1.5);
    QCOMPARE(acquisition.buffer(id)->latest()->status, std::uint16_t{2});
    QCOMPARE(acquisition.buffer(id)->latest()->severity, std::uint16_t{1});
    QCOMPARE(acquisition.metadata(id).lastUpdate.value(), now);
    provider.publishSample(id, {now + std::chrono::seconds(1), 2.5, 0, 0});
    provider.publishSample(id, {now + std::chrono::seconds(2), 3.5, 0, 0});
    QCOMPARE(acquisition.buffer(id)->size(), std::size_t{2});
    QCOMPARE(acquisition.buffer(id)->samples().front().value, 2.5);
    acquisition.setBufferCapacity(1);
    QCOMPARE(acquisition.buffer(id)->size(), std::size_t{1});
    QCOMPARE(acquisition.buffer(id)->latest()->value, 3.5);

    provider.publishConnection(id, striptool::ConnectionState::Disconnected);
    QCOMPARE(acquisition.metadata(id).connection, striptool::ConnectionState::Disconnected);
    provider.publishConnection(id, striptool::ConnectionState::Connected);
    QCOMPARE(acquisition.metadata(id).connection, striptool::ConnectionState::Connected);
  }
  void nonFiniteDisplayLimitsFallBackToFiniteRange() {
    striptool::ChannelAccessProvider provider;
    striptool::AcquisitionManager acquisition(&provider);
    const auto id = acquisition.addChannel(QStringLiteral("QTST:LIMITS:NONFINITE"), 100);
    using Access = striptool::ChannelAccessProviderTestAccess;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    struct Case {
      double lowDisplay, highDisplay, lowControl, highControl, value, low, high;
    };
    for (const Case& c : {Case{-5, 5, 0, 1, 0, -5, 5},          // finite: unchanged
                          Case{0, nan, 1, 4, 2, 1, 4},          // NaN display limit
                          Case{-inf, inf, nan, 0, 10, 9, 11},   // no usable limits
                          Case{nan, nan, nan, nan, nan, -100, 100}}) {
      dbr_ctrl_double control{};
      std::strcpy(control.units, "mm");
      control.lower_disp_limit = c.lowDisplay;
      control.upper_disp_limit = c.highDisplay;
      control.lower_ctrl_limit = c.lowControl;
      control.upper_ctrl_limit = c.highControl;
      control.value = c.value;
      Access::control(provider, id, control);
      QTRY_VERIFY(acquisition.metadata(id).displayMinimum == c.low &&
                  acquisition.metadata(id).displayMaximum == c.high);
    }
  }
  void acquisitionShutdownDisconnectsActiveChannels() {
    FakeChannelProvider provider;
    {
      striptool::AcquisitionManager acquisition(&provider);
      acquisition.addChannel(QStringLiteral("one"));
      acquisition.addChannel(QStringLiteral("two"));
      QCOMPARE(provider.names.size(), 2);
    }
    QVERIFY(provider.names.isEmpty());
  }
  void samplingAndRefreshCadencesRemainIndependent() {
    FakeChannelProvider provider;
    striptool::AcquisitionManager acquisition(&provider);
    acquisition.setRefreshInterval(std::chrono::milliseconds(45));
    const auto id = acquisition.addChannel(QStringLiteral("timing:test"), 64);
    provider.publishConnection(id, striptool::ConnectionState::Connected);
    QSignalSpy refreshes(&acquisition,
                         &striptool::AcquisitionManager::displayRefreshRequested);
    const auto base = std::chrono::system_clock::from_time_t(1000);
    for (int i = 0; i < 1000; ++i)
      provider.publishSample(id, {base + std::chrono::microseconds(i), double(i),
                                  static_cast<std::uint16_t>(i % 7),
                                  static_cast<std::uint16_t>(i % 4)});
    QCOMPARE(acquisition.buffer(id)->size(), std::size_t{64});
    QCOMPARE(acquisition.buffer(id)->samples().front().value, 936.0);
    QCOMPARE(acquisition.buffer(id)->latest()->timestamp,
             base + std::chrono::microseconds(999));
    QCOMPARE(acquisition.buffer(id)->latest()->status, std::uint16_t{5});
    QCOMPARE(acquisition.buffer(id)->latest()->severity, std::uint16_t{3});
    QTest::qWait(115);
    QVERIFY(refreshes.count() >= 2);
    QCOMPARE(acquisition.buffer(id)->size(), std::size_t{64});
  }
  void staleStatusKeepsUnchangedValuePlotable() {
    FakeChannelProvider provider;
    striptool::AcquisitionManager acquisition(&provider);
    acquisition.setStaleAfter(std::chrono::milliseconds(0));
    const auto id = acquisition.addChannel(QStringLiteral("test:stale"));
    provider.publishConnection(id, striptool::ConnectionState::Connected);
    provider.publishSample(id, {std::chrono::system_clock::now() - std::chrono::seconds(1),
                                1.0, 0, 0});
    QSignalSpy metadataSpy(&acquisition,
                           &striptool::AcquisitionManager::channelMetadataChanged);
    acquisition.checkStaleNow();
    QCOMPARE(acquisition.metadata(id).connection, striptool::ConnectionState::Stale);
    QCOMPARE(acquisition.buffer(id)->size(), std::size_t{1});
    QVERIFY(acquisition.buffer(id)->latest()->plotable);
    QCOMPARE(acquisition.buffer(id)->latest()->value, 1.0);
    QVERIFY(metadataSpy.count() >= 1);
  }
  void disconnectedChannelDoesNotRepeatOldSamples() {
    FakeChannelProvider provider;
    striptool::AcquisitionManager acquisition(&provider);
    const auto id = acquisition.addChannel(QStringLiteral("test:disconnect"));
    provider.publishConnection(id, striptool::ConnectionState::Connected);
    provider.publishSample(id, {std::chrono::system_clock::now(), 42.0, 0, 0});
    QCOMPARE(acquisition.buffer(id)->size(), std::size_t{1});
    provider.publishConnection(id, striptool::ConnectionState::Disconnected);
    QCOMPARE(acquisition.buffer(id)->size(), std::size_t{2});
    QVERIFY(!acquisition.buffer(id)->latest()->plotable);
    provider.publishConnection(id, striptool::ConnectionState::Connected);
    provider.publishSample(id, {std::chrono::system_clock::now(), 43.0, 0, 0});
    QCOMPARE(acquisition.buffer(id)->size(), std::size_t{3});
    QVERIFY(acquisition.buffer(id)->latest()->plotable);
    acquisition.clearSamples();
    QVERIFY(acquisition.buffer(id)->empty());
  }
  void cpuUsageIsASeparateLocalProvider() {
    striptool::CpuUsageProvider provider;
    provider.setSampleInterval(std::chrono::milliseconds(125));
    QCOMPARE(provider.sampleInterval(), std::chrono::milliseconds(125));
    const auto id = provider.connectChannel(QStringLiteral("CPU_Usage"));
    QSignalSpy samples(&provider, &striptool::ChannelProvider::sampleReceived);
    QTRY_VERIFY_WITH_TIMEOUT(samples.count() >= 1, 500);
    const auto sample = qvariant_cast<striptool::Sample>(samples.last().at(1));
    QVERIFY(sample.value >= 0.0 && sample.value <= 100.0);
    provider.disconnectChannel(id);
  }
  void plotDataSelectionAndHistoryJoinAreToolkitNeutral() {
    const auto base = std::chrono::system_clock::time_point{};
    std::vector<striptool::Sample> live{
        {base + std::chrono::seconds(2), 20.0, 0, 0},
        {base + std::chrono::seconds(3), 30.0, 0, 0}};
    std::vector<striptool::Sample> history{
        {base, 0.0, 0, 0},
        {base + std::chrono::seconds(1), 10.0, 0, 0},
        {base + std::chrono::seconds(2), -1.0, 0, 0}};
    const auto joined = striptool::joinHistoricalAndLive(history, live);
    QCOMPARE(joined.size(), std::size_t{4});
    QCOMPARE(joined[2].value, 20.0);
    const auto selected = striptool::selectSamples(
        joined, base + std::chrono::seconds(1),
        base + std::chrono::seconds(2), 10);
    QCOMPARE(selected.size(), std::size_t{2});
    QCOMPARE(striptool::plotValue(100.0, striptool::ScaleMode::Log10), 2.0);
    QVERIFY(std::isnan(striptool::plotValue(-1.0, striptool::ScaleMode::Log10)));
  }
  void plotWidgetSupportsViewAndAnnotationOperations() {
    auto model = striptool::makeDefaultModel();
    model.timing.timespanSeconds = 10;
    model.curves[0].name = "linear";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0;
    model.curves[0].maximum = 10;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    model.curves[1].name = "log";
    model.curves[1].nameSet = true;
    model.curves[1].scale = striptool::ScaleMode::Log10;
    model.curves[1].minimum = 1;
    model.curves[1].maximum = 1000;
    model.curves[1].minimumSet = true;
    model.curves[1].maximumSet = true;
    const auto base = std::chrono::system_clock::now();
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    plot.setCurveSamples(0, {{base, 0, 0, 0},
                             {base + std::chrono::seconds(10), 10, 0, 0}});
    plot.setCurveSamples(1, {{base, 1, 0, 0},
                             {base + std::chrono::seconds(5), 10, 0, 0},
                             {base + std::chrono::seconds(10), 1000, 0, 0}});
    plot.resetVerticalView();
    QCOMPARE(plot.valueRange(1).minimum, 0.0);
    QCOMPARE(plot.valueRange(1).maximum, 3.0);
    const auto original = plot.visibleTimeRange();
    plot.setPaused(true);
    plot.pan(-0.5);
    QVERIFY(plot.visibleTimeRange().start < original.start);
    plot.zoom(0.5);
    QVERIFY(plot.visibleTimeRange().end - plot.visibleTimeRange().start <
            original.end - original.start);
    plot.resetView();
    QVERIFY(plot.autoScroll());
    const int annotation = plot.addAnnotation({base, 5.0, "event"});
    QCOMPARE(annotation, 0);
    QCOMPARE(plot.selectedAnnotation(), 0);
    QVERIFY(plot.updateAnnotation(0, {base, 6.0, "edited"}));
    QVERIFY(plot.removeAnnotation(0));
    QCOMPARE(plot.selectedAnnotation(), -1);
  }
  void liveAndHistoricalTracesRenderAsStepsWithoutCrossingGaps() {
    auto model = striptool::makeDefaultModel();
    model.colors.background = {65535, 65535, 65535, 65535};
    model.colors.curves[0] = {0, 0, 65535, 65535};
    model.graph.lineWidth = 3;
    model.curves[0].name = "step:test";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0.0;
    model.curves[0].maximum = 10.0;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    const auto base = std::chrono::system_clock::from_time_t(1000);
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    plot.setCurveSamples(0, {{base, 2.0, 0, 0},
                             {base + std::chrono::seconds(5), 8.0, 0, 0}});
    plot.joinHistoricalSamples(0, {
        {base - std::chrono::seconds(10), 7.0, 0, 0},
        {base - std::chrono::seconds(5), 0.0, 0, 0, false}});
    plot.setVisibleTimeRange({base - std::chrono::seconds(10),
                              base + std::chrono::seconds(10)});
    plot.resetVerticalView();
    QImage image(plot.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    plot.render(&image);

    const auto hasBlueNear = [&image](int centerX, int centerY) {
      for (int y = centerY - 3; y <= centerY + 3; ++y)
        for (int x = centerX - 3; x <= centerX + 3; ++x) {
          const QColor pixel = image.pixelColor(x, y);
          if (pixel.blue() > 150 && pixel.red() < 100) return true;
        }
      return false;
    };
    int left = -1;
    int right = -1;
    int top = -1;
    int bottom = -1;
    for (int x = 0; x < plot.width(); ++x)
      if (plot.isInPlot(QPoint(x, plot.height() / 2))) {
        if (left < 0) left = x;
        right = x;
      }
    for (int y = 0; y < plot.height(); ++y)
      if (plot.isInPlot(QPoint((left + right) / 2, y))) {
        if (top < 0) top = y;
        bottom = y;
      }
    const auto xAt = [left, right](double fraction) {
      return int(std::lround(left + (right - left) * fraction));
    };
    const auto yAt = [top, bottom](double value) {
      return int(std::lround(bottom - (bottom - top) * value / 10.0));
    };
    QVERIFY(!hasBlueNear(xAt(0.375), yAt(7.0))); // no step across the gap
    QVERIFY(hasBlueNear(xAt(0.625), yAt(2.0)));  // held live value
    QVERIFY(!hasBlueNear(xAt(0.625), yAt(5.0))); // not a diagonal interpolation

    model.curves[0].scale = striptool::ScaleMode::Log10;
    model.curves[0].minimum = 1.0;
    model.curves[0].maximum = 100.0;
    plot.clearSamples();
    plot.setModel(model);
    plot.joinHistoricalSamples(0, {{base, 10.0, 0, 0},
                                    {base + std::chrono::seconds(5), 100.0, 0, 0}});
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(10)});
    image.fill(Qt::white);
    plot.render(&image);
    QVERIFY(hasBlueNear(xAt(0.125), yAt(5.0)) ||
            hasBlueNear(xAt(0.125), yAt(10.0)));
  }
  void stepTraceStopsAtPresentTime() {
    auto model = striptool::makeDefaultModel();
    model.colors.background = {65535, 65535, 65535, 65535};
    model.colors.curves[0] = {0, 0, 65535, 65535};
    model.graph.lineWidth = 3;
    model.curves[0].name = "present:test";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0.0;
    model.curves[0].maximum = 10.0;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    const auto now = std::chrono::system_clock::now();
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    plot.setCurveSamples(0, {{now - std::chrono::seconds(10), 5.0, 0, 0}});
    plot.setVisibleTimeRange({now - std::chrono::seconds(20),
                              now + std::chrono::seconds(20)});
    QImage image(plot.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    plot.render(&image);

    int left = -1;
    int right = -1;
    int top = -1;
    int bottom = -1;
    for (int x = 0; x < plot.width(); ++x)
      if (plot.isInPlot(QPoint(x, plot.height() / 2))) {
        if (left < 0) left = x;
        right = x;
      }
    for (int y = 0; y < plot.height(); ++y)
      if (plot.isInPlot(QPoint((left + right) / 2, y))) {
        if (top < 0) top = y;
        bottom = y;
      }
    const int y = int(std::lround(bottom - (bottom - top) * 0.5));
    const auto hasBlueNear = [&image, y](int centerX) {
      for (int py = y - 3; py <= y + 3; ++py)
        for (int px = centerX - 3; px <= centerX + 3; ++px) {
          const QColor pixel = image.pixelColor(px, py);
          if (pixel.blue() > 150 && pixel.red() < 100) return true;
        }
      return false;
    };
    QVERIFY(hasBlueNear(int(std::lround(left + (right - left) * 0.40))));
    QVERIFY(!hasBlueNear(int(std::lround(left + (right - left) * 0.75))));
  }
  void mouseWheelZoomKeepsPointerTimestampFixed() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "wheel:test";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0.0;
    model.curves[0].maximum = 10.0;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    const auto base = std::chrono::system_clock::from_time_t(1000);
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(100)});

    int left = -1;
    int right = -1;
    for (int x = 0; x < plot.width(); ++x) {
      if (!plot.isInPlot(QPoint(x, plot.height() / 2))) continue;
      if (left < 0) left = x;
      right = x;
    }
    QVERIFY(left >= 0);
    QVERIFY(right > left);
    const QPoint position(left + (right - left) / 4, plot.height() / 2);
    const double fraction = double(position.x() - left) / (right - left);
    const auto anchorBefore = plot.visibleTimeRange().start +
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double>(100.0 * fraction));

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QWheelEvent wheel(QPointF(position), QPointF(plot.mapToGlobal(position)),
                      QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
#else
    QWheelEvent wheel(QPointF(position), QPointF(plot.mapToGlobal(position)),
                      QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
#endif
    QApplication::sendEvent(&plot, &wheel);

    const auto zoomed = plot.visibleTimeRange();
    const double zoomedSeconds = std::chrono::duration<double>(
        zoomed.end - zoomed.start).count();
    QVERIFY(std::abs(zoomedSeconds - 80.0) < 1e-6);
    const auto anchorAfter = zoomed.start +
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double>(zoomedSeconds * fraction));
    const double anchorShift = std::abs(std::chrono::duration<double>(
        anchorAfter - anchorBefore).count());
    QVERIFY2(anchorShift < 0.001,
             "Mouse-wheel zoom moved the timestamp under the pointer");
  }
  void narrowYAxisTicksRemainDistinct() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "S-DCCT:CurrentM";
    model.curves[0].nameSet = true;
    model.curves[0].precision = 2;
    model.curves[0].minimum = 99.991;
    model.curves[0].maximum = 100.009;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);

    const QStringList labels = plot.yAxisLabels(0);
    QCOMPARE(labels.size(), 6);
    for (int i = 1; i < labels.size(); ++i)
      QVERIFY2(labels[i] != labels[i - 1],
               "A narrow Y-axis range produced duplicate tick labels");
    QVERIFY(labels.front().contains(QLatin1Char('.')));

    plot.resize(800, 500);
    const QFontMetrics metrics(plot.font());
    int widestLabel = 0;
    for (const QString& label : labels)
      widestLabel = std::max(widestLabel, metrics.horizontalAdvance(label));
    const int firstPlotPixel = [&plot] {
      for (int x = 0; x < plot.width(); ++x)
        if (plot.isInPlot(QPoint(x, plot.height() / 2))) return x;
      return -1;
    }();
    QVERIFY(firstPlotPixel >= 22 + 8 + widestLabel + 8);
  }
  void yAxisUsesConciseSignificantDigits() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].nameSet = true;
    model.curves[0].precision = 12;
    model.curves[0].minimum = 0.006;
    model.curves[0].maximum = 199.7957;
    model.curves[0].minimumSet = model.curves[0].maximumSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);
    QCOMPARE(plot.yAxisLabels(0).front(), QStringLiteral("199.8"));
    for (const auto& label : plot.yAxisLabels(0)) QVERIFY(label.size() <= 6);
    model.curves[0].scale = striptool::ScaleMode::Log10;
    model.curves[0].minimum = 1e-12;
    model.curves[0].maximum = 1e-6;
    plot.setModel(model);
    const auto logarithmic = plot.yAxisLabels(0);
    for (int i = 1; i < logarithmic.size(); ++i) QVERIFY(logarithmic[i] != logarithmic[i - 1]);
    QCOMPARE(logarithmic.back(), QStringLiteral("1e-12"));

    model.curves[0].scale = striptool::ScaleMode::Linear;
    const auto linearLabels = [&](double minimum, double maximum) {
      model.curves[0].minimum = minimum;
      model.curves[0].maximum = maximum;
      plot.setModel(model);
      return plot.yAxisLabels(0);
    };
    QCOMPARE(linearLabels(0, 250000),
             (QStringList{"250000", "200000", "150000", "100000", "50000", "0"}));
    QCOMPARE(linearLabels(12000, 13000),
             (QStringList{"13000", "12800", "12600", "12400", "12200", "12000"}));
    QCOMPARE(linearLabels(-0.02, 0.03),
             (QStringList{"0.03", "0.02", "0.01", "0", "-0.01", "-0.02"}));
    QCOMPARE(linearLabels(-0.05, 0.15),
             (QStringList{"0.15", "0.11", "0.07", "0.03", "-0.01", "-0.05"}));
    QCOMPARE(plot.yAxisLabels(0, 4),
             (QStringList{"0.15", "0.1", "0.05", "0", "-0.05"}));
    QCOMPARE(linearLabels(0.0012, 0.0017).front(), QStringLiteral("0.0017"));
    const auto large = linearLabels(0, 5e9);
    QCOMPARE(large.front(), QStringLiteral("5e+09"));
    for (int i = 1; i < large.size(); ++i) QVERIFY(large[i] != large[i - 1]);
  }
  void gridLinesFollowLabelledTicks() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "S-DCCT:CurrentM";
    model.curves[0].nameSet = true;
    model.curves[0].units = "mA";
    model.curves[0].minimum = 0.006;
    model.curves[0].maximum = 199.7957;
    model.curves[0].minimumSet = model.curves[0].maximumSet = true;
    model.colors.grid = striptool::Rgba16{65535, 0, 65535, 65535};
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    const auto end = std::chrono::system_clock::from_time_t(1000000);
    plot.setVisibleTimeRange({end - std::chrono::minutes(5), end});

    // Returns an empty string when every grid line sits on a labelled tick,
    // or midway between ticks for GridMode::All.
    const auto gridMismatch = [&plot](int linesPerTick) -> QString {
      QImage image(plot.size(), QImage::Format_ARGB32_Premultiplied);
      plot.render(&image);
      int left = 0, top = 0;
      while (!plot.isInPlot(QPoint(left, plot.height() / 2))) ++left;
      int right = left;
      while (plot.isInPlot(QPoint(right + 1, plot.height() / 2))) ++right;
      while (!plot.isInPlot(QPoint(plot.width() / 2, top))) ++top;
      int bottom = top;
      while (plot.isInPlot(QPoint(plot.width() / 2, bottom + 1))) ++bottom;
      const auto check = [&](bool vertical, int divisions) -> QString {
        const int from = vertical ? left : top, to = vertical ? right : bottom;
        const int acrossFrom = vertical ? top : left, acrossTo = vertical ? bottom : right;
        QList<int> found, expected;
        for (int p = from + 2; p <= to - 2; ++p) {
          int count = 0;
          for (int q = acrossFrom; q <= acrossTo; ++q)
            if (image.pixelColor(vertical ? p : q, vertical ? q : p) == QColor(255, 0, 255))
              ++count;
          if (count > (acrossTo - acrossFrom) / 4) found.push_back(p);
        }
        for (int i = 1; i < divisions; ++i)
          expected.push_back(qRound(from + double(to - from) * i / divisions));
        bool same = found.size() == expected.size();
        for (int i = 0; same && i < found.size(); ++i)
          same = std::abs(found[i] - expected[i]) <= 1;
        if (same) return {};
        QStringList foundText, expectedText;
        for (int x : found) foundText << QString::number(x);
        for (int x : expected) expectedText << QString::number(x);
        return QStringLiteral("%1 grid at %2, ticks expect %3")
            .arg(vertical ? "Time" : "Value", foundText.join(','), expectedText.join(','));
      };
      const QString time = check(true, plot.timeAxisDivisions() * linesPerTick);
      return time.isEmpty() ? check(false, plot.valueAxisDivisions() * linesPerTick) : time;
    };

    QCOMPARE(plot.timeAxisDivisions(), 5);
    QCOMPARE(plot.valueAxisDivisions(), 5);
    QCOMPARE(plot.xAxisLabels(5).relative, (QStringList{"-5", "-4", "-3", "-2", "-1", "0"}));
    QString mismatch = gridMismatch(1);
    QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));

    model.graph.xGrid = model.graph.yGrid = striptool::GridMode::All;
    plot.setModel(model);
    mismatch = gridMismatch(2);
    QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));

    model.graph.xGrid = model.graph.yGrid = striptool::GridMode::Some;
    plot.setModel(model);
    QFont font = plot.font();
    font.setPointSize(18);
    plot.setFont(font);
    plot.resize(960, 300);
    plot.setVisibleTimeRange({end - std::chrono::hours(2), end});
    QVERIFY(plot.valueAxisDivisions() < 5);
    mismatch = gridMismatch(1);
    QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));
  }
  void timeAxisUnitsFollowVisibleDuration() {
    striptool::PlotWidget plot;
    const auto end = std::chrono::system_clock::from_time_t(1000000);
    plot.setVisibleTimeRange({end - std::chrono::seconds(300), end});
    auto labels = plot.xAxisLabels();
    QCOMPARE(labels.relative.front(), QStringLiteral("-5"));
    QCOMPARE(labels.relative.back(), QStringLiteral("0"));
    QVERIFY(labels.caption.startsWith(QStringLiteral("Minutes")));
    QCOMPARE(labels.clock.front().count(QLatin1Char(':')), 2);
    plot.setVisibleTimeRange({end - std::chrono::hours(2), end});
    labels = plot.xAxisLabels();
    QCOMPARE(labels.relative.front(), QStringLiteral("-2"));
    QCOMPARE(labels.relative.back(), QStringLiteral("0"));
    QVERIFY(labels.caption.startsWith(QStringLiteral("Hours")));
    QCOMPARE(labels.clock.front().count(QLatin1Char(':')), 1);
    plot.setVisibleTimeRange({end - std::chrono::hours(48), end});
    labels = plot.xAxisLabels();
    QVERIFY(labels.caption.startsWith(QStringLiteral("Days")));
    QCOMPARE(labels.relative.front(), QStringLiteral("-2"));
    QVERIFY(labels.clock.front() != labels.clock.back());
    plot.setVisibleTimeRange({end - std::chrono::seconds(5), end});
    labels = plot.xAxisLabels();
    QVERIFY(labels.caption.startsWith(QStringLiteral("Seconds")));
    QVERIFY(labels.clock.front().contains(QLatin1Char('.')));
  }
  void autoScrollAndLiveZoomPreserveDuration() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);
    const auto oldEnd = std::chrono::system_clock::now() - std::chrono::hours(1);
    plot.setVisibleTimeRange({oldEnd - std::chrono::hours(2), oldEnd});
    plot.setAutoScroll(true);
    auto range = plot.visibleTimeRange();
    QCOMPARE(range.end - range.start, std::chrono::hours(2));
    QVERIFY(range.end > oldEnd);
    plot.zoom(2.0);
    QVERIFY(plot.autoScroll());
    QCOMPARE(plot.visibleTimeRange().end, range.end);
    const auto checkDuration = [&plot] {
      const auto visible = plot.visibleTimeRange();
      return visible.end - visible.start == std::chrono::hours(4);
    };
    QVERIFY(checkDuration());
    plot.setCurveSamples(0, {{std::chrono::system_clock::now(), 100.0, 0, 0}});
    QVERIFY(checkDuration());
    plot.appendSample(0, {std::chrono::system_clock::now(), 101.0, 0, 0});
    QVERIFY(checkDuration());
    plot.advanceToNow();
    QVERIFY(checkDuration());
    plot.setPaused(true);
    range = plot.visibleTimeRange();
    plot.advanceToNow();
    QCOMPARE(plot.visibleTimeRange().end, range.end);
    plot.setPaused(false);
    QVERIFY(checkDuration());
    QCOMPARE(plot.xAxisLabels().relative.back(), QStringLiteral("0"));
    plot.resetView();
    range = plot.visibleTimeRange();
    QCOMPARE(range.end - range.start, std::chrono::seconds(300));
  }
  void plotAreaIgnoresLatestValueWidth() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "S-DCCT:CurrentM";
    model.curves[0].nameSet = true;
    model.curves[0].precision = 5;
    model.curves[0].minimum = -10;
    model.curves[0].maximum = 220;
    model.curves[0].minimumSet = model.curves[0].maximumSet = true;
    striptool::PlotWidget plot;
    plot.resize(960, 400);
    plot.setModel(model);
    plot.resetVerticalView();
    const auto rightEdge = [&plot] {
      int right = plot.width() - 1;
      while (right > 0 && !plot.isInPlot(QPoint(right, plot.height() / 2))) --right;
      return right;
    };
    const auto now = std::chrono::system_clock::now();
    QList<int> edges;
    for (const double value : {197.54456, 197.5, -1.0, 0.0012345, 197.54})
      plot.setCurveSamples(0, {{now, value, 0, 0}}), edges.push_back(rightEdge());
    // A changing value width must not rescale the time axis under the trace.
    // Values within the precision's width never move it; a wider one widens
    // the legend once, and narrower values afterwards keep that width.
    QCOMPARE(edges[1], edges[0]);
    QCOMPARE(edges[2], edges[0]);
    QVERIFY(edges[3] < edges[0]);
    QCOMPARE(edges[4], edges[3]);
  }
  void legendShowsFullValueAndLimits() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "S-INJ:MPS:LifetimeMinutesM:WithAnExtraLongDescriptor";
    model.curves[0].nameSet = true;
    model.curves[0].precision = 12;
    model.curves[0].minimum = -250000;
    model.curves[0].maximum = 250000;
    model.curves[0].minimumSet = model.curves[0].maximumSet = true;
    striptool::PlotWidget plot;
    QFont font = plot.font();
    font.setPointSize(18);
    plot.setFont(font);
    // Wide enough for the value line beyond the cap on names, while the plot
    // keeps its 200 pixel minimum.
    plot.resize(1100, 400);
    plot.setModel(model);
    plot.resetVerticalView();
    plot.setCurveSamples(0, {{std::chrono::system_clock::now(), -123456.789012, 0, 0}});
    int right = plot.width() - 1;
    while (right > 0 && !plot.isInPlot(QPoint(right, plot.height() / 2))) --right;
    const QFontMetrics metrics(plot.font(), &plot);
    const int legendText = plot.width() - right - 25;
    // The name is elided, but the value and limits line is shown in full.
    QVERIFY(legendText < metrics.horizontalAdvance(QString::fromStdString(model.curves[0].name)));
    QVERIFY(legendText >= metrics.horizontalAdvance(
        QStringLiteral("-123456.789012  (-2.5e+05, 2.5e+05)")));
  }
  void largeFontsExpandPlotMarginsAndLegendRows() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "S-DCCT:CurrentM";
    model.curves[0].nameSet = true;
    model.curves[0].units = "mA";
    model.curves[1].name = "S-INJ:EfficiencyM";
    model.curves[1].nameSet = true;
    model.curves[1].units = "percent";
    striptool::PlotWidget plot;
    QFont font = plot.font();
    font.setPointSize(9);
    plot.setFont(font);
    plot.resize(960, 300);
    plot.setModel(model);
    const auto end = std::chrono::system_clock::now();
    plot.setCurveSamples(0, {{end - std::chrono::hours(2), 0.006, 0, 0}, {end, 197.54456, 0, 0}});
    plot.setCurveSamples(1, {{end - std::chrono::hours(2), 25.0, 0, 0}, {end, 73.851, 0, 0}});
    plot.setVisibleTimeRange({end - std::chrono::hours(2), end});
    const auto bounds = [&plot] {
      int row = 0;
      while (row < plot.height() && !plot.isInPlot(QPoint(plot.width() / 2, row))) ++row;
      if (row == plot.height()) return QRect{};
      int left = 0, right = plot.width() - 1;
      while (!plot.isInPlot(QPoint(left, row))) ++left;
      while (!plot.isInPlot(QPoint(right, row))) --right;
      int bottom = plot.height() - 1;
      while (!plot.isInPlot(QPoint((left + right) / 2, bottom))) --bottom;
      return QRect(left, 0, right - left + 1, bottom + 1);
    };
    const QRect small = bounds();
    const int smallBottomMargin = plot.height() - small.bottom();
    font.setPointSize(18);
    plot.setFont(font);
    const QRect large = bounds();
    QVERIFY(large.isValid());
    QVERIFY(large.left() > small.left());
    QVERIFY(large.right() < small.right());
    QVERIFY(plot.height() - large.bottom() > smallBottomMargin);
    const QFontMetrics metrics(plot.font(), &plot);
    QVERIFY(plot.width() - large.right() >= metrics.horizontalAdvance(QString::fromStdString(model.curves[1].name)) + 36);
    QImage image(plot.size(), QImage::Format_ARGB32_Premultiplied);
    plot.render(&image);
    const QString output = qEnvironmentVariable("QTSTRIPTOOL_VISUAL_OUTPUT");
    if (!output.isEmpty()) {
      const auto directory = std::filesystem::path(output.toStdString()).parent_path();
      QVERIFY(image.save(QString::fromStdString((directory / "large-font.png").string())));
    }
    for (auto& curve : model.curves) curve.nameSet = true;
    plot.setModel(model);
    QVERIFY(plot.height() >= 10 * 2 * metrics.height());
    const auto fullBounds = bounds();
    image = QImage(plot.size(), QImage::Format_ARGB32_Premultiplied);
    plot.render(&image);
    // The rightmost time labels are centred on the axis end, so skip their
    // overhang; anything further right would be legend text.
    int labelOverhang = 0;
    const auto timeLabels = plot.xAxisLabels(plot.timeAxisDivisions());
    for (const auto& labels : {timeLabels.relative, timeLabels.clock})
      for (const QString& label : labels)
        labelOverhang = std::max(labelOverhang, (metrics.horizontalAdvance(label) + 8) / 2 + 1);
    for (int y = fullBounds.bottom() + 6; y < image.height(); ++y)
      for (int x = fullBounds.right() + std::max(12, labelOverhang + 2); x < image.width(); ++x)
        QVERIFY2(image.pixelColor(x, y) == QColor(Qt::white),
                 "Legend text and time labels overlap below the plot");
  }
  void visibleRangeIncludesCrossingSegments() {
    const auto base = std::chrono::system_clock::from_time_t(1000);
    const std::vector<striptool::Sample> crossing{
        {base - std::chrono::seconds(5), 0.0, 0, 0},
        {base + std::chrono::seconds(15), 100.0, 0, 0}};
    const auto linear = striptool::visibleSampleValueRange(
        crossing, base, base + std::chrono::seconds(10),
        striptool::ScaleMode::Linear);
    QVERIFY(linear);
    QCOMPARE(linear->minimum, -1.0);
    QCOMPARE(linear->maximum, 1.0);

    auto disconnected = crossing;
    disconnected[0].plotable = false;
    QVERIFY(!striptool::visibleSampleValueRange(
        disconnected, base, base + std::chrono::seconds(10),
        striptool::ScaleMode::Linear));

    auto logarithmic = crossing;
    logarithmic[0].value = 1.0;
    logarithmic[1].value = 10000.0;
    const auto logRange = striptool::visibleSampleValueRange(
        logarithmic, base, base + std::chrono::seconds(10),
        striptool::ScaleMode::Log10);
    QVERIFY(logRange);
    QCOMPARE(logRange->minimum, -1.0);
    QCOMPARE(logRange->maximum, 1.0);

    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "crossing";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);
    plot.setCurveSamples(0, crossing);
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(10)});
    QCOMPARE(plot.valueRange(0).minimum, -1.0);
    QCOMPARE(plot.valueRange(0).maximum, 1.0);
  }
  void legendSelectionChangesCursorCurve() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "first:pv";
    model.curves[0].nameSet = true;
    model.curves[1].name = "second:pv";
    model.curves[1].nameSet = true;
    model.curves[1].comment = "Complete channel description";
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    plot.show();
    QSignalSpy cursor(&plot, &striptool::PlotWidget::cursorLocationChanged);
    QPoint secondLegend;
    for (int y = 10; y < plot.height(); y += 8) {
      const QPoint point(plot.width() - 30, y);
      moveWhileDragging(&plot, point, Qt::NoButton);
      if (plot.toolTip().contains(QStringLiteral("Complete channel description"))) {
        secondLegend = point;
        break;
      }
    }
    QVERIFY(plot.toolTip().contains(QStringLiteral("Complete channel description")));
    QTest::mouseClick(&plot, Qt::LeftButton, Qt::NoModifier, secondLegend);
    moveWhileDragging(&plot, QPoint(400, 200), Qt::NoButton);
    QVERIFY(!cursor.isEmpty());
    QCOMPARE(cursor.last().at(2).toInt(), 1);
  }
  void plotMouseButtonsSelectMovePanAndShowCommands() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "test:pv";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0;
    model.curves[0].maximum = 10;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    const auto base = std::chrono::system_clock::from_time_t(1000);
    striptool::MainWindow window(model);
    window.resize(900, 620);
    window.show();
    auto* plot = window.plotWidget();
    plot->setVisibleTimeRange({base, base + std::chrono::seconds(100)});
    const QPoint original(440, 280);
    QVERIFY(plot->isInPlot(original));
    const int annotation = plot->addAnnotationAt(original, QStringLiteral("test note"));
    QCOMPARE(annotation, 0);
    plot->selectAnnotation(-1);
    QTest::mouseClick(plot, Qt::LeftButton, Qt::NoModifier, original + QPoint(4, 4));
    QCOMPARE(plot->selectedAnnotation(), 0);
    const auto selectedTime = plot->model().annotations[0].time;
    const auto selectedValue = *plot->model().annotations[0].value;
    QSignalSpy middleSelection(plot, &striptool::PlotWidget::annotationSelectionChanged);
    QTest::mousePress(plot, Qt::MiddleButton, Qt::NoModifier, original + QPoint(4, 4));
    QVERIFY(!middleSelection.isEmpty());
    moveWhileDragging(plot, original + QPoint(44, 34), Qt::MiddleButton);
    QCOMPARE(window.model().annotations[0].time,
             plot->model().annotations[0].time);
    window.controlsWindow()->modelChanged();
    QTest::mouseRelease(plot, Qt::MiddleButton, Qt::NoModifier,
                        original + QPoint(44, 34));
    QVERIFY(plot->model().annotations[0].time > selectedTime);
    QVERIFY(*plot->model().annotations[0].value < selectedValue);
    QCOMPARE(window.model().annotations[0].time, plot->model().annotations[0].time);
    plot->selectAnnotation(-1);
    QTest::mouseClick(plot, Qt::LeftButton, Qt::NoModifier,
                      original + QPoint(44, 34));
    QCOMPARE(plot->selectedAnnotation(), 0);
    QSignalSpy contextRequests(plot, &striptool::PlotWidget::plotContextMenuRequested);
    QTest::mouseClick(plot, Qt::RightButton, Qt::NoModifier, QPoint(500, 300));
    auto* menu = window.findChild<QMenu*>(QStringLiteral("plotContextMenu"));
    QVERIFY(menu);
    QCOMPARE(contextRequests.count(), 0);
    QContextMenuEvent nativeMenu(QContextMenuEvent::Mouse, QPoint(500, 300),
                                 plot->mapToGlobal(QPoint(500, 300)));
    QApplication::sendEvent(plot, &nativeMenu);
    QCOMPARE(contextRequests.count(), 1);
    QVERIFY(nativeMenu.isAccepted());
    QTRY_VERIFY(menu->isVisible());
    QVERIFY(window.findChild<QAction*>(QStringLiteral("newAnnotationAction"))->isEnabled());
    QVERIFY(window.findChild<QAction*>(QStringLiteral("editAnnotationAction"))->isEnabled());
    QVERIFY(window.findChild<QAction*>(QStringLiteral("deleteAnnotationAction"))->isEnabled());
    QVERIFY(menu->actions().contains(window.findChild<QAction*>(QStringLiteral("printAction"))));
    menu->hide();
    QContextMenuEvent keyboardMenu(QContextMenuEvent::Keyboard, QPoint(500, 300),
                                   plot->mapToGlobal(QPoint(500, 300)));
    QApplication::sendEvent(plot, &keyboardMenu);
    QCOMPARE(contextRequests.count(), 2);
    QVERIFY(keyboardMenu.isAccepted());
    QTRY_VERIFY(menu->isVisible());
    menu->hide();
    const auto afterMove = plot->visibleTimeRange();
    QTest::mousePress(plot, Qt::LeftButton, Qt::NoModifier, QPoint(560, 420));
    moveWhileDragging(plot, QPoint(590, 420), Qt::LeftButton);
    QTest::mouseRelease(plot, Qt::LeftButton, Qt::NoModifier, QPoint(590, 420));
    QVERIFY(plot->visibleTimeRange().start < afterMove.start);
    QCOMPARE(plot->selectedAnnotation(), -1);
    model.annotations = window.model().annotations;
    model.curves[0].nameSet = false;
    plot->setModel(model);
    QVERIFY(plot->model().annotations.empty());
    QVERIFY(window.model().annotations.empty());
  }
  void middleDragKeepsLiveViewStableUntilRelease() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "test:pv";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    plot.show();
    const auto first = std::chrono::system_clock::now() - std::chrono::seconds(10);
    plot.setCurveSamples(0, {{first, 5, 0, 0}});
    const QPoint start(400, 240);
    QVERIFY(plot.addAnnotationAt(start, QStringLiteral("drag me")) >= 0);
    const auto original = plot.visibleTimeRange();
    const auto originalValues = plot.valueRange(0);
    const auto annotationTime = plot.model().annotations[0].time;
    QTest::mousePress(&plot, Qt::MiddleButton, Qt::NoModifier, start + QPoint(4, 4));
    const auto later = first + std::chrono::seconds(20);
    plot.setCurveSamples(0, {{first, 5, 0, 0}, {later, 9, 0, 0}});
    plot.advanceToNow();
    QCOMPARE(plot.visibleTimeRange().end, original.end);
    QCOMPARE(plot.valueRange(0).maximum, originalValues.maximum);
    moveWhileDragging(&plot, start + QPoint(44, 4), Qt::MiddleButton);
    QVERIFY(plot.model().annotations[0].time > annotationTime);
    QTest::mouseRelease(&plot, Qt::MiddleButton, Qt::NoModifier,
                        start + QPoint(44, 4));
    QVERIFY(plot.visibleTimeRange().end > original.end);
    QVERIFY(plot.visibleTimeRange().end < later);
    QCOMPARE(plot.valueRange(0).maximum, originalValues.maximum);
    QVERIFY(plot.autoScroll());
  }
  void removingCurveRemapsAnnotationSelection() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "first";
    model.curves[0].nameSet = true;
    model.curves[1].name = "second";
    model.curves[1].nameSet = true;
    const auto time = std::chrono::system_clock::from_time_t(1000);
    model.annotations = {{time, 1.0, "first note", 0},
                         {time, 2.0, "second note", 1}};
    striptool::PlotWidget plot;
    plot.setModel(model);
    plot.selectAnnotation(1);
    auto changed = model;
    changed.curves[0].nameSet = false;
    plot.setModel(changed);
    QCOMPARE(plot.model().annotations.size(), std::size_t{1});
    QCOMPARE(plot.selectedAnnotation(), 0);
    QCOMPARE(plot.model().annotations[0].text, std::string("second note"));
    plot.setModel(model);
    plot.selectAnnotation(0);
    plot.setModel(changed);
    QCOMPARE(plot.selectedAnnotation(), -1);
  }
  void middleDragStopsAtPlotEdge() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "edge";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0;
    model.curves[0].maximum = 10;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    const auto base = std::chrono::system_clock::from_time_t(1000);
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(100)});
    plot.show();
    QVERIFY(plot.addAnnotationAt(QPoint(615, 200), QStringLiteral("edge")) >= 0);
    const auto originalTime = plot.model().annotations[0].time;
    const auto originalValue = plot.model().annotations[0].value;
    QTest::mousePress(&plot, Qt::MiddleButton, Qt::NoModifier, QPoint(605, 204));
    moveWhileDragging(&plot, QPoint(635, 204), Qt::MiddleButton);
    QCOMPARE(plot.model().annotations[0].time, originalTime);
    QCOMPARE(plot.model().annotations[0].value, originalValue);
    moveWhileDragging(&plot, QPoint(585, 204), Qt::MiddleButton);
    QVERIFY(plot.model().annotations[0].time < originalTime);
    const double movedSeconds = std::chrono::duration<double>(
        originalTime - plot.model().annotations[0].time).count();
    QVERIFY(movedSeconds > 3.0 && movedSeconds < 5.0);
    QTest::mouseRelease(&plot, Qt::MiddleButton, Qt::NoModifier, QPoint(585, 204));
  }
  void plotKeepsCrossingSegmentsAndDoesNotRewindOnLateSamples() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "test:pv";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0;
    model.curves[0].maximum = 10;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    const auto base = std::chrono::system_clock::from_time_t(1000);
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    plot.setCurveSamples(0, {{base - std::chrono::seconds(5), 0, 0, 0},
                             {base + std::chrono::seconds(15), 10, 0, 0}});
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(10)});
    plot.resetVerticalView();
    QImage image(plot.size(), QImage::Format_ARGB32_Premultiplied);
    plot.render(&image);
    bool traceAtCenter = false;
    int bottom = plot.height() - 1;
    while (bottom > 0 && !plot.isInPlot(QPoint(plot.width() / 2, bottom))) --bottom;
    for (int y = bottom - 3; y <= bottom + 3; ++y)
      for (int x = 300; x <= 410; ++x) {
        const QColor pixel = image.pixelColor(x, y);
        if (pixel.blue() > 150 && pixel.red() < 100) traceAtCenter = true;
      }
    QVERIFY(traceAtCenter);
    plot.resetView();
    const auto future = std::chrono::system_clock::now() + std::chrono::seconds(10);
    plot.appendSample(0, {future, 5, 0, 0});
    const auto end = plot.visibleTimeRange().end;
    plot.appendSample(0, {future - std::chrono::seconds(5), 6, 0, 0});
    QCOMPARE(plot.visibleTimeRange().end, end);
  }
  void toolbarRightClickUsesFinePanAndZoomSteps() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "linear:test";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0;
    model.curves[0].maximum = 100;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    model.curves[1].name = "log:test";
    model.curves[1].nameSet = true;
    model.curves[1].scale = striptool::ScaleMode::Log10;
    model.curves[1].minimum = 1;
    model.curves[1].maximum = 1000;
    model.curves[1].minimumSet = true;
    model.curves[1].maximumSet = true;
    striptool::MainWindow window(model);
    window.show();
    auto* plot = window.plotWidget();
    const auto base = std::chrono::system_clock::from_time_t(1000);
    plot->setVisibleTimeRange({base, base + std::chrono::seconds(100)});
    auto* panLeft = window.findChild<QToolButton*>(QStringLiteral("panLeftButton"));
    auto* panUp = window.findChild<QToolButton*>(QStringLiteral("panUpButton"));
    auto* panDown = window.findChild<QToolButton*>(QStringLiteral("panDownButton"));
    auto* zoomIn = window.findChild<QToolButton*>(QStringLiteral("zoomInButton"));
    auto* zoomInY = window.findChild<QToolButton*>(QStringLiteral("zoomInYButton"));
    auto* zoomOutY = window.findChild<QToolButton*>(QStringLiteral("zoomOutYButton"));
    QVERIFY(panLeft);
    QVERIFY(panUp);
    QVERIFY(panDown);
    QVERIFY(zoomIn);
    QVERIFY(zoomInY);
    QVERIFY(zoomOutY);
    const auto beforeCancelledStep = plot->visibleTimeRange();
    QTest::mousePress(panLeft, Qt::RightButton, Qt::NoModifier, panLeft->rect().center());
    moveWhileDragging(panLeft, QPoint(-12, panLeft->height() / 2), Qt::RightButton);
    QTest::mouseRelease(panLeft, Qt::RightButton, Qt::NoModifier,
                        QPoint(-12, panLeft->height() / 2));
    QCOMPARE(plot->visibleTimeRange().start, beforeCancelledStep.start);
    QTest::mouseClick(panLeft, Qt::RightButton);
    const double finePan = std::chrono::duration<double>(
        base - plot->visibleTimeRange().start).count();
    QVERIFY(finePan > 4.9 && finePan < 5.1);
    QTest::mouseClick(panLeft, Qt::LeftButton);
    const double coarsePan = std::chrono::duration<double>(
        base - plot->visibleTimeRange().start).count();
    QVERIFY(coarsePan > 54.9 && coarsePan < 55.1);
    QTest::mouseClick(zoomIn, Qt::RightButton);
    const double fineZoom = std::chrono::duration<double>(
        plot->visibleTimeRange().end - plot->visibleTimeRange().start).count();
    QVERIFY(fineZoom > 93.2 && fineZoom < 93.4);
    QTest::mouseClick(zoomIn, Qt::LeftButton);
    const double coarseZoom = std::chrono::duration<double>(
        plot->visibleTimeRange().end - plot->visibleTimeRange().start).count();
    QVERIFY(coarseZoom > 46.6 && coarseZoom < 46.7);
    QTest::mouseClick(panUp, Qt::RightButton);
    QVERIFY(std::abs(plot->valueRange(0).minimum - 5.0) < 1e-9);
    QVERIFY(std::abs(plot->valueRange(1).minimum - 0.15) < 1e-9);
    QTest::mouseClick(panUp, Qt::LeftButton);
    QVERIFY(std::abs(plot->valueRange(0).minimum - 55.0) < 1e-9);
    QTest::mouseClick(panDown, Qt::RightButton);
    QVERIFY(std::abs(plot->valueRange(0).minimum - 50.0) < 1e-9);
    QTest::mouseClick(zoomInY, Qt::RightButton);
    const double fineYWidth = plot->valueRange(0).maximum -
                              plot->valueRange(0).minimum;
    QVERIFY(fineYWidth > 93.2 && fineYWidth < 93.4);
    QTest::mouseClick(zoomInY, Qt::LeftButton);
    const double coarseYWidth = plot->valueRange(0).maximum -
                                plot->valueRange(0).minimum;
    QVERIFY(coarseYWidth > 46.6 && coarseYWidth < 46.7);
    QTest::mouseClick(zoomOutY, Qt::RightButton);
    QVERIFY(std::abs(plot->valueRange(0).maximum -
                     plot->valueRange(0).minimum -
                     coarseYWidth * 1.071773462536293) < 1e-9);
    plot->setCurveSamples(0, {{base, 50.0, 0, 0}});
    QVERIFY(std::abs(plot->valueRange(0).maximum -
                     plot->valueRange(0).minimum -
                     coarseYWidth * 1.071773462536293) < 1e-9);
    window.findChild<QAction*>(QStringLiteral("resetAction"))->trigger();
    QCOMPARE(plot->valueRange(0).minimum, 0.0);
    QCOMPARE(plot->valueRange(0).maximum, 100.0);
    QCOMPARE(plot->valueRange(1).minimum, 0.0);
    QCOMPARE(plot->valueRange(1).maximum, 3.0);
    plot->setVisibleTimeRange({base - std::chrono::seconds(1),
                               base + std::chrono::seconds(1)});
    plot->autoScale();
    QVERIFY(plot->valueRange(0).maximum < 60.0);
    window.findChild<QAction*>(QStringLiteral("resetAction"))->trigger();
    QCOMPARE(plot->valueRange(0).minimum, 0.0);
    QCOMPARE(plot->valueRange(0).maximum, 100.0);
  }
  void plotWidgetRetainsHistoryAcrossLiveRefreshAndModelEdits() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "history:test";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);
    const auto base = std::chrono::system_clock::from_time_t(1000);
    plot.setCurveSamples(0, {{base + std::chrono::seconds(10), 10.0, 0, 0}});
    plot.joinHistoricalSamples(0, {{base, 1.0, 0, 0}});
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(20)});
    const auto range = plot.visibleTimeRange();
    model.graph.lineWidth = 3;
    plot.setModel(model);
    QCOMPARE(plot.visibleTimeRange().start, range.start);
    QVERIFY(!plot.autoScroll());
    plot.setCurveSamples(0, {{base + std::chrono::seconds(20), 20.0, 0, 0}});
    QCOMPARE(plot.curveSamples(0).size(), std::size_t{2});
    QCOMPARE(plot.curveSamples(0).front().value, 1.0);
    plot.clearCurveSamples(0);
    QVERIFY(plot.curveSamples(0).empty());
  }
  void autoScaleTracksVisibleTrace() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "scale:test";
    model.curves[0].nameSet = true;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);
    const auto base = std::chrono::system_clock::from_time_t(1000);
    plot.setCurveSamples(0, {{base, 5.0, 0, 0},
                             {base + std::chrono::seconds(10), 1000.0, 0, 0}});
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(5)});
    plot.autoScale();
    QCOMPARE(plot.valueRange(0).minimum, 4.75);
    QCOMPARE(plot.valueRange(0).maximum, 5.25);
    plot.setCurveSamples(0, {{base, 5.0, 0, 0},
                             {base + std::chrono::seconds(10), 2000.0, 0, 0}});
    QCOMPARE(plot.valueRange(0).maximum, 5.25);
  }
  void automaticMetadataDoesNotCancelRequestedAutoScale() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "scale:metadata";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0.0;
    model.curves[0].minimumSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);
    const auto base = std::chrono::system_clock::from_time_t(1000);
    plot.setCurveSamples(0, {{base, 5.0, 0, 0},
                             {base + std::chrono::seconds(10), 10.0, 0, 0}});
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(10)});
    plot.autoScale();
    QVERIFY(plot.valueRange(0).minimum > 0.0);
    model.curves[0].maximum = 100.0;
    plot.setModel(model);
    QVERIFY(plot.valueRange(0).minimum > 0.0);
    QVERIFY(plot.valueRange(0).maximum < 100.0);
  }
  void globalAutoScaleIgnoresIndividualConfiguredBounds() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "visible:test";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);
    const auto base = std::chrono::system_clock::from_time_t(1000);
    plot.setCurveSamples(0, {{base, 5.0, 0, 0},
                             {base + std::chrono::seconds(10), 1000.0, 0, 0}});
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(5)});
    QCOMPARE(plot.valueRange(0).maximum, 5.25);
    model.curves[0].minimum = 4.0;
    model.curves[0].maximum = 6.0;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    plot.setModel(model);
    QCOMPARE(plot.valueRange(0).minimum, 4.75);
    QCOMPARE(plot.valueRange(0).maximum, 5.25);
    plot.resetVerticalView();
    QCOMPARE(plot.valueRange(0).minimum, 4.0);
    QCOMPARE(plot.valueRange(0).maximum, 6.0);
  }
  void liveScrollAdvancesWhenSamplesStop() {
    auto model = striptool::makeDefaultModel();
    model.timing.timespanSeconds = 10;
    model.curves[0].name = "stopped:test";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);
    const auto old = std::chrono::system_clock::now() - std::chrono::seconds(30);
    plot.setCurveSamples(0, {{old, 1.0, 0, 0}});
    QVERIFY(plot.visibleTimeRange().end > old + std::chrono::seconds(20));
    const auto beforeRefresh = plot.visibleTimeRange().end;
    QTest::qWait(20);
    plot.advanceToNow();
    QVERIFY(plot.visibleTimeRange().end > beforeRefresh);
    const auto liveRange = plot.visibleTimeRange();
    plot.setPaused(true);
    plot.advanceToNow();
    QCOMPARE(plot.visibleTimeRange().end, liveRange.end);
  }
  void liveRefreshDoesNotRewindTheTimeWindow() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "delayed:test";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);
    const auto old = std::chrono::system_clock::now() - std::chrono::seconds(30);
    plot.setCurveSamples(0, {{old, 1.0, 0, 0}});
    plot.advanceToNow();
    const auto liveEnd = plot.visibleTimeRange().end;
    plot.setCurveSamples(0, {{old, 1.0, 0, 0}});
    QCOMPARE(plot.visibleTimeRange().end, liveEnd);
    const auto future = liveEnd + std::chrono::seconds(10);
    plot.setCurveSamples(0, {{old, 1.0, 0, 0}, {future, 2.0, 0, 0}});
    QVERIFY(plot.visibleTimeRange().end >= liveEnd);
    QVERIFY(plot.visibleTimeRange().end < future);
    plot.advanceToNow();
    QVERIFY(plot.visibleTimeRange().end < future);
  }
  void appendedSampleUpdatesLiveAutomaticRangeImmediately() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "append:test";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.setModel(model);
    const auto now = std::chrono::system_clock::now();
    plot.setCurveSamples(0, {{now, 1.0, 0, 0}});
    const auto future = plot.visibleTimeRange().end + std::chrono::seconds(10);
    plot.appendSample(0, {future, 100.0, 0, 0});
    QVERIFY(plot.visibleTimeRange().end < future);
    QVERIFY(plot.valueRange(0).maximum < 100.0);
  }
  void panDraggedBackToStartRestoresOriginalRange() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "pan:test";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    const auto base = std::chrono::system_clock::from_time_t(1000);
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(100)});
    plot.show();
    const auto original = plot.visibleTimeRange();
    const QPoint start(400, 300);
    QTest::mousePress(&plot, Qt::LeftButton, Qt::NoModifier, start);
    moveWhileDragging(&plot, start + QPoint(60, 0), Qt::LeftButton);
    QVERIFY(plot.visibleTimeRange().start < original.start);
    moveWhileDragging(&plot, start, Qt::LeftButton);
    QCOMPARE(plot.visibleTimeRange().start, original.start);
    QCOMPARE(plot.visibleTimeRange().end, original.end);
    QTest::mouseRelease(&plot, Qt::LeftButton, Qt::NoModifier, start);
    QCOMPARE(plot.visibleTimeRange().start, original.start);
    QCOMPARE(plot.visibleTimeRange().end, original.end);
  }
  void stationaryPanKeepsAutoScroll() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "pan:test";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    plot.show();
    const QPoint panPoint(500, 300);
    QTest::mousePress(&plot, Qt::LeftButton, Qt::NoModifier, panPoint);
    moveWhileDragging(&plot, panPoint, Qt::LeftButton);
    QVERIFY(plot.autoScroll());
    QTest::mouseRelease(&plot, Qt::LeftButton, Qt::NoModifier, panPoint);
  }
  void livePanDragUsesStableRangeAndStationaryReleaseCatchesUp() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "pan:test";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    plot.show();
    const auto now = std::chrono::system_clock::now();
    plot.setCurveSamples(0, {{now, 1.0, 0, 0}});
    const QPoint start(400, 300);
    const auto original = plot.visibleTimeRange();
    QTest::mousePress(&plot, Qt::LeftButton, Qt::NoModifier, start);
    const auto future = original.end + std::chrono::seconds(20);
    plot.setCurveSamples(0, {{now, 1.0, 0, 0}, {future, 2.0, 0, 0}});
    plot.advanceToNow();
    QCOMPARE(plot.visibleTimeRange().end, original.end);
    moveWhileDragging(&plot, start + QPoint(40, 0), Qt::LeftButton);
    QVERIFY(plot.visibleTimeRange().start < original.start);
    QVERIFY(!plot.autoScroll());
    QTest::mouseRelease(&plot, Qt::LeftButton, Qt::NoModifier,
                        start + QPoint(40, 0));
    QVERIFY(plot.visibleTimeRange().end < future);

    plot.resetView();
    const auto beforeClick = plot.visibleTimeRange();
    QTest::mousePress(&plot, Qt::LeftButton, Qt::NoModifier, start);
    const auto later = future + std::chrono::seconds(20);
    plot.appendSample(0, {later, 3.0, 0, 0});
    QCOMPARE(plot.visibleTimeRange().end, beforeClick.end);
    QTest::mouseRelease(&plot, Qt::LeftButton, Qt::NoModifier, start);
    QVERIFY(plot.visibleTimeRange().end > beforeClick.end);
    QVERIFY(plot.visibleTimeRange().end < later);
    QVERIFY(plot.autoScroll());
  }
  void dragReleaseAppliesItsFinalPosition() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "drag:test";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0.0;
    model.curves[0].maximum = 10.0;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    const auto base = std::chrono::system_clock::from_time_t(1000);
    plot.setVisibleTimeRange({base, base + std::chrono::seconds(100)});
    plot.show();

    QTest::mousePress(&plot, Qt::LeftButton, Qt::NoModifier, QPoint(300, 300));
    QTest::mouseRelease(&plot, Qt::LeftButton, Qt::NoModifier, QPoint(350, 300));
    QVERIFY(plot.visibleTimeRange().start < base - std::chrono::seconds(8));
    QVERIFY(!plot.autoScroll());

    const QPoint anchor(400, 200);
    QVERIFY(plot.addAnnotationAt(anchor, QStringLiteral("note")) >= 0);
    const auto originalTime = plot.model().annotations[0].time;
    const double originalValue = *plot.model().annotations[0].value;
    QTest::mousePress(&plot, Qt::MiddleButton, Qt::NoModifier,
                      anchor + QPoint(4, 4));
    QTest::mouseRelease(&plot, Qt::MiddleButton, Qt::NoModifier,
                        anchor + QPoint(44, 24));
    QVERIFY(plot.model().annotations[0].time > originalTime);
    QVERIFY(*plot.model().annotations[0].value < originalValue);
  }
  void lostMouseReleaseCannotContinueDragging() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "drag:test";
    model.curves[0].nameSet = true;
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    plot.show();
    const QPoint start(400, 230);
    QVERIFY(plot.addAnnotationAt(start, QStringLiteral("note")) >= 0);
    const auto annotationTime = plot.model().annotations[0].time;
    QTest::mousePress(&plot, Qt::MiddleButton, Qt::NoModifier, start + QPoint(4, 4));
    moveWhileDragging(&plot, start + QPoint(44, 4), Qt::NoButton);
    QCOMPARE(plot.model().annotations[0].time, annotationTime);
    moveWhileDragging(&plot, start + QPoint(64, 4), Qt::NoButton);
    QCOMPARE(plot.model().annotations[0].time, annotationTime);

    const auto range = plot.visibleTimeRange();
    QTest::mousePress(&plot, Qt::LeftButton, Qt::NoModifier, QPoint(500, 350));
    moveWhileDragging(&plot, QPoint(540, 350), Qt::NoButton);
    QVERIFY(std::abs(std::chrono::duration<double>(
        plot.visibleTimeRange().start - range.start).count()) < 1.0);
    QVERIFY(plot.autoScroll());
  }
  void lightAnnotationTextRemainsReadable() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "light:test";
    model.curves[0].nameSet = true;
    model.curves[0].minimum = 0;
    model.curves[0].maximum = 100;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    model.colors.curves[0] = {65535, 65535, 0, 65535};
    model.graph.xGrid = striptool::GridMode::None;
    model.graph.yGrid = striptool::GridMode::None;
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    plot.show();
    QVERIFY(plot.addAnnotationAt(QPoint(300, 190), QStringLiteral("Note")) >= 0);
    plot.selectAnnotation(-1);
    QImage image(plot.size(), QImage::Format_ARGB32_Premultiplied);
    plot.render(&image);
    int darkTextPixels = 0;
    for (int y = 194; y < 215; ++y)
      for (int x = 306; x < 345; ++x) {
        const QColor pixel = image.pixelColor(x, y);
        if (pixel.red() < 120 && pixel.green() < 120 && pixel.blue() < 120)
          ++darkTextPixels;
      }
    QVERIFY(darkTextPixels > 5);
  }
  void plotWidgetRendersFixtureOffscreen() {
    auto model = striptool::makeDefaultModel();
    QVERIFY(striptool::readConfigurationFile(fixture("visual-baseline-1.2.stp"), model).success);
    striptool::PlotWidget plot;
    plot.resize(800, 500);
    plot.setModel(model);
    const auto end = std::chrono::system_clock::now();
    std::vector<striptool::Sample> samples;
    for (int i = 0; i <= 300; ++i)
      samples.push_back({end - std::chrono::seconds(300 - i),
                         50.0 + 35.0 * std::sin(i / 20.0), 0, 0});
    plot.setCurveSamples(0, std::move(samples));
    plot.setVisibleTimeRange({end - std::chrono::seconds(300), end});
    QImage image(plot.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    plot.render(&image);
    QVERIFY(!image.isNull());
    int nonBackground = 0;
    const QColor background = image.pixelColor(5, 5);
    for (int y = 0; y < image.height(); y += 5)
      for (int x = 0; x < image.width(); x += 5)
        if (image.pixelColor(x, y) != background) ++nonBackground;
    QVERIFY(nonBackground > 100);
    const auto tracePixels = [](const QImage& frame) {
      int count = 0;
      for (int y = 70; y < 430; ++y)
        for (int x = 100; x < 700; ++x) {
          const QColor pixel = frame.pixelColor(x, y);
          if (pixel.blue() > 150 && pixel.red() < 100 && pixel.green() < 100)
            ++count;
        }
      return count;
    };
    QVERIFY(tracePixels(image) > 100);
    const QString output = qEnvironmentVariable("QTSTRIPTOOL_VISUAL_OUTPUT");
    if (!output.isEmpty()) {
      QVERIFY(image.save(output));
      QVERIFY(plot.addAnnotationAt(QPoint(300, 190), QStringLiteral("Operator note")) >= 0);
      plot.render(&image);
      QVERIFY(tracePixels(image) > 100);
      const auto directory = std::filesystem::path(output.toStdString()).parent_path();
      QVERIFY(image.save(QString::fromStdString((directory / "annotation.png").string())));
      auto manyCurves = striptool::makeDefaultModel();
      manyCurves.timing.timespanSeconds = 300;
      for (std::size_t curve = 0; curve < striptool::kMaximumCurves; ++curve) {
        auto& config = manyCurves.curves[curve];
        config.name = "test:curve:" + std::to_string(curve + 1);
        config.nameSet = true;
        config.units = "percent";
        config.minimum = 0.0;
        config.maximum = 100.0;
        config.minimumSet = true;
        config.maximumSet = true;
      }
      striptool::PlotWidget tenCurvePlot;
      tenCurvePlot.resize(1200, 650);
      tenCurvePlot.setModel(manyCurves);
      for (std::size_t curve = 0; curve < striptool::kMaximumCurves; ++curve) {
        std::vector<striptool::Sample> curveSamples;
        for (int i = 0; i <= 300; i += 10)
          curveSamples.push_back({end - std::chrono::seconds(300 - i),
                                  50.0 + 35.0 * std::sin(i / 20.0 + curve), 0, 0});
        tenCurvePlot.setCurveSamples(curve, std::move(curveSamples));
      }
      tenCurvePlot.setVisibleTimeRange({end - std::chrono::seconds(300), end});
      QImage tenCurveImage(tenCurvePlot.size(), QImage::Format_ARGB32_Premultiplied);
      tenCurveImage.fill(Qt::transparent);
      tenCurvePlot.render(&tenCurveImage);
      QVERIFY(tenCurveImage.save(QString::fromStdString((directory / "ten-curves.png").string())));
    }
  }
  void mainAndControlsRenderOffscreen() {
    auto model = striptool::makeDefaultModel();
    model.curves[0].name = "CPU_Usage";
    model.curves[0].nameSet = true;
    model.curves[0].units = "percent";
    model.curves[0].minimum = 0.0;
    model.curves[0].maximum = 100.0;
    model.curves[0].minimumSet = true;
    model.curves[0].maximumSet = true;
    striptool::MainWindow window(model);
    window.resize(900, 620);
    const auto end = std::chrono::system_clock::now();
    window.plotWidget()->setCurveSamples(0,
        {{end - std::chrono::seconds(120), 40.0, 0, 0},
         {end - std::chrono::seconds(60), 70.0, 0, 0},
         {end, 50.0, 0, 0}});
    window.show();
    window.showControls();
    striptool::ChannelMetadata metadata;
    metadata.connection = striptool::ConnectionState::Connected;
    window.controlsWindow()->setChannelMetadata(0, metadata);
    QApplication::processEvents();
    const QString output = qEnvironmentVariable("QTSTRIPTOOL_VISUAL_OUTPUT");
    if (!output.isEmpty()) {
      const auto directory = std::filesystem::path(output.toStdString()).parent_path();
      QVERIFY(window.grab().save(QString::fromStdString((directory / "main-window.png").string())));
      QVERIFY(window.controlsWindow()->grab().save(
          QString::fromStdString((directory / "controls-window.png").string())));
    }
  }
  void performanceTenCurveBufferAndLargeRender() {
    auto model = striptool::makeDefaultModel();
    model.timing.numberOfSamples = 10000;
    model.timing.timespanSeconds = 10000;
    for (std::size_t curve = 0; curve < striptool::kMaximumCurves; ++curve) {
      model.curves[curve].name = "performance:" + std::to_string(curve);
      model.curves[curve].nameSet = true;
      model.curves[curve].minimum = -1.0;
      model.curves[curve].maximum = 1.0;
      model.curves[curve].minimumSet = true;
      model.curves[curve].maximumSet = true;
    }
    striptool::PlotWidget plot;
    plot.resize(1200, 700);
    plot.setModel(model);
    const auto start = std::chrono::system_clock::now();
    QElapsedTimer timer;
    timer.start();
    for (std::size_t curve = 0; curve < striptool::kMaximumCurves; ++curve) {
      std::vector<striptool::Sample> samples;
      samples.reserve(10000);
      for (int i = 0; i < 10000; ++i)
        samples.push_back({start + std::chrono::seconds(i),
                           std::sin(i * 0.01 + curve), 0, 0});
      plot.setCurveSamples(curve, std::move(samples));
      QCOMPARE(plot.curveSamples(curve).size(), std::size_t{10000});
    }
    QImage image(plot.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    plot.render(&image);
    QVERIFY2(timer.elapsed() < 10000,
             "Ten-curve buffering and large-range render exceeded 10 seconds");
  }
  void performanceWindowStartupAndShutdown() {
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 25; ++i) {
      striptool::MainWindow window;
      window.show();
      QApplication::processEvents();
    }
    QVERIFY2(timer.elapsed() < 10000,
             "Twenty-five window startup/shutdown cycles exceeded 10 seconds");
  }
};
int main(int argc, char** argv) {
  striptool::setDefaultApplicationStyle();
  QApplication application(argc, argv);
  striptool::configureApplication(application);
  SmokeTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "test_smoke.moc"
