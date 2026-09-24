#include "ui/main_window.h"
#include "core/application.h"
#include "services/export_service.h"
#include "services/file_workflow.h"
#include "services/history_provider.h"
#include "services/acquisition_manager.h"
#include "services/channel_access.h"
#include "services/cpu_usage_provider.h"
#include "ui/controls_window.h"
#include "ui/history_dialog.h"
#include "ui/plot_widget.h"
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QSignalBlocker>
#include <QScreen>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QtPrintSupport/QPrintDialog>
#include <QtPrintSupport/QPrintPreviewDialog>
#include <QtPrintSupport/QPrintPreviewWidget>
#include <QtPrintSupport/QPrinter>
#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <utility>
namespace striptool {
namespace {
class GraphToolButton final : public QToolButton {
public:
  GraphToolButton(QAction* action, std::function<void()> fineStep, QWidget* parent)
      : QToolButton(parent), fineStep_(std::move(fineStep)) {
    setDefaultAction(action);
  }

protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::RightButton) {
      rightPressed_ = true;
      event->accept();
      return;
    }
    QToolButton::mousePressEvent(event);
  }
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::RightButton) {
      if (rightPressed_ && rect().contains(event->pos())) fineStep_();
      rightPressed_ = false;
      event->accept();
      return;
    }
    QToolButton::mouseReleaseEvent(event);
  }
  void contextMenuEvent(QContextMenuEvent* event) override { event->accept(); }

private:
  std::function<void()> fineStep_;
  bool rightPressed_ = false;
};

std::chrono::milliseconds timerInterval(double seconds) {
  return std::chrono::milliseconds(static_cast<int>(std::clamp(
      seconds * 1000.0, 10.0, double(std::numeric_limits<int>::max()))));
}
}
MainWindow::MainWindow(QWidget* parent) : MainWindow(makeDefaultModel(), parent) {}

