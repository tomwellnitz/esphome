#include "grundfos_alpha.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdlib>

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/progmem.h"

namespace esphome::grundfos_alpha {

ESPHOME_LOG_TAG(TAG, "grundfos_alpha");

using genibus::GenibusError;

static constexpr uint8_t CLASS_MEASURED_DATA = 2;
static constexpr uint8_t CLASS_REFERENCE = 5;
// Class 2 GET of unit_family (148), unit_type (149) and unit_version (150)
static constexpr uint8_t IDENTITY_REQUEST[] = {CLASS_MEASURED_DATA, 0x03, 148, 149, 150};
// warned_ holds a bit per object index below these flags
static constexpr uint8_t WARNED_IDENTITY = 0x80;
static constexpr uint8_t WARNED_SETTING = 0x40;
static constexpr uint8_t WARNED_INFO = 0x20;
static constexpr uint8_t WARNED_FLAGS = WARNED_IDENTITY | WARNED_SETTING | WARNED_INFO;

// Grundfos GO app 8.5.2, unit_factor_mapping.csv: 1 m = 9804.14 Pa. The ALPHA3 factory limits of the constant
// pressure set point (86/15: 19608, 39216, 58824 Pa) are whole multiples of 9804. The alpha3 component uses 10000.
static constexpr float PASCAL_PER_METER = 9804.0f;
static constexpr float SECONDS_PER_HOUR = 3600.0f;
static constexpr float WATT_SECONDS_PER_KILOWATT_HOUR = 3.6e6f;

// Object layouts and field names from the Grundfos GENI profile in the Grundfos GO app 8.5.2
// (ALPHA3 Model B: profile 52_1_0, version V01.11.00.00037). The alpha3 component reads the same requests,
// reply headers and the offsets 0, 4, 0, 8, 12 and 20 from a real ALPHA3.
// 93/289, type 0x0130 v1: pump_flow (m³/s), pump_differential_pressure (Pa), inlet_pressure, outlet_pressure,
// media_temperature, differential_temperature
static constexpr Field ALPHA3_PROCESS_FIELDS[] = {
    {Reading::READING_FLOW, 0, FieldFormat::FIELD_FORMAT_FLOAT, SECONDS_PER_HOUR},
    {Reading::READING_HEAD, 4, FieldFormat::FIELD_FORMAT_FLOAT, 1.0f / PASCAL_PER_METER},
    // °C, unlike the SI units of the other fields
    {Reading::READING_MEDIUM_TEMPERATURE, 16, FieldFormat::FIELD_FORMAT_FLOAT, 1.0f},
};
// 87/69, type 0x0100 v1: grid_voltage, dc_link_voltage, current, power_consumption, dc_power_consumption,
// absolute_speed (rpm), three temperatures, pwm_type
static constexpr Field ALPHA3_MOTOR_FIELDS[] = {
    {Reading::READING_VOLTAGE, 0, FieldFormat::FIELD_FORMAT_FLOAT, 1.0f},
    {Reading::READING_CURRENT, 8, FieldFormat::FIELD_FORMAT_FLOAT, 1.0f},
    // dc_power_consumption: the app shows it as the power (widget_configuration_52_1_0.xml); alpha3 reads offset 12
    {Reading::READING_POWER, 16, FieldFormat::FIELD_FORMAT_FLOAT, 1.0f},
    {Reading::READING_SPEED, 20, FieldFormat::FIELD_FORMAT_FLOAT, 1.0f},
};
// 87/1, type 0x00E8 v1: system_energy (double, Ws), system_power, then a counted list of one entry per pump.
// Whether system_energy also falls back after a power cut is not known yet; it is held like 93/1 (see publish_).
static constexpr Field ALPHA3_ENERGY_FIELDS[] = {
    {Reading::READING_ENERGY, 0, FieldFormat::FIELD_FORMAT_DOUBLE, 1.0f / WATT_SECONDS_PER_KILOWATT_HOUR},
};
// 93/1, type 0x00F8 v2: starts (u32), starts_last_hour (u16), starts_last24hrs (u16), operating_time (u32, s, total),
// operating_time_last_24hrs, last_operating_time, power_up_counter, power_on_time. The pump stores starts and
// operating_time only now and then: after a power cut a real ALPHA3 reported one start and 3637 s less than before.
static constexpr Field ALPHA3_HISTORY_FIELDS[] = {
    {Reading::READING_OPERATING_HOURS, 8, FieldFormat::FIELD_FORMAT_UINT32, 1.0f / SECONDS_PER_HOUR},
    {Reading::READING_STARTS, 0, FieldFormat::FIELD_FORMAT_UINT32, 1.0f},
};
// 88/25, type 0x010A v1: warn_code, alarm_code, highest_prioritized_error_code (u16 each, 0 = no fault)
static constexpr Field ALPHA3_FAULT_FIELDS[] = {
    {Reading::READING_WARNING_CODE, 0, FieldFormat::FIELD_FORMAT_UINT16, 1.0f},
    {Reading::READING_ALARM_CODE, 2, FieldFormat::FIELD_FORMAT_UINT16, 1.0f},
};
static constexpr ObjectLayout ALPHA3_OBJECTS[] = {
    {93, 289, 0x0130, 1, 24, ALPHA3_PROCESS_FIELDS},  // process values
    {87, 69, 0x0100, 1, 37, ALPHA3_MOTOR_FIELDS},     // motor values
    {87, 1, 0x00E8, 1, 26, ALPHA3_ENERGY_FIELDS},     // energy; 26 bytes: one pump in the list
    {93, 1, 0x00F8, 2, 28, ALPHA3_HISTORY_FIELDS},    // operation history
    {88, 25, 0x010A, 1, 6, ALPHA3_FAULT_FIELDS},      // fault status
};

// Settings objects of profile 52_1_0, in the order of Setting (Grundfos GO app data; the types, versions and sizes
// match the replies of a real ALPHA3).
// Type 0x012F operation status: control_source, operation_mode, control_mode, set_point (float). Type 0x0154
// operation mode configuration: byte 1 local_operation_mode. Type 0x012D factory configuration: default, min, max
// set point (floats), ... Type 0x012E user configuration: feedback_id (u16), local_set_point (float), PID values.
// Type 0x014D AutoNight enable: 0 off, 1 on.
// Type 0x00E5 automatic test run configuration: enabled (bit 0), time_without_activity (u32, s),
// duration_of_testrun_in_secs (u16). Type 0x00DA clock program overview: four maximum counts, clock_program_enabled
// (byte 4, bit 0), default_action, base_set_point (float).
static constexpr SettingLayouts ALPHA3_SETTINGS = {{
    {86, 8, 0x012F, 1, 7},
    {86, 10, 0x012F, 1, 7},
    {86, 100, 0x0154, 1, 4},
    {86, 15, 0x012D, 1, 28},
    {86, 16, 0x012E, 1, 18},
    {86, 17, 0x012D, 1, 28},
    {86, 18, 0x012E, 1, 18},
    {86, 13, 0x012D, 1, 28},
    {86, 14, 0x012E, 1, 18},
    {86, 19, 0x012D, 1, 28},
    {86, 20, 0x012E, 1, 18},
    {86, 21, 0x012D, 1, 28},
    {86, 22, 0x012E, 1, 18},
    {86, 23, 0x012D, 1, 28},
    {86, 24, 0x012E, 1, 18},
    {90, 8, 0x014D, 1, 1},
    {86, 2, 0x00E5, 2, 7},
    {84, 1, 0x00DA, 1, 10},
}};
static_assert(static_cast<size_t>(Setting::SETTING_COUNT) <= 32, "last_known_ has a bit per setting");

PROGMEM_STRING_TABLE(ModelNames, "ALPHA3 Model B");
// Add a model only after it was tested on a real pump
static constexpr Model MODELS[] = {
    {52, 1, 0, 0, ALPHA3_OBJECTS, &ALPHA3_SETTINGS, true, true},
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

/// The field in body, or NAN when the body is too short for it.
static float read_field(const Field &field, std::span<const uint8_t> body) {
  static constexpr uint8_t SIZES[] = {4, 8, 2, 4};
  if (field.offset + SIZES[static_cast<size_t>(field.format)] > body.size())
    return NAN;
  const uint8_t *at = &body[field.offset];
  switch (field.format) {
    case FieldFormat::FIELD_FORMAT_DOUBLE: {
      const uint64_t bits = (static_cast<uint64_t>(encode_uint32(at[0], at[1], at[2], at[3])) << 32) |
                            encode_uint32(at[4], at[5], at[6], at[7]);
      return static_cast<float>(bit_cast<double>(bits));
    }
    case FieldFormat::FIELD_FORMAT_UINT16:
      return encode_uint16(at[0], at[1]);
    case FieldFormat::FIELD_FORMAT_UINT32:
      return static_cast<float>(encode_uint32(at[0], at[1], at[2], at[3]));
    case FieldFormat::FIELD_FORMAT_FLOAT:
      break;
  }
  return read_float(at);
}
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_NUMBER)
static uint32_t read_unsigned(const uint8_t *at, uint8_t width) {
  return width == 4 ? encode_uint32(at[0], at[1], at[2], at[3]) : encode_uint16(at[0], at[1]);
}
#endif

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
// The class 5 ID of air venting (temporary_operation_control): 1 starts it, 0 stops it
static constexpr uint8_t REFERENCE_AIR_VENTING = 75;
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
static_assert(std::size(NUMBERS) == SET_POINT_COUNT, "NUMBERS has an entry per set point");

/// An unsigned big-endian field of 86/2 behind a number, and the seconds in one unit of the entity.
struct PumpKickNumber {
  uint8_t offset;
  uint8_t width;
  float seconds_per_unit;
};
// The time without activity (u32) in hours and the duration of the test run (u16) in seconds
static constexpr PumpKickNumber PUMP_KICK_NUMBERS[] = {{1, 4, SECONDS_PER_HOUR}, {5, 2, 1.0f}};
static_assert(SET_POINT_COUNT + std::size(PUMP_KICK_NUMBERS) ==
                  static_cast<size_t>(NumberSetting::NUMBER_SETTING_COUNT),
              "PUMP_KICK_NUMBERS has an entry per pump kick number");
#endif

#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SWITCH)
// The bools behind the switches, bit 0 of the byte: 86/2 enabled and 84/1 clock_program_enabled
static constexpr uint8_t OFFSET_PUMP_KICK_ENABLED = 0;
static constexpr uint8_t OFFSET_CLOCK_PROGRAM_ENABLED = 4;
#endif

#ifdef USE_GRUNDFOS_ALPHA_WRITE
/// The bits of last_known_ for the set point limits, which do not change.
static constexpr uint32_t limit_bits() {
  uint32_t bits = 0;
#ifdef USE_NUMBER
  for (const auto &layout : NUMBERS)
    bits |= 1u << static_cast<size_t>(layout.limits);
#endif
  return bits;
}
static constexpr uint32_t LIMIT_BITS = limit_bits();
#endif

#ifdef USE_TEXT_SENSOR
// OperationMode and ControlSource of profile 52_1_0; the last name is the fallback
PROGMEM_STRING_TABLE(OperatingModeNames, "Normal", "Stop", "Min", "Max", "User defined", "Forced start", "No command",
                     "Hand", "Unknown");
static constexpr uint8_t CONTROL_SOURCES[] = {0, 1, 2, 3, 4, 5, 101, 104, 111};
PROGMEM_STRING_TABLE(ControlSourceNames, "Undefined", "Panel", "Bus", "Link", "External input", "Off button",
                     "Clock program", "Automatic night setback", "Air venting", "Unknown");
#endif

#ifdef USE_TEXT_SENSOR
// Class 7 IDs of product_name, device_prod_no, sw_ver_str and ble_sw_ver_str, in the order of InfoStep
static constexpr uint8_t TEXT_IDS[] = {1, 8, 50, 58};
static_assert(std::size(TEXT_IDS) == TEXT_COUNT);

// A class 7 reply carries no ID, so a reply to an earlier text that comes late would pass for the text asked for. A
// real ALPHA3 sent "ALPHA3 25-60 180", "99371929", "99365052V01.00.06.00429" and "99365068V03.00.06.00001": the name
// starts with a letter, the product number has only digits and a version has digits before a V. The two versions
// cannot be told apart.
static bool is_plausible_text(InfoStep step, std::span<const uint8_t> text) {
  const auto is_digit = [](uint8_t c) { return c >= '0' && c <= '9'; };
  const auto digits = std::find_if_not(text.begin(), text.end(), is_digit);
  switch (step) {
    case InfoStep::INFO_STEP_PRODUCT_NAME:
      return !text.empty() && ((text[0] >= 'A' && text[0] <= 'Z') || (text[0] >= 'a' && text[0] <= 'z'));
    case InfoStep::INFO_STEP_PRODUCT_NUMBER:
      return !text.empty() && digits == text.end();
    default:
      return digits != text.begin() && digits != text.end() && *digits == 'V';
  }
}

static constexpr uint8_t DIAGNOSTICS_OBJECT = 88;
// 88/10199 EventLogInfo, type 0x00F3 v1: u16 latest_event_id, u16 current_entries, u16 log_size, bool extended_info
static constexpr uint16_t EVENT_LOG_SUB_ID = 10199;
static constexpr uint16_t EVENT_LOG_TYPE = 0x00F3;
static constexpr size_t EVENT_LOG_SIZE = 7;
// 88/10200 + n EventLogEntry, type 0x00F4 v1: u16 event_id, u16 event_code, u8 source, u8 related_no, u8 event_type,
// u32 Unix time. A real ALPHA3 held events 1 to 6 in 88/10200 to 88/10205 of a log of 40; that the log wraps around
// after log_size entries is assumed.
static constexpr uint16_t FIRST_EVENT_SUB_ID = 10200;
static constexpr uint16_t EVENT_TYPE = 0x00F4;
static constexpr size_t EVENT_SIZE = 11;
static constexpr uint8_t OFFSET_EVENT_TYPE = 6;
// Enum EventType of the Grundfos GO app profile 52_1_0 at its index (1 AlarmAppeared, 2 AlarmDisappered,
// 3 WarningAppeared, 4 WarningDisappered), not confirmed on a pump beyond a real ALPHA3 logging dry running (code 57)
// with type 1, then 2. The first name is the fallback, the last is an empty log.
PROGMEM_STRING_TABLE(EventNames, "Event", "Alarm appeared", "Alarm cleared", "Warning appeared", "Warning cleared",
                     "No events");
#endif
#endif

#if defined(USE_GRUNDFOS_ALPHA_WRITE) || defined(USE_GRUNDFOS_ALPHA_CLOCK)
// Status byte of a class 10 SET acknowledgement
static constexpr uint8_t SET_STATUS_BUSY = 2;
static constexpr uint8_t SET_STATUS_FAILED = 4;
#endif

#ifdef USE_GRUNDFOS_ALPHA_CLOCK
// Clock objects of profile 52_1_0, measured on an ALPHA3. 94/100 type 0x0141 v1: sync_enabled, year (u16), month,
// day, hour, minute, second, 1/100 s, hour_format, in local time; a write sets the clock. 94/101 type 0x0142 v1: the
// pump's local time in the same order, then weekday, day type and daylight saving status. 94/102, the daylight saving
// rule, is never written: a single byte of it clears the whole rule.
static constexpr uint8_t CLOCK_OBJECT = 94;
static constexpr uint16_t CLOCK_CONFIG_SUB_ID = 100;
static constexpr uint16_t CLOCK_CONFIG_TYPE = 0x0141;
static constexpr uint16_t CLOCK_ACTUAL_SUB_ID = 101;
static constexpr uint16_t CLOCK_ACTUAL_TYPE = 0x0142;
static constexpr uint8_t CLOCK_VERSION = 1;
static constexpr uint8_t CLOCK_CONFIG_SIZE = 10;
static constexpr uint8_t CLOCK_ACTUAL_SIZE = 12;
static constexpr uint8_t OFFSET_SYNC = 0;         // 94/100
static constexpr uint8_t OFFSET_HOUR_FORMAT = 9;  // 94/100; 0 is 24 h
static constexpr uint32_t CLOCK_CHECK_INTERVAL_MS = 24 * 60 * 60 * 1000;
// After a check or write that got no valid answer, or a busy pump
static constexpr uint32_t CLOCK_RETRY_MS = 60 * 60 * 1000;
// The time source has whole seconds; a smaller deviation is not worth a write to the pump's memory
static constexpr int32_t CLOCK_TOLERANCE_S = 2;

/// The class 10 object in a reply with status 0, when it has the given type, version 1 and size.
static bool parse_clock(const genibus::GenibusResponse &response, uint16_t type, uint8_t size,
                        genibus::DataObject &object) {
  genibus::ApduReader reader(response.apdus);
  genibus::Apdu apdu{};
  return response.error == GenibusError::GENIBUS_ERROR_NONE && reader.next(apdu) &&
         apdu.data_class == genibus::CLASS_DATA_OBJECT && genibus::parse_data_object(apdu, object) &&
         object.status == 0 && object.type == type && object.version == CLOCK_VERSION && object.body.size() == size;
}

/// Local calendar fields as seconds on one scale, to compare two local times.
static time_t local_seconds(ESPTime time) {
  time.recalc_timestamp_utc(false);
  return time.timestamp;
}
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
  LOG_SENSOR("  ", "Energy", this->energy_sensor_);
  LOG_SENSOR("  ", "Operating hours", this->operating_hours_sensor_);
  LOG_SENSOR("  ", "Starts", this->starts_sensor_);
  LOG_SENSOR("  ", "Warning code", this->warning_code_sensor_);
  LOG_SENSOR("  ", "Alarm code", this->alarm_code_sensor_);
#endif
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
  LOG_TEXT_SENSOR("  ", "Operating mode", this->operating_mode_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Control source", this->control_source_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Product name", this->product_name_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Product number", this->product_number_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Software version", this->software_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Bluetooth software version", this->bluetooth_software_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Last event", this->last_event_text_sensor_);
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
  LOG_NUMBER("  ", "Pump kick wait", this->numbers_[6]);
  LOG_NUMBER("  ", "Pump kick duration", this->numbers_[7]);
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_BUTTON)
  LOG_BUTTON("  ", "Start", this->start_button_);
  LOG_BUTTON("  ", "Stop", this->stop_button_);
  LOG_BUTTON("  ", "Reset alarm", this->reset_alarm_button_);
  LOG_BUTTON("  ", "Start air venting", this->start_air_venting_button_);
  LOG_BUTTON("  ", "Stop air venting", this->stop_air_venting_button_);
  LOG_BUTTON("  ", "Clear alarm log", this->clear_alarm_log_button_);
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SWITCH)
  LOG_SWITCH("  ", "Automatic night setback", this->automatic_night_setback_switch_);
  LOG_SWITCH("  ", "Pump kick", this->pump_kick_switch_);
  LOG_SWITCH("  ", "Clock program", this->clock_program_switch_);
