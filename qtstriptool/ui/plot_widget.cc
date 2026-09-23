#include "ui/plot_widget.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace striptool {
namespace {

qint64 milliseconds(std::chrono::system_clock::time_point time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             time.time_since_epoch()).count();
}

QString formattedValue(double value, int precision, ScaleMode scale) {
  if (scale == ScaleMode::Log10)
    return QString::number(std::pow(10.0, value), 'g', std::max(1, precision));
  return QString::number(value, 'f', std::clamp(precision, 0, 12));
}

}  // namespace

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("plotWidget"));
  setMinimumSize(800, 260);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  resetView();
}

void PlotWidget::setModel(const StripToolModel& model) {
  for (std::size_t i = 0; i < kMaximumCurves; ++i) {
    const auto& old = model_.curves[i];
    const auto& next = model.curves[i];
    if (old.nameSet != next.nameSet || old.name != next.name ||
        old.scale != next.scale || old.minimumSet != next.minimumSet ||
        old.maximumSet != next.maximumSet ||
        (old.minimumSet && old.minimum != next.minimum) ||
        (old.maximumSet && old.maximum != next.maximum))
      autoScaleOverrides_[i] = false;
  }
  model_ = model;
  if (selectedCurve_ >= 0 &&
      (!model_.curves[static_cast<std::size_t>(selectedCurve_)].nameSet ||
       !model_.curves[static_cast<std::size_t>(selectedCurve_)].plotted))
    selectedCurve_ = -1;
  updateAutoRange();
  update();
}

void PlotWidget::setCurveSamples(std::size_t curve, std::vector<Sample> samples) {
  if (curve >= samples_.size()) return;
  std::sort(samples.begin(), samples.end(), [](const auto& left, const auto& right) {
    return left.timestamp < right.timestamp;
  });
  liveSamples_[curve] = std::move(samples);
  samples_[curve] = joinHistoricalAndLive(historicalSamples_[curve], liveSamples_[curve]);
  updateAutoRange();
  if (autoScroll_ && !paused_) resetView();
  update();
}

void PlotWidget::appendSample(std::size_t curve, Sample sample) {
  if (curve >= samples_.size()) return;
  auto& data = liveSamples_[curve];
  if (!data.empty() && sample.timestamp < data.back().timestamp) {
    const auto at = std::upper_bound(data.begin(), data.end(), sample.timestamp,
                                     [](const auto& time, const Sample& item) {
                                       return time < item.timestamp;
                                     });
    data.insert(at, sample);
  } else {
    data.push_back(sample);
  }
  const std::size_t maximum = std::max(1, model_.timing.numberOfSamples);
  if (data.size() > maximum)
    data.erase(data.begin(), data.end() - static_cast<std::ptrdiff_t>(maximum));
  samples_[curve] = joinHistoricalAndLive(historicalSamples_[curve], data);
  updateAutoRange();
  if (autoScroll_ && !paused_) {
    visibleTimeRange_.end = sample.timestamp;
    visibleTimeRange_.start = sample.timestamp -
        std::chrono::seconds(model_.timing.timespanSeconds);
  }
  update();
}

void PlotWidget::clearCurveSamples(std::size_t curve) {
  if (curve >= samples_.size()) return;
  liveSamples_[curve].clear();
  historicalSamples_[curve].clear();
  samples_[curve].clear();
  automaticRanges_[curve].reset();
  autoScaleOverrides_[curve] = false;
  update();
}

void PlotWidget::clearSamples() {
  for (std::size_t i = 0; i < samples_.size(); ++i) clearCurveSamples(i);
  resetView();
}

const std::vector<Sample>& PlotWidget::curveSamples(std::size_t curve) const {
  static const std::vector<Sample> empty;
  return curve < samples_.size() ? samples_[curve] : empty;
}

void PlotWidget::joinHistoricalSamples(std::size_t curve,
                                       const std::vector<Sample>& samples) {
  if (curve >= samples_.size()) return;
  auto ordered = samples;
  std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
    return left.timestamp < right.timestamp;
  });
  historicalSamples_[curve] = joinHistoricalAndLive(historicalSamples_[curve], ordered);
  samples_[curve] = joinHistoricalAndLive(historicalSamples_[curve], liveSamples_[curve]);
  updateAutoRange();
  update();
}

