#pragma once

#include <array>
#include <cmath>
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
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SELECT)
#include "esphome/components/select/select.h"
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_NUMBER)
#include "esphome/components/number/number.h"
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_BUTTON)
#include "esphome/components/button/button.h"
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SWITCH)
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_GRUNDFOS_ALPHA_CLOCK
#include "esphome/components/time/real_time_clock.h"
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
  READING_ENERGY,
  // The counters of 93/1, which fall back a little after a power cut; keep them together (see totals_)
  READING_OPERATING_HOURS,
  READING_STARTS,
  READING_WARNING_CODE,
  READING_ALARM_CODE,
};

/// How a field is stored in the body; all are big-endian.
enum class FieldFormat : uint8_t {
  FIELD_FORMAT_FLOAT,
  FIELD_FORMAT_DOUBLE,
  FIELD_FORMAT_UINT16,
  FIELD_FORMAT_UINT32,
};

/// A value at offset in the body of an object, multiplied by factor before it is published.
struct Field {
  Reading reading;
  uint8_t offset;
  FieldFormat format;
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

/// The settings objects the entities show, in the order they are polled after the measurements.
enum class Setting : uint8_t {
  SETTING_REALIZED,  // 86/8: the operating mode and control source in effect
  SETTING_CONTROL_MODE,
  SETTING_OPERATING_MODE,
  SETTING_CONSTANT_PRESSURE_LIMITS,
  SETTING_CONSTANT_PRESSURE,
  SETTING_PROPORTIONAL_PRESSURE_LIMITS,
  SETTING_PROPORTIONAL_PRESSURE,
  SETTING_CONSTANT_SPEED_LIMITS,
  SETTING_CONSTANT_SPEED,
  SETTING_AUTOADAPT_RADIATOR_LIMITS,
  SETTING_AUTOADAPT_RADIATOR,
  SETTING_AUTOADAPT_UNDERFLOOR_LIMITS,
  SETTING_AUTOADAPT_UNDERFLOOR,
  SETTING_AUTOADAPT_RADIATOR_AND_UNDERFLOOR_LIMITS,
  SETTING_AUTOADAPT_RADIATOR_AND_UNDERFLOOR,
  SETTING_AUTOMATIC_NIGHT_SETBACK,
  SETTING_COUNT,
};

/// A settings object as one model sends it.
struct SettingLayout {
  uint8_t object;
  uint16_t sub_id;
  uint16_t type;
  uint8_t version;
  uint8_t size;
};
using SettingLayouts = std::array<SettingLayout, static_cast<size_t>(Setting::SETTING_COUNT)>;

/// The reads of the text sensors, polled after the settings: the texts once per connection, the event log at every
/// update and the entry of its latest event when that changed.
enum class InfoStep : uint8_t {
  INFO_STEP_PRODUCT_NAME,
  INFO_STEP_PRODUCT_NUMBER,
  INFO_STEP_SOFTWARE_VERSION,
  INFO_STEP_BLUETOOTH_SOFTWARE_VERSION,
  INFO_STEP_EVENT_LOG,
  INFO_STEP_EVENT,
  INFO_STEP_COUNT,
};
static constexpr size_t TEXT_COUNT = static_cast<size_t>(InfoStep::INFO_STEP_EVENT_LOG);

enum class SelectSetting : uint8_t { SELECT_SETTING_CONTROL_MODE, SELECT_SETTING_OPERATING_MODE, SELECT_SETTING_COUNT };
enum class NumberSetting : uint8_t {
  NUMBER_SETTING_CONSTANT_PRESSURE,
  NUMBER_SETTING_PROPORTIONAL_PRESSURE,
  NUMBER_SETTING_CONSTANT_SPEED,
  NUMBER_SETTING_AUTOADAPT_RADIATOR,
  NUMBER_SETTING_AUTOADAPT_UNDERFLOOR,
  NUMBER_SETTING_AUTOADAPT_RADIATOR_AND_UNDERFLOOR,
  NUMBER_SETTING_COUNT,
};

enum class JobPhase : uint8_t {
  JOB_PHASE_NONE,
  JOB_PHASE_READ,     // the object is read before it is changed
  JOB_PHASE_WRITE,    // the changed object is written
  JOB_PHASE_VERIFY,   // the object is read back
  JOB_PHASE_COMMAND,  // a class 3 command or class 5 write is on the wire
};

/// Class 3 commands the Grundfos GO app sends to an ALPHA3; the value is the command ID. A stop lasts through a loss
/// of power until a start.
enum class Command : uint8_t {
  COMMAND_NONE = 0,
  COMMAND_RESET_ALARM = 2,
  COMMAND_STOP = 5,
  COMMAND_START = 6,  // AUTO in the profile
  COMMAND_RESET_ALARM_LOG = 51,
  // Not a class 3 ID: the class 5 write of air venting, byte_value 1 starts it, 0 stops it
  COMMAND_AIR_VENTING = 0xFF,
};

// The largest settings object that is written (type 0x012E, 18 bytes)
static constexpr size_t MAX_WRITE_SIZE = 18;
// Writes that wait while one runs
static constexpr size_t WRITE_QUEUE_SIZE = 4;
// Updates a waiting write may wait for a valid read of its object before it is dropped
static constexpr uint8_t WRITE_WAIT_CYCLES = 3;

enum class WriteWarning : uint8_t { WRITE_WARNING_NONE, WRITE_WARNING_SET, WRITE_WARNING_CLEAR_ON_READ };

/// One write: change one byte or one big-endian float of a settings object, or send a command (setting is then
/// SETTING_COUNT; COMMAND_AIR_VENTING is the class 5 air venting write).
struct WriteRequest {
  Setting setting;
  uint8_t offset;  // of the value in the body
  uint8_t byte_value;
  bool is_float;
  float float_value;
  Command command;
};

enum class ClockPhase : uint8_t {
  CLOCK_PHASE_NONE,
  CLOCK_PHASE_CHECK,    // 94/101 is read to compare the pump's clock with the time source
  CLOCK_PHASE_READ,     // 94/100 is read before it is written
  CLOCK_PHASE_WRITE,    // 94/100 is written with the local time
  CLOCK_PHASE_VERIFY,   // 94/100 is read back
  CLOCK_PHASE_RECHECK,  // 94/101 is read again to see that the clock took the time
};

/// The objects read from a pump of one unit family, type and version; settings is null for a model without them.
/// has_clock: the pump has the clock objects 94/100 and 94/101 in the layout of profile 52_1_0.
struct Model {
  uint8_t family;
  uint8_t type;
  uint8_t version;
  uint8_t name;  // index into ModelNames
  std::span<const ObjectLayout> objects;
  const SettingLayouts *settings;
  bool info;  // whether the pump has the class 7 texts of TEXT_IDS and the event log in object 88
  bool has_clock;
};

// Values of current_ besides a step of the poll chain
static constexpr uint8_t REQUEST_CLOCK = 0xFC;
static constexpr uint8_t REQUEST_WRITE = 0xFD;
static constexpr uint8_t REQUEST_IDENTITY = 0xFE;
static constexpr uint8_t REQUEST_NONE = 0xFF;
static constexpr uint32_t NO_EVENT_SHOWN = 0x10000;

/// A Grundfos ALPHA circulator pump. It reads the pump's unit family, type and version once, then polls the
/// objects of that model: the measurements first, then the settings its entities show. A write reads the settings
/// object, checks it against the last poll, changes one value, writes it and reads it back; writes run one at a time.
class GrundfosAlpha final : public genibus::GenibusDevice {
#ifdef USE_SENSOR
  SUB_SENSOR(flow)
  SUB_SENSOR(head)
  SUB_SENSOR(medium_temperature)
  SUB_SENSOR(voltage)
  SUB_SENSOR(current)
  SUB_SENSOR(power)
  SUB_SENSOR(speed)
  SUB_SENSOR(energy)
  SUB_SENSOR(operating_hours)
  SUB_SENSOR(starts)
  SUB_SENSOR(warning_code)
  SUB_SENSOR(alarm_code)
#endif
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
  SUB_TEXT_SENSOR(operating_mode)
  SUB_TEXT_SENSOR(control_source)
  SUB_TEXT_SENSOR(product_name)
  SUB_TEXT_SENSOR(product_number)
  SUB_TEXT_SENSOR(software_version)
  SUB_TEXT_SENSOR(bluetooth_software_version)
  SUB_TEXT_SENSOR(last_event)
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_BUTTON)
  SUB_BUTTON(start)
  SUB_BUTTON(stop)
  SUB_BUTTON(reset_alarm)
  SUB_BUTTON(start_air_venting)
  SUB_BUTTON(stop_air_venting)
  SUB_BUTTON(clear_alarm_log)
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SWITCH)
  SUB_SWITCH(automatic_night_setback)