#endif
#ifdef USE_GRUNDFOS_ALPHA_CLOCK
  ESP_LOGCONFIG(TAG, "  Sets the pump's clock from the time source");
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Clock deviation", this->clock_deviation_sensor_);
#endif
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
#ifdef USE_GRUNDFOS_ALPHA_CLOCK
  if (this->start_clock_())
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
#ifdef USE_GRUNDFOS_ALPHA_CLOCK
  if (current == REQUEST_CLOCK) {
    this->continue_clock_(response);
    return;
  }
#endif
  if (current == REQUEST_IDENTITY) {
    if (!this->identify_(response))
      return;
#ifdef USE_GRUNDFOS_ALPHA_CLOCK
    if (this->start_clock_())
      return;
#endif
    this->request_from_(0);
    return;
  }
  if (this->model_ == nullptr)
    return;
  const size_t objects = this->model_->objects.size();
  const size_t info = objects + static_cast<size_t>(Setting::SETTING_COUNT);
  if (current < objects) {
    this->decode_(current, response);
#ifdef USE_GRUNDFOS_ALPHA_SETTINGS
  } else if (this->model_->settings != nullptr && current < info) {
    this->decode_setting_(static_cast<Setting>(current - objects), response);
#endif
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
  } else if (current >= info && current < info + static_cast<size_t>(InfoStep::INFO_STEP_COUNT)) {
    this->decode_info_(static_cast<InfoStep>(current - info), response);
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

void GrundfosAlpha::on_connected() {
  this->missing_ = 0;
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
  this->texts_read_ = 0;
  this->texts_retried_ = 0;
  this->event_wanted_ = 0;
  this->event_log_missing_ = false;
#endif
  this->update();
}

void GrundfosAlpha::on_disconnected() {
  this->publish_unknown_();
#ifdef USE_GRUNDFOS_ALPHA_CLOCK
  // The pump may have lost its clock with its power; check again after the next connection
  this->clock_phase_ = ClockPhase::CLOCK_PHASE_NONE;
  this->clock_due_ = true;
#endif
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
  // Empty, as ble_client text sensors show a lost connection. The product texts stay: they are read again after the
  // pump comes back, which happens several times a day when the heating cuts its power.
  for (auto *text_sensor :
       {this->operating_mode_text_sensor_, this->control_source_text_sensor_, this->last_event_text_sensor_}) {
    if (text_sensor != nullptr && text_sensor->has_state())
      text_sensor->publish_state("");
  }
  this->event_shown_ = NO_EVENT_SHOWN;
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
  for (const auto &field : layout.fields)
    this->publish_(field.reading, ok ? read_field(field, object.body) * field.factor : NAN);
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
  if (this->model_->settings != nullptr) {
    for (size_t i = step - objects.size(); i < static_cast<size_t>(Setting::SETTING_COUNT); i++) {
      if (!this->wants_setting_(static_cast<Setting>(i)))
        continue;
      const SettingLayout &layout = (*this->model_->settings)[i];
      if (this->request_object_(layout.object, layout.sub_id))
        this->current_ = static_cast<uint8_t>(objects.size() + i);
      return;
    }
  }
#ifdef USE_TEXT_SENSOR
  const size_t info = objects.size() + static_cast<size_t>(Setting::SETTING_COUNT);
  for (size_t i = std::max(step, info) - info; i < static_cast<size_t>(InfoStep::INFO_STEP_COUNT); i++) {
    const auto info_step = static_cast<InfoStep>(i);
    if (!this->wants_info_(info_step))
      continue;
    if (this->request_info_(info_step))
      this->current_ = static_cast<uint8_t>(info + i);
    return;
  }
#endif
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
  // A counter that falls back after a power cut keeps its highest value until it passes it again, so that
  // total_increasing sensors do not go down. A restart of the ESP forgets the highest values.
  const size_t total = static_cast<size_t>(reading) - static_cast<size_t>(Reading::READING_ENERGY);
  if (total < this->totals_.size() && !std::isnan(value)) {
    if (value < this->totals_[total])
      value = this->totals_[total];
    this->totals_[total] = value;
  }
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
    case Reading::READING_ENERGY:
      return this->energy_sensor_;
    case Reading::READING_OPERATING_HOURS:
      return this->operating_hours_sensor_;
    case Reading::READING_STARTS:
      return this->starts_sensor_;
    case Reading::READING_WARNING_CODE:
      return this->warning_code_sensor_;
    case Reading::READING_ALARM_CODE:
      return this->alarm_code_sensor_;
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
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SWITCH)
  if (setting == Setting::SETTING_AUTOMATIC_NIGHT_SETBACK)
    return this->automatic_night_setback_switch_ != nullptr;
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
  if (setting == Setting::SETTING_PUMP_KICK &&
      std::any_of(this->numbers_.begin() + SET_POINT_COUNT, this->numbers_.end(),
                  [](const number::Number *entity) { return entity != nullptr; }))
    return true;
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SWITCH)
  if (setting == Setting::SETTING_PUMP_KICK && this->pump_kick_switch_ != nullptr)
    return true;
  if (setting == Setting::SETTING_CLOCK_PROGRAM && this->clock_program_switch_ != nullptr)
    return true;
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
    ESP_LOGW(TAG, "Unexpected reply to object %u/%u: class %u, ACK %u, status %u, type 0x%04X v%u, %u bytes",
             layout.object, layout.sub_id, apdu.data_class, apdu.ack, object.status, object.type, object.version,
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
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SWITCH)
    case Setting::SETTING_AUTOMATIC_NIGHT_SETBACK:
      PUBLISH_STATE(this->automatic_night_setback_switch_, body[0] != 0);
      break;
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
  if (setting == Setting::SETTING_PUMP_KICK) {
    for (size_t i = 0; i < std::size(PUMP_KICK_NUMBERS); i++) {
      const PumpKickNumber &layout = PUMP_KICK_NUMBERS[i];
      PUBLISH_STATE(this->numbers_[SET_POINT_COUNT + i],
                    read_unsigned(&body[layout.offset], layout.width) / layout.seconds_per_unit);
    }
  }
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SWITCH)
  if (setting == Setting::SETTING_PUMP_KICK)
    PUBLISH_STATE(this->pump_kick_switch_, (body[OFFSET_PUMP_KICK_ENABLED] & 0x01) != 0);
  if (setting == Setting::SETTING_CLOCK_PROGRAM)
    PUBLISH_STATE(this->clock_program_switch_, (body[OFFSET_CLOCK_PROGRAM_ENABLED] & 0x01) != 0);