MainWindow::MainWindow(StripToolModel model, QWidget* parent)
    : QMainWindow(parent), model_(std::move(model)) {
  setObjectName(QStringLiteral("mainWindow"));
  setWindowTitle(model_.title.empty()
                     ? applicationName()
                     : QString::fromStdString(model_.title) +
                           QStringLiteral(" — ") + applicationName());
  resize(900, 620);
  auto* fileMenu = menuBar()->addMenu(tr("&File"));
  fileMenu->setObjectName(QStringLiteral("fileMenu"));
  auto* openAction = fileMenu->addAction(tr("&Open…"));
  openAction->setObjectName(QStringLiteral("graphOpenAction"));
  auto* saveAction = fileMenu->addAction(tr("&Save"));
  saveAction->setObjectName(QStringLiteral("graphSaveAction"));
  auto* saveAsAction = fileMenu->addAction(tr("Save &As…"));
  saveAsAction->setObjectName(QStringLiteral("graphSaveAsAction"));
  recentMenu_ = fileMenu->addMenu(tr("Open &Recent"));
  recentMenu_->setObjectName(QStringLiteral("recentFilesMenu"));
  fileMenu->addSeparator();
  auto* textAction = fileMenu->addAction(tr("Export &Text…"));
  textAction->setObjectName(QStringLiteral("exportTextAction"));
  auto* csvAction = fileMenu->addAction(tr("Export &CSV…"));
  csvAction->setObjectName(QStringLiteral("exportCsvAction"));
  auto* snapshotAction = fileMenu->addAction(tr("Save S&napshot…"));
  snapshotAction->setObjectName(QStringLiteral("snapshotAction"));
  fileMenu->addSeparator();
  auto* printAction = fileMenu->addAction(tr("&Print…"));
  printAction->setObjectName(QStringLiteral("printAction"));
  auto* previewAction = fileMenu->addAction(tr("Print Pre&view…"));
  previewAction->setObjectName(QStringLiteral("printPreviewAction"));
  fileMenu->addSeparator();
  auto* exitAction = fileMenu->addAction(tr("E&xit"));
  exitAction->setObjectName(QStringLiteral("exitAction"));
  connect(exitAction, &QAction::triggered, qApp, &QApplication::closeAllWindows);
  auto* viewMenu = menuBar()->addMenu(tr("&View"));
  viewMenu->setObjectName(QStringLiteral("viewMenu"));
  auto* pauseAction = viewMenu->addAction(tr("&Pause"));
  pauseAction->setObjectName(QStringLiteral("pauseAction"));
  pauseAction->setCheckable(true);
  auto* autoScrollAction = viewMenu->addAction(tr("&Auto Scroll"));
  autoScrollAction->setObjectName(QStringLiteral("autoScrollAction"));
  autoScrollAction->setCheckable(true);
  autoScrollAction->setChecked(true);
  viewMenu->addSeparator();
  auto* panLeftAction = viewMenu->addAction(tr("Pan &Left"));
  panLeftAction->setObjectName(QStringLiteral("panLeftAction"));
  auto* panRightAction = viewMenu->addAction(tr("Pan &Right"));
  panRightAction->setObjectName(QStringLiteral("panRightAction"));
  auto* panUpAction = viewMenu->addAction(tr("Pan &Up"));
  panUpAction->setObjectName(QStringLiteral("panUpAction"));
  auto* panDownAction = viewMenu->addAction(tr("Pan &Down"));
  panDownAction->setObjectName(QStringLiteral("panDownAction"));
  auto* zoomInAction = viewMenu->addAction(tr("Zoom &In"));
  zoomInAction->setObjectName(QStringLiteral("zoomInAction"));
  auto* zoomOutAction = viewMenu->addAction(tr("Zoom &Out"));
  zoomOutAction->setObjectName(QStringLiteral("zoomOutAction"));
  auto* zoomInYAction = viewMenu->addAction(tr("Zoom In &Y"));
  zoomInYAction->setObjectName(QStringLiteral("zoomInYAction"));
  auto* zoomOutYAction = viewMenu->addAction(tr("Zoom Out Y"));
  zoomOutYAction->setObjectName(QStringLiteral("zoomOutYAction"));
  auto* autoScaleAction = viewMenu->addAction(tr("Auto &Scale"));
  autoScaleAction->setObjectName(QStringLiteral("autoScaleAction"));
  autoScaleAction->setCheckable(true);
  autoScaleAction->setChecked(true);
  auto* resetAction = viewMenu->addAction(tr("&Reset View"));
  resetAction->setObjectName(QStringLiteral("resetAction"));
  auto* replotAction = viewMenu->addAction(tr("Re&plot"));
  replotAction->setObjectName(QStringLiteral("replotAction"));
  auto* historyAction = viewMenu->addAction(tr("&Historical Range…"));
  viewMenu->addSeparator();
  auto* clearAction = viewMenu->addAction(tr("&Clear Data"));
  clearAction->setObjectName(QStringLiteral("clearDataAction"));
  historyAction->setObjectName(QStringLiteral("historyAction"));
  auto* windowMenu = menuBar()->addMenu(tr("&Window"));
  windowMenu->setObjectName(QStringLiteral("windowMenu"));
  auto* showControlsAction = windowMenu->addAction(tr("Show &Controls"));
  showControlsAction->setObjectName(QStringLiteral("showControlsAction"));
  auto* helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->setObjectName(QStringLiteral("helpMenu"));
  auto* aboutAction = helpMenu->addAction(tr("&About Qt StripTool"));
  aboutAction->setObjectName(QStringLiteral("aboutAction"));
  auto* helpAction = helpMenu->addAction(tr("&Help"));
  helpAction->setObjectName(QStringLiteral("helpAction"));
  plotWidget_ = new PlotWidget(this);
  plotWidget_->setObjectName(QStringLiteral("plotArea"));
  plotWidget_->setModel(model_);
  setCentralWidget(plotWidget_);
  auto* toolbar = new QToolBar(tr("Graph"), this);
  addToolBar(Qt::BottomToolBarArea, toolbar);
  toolbar->setObjectName(QStringLiteral("graphToolbar"));
  const auto addStepButton = [toolbar](QAction* action, const QString& name,
                                      const QString& label,
                                      std::function<void()> fineStep) {
    auto* button = new GraphToolButton(action, std::move(fineStep), toolbar);
    button->setObjectName(name);
    button->setText(label);
    button->setToolTip(action->text());
    toolbar->addWidget(button);
  };
  addStepButton(panLeftAction, QStringLiteral("panLeftButton"),
                tr("←"),
                [this] { plotWidget_->pan(-0.05); });
  addStepButton(panRightAction, QStringLiteral("panRightButton"),
                tr("→"),
                [this] { plotWidget_->pan(0.05); });
  addStepButton(panUpAction, QStringLiteral("panUpButton"), tr("↑"),
                [this] { plotWidget_->panY(0.05); });
  addStepButton(panDownAction, QStringLiteral("panDownButton"), tr("↓"),
                [this] { plotWidget_->panY(-0.05); });
  toolbar->addSeparator();
  addStepButton(zoomInAction, QStringLiteral("zoomInButton"),
                tr("X+"),
                [this] { plotWidget_->zoom(1.0 / 1.071773462536293); });
  addStepButton(zoomOutAction, QStringLiteral("zoomOutButton"),
                tr("X−"),
                [this] { plotWidget_->zoom(1.071773462536293); });
  addStepButton(zoomInYAction, QStringLiteral("zoomInYButton"), tr("Y+"),
                [this] { plotWidget_->zoomY(1.0 / 1.071773462536293); });
  addStepButton(zoomOutYAction, QStringLiteral("zoomOutYButton"), tr("Y−"),
                [this] { plotWidget_->zoomY(1.071773462536293); });
  toolbar->addSeparator();
  toolbar->addAction(autoScaleAction);
  toolbar->addAction(resetAction);
  toolbar->addAction(autoScrollAction);
  toolbar->addAction(pauseAction);
  controlsWindow_ = std::make_unique<ControlsWindow>(&model_);
  setHistoryProvider(std::make_unique<ArchiverHistoryProvider>());
  updateRecentFiles();
  const auto chooseOpen = [this] {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open StripTool Configuration"), {}, tr("StripTool files (*.stp);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!openConfiguration(path, &error)) QMessageBox::critical(this, tr("Open Failed"), error);
  };
  const auto chooseSave = [this](bool forceName) {
    QString path = forceName ? QString() : QString::fromStdString(model_.filename);
    if (path.isEmpty())
      path = QFileDialog::getSaveFileName(
          this, tr("Save StripTool Configuration"), QStringLiteral("StripTool.stp"),
          tr("StripTool files (*.stp);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!saveConfiguration(path, &error)) QMessageBox::critical(this, tr("Save Failed"), error);
  };
  connect(openAction, &QAction::triggered, this, chooseOpen);
  connect(saveAction, &QAction::triggered, this, [chooseSave] { chooseSave(false); });
  connect(saveAsAction, &QAction::triggered, this, [chooseSave] { chooseSave(true); });
  connect(textAction, &QAction::triggered, this, [this] {
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Text Data"), {},
                                                       tr("Text files (*.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!exportData(path, false, &error)) QMessageBox::critical(this, tr("Export Failed"), error);
  });
  connect(csvAction, &QAction::triggered, this, [this] {
    const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV Data"), {},
                                                       tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!exportData(path, true, &error)) QMessageBox::critical(this, tr("Export Failed"), error);
  });
  connect(snapshotAction, &QAction::triggered, this, [this] {
    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Plot Snapshot"), {},
                                                       tr("PNG images (*.png);;JPEG images (*.jpg)"),
                                                       &selectedFilter);
    if (path.isEmpty()) return;
    const QString format = selectedFilter.contains(QStringLiteral("*.jpg"),
                                                    Qt::CaseInsensitive)
                               ? QStringLiteral("jpg")
                               : QStringLiteral("png");
    QString error;
    if (!saveSnapshot(path, format, &error))
      QMessageBox::critical(this, tr("Snapshot Failed"), error);
  });
  const auto paintPlot = [this](QPrinter* printer) {
    QPainter painter(printer);
    const QRect page = printer->pageLayout().paintRectPixels(printer->resolution());
    const QPixmap plot = plotWidget_->grab();
    const QSize scaled = plot.size().scaled(page.size(), Qt::KeepAspectRatio);
    const QRect target(page.x() + (page.width() - scaled.width()) / 2,
                       page.y(), scaled.width(), scaled.height());
    painter.drawPixmap(target, plot, plot.rect());
  };
  connect(printAction, &QAction::triggered, this, [this, paintPlot] {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(QPageLayout::Landscape);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() == QDialog::Accepted) paintPlot(&printer);
  });
  connect(previewAction, &QAction::triggered, this, [this, paintPlot] {
    // HighResolution makes the preview page thousands of device pixels wide,
    // which Qt then initially displays at only a few percent. ScreenResolution
    // gives the preview sensible page metrics; the real Print path above still
    // uses the printer's full resolution.
    QPrinter printer(QPrinter::ScreenResolution);
    printer.setPageOrientation(QPageLayout::Landscape);
    QPrintPreviewDialog preview(&printer, this);
    const QSize available = screen() ? screen()->availableGeometry().size()
                                     : QSize(1200, 800);
    preview.resize(std::min(1200, std::max(800, available.width() - 120)),
                   std::min(850, std::max(600, available.height() - 120)));
    connect(&preview, &QPrintPreviewDialog::paintRequested, this, paintPlot);
    QTimer::singleShot(0, &preview, [&preview] {
      if (auto* widget = preview.findChild<QPrintPreviewWidget*>())
        widget->fitToWidth();
    });
    preview.exec();
  });
  connect(pauseAction, &QAction::toggled, plotWidget_, &PlotWidget::setPaused);
  connect(autoScrollAction, &QAction::toggled, plotWidget_, &PlotWidget::setAutoScroll);
  connect(plotWidget_, &PlotWidget::autoScrollChanged, autoScrollAction,
          &QAction::setChecked);
  connect(panLeftAction, &QAction::triggered, this,
          [this] { plotWidget_->pan(-0.5); });
  connect(panRightAction, &QAction::triggered, this,
          [this] { plotWidget_->pan(0.5); });
  connect(panUpAction, &QAction::triggered, this,
          [this] { plotWidget_->panY(0.5); });
  connect(panDownAction, &QAction::triggered, this,
          [this] { plotWidget_->panY(-0.5); });
  connect(zoomInAction, &QAction::triggered, this,
          [this] { plotWidget_->zoom(0.5); });
  connect(zoomOutAction, &QAction::triggered, this,
          [this] { plotWidget_->zoom(2.0); });
  connect(zoomInYAction, &QAction::triggered, this,
          [this] { plotWidget_->zoomY(0.5); });
  connect(zoomOutYAction, &QAction::triggered, this,
          [this] { plotWidget_->zoomY(2.0); });
  connect(autoScaleAction, &QAction::toggled, this,
          [this](bool enabled) {
            if (enabled) plotWidget_->autoScale();
            else plotWidget_->resetVerticalView();
          });
  connect(plotWidget_, &PlotWidget::autoScaleChanged, this,
          [autoScaleAction](bool enabled) {
            const QSignalBlocker blocker(autoScaleAction);
            autoScaleAction->setChecked(enabled);
          });
  connect(resetAction, &QAction::triggered, this, [this] {
    plotWidget_->resetVerticalView();
    plotWidget_->resetView();
  });
  connect(replotAction, &QAction::triggered, plotWidget_, &PlotWidget::replot);
  connect(historyAction, &QAction::triggered, this, &MainWindow::requestHistory);
  connect(clearAction, &QAction::triggered, this, [this] {
    plotWidget_->clearSamples();
    if (channelAcquisition_) channelAcquisition_->clearSamples();
    if (cpuAcquisition_) cpuAcquisition_->clearSamples();
  });
  connect(showControlsAction, &QAction::triggered, this, &MainWindow::showControls);
  connect(controlsWindow_.get(), &ControlsWindow::modelChanged, this, [this] {
    const bool timespanChanged = plotWidget_->model().timing.timespanSeconds !=
                                 model_.timing.timespanSeconds;
    for (std::size_t i = 0; i < model_.curves.size(); ++i) {
      const auto& old = plotWidget_->model().curves[i];
      const auto& next = model_.curves[i];
      if (old.nameSet != next.nameSet || old.name != next.name)
        plotWidget_->clearCurveSamples(i);
    }
    plotWidget_->setModel(model_);
    if (timespanChanged && plotWidget_->autoScroll() && !plotWidget_->paused())
      plotWidget_->resetView();
    setWindowTitle(model_.title.empty()
                       ? applicationName()
                       : QString::fromStdString(model_.title) + QStringLiteral(" — ") +
                             applicationName());
  });
  connect(controlsWindow_.get(), &ControlsWindow::acquisitionConfigurationChanged,
          this, &MainWindow::restartAcquisition);
  connect(controlsWindow_.get(), &ControlsWindow::showGraphRequested, this, [this] {
    show();
    raise();
    activateWindow();
  });
  connect(controlsWindow_.get(), &ControlsWindow::openRequested, this, chooseOpen);
  connect(controlsWindow_.get(), &ControlsWindow::saveRequested, this,
          [chooseSave] { chooseSave(false); });
  connect(controlsWindow_.get(), &ControlsWindow::saveAsRequested, this,
          [chooseSave] { chooseSave(true); });
  connect(plotWidget_, &PlotWidget::cursorLocationChanged, this,
          [this](const QDateTime& time, double value, int curve) {
            if (curve >= 0)
              statusBar()->showMessage(
                  tr("%1   %2: %3")
                      .arg(time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                      .arg(QString::fromStdString(model_.curves[static_cast<std::size_t>(curve)].name))
                      .arg(value, 0, 'g', 8));
          });
  connect(plotWidget_, &PlotWidget::annotationsChanged, this, [this] {
    model_.annotations = plotWidget_->model().annotations;
  });
  statusBar()->showMessage(model_.filename.empty()
                               ? tr("Ready")
                               : tr("Loaded %1").arg(QString::fromStdString(model_.filename)));
  connect(aboutAction, &QAction::triggered, this, [this] {
    QMessageBox::about(this, tr("About Qt StripTool"), aboutText());
  });
  connect(helpAction, &QAction::triggered, this, [this] {
    const QString configured = qEnvironmentVariable("STRIP_HELP_PATH");
    if (!configured.isEmpty()) {
      const QUrl supplied(configured);
      const QUrl helpUrl = supplied.isValid() && supplied.scheme().size() > 1
                               ? supplied
                               : QUrl::fromLocalFile(QFileInfo(configured).absoluteFilePath());
      if (QDesktopServices::openUrl(helpUrl)) return;
    }
    QMessageBox::information(this, tr("Qt StripTool Help"),
        tr("Use the Controls window to connect curves and configure timing and appearance. "
           "Use the graph View menu to pan, zoom, pause, and reset the display."));
  });
  plotMenu_ = new QMenu(plotWidget_);
  plotMenu_->setObjectName(QStringLiteral("plotContextMenu"));
  plotMenu_->addAction(showControlsAction);
  plotMenu_->addAction(autoScrollAction);
  plotMenu_->addSeparator();
  auto* newAnnotationAction = plotMenu_->addAction(tr("Annotate Here…"));
  newAnnotationAction->setObjectName(QStringLiteral("newAnnotationAction"));
  auto* editAnnotationAction = plotMenu_->addAction(tr("Edit Selected Annotation…"));
  editAnnotationAction->setObjectName(QStringLiteral("editAnnotationAction"));
  auto* deleteAnnotationAction = plotMenu_->addAction(tr("Delete Selected Annotation"));
  deleteAnnotationAction->setObjectName(QStringLiteral("deleteAnnotationAction"));
  plotMenu_->addSeparator();
  plotMenu_->addAction(printAction);
  plotMenu_->addAction(snapshotAction);
  plotMenu_->addAction(textAction);
  plotMenu_->addAction(csvAction);
  plotMenu_->addSeparator();
  auto* dismissAction = plotMenu_->addAction(tr("Dismiss Graph"));
  dismissAction->setObjectName(QStringLiteral("dismissGraphAction"));
  plotMenu_->addAction(exitAction);
  connect(newAnnotationAction, &QAction::triggered, this, [this] {
    bool accepted = false;
    const QString text = QInputDialog::getText(
        this, tr("Plot Annotation"), tr("Text:"), QLineEdit::Normal, {}, &accepted);
    if (accepted && !text.isEmpty())
      plotWidget_->addAnnotationAt(contextPlotPosition_, text);
  });
  connect(editAnnotationAction, &QAction::triggered,
          plotWidget_, &PlotWidget::editSelectedAnnotation);
  connect(deleteAnnotationAction, &QAction::triggered, this, [this] {
    plotWidget_->removeAnnotation(plotWidget_->selectedAnnotation());
  });
  connect(dismissAction, &QAction::triggered, this, [this] {
    showControls();
    hide();
  });
  connect(plotWidget_, &PlotWidget::plotContextMenuRequested, this,
          [this, newAnnotationAction, editAnnotationAction, deleteAnnotationAction](
              const QPoint& globalPosition, const QPoint& plotPosition) {
            contextPlotPosition_ = plotPosition;
            newAnnotationAction->setEnabled(plotWidget_->isInPlot(plotPosition));
            const bool selected = plotWidget_->selectedAnnotation() >= 0;
            editAnnotationAction->setEnabled(selected);
            deleteAnnotationAction->setEnabled(selected);
            if (!plotMenu_->isVisible()) plotMenu_->popup(globalPosition);
          });
}

