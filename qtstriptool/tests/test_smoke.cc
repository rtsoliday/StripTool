#include "core/application.h"
#include "core/config.h"
#include "core/model.h"
#include "core/sample_buffer.h"
#include "services/acquisition_manager.h"
#include "services/cpu_usage_provider.h"
#include "services/export_service.h"
#include "services/file_workflow.h"
#include "services/history_provider.h"
#include "services/runtime_capabilities.h"
#include "ui/controls_window.h"
#include "ui/history_dialog.h"
#include "ui/main_window.h"
#include "ui/plot_widget.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QDateTimeEdit>
#include <QIcon>
#include <QImage>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QPrintPreviewDialog>
#include <QPrintPreviewWidget>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSettings>
#include <QStyle>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QTest>
#include <QToolButton>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace {
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
  void retryDisconnected() override { ++retryCount; }
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
  int retryCount = 0;
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
    auto* autoScale = window.findChild<QAction*>(QStringLiteral("autoScaleAction"));
    QVERIFY(autoScale->isCheckable());
    QVERIFY(autoScale->isChecked());
    QVERIFY(window.findChild<QAction*>(QStringLiteral("pauseAction"))->isCheckable());
    QVERIFY(window.findChild<QAction*>(QStringLiteral("graphOpenAction"))->isEnabled());
    QVERIFY(window.findChild<QAction*>(QStringLiteral("exportCsvAction"))->isEnabled());
    QVERIFY(window.controlsWindow());
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
    QCOMPARE(model.title, std::string("saved.stp"));
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

    qputenv("QTSTRIPTOOL_ARCHIVER_URL", QByteArrayLiteral("http://example.invalid/archive/"));
    QCOMPARE(striptool::ArchiverHistoryProvider::configuredRetrievalRoot(),
             QStringLiteral("http://example.invalid/archive/"));
    qunsetenv("QTSTRIPTOOL_ARCHIVER_URL");

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
    QVERIFY(std::filesystem::file_size(root / "data.csv") > 0);
    QVERIFY(std::filesystem::file_size(root / "plot.png") > 0);
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
    moveWhileDragging(&plot, QPoint(680, 76), Qt::NoButton);
    QVERIFY(plot.toolTip().contains(QStringLiteral("Complete channel description")));
    QTest::mouseClick(&plot, Qt::LeftButton, Qt::NoModifier, QPoint(680, 76));
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
    QVERIFY(menu->actions().contains(window.findChild<QAction*>(QStringLiteral("retryConnectionsAction"))));
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
    for (int y = 420; y <= 440; ++y)
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
