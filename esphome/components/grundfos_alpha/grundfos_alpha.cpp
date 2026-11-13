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
// warned_ holds a bit per object index below these flags
static constexpr uint8_t WARNED_IDENTITY = 0x80;
static constexpr uint8_t WARNED_SETTING = 0x40;
static constexpr uint8_t WARNED_FLAGS = WARNED_IDENTITY | WARNED_SETTING;

// The data model of the Grundfos GO app converts with 1 m = 9804.14 Pa. A real ALPHA3 reports the default and limits
// of its constant pressure set point (86/15) as 29412 (default), 14706 (minimum) and 44118 Pa (maximum): 3, 1.5 and
// 4.5 m at 9804 Pa/m. The alpha3 component uses 10000.
static constexpr float PASCAL_PER_METER = 9804.0f;
static constexpr float SECONDS_PER_HOUR = 3600.0f;

// Object layouts and field names from the data model of the Grundfos GO app for the ALPHA3 Model B. The alpha3
// component reads the same requests, reply headers and the offsets 0, 4, 0, 8, 12 and 20 from a real ALPHA3.
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
    // dc_power_consumption: the value the Grundfos GO app shows as power; alpha3 reads offset 12
    {Reading::READING_POWER, 16, 1.0f},
    {Reading::READING_SPEED, 20, 1.0f},
};
static constexpr ObjectLayout ALPHA3_OBJECTS[] = {
    {93, 289, 0x0130, 1, 24, ALPHA3_PROCESS_FIELDS},
    {87, 69, 0x0100, 1, 37, ALPHA3_MOTOR_FIELDS},
};

static constexpr uint8_t SETTINGS_OBJECT = 86;
// Settings objects of profile 52_1_0, in the order of Setting, with the type, version and size a real ALPHA3 sends.
// Type 0x012F operation status: control_source, operation_mode, control_mode, set_point (float).
static constexpr SettingLayouts ALPHA3_SETTINGS = {{
    {8, 0x012F, 1, 7},
}};

PROGMEM_STRING_TABLE(ModelNames, "ALPHA3 Model B");
// Add a model only after it was tested on a real pump
static constexpr Model MODELS[] = {
    {52, 1, 0, 0, ALPHA3_OBJECTS, &ALPHA3_SETTINGS},
};

// missing_ and warned_ keep a bit per object index
static constexpr bool objects_fit_bits() {
  for (const auto &model : MODELS) {
    if (((1u << model.objects.size()) - 1u) & (WARNED_FLAGS | ~0xFFu))
      return false;
  }
  return true;
}
static_assert(objects_fit_bits(), "A model has more objects than missing_ and warned_ have bits");

#ifdef USE_GRUNDFOS_ALPHA_SETTINGS
#ifdef USE_TEXT_SENSOR
// Byte offsets in the bodies
static constexpr uint8_t OFFSET_SOURCE = 0;          // 0x012F control_source
static constexpr uint8_t OFFSET_OPERATION_MODE = 1;  // 0x012F operation_mode

/// The position of value in values, or values.size() when it is not there.
static size_t index_of(std::span<const uint8_t> values, uint8_t value) {
  size_t i = 0;
  while (i < values.size() && values[i] != value)
    i++;
  return i;
}

// OperationMode of profile 52_1_0; the last name is the fallback
PROGMEM_STRING_TABLE(OperatingModeNames, "Normal", "Stop", "Min", "Max", "User defined", "Forced start", "No command",
                     "Hand", "Unknown");