MainWindow::~MainWindow() = default;

void MainWindow::setHistoryProvider(std::unique_ptr<HistoryProvider> provider) {
  cancelHistoryRequests();
  historyProvider_ = std::move(provider);
  if (!historyProvider_) historyProvider_ = std::make_unique<NoHistoryProvider>();
  connect(historyProvider_.get(), &HistoryProvider::resultReady, this,
          [this](HistoryRequestId id, const QString&, std::vector<Sample> samples) {
            const bool automatic = automaticHistoryRequests_.remove(id);
            if (!historyRequests_.contains(id)) return;
            plotWidget_->joinHistoricalSamples(historyRequests_.take(id), samples);
            if (!automatic)
              statusBar()->showMessage(tr("Historical samples loaded"), 5000);
          });
  connect(historyProvider_.get(), &HistoryProvider::requestFailed, this,
          [this](HistoryRequestId id, const QString& message) {
            const bool automatic = automaticHistoryRequests_.remove(id);
            if (!historyRequests_.remove(id) || automatic) return;
            QMessageBox::information(this, tr("History Unavailable"), message);
          });
}

void MainWindow::cancelHistoryRequests(std::optional<std::size_t> curve) {
  if (!historyProvider_) {
    historyRequests_.clear();
    automaticHistoryRequests_.clear();
    return;
  }
  const auto requests = historyRequests_;
  for (auto it = requests.cbegin(); it != requests.cend(); ++it) {
    if (curve && it.value() != *curve) continue;
    historyRequests_.remove(it.key());
    automaticHistoryRequests_.remove(it.key());
    historyProvider_->cancel(it.key());
  }
}

