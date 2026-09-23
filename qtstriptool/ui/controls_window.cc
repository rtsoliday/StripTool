#include "ui/controls_window.h"

#include "core/application.h"
#include "services/file_workflow.h"
#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace striptool {
namespace {

QColor toQColor(const Rgba16& color) {
  return QColor::fromRgbF(color.red / 65535.0, color.green / 65535.0,
                          color.blue / 65535.0, color.alpha / 65535.0);
}

Rgba16 fromQColor(const QColor& color) {
  return {static_cast<std::uint16_t>(color.redF() * 65535.0 + 0.5),
          static_cast<std::uint16_t>(color.greenF() * 65535.0 + 0.5),
          static_cast<std::uint16_t>(color.blueF() * 65535.0 + 0.5),
          static_cast<std::uint16_t>(color.alphaF() * 65535.0 + 0.5)};
}

QDoubleSpinBox* valueEditor(QWidget* parent) {
  auto* editor = new QDoubleSpinBox(parent);
  editor->setDecimals(8);
  editor->setRange(-1e100, 1e100);
  editor->setKeyboardTracking(false);
  return editor;
}

QLineEdit* limitEditor(QWidget* parent) {
  auto* editor = new QLineEdit(parent);
  auto* validator = new QDoubleValidator(editor);
  validator->setLocale(QLocale::c());
  validator->setNotation(QDoubleValidator::ScientificNotation);
  editor->setValidator(validator);
  return editor;
}

QString limitText(double value) {
  return QString::number(value, 'g', 16);
}

}  // namespace

ControlsWindow::ControlsWindow(StripToolModel* model, QWidget* parent)
    : QMainWindow(parent), model_(model) {
  setObjectName(QStringLiteral("controlsWindow"));
  setWindowTitle(tr("Qt StripTool Controls"));
  resize(1040, 610);

  auto* fileMenu = menuBar()->addMenu(tr("&File"));
  fileMenu->setObjectName(QStringLiteral("controlsFileMenu"));
  auto* open = fileMenu->addAction(tr("&Open…"));
  open->setObjectName(QStringLiteral("openAction"));
  auto* save = fileMenu->addAction(tr("&Save"));
  save->setObjectName(QStringLiteral("saveAction"));
  auto* saveAs = fileMenu->addAction(tr("Save &As…"));
  saveAs->setObjectName(QStringLiteral("saveAsAction"));
  fileMenu->addSeparator();
  auto* defaults = fileMenu->addAction(tr("Restore &Defaults"));
  defaults->setObjectName(QStringLiteral("defaultsAction"));
  auto* dismiss = fileMenu->addAction(tr("&Dismiss"));
  dismiss->setObjectName(QStringLiteral("dismissControlsAction"));

  auto* windowMenu = menuBar()->addMenu(tr("&Window"));
  windowMenu->setObjectName(QStringLiteral("controlsWindowMenu"));
  auto* showGraph = windowMenu->addAction(tr("Show &Graph"));
  showGraph->setObjectName(QStringLiteral("showGraphAction"));

  auto* helpMenu = menuBar()->addMenu(tr("&Help"));
  helpMenu->setObjectName(QStringLiteral("controlsHelpMenu"));
  auto* about = helpMenu->addAction(tr("&About Qt StripTool"));
  about->setObjectName(QStringLiteral("controlsAboutAction"));

  auto* tabs = new QTabWidget(this);
  tabs->setObjectName(QStringLiteral("controlsTabs"));
  tabs->addTab(createCurvePage(), tr("Curves"));
  tabs->addTab(createTimingPage(), tr("Timing"));
  tabs->addTab(createAppearancePage(), tr("Appearance"));
  setCentralWidget(tabs);

  connect(open, &QAction::triggered, this, &ControlsWindow::openRequested);
  connect(save, &QAction::triggered, this, &ControlsWindow::saveRequested);
  connect(saveAs, &QAction::triggered, this, &ControlsWindow::saveAsRequested);
  connect(showGraph, &QAction::triggered, this, &ControlsWindow::showGraphRequested);
  connect(dismiss, &QAction::triggered, this, &QWidget::hide);
  connect(defaults, &QAction::triggered, this, [this] {
    FileWorkflow::restoreDefaults(*model_);
    reloadFromModel();
    emit modelChanged();
    emit acquisitionConfigurationChanged();
  });
  connect(about, &QAction::triggered, this, [this] {
    QMessageBox::about(this, tr("About Qt StripTool"), versionText());
  });
  reloadFromModel();
}