#endif
}
#endif

#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
text_sensor::TextSensor *GrundfosAlpha::text_sensor_(InfoStep step) const {
  switch (step) {
    case InfoStep::INFO_STEP_PRODUCT_NAME:
      return this->product_name_text_sensor_;
    case InfoStep::INFO_STEP_PRODUCT_NUMBER:
      return this->product_number_text_sensor_;
    case InfoStep::INFO_STEP_SOFTWARE_VERSION:
      return this->software_version_text_sensor_;
    case InfoStep::INFO_STEP_BLUETOOTH_SOFTWARE_VERSION:
      return this->bluetooth_software_version_text_sensor_;
    default:
      return this->last_event_text_sensor_;
  }
}

bool GrundfosAlpha::wants_info_(InfoStep step) const {
  if (!this->model_->info || this->text_sensor_(step) == nullptr)
    return false;
  const size_t i = static_cast<size_t>(step);
  if (i < TEXT_COUNT)
    return (this->texts_read_ & (1u << i)) == 0;
  if (step == InfoStep::INFO_STEP_EVENT_LOG)
    return !this->event_log_missing_;
  return this->event_wanted_ != 0;
}

bool GrundfosAlpha::request_info_(InfoStep step) {
  const size_t i = static_cast<size_t>(step);
  if (i < TEXT_COUNT)
    return this->request_text_(TEXT_IDS[i]);
  if (step == InfoStep::INFO_STEP_EVENT_LOG)
    return this->request_object_(DIAGNOSTICS_OBJECT, EVENT_LOG_SUB_ID);
  return this->request_object_(DIAGNOSTICS_OBJECT, this->event_sub_id_);
}