void MainWindow::requestRecentHistory(std::size_t curve,
                                      const std::string& channel) {
  if (!historyProvider_ || curve >= kMaximumCurves || channel.empty() ||
      channel == "CPU_Usage") return;
  const auto end = std::chrono::system_clock::now();
  const auto id = historyProvider_->request(
      QString::fromStdString(channel), {end - std::chrono::minutes(5), end});
  historyRequests_.insert(id, curve);
  automaticHistoryRequests_.insert(id);
}

void MainWindow::showControls() {
  controlsWindow_->show();
  controlsWindow_->raise();
  controlsWindow_->activateWindow();
}

void MainWindow::applyModel() {
  cancelHistoryRequests();
  plotWidget_->clearSamples();
  if (channelAcquisition_) channelAcquisition_->clearSamples();
  if (cpuAcquisition_) cpuAcquisition_->clearSamples();
  plotWidget_->setModel(model_);
  plotWidget_->resetView();
  controlsWindow_->reloadFromModel();
  setWindowTitle(model_.title.empty()
                     ? applicationName()
                     : QString::fromStdString(model_.title) + QStringLiteral(" — ") +
                           applicationName());
  restartAcquisition();
}

bool MainWindow::openConfiguration(const QString& path, QString* error) {
  const auto result = FileWorkflow::open(path.toStdString(), model_);
  if (!result.success) {
    if (error)
      *error = result.diagnostics.empty()
                   ? tr("Unable to open %1").arg(path)
                   : QString::fromStdString(result.diagnostics.front().message);
    return false;
  }
  applyModel();
  updateRecentFiles(path);
  statusBar()->showMessage(tr("Loaded %1").arg(path), 5000);
  return true;
}