QWidget* ControlsWindow::createCurvePage() {
  auto* page = new QWidget(this);
  auto* layout = new QVBoxLayout(page);
  auto* connectLayout = new QHBoxLayout;
  pvEntry_ = new QLineEdit(page);
  pvEntry_->setObjectName(QStringLiteral("pvEntry"));
  pvEntry_->setPlaceholderText(tr("Process variable name"));
  auto* connectButton = new QPushButton(tr("Connect"), page);
  connectButton->setObjectName(QStringLiteral("connectButton"));
  connectLayout->addWidget(new QLabel(tr("PV:"), page));
  connectLayout->addWidget(pvEntry_, 1);
  connectLayout->addWidget(connectButton);
  layout->addLayout(connectLayout);

  auto* rows = new QWidget(page);
  auto* grid = new QGridLayout(rows);
  grid->setAlignment(Qt::AlignTop);
  const QStringList headings{tr("#"), tr("Name"), tr("Color"), tr("Plot"),
                             tr("Scale"), tr("Precision"), tr("Minimum"),
                             tr("Maximum"), QString(), QString(), tr("Status")};
  for (int column = 0; column < headings.size(); ++column)
    grid->addWidget(new QLabel(headings[column], rows), 0, column);
  for (std::size_t i = 0; i < curveRows_.size(); ++i) {
    auto& row = curveRows_[i];
    const QString suffix = QString::number(i);
    grid->addWidget(new QLabel(QString::number(i + 1), rows), int(i + 1), 0);
    row.name = new QLineEdit(rows);
    row.name->setObjectName(QStringLiteral("curveName") + suffix);
    row.name->setMaxLength(static_cast<int>(kMaximumCurveNameLength));
    row.color = new QPushButton(tr("Color"), rows);
    row.color->setObjectName(QStringLiteral("curveColor") + suffix);
    row.plotted = new QCheckBox(rows);
    row.plotted->setObjectName(QStringLiteral("curvePlotted") + suffix);
    row.scale = new QComboBox(rows);
    row.scale->setObjectName(QStringLiteral("curveScale") + suffix);
    row.scale->addItems({tr("Linear"), tr("Log 10")});
    row.precision = new QSpinBox(rows);
    row.precision->setObjectName(QStringLiteral("curvePrecision") + suffix);
    row.precision->setRange(0, 20);
    row.minimum = limitEditor(rows);
    row.minimum->setObjectName(QStringLiteral("curveMinimum") + suffix);
    row.maximum = limitEditor(rows);
    row.maximum->setObjectName(QStringLiteral("curveMaximum") + suffix);
    row.modify = new QPushButton(tr("Modify"), rows);
    row.modify->setObjectName(QStringLiteral("curveModify") + suffix);
    row.remove = new QPushButton(tr("Remove"), rows);
    row.remove->setObjectName(QStringLiteral("curveRemove") + suffix);
    row.status = new QLabel(tr("Inactive"), rows);
    row.status->setObjectName(QStringLiteral("curveStatus") + suffix);
    grid->addWidget(row.name, int(i + 1), 1);
    grid->addWidget(row.color, int(i + 1), 2);
    grid->addWidget(row.plotted, int(i + 1), 3, Qt::AlignCenter);
    grid->addWidget(row.scale, int(i + 1), 4);
    grid->addWidget(row.precision, int(i + 1), 5);
    grid->addWidget(row.minimum, int(i + 1), 6);
    grid->addWidget(row.maximum, int(i + 1), 7);
    grid->addWidget(row.modify, int(i + 1), 8);
    grid->addWidget(row.remove, int(i + 1), 9);
    grid->addWidget(row.status, int(i + 1), 10);
    connect(row.modify, &QPushButton::clicked, this, [this, i] { applyCurve(i); });
    connect(row.name, &QLineEdit::returnPressed, row.modify, &QPushButton::click);
    connect(row.minimum, &QLineEdit::returnPressed, row.modify, &QPushButton::click);
    connect(row.maximum, &QLineEdit::returnPressed, row.modify, &QPushButton::click);
    connect(row.remove, &QPushButton::clicked, this, [this, i] { removeCurve(i); });
    connect(row.color, &QPushButton::clicked, this,
            [this, i] { chooseColor(model_->colors.curves[i], curveRows_[i].color); });
    connect(row.precision, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, i](int) { if (!loading_) curveRows_[i].precisionEdited = true; });
    connect(row.minimum, &QLineEdit::textChanged, this,
            [this, i] { if (!loading_) curveRows_[i].minimumEdited = true; });
    connect(row.maximum, &QLineEdit::textChanged, this,
            [this, i] { if (!loading_) curveRows_[i].maximumEdited = true; });
  }
  grid->setColumnStretch(1, 1);
  auto* scroll = new QScrollArea(page);
  scroll->setWidgetResizable(true);
  scroll->setWidget(rows);
  layout->addWidget(scroll);
  connect(connectButton, &QPushButton::clicked, this, &ControlsWindow::connectEnteredPv);
  connect(pvEntry_, &QLineEdit::returnPressed, this, &ControlsWindow::connectEnteredPv);
  return page;
}

