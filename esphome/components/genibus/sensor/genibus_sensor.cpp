#include "genibus_sensor.h"

#include <cmath>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::genibus {

ESPHOME_LOG_TAG(TAG, "genibus.sensor");

uint8_t value_size(ValueType type) {
  switch (type) {
    case ValueType::VALUE_TYPE_UINT8:
    case ValueType::VALUE_TYPE_INT8:
      return 1;
    case ValueType::VALUE_TYPE_UINT16:
    case ValueType::VALUE_TYPE_INT16:
      return 2;
    case ValueType::VALUE_TYPE_DOUBLE:
      return 8;
    default:
      return 4;
  }
}

float read_value(std::span<const uint8_t> body, uint8_t offset, ValueType type) {
  if (static_cast<size_t>(offset) + value_size(type) > body.size())
    return NAN;
  const uint8_t *at = &body[offset];
  switch (type) {
    case ValueType::VALUE_TYPE_UINT8:
      return at[0];
    case ValueType::VALUE_TYPE_UINT16:
      return encode_value<uint16_t>(at);
    case ValueType::VALUE_TYPE_UINT32:
      return encode_value<uint32_t>(at);
    case ValueType::VALUE_TYPE_INT8:
      return static_cast<int8_t>(at[0]);
    case ValueType::VALUE_TYPE_INT16:
      return static_cast<int16_t>(encode_value<uint16_t>(at));
    case ValueType::VALUE_TYPE_INT32:
      return static_cast<int32_t>(encode_value<uint32_t>(at));
    case ValueType::VALUE_TYPE_FLOAT:
      return bit_cast<float>(encode_value<uint32_t>(at));
    case ValueType::VALUE_TYPE_DOUBLE:
      return static_cast<float>(bit_cast<double>(encode_value<uint64_t>(at)));
  }
  return NAN;
}

bool parse_object_prefix(const Apdu &apdu, DataObject &object) {
  const auto &data = apdu.data;
  if (apdu.data_class != CLASS_DATA_OBJECT || apdu.ack != 0 || data.size() < DATA_OBJECT_HEADER_SIZE)
    return false;
  // A body longer than the size in the header is a broken reply
  if (encode_uint24(data[4], data[5], data[6]) < data.size() - DATA_OBJECT_HEADER_SIZE)
    return false;
  object.status = data[0];
  object.type = encode_uint16(data[1], data[2]);
  object.version = data[3];
  object.body = data.subspan(DATA_OBJECT_HEADER_SIZE);
  return true;
}

void GenibusSensor::update() {
  if (this->missing_ || !this->parent_->is_connected() || this->parent_->is_busy(this))
    return;
  this->request_object_(this->object_, this->sub_id_);
}

void GenibusSensor::dump_config() {
  LOG_SENSOR("", "GENIbus Sensor", this);
  ESP_LOGCONFIG(TAG,
                "  Address: 0x%02X\n"
                "  Object: %u/%u\n"
                "  Offset: %u\n"
                "  Expected type: 0x%04X\n"
                "  Expected version: %u",
                this->address_, this->object_, this->sub_id_, this->offset_, this->expected_type_,
                this->expected_version_);
  LOG_UPDATE_INTERVAL(this);
}

void GenibusSensor::on_response(const GenibusResponse &response) {
  // on_disconnected() follows and marks the value unknown
  if (response.error == GenibusError::GENIBUS_ERROR_DISCONNECTED)
    return;
  this->publish_state(this->decode_(response));
}

float GenibusSensor::decode_(const GenibusResponse &response) {
  // The bus logs timeouts and broken frames
  if (response.error != GenibusError::GENIBUS_ERROR_NONE) {
    this->missing_replies_ = 0;
    return NAN;
  }
  ApduReader reader(response.apdus);
  Apdu apdu{};
  DataObject object{};
  if (reader.next(apdu) && is_object_missing(apdu)) {
    if (++this->missing_replies_ >= 2) {
      this->missing_ = true;
      if (!this->warned_missing_) {
        ESP_LOGW(TAG, "Device 0x%02X has no object %u/%u", this->address_, this->object_, this->sub_id_);
        this->warned_missing_ = true;
      }
    }
    return NAN;
  }
  this->missing_replies_ = 0;
  // Never read the offset from an object with a status, from another type or version of it, or past the bytes it sent
  const bool parsed = parse_object_prefix(apdu, object);
  if (parsed && object.status == 0 && object.type == this->expected_type_ &&
      object.version == this->expected_version_ && this->offset_ + value_size(this->type_) <= object.body.size())
    return read_value(object.body, this->offset_, this->type_);
  if (!this->warned_unexpected_) {
    if (parsed) {
      ESP_LOGW(TAG, "Unexpected reply to object %u/%u: status %u, type 0x%04X v%u, %u bytes", this->object_,
               this->sub_id_, object.status, object.type, object.version, static_cast<unsigned>(object.body.size()));
    } else {
      ESP_LOGW(TAG, "Unexpected reply to object %u/%u: class %u, ACK %u, %u bytes", this->object_, this->sub_id_,
               apdu.data_class, apdu.ack, static_cast<unsigned>(apdu.data.size()));
    }
    this->warned_unexpected_ = true;
  }
  return NAN;
}

void GenibusSensor::on_disconnected() {
  // The device may have a new firmware when it comes back
  this->missing_ = false;
  this->missing_replies_ = 0;
  if (this->has_state())
    this->publish_state(NAN);
}

}  // namespace esphome::genibus
