#include "grundfos_alpha.h"

#include <algorithm>
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
    // dc_power_consumption: the app shows it as the power (widget_configuration_52_1_0.xml); alpha3 reads offset 12
    {Reading::READING_POWER, 16, 1.0f},
    {Reading::READING_SPEED, 20, 1.0f},
};
static constexpr ObjectLayout ALPHA3_OBJECTS[] = {
    {93, 289, 0x0130, 1, 24, ALPHA3_PROCESS_FIELDS},
    {87, 69, 0x0100, 1, 37, ALPHA3_MOTOR_FIELDS},
};

static constexpr uint8_t SETTINGS_OBJECT = 86;
// Settings objects of profile 52_1_0, in the order of Setting (Grundfos GO app data; not yet read from a pump).
// Type 0x012F operation status: control_source, operation_mode, control_mode, set_point (float). Type 0x0154
// operation mode configuration: byte 1 local_operation_mode. Type 0x012D factory configuration: default, min, max
// set point (floats), ... Type 0x012E user configuration: feedback_id (u16), local_set_point (float), PID values.
static constexpr SettingLayouts ALPHA3_SETTINGS = {{
    {8, 0x012F, 1, 7},
    {10, 0x012F, 1, 7},
    {100, 0x0154, 1, 4},
    {15, 0x012D, 1, 28},
    {16, 0x012E, 1, 18},
    {17, 0x012D, 1, 28},
    {18, 0x012E, 1, 18},
    {13, 0x012D, 1, 28},
    {14, 0x012E, 1, 18},
    {19, 0x012D, 1, 28},
    {20, 0x012E, 1, 18},
    {21, 0x012D, 1, 28},
    {22, 0x012E, 1, 18},
    {23, 0x012D, 1, 28},
    {24, 0x012E, 1, 18},
}};
static_assert(static_cast<size_t>(Setting::SETTING_COUNT) <= 16, "last_known_ has a bit per setting");

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

static float read_float(const uint8_t *at) { return bit_cast<float>(encode_uint32(at[0], at[1], at[2], at[3])); }

#ifdef USE_GRUNDFOS_ALPHA_SETTINGS
#if defined(USE_TEXT_SENSOR) || defined(USE_GRUNDFOS_ALPHA_WRITE)
// Byte offsets in the bodies
static constexpr uint8_t OFFSET_SOURCE = 0;          // 0x012F control_source
static constexpr uint8_t OFFSET_OPERATION_MODE = 1;  // 0x012F operation_mode, 0x0154 local_operation_mode

/// The position of value in values, or values.size() when it is not there.
static size_t index_of(std::span<const uint8_t> values, uint8_t value) {
  size_t i = 0;
  while (i < values.size() && values[i] != value)
    i++;
  return i;
}
#endif

#ifdef USE_GRUNDFOS_ALPHA_WRITE
static constexpr uint8_t OFFSET_CONTROL_MODE = 2;  // 0x012F and 0x0154: the requested control mode
// The values the app offers for 86/10 byte 2 and 86/100 byte 1, in the order of the select options. 86/100 byte 1 is
// the requested operating mode: the class 3 stop also sets it to Stop (1), and Stop stays after a loss of power.
static constexpr uint8_t CONTROL_MODES[] = {0, 1, 2, 13, 14, 15};
static constexpr uint8_t OPERATING_MODES[] = {0, 1, 2, 3};
// Status byte of a class 10 SET acknowledgement
static constexpr uint8_t SET_STATUS_BUSY = 2;
static constexpr uint8_t SET_STATUS_FAILED = 4;
#endif

#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_NUMBER)
static constexpr uint8_t OFFSET_SET_POINT = 2;  // 0x012E local_set_point
static constexpr uint8_t OFFSET_MIN = 4;        // 0x012D minimum set point
static constexpr uint8_t OFFSET_MAX = 8;        // 0x012D maximum set point

// An AutoAdapt set point: written only in constant pressure, proportional pressure or constant speed (0 to 2)
static constexpr uint8_t OUTSIDE_AUTOADAPT = 0xFF;
static constexpr uint8_t LAST_FIXED_CONTROL_MODE = 2;