QWidget* ControlsWindow::createTimingPage() {
  auto* page = new QWidget(this);
  auto* form = new QFormLayout(page);
  timespan_ = new QSpinBox(page);
  timespan_->setObjectName(QStringLiteral("timespanSeconds"));
  timespan_->setRange(1, std::numeric_limits<int>::max());
  sampleCount_ = new QSpinBox(page);
  sampleCount_->setObjectName(QStringLiteral("sampleCount"));
  sampleCount_->setRange(1, 65536);
  sampleInterval_ = valueEditor(page);
  sampleInterval_->setObjectName(QStringLiteral("sampleInterval"));
  sampleInterval_->setRange(0.01, 1e9);
  refreshInterval_ = valueEditor(page);
  refreshInterval_->setObjectName(QStringLiteral("refreshInterval"));
  refreshInterval_->setRange(0.1, 1e9);
  form->addRow(tr("History length (seconds):"), timespan_);
  form->addRow(tr("Sample count:"), sampleCount_);
  form->addRow(tr("Sample interval (seconds):"), sampleInterval_);
  form->addRow(tr("Refresh interval (seconds):"), refreshInterval_);
  auto updateTiming = [this] {
    if (loading_) return;
    model_->timing.timespanSeconds = static_cast<unsigned>(timespan_->value());
    model_->timing.numberOfSamples = sampleCount_->value();
    model_->timing.sampleIntervalSeconds = sampleInterval_->value();
    model_->timing.refreshIntervalSeconds = refreshInterval_->value();
    emit modelChanged();
    emit acquisitionConfigurationChanged();
  };
  connect(timespan_, qOverload<int>(&QSpinBox::valueChanged), this,
          [updateTiming](int) { updateTiming(); });
  connect(sampleCount_, qOverload<int>(&QSpinBox::valueChanged), this,
          [updateTiming](int) { updateTiming(); });
  connect(sampleInterval_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
          [updateTiming](double) { updateTiming(); });
  connect(refreshInterval_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
          [updateTiming](double) { updateTiming(); });
  return page;
}

QWidget* ControlsWindow::createAppearancePage() {
  auto* page = new QWidget(this);
  auto* form = new QFormLayout(page);
  foreground_ = new QPushButton(tr("Foreground"), page);
  foreground_->setObjectName(QStringLiteral("foregroundColor"));
  background_ = new QPushButton(tr("Background"), page);
  background_->setObjectName(QStringLiteral("backgroundColor"));
  gridColor_ = new QPushButton(tr("Grid"), page);
  gridColor_->setObjectName(QStringLiteral("gridColor"));
  xGrid_ = new QComboBox(page);
  xGrid_->setObjectName(QStringLiteral("xGridMode"));
  xGrid_->addItems({tr("None"), tr("Some"), tr("All")});
  yGrid_ = new QComboBox(page);
  yGrid_->setObjectName(QStringLiteral("yGridMode"));
  yGrid_->addItems({tr("None"), tr("Some"), tr("All")});
  coloredAxes_ = new QCheckBox(tr("Use curve colors for Y axes"), page);
  coloredAxes_->setObjectName(QStringLiteral("coloredAxes"));
  lineWidth_ = new QSpinBox(page);
  lineWidth_->setObjectName(QStringLiteral("lineWidth"));
  lineWidth_->setRange(0, 10);
  form->addRow(tr("Foreground color:"), foreground_);
  form->addRow(tr("Background color:"), background_);
  form->addRow(tr("Grid color:"), gridColor_);
  form->addRow(tr("X grid:"), xGrid_);
  form->addRow(tr("Y grid:"), yGrid_);
  form->addRow(QString(), coloredAxes_);
  form->addRow(tr("Graph line width:"), lineWidth_);
  connect(foreground_, &QPushButton::clicked, this,
          [this] { chooseColor(model_->colors.foreground, foreground_); });
  connect(background_, &QPushButton::clicked, this,
          [this] { chooseColor(model_->colors.background, background_); });
  connect(gridColor_, &QPushButton::clicked, this,
          [this] { chooseColor(model_->colors.grid, gridColor_); });
  const auto updateGraph = [this] {
    if (loading_) return;
    model_->graph.xGrid = static_cast<GridMode>(xGrid_->currentIndex());
    model_->graph.yGrid = static_cast<GridMode>(yGrid_->currentIndex());
    model_->graph.coloredYAxis = coloredAxes_->isChecked();
    model_->graph.lineWidth = lineWidth_->value();
    emit modelChanged();
  };
  connect(xGrid_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [updateGraph](int) { updateGraph(); });
  connect(yGrid_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [updateGraph](int) { updateGraph(); });
  connect(coloredAxes_, &QCheckBox::toggled, this,
          [updateGraph](bool) { updateGraph(); });
  connect(lineWidth_, qOverload<int>(&QSpinBox::valueChanged), this,
          [updateGraph](int) { updateGraph(); });
  return page;
}