#endif
#if defined(USE_GRUNDFOS_ALPHA_CLOCK) && defined(USE_SENSOR)
  SUB_SENSOR(clock_deviation)
#endif

 public:
  void update() override;
  void dump_config() override;
  void on_response(const genibus::GenibusResponse &response) override;
  void on_connected() override;
  void on_disconnected() override;
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SELECT)
  void set_select(SelectSetting setting, select::Select *entity) {
    this->selects_[static_cast<size_t>(setting)] = entity;
  }
  void write_select(SelectSetting setting, size_t index);
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_NUMBER)
  void set_number(NumberSetting setting, number::Number *entity) {
    this->numbers_[static_cast<size_t>(setting)] = entity;
  }
  void write_number(NumberSetting setting, float value);
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SWITCH)
  void write_automatic_night_setback(bool state);
#endif
#ifdef USE_GRUNDFOS_ALPHA_WRITE
  void send_command(Command command);
  /// Start or stop air venting, which runs the pump at high speed. The Grundfos GO app data give it a timeout (86/101,
  /// 1800 s); that the pump ends it by itself is not confirmed on a pump, so stop it when done.
  void write_air_venting(bool on);
#endif
#ifdef USE_GRUNDFOS_ALPHA_CLOCK
  void set_time_id(time::RealTimeClock *time) { this->time_ = time; }
  /// Set the pump's clock when it is off; without this the clock is only checked.
  void set_clock_writes(bool writes) { this->clock_writes_ = writes; }
