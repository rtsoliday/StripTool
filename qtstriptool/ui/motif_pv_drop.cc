#include "ui/motif_pv_drop.h"
#include "ui/pv_drop.h"

#ifdef STRIPTOOL_HAVE_XCB
#include <QApplication>
#include <QEvent>
#include <QSocketNotifier>
#include <QTimer>
#include <QWidget>
#include <xcb/xcb.h>
#include <array>
#include <cstdlib>
#include <cstring>

namespace striptool {
namespace {
// Motif wire values, independent of the Motif toolkit. DROP_ONLY needs only
// DROP_START and ICCCM selection transfers, and coexists with Qt's XDND path.
// Wire protocol: https://lesstif.sourceforge.net/InsideLessTif/node89.html
constexpr unsigned char kDropStart = 5;
constexpr unsigned char kReceiverReply = 0x80;
constexpr quint16 kCopy = 2;
constexpr quint16 kValidSite = 3 << 4;
constexpr quint16 kInvalidSite = 2 << 4;
constexpr quint16 kCancel = 2 << 12;

quint32 readWire(const uint8_t* data, int size, bool little) {
  quint32 value = 0;
  for (int i = 0; i < size; ++i)
    value |= quint32(data[i]) << (8 * (little ? i : size - 1 - i));
  return value;
}
void writeWire(uint8_t* data, quint32 value, int size, bool little) {
  for (int i = 0; i < size; ++i)
    data[i] = uint8_t(value >> (8 * (little ? i : size - 1 - i)));
}

class MotifPvDropTarget final : public QObject {
public:
  MotifPvDropTarget(QWidget* window,
                    std::function<bool(const QStringList&)> addPvs)
      : QObject(window), window_(window), addPvs_(std::move(addPvs)) {
    int screenNumber = 0;
    connection_ = xcb_connect(nullptr, &screenNumber);
    if (xcb_connection_has_error(connection_)) return;
    auto screens = xcb_setup_roots_iterator(xcb_get_setup(connection_));
    for (int n = 0; n < screenNumber; ++n) xcb_screen_next(&screens);
    proxy_ = xcb_generate_id(connection_);
    xcb_create_window(connection_, XCB_COPY_FROM_PARENT, proxy_, screens.data->root,
                      0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screens.data->root_visual, 0, nullptr);
    receiverInfo_ = atom("_MOTIF_DRAG_RECEIVER_INFO");
    message_ = atom("_MOTIF_DRAG_AND_DROP_MESSAGE");
    dataProperty_ = atom("_QTSTRIPTOOL_MOTIF_PV");
    compoundText_ = atom("COMPOUND_TEXT");
    success_ = atom("XmTRANSFER_SUCCESS");
    failure_ = atom("XmTRANSFER_FAILURE");
    notifier_ = new QSocketNotifier(xcb_get_file_descriptor(connection_),
                                    QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this] { drainEvents(); });
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] { finish(false); });
    window_->installEventFilter(this);
    if (window_->isVisible()) advertise();
  }
  ~MotifPvDropTarget() override {
    finish(false);
    if (notifier_) notifier_->setEnabled(false);
    if (connection_) {
      if (proxy_) xcb_destroy_window(connection_, proxy_);
      xcb_disconnect(connection_);
    }
  }
protected:
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange)
      advertise();
    return false;
  }