bool MainWindow::saveConfiguration(const QString& path, QString* error) {
  std::string detail;
  if (!FileWorkflow::save(path.toStdString(), model_, &detail)) {
    if (error) *error = QString::fromStdString(detail);
    return false;
  }
  updateRecentFiles(path);
  plotWidget_->setModel(model_);
  setWindowTitle(QString::fromStdString(model_.title) + QStringLiteral(" — ") +
                 applicationName());
  controlsWindow_->updateTitle();
  statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
  return true;
}

bool MainWindow::exportData(const QString& path, bool csv, QString* error) const {
  CurveSamples samples;
  const auto range = plotWidget_->visibleTimeRange();
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const auto& source = plotWidget_->curveSamples(i);
    const auto first = std::lower_bound(source.begin(), source.end(), range.start,
        [](const Sample& sample, const auto& time) { return sample.timestamp < time; });
    const auto last = std::upper_bound(first, source.end(), range.end,
        [](const auto& time, const Sample& sample) { return time < sample.timestamp; });
    samples[i].assign(first, last);
  }
  std::string detail;
  const bool ok = csv ? ExportService::writeCsvFile(path.toStdString(), model_, samples, &detail)
                      : ExportService::writeTextFile(path.toStdString(), model_, samples, &detail);
  if (!ok && error) *error = QString::fromStdString(detail);
  return ok;
}