/// The settings objects behind a set point entity, its unit in Pa or rpm, and the control mode it is written in.
struct NumberLayout {
  Setting value;
  Setting limits;
  float raw_per_unit;
  uint8_t control_mode;
};
static constexpr NumberLayout NUMBERS[] = {
    {Setting::SETTING_CONSTANT_PRESSURE, Setting::SETTING_CONSTANT_PRESSURE_LIMITS, PASCAL_PER_METER, 0},
    {Setting::SETTING_PROPORTIONAL_PRESSURE, Setting::SETTING_PROPORTIONAL_PRESSURE_LIMITS, PASCAL_PER_METER, 1},
    {Setting::SETTING_CONSTANT_SPEED, Setting::SETTING_CONSTANT_SPEED_LIMITS, 1.0f, 2},
    // The AutoAdapt set points 86/20, 86/22, 86/24. One ALPHA3 mirrored the value of the AutoAdapt mode in effect to
    // 86/6, so a write there acts at once; what it does to the learned state (90/103) was not tested, and the Grundfos
    // app offers no AutoAdapt set point. So they are written only outside the AutoAdapt modes. That this leaves the
    // operation unchanged is assumed from 86/18, which a write in AutoAdapt left without effect.
    // On that pump the three objects differ only in the set point: a late reply of one passes the read check of
    // another. The SET then has the intended bytes; the entity can show the other value until the next poll.
    {Setting::SETTING_AUTOADAPT_RADIATOR, Setting::SETTING_AUTOADAPT_RADIATOR_LIMITS, PASCAL_PER_METER,
     OUTSIDE_AUTOADAPT},
    {Setting::SETTING_AUTOADAPT_UNDERFLOOR, Setting::SETTING_AUTOADAPT_UNDERFLOOR_LIMITS, PASCAL_PER_METER,
     OUTSIDE_AUTOADAPT},
    {Setting::SETTING_AUTOADAPT_RADIATOR_AND_UNDERFLOOR, Setting::SETTING_AUTOADAPT_RADIATOR_AND_UNDERFLOOR_LIMITS,
     PASCAL_PER_METER, OUTSIDE_AUTOADAPT},
};
static_assert(std::size(NUMBERS) == static_cast<size_t>(NumberSetting::NUMBER_SETTING_COUNT));
#endif

#ifdef USE_GRUNDFOS_ALPHA_WRITE
/// The bits of last_known_ for the set point limits, which do not change.
static constexpr uint16_t limit_bits() {
  uint16_t bits = 0;
#ifdef USE_NUMBER
  for (const auto &layout : NUMBERS)
    bits |= 1u << static_cast<size_t>(layout.limits);
#endif
  return bits;
}
static constexpr uint16_t LIMIT_BITS = limit_bits();
#endif

#ifdef USE_TEXT_SENSOR
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
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SELECT)
  LOG_SELECT("  ", "Control mode", this->selects_[0]);
  LOG_SELECT("  ", "Operating mode", this->selects_[1]);
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_NUMBER)
  LOG_NUMBER("  ", "Constant pressure set point", this->numbers_[0]);
  LOG_NUMBER("  ", "Proportional pressure set point", this->numbers_[1]);
  LOG_NUMBER("  ", "Constant speed set point", this->numbers_[2]);
  LOG_NUMBER("  ", "AutoAdapt radiator set point", this->numbers_[3]);
  LOG_NUMBER("  ", "AutoAdapt underfloor heating set point", this->numbers_[4]);
  LOG_NUMBER("  ", "AutoAdapt radiator and underfloor heating set point", this->numbers_[5]);
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_BUTTON)
  LOG_BUTTON("  ", "Start", this->start_button_);
  LOG_BUTTON("  ", "Stop", this->stop_button_);
  LOG_BUTTON("  ", "Reset alarm", this->reset_alarm_button_);
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
#ifdef USE_GRUNDFOS_ALPHA_WRITE
  // A new poll cycle: a late answer to an old request can no longer come
  this->reply_failed_ = false;
  if (this->write_warning_ == WriteWarning::WRITE_WARNING_SET) {
    // The first valid settings read of this cycle clears it; with no settings to read, the new cycle does
    this->write_warning_ = WriteWarning::WRITE_WARNING_CLEAR_ON_READ;
    if (!this->polls_settings_()) {
      this->write_warning_ = WriteWarning::WRITE_WARNING_NONE;
      this->status_clear_warning();
    }
  }
  if (this->start_job_())
    return;
