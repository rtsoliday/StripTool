#include "core/application.h"
#include "services/channel_access.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace {

bool observedState(const QSignalSpy& events, striptool::ConnectionState expected) {
  for (const auto& event : events) {
    if (qvariant_cast<striptool::ConnectionState>(event.at(1)) == expected)
      return true;
  }
  return false;
}

void stopIoc(QProcess& ioc) {
  if (ioc.state() == QProcess::NotRunning) return;
  ioc.terminate();
  if (!ioc.waitForFinished(5000)) {
    ioc.kill();
    ioc.waitForFinished(5000);
  }
}

class IocGuard {
public:
  explicit IocGuard(QProcess& process) : process_(process) {}
  ~IocGuard() { stopIoc(process_); }

private:
  QProcess& process_;
};

}  // namespace

class ChannelAccessIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void automaticallyReconnectsAfterIocRestart() {
    const QString softIoc = QStandardPaths::findExecutable(QStringLiteral("softIoc"));
    const QString database = qEnvironmentVariable("QTSTRIPTOOL_TEST_IOC_DB");
    if (softIoc.isEmpty()) QSKIP("softIoc is not available on PATH");
    if (database.isEmpty()) QSKIP("QTSTRIPTOOL_TEST_IOC_DB is not set");

    const int port = 15000 + int(QCoreApplication::applicationPid() % 10000);
    qputenv("EPICS_CA_AUTO_ADDR_LIST", "NO");
    qputenv("EPICS_CA_SERVER_PORT", QByteArray::number(port));
    qputenv("EPICS_CA_ADDR_LIST", "127.0.0.1:" + QByteArray::number(port));

    QProcess ioc;
    IocGuard iocGuard(ioc);
    ioc.setProgram(softIoc);
    ioc.setArguments({QStringLiteral("-S"), QStringLiteral("-d"), database});
    ioc.start();
    QVERIFY2(ioc.waitForStarted(5000), qPrintable(ioc.errorString()));

    striptool::ChannelAccessProvider provider;
    QSignalSpy connections(&provider, &striptool::ChannelProvider::connectionChanged);
    QSignalSpy samples(&provider, &striptool::ChannelProvider::sampleReceived);
    provider.connectChannel(QStringLiteral("QTST:RECONNECT"));
    QTRY_VERIFY_WITH_TIMEOUT(
        observedState(connections, striptool::ConnectionState::Connected), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(samples.count() > 0, 5000);

    connections.clear();
    samples.clear();
    stopIoc(ioc);
    QTRY_VERIFY_WITH_TIMEOUT(
        observedState(connections, striptool::ConnectionState::Disconnected), 15000);

    connections.clear();
    ioc.start();
    QVERIFY2(ioc.waitForStarted(5000), qPrintable(ioc.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(
        observedState(connections, striptool::ConnectionState::Connected), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(samples.count() > 0, 5000);
    stopIoc(ioc);
  }

  void receivesMetadataAndSampleFromIoc() {
    const QString pv = qEnvironmentVariable("QTSTRIPTOOL_TEST_PV");
    if (pv.isEmpty()) QSKIP("Set QTSTRIPTOOL_TEST_PV to a readable numeric PV");

    striptool::ChannelAccessProvider provider;
    QSignalSpy connections(&provider, &striptool::ChannelProvider::connectionChanged);
    QSignalSpy metadata(&provider, &striptool::ChannelProvider::metadataChanged);
    QSignalSpy samples(&provider, &striptool::ChannelProvider::sampleReceived);
    QSignalSpy descriptions(&provider, &striptool::ChannelProvider::descriptionReceived);
    const auto id = provider.connectChannel(pv);
    QTRY_VERIFY_WITH_TIMEOUT(connections.count() >= 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(metadata.count() >= 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(samples.count() >= 1, 10000);
    QCOMPARE(qvariant_cast<striptool::ChannelId>(samples.last().at(0)), id);
    const auto sample = qvariant_cast<striptool::Sample>(samples.last().at(1));
    const auto now = std::chrono::system_clock::now();
    QVERIFY(sample.timestamp <= now);
    QVERIFY(sample.timestamp >= now - std::chrono::seconds(10));
    const QString expectedDescription = qEnvironmentVariable("QTSTRIPTOOL_TEST_DESC");
    if (!expectedDescription.isEmpty()) {
      QTRY_VERIFY_WITH_TIMEOUT(descriptions.count() >= 1, 10000);
      QCOMPARE(descriptions.last().at(1).toString(), expectedDescription);
    }
    provider.disconnectChannel(id);
    if (!expectedDescription.isEmpty()) {
      auto model = striptool::makeDefaultModel();
      model.curves[0].name = pv.toStdString();
      model.curves[0].nameSet = true;
      striptool::MainWindow window(model);
      window.startAcquisition();
      QTRY_COMPARE_WITH_TIMEOUT(
          QString::fromStdString(window.model().curves[0].comment),
          expectedDescription, 10000);
    }
  }
  void shutsDownWithAnActiveSubscription() {
    const QString pv = qEnvironmentVariable("QTSTRIPTOOL_TEST_PV");
    if (pv.isEmpty()) QSKIP("Set QTSTRIPTOOL_TEST_PV to a readable numeric PV");
    {
      striptool::ChannelAccessProvider provider;
      QSignalSpy samples(&provider, &striptool::ChannelProvider::sampleReceived);
      provider.connectChannel(pv);
      QTRY_VERIFY_WITH_TIMEOUT(samples.count() >= 1, 10000);
    }
    QVERIFY(true);
  }
};

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  striptool::configureApplication(application);
  qRegisterMetaType<striptool::ChannelId>("striptool::ChannelId");
  qRegisterMetaType<striptool::ChannelMetadata>();
  qRegisterMetaType<striptool::Sample>();
  ChannelAccessIntegrationTests tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "test_channel_access.moc"