bool MainWindow::saveSnapshot(const QString& path, QString* error) const {
  return saveSnapshot(path, QStringLiteral("png"), error);
}

bool MainWindow::saveSnapshot(const QString& path, const QString& defaultFormat,
                              QString* error) const {
  QString outputPath = path;
  if (QFileInfo(outputPath).suffix().isEmpty()) {
    const bool jpeg = defaultFormat.compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0 ||
                      defaultFormat.compare(QStringLiteral("jpeg"), Qt::CaseInsensitive) == 0;
    outputPath += jpeg ? QStringLiteral(".jpg") : QStringLiteral(".png");
  }
  if (plotWidget_->grab().save(outputPath)) return true;
  if (error) *error = tr("Unable to save image %1").arg(outputPath);
  return false;
}

void MainWindow::updateRecentFiles(const QString& path) {
  QSettings settings;
  std::vector<std::string> recent;
  for (const auto& item : settings.value(QStringLiteral("recentFiles")).toStringList())
    recent.push_back(item.toStdString());
  recent = FileWorkflow::addRecent(recent, path.toStdString());
  QStringList stored;
  for (const auto& item : recent) stored.push_back(QString::fromStdString(item));
  if (!path.isEmpty()) settings.setValue(QStringLiteral("recentFiles"), stored);
  recentMenu_->clear();
  recentMenu_->setEnabled(!stored.isEmpty());
  for (const auto& item : stored) {
    auto* action = recentMenu_->addAction(item);
    connect(action, &QAction::triggered, this, [this, item] {
      QString error;
      if (!openConfiguration(item, &error))
        QMessageBox::critical(this, tr("Open Failed"), error);
    });
  }
}