#endif
  this->request_from_(0);
}

void GrundfosAlpha::on_response(const genibus::GenibusResponse &response) {
  const uint8_t current = this->current_;
  this->current_ = REQUEST_NONE;
  // on_disconnected() follows and marks the values unknown
  if (response.error == GenibusError::GENIBUS_ERROR_DISCONNECTED)
    return;
#ifdef USE_GRUNDFOS_ALPHA_WRITE
  this->reply_failed_ = response.error != GenibusError::GENIBUS_ERROR_NONE;
  if (current == REQUEST_WRITE) {
    this->continue_job_(response);
    return;
  }
#endif
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
#ifdef USE_GRUNDFOS_ALPHA_WRITE
  // A write goes before the rest of the poll chain, but not right after a failed reply
  if (!this->reply_failed_ && this->start_job_())
    return;
#endif
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
#ifdef USE_GRUNDFOS_ALPHA_WRITE
  if (this->phase_ != JobPhase::JOB_PHASE_NONE)
    this->end_job_(LOG_STR("connection lost"));
  if (this->queued_ != 0) {
    ESP_LOGW(TAG, "%u waiting writes dropped: connection lost", this->queued_);
    this->queued_ = 0;
    this->write_warning_ = WriteWarning::WRITE_WARNING_SET;
    this->status_set_warning(LOG_STR("Write failed"));
  }
  // The pump may change while away; a write waits for the next poll
  this->last_known_ &= LIMIT_BITS;
#ifdef USE_NUMBER
  for (auto *entity : this->numbers_) {
    if (entity != nullptr && entity->has_state())
      entity->publish_state(NAN);
  }
#endif
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
  const bool replied = response.error == GenibusError::GENIBUS_ERROR_NONE && reader.next(apdu);
  const bool ok = replied && genibus::parse_data_object(apdu, object) && object.type == layout.type &&
                  object.version == layout.version && object.body.size() == layout.size;
  if (replied && genibus::is_object_missing(apdu)) {
    // A firmware without the object: skip it until the next connection instead of asking at every update
    this->missing_ |= 1u << index;
    if (this->first_warning_(1u << index)) {
      ESP_LOGW(TAG, "The pump has no object %u/%u; its sensors stay unknown", layout.object, layout.sub_id);
    }
  } else if (!ok && response.error == GenibusError::GENIBUS_ERROR_NONE && this->first_warning_(1u << index)) {
    // Never read offsets from another version of the object
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
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SELECT)
  if (setting == Setting::SETTING_CONTROL_MODE && this->selects_[0] != nullptr)
    return true;
  if (setting == Setting::SETTING_OPERATING_MODE && this->selects_[1] != nullptr)
    return true;
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_NUMBER)
  for (size_t i = 0; i < std::size(NUMBERS); i++) {
    if (this->numbers_[i] == nullptr)
      continue;
    // A set point depends on the control mode, so that is read too; the limits do not change
    if (setting == Setting::SETTING_CONTROL_MODE || NUMBERS[i].value == setting ||
        (NUMBERS[i].limits == setting && (this->last_known_ & (1u << static_cast<size_t>(setting))) == 0))
      return true;
  }
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
#ifdef USE_GRUNDFOS_ALPHA_WRITE
  const size_t index = static_cast<size_t>(setting);
  if (body.size() <= MAX_WRITE_SIZE)
    std::copy(body.begin(), body.end(), this->last_body_[index].begin());
  this->last_known_ |= 1u << index;
  if (this->write_warning_ == WriteWarning::WRITE_WARNING_CLEAR_ON_READ) {
    this->write_warning_ = WriteWarning::WRITE_WARNING_NONE;
    this->status_clear_warning();
  }
