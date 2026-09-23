#include "core/application.h"
#include "services/channel_access.h"
#include "ui/main_window.h"

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
    QSignalSpy descriptions(&provider, &striptool::ChannelProvider::descriptionReceived);
    const auto id = provider.connectChannel(pv);
    QTRY_VERIFY_WITH_TIMEOUT(connections.count() >= 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(metadata.count() >= 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(samples.count() >= 1, 10000);
    QCOMPARE(qvariant_cast<striptool::ChannelId>(samples.last().at(0)), id);
    const auto sample = qvariant_cast<striptool::Sample>(samples.last().at(1));
    QVERIFY(sample.timestamp.time_since_epoch().count() > 0);
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