void GrundfosAlpha::decode_info_(InfoStep step, const genibus::GenibusResponse &response) {
  if (step == InfoStep::INFO_STEP_EVENT_LOG) {
    this->decode_event_log_(response);
    return;
  }
  if (step == InfoStep::INFO_STEP_EVENT) {
    this->decode_event_(response);
    return;
  }
  // The bus has logged a timeout or a broken frame; read again at the next update
  if (response.error != GenibusError::GENIBUS_ERROR_NONE)
    return;
  const uint8_t bit = 1u << static_cast<size_t>(step);
  genibus::ApduReader reader(response.apdus);
  genibus::Apdu apdu{};
  std::span<const uint8_t> text;
  const bool replied = reader.next(apdu);
  if (replied && genibus::parse_text(apdu, text) && is_plausible_text(step, text)) {
    PUBLISH_STATE(this->text_sensor_(step), reinterpret_cast<const char *>(text.data()), text.size());
    this->texts_read_ |= bit;
    return;
  }
  if (this->first_warning_(WARNED_INFO)) {
    ESP_LOGW(TAG, "Unexpected reply to text %u: class %u, ACK %u, %u bytes", TEXT_IDS[static_cast<size_t>(step)],
             apdu.data_class, apdu.ack, static_cast<unsigned>(apdu.data.size()));
  }
  // A text the pump refuses (an ACK) or sends in another form is not asked for again until the next connection. A
  // reply of another class or an implausible text may be a late reply to an earlier request: asked for once more.
  const bool late = !replied || apdu.data_class != genibus::CLASS_ASCII || (apdu.ack == 0 && !text.empty());
  if (!late || (this->texts_retried_ & bit) != 0)
    this->texts_read_ |= bit;
  this->texts_retried_ |= bit;
}

