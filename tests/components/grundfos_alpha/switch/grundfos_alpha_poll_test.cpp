#include "../select/select_fixture.h"

#include <array>
#include <map>
#include <utility>

#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"

namespace esphome::grundfos_alpha::testing {

namespace {

class PollNumber : public number::Number {
 protected:
  void control(float value) override {}
};

class PollSwitch : public switch_::Switch {
 protected:
  void write_state(bool state) override {}
};

using ObjectKey = std::pair<uint8_t, uint16_t>;

// Class 7 ACK 2 (ID unknown), as in the text sensor tests
const Bytes REPLY_TEXT_REFUSED = {0x24, 0x04, 0xF8, 0xE7, 0x07, 0x80, 0x50, 0xC5};

// The body sizes of the class 10 objects as the replies of a real ALPHA3 had them
const std::map<ObjectKey, size_t> BODY_SIZES = {
    {{93, 289}, 24}, {{87, 69}, 37}, {{87, 1}, 26},  {{93, 1}, 28},  {{88, 25}, 6},  {{86, 8}, 7},
    {{86, 10}, 7},   {{86, 100}, 4}, {{86, 15}, 28}, {{86, 16}, 18}, {{86, 17}, 28}, {{86, 18}, 18},
    {{86, 13}, 28},  {{86, 14}, 18}, {{86, 19}, 28}, {{86, 20}, 18}, {{86, 21}, 28}, {{86, 22}, 18},
    {{86, 23}, 28},  {{86, 24}, 18}, {{90, 8}, 1},   {{86, 2}, 7},   {{84, 1}, 10},  {{88, 10199}, 7},
};

/// The GETs of one request: class 10 objects, or the ID of one class 7 text.
struct Request {
  std::vector<ObjectKey> objects;
  std::vector<uint8_t> texts;
  size_t others{0};  // APDUs of any other class
};

Request parse_request(const Bytes &frame) {
  Request request;
  for (size_t i = 4; i + 2 <= frame.size() - 2;) {
    const uint8_t data_class = frame[i];
    const size_t length = frame[i + 1] & 0x3F;
    if (data_class == 10 && length == 3) {
      request.objects.emplace_back(frame[i + 2], encode_uint16(frame[i + 3], frame[i + 4]));
    } else if (data_class == 7 && length == 1) {
      request.texts.push_back(frame[i + 2]);
    } else {
      request.others++;
    }
    i += 2 + length;
  }
  return request;
}

}  // namespace

/// A pump with every entity: the poll requests must keep to the limits measured on a real ALPHA3.
class FullPollTest : public SelectTest {
 protected:
  void SetUp() override {
    SelectTest::SetUp();
    this->use_selects_();
    auto *s = this->sensors_.data();
    for (auto setter : {&GrundfosAlpha::set_flow_sensor, &GrundfosAlpha::set_head_sensor,
                        &GrundfosAlpha::set_medium_temperature_sensor, &GrundfosAlpha::set_voltage_sensor,
                        &GrundfosAlpha::set_current_sensor, &GrundfosAlpha::set_power_sensor,
                        &GrundfosAlpha::set_speed_sensor, &GrundfosAlpha::set_energy_sensor,
                        &GrundfosAlpha::set_operating_hours_sensor, &GrundfosAlpha::set_starts_sensor,
                        &GrundfosAlpha::set_warning_code_sensor, &GrundfosAlpha::set_alarm_code_sensor})
      (this->pump_.*setter)(s++);
    auto *t = this->texts_.data();
    for (auto setter :
         {&GrundfosAlpha::set_operating_mode_text_sensor, &GrundfosAlpha::set_control_source_text_sensor,
          &GrundfosAlpha::set_product_name_text_sensor, &GrundfosAlpha::set_product_number_text_sensor,
          &GrundfosAlpha::set_software_version_text_sensor, &GrundfosAlpha::set_bluetooth_software_version_text_sensor,
          &GrundfosAlpha::set_last_event_text_sensor})
      (this->pump_.*setter)(t++);
    for (size_t i = 0; i < this->numbers_.size(); i++)
      this->pump_.set_number(static_cast<NumberSetting>(i), &this->numbers_[i]);
    this->pump_.set_automatic_night_setback_switch(&this->switches_[0]);
    this->pump_.set_pump_kick_switch(&this->switches_[1]);
    this->pump_.set_clock_program_switch(&this->switches_[2]);
  }