void ControlsWindow::connectEnteredPv() {
  const QString name = pvEntry_->text().trimmed();
  if (name.isEmpty()) return;
  for (std::size_t i = 0; i < model_->curves.size(); ++i) {
    if (!model_->curves[i].nameSet) {
      curveRows_[i].name->setText(name);
      curveRows_[i].plotted->setChecked(true);
      applyCurve(i);
      pvEntry_->clear();
      return;
    }
  }
  QMessageBox::warning(this, tr("Curve Limit"), tr("All ten curve slots are in use."));
}

void ControlsWindow::applyCurve(std::size_t index) {
  auto& curve = model_->curves[index];
  const auto& row = curveRows_[index];
  bool minimumValid = true;
  bool maximumValid = true;
  const double minimum = row.minimumEdited
                             ? QLocale::c().toDouble(row.minimum->text(), &minimumValid)
                             : curve.minimum;
  const double maximum = row.maximumEdited
                             ? QLocale::c().toDouble(row.maximum->text(), &maximumValid)
                             : curve.maximum;
  if (!minimumValid || !maximumValid || !std::isfinite(minimum) ||
      !std::isfinite(maximum)) {
    QMessageBox::warning(this, tr("Invalid Curve Limit"),
                         tr("Enter finite numeric minimum and maximum values."));
    return;
  }
  if (minimum >= maximum) {
    QMessageBox::warning(this, tr("Invalid Curve Limit"),
                         tr("The minimum must be less than the maximum."));
    return;
  }
  const auto scale = static_cast<ScaleMode>(row.scale->currentIndex());
  if (scale == ScaleMode::Log10 &&
      (((row.minimumEdited || curve.minimumSet) && minimum <= 0.0) ||
       ((row.maximumEdited || curve.maximumSet) && maximum <= 0.0))) {
    QMessageBox::warning(this, tr("Invalid Logarithmic Limit"),
                         tr("Manual logarithmic limits must be positive."));
    return;
  }
  const std::string oldName = curve.name;
  const bool wasActive = curve.nameSet;
  const QString name = row.name->text().trimmed();
  if (name.contains(QRegularExpression(QStringLiteral("\\s")))) {
    QMessageBox::warning(this, tr("Invalid Curve Name"),
                         tr("A process variable name cannot contain whitespace."));
    return;
  }
  curve.name = name.toStdString();
  curve.nameSet = !name.isEmpty();
  curve.plotted = row.plotted->isChecked();
  curve.scale = scale;
  if (row.precisionEdited) curve.precision = row.precision->value();
  if (row.precisionEdited) curve.precisionSet = true;
  curve.minimum = minimum;
  if (row.minimumEdited) curve.minimumSet = true;
  curve.maximum = maximum;
  if (row.maximumEdited) curve.maximumSet = true;
  curveRows_[index].precisionEdited = false;
  curveRows_[index].minimumEdited = false;
  curveRows_[index].maximumEdited = false;
  emit modelChanged();
  if (wasActive != curve.nameSet || oldName != curve.name)
    emit acquisitionConfigurationChanged();
}

void ControlsWindow::removeCurve(std::size_t index) {
  auto defaults = makeDefaultModel();
  model_->curves[index] = defaults.curves[index];
  reloadCurveRow(index);
  setChannelMetadata(index, {});
  emit modelChanged();
  emit acquisitionConfigurationChanged();
}