#endif

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
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
  bool wants_info_(InfoStep step) const;
  bool request_info_(InfoStep step);
  void decode_info_(InfoStep step, const genibus::GenibusResponse &response);
  /// The class 10 object in a reply with status 0, checked against type, version and size.
  bool parse_event_object_(const genibus::GenibusResponse &response, uint16_t sub_id, uint16_t type, size_t size,
                           genibus::DataObject &object);
  void decode_event_log_(const genibus::GenibusResponse &response);
  void decode_event_(const genibus::GenibusResponse &response);
  /// Empty the last event sensor when it shows an event that may no longer be the latest.
  void clear_last_event_();
  text_sensor::TextSensor *text_sensor_(InfoStep step) const;
#endif
#ifdef USE_GRUNDFOS_ALPHA_WRITE
  /// Queue a write; a waiting write of the same setting is replaced. Returns false when it is refused.
  bool enqueue_(const WriteRequest &request, const LogString *what);
  /// Drop the first waiting write when its object had no valid read for WRITE_WAIT_CYCLES updates.
  void drop_stuck_write_();
  /// Start the next waiting write; false when none was started.
  bool start_job_();
  /// Whether the objects a write depends on were polled since the connection came up and since the last write or
  /// command.
  bool job_ready_(const WriteRequest &request) const;
  /// Whether any settings object is polled.
  bool polls_settings_() const;
  void continue_job_(const genibus::GenibusResponse &response);
  void end_job_(const LogString *failure);
  /// Set the requested value in job_body_.
  void apply_job_();
  /// Whether body equals job_body_ outside the value the job changes.
  bool same_outside_value_(std::span<const uint8_t> body) const;
  /// Whether a read-back confirms the write in job_body_.
  bool confirms_job_(std::span<const uint8_t> body) const;
  const SettingLayout &job_layout_() const;
  /// Why the job must not start now, or nullptr when it may.
  const LogString *job_refusal_() const;