#endif
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
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SELECT)
    case Setting::SETTING_CONTROL_MODE:
    case Setting::SETTING_OPERATING_MODE: {
      const bool control = setting == Setting::SETTING_CONTROL_MODE;
      select::Select *entity = this->selects_[control ? 0 : 1];
      const std::span<const uint8_t> values = control ? std::span<const uint8_t>(CONTROL_MODES) : OPERATING_MODES;
      const size_t option = index_of(values, body[control ? OFFSET_CONTROL_MODE : OFFSET_OPERATION_MODE]);
      if (entity != nullptr && option < values.size())
        entity->publish_state(option);
      break;
    }
#endif
    default:
      break;
  }
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_NUMBER)
  for (size_t i = 0; i < std::size(NUMBERS); i++) {
    if (NUMBERS[i].value == setting) {
      PUBLISH_STATE(this->numbers_[i], read_float(&body[OFFSET_SET_POINT]) / NUMBERS[i].raw_per_unit);
    } else if (NUMBERS[i].limits == setting) {
      this->min_[i] = read_float(&body[OFFSET_MIN]);
      this->max_[i] = read_float(&body[OFFSET_MAX]);
    }
  }
#endif
}
#endif

#ifdef USE_GRUNDFOS_ALPHA_WRITE
#ifdef USE_NUMBER
void GrundfosAlpha::write_number(NumberSetting setting, float value) {
  const size_t i = static_cast<size_t>(setting);
  if (i >= std::size(NUMBERS))
    return;
  const float raw = value * NUMBERS[i].raw_per_unit;
  if ((this->last_known_ & (1u << static_cast<size_t>(NUMBERS[i].limits))) == 0 || std::isnan(this->min_[i]) ||
      std::isnan(this->max_[i])) {
    ESP_LOGW(TAG, "Set point not written: no limits from the pump");
    return;
  }
  // The pump limits a value outside its range without saying so and still acknowledges it
  if (std::isnan(raw) || raw < this->min_[i] || raw > this->max_[i]) {
    ESP_LOGW(TAG, "Set point %.2f not written: outside %.2f to %.2f", value, this->min_[i] / NUMBERS[i].raw_per_unit,
             this->max_[i] / NUMBERS[i].raw_per_unit);
    return;
  }
  this->enqueue_({NUMBERS[i].value, OFFSET_SET_POINT, 0, true, raw, Command::COMMAND_NONE}, LOG_STR("Set point"));
}
#endif

#ifdef USE_SELECT
void GrundfosAlpha::write_select(SelectSetting setting, size_t index) {
  const bool control = setting == SelectSetting::SELECT_SETTING_CONTROL_MODE;
  const std::span<const uint8_t> values = control ? std::span<const uint8_t>(CONTROL_MODES) : OPERATING_MODES;
  if (index >= values.size())
    return;
  this->enqueue_(
      {control ? Setting::SETTING_CONTROL_MODE : Setting::SETTING_OPERATING_MODE,
       control ? OFFSET_CONTROL_MODE : OFFSET_OPERATION_MODE, values[index], false, 0.0f, Command::COMMAND_NONE},
      control ? LOG_STR("Control mode") : LOG_STR("Operating mode"));
}
#endif

bool GrundfosAlpha::enqueue_(const WriteRequest &request, const LogString *what) {
  if (!this->parent_->is_connected() || this->model_ == nullptr || this->model_->settings == nullptr) {
    ESP_LOGW(TAG, "%s not written: pump not connected or not supported", LOG_STR_ARG(what));
    return false;
  }
  size_t slot = 0;
  while (slot < this->queued_ &&
         (this->queue_[slot].setting != request.setting || this->queue_[slot].command != request.command))
    slot++;
  if (slot < this->queued_) {
    ESP_LOGV(TAG, "Replacing a waiting write");
  } else if (this->queued_ == WRITE_QUEUE_SIZE) {
    ESP_LOGW(TAG, "%s not written: too many waiting writes", LOG_STR_ARG(what));
    return false;
  } else {
    this->queued_++;
  }
  this->queue_[slot] = request;
  // Not right after a failed reply: its late answer could be taken for the read of the write
  if (!this->reply_failed_ && !this->parent_->is_busy(this))
    this->start_job_();
  return true;
}