// The control sources a real ALPHA3 reported: 1 with the settings of the panel, 101 while a clock program ran.
// The data model knows more sources; they show as the fallback until a pump was seen to send them.
static constexpr uint8_t CONTROL_SOURCES[] = {1, 101};
PROGMEM_STRING_TABLE(ControlSourceNames, "Panel", "Clock program", "Unknown");
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
  const bool has_apdu = reader.next(apdu);
  if (!has_apdu || apdu.data_class != CLASS_MEASURED_DATA || apdu.ack != 0 || apdu.data.size() != 3) {
    if (this->first_warning_(WARNED_IDENTITY)) {
      if (!has_apdu) {
        ESP_LOGW(TAG, "No APDU in the identity reply");
      } else {
        ESP_LOGW(TAG, "No unit family, type and version in the reply (class %u, ACK %u, %u bytes)", apdu.data_class,
                 apdu.ack, static_cast<unsigned>(apdu.data.size()));
      }
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
  const bool replied = response.error == GenibusError::GENIBUS_ERROR_NONE && reader.next(apdu);
  const bool parsed = replied && genibus::parse_data_object(apdu, object);
  const bool ok =
      parsed && object.type == layout.type && object.version == layout.version && object.body.size() == layout.size;
  if (replied && genibus::is_object_missing(apdu)) {
    // A firmware without the object: skip it until the next connection instead of asking at every update
    this->missing_ |= 1u << index;
    if (this->first_warning_(1u << index)) {
      ESP_LOGW(TAG, "The pump has no object %u/%u; its sensors stay unknown", layout.object, layout.sub_id);
    }
  } else if (!ok && response.error == GenibusError::GENIBUS_ERROR_NONE && this->first_warning_(1u << index)) {
    // Never read offsets from another version of the object; log only the fields the reply really had
    if (!replied) {
      ESP_LOGW(TAG, "No APDU in the reply to object %u/%u", layout.object, layout.sub_id);
    } else if (!parsed) {
      ESP_LOGW(TAG, "Unexpected reply to object %u/%u: class %u, ACK %u, %u bytes", layout.object, layout.sub_id,
               apdu.data_class, apdu.ack, static_cast<unsigned>(apdu.data.size()));
    } else {
      ESP_LOGW(TAG, "Unexpected reply to object %u/%u: type 0x%04X v%u, %u bytes", layout.object, layout.sub_id,
               object.type, object.version, static_cast<unsigned>(object.body.size()));
    }
  }
  for (const auto &field : layout.fields) {
    float value = NAN;
    if (ok && field.offset + 4u <= object.body.size()) {
      const auto *at = &object.body[field.offset];
      value = bit_cast<float>(encode_uint32(at[0], at[1], at[2], at[3])) * field.factor;
    }
    this->publish_(field.reading, value);
  }
  if (ok)
    this->has_values_ = true;
}

void GrundfosAlpha::request_from_(size_t step) {
  const auto objects = this->model_->objects;
  for (; step < objects.size(); step++) {
    const ObjectLayout &layout = objects[step];
    if ((this->missing_ & (1u << step)) != 0 || !this->wants_(layout))
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
  const bool has_apdu = reader.next(apdu);
  const bool parsed =
      has_apdu && apdu.data_class == genibus::CLASS_DATA_OBJECT && genibus::parse_data_object(apdu, object);
  const bool ok = parsed && object.status == 0 && object.type == layout.type && object.version == layout.version &&
                  object.body.size() == layout.size;
  if (!ok && this->first_warning_(WARNED_SETTING)) {
    // Log only the fields the reply really had
    if (!has_apdu) {
      ESP_LOGW(TAG, "No APDU in the reply to object 86/%u", layout.sub_id);
    } else if (!parsed) {
      ESP_LOGW(TAG, "Unexpected reply to object 86/%u: class %u, ACK %u, %u bytes", layout.sub_id, apdu.data_class,
               apdu.ack, static_cast<unsigned>(apdu.data.size()));
    } else {
      ESP_LOGW(TAG, "Unexpected reply to object 86/%u: status %u, type 0x%04X v%u, %u bytes", layout.sub_id,
               object.status, object.type, object.version, static_cast<unsigned>(object.body.size()));
    }
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
      if (this->control_source_text_sensor_ != nullptr) {
        const size_t source = index_of(CONTROL_SOURCES, body[OFFSET_SOURCE]);
        // The raw value lets a user report a source that has no name yet
        if (source == std::size(CONTROL_SOURCES) && this->control_source_text_sensor_->state != "Unknown") {
          ESP_LOGD(TAG, "Control source %u has no name", body[OFFSET_SOURCE]);
        }
        this->control_source_text_sensor_->publish_state(
            ControlSourceNames::get_progmem_str(source, ControlSourceNames::LAST_INDEX));
      }
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