private:
  xcb_atom_t atom(const char* name) {
    auto* reply = xcb_intern_atom_reply(connection_,
        xcb_intern_atom(connection_, false, std::strlen(name), name), nullptr);
    const auto value = reply ? reply->atom : xcb_atom_t(XCB_ATOM_NONE);
    std::free(reply);
    return value;
  }
  void advertise() {
    if (!proxy_ || !window_->internalWinId()) return;
    // Receiver info: byte order, version, DROP_ONLY style, proxy window,
    // drop-site count, padding, heap offset. All multibyte fields are LE.
    std::array<uint8_t, 16> info{};
    info[0] = 'l';
    info[2] = 1;
    writeWire(info.data() + 4, proxy_, 4, true);
    writeWire(info.data() + 12, info.size(), 4, true);
    xcb_change_property(connection_, XCB_PROP_MODE_REPLACE,
                         window_->internalWinId(), receiverInfo_, receiverInfo_,
                         8, info.size(), info.data());
    xcb_flush(connection_);
  }
  void drainEvents() {
    while (auto* event = xcb_poll_for_event(connection_)) {
      const auto type = event->response_type & 0x7f;
      if (type == XCB_CLIENT_MESSAGE)
        startDrop(*reinterpret_cast<xcb_client_message_event_t*>(event));
      else if (type == XCB_SELECTION_NOTIFY)
        receiveSelection(*reinterpret_cast<xcb_selection_notify_event_t*>(event));
      std::free(event);
    }
  }
  void startDrop(const xcb_client_message_event_t& event) {
    if (event.type != message_ || event.format != 8 ||
        event.data.data8[0] != kDropStart) return;
    const auto* bytes = event.data.data8;
    if (bytes[1] != 'l' && bytes[1] != 'B') return;
    const bool little = bytes[1] == 'l';
    const auto flags = readWire(bytes + 2, 2, little);
    const auto timestamp = readWire(bytes + 4, 4, little);
    const auto selection = readWire(bytes + 12, 4, little);
    const auto source = readWire(bytes + 16, 4, little);
    const bool accept = !selection_ && window_->isVisible() && selection && source &&
                        ((flags >> 8) & kCopy) && !(flags & 0xf000);
    xcb_client_message_event_t reply{};
    reply.response_type = XCB_CLIENT_MESSAGE;
    reply.format = 8;
    reply.window = source;
    reply.type = message_;
    std::memcpy(reply.data.data8, bytes, 20);
    reply.data.data8[0] = kDropStart | kReceiverReply;
    writeWire(reply.data.data8 + 2,
              accept ? kCopy | kValidSite | (kCopy << 8)
                     : kInvalidSite | kCancel, 2, little);
    xcb_send_event(connection_, false, source, XCB_EVENT_MASK_NO_EVENT,
                    reinterpret_cast<const char*>(&reply));
    if (accept) {
      selection_ = selection;
      timestamp_ = timestamp;
      target_ = compoundText_;
      xcb_convert_selection(connection_, proxy_, selection_, target_,
                             dataProperty_, timestamp_);
      timeout_.start(5000);
    }
    xcb_flush(connection_);
  }
  void receiveSelection(const xcb_selection_notify_event_t& event) {
    if (!selection_ || event.requestor != proxy_ ||
        event.selection != selection_ || event.target != target_ ||
        event.time != timestamp_) return;
    if (!event.property) {
      if (target_ == compoundText_) {
        target_ = XCB_ATOM_STRING;
        xcb_convert_selection(connection_, proxy_, selection_, target_,
                               dataProperty_, timestamp_);
        xcb_flush(connection_);
      } else finish(false);
      return;
    }
    if (event.property != dataProperty_) { finish(false); return; }
    auto* reply = xcb_get_property_reply(connection_,
        xcb_get_property(connection_, true, proxy_, dataProperty_,
                          XCB_GET_PROPERTY_TYPE_ANY, 0, 1024), nullptr);
    bool accepted = false;
    if (reply && reply->format == 8 && !reply->bytes_after &&
        (reply->type == compoundText_ || reply->type == XCB_ATOM_STRING)) {
      // EPICS PV names are ASCII; compound-text escape sequences are not PVs.
      const auto names = droppedPvNames(QString::fromLatin1(
          static_cast<const char*>(xcb_get_property_value(reply)),
          xcb_get_property_value_length(reply)));
      accepted = !names.isEmpty() && addPvs_(names);
    }
    std::free(reply);
    finish(accepted);
  }
  void finish(bool accepted) {
    timeout_.stop();
    if (!selection_) return;
    xcb_convert_selection(connection_, proxy_, selection_,
                           accepted ? success_ : failure_, XCB_ATOM_NONE,
                           timestamp_);
    xcb_delete_property(connection_, proxy_, dataProperty_);
    xcb_flush(connection_);
    selection_ = XCB_ATOM_NONE;
  }
  QWidget* window_;
  std::function<bool(const QStringList&)> addPvs_;
  xcb_connection_t* connection_ = nullptr;
  xcb_window_t proxy_ = XCB_WINDOW_NONE;
  xcb_atom_t receiverInfo_ = 0, message_ = 0, dataProperty_ = 0;
  xcb_atom_t compoundText_ = 0, success_ = 0, failure_ = 0;
  xcb_atom_t selection_ = 0, target_ = 0;
  xcb_timestamp_t timestamp_ = 0;
  QSocketNotifier* notifier_ = nullptr;
  QTimer timeout_;
};
}  // namespace
void installMotifPvDropTarget(QWidget* window,
                              std::function<bool(const QStringList&)> addPvs) {
  if (QApplication::platformName() == QStringLiteral("xcb"))
    new MotifPvDropTarget(window, std::move(addPvs));
}
}  // namespace striptool
#else
namespace striptool {
void installMotifPvDropTarget(QWidget*, std::function<bool(const QStringList&)>) {}
}
#endif