void GrundfosAlpha::send_command(Command command) {
  // Only the commands the app sends; the bus would send any ID
  switch (command) {
    case Command::COMMAND_RESET_ALARM:
    case Command::COMMAND_STOP:
    case Command::COMMAND_START:
      this->enqueue_({Setting::SETTING_COUNT, 0, 0, false, 0.0f, command}, LOG_STR("Command"));
      break;
    default:
      break;
  }
}

bool GrundfosAlpha::start_job_() {
  if (this->phase_ != JobPhase::JOB_PHASE_NONE || this->model_ == nullptr || this->model_->settings == nullptr)
    return false;
  while (this->queued_ != 0) {
    // A write waits until its object was polled; the read before the write is checked against that poll
    if (!this->job_ready_(this->queue_[0]))
      return false;
    this->job_ = this->queue_[0];
    std::copy(this->queue_.begin() + 1, this->queue_.begin() + this->queued_, this->queue_.begin());
    this->queued_--;
    if (this->job_.setting == Setting::SETTING_COUNT) {
      this->phase_ = JobPhase::JOB_PHASE_COMMAND;
      if (!this->send_command_(static_cast<uint8_t>(this->job_.command))) {
        this->end_job_(LOG_STR("not sent"));
        return false;
      }
      this->current_ = REQUEST_WRITE;
      return true;
    }
    this->phase_ = JobPhase::JOB_PHASE_READ;
    // Checked now, not when queued: a control mode written just before counts
    if (const LogString *refusal = this->job_refusal_(); refusal != nullptr) {
      this->end_job_(refusal);
      continue;
    }
    if (!this->request_object_(SETTINGS_OBJECT, this->job_sub_id_())) {
      this->end_job_(LOG_STR("not sent"));
      return false;
    }
    this->current_ = REQUEST_WRITE;
    return true;
  }
  return false;
}

const LogString *GrundfosAlpha::job_refusal_() const {
  constexpr size_t control_mode = static_cast<size_t>(Setting::SETTING_CONTROL_MODE);
  if (this->job_.setting == Setting::SETTING_OPERATING_MODE && (this->last_known_ & (1u << control_mode)) != 0) {
    // 86/100 byte 2 is the requested control mode, as in 86/10 byte 2, in every operating mode: measured on an ALPHA3
    // as 00 03 0F 01 in Max with control mode 15 (86/8 showed control mode 2 in effect) and 00 00 02 01 after a write
    // of control mode 2. It follows a control mode write, but when is not known; until then the SET would write the
    // old control mode back.
    const auto &body = this->last_body_[static_cast<size_t>(Setting::SETTING_OPERATING_MODE)];
    if (body[OFFSET_CONTROL_MODE] != this->last_body_[control_mode][OFFSET_CONTROL_MODE])
      return LOG_STR("the pump has not taken over the control mode yet");
  }
#ifdef USE_NUMBER
  // job_ready_() made sure the control mode was polled
  const uint8_t mode = this->last_body_[control_mode][OFFSET_CONTROL_MODE];
  for (const auto &layout : NUMBERS) {
    if (layout.value != this->job_.setting)
      continue;
    if (layout.control_mode == OUTSIDE_AUTOADAPT && mode > LAST_FIXED_CONTROL_MODE)
      return LOG_STR("AutoAdapt set points are written only outside AutoAdapt");
    if (layout.control_mode != OUTSIDE_AUTOADAPT && mode != layout.control_mode)
      return LOG_STR("not in the control mode of this set point");
  }
#endif
  return nullptr;
}

bool GrundfosAlpha::job_ready_(const WriteRequest &request) const {
  if (request.setting == Setting::SETTING_COUNT)
    return true;  // a command reads nothing first
  uint16_t needed = 1u << static_cast<size_t>(request.setting);
  // The operating mode is checked against the control mode, when that is polled
  if (request.setting == Setting::SETTING_OPERATING_MODE && this->wants_setting_(Setting::SETTING_CONTROL_MODE))
    needed |= 1u << static_cast<size_t>(Setting::SETTING_CONTROL_MODE);
#ifdef USE_NUMBER
  // A set point also needs the control mode
  for (const auto &layout : NUMBERS) {
    if (layout.value == request.setting)
      needed |= 1u << static_cast<size_t>(Setting::SETTING_CONTROL_MODE);
  }
#endif
  return (this->last_known_ & needed) == needed;
}

