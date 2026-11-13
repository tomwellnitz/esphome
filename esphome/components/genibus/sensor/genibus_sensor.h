#pragma once

#include <cstdint>
#include <span>

#include "esphome/components/genibus/genibus.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::genibus {

enum class ValueType : uint8_t {
  VALUE_TYPE_UINT8,
  VALUE_TYPE_UINT16,
  VALUE_TYPE_UINT32,
  VALUE_TYPE_INT8,
  VALUE_TYPE_INT16,
  VALUE_TYPE_INT32,
  VALUE_TYPE_FLOAT,
  VALUE_TYPE_DOUBLE,
};

/// Size in bytes of a value of type.
uint8_t value_size(ValueType type);
/// Read the big-endian value of type at offset of body; NAN when it does not fit into body.
float read_value(std::span<const uint8_t> body, uint8_t offset, ValueType type);

/// Read the data object in a class 10 reply. Unlike parse_data_object() it also takes a body that the device cut
/// short: an ALPHA3 sends at most 46 body bytes of a larger object and keeps the full size in the header.
bool parse_object_prefix(const Apdu &apdu, DataObject &object);

/// Draft: one value at a fixed offset of a class 10 data object, read at every update. It never writes.
/// A reply names no object, so only a reply with the expected type and version is decoded.
class GenibusSensor final : public sensor::Sensor, public GenibusDevice {
 public:
  GenibusSensor(uint8_t object, uint16_t sub_id, uint8_t offset, ValueType type, uint16_t expected_type,
                uint8_t expected_version)
      : sub_id_(sub_id),
        expected_type_(expected_type),
        object_(object),
        offset_(offset),
        type_(type),
        expected_version_(expected_version) {}

  void update() override;
  void dump_config() override;
  void on_response(const GenibusResponse &response) override;
  void on_connected() override { this->update(); }
  void on_disconnected() override;

 protected:
  float decode_(const GenibusResponse &response);

  uint16_t sub_id_;
  uint16_t expected_type_;
  uint8_t object_;
  uint8_t offset_;
  ValueType type_;
  uint8_t expected_version_;
  // Replies in a row that say the object is missing; one alone may be a late reply to another device
  uint8_t missing_replies_{0};
  bool missing_{false};  // the device has no such object; asked again after the next connection
  bool warned_missing_{false};
  bool warned_unexpected_{false};
};

}  // namespace esphome::genibus
