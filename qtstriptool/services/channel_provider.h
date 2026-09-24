#pragma once

#include "core/model.h"
#include "core/sample_buffer.h"

#include <QObject>
#include <QString>

namespace striptool {

using ChannelId = quint64;

class ChannelProvider : public QObject {
  Q_OBJECT
public:
  explicit ChannelProvider(QObject* parent = nullptr) : QObject(parent) {}
  ~ChannelProvider() override = default;

  virtual ChannelId connectChannel(const QString& name) = 0;
  virtual void disconnectChannel(ChannelId id) = 0;

signals:
  void connectionChanged(striptool::ChannelId id,
                         striptool::ConnectionState state,
                         const QString& message);
  void metadataChanged(striptool::ChannelId id,
                       const striptool::ChannelMetadata& metadata);
  void descriptionReceived(striptool::ChannelId id, const QString& description);
  void sampleReceived(striptool::ChannelId id, const striptool::Sample& sample);
};

}  // namespace striptool

Q_DECLARE_METATYPE(striptool::ConnectionState)
Q_DECLARE_METATYPE(striptool::ChannelMetadata)
Q_DECLARE_METATYPE(striptool::Sample)