void MainWindow::requestHistory() {
  HistoryDialog dialog(this);
  dialog.setRange(plotWidget_->visibleTimeRange());
  if (dialog.exec() != QDialog::Accepted) return;
  const TimeRange range = dialog.selectedRange();
  if (!range.isValid()) {
    QMessageBox::warning(this, tr("Invalid Range"), tr("The From time must precede the To time."));
    return;
  }
  cancelHistoryRequests();
  plotWidget_->setVisibleTimeRange(range);
  for (std::size_t i = 0; i < model_.curves.size(); ++i) {
    if (!model_.curves[i].nameSet || model_.curves[i].name == "CPU_Usage") continue;
    const auto id = historyProvider_->request(QString::fromStdString(model_.curves[i].name), range);
    historyRequests_.insert(id, i);
  }
  if (!historyRequests_.isEmpty())
    statusBar()->showMessage(tr("Retrieving historical samples…"));
}

void MainWindow::startAcquisition() {
  if (channelAccess_) return;
  acquisitionRunning_ = true;
  channelAccess_ = std::make_unique<ChannelAccessProvider>();
  cpuUsage_ = std::make_unique<CpuUsageProvider>();
  channelAcquisition_ = std::make_unique<AcquisitionManager>(channelAccess_.get());
  cpuAcquisition_ = std::make_unique<AcquisitionManager>(cpuUsage_.get());
  const auto sampleInterval = timerInterval(model_.timing.sampleIntervalSeconds);
  const auto refreshInterval = timerInterval(model_.timing.refreshIntervalSeconds);
  cpuUsage_->setSampleInterval(sampleInterval);
  channelAcquisition_->setRefreshInterval(refreshInterval);
  cpuAcquisition_->setRefreshInterval(refreshInterval);

  const auto updateMetadata = [this](bool local, ChannelId id,
                                      const ChannelMetadata& metadata) {
    for (std::size_t i = 0; i < channelIds_.size(); ++i) {
      if (channelIds_[i] != id || localChannels_[i] != local) continue;
      controlsWindow_->setChannelMetadata(i, metadata);
      if (metadata.connection != ConnectionState::Connected ||
          (metadata.units.empty() && metadata.description.empty() &&
           !metadata.displayMinimum &&
           !metadata.displayMaximum)) break;
      auto& curve = model_.curves[i];
      bool changed = false;
      if (!metadata.description.empty()) {
        curve.commentDiscovered = true;
        if (!curve.commentSet && curve.comment != metadata.description) {
          curve.comment = metadata.description;
          changed = true;
        }
      }
      if (!metadata.units.empty()) {
        curve.unitsDiscovered = true;
        if (!curve.unitsSet && curve.units != metadata.units) {
          curve.units = metadata.units;
          changed = true;
        }
      }
      if (!metadata.units.empty() || metadata.displayMinimum || metadata.displayMaximum) {
        curve.precisionDiscovered = true;
        const int precision = std::clamp(metadata.precision, 0, 20);
        if (!curve.precisionSet && curve.precision != precision) {
          curve.precision = precision;
          changed = true;
        }
      }
      if (metadata.displayMinimum) {
        curve.minimumDiscovered = true;
        if (!curve.minimumSet && curve.minimum != *metadata.displayMinimum) {
          curve.minimum = *metadata.displayMinimum;
          changed = true;
        }
      }
      if (metadata.displayMaximum) {
        curve.maximumDiscovered = true;
        if (!curve.maximumSet && curve.maximum != *metadata.displayMaximum) {
          curve.maximum = *metadata.displayMaximum;
          changed = true;
        }
      }
      if (changed) {
        plotWidget_->setModel(model_);
        controlsWindow_->refreshCurveMetadata(i);
      }
      break;
    }
  };
  connect(channelAcquisition_.get(), &AcquisitionManager::channelMetadataChanged,
          this, [updateMetadata](ChannelId id, const ChannelMetadata& metadata) {
            updateMetadata(false, id, metadata);
          });
  connect(cpuAcquisition_.get(), &AcquisitionManager::channelMetadataChanged,
          this, [updateMetadata](ChannelId id, const ChannelMetadata& metadata) {
            updateMetadata(true, id, metadata);
          });

  for (std::size_t i = 0; i < model_.curves.size(); ++i) {
    if (!model_.curves[i].nameSet) continue;
    const bool local = model_.curves[i].name == "CPU_Usage";
    auto* acquisition = local ? cpuAcquisition_.get() : channelAcquisition_.get();
    channelIds_[i] = acquisition->addChannel(
        QString::fromStdString(model_.curves[i].name),
        static_cast<std::size_t>(model_.timing.numberOfSamples));
    localChannels_[i] = local;
    acquiredNames_[i] = model_.curves[i].name;
    controlsWindow_->setChannelMetadata(i, acquisition->metadata(channelIds_[i]));
    if (!local) requestRecentHistory(i, acquiredNames_[i]);
  }
  const auto refresh = [this] {
    for (std::size_t i = 0; i < channelIds_.size(); ++i) {
      if (!channelIds_[i]) continue;
      const auto* acquisition = localChannels_[i] ? cpuAcquisition_.get()
                                                  : channelAcquisition_.get();
      if (const auto* buffer = acquisition->buffer(channelIds_[i]))
        plotWidget_->setCurveSamples(i, buffer->samples());
    }
    plotWidget_->advanceToNow();
  };
  connect(channelAcquisition_.get(), &AcquisitionManager::displayRefreshRequested,
          this, refresh);
  connect(cpuAcquisition_.get(), &AcquisitionManager::displayRefreshRequested,
          this, refresh);
}

