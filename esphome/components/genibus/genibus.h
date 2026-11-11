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

static constexpr uint8_t DEFAULT_MASTER_ADDRESS = 0xF8;
static constexpr uint8_t DEFAULT_DEVICE_ADDRESS = 0xE7;
// The APDUs of one request
static constexpr size_t MAX_REQUEST_SIZE = 64;
static constexpr uint32_t RESPONSE_TIMEOUT_MS = 1000;

enum class GenibusError : uint8_t {
  GENIBUS_ERROR_NONE,
  GENIBUS_ERROR_TIMEOUT,
  GENIBUS_ERROR_CRC,
  GENIBUS_ERROR_MALFORMED,  // the APDU lengths do not add up to the frame
  GENIBUS_ERROR_WRITE_FAILED,
  GENIBUS_ERROR_DISCONNECTED,
};

/// The answer to a request; apdus points into the receive buffer, is valid only during the callback and is
/// empty on error. Each APDU still carries its own ACK code.
struct GenibusResponse {
  GenibusError error;
  std::span<const uint8_t> apdus;
};

class GenibusDevice;

/// The request of one device that waits for the wire; size is 0 while nothing waits.
struct GenibusPending {
  GenibusDevice *device;
  std::array<uint8_t, MAX_REQUEST_SIZE> apdus;
  uint8_t size;
};

/// The protocol side of a GENIbus master. A transport derives from it, writes frames and feeds received bytes.
/// Only one transaction is on the wire at a time; each device has at most one request waiting.
class GenibusBus : public Component {
 public:
  void loop() override;
  void dump_config() override;

  void set_master_address(uint8_t address) { this->master_address_ = address; }
  uint8_t get_master_address() const { return this->master_address_; }

  SLOT_LIST_REQUIRES(GENIBUS_DEVICE_COUNT)
  void register_device(GenibusDevice *device) { this->pending_.push_back({device, {}, 0}); }

  /// Queue the request of device. A device's new request replaces its earlier one, which then gets no answer.
  /// Returns false, queueing nothing, when the bus is not connected or the request is empty or too long;
  /// otherwise device gets exactly one on_response() for it.
  bool send(GenibusDevice *device, std::span<const uint8_t> apdus);

  /// Feed received bytes, in any chunks. Call from the main loop only.
  void receive_bytes(std::span<const uint8_t> data);

  virtual bool is_connected() const = 0;

 protected:
  /// Write a whole frame; a transport that needs smaller pieces splits it.
  virtual bool write_frame(std::span<const uint8_t> frame) = 0;
  /// Call when the connection drops: every open and waiting request ends with GENIBUS_ERROR_DISCONNECTED.
  void connection_lost_();

  void handle_frame_();
  void transmit_next_();
  void finish_(GenibusError error, std::span<const uint8_t> apdus = {});

  [[no_unique_address]] SlotList<GenibusPending, GENIBUS_DEVICE_COUNT> pending_;
  FrameAssembler rx_;
  GenibusDevice *active_{nullptr};  // the device whose request is on the wire
  uint32_t sent_at_{0};
  uint8_t next_slot_{0};  // where the round-robin search for the next request starts
  uint8_t master_address_{DEFAULT_MASTER_ADDRESS};
  uint8_t active_address_{0};
  bool active_replaced_{false};  // the active device has sent a newer request; drop the answer to the old one
};

/// A device on the bus; it gets the answer to its request in on_response().
class GenibusDevice : public PollingComponent, public Parented<GenibusBus> {
 public:
  virtual void on_response(const GenibusResponse &response) = 0;

  void set_address(uint8_t address) { this->address_ = address; }
  uint8_t get_address() const { return this->address_; }

 protected:
  bool request_object_(uint8_t object, uint16_t sub_id);

  uint8_t address_{DEFAULT_DEVICE_ADDRESS};
};

}  // namespace esphome::genibus