#endif
#ifdef USE_GRUNDFOS_ALPHA_CLOCK
  /// Start the clock check when it is due and the time source is valid; false when it was not started.
  bool start_clock_();
  void continue_clock_(const genibus::GenibusResponse &response);
  /// The pump's clock in 94/101 minus the local time of the time source, in seconds; false when the reply holds no
  /// valid time.
  bool clock_deviation_(const genibus::GenibusResponse &response, int32_t &deviation);
  /// block_writes: the pump answered a write but the clock is still not right; only checks follow until restart.
  void end_clock_(const LogString *failure, bool block_writes);
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
#ifdef USE_SENSOR
  // The highest value published per counter, from READING_OPERATING_HOURS to READING_STARTS
  std::array<float,
             static_cast<size_t>(Reading::READING_STARTS) - static_cast<size_t>(Reading::READING_OPERATING_HOURS) + 1>
      totals_{NAN, NAN};
#endif
#if defined(USE_GRUNDFOS_ALPHA_SETTINGS) && defined(USE_TEXT_SENSOR)
  // The latest event of the log the last event sensor was set for; NO_EVENT_SHOWN before it was set
  uint32_t event_shown_{NO_EVENT_SHOWN};
  uint16_t event_wanted_{0};  // the latest event of the log, whose entry is read next; 0 for none
  uint16_t event_sub_id_{0};  // the object 88 entry that holds it
  uint8_t texts_read_{0};     // a bit per text read since the connection came up
  uint8_t texts_retried_{0};  // a bit per text whose reply did not fit once since the connection came up
  bool event_log_missing_{false};
#endif
#ifdef USE_GRUNDFOS_ALPHA_WRITE
#ifdef USE_SELECT
  std::array<select::Select *, static_cast<size_t>(SelectSetting::SELECT_SETTING_COUNT)> selects_{};
#endif
#ifdef USE_NUMBER
  std::array<number::Number *, static_cast<size_t>(NumberSetting::NUMBER_SETTING_COUNT)> numbers_{};
  // The set point limits the pump reports, in Pa or rpm
  std::array<float, static_cast<size_t>(NumberSetting::NUMBER_SETTING_COUNT)> min_{};
  std::array<float, static_cast<size_t>(NumberSetting::NUMBER_SETTING_COUNT)> max_{};
#endif
  // The body of each settings object at its last valid read, to check a read before a write against it
  std::array<std::array<uint8_t, MAX_WRITE_SIZE>, static_cast<size_t>(Setting::SETTING_COUNT)> last_body_{};
  std::array<WriteRequest, WRITE_QUEUE_SIZE> queue_{};
  WriteRequest job_{};
  std::array<uint8_t, MAX_WRITE_SIZE> job_body_{};  // the object as written
  uint16_t last_known_{0};                          // bit per Setting: last_body_ holds a read
  uint16_t job_type_{0};
  uint8_t job_version_{0};
  uint8_t job_size_{0};
  uint8_t queued_{0};
  uint8_t waited_{0};  // updates the first waiting write has not been ready
  JobPhase phase_{JobPhase::JOB_PHASE_NONE};
  bool reply_failed_{false};  // the last reply was an error, so a late answer may still come
  // The status warning is a failed write; it clears at the first valid settings read of a later poll cycle
  WriteWarning write_warning_{WriteWarning::WRITE_WARNING_NONE};
#endif
#ifdef USE_GRUNDFOS_ALPHA_CLOCK
  time::RealTimeClock *time_{nullptr};
  uint32_t clock_checked_at_{0};
  std::array<uint8_t, 10> clock_body_{};  // 94/100 as written
  ClockPhase clock_phase_{ClockPhase::CLOCK_PHASE_NONE};
  bool clock_due_{true};      // check at the first update with a valid time after a connection
  bool clock_polled_{false};  // a poll chain ended since the connection came up
  bool clock_writes_{false};
  bool clock_write_blocked_{false};
#endif
};

}  // namespace esphome::grundfos_alpha
