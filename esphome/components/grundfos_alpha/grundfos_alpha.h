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
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_SELECT)
#include "esphome/components/select/select.h"
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_NUMBER)
#include "esphome/components/number/number.h"
#endif
#if defined(USE_GRUNDFOS_ALPHA_WRITE) && defined(USE_BUTTON)
#include "esphome/components/button/button.h"
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
  SETTING_CONTROL_MODE,
  SETTING_OPERATING_MODE,
  SETTING_CONSTANT_PRESSURE_LIMITS,
  SETTING_CONSTANT_PRESSURE,
  SETTING_PROPORTIONAL_PRESSURE_LIMITS,
  SETTING_PROPORTIONAL_PRESSURE,
  SETTING_CONSTANT_SPEED_LIMITS,
  SETTING_CONSTANT_SPEED,
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
  NUMBER_SETTING_COUNT,
};

enum class JobPhase : uint8_t {
  JOB_PHASE_NONE,
  JOB_PHASE_READ,     // the object is read before it is changed
  JOB_PHASE_WRITE,    // the changed object is written
  JOB_PHASE_VERIFY,   // the object is read back
  JOB_PHASE_COMMAND,  // a class 3 command is on the wire
};

/// Class 3 commands the Grundfos GO app sends to an ALPHA3; the value is the command ID. A stop lasts through a loss
/// of power until a start.
enum class Command : uint8_t {
  COMMAND_NONE = 0,
  COMMAND_RESET_ALARM = 2,
  COMMAND_STOP = 5,
  COMMAND_START = 6,  // AUTO in the profile
};

// The largest settings object that is written (type 0x012E, 18 bytes)
static constexpr size_t MAX_WRITE_SIZE = 18;
// Writes that wait while one runs
static constexpr size_t WRITE_QUEUE_SIZE = 4;

enum class WriteWarning : uint8_t { WRITE_WARNING_NONE, WRITE_WARNING_SET, WRITE_WARNING_CLEAR_ON_READ };

/// One write: change one byte or one big-endian float of a settings object, or send a command (setting is then
/// SETTING_COUNT).
struct WriteRequest {
  Setting setting;
  uint8_t offset;  // of the value in the body
  uint8_t byte_value;
  bool is_float;
  float float_value;
  Command command;
};

/// The objects read from a pump of one unit family, type and version; settings is null for a model without them.
struct Model {
  uint8_t family;
  uint8_t type;
  uint8_t version;
  uint8_t name;  // index into ModelNames
  std::span<const ObjectLayout> objects;
  const SettingLayouts *settings;
  bool info;  // whether the pump has the class 7 texts of TEXT_IDS and the event log in object 88
};

// Values of current_ besides a step of the poll chain
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
#ifdef USE_GRUNDFOS_ALPHA_WRITE
  void send_command(Command command);
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
  uint16_t job_sub_id_() const;
  /// Why the job must not start now, or nullptr when it may.
  const LogString *job_refusal_() const;
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
  JobPhase phase_{JobPhase::JOB_PHASE_NONE};
  bool reply_failed_{false};  // the last reply was an error, so a late answer may still come
  // The status warning is a failed write; it clears at the first valid settings read of a later poll cycle
  WriteWarning write_warning_{WriteWarning::WRITE_WARNING_NONE};
#endif
};

}  // namespace esphome::grundfos_alpha
