#include "core/application.h"
#include "services/channel_access.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTest>

class ChannelAccessIntegrationTests final : public QObject {
  Q_OBJECT
private slots:
  void receivesMetadataAndSampleFromIoc() {
    const QString pv = qEnvironmentVariable("QTSTRIPTOOL_TEST_PV");
    if (pv.isEmpty()) QSKIP("Set QTSTRIPTOOL_TEST_PV to a readable numeric PV");

    striptool::ChannelAccessProvider provider;
    QSignalSpy connections(&provider, &striptool::ChannelProvider::connectionChanged);
    QSignalSpy metadata(&provider, &striptool::ChannelProvider::metadataChanged);
    QSignalSpy samples(&provider, &striptool::ChannelProvider::sampleReceived);
    const auto id = provider.connectChannel(pv);
    QTRY_VERIFY_WITH_TIMEOUT(connections.count() >= 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(metadata.count() >= 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(samples.count() >= 1, 10000);
    QCOMPARE(qvariant_cast<striptool::ChannelId>(samples.last().at(0)), id);
    const auto sample = qvariant_cast<striptool::Sample>(samples.last().at(1));
    QVERIFY(sample.timestamp.time_since_epoch().count() > 0);
    provider.disconnectChannel(id);
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
