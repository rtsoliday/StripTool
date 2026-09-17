#include "ui/history_dialog.h"

#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>

namespace striptool {

HistoryDialog::HistoryDialog(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("historyDialog"));
  setWindowTitle(tr("Historical Time Range"));
  auto* layout = new QVBoxLayout(this);
  auto* form = new QFormLayout;
  from_ = new QDateTimeEdit(this);
  from_->setObjectName(QStringLiteral("historyFrom"));
  from_->setCalendarPopup(true);
  from_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  to_ = new QDateTimeEdit(this);
  to_->setObjectName(QStringLiteral("historyTo"));
  to_->setCalendarPopup(true);
  to_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  form->addRow(tr("From:"), from_);
  form->addRow(tr("To:"), to_);
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
  const auto now = std::chrono::system_clock::now();
  setRange({now - std::chrono::hours(1), now});
}

TimeRange HistoryDialog::selectedRange() const {
  return {std::chrono::system_clock::from_time_t(from_->dateTime().toSecsSinceEpoch()),
          std::chrono::system_clock::from_time_t(to_->dateTime().toSecsSinceEpoch())};
}

void HistoryDialog::setRange(TimeRange range) {
  from_->setDateTime(QDateTime::fromSecsSinceEpoch(
      std::chrono::system_clock::to_time_t(range.start)));
  to_->setDateTime(QDateTime::fromSecsSinceEpoch(
      std::chrono::system_clock::to_time_t(range.end)));
}

}  // namespace striptool
