#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

#include "genibus_frame.h"

namespace esphome::genibus {

// The APDUs of one request. The largest planned one is a class 10 SET of an 18 byte object (86/14, 86/16, 86/18):
// 2 + 9 + 18 = 29 bytes.
static constexpr size_t MAX_REQUEST_SIZE = 32;
// The largest object body a class 10 SET fits into one request
static constexpr size_t MAX_SET_BODY_SIZE = MAX_REQUEST_SIZE - 2 - SET_OBJECT_HEADER_SIZE;
// The most class 10 GETs one request holds. A device may answer fewer; it sets its own limit.
static constexpr size_t MAX_GET_OBJECTS = MAX_REQUEST_SIZE / GET_OBJECT_SIZE;

enum class GenibusError : uint8_t {
  GENIBUS_ERROR_NONE,
  GENIBUS_ERROR_TIMEOUT,
  GENIBUS_ERROR_CRC,
  GENIBUS_ERROR_MALFORMED,  // the APDU lengths do not add up to the frame
  GENIBUS_ERROR_WRITE_FAILED,
  GENIBUS_ERROR_DISCONNECTED,
};

/// The answer to a request; apdus points into the receive buffer and is valid only during the callback. It is empty
/// on error, and also on success when the reply has no APDU (LE = 2).
/// GENIBUS_ERROR_NONE only means a valid reply frame arrived: an APDU with an ACK code other than 0 (unknown class or
/// ID, illegal operation) is still a success for the bus and clears its warning, so the device checks each ACK.
/// GENIbus has no transaction id: a reply that comes after the timeout and the quiet period is taken for the answer
/// to the device's next request. A device checks class, object type and version of every reply, and counts a write
/// as done only after reading the value back, never on the acknowledgement alone.
struct GenibusResponse {
  GenibusError error;
  std::span<const uint8_t> apdus;

  /// The APDU at index as a response of its own, with the same error; its APDUs are empty when the reply has fewer.
  /// A reply holds the APDUs in the order of the request and does not name the objects again.
  GenibusResponse part(size_t index) const;
};

/// A class 10 data object.
struct ObjectId {
  uint8_t object;
  uint16_t sub_id;
};

class GenibusDevice;

/// The request of one device that waits for the wire; size is 0 while nothing waits.
struct GenibusPending {
  GenibusDevice *device;
  std::array<uint8_t, MAX_REQUEST_SIZE> apdus;
  uint8_t size;
};

/// The protocol side of a GENIbus master. A transport derives from it, writes frames and feeds received bytes.
/// Only one transaction is on the wire at a time; each device has at most one request open.
/// The status warning is per bus: the last transaction sets or clears it.
class GenibusBus : public Component {
 public:
  void loop() override;
  void dump_config() override;

  void set_master_address(uint8_t address) { this->master_address_ = address; }
  uint8_t get_master_address() const { return this->master_address_; }
  void set_response_timeout(uint32_t timeout_ms) { this->response_timeout_ms_ = timeout_ms; }
  void set_quiet_period(uint32_t quiet_ms) { this->quiet_ms_ = quiet_ms; }

  SLOT_LIST_REQUIRES(GENIBUS_DEVICE_COUNT)
  void register_device(GenibusDevice *device) { this->pending_.push_back({device, {}, 0}); }

  /// Queue the request of device. Returns false, queueing nothing, when the bus is not connected, the request is
  /// empty or longer than MAX_REQUEST_SIZE, the device is not registered, or it already has a request open (see
  /// is_busy()). Every accepted request gets exactly one on_response(); the device's slot is free again when
  /// on_response() runs, so it may send its next request from there.
  bool send(GenibusDevice *device, std::span<const uint8_t> apdus);
  /// Whether device has a request waiting or on the wire.
  bool is_busy(const GenibusDevice *device) const;

  /// Feed received bytes, in any chunks. Call from the main loop only.
  void receive_bytes(std::span<const uint8_t> data);

  virtual bool is_connected() const = 0;

 protected:
  /// Write a whole frame; a transport that needs smaller pieces splits it.
  virtual bool write_frame(std::span<const uint8_t> frame) = 0;
  /// Call when the connection is up, after is_connected() has started to return true: every device gets
  /// on_connected() and may send its first request from there.
  void connection_established_();
  /// Call when the connection drops, after is_connected() has started to return false: every open and waiting request
  /// ends with GENIBUS_ERROR_DISCONNECTED, then every device gets on_disconnected(); requests sent from those callbacks
  /// are refused. Calls after the first one until the next connection_established_() end requests only.
  void connection_lost_();

  void handle_frame_();
  void transmit_next_();
  void finish_(GenibusError error, std::span<const uint8_t> apdus = {});

  [[no_unique_address]] SlotList<GenibusPending, GENIBUS_DEVICE_COUNT> pending_;
  FrameAssembler rx_;
  GenibusDevice *active_{nullptr};  // the device whose request is on the wire
  uint32_t sent_at_{0};
  uint32_t quiet_since_{0};
  // Default for a transport that sets none
  uint32_t response_timeout_ms_{1000};
  // After a timeout, a late reply must not be taken for the answer to the next request
  uint32_t quiet_ms_{250};
  uint8_t next_slot_{0};  // where the round-robin search for the next request starts
  uint8_t master_address_{0};
  uint8_t active_address_{0};
  bool quiet_{false};
  bool down_notified_{false};  // whether the devices have heard of the current loss of the connection
};

/// A device on the bus; it gets the answer to its request in on_response().
class GenibusDevice : public PollingComponent, public Parented<GenibusBus> {
 public:
  virtual void on_response(const GenibusResponse &response) = 0;
  /// The connection is up; a device may read at once instead of waiting for its next update().
  virtual void on_connected() {}
  /// The connection is down and every request of this device has ended; a device may mark its values unknown.
  virtual void on_disconnected() {}

  void set_address(uint8_t address) { this->address_ = address; }
  uint8_t get_address() const { return this->address_; }

 protected:
  bool request_object_(uint8_t object, uint16_t sub_id) {
    const ObjectId id{object, sub_id};
    return this->request_objects_({&id, 1});
  }
  /// Send a class 10 GET of up to MAX_GET_OBJECTS objects in one frame; the reply has an APDU per object, in this
  /// order (see GenibusResponse::part()). False, sending nothing, when there are none or too many. Not every device
  /// answers that many in one request; the device component knows its limit.
  bool request_objects_(std::span<const ObjectId> objects);
  /// Send a class 10 SET of object/sub_id; false, sending nothing, when the body is empty or longer than
  /// MAX_SET_BODY_SIZE, or when send() refuses it.
  bool set_object_(uint8_t object, uint16_t sub_id, uint16_t type, uint8_t version, std::span<const uint8_t> body);
  /// Send one class 3 command.
  bool send_command_(uint8_t command);

  uint8_t address_{0};
};

}  // namespace esphome::genibus