  // Answer each request until the chain ends: a missing object for each class 10 GET, ACK 2 for a text
  std::vector<Request> poll_() {
    std::vector<Request> requests;
    for (Bytes frame = this->next_request_(); !frame.empty(); frame = this->next_request_()) {
      Request request = parse_request(frame);
      if (request.texts.empty()) {
        std::vector<Bytes> parts(request.objects.size(), REPLY_OBJECT_MISSING);
        Bytes reply(parts[0].begin(), parts[0].begin() + 4);
        for (const auto &part : parts)
          reply.insert(reply.end(), part.begin() + 4, part.end() - 2);
        reply[1] = static_cast<uint8_t>(reply.size() - 2);
        const uint16_t crc = genibus::frame_crc(&reply[1], reply.size() - 1);
        reply.push_back(static_cast<uint8_t>(crc >> 8));
        reply.push_back(static_cast<uint8_t>(crc));
        this->reply_(reply);
      } else {
        this->reply_(REPLY_TEXT_REFUSED);
      }
      requests.push_back(std::move(request));
      if (requests.size() > 50)
        break;
    }
    return requests;
  }

  std::array<sensor::Sensor, 12> sensors_;
  std::array<text_sensor::TextSensor, 7> texts_;
  std::array<PollNumber, static_cast<size_t>(NumberSetting::NUMBER_SETTING_COUNT)> numbers_;
  std::array<PollSwitch, 3> switches_;
};

TEST_F(FullPollTest, RequestsKeepToTheMeasuredLimits) {
  this->identify_();
  const auto requests = this->poll_();
  std::vector<ObjectKey> objects;
  std::vector<uint8_t> texts;
  for (const auto &request : requests) {
    EXPECT_EQ(request.others, 0u);
    if (!request.texts.empty()) {
      // A text goes alone
      EXPECT_EQ(request.texts.size(), 1u);
      EXPECT_TRUE(request.objects.empty());
      texts.push_back(request.texts[0]);
      continue;
    }
    ASSERT_FALSE(request.objects.empty());
    EXPECT_LE(request.objects.size(), MAX_POLL_OBJECTS);
    size_t reply = 0;
    for (const auto &key : request.objects) {
      ASSERT_TRUE(BODY_SIZES.contains(key)) << int(key.first) << "/" << key.second;
      reply += 2 + genibus::DATA_OBJECT_HEADER_SIZE + BODY_SIZES.at(key);
      objects.push_back(key);
    }
    EXPECT_LE(reply, MAX_POLL_REPLY);
  }
  // Every object once, in the order of the poll chain, the event log after the texts
  const std::vector<ObjectKey> expected = {
      {93, 289}, {87, 69}, {87, 1},  {93, 1},  {88, 25}, {86, 8},  {86, 10}, {86, 100},
      {86, 15},  {86, 16}, {86, 17}, {86, 18}, {86, 13}, {86, 14}, {86, 19}, {86, 20},
      {86, 21},  {86, 22}, {86, 23}, {86, 24}, {90, 8},  {86, 2},  {84, 1},  {88, 10199},
  };
  EXPECT_EQ(objects, expected);
  EXPECT_EQ(texts, (std::vector<uint8_t>{1, 8, 50, 58}));
  // The objects fill the requests as far as the limits allow: 7 class 10 requests, 4 texts, then the event log
  ASSERT_EQ(requests.size(), 12u);
  const std::vector<size_t> sizes = {2, 4, 4, 3, 3, 3, 4};
  for (size_t i = 0; i < sizes.size(); i++)
    EXPECT_EQ(requests[i].objects.size(), sizes[i]) << "request " << i;
  EXPECT_EQ(requests.back().objects, (std::vector<ObjectKey>{{88, 10199}}));
}

TEST_F(FullPollTest, NextCycleSkipsWhatThePumpDoesNotHave) {
  this->identify_();
  this->poll_();
  this->pump_.update();
  const auto requests = this->poll_();
  // The measurement objects, the texts and the event log were missing or refused; the settings are asked again
  for (const auto &request : requests) {
    EXPECT_TRUE(request.texts.empty());
    EXPECT_LE(request.objects.size(), MAX_POLL_OBJECTS);
    for (const auto &key : request.objects)
      EXPECT_TRUE(key.first == 86 || key.first == 90 || key.first == 84) << int(key.first) << "/" << key.second;
  }
  EXPECT_FALSE(requests.empty());
}

}  // namespace esphome::grundfos_alpha::testing
