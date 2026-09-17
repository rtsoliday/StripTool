#pragma once

#include "core/model.h"

#include <QDialog>

class QDateTimeEdit;

namespace striptool {

class HistoryDialog final : public QDialog {
  Q_OBJECT
public:
  explicit HistoryDialog(QWidget* parent = nullptr);
  TimeRange selectedRange() const;
  void setRange(TimeRange range);

private:
  QDateTimeEdit* from_ = nullptr;
  QDateTimeEdit* to_ = nullptr;
};

}  // namespace striptool