bool GrundfosAlpha::polls_settings_() const {
  for (size_t i = 0; i < static_cast<size_t>(Setting::SETTING_COUNT); i++) {
    if (this->wants_setting_(static_cast<Setting>(i)))
      return true;
  }
  return false;
}

uint16_t GrundfosAlpha::job_sub_id_() const {
  return (*this->model_->settings)[static_cast<size_t>(this->job_.setting)].sub_id;
}

void GrundfosAlpha::apply_job_() {
  if (this->job_.is_float) {
    put_be32(&this->job_body_[this->job_.offset], bit_cast<uint32_t>(this->job_.float_value));
  } else {
    this->job_body_[this->job_.offset] = this->job_.byte_value;
  }
}

bool GrundfosAlpha::same_outside_value_(std::span<const uint8_t> body) const {
  const size_t width = this->job_.is_float ? 4 : 1;
  for (size_t i = 0; i < body.size(); i++) {
    if ((i < this->job_.offset || i >= this->job_.offset + width) && body[i] != this->job_body_[i])
      return false;
  }
  return true;
}

bool GrundfosAlpha::confirms_job_(std::span<const uint8_t> body) const {
  // The pump stores a set point exactly, but limits it silently: only the same bytes count as written. Of 86/100
  // only the operating mode counts; the other bytes report the pump's state.
  if (this->job_.setting == Setting::SETTING_OPERATING_MODE)
    return body[this->job_.offset] == this->job_body_[this->job_.offset];
  return std::equal(body.begin(), body.end(), this->job_body_.begin());
}

void GrundfosAlpha::continue_job_(const genibus::GenibusResponse &response) {
  const size_t index = static_cast<size_t>(this->job_.setting);
  genibus::DataObject object{};
  switch (this->phase_) {
    case JobPhase::JOB_PHASE_READ: {
      if (!this->parse_setting_(this->job_.setting, response, object) || object.body.size() > MAX_WRITE_SIZE) {
        this->end_job_(LOG_STR("no valid read"));
        break;
      }
      // A late reply to another object of the same type must not be written back: the read has to match the
      // last poll of this object outside the value that changes
      std::copy(this->last_body_[index].begin(), this->last_body_[index].end(), this->job_body_.begin());
      if ((this->last_known_ & (1u << index)) == 0 || !this->same_outside_value_(object.body)) {
        this->end_job_(LOG_STR("object differs from the last read"));
        break;
      }
      std::copy(object.body.begin(), object.body.end(), this->job_body_.begin());
      this->job_size_ = static_cast<uint8_t>(object.body.size());
      this->apply_job_();
      if (std::equal(object.body.begin(), object.body.end(), this->job_body_.begin())) {
        // Already set: spare the pump's memory a write
        this->publish_setting_(this->job_.setting, object.body);
        this->end_job_(nullptr);
        break;
      }
      this->job_type_ = object.type;
      this->job_version_ = object.version;
      this->phase_ = JobPhase::JOB_PHASE_WRITE;
      if (!this->set_object_(SETTINGS_OBJECT, this->job_sub_id_(), this->job_type_, this->job_version_,
                             {this->job_body_.data(), this->job_size_})) {
        this->end_job_(LOG_STR("not sent"));
        break;
      }
      this->current_ = REQUEST_WRITE;
      return;
    }
    case JobPhase::JOB_PHASE_WRITE: {
      // Acknowledgement: class 10, ACK 0, one status byte
      genibus::ApduReader reader(response.apdus);
      genibus::Apdu apdu{};
      if (response.error != GenibusError::GENIBUS_ERROR_NONE) {
        this->end_job_(LOG_STR("no reply"));
        break;
      }
      if (!reader.next(apdu) || apdu.data_class != genibus::CLASS_DATA_OBJECT || apdu.ack != 0 ||
          apdu.data.size() != 1) {
        ESP_LOGV(TAG, "Write reply: class %u, ACK %u, %u bytes", apdu.data_class, apdu.ack,
                 static_cast<unsigned>(apdu.data.size()));
        this->end_job_(LOG_STR("refused"));
        break;
      }
      if (apdu.data[0] != 0) {
        ESP_LOGV(TAG, "Write status %u", apdu.data[0]);
        this->end_job_(apdu.data[0] == SET_STATUS_BUSY     ? LOG_STR("pump busy")
                       : apdu.data[0] == SET_STATUS_FAILED ? LOG_STR("operation failed")
                                                           : LOG_STR("refused"));
        break;
      }
      // The acknowledgement alone proves nothing; read the object back
      this->phase_ = JobPhase::JOB_PHASE_VERIFY;
      if (!this->request_object_(SETTINGS_OBJECT, this->job_sub_id_())) {
        this->end_job_(LOG_STR("read-back not sent"));
        break;
      }
      this->current_ = REQUEST_WRITE;
      return;
    }
    case JobPhase::JOB_PHASE_VERIFY: {
      if (!this->parse_setting_(this->job_.setting, response, object)) {
        this->end_job_(LOG_STR("no valid read-back"));
        break;
      }
      this->publish_setting_(this->job_.setting, object.body);
      this->end_job_(this->confirms_job_(object.body) ? nullptr : LOG_STR("the pump kept another value"));
      break;
    }
    case JobPhase::JOB_PHASE_COMMAND: {
      // A command reply has no data
      genibus::ApduReader reader(response.apdus);
      genibus::Apdu apdu{};
      if (response.error != GenibusError::GENIBUS_ERROR_NONE) {
        this->end_job_(LOG_STR("no reply"));
      } else if (!reader.next(apdu) || apdu.data_class != genibus::CLASS_COMMAND || apdu.ack != 0 ||
                 !apdu.data.empty()) {
        ESP_LOGV(TAG, "Command reply: class %u, ACK %u, %u bytes", apdu.data_class, apdu.ack,
                 static_cast<unsigned>(apdu.data.size()));
        this->end_job_(LOG_STR("refused"));
      } else {
        this->end_job_(nullptr);
      }
      break;
    }
    default:
      return;
  }
  // After a failed reply a late answer may still come; the next update() goes on
  if (!this->parent_->is_connected() || this->reply_failed_)
    return;
  // The next waiting write, then a fresh read of what the entities show
  if (this->start_job_())
    return;
  this->request_from_(this->model_->objects.size());
}

