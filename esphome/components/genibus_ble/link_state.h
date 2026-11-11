#pragma once

#include <cstdint>

namespace esphome::genibus_ble {

/// The connection bookkeeping of GenibusBLE, free of ESP-IDF types so it can be tested on the host.
/// The link is up only once notifications of the GENI characteristic are registered. A fault (pairing or GATT setup
/// failed) is reported once per series; the series ends when the link comes up.
class LinkState {
 public:
  void set_handle(uint16_t handle) { this->handle_ = handle; }
  uint16_t handle() const { return this->handle_; }
  bool is_up() const { return this->up_; }
  /// Whether the status warning is the transport's own and not one the bus set.
  bool has_fault() const { return this->fault_; }
  /// Whether handle is the GENI characteristic of this connection.
  bool is_ours(uint16_t handle) const { return this->handle_ != 0 && handle == this->handle_; }
  /// Whether a notification of handle carries GENIbus bytes.
  bool accepts(uint16_t handle) const { return this->up_ && this->is_ours(handle); }

  /// Bring the link up. Returns true when it ends a fault series, so the transport clears its warning.
  bool up() {
    this->up_ = true;
    const bool had_fault = this->fault_;
    this->fault_ = false;
    return had_fault;
  }
  /// Take the link down; DISCONNECT and CLOSE may both call it. Returns true only for the call that ends an up link,
  /// after is_up() has turned false, so the transport may end the bus's requests.
  bool down() {
    this->handle_ = 0;
    if (!this->up_)
      return false;
    this->up_ = false;
    return true;
  }
  /// Record a fault. Returns true for the first fault of a series, the one to warn about.
  bool fault() {
    const bool first = !this->fault_;
    this->fault_ = true;
    return first;
  }

 protected:
  uint16_t handle_{0};
  bool up_{false};
  bool fault_{false};
};

}  // namespace esphome::genibus_ble
