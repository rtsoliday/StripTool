#include "core/application.h"
#include "services/channel_access.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
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

// QTSTRIPTOOL_SOFTIOC names the softIoc to run (make test-ioc passes the EPICS Base one);
// otherwise softIoc is looked up on PATH. Empty when no executable is found.
QString softIocProgram() {
  const QString configured = qEnvironmentVariable("QTSTRIPTOOL_SOFTIOC");
  if (configured.isEmpty()) return QStandardPaths::findExecutable(QStringLiteral("softIoc"));
  const QFileInfo info(configured);
  return info.isFile() && info.isExecutable() ? info.absoluteFilePath() : QString();
}

QString missingSoftIocReason() {
  const QString configured = qEnvironmentVariable("QTSTRIPTOOL_SOFTIOC");
  return configured.isEmpty()
      ? QStringLiteral("softIoc is not available on PATH")
      : QStringLiteral("QTSTRIPTOOL_SOFTIOC is not an executable: %1").arg(configured);
}

// make test-ioc sets QTSTRIPTOOL_REQUIRE_IOC so a missing softIoc fails instead of skipping.
bool iocRequired() { return !qEnvironmentVariableIsEmpty("QTSTRIPTOOL_REQUIRE_IOC"); }

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
    const QString softIoc = softIocProgram();
    const QString database = qEnvironmentVariable("QTSTRIPTOOL_TEST_IOC_DB");
    if (softIoc.isEmpty() && iocRequired()) QFAIL(qPrintable(missingSoftIocReason()));
    if (softIoc.isEmpty()) QSKIP(qPrintable(missingSoftIocReason()));
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
    const auto id = provider.connectChannel(QStringLiteral("QTST:RECONNECT"));
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
    provider.disconnectChannel(id);
    QCOMPARE(provider.retainedStateCount(), std::size_t(0));
  }

  void refreshesMetadataAfterIocRestart() {
    const QString softIoc = softIocProgram();
    if (softIoc.isEmpty() && iocRequired()) QFAIL(qPrintable(missingSoftIocReason()));
    if (softIoc.isEmpty()) QSKIP(qPrintable(missingSoftIocReason()));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto writeDatabase = [&directory](const QString& name, const char* units,
                                            const char* high) {
      const QString path = directory.filePath(name);
      QFile file(path);
      if (!file.open(QIODevice::WriteOnly)) return QString();
      file.write(QStringLiteral("record(ai, \"QTST:METADATA\") {\n"
                                "    field(EGU, \"%1\")\n    field(HOPR, \"%2\")\n"
                                "    field(LOPR, \"0\")\n    field(PINI, \"YES\")\n}\n")
                     .arg(QLatin1String(units), QLatin1String(high)).toUtf8());
      return path;
    };
    const QString before = writeDatabase(QStringLiteral("before.db"), "V", "10");
    const QString after = writeDatabase(QStringLiteral("after.db"), "mV", "10000");
    QVERIFY(!before.isEmpty() && !after.isEmpty());

    const int port = 15000 + int((QCoreApplication::applicationPid() + 1) % 10000);
    qputenv("EPICS_CA_AUTO_ADDR_LIST", "NO");
    qputenv("EPICS_CA_SERVER_PORT", QByteArray::number(port));
    qputenv("EPICS_CA_ADDR_LIST", "127.0.0.1:" + QByteArray::number(port));

    QProcess ioc;
    IocGuard iocGuard(ioc);
    ioc.setProgram(softIoc);
    ioc.setArguments({QStringLiteral("-S"), QStringLiteral("-d"), before});
    ioc.start();
    QVERIFY2(ioc.waitForStarted(5000), qPrintable(ioc.errorString()));

    striptool::ChannelAccessProvider provider;
    QSignalSpy connections(&provider, &striptool::ChannelProvider::connectionChanged);
    QSignalSpy metadata(&provider, &striptool::ChannelProvider::metadataChanged);
    const auto id = provider.connectChannel(QStringLiteral("QTST:METADATA"));
    const auto latestUnits = [&metadata] {
      return metadata.isEmpty() ? std::string()
          : qvariant_cast<striptool::ChannelMetadata>(metadata.last().at(1)).units;
    };
    QTRY_COMPARE_WITH_TIMEOUT(latestUnits(), std::string("V"), 15000);

    stopIoc(ioc);
    QTRY_VERIFY_WITH_TIMEOUT(
        observedState(connections, striptool::ConnectionState::Disconnected), 15000);
    connections.clear();
    ioc.setArguments({QStringLiteral("-S"), QStringLiteral("-d"), after});
    ioc.start();
    QVERIFY2(ioc.waitForStarted(5000), qPrintable(ioc.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(
        observedState(connections, striptool::ConnectionState::Connected), 15000);
    QTRY_COMPARE_WITH_TIMEOUT(latestUnits(), std::string("mV"), 5000);
    QCOMPARE(*qvariant_cast<striptool::ChannelMetadata>(metadata.last().at(1)).displayMaximum,
             10000.0);
    stopIoc(ioc);
    provider.disconnectChannel(id);
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
  QTemporaryDir settingsDirectory;
  if (!settingsDirectory.isValid()) return 1;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory.path());
  qRegisterMetaType<striptool::ChannelId>("striptool::ChannelId");
  qRegisterMetaType<striptool::ChannelMetadata>();
  qRegisterMetaType<striptool::Sample>();
  ChannelAccessIntegrationTests tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "test_channel_access.moc"