void GrundfosAlpha::end_job_(const LogString *failure) {
  if (this->phase_ == JobPhase::JOB_PHASE_WRITE || this->phase_ == JobPhase::JOB_PHASE_VERIFY ||
      this->phase_ == JobPhase::JOB_PHASE_COMMAND) {
    // A write or command can change other objects too (a control mode write changes 86/100 byte 2): the next write
    // of one of them waits until it was polled again, so its read is not checked against an old poll. A failed job
    // clears its own bit too, even after a fresh read-back; a new write then waits for the next poll.
    uint16_t keep = LIMIT_BITS;
    if (failure == nullptr && this->job_.setting != Setting::SETTING_COUNT)
      keep |= 1u << static_cast<size_t>(this->job_.setting);
    this->last_known_ &= keep;
  }
  this->phase_ = JobPhase::JOB_PHASE_NONE;
  if (failure == nullptr) {
    ESP_LOGV(TAG, "Write done");
    if (this->write_warning_ != WriteWarning::WRITE_WARNING_NONE) {
      this->write_warning_ = WriteWarning::WRITE_WARNING_NONE;
      this->status_clear_warning();
    }
    return;
  }
  if (this->job_.setting == Setting::SETTING_COUNT) {
    ESP_LOGW(TAG, "Command %u failed: %s", static_cast<uint8_t>(this->job_.command), LOG_STR_ARG(failure));
  } else {
    ESP_LOGW(TAG, "Write to 86/%u failed: %s", this->job_sub_id_(), LOG_STR_ARG(failure));
  }
  this->write_warning_ = WriteWarning::WRITE_WARNING_SET;
  this->status_set_warning(LOG_STR("Write failed"));
}
#endif

bool GrundfosAlpha::first_warning_(uint8_t bit) {
  if ((this->warned_ & bit) != 0)
    return false;
  this->warned_ |= bit;
  return true;
}

}  // namespace esphome::grundfos_alpha