ValueRange PlotWidget::valueRange(std::size_t curve) const {
  if (curve >= kMaximumCurves) return {};
  if (automaticRanges_[curve]) return *automaticRanges_[curve];
  const auto& config = model_.curves[curve];
  ValueRange range{plotValue(config.minimum, config.scale),
                   plotValue(config.maximum, config.scale)};
  return range.isValid() ? range : ValueRange{};
}

void PlotWidget::setAutoScroll(bool enabled) {
  const bool changed = autoScroll_ != enabled;
  autoScroll_ = enabled;
  if (changed) emit autoScrollChanged(enabled);
  if (enabled && !paused_) resetView();
}

void PlotWidget::setPaused(bool paused) {
  paused_ = paused;
  if (!paused_ && autoScroll_) resetView();
}

void PlotWidget::setVisibleTimeRange(TimeRange range) {
  if (!range.isValid() || range.start == range.end) return;
  visibleTimeRange_ = range;
  setAutoScroll(false);
  updateAutoRange();
  update();
}

void PlotWidget::pan(double fractionOfWindow) {
  const auto duration = visibleTimeRange_.end - visibleTimeRange_.start;
  const auto shift = std::chrono::duration_cast<std::chrono::system_clock::duration>(
      std::chrono::duration<double>(
          std::chrono::duration<double>(duration).count() * fractionOfWindow));
  visibleTimeRange_.start += shift;
  visibleTimeRange_.end += shift;
  setAutoScroll(false);
  updateAutoRange();
  update();
}

void PlotWidget::zoom(double factor) {
  if (!(factor > 0.0)) return;
  const auto center = visibleTimeRange_.start +
                      (visibleTimeRange_.end - visibleTimeRange_.start) / 2;
  const auto half = std::chrono::duration_cast<std::chrono::system_clock::duration>(
      std::chrono::duration<double>(
          std::chrono::duration<double>(visibleTimeRange_.end -
                                        visibleTimeRange_.start).count() *
          factor / 2.0));
  if (half <= std::chrono::milliseconds(1)) return;
  visibleTimeRange_ = {center - half, center + half};
  setAutoScroll(false);
  updateAutoRange();
  update();
}

void PlotWidget::resetView() {
  auto latest = std::chrono::system_clock::now();
  bool hasSamples = false;
  for (const auto& curve : samples_) {
    if (!curve.empty() && (!hasSamples || curve.back().timestamp > latest)) {
      latest = curve.back().timestamp;
      hasSamples = true;
    }
  }
  visibleTimeRange_.end = latest;
  visibleTimeRange_.start = latest - std::chrono::seconds(model_.timing.timespanSeconds);
  const bool changed = !autoScroll_;
  autoScroll_ = true;
  if (changed) emit autoScrollChanged(true);
  updateAutoRange();
  update();
}

void PlotWidget::advanceToNow() {
  if (!autoScroll_ || paused_) return;
  visibleTimeRange_.end = std::chrono::system_clock::now();
  visibleTimeRange_.start = visibleTimeRange_.end -
      std::chrono::seconds(model_.timing.timespanSeconds);
  updateAutoRange();
  update();
}

void PlotWidget::autoScale(std::optional<std::size_t> curve) {
  const auto apply = [this](std::size_t index) {
    const auto selected = selectSamples(samples_[index], visibleTimeRange_.start,
                                        visibleTimeRange_.end,
                                        std::max(2, plotRect().width() > 0
                                                        ? int(plotRect().width()) * 2
                                                        : 2),
                                        model_.curves[index].scale);
    automaticRanges_[index] = sampleValueRange(selected, model_.curves[index].scale);
    autoScaleOverrides_[index] = true;
  };
  if (curve && *curve < kMaximumCurves) apply(*curve);
  else for (std::size_t i = 0; i < kMaximumCurves; ++i) apply(i);
  update();
}

void PlotWidget::replot() { update(); }

int PlotWidget::addAnnotation(Annotation annotation) {
  model_.annotations.push_back(std::move(annotation));
  selectedAnnotation_ = static_cast<int>(model_.annotations.size()) - 1;
  emit annotationSelectionChanged(selectedAnnotation_);
  emit annotationsChanged();
  update();
  return selectedAnnotation_;
}

