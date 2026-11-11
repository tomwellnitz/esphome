#include "genibus.h"

#include <algorithm>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

namespace esphome::genibus {

ESPHOME_LOG_TAG(TAG, "genibus");

static constexpr size_t MAX_LOG_BYTES = 64;

// GenibusError, in order
PROGMEM_STRING_TABLE(GenibusErrorStrings, "none", "no answer", "CRC error", "malformed answer", "write failed",
                     "disconnected");

void GenibusBus::dump_config() {
  ESP_LOGCONFIG(TAG,
                "GENIbus:\n"
                "  Master address: 0x%02X",
                this->master_address_);
}

bool GenibusBus::send(GenibusDevice *device, std::span<const uint8_t> apdus) {
  if (apdus.empty() || apdus.size() > MAX_REQUEST_SIZE) {
    ESP_LOGW(TAG, "Request of %u bytes not sent", static_cast<unsigned>(apdus.size()));
    return false;
  }
  if (!this->is_connected())
    return false;
  for (auto &pending : this->pending_) {
    if (pending.device != device)
      continue;
    if (pending.size != 0 || this->active_ == device) {
      ESP_LOGV(TAG, "Request to 0x%02X replaced", device->get_address());
    }
    this->active_replaced_ = this->active_replaced_ || this->active_ == device;
    std::copy(apdus.begin(), apdus.end(), pending.apdus.begin());
    pending.size = static_cast<uint8_t>(apdus.size());
    this->enable_loop();
    return true;
  }
  ESP_LOGE(TAG, "Device 0x%02X is not registered", device->get_address());
  return false;
}

void GenibusBus::loop() {
  if (this->active_ != nullptr) {
    if (App.get_loop_component_start_time() - this->sent_at_ < RESPONSE_TIMEOUT_MS)
      return;
    this->rx_.reset();
    this->finish_(GenibusError::GENIBUS_ERROR_TIMEOUT);
  }
  this->transmit_next_();
}

void GenibusBus::transmit_next_() {
  if (!this->is_connected()) {
    this->connection_lost_();
    this->disable_loop();
    return;
  }
  const size_t count = this->pending_.size();
  for (size_t i = 0; i < count; i++) {
    const size_t slot = (this->next_slot_ + i) % count;
    auto &pending = this->pending_[slot];
    if (pending.size == 0)
      continue;
    std::array<uint8_t, MAX_REQUEST_SIZE + 2 + FRAME_OVERHEAD> frame;
    const size_t size = build_request(frame, pending.device->get_address(), this->master_address_,
                                      {pending.apdus.data(), pending.size});
    pending.size = 0;
    this->next_slot_ = static_cast<uint8_t>((slot + 1) % count);
    this->active_ = pending.device;
    this->active_address_ = pending.device->get_address();
    this->active_replaced_ = false;
    this->sent_at_ = App.get_loop_component_start_time();
    this->rx_.reset();
    ESP_LOGV_HEX(TAG, frame.data(), size, MAX_LOG_BYTES, "TX %s");
    if (!this->write_frame({frame.data(), size}))
      this->finish_(GenibusError::GENIBUS_ERROR_WRITE_FAILED);
    return;
  }
  this->disable_loop();
}

void GenibusBus::receive_bytes(std::span<const uint8_t> data) {
  for (uint8_t byte : data) {
    if (this->rx_.feed(byte)) {
      this->handle_frame_();
      this->rx_.reset();
    }
  }
}

void GenibusBus::handle_frame_() {
  const auto frame = this->rx_.frame();
  ESP_LOGV_HEX(TAG, frame.data(), frame.size(), MAX_LOG_BYTES, "RX %s");
  if (this->active_ == nullptr) {
    ESP_LOGV(TAG, "Frame without request dropped");
    return;
  }
  if (!this->rx_.crc_ok()) {
    this->finish_(GenibusError::GENIBUS_ERROR_CRC);
    return;
  }
  // Another node's traffic on a shared bus, or our own echo
  if (frame[0] != START_REPLY || frame[2] != this->master_address_ || frame[3] != this->active_address_)
    return;
  ApduReader reader(this->rx_.apdus());
  Apdu apdu;
  while (reader.next(apdu)) {
  }
  if (!reader.at_end()) {
    this->finish_(GenibusError::GENIBUS_ERROR_MALFORMED);
    return;
  }
  this->finish_(GenibusError::GENIBUS_ERROR_NONE, this->rx_.apdus());
}

void GenibusBus::finish_(GenibusError error, std::span<const uint8_t> apdus) {
  GenibusDevice *device = this->active_;
  const bool replaced = this->active_replaced_;
  this->active_ = nullptr;
  this->active_replaced_ = false;
  this->enable_loop();
  // A lost connection is the transport's to report
  if (error != GenibusError::GENIBUS_ERROR_DISCONNECTED &&
      !this->status_comm_result(error == GenibusError::GENIBUS_ERROR_NONE)) {
    ESP_LOGW(TAG, "Request to 0x%02X: %s", this->active_address_,
             LOG_STR_ARG(GenibusErrorStrings::get_log_str(static_cast<uint8_t>(error), 0)));
  }
  if (device != nullptr && !replaced)
    device->on_response({error, apdus});
}

void GenibusBus::connection_lost_() {
  this->rx_.reset();
  if (this->active_ != nullptr)
    this->finish_(GenibusError::GENIBUS_ERROR_DISCONNECTED);
  for (auto &pending : this->pending_) {
    if (pending.size == 0)
      continue;
    pending.size = 0;
    pending.device->on_response({GenibusError::GENIBUS_ERROR_DISCONNECTED, {}});
  }
}

bool GenibusDevice::request_object_(uint8_t object, uint16_t sub_id) {
  uint8_t apdu[5];
  put_get_object(apdu, object, sub_id);
  return this->parent_->send(this, apdu);
}

}  // namespace esphome::genibus