void MainWindow::stopAcquisition() {
  for (std::size_t i = 0; i < channelIds_.size(); ++i)
    if (channelIds_[i]) controlsWindow_->setChannelMetadata(i, {});
  channelAcquisition_.reset();
  cpuAcquisition_.reset();
  channelAccess_.reset();
  cpuUsage_.reset();
  channelIds_.fill(0);
  localChannels_.fill(false);
  acquiredNames_.fill(std::string{});
  acquisitionRunning_ = false;
}

void MainWindow::restartAcquisition() {
  if (!acquisitionRunning_) return;
  const auto sampleInterval = timerInterval(model_.timing.sampleIntervalSeconds);
  const auto refreshInterval = timerInterval(model_.timing.refreshIntervalSeconds);
  cpuUsage_->setSampleInterval(sampleInterval);
  channelAcquisition_->setRefreshInterval(refreshInterval);
  cpuAcquisition_->setRefreshInterval(refreshInterval);
  channelAcquisition_->setBufferCapacity(model_.timing.numberOfSamples);
  cpuAcquisition_->setBufferCapacity(model_.timing.numberOfSamples);
  for (std::size_t i = 0; i < acquiredNames_.size(); ++i) {
    const std::string name = model_.curves[i].nameSet ? model_.curves[i].name : "";
    if (acquiredNames_[i] == name) continue;
    cancelHistoryRequests(i);
    if (channelIds_[i]) {
      auto* oldAcquisition = localChannels_[i] ? cpuAcquisition_.get()
                                                : channelAcquisition_.get();
      oldAcquisition->removeChannel(channelIds_[i]);
      channelIds_[i] = 0;
    }
    plotWidget_->clearCurveSamples(i);
    acquiredNames_[i] = name;
    if (name.empty()) {
      controlsWindow_->setChannelMetadata(i, {});
      continue;
    }
    const bool local = name == "CPU_Usage";
    auto* acquisition = local ? cpuAcquisition_.get() : channelAcquisition_.get();
    channelIds_[i] = acquisition->addChannel(
        QString::fromStdString(name),
        static_cast<std::size_t>(model_.timing.numberOfSamples));
    localChannels_[i] = local;
    controlsWindow_->setChannelMetadata(i, acquisition->metadata(channelIds_[i]));
    if (!local) requestRecentHistory(i, name);
  }
}
}