bool PlotWidget::updateAnnotation(int index, Annotation annotation) {
  if (index < 0 || index >= static_cast<int>(model_.annotations.size())) return false;
  model_.annotations[static_cast<std::size_t>(index)] = std::move(annotation);
  emit annotationsChanged();
  update();
  return true;
}

bool PlotWidget::removeAnnotation(int index) {
  if (index < 0 || index >= static_cast<int>(model_.annotations.size())) return false;
  model_.annotations.erase(model_.annotations.begin() + index);
  selectedAnnotation_ = -1;
  emit annotationSelectionChanged(-1);
  emit annotationsChanged();
  update();
  return true;
}

void PlotWidget::selectAnnotation(int index) {
  if (index < -1 || index >= static_cast<int>(model_.annotations.size())) return;
  selectedAnnotation_ = index;
  emit annotationSelectionChanged(index);
  update();
}

QRectF PlotWidget::plotRect() const {
  int active = 0;
  for (const auto& curve : model_.curves)
    if (curve.nameSet && curve.plotted) ++active;
  const int leftAxes = (active + 1) / 2;
  const int rightAxes = active / 2;
  const int legendRows = active ? (active + legendColumns() - 1) / legendColumns() : 0;
  return rect().adjusted(18 + 56 * std::max(1, leftAxes), 22 + 22 * legendRows,
                         -(18 + 56 * std::max(1, rightAxes)), -44);
}

std::vector<std::size_t> PlotWidget::plottedCurves() const {
  std::vector<std::size_t> result;
  for (std::size_t i = 0; i < kMaximumCurves; ++i)
    if (model_.curves[i].nameSet && model_.curves[i].plotted) result.push_back(i);
  return result;
}

int PlotWidget::legendColumns() const {
  return std::clamp(width() / 170, 1, 5);
}

QRectF PlotWidget::legendRect(std::size_t position) const {
  const int columns = legendColumns();
  const qreal columnWidth = qreal(width() - 24) / columns;
  return QRectF(12 + (position % columns) * columnWidth,
                8 + (position / columns) * 22, columnWidth - 6, 20);
}

void PlotWidget::updateAutoRange() {
  for (std::size_t i = 0; i < kMaximumCurves; ++i) {
    if (autoScaleOverrides_[i]) {
      const auto visible = selectSamples(samples_[i], visibleTimeRange_.start,
                                         visibleTimeRange_.end,
                                         std::max(2, int(plotRect().width()) * 2),
                                         model_.curves[i].scale);
      automaticRanges_[i] = sampleValueRange(visible, model_.curves[i].scale);
    } else if (!model_.curves[i].minimumSet || !model_.curves[i].maximumSet) {
      const auto visible = selectSamples(samples_[i], visibleTimeRange_.start,
                                         visibleTimeRange_.end,
                                         std::max(2, int(plotRect().width()) * 2),
                                         model_.curves[i].scale);
      auto range = sampleValueRange(visible, model_.curves[i].scale);
      if (range) {
        const auto& config = model_.curves[i];
        if (config.minimumSet) range->minimum = plotValue(config.minimum, config.scale);
        if (config.maximumSet) range->maximum = plotValue(config.maximum, config.scale);
      }
      automaticRanges_[i] = range && range->isValid() ? range : std::nullopt;
    } else {
      automaticRanges_[i].reset();
    }
  }
}

QColor PlotWidget::color(const Rgba16& value) const {
  return QColor::fromRgbF(value.red / 65535.0, value.green / 65535.0,
                          value.blue / 65535.0, value.alpha / 65535.0);
}

std::optional<QPointF> PlotWidget::mapSample(const Sample& sample,
                                            const ValueRange& range,
                                            ScaleMode scale) const {
  const QRectF area = plotRect();
  const qint64 begin = milliseconds(visibleTimeRange_.start);
  const qint64 end = milliseconds(visibleTimeRange_.end);
  const qint64 time = milliseconds(sample.timestamp);
  const double value = plotValue(sample.value, scale);
  if (end <= begin || !sample.plotable || !range.isValid() || !std::isfinite(value) ||
      time < begin || time > end) return std::nullopt;
  return QPointF(area.left() + area.width() * double(time - begin) / double(end - begin),
                 area.bottom() - area.height() * (value - range.minimum) /
                                     (range.maximum - range.minimum));
}

