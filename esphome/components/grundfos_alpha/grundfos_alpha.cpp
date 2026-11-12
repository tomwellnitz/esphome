#include "grundfos_alpha.h"

#include <cmath>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

namespace esphome::grundfos_alpha {

ESPHOME_LOG_TAG(TAG, "grundfos_alpha");

using genibus::GenibusError;

static constexpr uint8_t CLASS_MEASURED_DATA = 2;
// Class 2 GET of unit_family (148), unit_type (149) and unit_version (150)
static constexpr uint8_t IDENTITY_REQUEST[] = {CLASS_MEASURED_DATA, 0x03, 148, 149, 150};
static constexpr uint8_t WARNED_IDENTITY = 0x80;

// Grundfos GO app 8.5.2, unit_factor_mapping.csv: 1 m = 9804.14 Pa. The ALPHA3 factory limits of the constant
// pressure set point (86/15: 19608, 39216, 58824 Pa) are whole multiples of 9804. The alpha3 component uses 10000.
static constexpr float PASCAL_PER_METER = 9804.0f;
static constexpr float SECONDS_PER_HOUR = 3600.0f;

// Object layouts and field names from the Grundfos GENI profile in the Grundfos GO app 8.5.2
// (ALPHA3 Model B: profile 52_1_0, version V01.11.00.00037). The alpha3 component reads the same requests,
// reply headers and the offsets 0, 4, 0, 8, 12 and 20 from a real ALPHA3.
// 93/289, type 0x0130 v1: pump_flow (m³/s), pump_differential_pressure (Pa), inlet_pressure, outlet_pressure,
// media_temperature, differential_temperature
static constexpr Field ALPHA3_PROCESS_FIELDS[] = {
    {Reading::READING_FLOW, 0, SECONDS_PER_HOUR},
    {Reading::READING_HEAD, 4, 1.0f / PASCAL_PER_METER},
    {Reading::READING_MEDIUM_TEMPERATURE, 16, 1.0f},  // °C, unlike the SI units of the other fields
};
// 87/69, type 0x0100 v1: grid_voltage, dc_link_voltage, current, power_consumption, dc_power_consumption,
// absolute_speed (rpm), three temperatures, pwm_type
static constexpr Field ALPHA3_MOTOR_FIELDS[] = {
    {Reading::READING_VOLTAGE, 0, 1.0f},
    {Reading::READING_CURRENT, 8, 1.0f},
    {Reading::READING_POWER, 12, 1.0f},  // power_consumption, as alpha3; the app shows dc_power_consumption
    {Reading::READING_SPEED, 20, 1.0f},
};
static constexpr ObjectLayout ALPHA3_OBJECTS[] = {
    {93, 289, 0x0130, 1, 24, ALPHA3_PROCESS_FIELDS},
    {87, 69, 0x0100, 1, 37, ALPHA3_MOTOR_FIELDS},
};

static constexpr uint8_t SETTINGS_OBJECT = 86;
// Settings objects of profile 52_1_0, in the order of Setting (Grundfos GO app data; not yet read from a pump).
// Type 0x012F operation status: control_source, operation_mode, control_mode, set_point (float).
static constexpr SettingLayouts ALPHA3_SETTINGS = {{
    {8, 0x012F, 1, 7},
}};

PROGMEM_STRING_TABLE(ModelNames, "ALPHA3 Model B");
// Add a model only after it was tested on a real pump
static constexpr Model MODELS[] = {
    {52, 1, 0, 0, ALPHA3_OBJECTS, &ALPHA3_SETTINGS},
};

static float read_float(const uint8_t *at) { return bit_cast<float>(encode_uint32(at[0], at[1], at[2], at[3])); }

#ifdef USE_GRUNDFOS_ALPHA_SETTINGS
static constexpr uint8_t WARNED_SETTING = 0x40;

#ifdef USE_TEXT_SENSOR
// Byte offsets in a type 0x012F body
static constexpr uint8_t OFFSET_SOURCE = 0;
static constexpr uint8_t OFFSET_OPERATION_MODE = 1;

/// The position of value in values, or values.size() when it is not there.
static size_t index_of(std::span<const uint8_t> values, uint8_t value) {
  size_t i = 0;
  while (i < values.size() && values[i] != value)
    i++;
  return i;
}

// OperationMode and ControlSource of profile 52_1_0; the last name is the fallback
PROGMEM_STRING_TABLE(OperatingModeNames, "Normal", "Stop", "Min", "Max", "User defined", "Forced start", "No command",
                     "Hand", "Unknown");
static constexpr uint8_t CONTROL_SOURCES[] = {0, 1, 2, 3, 4, 5, 101, 104, 111};
PROGMEM_STRING_TABLE(ControlSourceNames, "Undefined", "Panel", "Bus", "Link", "External input", "Off button",
                     "Clock program", "Automatic night setback", "Air venting", "Unknown");
#endif
#endif

void GrundfosAlpha::dump_config() {
  ESP_LOGCONFIG(TAG, "Grundfos ALPHA:\n  Address: 0x%02X", this->address_);
  LOG_UPDATE_INTERVAL(this);
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Flow", this->flow_sensor_);
  LOG_SENSOR("  ", "Head", this->head_sensor_);
  LOG_SENSOR("  ", "Medium temperature", this->medium_temperature_sensor_);
  LOG_SENSOR("  ", "Voltage", this->voltage_sensor_);
  LOG_SENSOR("  ", "Current", this->current_sensor_);
  LOG_SENSOR("  ", "Power", this->power_sensor_);
  LOG_SENSOR("  ", "Speed", this->speed_sensor_);
#endif
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
  LOG_TEXT_SENSOR("  ", "Operating mode", this->operating_mode_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Control source", this->control_source_text_sensor_);
#endif
}

void GrundfosAlpha::update() {
  // A pump without power is normal: skip quietly and show the values as unknown
  if (!this->parent_->is_connected()) {
    this->publish_unknown_();
    return;
  }
  if (this->unsupported_ || this->parent_->is_busy(this))
    return;
  if (this->model_ == nullptr) {
    if (this->parent_->send(this, IDENTITY_REQUEST))
      this->current_ = REQUEST_IDENTITY;
    return;
  }
  this->request_from_(0);
}

void GrundfosAlpha::on_response(const genibus::GenibusResponse &response) {
  const uint8_t current = this->current_;
  this->current_ = REQUEST_NONE;
  // on_disconnected() follows and marks the values unknown
  if (response.error == GenibusError::GENIBUS_ERROR_DISCONNECTED)
    return;
  if (current == REQUEST_IDENTITY) {
    if (this->identify_(response))
      this->request_from_(0);
    return;
  }
  if (this->model_ == nullptr)
    return;
  const size_t objects = this->model_->objects.size();
  if (current < objects) {
    this->decode_(current, response);
#ifdef USE_GRUNDFOS_ALPHA_SETTINGS
  } else if (this->model_->settings != nullptr && current < objects + static_cast<size_t>(Setting::SETTING_COUNT)) {
    this->decode_setting_(static_cast<Setting>(current - objects), response);
#endif
  } else {
    return;
  }
  this->request_from_(current + 1);
}

void GrundfosAlpha::on_disconnected() {
  this->publish_unknown_();
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
  // Empty, as ble_client text sensors show a lost connection
  for (auto *text_sensor : {this->operating_mode_text_sensor_, this->control_source_text_sensor_}) {
    if (text_sensor != nullptr && text_sensor->has_state())
      text_sensor->publish_state("");
  }
#endif
}

bool GrundfosAlpha::identify_(const genibus::GenibusResponse &response) {
  // The bus has logged a timeout or a broken frame; try again at the next update
  if (response.error != GenibusError::GENIBUS_ERROR_NONE)
    return false;
  genibus::ApduReader reader(response.apdus);
  genibus::Apdu apdu{};
  if (!reader.next(apdu) || apdu.data_class != CLASS_MEASURED_DATA || apdu.ack != 0 || apdu.data.size() != 3) {
    if (this->first_warning_(WARNED_IDENTITY)) {
      ESP_LOGW(TAG, "No unit family, type and version in the reply (class %u, ACK %u)", apdu.data_class, apdu.ack);
    }
    this->status_set_warning(LOG_STR("No pump identity"));
    return false;
  }
  const uint8_t family = apdu.data[0];
  const uint8_t type = apdu.data[1];
  const uint8_t version = apdu.data[2];
  for (const auto &model : MODELS) {
    if (model.family == family && model.type == type && model.version == version) {
      this->model_ = &model;
      this->status_clear_warning();
      ESP_LOGI(TAG, "Found %s", LOG_STR_ARG(ModelNames::get_log_str(model.name, 0)));
      return true;
    }
  }
  this->unsupported_ = true;
  this->status_set_warning(LOG_STR("Model not supported"));
  ESP_LOGW(TAG, "Model %u/%u/%u not supported", family, type, version);
  return false;
}

void GrundfosAlpha::decode_(uint8_t index, const genibus::GenibusResponse &response) {
  const ObjectLayout &layout = this->model_->objects[index];
  genibus::ApduReader reader(response.apdus);
  genibus::Apdu apdu{};
  genibus::DataObject object{};
  const bool ok = response.error == GenibusError::GENIBUS_ERROR_NONE && reader.next(apdu) &&
                  genibus::parse_data_object(apdu, object) && object.type == layout.type &&
                  object.version == layout.version && object.body.size() == layout.size;
  // Never read offsets from another version of the object
  if (!ok && response.error == GenibusError::GENIBUS_ERROR_NONE && this->first_warning_(1u << index)) {
    ESP_LOGW(TAG, "Unexpected reply to object %u/%u: class %u, ACK %u, type 0x%04X v%u, %u bytes", layout.object,
             layout.sub_id, apdu.data_class, apdu.ack, object.type, object.version,
             static_cast<unsigned>(object.body.size()));
  }
  for (const auto &field : layout.fields) {
    float value = NAN;
    if (ok && field.offset + 4u <= object.body.size())
      value = read_float(&object.body[field.offset]) * field.factor;
    this->publish_(field.reading, value);
  }
  if (ok)
    this->has_values_ = true;
}

void GrundfosAlpha::request_from_(size_t step) {
  const auto objects = this->model_->objects;
  for (; step < objects.size(); step++) {
    const ObjectLayout &layout = objects[step];
    if (!this->wants_(layout))
      continue;
    if (this->request_object_(layout.object, layout.sub_id))
      this->current_ = static_cast<uint8_t>(step);
    return;
  }
#ifdef USE_GRUNDFOS_ALPHA_SETTINGS
  if (this->model_->settings == nullptr)
    return;
  for (size_t i = step - objects.size(); i < static_cast<size_t>(Setting::SETTING_COUNT); i++) {
    if (!this->wants_setting_(static_cast<Setting>(i)))
      continue;
    if (this->request_object_(SETTINGS_OBJECT, (*this->model_->settings)[i].sub_id))
      this->current_ = static_cast<uint8_t>(objects.size() + i);
    return;
  }
#endif
}

bool GrundfosAlpha::wants_(const ObjectLayout &layout) const {
#ifdef USE_SENSOR
  for (const auto &field : layout.fields) {
    if (this->sensor_(field.reading) != nullptr)
      return true;
  }
#endif
  return false;
}

void GrundfosAlpha::publish_(Reading reading, float value) {
#ifdef USE_SENSOR
  PUBLISH_STATE(this->sensor_(reading), value);
#endif
}

#ifdef USE_SENSOR
sensor::Sensor *GrundfosAlpha::sensor_(Reading reading) const {
  switch (reading) {
    case Reading::READING_FLOW:
      return this->flow_sensor_;
    case Reading::READING_HEAD:
      return this->head_sensor_;
    case Reading::READING_MEDIUM_TEMPERATURE:
      return this->medium_temperature_sensor_;
    case Reading::READING_VOLTAGE:
      return this->voltage_sensor_;
    case Reading::READING_CURRENT:
      return this->current_sensor_;
    case Reading::READING_POWER:
      return this->power_sensor_;
    case Reading::READING_SPEED:
      return this->speed_sensor_;
  }
  return nullptr;
}
#endif

void GrundfosAlpha::publish_unknown_() {
  if (!this->has_values_)
    return;
  this->has_values_ = false;
  if (this->model_ == nullptr)
    return;
  for (const auto &layout : this->model_->objects) {
    for (const auto &field : layout.fields)
      this->publish_(field.reading, NAN);
  }
}

#ifdef USE_GRUNDFOS_ALPHA_SETTINGS
bool GrundfosAlpha::wants_setting_(Setting setting) const {
#ifdef USE_TEXT_SENSOR
  if (setting == Setting::SETTING_REALIZED)
    return this->operating_mode_text_sensor_ != nullptr || this->control_source_text_sensor_ != nullptr;
#endif
  return false;
}

bool GrundfosAlpha::parse_setting_(Setting setting, const genibus::GenibusResponse &response,
                                   genibus::DataObject &object) {
  if (response.error != GenibusError::GENIBUS_ERROR_NONE)
    return false;
  genibus::ApduReader reader(response.apdus);
  genibus::Apdu apdu{};
  const SettingLayout &layout = (*this->model_->settings)[static_cast<size_t>(setting)];
  const bool ok = reader.next(apdu) && apdu.data_class == genibus::CLASS_DATA_OBJECT &&
                  genibus::parse_data_object(apdu, object) && object.status == 0 && object.type == layout.type &&
                  object.version == layout.version && object.body.size() == layout.size;
  if (!ok && this->first_warning_(WARNED_SETTING)) {
    ESP_LOGW(TAG, "Unexpected reply to object 86/%u: class %u, ACK %u, status %u, type 0x%04X v%u, %u bytes",
             layout.sub_id, apdu.data_class, apdu.ack, object.status, object.type, object.version,
             static_cast<unsigned>(object.body.size()));
  }
  return ok;
}

bool GrundfosAlpha::decode_setting_(Setting setting, const genibus::GenibusResponse &response) {
  genibus::DataObject object{};
  if (!this->parse_setting_(setting, response, object))
    return false;
  this->publish_setting_(setting, object.body);
  return true;
}

void GrundfosAlpha::publish_setting_(Setting setting, std::span<const uint8_t> body) {
  switch (setting) {
    case Setting::SETTING_REALIZED:
#ifdef USE_TEXT_SENSOR
      PUBLISH_STATE(this->operating_mode_text_sensor_,
                    OperatingModeNames::get_progmem_str(body[OFFSET_OPERATION_MODE], OperatingModeNames::LAST_INDEX));
      PUBLISH_STATE(this->control_source_text_sensor_,
                    ControlSourceNames::get_progmem_str(index_of(CONTROL_SOURCES, body[OFFSET_SOURCE]),
                                                        ControlSourceNames::LAST_INDEX));
#endif
      break;
    default:
      break;
  }
}
#endif

bool GrundfosAlpha::first_warning_(uint8_t bit) {
  if ((this->warned_ & bit) != 0)
    return false;
  this->warned_ |= bit;
  return true;
}

}  // namespace esphome::grundfos_alpha