void GrundfosAlpha::clear_last_event_() {
  auto *sensor = this->last_event_text_sensor_;
  if (sensor->has_state() && !sensor->state.empty())
    sensor->publish_state("");
}

bool GrundfosAlpha::parse_event_object_(const genibus::GenibusResponse &response, uint16_t sub_id, uint16_t type,
                                        size_t size, genibus::DataObject &object) {
  if (response.error != GenibusError::GENIBUS_ERROR_NONE)
    return false;
  genibus::ApduReader reader(response.apdus);
  genibus::Apdu apdu{};
  const bool replied = reader.next(apdu);
  if (replied && genibus::parse_data_object(apdu, object) && object.status == 0 && object.type == type &&
      object.version == 1 && object.body.size() == size)
    return true;
  if (replied && genibus::is_object_missing(apdu)) {
    // Not asked for again until the next connection
    this->event_log_missing_ = true;
    ESP_LOGW(TAG, "The pump has no object 88/%u; the last event stays unknown", sub_id);
  } else if (this->first_warning_(WARNED_INFO)) {
    ESP_LOGW(TAG, "Unexpected reply to object 88/%u: class %u, ACK %u, status %u, type 0x%04X v%u, %u bytes", sub_id,
             apdu.data_class, apdu.ack, object.status, object.type, object.version,
             static_cast<unsigned>(object.body.size()));
  }
  return false;
}

void GrundfosAlpha::decode_event_log_(const genibus::GenibusResponse &response) {
  genibus::DataObject object{};
  if (!this->parse_event_object_(response, EVENT_LOG_SUB_ID, EVENT_LOG_TYPE, EVENT_LOG_SIZE, object))
    return;
  const auto &body = object.body;
  const uint16_t latest = encode_uint16(body[0], body[1]);
  const uint16_t entries = encode_uint16(body[2], body[3]);
  const uint16_t log_size = encode_uint16(body[4], body[5]);
  if (latest == 0 || entries == 0) {
    if (this->event_shown_ != 0) {
      this->last_event_text_sensor_->publish_state(EventNames::get_progmem_str(EventNames::LAST_INDEX, 0));
      this->event_shown_ = 0;
    }
    return;
  }
  if (log_size == 0 || log_size > UINT16_MAX - FIRST_EVENT_SUB_ID) {
    if (this->first_warning_(WARNED_INFO)) {
      ESP_LOGW(TAG, "Event log of %u entries not read", log_size);
    }
    this->clear_last_event_();
    this->event_shown_ = NO_EVENT_SHOWN;
    return;
  }
  if (latest == this->event_shown_)
    return;
  this->event_wanted_ = latest;
  this->event_sub_id_ = FIRST_EVENT_SUB_ID + (latest - 1) % log_size;
}