void PlotWidget::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), color(model_.colors.background));
  const QRectF area = plotRect();
  const QColor foreground = color(model_.colors.foreground);
  const auto visibleCurves = plottedCurves();
  for (std::size_t position = 0; position < visibleCurves.size(); ++position) {
    const std::size_t index = visibleCurves[position];
    const QRectF legend = legendRect(position);
    if (selectedCurve_ == static_cast<int>(index)) {
      painter.setPen(QPen(foreground, 1));
      painter.drawRoundedRect(legend, 3, 3);
    }
    painter.setPen(QPen(color(model_.colors.curves[index]), 3));
    painter.drawLine(QPointF(legend.left() + 5, legend.center().y()),
                     QPointF(legend.left() + 19, legend.center().y()));
    QString label = QString::fromStdString(model_.curves[index].name);
    if (!samples_[index].empty() && samples_[index].back().plotable &&
        std::isfinite(samples_[index].back().value))
      label += QStringLiteral("  %1").arg(
          samples_[index].back().value, 0, 'g',
          std::clamp(model_.curves[index].precision, 1, 15));
    painter.setPen(foreground);
    const QRectF textArea = legend.adjusted(24, 0, -3, 0);
    painter.drawText(textArea, Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(label, Qt::ElideRight,
                                                       static_cast<int>(textArea.width())));
  }
  painter.setPen(foreground);
  painter.drawRect(area);

  const QColor grid = color(model_.colors.grid);
  painter.setPen(QPen(grid, 1, Qt::DashLine));
  const int xDivisions = model_.graph.xGrid == GridMode::All ? 10 : 5;
  const int yDivisions = model_.graph.yGrid == GridMode::All ? 10 : 5;
  if (model_.graph.xGrid != GridMode::None)
    for (int i = 1; i < xDivisions; ++i)
      painter.drawLine(QPointF(area.left() + area.width() * i / xDivisions, area.top()),
                       QPointF(area.left() + area.width() * i / xDivisions, area.bottom()));
  if (model_.graph.yGrid != GridMode::None)
    for (int i = 1; i < yDivisions; ++i)
      painter.drawLine(QPointF(area.left(), area.top() + area.height() * i / yDivisions),
                       QPointF(area.right(), area.top() + area.height() * i / yDivisions));

  painter.setPen(foreground);
  const qint64 begin = milliseconds(visibleTimeRange_.start);
  const qint64 end = milliseconds(visibleTimeRange_.end);
  const bool showMilliseconds = end - begin < 10000;
  const int labelWidth = showMilliseconds ? 106 : 76;
  const int timeDivisions = std::clamp(int(area.width() / (labelWidth + 12)), 1, 5);
  for (int i = 0; i <= timeDivisions; ++i) {
    const qint64 time = begin + (end - begin) * i / timeDivisions;
    const QString label = QDateTime::fromMSecsSinceEpoch(time).toString(
        showMilliseconds ? QStringLiteral("HH:mm:ss.zzz") : QStringLiteral("HH:mm:ss"));
    const qreal x = area.left() + area.width() * i / timeDivisions;
    painter.drawText(QRectF(x - labelWidth / 2.0, area.bottom() + 7,
                            labelWidth, 20), Qt::AlignHCenter, label);
  }
  painter.drawText(QRectF(area.right() - 160, area.bottom() + 26, 160, 16),
                   Qt::AlignRight,
                   QDateTime::fromMSecsSinceEpoch(milliseconds(visibleTimeRange_.end))
                       .toString(QStringLiteral("MMM d, yyyy")));

  int leftAxis = 0;
  int rightAxis = 0;
  for (std::size_t curveIndex = 0; curveIndex < kMaximumCurves; ++curveIndex) {
    const auto& config = model_.curves[curveIndex];
    if (!config.nameSet || !config.plotted) continue;
    const auto range = valueRange(curveIndex);
    const QColor curveColor = color(model_.colors.curves[curveIndex]);
    const QColor axisColor = model_.graph.coloredYAxis ? curveColor : foreground;
    painter.setPen(axisColor);
    const bool useLeftAxis = (leftAxis + rightAxis) % 2 == 0;
    const int axisNumber = useLeftAxis ? leftAxis++ : rightAxis++;
    const qreal axisX = useLeftAxis ? area.left() - 46 - 56 * axisNumber
                                    : area.right() + 6 + 56 * axisNumber;
    for (int tick = 0; tick <= 5; ++tick) {
      const double value = range.maximum -
          (range.maximum - range.minimum) * tick / 5.0;
      QString label = formattedValue(value, config.precision, config.scale);
      if (painter.fontMetrics().horizontalAdvance(label) > 40)
        label = QString::number(config.scale == ScaleMode::Log10
                                    ? std::pow(10.0, value) : value, 'g', 4);
      painter.drawText(QRectF(axisX, area.top() + area.height() * tick / 5.0 - 9,
                              40, 18),
                       useLeftAxis ? Qt::AlignRight : Qt::AlignLeft, label);
    }
    painter.save();
    const qreal unitX = useLeftAxis ? axisX - 9 : axisX + 49;
    painter.translate(unitX, area.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-area.height() / 2, -9, area.height(), 18),
                     Qt::AlignCenter, QString::fromStdString(config.units));
    painter.restore();

    const auto selected = selectSamples(samples_[curveIndex], visibleTimeRange_.start,
                                        visibleTimeRange_.end,
                                        std::max(2, int(area.width()) * 2), config.scale);
    QPainterPath path;
    bool started = false;
    for (const auto& sample : selected) {
      const auto point = mapSample(sample, range, config.scale);
      if (!point) { started = false; continue; }
      if (!started) { path.moveTo(*point); started = true; }
      else path.lineTo(*point);
    }
    painter.setPen(QPen(curveColor, std::max(1, model_.graph.lineWidth)));
    painter.save();
    painter.setClipRect(area);
    painter.drawPath(path);
    painter.restore();
  }

  for (std::size_t i = 0; i < model_.annotations.size(); ++i) {
    const auto& annotation = model_.annotations[i];
    const qint64 begin = milliseconds(visibleTimeRange_.start);
    const qint64 end = milliseconds(visibleTimeRange_.end);
    const qint64 time = milliseconds(annotation.time);
    if (time < begin || time > end || end <= begin) continue;
    const qreal x = area.left() + area.width() * double(time - begin) / double(end - begin);
    painter.setPen(QPen(i == static_cast<std::size_t>(selectedAnnotation_)
                            ? QColor(255, 140, 0) : foreground, 1));
    painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    painter.drawText(QRectF(x + 3, area.top() + 3, 180, 18),
                     QString::fromStdString(annotation.text));
  }

  if (area.contains(cursorPosition_)) {
    painter.setPen(QPen(foreground, 1, Qt::DotLine));
    painter.drawLine(QPointF(cursorPosition_.x(), area.top()),
                     QPointF(cursorPosition_.x(), area.bottom()));
    painter.drawLine(QPointF(area.left(), cursorPosition_.y()),
                     QPointF(area.right(), cursorPosition_.y()));
  }
}