void ControlsWindow::chooseColor(Rgba16& color, QPushButton* button) {
  const QColor selected = QColorDialog::getColor(toQColor(color), this);
  if (!selected.isValid()) return;
  color = fromQColor(selected);
  updateColorButton(button, color);
  emit modelChanged();
}

void ControlsWindow::updateColorButton(QPushButton* button, const Rgba16& color) {
  const QColor value = toQColor(color);
  const QColor text = value.lightnessF() < 0.5 ? Qt::white : Qt::black;
  button->setStyleSheet(QStringLiteral("background:%1;color:%2")
                            .arg(value.name(), text.name()));
}

void ControlsWindow::reloadCurveRow(std::size_t index) {
  const bool wasLoading = loading_;
  loading_ = true;
  const auto& curve = model_->curves[index];
  auto& row = curveRows_[index];
  row.name->setText(curve.nameSet ? QString::fromStdString(curve.name) : QString());
  row.name->setToolTip(QString::fromStdString(curve.comment));
  row.plotted->setChecked(curve.plotted);
  row.scale->setCurrentIndex(static_cast<int>(curve.scale));
  row.precision->setValue(curve.precision);
  row.minimum->setText(limitText(curve.minimum));
  row.maximum->setText(limitText(curve.maximum));
  row.minimum->setCursorPosition(0);
  row.maximum->setCursorPosition(0);
  row.precisionEdited = false;
  row.minimumEdited = false;
  row.maximumEdited = false;
  updateColorButton(row.color, model_->colors.curves[index]);
  loading_ = wasLoading;
}

void ControlsWindow::reloadFromModel() {
  updateTitle();
  loading_ = true;
  for (std::size_t i = 0; i < curveRows_.size(); ++i) reloadCurveRow(i);
  timespan_->setValue(static_cast<int>(model_->timing.timespanSeconds));
  sampleCount_->setValue(model_->timing.numberOfSamples);
  sampleInterval_->setValue(model_->timing.sampleIntervalSeconds);
  refreshInterval_->setValue(model_->timing.refreshIntervalSeconds);
  xGrid_->setCurrentIndex(static_cast<int>(model_->graph.xGrid));
  yGrid_->setCurrentIndex(static_cast<int>(model_->graph.yGrid));
  coloredAxes_->setChecked(model_->graph.coloredYAxis);
  lineWidth_->setValue(model_->graph.lineWidth);
  updateColorButton(foreground_, model_->colors.foreground);
  updateColorButton(background_, model_->colors.background);
  updateColorButton(gridColor_, model_->colors.grid);
  loading_ = false;
}

void ControlsWindow::updateTitle() {
  setWindowTitle(model_->filename.empty()
                     ? tr("Qt StripTool Controls")
                     : QString::fromStdString(model_->title) +
                           tr(" Controls — Qt StripTool"));
}

void ControlsWindow::refreshCurveMetadata(std::size_t index) {
  if (index >= curveRows_.size()) return;
  const auto& curve = model_->curves[index];
  auto& row = curveRows_[index];
  const bool wasLoading = loading_;
  loading_ = true;
  row.name->setToolTip(QString::fromStdString(curve.comment));
  if (!row.precisionEdited && !row.precision->hasFocus())
    row.precision->setValue(curve.precision);
  if (!row.minimumEdited && !row.minimum->hasFocus())
    row.minimum->setText(limitText(curve.minimum));
  if (!row.maximumEdited && !row.maximum->hasFocus())
    row.maximum->setText(limitText(curve.maximum));
  loading_ = wasLoading;
}

void ControlsWindow::setChannelMetadata(std::size_t curve, const ChannelMetadata& metadata) {
  if (curve >= curveRows_.size()) return;
  QString state;
  switch (metadata.connection) {
    case ConnectionState::Connected: state = tr("Live"); break;
    case ConnectionState::Connecting: state = tr("Connecting"); break;
    case ConnectionState::Stale: state = tr("Stale"); break;
    case ConnectionState::Error: state = tr("Error"); break;
    case ConnectionState::Disconnected: state = tr("Offline"); break;
  }
  if (!model_->curves[curve].nameSet) state = tr("Inactive");
  curveRows_[curve].status->setText(state);
  curveRows_[curve].status->setToolTip(QString::fromStdString(metadata.statusMessage));
}

}  // namespace striptool