void GrundfosAlpha::decode_event_(const genibus::GenibusResponse &response) {
  const uint16_t wanted = this->event_wanted_;
  this->event_wanted_ = 0;
  genibus::DataObject object{};
  // On failure the next update reads the log and the entry again
  if (!this->parse_event_object_(response, this->event_sub_id_, EVENT_TYPE, EVENT_SIZE, object))
    return;
  const auto &body = object.body;
  const uint16_t id = encode_uint16(body[0], body[1]);
  // Shown or not, the entry is read again only when the log names another event
  this->event_shown_ = wanted;
  if (id != wanted) {
    if (this->first_warning_(WARNED_INFO)) {
      ESP_LOGW(TAG, "Object 88/%u holds event %u, not the latest event %u", this->event_sub_id_, id, wanted);
    }
    this->clear_last_event_();
    return;
  }
  const uint8_t type = body[OFFSET_EVENT_TYPE];
  char text[EventNames::MAX_LENGTH + sizeof(" (code 65535)")];
  ESPHOME_snprintf_P(text, sizeof(text), ESPHOME_PSTR("%s (code %u)"),
                     LOG_STR_ARG(EventNames::get_log_str(type < EventNames::LAST_INDEX ? type : 0, 0)),
                     encode_uint16(body[2], body[3]));
  this->last_event_text_sensor_->publish_state(text);
}
#endif

#ifdef USE_GRUNDFOS_ALPHA_WRITE
#ifdef USE_NUMBER
void GrundfosAlpha::write_number(NumberSetting setting, float value) {
  const size_t i = static_cast<size_t>(setting);
  if (i >= this->numbers_.size())
    return;
  if (i >= SET_POINT_COUNT) {
    // Without its entity 86/2 is not polled, and the write would wait for ever
    if (this->numbers_[i] == nullptr)
      return;
    // The pump reports no limits for these: keep to the entity's range
    const number::NumberTraits &traits = this->numbers_[i]->traits;
    if (std::isnan(value) || value < traits.get_min_value() || value > traits.get_max_value()) {
      ESP_LOGW(TAG, "Pump kick value %.0f not written: outside %.0f to %.0f", value, traits.get_min_value(),
               traits.get_max_value());
      return;
    }
    const PumpKickNumber &layout = PUMP_KICK_NUMBERS[i - SET_POINT_COUNT];
    this->enqueue_({Setting::SETTING_PUMP_KICK, layout.offset, layout.width,
                    static_cast<uint32_t>(lroundf(value * layout.seconds_per_unit)), Command::COMMAND_NONE},
                   LOG_STR("Pump kick"));
    return;
  }
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
  this->enqueue_({NUMBERS[i].value, OFFSET_SET_POINT, 4, bit_cast<uint32_t>(raw), Command::COMMAND_NONE},
                 LOG_STR("Set point"));
}
#endif

#ifdef USE_SWITCH
// Without its entity the object is not polled, and the write would wait for ever
void GrundfosAlpha::write_pump_kick(bool state) {
  if (this->pump_kick_switch_ != nullptr) {
    this->enqueue_({Setting::SETTING_PUMP_KICK, OFFSET_PUMP_KICK_ENABLED, WIDTH_BIT, state, Command::COMMAND_NONE},
                   LOG_STR("Pump kick"));
  }
}

void GrundfosAlpha::write_clock_program(bool state) {
  if (this->clock_program_switch_ != nullptr) {
    this->enqueue_(
        {Setting::SETTING_CLOCK_PROGRAM, OFFSET_CLOCK_PROGRAM_ENABLED, WIDTH_BIT, state, Command::COMMAND_NONE},
        LOG_STR("Clock program"));
  }
}
#endif

#ifdef USE_SWITCH
void GrundfosAlpha::write_automatic_night_setback(bool state) {
  this->enqueue_({Setting::SETTING_AUTOMATIC_NIGHT_SETBACK, 0, 1, state, Command::COMMAND_NONE},
                 LOG_STR("Automatic night setback"));
}
#endif

#ifdef USE_SELECT
void GrundfosAlpha::write_select(SelectSetting setting, size_t index) {
  const bool control = setting == SelectSetting::SELECT_SETTING_CONTROL_MODE;
  const std::span<const uint8_t> values = control ? std::span<const uint8_t>(CONTROL_MODES) : OPERATING_MODES;
  if (index >= values.size())
    return;
  this->enqueue_({control ? Setting::SETTING_CONTROL_MODE : Setting::SETTING_OPERATING_MODE,
                  control ? OFFSET_CONTROL_MODE : OFFSET_OPERATION_MODE, 1, values[index], Command::COMMAND_NONE},
                 control ? LOG_STR("Control mode") : LOG_STR("Operating mode"));
}
#endif

bool GrundfosAlpha::enqueue_(const WriteRequest &request, const LogString *what) {
  if (!this->parent_->is_connected() || this->model_ == nullptr || this->model_->settings == nullptr) {
    ESP_LOGW(TAG, "%s not written: pump not connected or not supported", LOG_STR_ARG(what));
    return false;
  }
  // One object can hold several values: only a write of the same value replaces a waiting one
  size_t slot = 0;
  while (slot < this->queued_ &&
         (this->queue_[slot].setting != request.setting || this->queue_[slot].offset != request.offset ||
          this->queue_[slot].command != request.command))
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
    case Command::COMMAND_RESET_ALARM_LOG:
      this->enqueue_({Setting::SETTING_COUNT, 0, 0, 0, command}, LOG_STR("Command"));
      break;
    default:
      break;
  }
}

