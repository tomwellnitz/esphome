#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "esphome/components/genibus/genibus.h"
#include "esphome/core/defines.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome::grundfos_alpha {

enum class Reading : uint8_t {
  READING_FLOW,
  READING_HEAD,
  READING_MEDIUM_TEMPERATURE,
  READING_VOLTAGE,
  READING_CURRENT,
  READING_POWER,
  READING_SPEED,
};

/// A big-endian float at offset in the body of an object, multiplied by factor before it is published.
struct Field {
  Reading reading;
  uint8_t offset;
  float factor;
};

/// A class 10 object as one model sends it. A reply of another type, version or size is not decoded.
struct ObjectLayout {
  uint8_t object;
  uint16_t sub_id;
  uint16_t type;
  uint8_t version;
  uint8_t size;
  std::span<const Field> fields;
};

/// The settings objects of group 86 the entities show, in the order they are polled after the measurements.
enum class Setting : uint8_t {
  SETTING_REALIZED,  // 86/8: the operating mode and control source in effect
  SETTING_COUNT,
};

/// A settings object (object 86) as one model sends it.
struct SettingLayout {
  uint16_t sub_id;
  uint16_t type;
  uint8_t version;
  uint8_t size;
};
using SettingLayouts = std::array<SettingLayout, static_cast<size_t>(Setting::SETTING_COUNT)>;

/// The objects read from a pump of one unit family, type and version; settings is null for a model without them.
struct Model {
  uint8_t family;
  uint8_t type;
  uint8_t version;
  uint8_t name;  // index into ModelNames
  std::span<const ObjectLayout> objects;
  const SettingLayouts *settings;
};

// Values of current_ besides a step of the poll chain
static constexpr uint8_t REQUEST_IDENTITY = 0xFE;
static constexpr uint8_t REQUEST_NONE = 0xFF;

/// A Grundfos ALPHA circulator pump. It reads the pump's unit family, type and version once, then polls the
/// objects of that model: the measurements first, then the settings its entities show. It only reads; it never
/// writes to the pump.
class GrundfosAlpha final : public genibus::GenibusDevice {
#ifdef USE_SENSOR
  SUB_SENSOR(flow)
  SUB_SENSOR(head)
  SUB_SENSOR(medium_temperature)
  SUB_SENSOR(voltage)
  SUB_SENSOR(current)
  SUB_SENSOR(power)
  SUB_SENSOR(speed)
#endif
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
  SUB_TEXT_SENSOR(operating_mode)
  SUB_TEXT_SENSOR(control_source)
#endif

 public:
  void update() override;
  void dump_config() override;
  void on_response(const genibus::GenibusResponse &response) override;
  void on_connected() override {
    this->missing_ = 0;
    this->update();
  }
  void on_disconnected() override;

 protected:
  bool identify_(const genibus::GenibusResponse &response);
  void decode_(uint8_t index, const genibus::GenibusResponse &response);
  /// Send the request of the first wanted step from step on: the measurement objects, then the settings.
  void request_from_(size_t step);
  bool wants_(const ObjectLayout &layout) const;
#ifdef USE_GRUNDFOS_ALPHA_SETTINGS
  bool wants_setting_(Setting setting) const;
  /// The settings object in a reply with status 0, checked against the model's layout.
  bool parse_setting_(Setting setting, const genibus::GenibusResponse &response, genibus::DataObject &object);
  /// Returns whether the reply held a valid object.
  bool decode_setting_(Setting setting, const genibus::GenibusResponse &response);
  void publish_setting_(Setting setting, std::span<const uint8_t> body);
#endif
  void publish_(Reading reading, float value);
  void publish_unknown_();
  bool first_warning_(uint8_t bit);
#ifdef USE_SENSOR
  sensor::Sensor *sensor_(Reading reading) const;
#endif

  const Model *model_{nullptr};
  uint8_t current_{REQUEST_NONE};  // step of the poll chain on the wire, or REQUEST_IDENTITY
  uint8_t warned_{0};
  uint8_t missing_{0};  // a bit per object index the pump does not have
  bool unsupported_{false};
  bool has_values_{false};
};

}  // namespace esphome::grundfos_alpha
