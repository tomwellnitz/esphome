#include "genibus_ble.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include "write_chunks.h"

namespace esphome::genibus_ble {

ESPHOME_LOG_TAG(TAG, "genibus_ble");

// The GENI service and its characteristic 859cffd1-036e-432a-aa28-1a0085b87ba9 (bytes in reverse order)
static const espbt::ESPBTUUID GENI_SERVICE_UUID = espbt::ESPBTUUID::from_uint16(0xFE5D);
static const espbt::ESPBTUUID GENI_CHARACTERISTIC_UUID = espbt::ESPBTUUID::from_raw(
    {0xA9, 0x7B, 0xB8, 0x85, 0x00, 0x1A, 0x28, 0xAA, 0x2A, 0x43, 0x6E, 0x03, 0xD1, 0xFF, 0x9C, 0x85});
// The payload of one write with the default ATT MTU of 23
static constexpr size_t CHUNK_SIZE = 20;

void GenibusBLE::dump_config() {
  ESP_LOGCONFIG(TAG, "GENIbus BLE:\n  Address: %s", this->parent_->address_str());
  GenibusBus::dump_config();
}

void GenibusBLE::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                     esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_CONNECT_EVT: {
      // The pump answers only on an encrypted link
      esp_err_t err = this->parent_->pair();
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "[%s] Starting encryption failed: %s", this->parent_->address_str(), esp_err_to_name(err));
      }
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *chr = this->parent_->get_characteristic(GENI_SERVICE_UUID, GENI_CHARACTERISTIC_UUID);
      if (chr == nullptr) {
        ESP_LOGE(TAG, "[%s] No GENI characteristic, not a GENIbus device", this->parent_->address_str());
        break;
      }
      this->char_handle_ = chr->handle;
      esp_err_t err = this->parent_->register_for_notify(chr->handle);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "[%s] Notify registration failed: %s", this->parent_->address_str(), esp_err_to_name(err));
      }
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (this->char_handle_ == 0 || param->reg_for_notify.handle != this->char_handle_)
        break;
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "[%s] Notify registration failed, status %d", this->parent_->address_str(),
                 param->reg_for_notify.status);
        break;
      }
      this->node_state = espbt::ClientState::ESTABLISHED;
      this->connected_ = true;
      ESP_LOGI(TAG, "[%s] Connected", this->parent_->address_str());
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (this->connected_ && param->notify.handle == this->char_handle_)
        this->receive_bytes({param->notify.value, param->notify.value_len});
      break;
    }
    // A passive disconnect sends DISCONNECT, then CLOSE; an active one may send only CLOSE
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      this->link_down_();
      break;
    default:
      break;
  }
}

void GenibusBLE::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (event != ESP_GAP_BLE_AUTH_CMPL_EVT || !this->parent_->check_addr(param->ble_security.auth_cmpl.bd_addr))
    return;
  if (param->ble_security.auth_cmpl.success) {
    this->status_clear_warning();
    return;
  }
  // The pump was seen and answered, so this is a real fault, unlike a pump without power
  ESP_LOGW(TAG,
           "[%s] Pairing failed, reason %u. Put the pump in pairing mode (ALPHA: hold Connect for 3 s) "
           "and reconnect. If it fails again after each disconnect, check the ESP-IDF version for bonding bugs",
           this->parent_->address_str(), param->ble_security.auth_cmpl.fail_reason);
  this->status_set_warning(LOG_STR("Pairing failed"));
}

bool GenibusBLE::write_frame(std::span<const uint8_t> frame) {
  if (!this->connected_)
    return false;
  return write_in_chunks(frame, CHUNK_SIZE, [this](std::span<const uint8_t> chunk) {
    // The API does not write through the pointer
    esp_err_t err = esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(),
                                             this->char_handle_, chunk.size(), const_cast<uint8_t *>(chunk.data()),
                                             ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "[%s] Write failed: %s", this->parent_->address_str(), esp_err_to_name(err));
    }
    return err == ESP_OK;
  });
}

void GenibusBLE::link_down_() {
  this->char_handle_ = 0;
  if (!this->connected_)
    return;
  // is_connected() must be false before the bus ends the open requests
  this->connected_ = false;
  ESP_LOGI(TAG, "[%s] Disconnected", this->parent_->address_str());
  this->connection_lost_();
}

}  // namespace esphome::genibus_ble

#endif  // USE_ESP32