void GrundfosAlpha::write_air_venting(bool on) {
  this->enqueue_({Setting::SETTING_COUNT, 0, 0, on, Command::COMMAND_AIR_VENTING}, LOG_STR("Air venting"));
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
      bool sent;
      if (this->job_.command == Command::COMMAND_AIR_VENTING) {
        // Class 5 SET of one value; the only class 5 value written
        const uint8_t apdu[] = {CLASS_REFERENCE, genibus::OPERATION_SET | 2, REFERENCE_AIR_VENTING,
                                static_cast<uint8_t>(this->job_.value)};
        sent = this->parent_->send(this, apdu);
      } else {
        sent = this->send_command_(static_cast<uint8_t>(this->job_.command));
      }
      if (!sent) {
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
    if (!this->request_object_(this->job_layout_().object, this->job_layout_().sub_id)) {
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
  uint32_t needed = 1u << static_cast<size_t>(request.setting);
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

const SettingLayout &GrundfosAlpha::job_layout_() const {
  return (*this->model_->settings)[static_cast<size_t>(this->job_.setting)];
}

void GrundfosAlpha::apply_job_() {
  uint8_t *at = &this->job_body_[this->job_.offset];
  switch (this->job_.width) {
    case WIDTH_BIT:
      *at = (*at & 0xFE) | (this->job_.value & 0x01);
      break;
    case 1:
      *at = static_cast<uint8_t>(this->job_.value);
      break;
    case 2:
      put_be16(at, static_cast<uint16_t>(this->job_.value));
      break;
    default:
      put_be32(at, this->job_.value);
      break;
  }
}

bool GrundfosAlpha::same_outside_value_(std::span<const uint8_t> body) const {
  const size_t width = this->job_.width == WIDTH_BIT ? 1 : this->job_.width;
  for (size_t i = 0; i < body.size(); i++) {
    uint8_t changes = 0;  // the bits the job writes
    if (i >= this->job_.offset && i < this->job_.offset + width)
      changes = this->job_.width == WIDTH_BIT ? 0x01 : 0xFF;
    if (((body[i] ^ this->job_body_[i]) & ~changes) != 0)
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
      if (!this->set_object_(this->job_layout_().object, this->job_layout_().sub_id, this->job_type_,
                             this->job_version_, {this->job_body_.data(), this->job_size_})) {
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
      if (!this->request_object_(this->job_layout_().object, this->job_layout_().sub_id)) {
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
      // A command or class 5 reply has no data
      genibus::ApduReader reader(response.apdus);
      genibus::Apdu apdu{};
      const uint8_t data_class =
          this->job_.command == Command::COMMAND_AIR_VENTING ? CLASS_REFERENCE : genibus::CLASS_COMMAND;
      if (response.error != GenibusError::GENIBUS_ERROR_NONE) {
        this->end_job_(LOG_STR("no reply"));
      } else if (!reader.next(apdu) || apdu.data_class != data_class || apdu.ack != 0 || !apdu.data.empty()) {
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
    uint32_t keep = LIMIT_BITS;
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
  if (this->job_.setting != Setting::SETTING_COUNT) {
    ESP_LOGW(TAG, "Write to %u/%u failed: %s", this->job_layout_().object, this->job_layout_().sub_id,
             LOG_STR_ARG(failure));
  } else if (this->job_.command == Command::COMMAND_AIR_VENTING) {
    ESP_LOGW(TAG, "Air venting write failed: %s", LOG_STR_ARG(failure));
  } else {
    ESP_LOGW(TAG, "Command %u failed: %s", static_cast<uint8_t>(this->job_.command), LOG_STR_ARG(failure));
  }
  this->write_warning_ = WriteWarning::WRITE_WARNING_SET;
  this->status_set_warning(LOG_STR("Write failed"));
}
#endif

#ifdef USE_GRUNDFOS_ALPHA_CLOCK
bool GrundfosAlpha::start_clock_() {
  if (this->time_ == nullptr || this->model_ == nullptr || !this->model_->has_clock ||
      this->clock_phase_ != ClockPhase::CLOCK_PHASE_NONE)
    return false;
#ifdef USE_GRUNDFOS_ALPHA_WRITE
  if (this->phase_ != JobPhase::JOB_PHASE_NONE)
    return false;
#endif
  if (!this->clock_due_ && App.get_loop_component_start_time() - this->clock_checked_at_ < CLOCK_CHECK_INTERVAL_MS)
    return false;
  // Never set the pump's clock from a time source that has no time yet
  if (!this->time_->now().is_valid(false, false))
    return false;
  if (!this->request_object_(CLOCK_OBJECT, CLOCK_ACTUAL_SUB_ID))
    return false;
  this->clock_phase_ = ClockPhase::CLOCK_PHASE_CHECK;
  this->current_ = REQUEST_CLOCK;
  return true;
}

bool GrundfosAlpha::clock_deviation_(const genibus::GenibusResponse &response, int32_t &deviation) {
  genibus::DataObject object{};
  if (!parse_clock(response, CLOCK_ACTUAL_TYPE, CLOCK_ACTUAL_SIZE, object))
    return false;
  const auto &body = object.body;
  ESPTime pump{};
  pump.year = encode_uint16(body[0], body[1]);
  pump.month = body[2];
  pump.day_of_month = body[3];
  pump.hour = body[4];
  pump.minute = body[5];
  pump.second = body[6];
  const ESPTime now = this->time_->now();
  if (!pump.fields_in_range(false, false) || !now.is_valid(false, false))
    return false;
  deviation = static_cast<int32_t>(local_seconds(pump) - local_seconds(now));
  return true;
}

void GrundfosAlpha::continue_clock_(const genibus::GenibusResponse &response) {
  genibus::DataObject object{};
  int32_t deviation = 0;
  const LogString *failure = nullptr;
  bool done = false;
  bool block = false;  // the pump answered the write; the same write would only repeat
  switch (this->clock_phase_) {
    case ClockPhase::CLOCK_PHASE_CHECK: {
      if (!this->clock_deviation_(response, deviation)) {
        failure = LOG_STR("no valid time in 94/101");
        break;
      }
#ifdef USE_SENSOR
      PUBLISH_STATE(this->clock_deviation_sensor_, static_cast<float>(deviation));
#endif
      if (std::abs(deviation) < CLOCK_TOLERANCE_S) {
        done = true;
        break;
      }
      if (this->clock_write_blocked_) {
        ESP_LOGD(TAG, "Pump clock is %" PRId32 " s off; not set again until restart", deviation);
        done = true;
        break;
      }
      ESP_LOGD(TAG, "Pump clock is %" PRId32 " s off; setting it", deviation);
      this->clock_phase_ = ClockPhase::CLOCK_PHASE_READ;
      if (!this->request_object_(CLOCK_OBJECT, CLOCK_CONFIG_SUB_ID)) {
        failure = LOG_STR("not sent");
        break;
      }
      this->current_ = REQUEST_CLOCK;
      return;
    }
    case ClockPhase::CLOCK_PHASE_READ: {
      const ESPTime now = this->time_->now();
      if (!parse_clock(response, CLOCK_CONFIG_TYPE, CLOCK_CONFIG_SIZE, object)) {
        failure = LOG_STR("no valid read of 94/100");
        break;
      }
      if (!now.is_valid(false, false)) {
        failure = LOG_STR("no valid time");
        break;
      }
      // Only the time changes, and it is always the current time: written back unchanged, an old time in the object
      // would set the clock back
      auto &body = this->clock_body_;
      body[OFFSET_SYNC] = object.body[OFFSET_SYNC];
      put_be16(&body[1], now.year);
      body[3] = now.month;
      body[4] = now.day_of_month;
      body[5] = now.hour;
      body[6] = now.minute;
      body[7] = now.second;
      body[8] = 0;
      // The pump refuses 12 h (1), but 94/100 can still show it after the refusal; 24 h is the only valid value
      body[OFFSET_HOUR_FORMAT] = 0;
      this->clock_phase_ = ClockPhase::CLOCK_PHASE_WRITE;
      if (!this->set_object_(CLOCK_OBJECT, CLOCK_CONFIG_SUB_ID, CLOCK_CONFIG_TYPE, CLOCK_VERSION, body)) {
        failure = LOG_STR("not sent");
        break;
      }
      this->current_ = REQUEST_CLOCK;
      return;
    }
    case ClockPhase::CLOCK_PHASE_WRITE: {
      genibus::ApduReader reader(response.apdus);
      genibus::Apdu apdu{};
      if (response.error != GenibusError::GENIBUS_ERROR_NONE) {
        failure = LOG_STR("no reply");
        break;
      }
      if (!reader.next(apdu) || apdu.data_class != genibus::CLASS_DATA_OBJECT || apdu.ack != 0 ||
          apdu.data.size() != 1) {
        ESP_LOGV(TAG, "Clock write reply: class %u, ACK %u, %u bytes", apdu.data_class, apdu.ack,
                 static_cast<unsigned>(apdu.data.size()));
        failure = LOG_STR("refused");
        block = true;
        break;
      }
      if (apdu.data[0] != 0) {
        ESP_LOGV(TAG, "Clock write status %u", apdu.data[0]);
        // A busy pump may take the same write later; any other status refuses it again
        block = apdu.data[0] != SET_STATUS_BUSY;
        failure = block ? (apdu.data[0] == SET_STATUS_FAILED ? LOG_STR("operation failed") : LOG_STR("refused"))
                        : LOG_STR("pump busy");
        break;
      }
      this->clock_phase_ = ClockPhase::CLOCK_PHASE_VERIFY;
      if (!this->request_object_(CLOCK_OBJECT, CLOCK_CONFIG_SUB_ID)) {
        failure = LOG_STR("read-back not sent");
        break;
      }
      this->current_ = REQUEST_CLOCK;
      return;
    }
    case ClockPhase::CLOCK_PHASE_VERIFY: {
      if (!parse_clock(response, CLOCK_CONFIG_TYPE, CLOCK_CONFIG_SIZE, object)) {
        failure = LOG_STR("no valid read-back");
        break;
      }
      if (!std::equal(object.body.begin(), object.body.end(), this->clock_body_.begin())) {
        failure = LOG_STR("the read-back differs");
        block = true;
        break;
      }
      this->clock_phase_ = ClockPhase::CLOCK_PHASE_RECHECK;
      if (!this->request_object_(CLOCK_OBJECT, CLOCK_ACTUAL_SUB_ID)) {
        failure = LOG_STR("check not sent");
        break;
      }
      this->current_ = REQUEST_CLOCK;
      return;
    }
    case ClockPhase::CLOCK_PHASE_RECHECK: {
      if (!this->clock_deviation_(response, deviation)) {
        failure = LOG_STR("no valid time in 94/101 after the write");
        break;
      }
#ifdef USE_SENSOR
      PUBLISH_STATE(this->clock_deviation_sensor_, static_cast<float>(deviation));
#endif
      // For example a daylight saving rule in the pump that does not match the time source
      if (std::abs(deviation) >= CLOCK_TOLERANCE_S) {
        failure = LOG_STR("the clock did not take the time");
        block = true;
        break;
      }
      done = true;
      break;
    }
    default:
      return;
  }
  if (done || failure != nullptr)
    this->end_clock_(failure, block);
  // After a failed reply a late answer may still come; the next update() goes on
  if (response.error != GenibusError::GENIBUS_ERROR_NONE || !this->parent_->is_connected())
    return;
#ifdef USE_GRUNDFOS_ALPHA_WRITE
  if (this->start_job_())
    return;
#endif
  this->request_from_(0);
}

void GrundfosAlpha::end_clock_(const LogString *failure, bool block_writes) {
  this->clock_phase_ = ClockPhase::CLOCK_PHASE_NONE;
  this->clock_due_ = false;
  this->clock_checked_at_ = App.get_loop_component_start_time();
  if (failure == nullptr) {
    ESP_LOGV(TAG, "Pump clock checked");
    return;
  }
  // Only a log line: the clock does not affect the pumping
  if (block_writes) {
    // Writing again would wear the pump's memory, for example each hour all summer for an unknown daylight saving
    // rule; the checks go on
    this->clock_write_blocked_ = true;
    ESP_LOGW(TAG, "Setting the pump clock failed: %s; not set again until restart", LOG_STR_ARG(failure));
    return;
  }
  ESP_LOGW(TAG, "Setting the pump clock failed: %s", LOG_STR_ARG(failure));
  this->clock_checked_at_ -= CLOCK_CHECK_INTERVAL_MS - CLOCK_RETRY_MS;
}
#endif

bool GrundfosAlpha::first_warning_(uint8_t bit) {
  if ((this->warned_ & bit) != 0)
    return false;
  this->warned_ |= bit;
  return true;
}

}  // namespace esphome::grundfos_alpha