void PlotWidget::mouseMoveEvent(QMouseEvent* event) {
  cursorPosition_ = event->pos();
  const QRectF area = plotRect();
  if (dragging_ && area.width() > 0) {
    if (draggingAnnotation_ && selectedAnnotation_ >= 0) {
      const qint64 begin = milliseconds(visibleTimeRange_.start);
      const qint64 end = milliseconds(visibleTimeRange_.end);
      const qreal position = std::clamp<qreal>(
          (event->pos().x() - area.left()) / area.width(), 0.0, 1.0);
      model_.annotations[static_cast<std::size_t>(selectedAnnotation_)].time =
          std::chrono::system_clock::time_point(
              std::chrono::milliseconds(begin + qint64((end - begin) * position)));
    } else {
      const double fraction = -double(event->pos().x() - dragStart_.x()) / area.width();
      const auto duration = dragRange_.end - dragRange_.start;
      const auto shift = std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::duration<double>(std::chrono::duration<double>(duration).count() * fraction));
      visibleTimeRange_ = {dragRange_.start + shift, dragRange_.end + shift};
      setAutoScroll(false);
      updateAutoRange();
    }
  }
  if (area.contains(event->pos())) {
    const qint64 begin = milliseconds(visibleTimeRange_.start);
    const qint64 end = milliseconds(visibleTimeRange_.end);
    const qint64 time = begin + qint64((end - begin) *
        (event->pos().x() - area.left()) / area.width());
    int curveIndex = selectedCurve_;
    if (curveIndex < 0) {
      const auto curves = plottedCurves();
      if (!curves.empty()) curveIndex = static_cast<int>(curves.front());
    }
    double value = std::numeric_limits<double>::quiet_NaN();
    if (curveIndex >= 0) {
      const auto range = valueRange(static_cast<std::size_t>(curveIndex));
      const double plotted = range.maximum -
          (event->pos().y() - area.top()) / area.height() *
              (range.maximum - range.minimum);
      value = model_.curves[static_cast<std::size_t>(curveIndex)].scale == ScaleMode::Log10
                  ? std::pow(10.0, plotted) : plotted;
    }
    emit cursorLocationChanged(QDateTime::fromMSecsSinceEpoch(time), value, curveIndex);
  }
  update();
}

void PlotWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) return;
  const auto curves = plottedCurves();
  for (std::size_t position = 0; position < curves.size(); ++position) {
    if (legendRect(position).contains(event->pos())) {
      selectedCurve_ = static_cast<int>(curves[position]);
      update();
      return;
    }
  }
  if (!plotRect().contains(event->pos())) return;
  const qint64 begin = milliseconds(visibleTimeRange_.start);
  const qint64 end = milliseconds(visibleTimeRange_.end);
  const qint64 time = begin + qint64((end - begin) *
      (event->pos().x() - plotRect().left()) / plotRect().width());
  int nearest = -1;
  qint64 distance = std::numeric_limits<qint64>::max();
  for (std::size_t i = 0; i < model_.annotations.size(); ++i) {
    const qint64 candidate = std::abs(milliseconds(model_.annotations[i].time) - time);
    if (candidate < distance) { distance = candidate; nearest = static_cast<int>(i); }
  }
  const qint64 tolerance = (end - begin) * 6 / std::max(1, int(plotRect().width()));
  selectAnnotation(distance <= tolerance ? nearest : -1);
  dragging_ = true;
  draggingAnnotation_ = selectedAnnotation_ >= 0;
  dragStart_ = event->pos();
  dragRange_ = visibleTimeRange_;
}

void PlotWidget::mouseReleaseEvent(QMouseEvent*) {
  if (draggingAnnotation_) emit annotationsChanged();
  dragging_ = false;
  draggingAnnotation_ = false;
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !plotRect().contains(event->pos())) return;
  const qint64 begin = milliseconds(visibleTimeRange_.start);
  const qint64 end = milliseconds(visibleTimeRange_.end);
  const qint64 time = begin + qint64((end - begin) *
      (event->pos().x() - plotRect().left()) / plotRect().width());
  bool accepted = false;
  const QString text = QInputDialog::getText(
      this, tr("Plot Annotation"), tr("Text:"), QLineEdit::Normal,
      selectedAnnotation_ >= 0
          ? QString::fromStdString(model_.annotations[static_cast<std::size_t>(
                                      selectedAnnotation_)].text)
          : QString(),
      &accepted);
  if (!accepted) return;
  if (selectedAnnotation_ >= 0) {
    auto annotation = model_.annotations[static_cast<std::size_t>(selectedAnnotation_)];
    annotation.text = text.toStdString();
    updateAnnotation(selectedAnnotation_, std::move(annotation));
  } else {
    addAnnotation({std::chrono::system_clock::time_point(std::chrono::milliseconds(time)),
                   std::nullopt, text.toStdString()});
  }
}

void PlotWidget::wheelEvent(QWheelEvent* event) {
  const int verticalDelta = event->angleDelta().y();
  if (verticalDelta == 0) {
    event->ignore();
    return;
  }
  zoom(verticalDelta > 0 ? 0.8 : 1.25);
  event->accept();
}

void PlotWidget::keyPressEvent(QKeyEvent* event) {
  if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
      selectedAnnotation_ >= 0) {
    removeAnnotation(selectedAnnotation_);
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

}  // namespace striptool
