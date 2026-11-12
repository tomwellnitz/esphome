#include <gtest/gtest.h>

#include <cmath>

#include "../common.h"

namespace esphome::grundfos_alpha::testing {

// Requests for 87/1, 93/1 and 88/25 and the replies of a real ALPHA3, unless a comment says otherwise
inline const Bytes FRAME_ENERGY = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x57, 0x00, 0x01, 0x82, 0x8D};
inline const Bytes FRAME_HISTORY = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x5D, 0x00, 0x01, 0x45, 0x4C};
inline const Bytes FRAME_FAULT = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x58, 0x00, 0x19, 0x3D, 0x85};
// 87/1 type 0x00E8 v1, 26 bytes: 235705441.4 Ws, 6.34 W, one pump with the same values
inline const Bytes REPLY_ENERGY = {0x24, 0x25, 0xF8, 0xE7, 0x0A, 0x21, 0x00, 0x00, 0xE8, 0x01, 0x00, 0x00, 0x1A, 0x41,
                                   0xAC, 0x19, 0x28, 0xC2, 0xCC, 0x58, 0xC0, 0x40, 0xCA, 0xDF, 0x00, 0x00, 0x01, 0x40,
                                   0xCA, 0xDF, 0x00, 0x41, 0xAC, 0x19, 0x28, 0xC2, 0xCC, 0x58, 0xC0, 0x3F, 0x83};
// 93/1 type 0x00F8 v2, 28 bytes: 1849 starts, operating time 33645002 s, power on time 33645059 s
inline const Bytes REPLY_HISTORY = {0x24, 0x27, 0xF8, 0xE7, 0x0A, 0x23, 0x00, 0x00, 0xF8, 0x02, 0x00,
                                    0x00, 0x1C, 0x00, 0x00, 0x07, 0x39, 0x00, 0x01, 0x00, 0x01, 0x02,
                                    0x01, 0x61, 0xCA, 0x00, 0x00, 0x0B, 0xF3, 0x00, 0x00, 0x0B, 0xF3,
                                    0x00, 0x00, 0x07, 0x36, 0x02, 0x01, 0x62, 0x03, 0xC4, 0x05};
// The same body with version 1 in the header (test vector)
inline const Bytes REPLY_HISTORY_V1 = {0x24, 0x27, 0xF8, 0xE7, 0x0A, 0x23, 0x00, 0x00, 0xF8, 0x01, 0x00,
                                       0x00, 0x1C, 0x00, 0x00, 0x07, 0x39, 0x00, 0x01, 0x00, 0x01, 0x02,
                                       0x01, 0x61, 0xCA, 0x00, 0x00, 0x0B, 0xF3, 0x00, 0x00, 0x0B, 0xF3,
                                       0x00, 0x00, 0x07, 0x36, 0x02, 0x01, 0x62, 0x03, 0x2F, 0xCF};
// 88/25 type 0x010A v1, 6 bytes: no warning, no alarm
inline const Bytes REPLY_FAULT_NONE = {0x24, 0x11, 0xF8, 0xE7, 0x0A, 0x0D, 0x00, 0x01, 0x0A, 0x01, 0x00,
                                       0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7, 0x4C};
// Test vector: warning 21 (too many starts per hour), alarm 57 (dry running), highest 57
inline const Bytes REPLY_FAULT_ACTIVE = {0x24, 0x11, 0xF8, 0xE7, 0x0A, 0x0D, 0x00, 0x01, 0x0A, 0x01, 0x00,
                                         0x00, 0x06, 0x00, 0x15, 0x00, 0x39, 0x00, 0x39, 0x2C, 0x0F};

class GrundfosAlphaDiagnosticsTest : public PumpTest {
 protected:
  void SetUp() override {
    PumpTest::SetUp();
    this->pump_.set_energy_sensor(&this->energy_);
    this->pump_.set_operating_hours_sensor(&this->operating_hours_);
    this->pump_.set_starts_sensor(&this->starts_);
    this->pump_.set_warning_code_sensor(&this->warning_code_);
    this->pump_.set_alarm_code_sensor(&this->alarm_code_);
  }

  void read_all_(const Bytes &fault = REPLY_FAULT_NONE) {
    this->identify_();
    ASSERT_EQ(this->next_request_(), FRAME_ENERGY);
    this->reply_(REPLY_ENERGY);
    ASSERT_EQ(this->next_request_(), FRAME_HISTORY);
    this->reply_(REPLY_HISTORY);
    ASSERT_EQ(this->next_request_(), FRAME_FAULT);
    this->reply_(fault);
  }

  sensor::Sensor energy_, operating_hours_, starts_, warning_code_, alarm_code_;
};

TEST_F(GrundfosAlphaDiagnosticsTest, DecodesRealReplies) {
  this->read_all_();
  EXPECT_NEAR(this->energy_.state, 65.4737f, 0.0005f);  // kWh, as the app shows
  EXPECT_NEAR(this->operating_hours_.state, 33645002.0f / 3600.0f, 0.01f);
  EXPECT_FLOAT_EQ(this->starts_.state, 1849.0f);
  EXPECT_FLOAT_EQ(this->warning_code_.state, 0.0f);
  EXPECT_FLOAT_EQ(this->alarm_code_.state, 0.0f);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(GrundfosAlphaDiagnosticsTest, DecodesActiveFaultCodes) {
  this->read_all_(REPLY_FAULT_ACTIVE);
  EXPECT_FLOAT_EQ(this->warning_code_.state, 21.0f);
  EXPECT_FLOAT_EQ(this->alarm_code_.state, 57.0f);
}

TEST_F(GrundfosAlphaDiagnosticsTest, ReadsOnlyObjectsWithSensors) {
  this->pump_.set_energy_sensor(nullptr);
  this->pump_.set_operating_hours_sensor(nullptr);
  this->pump_.set_warning_code_sensor(nullptr);
  this->pump_.set_alarm_code_sensor(nullptr);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_HISTORY);
  this->reply_(REPLY_HISTORY);
  EXPECT_FLOAT_EQ(this->starts_.state, 1849.0f);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(GrundfosAlphaDiagnosticsTest, OneCodeSensorReadsTheFaultObjectOnly) {
  this->pump_.set_energy_sensor(nullptr);
  this->pump_.set_operating_hours_sensor(nullptr);
  this->pump_.set_starts_sensor(nullptr);
  this->pump_.set_warning_code_sensor(nullptr);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_FAULT);
  this->reply_(REPLY_FAULT_ACTIVE);
  EXPECT_FLOAT_EQ(this->alarm_code_.state, 57.0f);
  EXPECT_FALSE(this->warning_code_.has_state());
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(GrundfosAlphaDiagnosticsTest, NoDiagnosticRequestsWithoutTheirSensors) {
  for (auto setter : {&GrundfosAlpha::set_energy_sensor, &GrundfosAlpha::set_operating_hours_sensor,
                      &GrundfosAlpha::set_starts_sensor, &GrundfosAlpha::set_warning_code_sensor,
                      &GrundfosAlpha::set_alarm_code_sensor})
    (this->pump_.*setter)(nullptr);
  sensor::Sensor power;
  this->pump_.set_power_sensor(&power);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_MOTOR);
  this->reply_(REPLY_MOTOR);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(GrundfosAlphaDiagnosticsTest, FollowsTheMeasurementsInOnePoll) {
  sensor::Sensor flow, power;
  this->pump_.set_flow_sensor(&flow);
  this->pump_.set_power_sensor(&power);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_PROCESS);
  this->reply_(REPLY_PROCESS);
  ASSERT_EQ(this->next_request_(), FRAME_MOTOR);
  this->reply_(REPLY_MOTOR);
  ASSERT_EQ(this->next_request_(), FRAME_ENERGY);
  this->reply_(REPLY_ENERGY);
  ASSERT_EQ(this->next_request_(), FRAME_HISTORY);
  this->reply_(REPLY_HISTORY);
  ASSERT_EQ(this->next_request_(), FRAME_FAULT);
  this->reply_(REPLY_FAULT_NONE);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FLOAT_EQ(power.state, 19.5f);
  EXPECT_FLOAT_EQ(this->starts_.state, 1849.0f);
}

TEST_F(GrundfosAlphaDiagnosticsTest, UnexpectedVersionGivesNanAndChainGoesOn) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_ENERGY);
  this->reply_(REPLY_ENERGY);
  ASSERT_EQ(this->next_request_(), FRAME_HISTORY);
  this->reply_(REPLY_HISTORY_V1);
  EXPECT_TRUE(this->starts_.has_state());
  EXPECT_TRUE(std::isnan(this->starts_.state));
  EXPECT_TRUE(std::isnan(this->operating_hours_.state));
  ASSERT_EQ(this->next_request_(), FRAME_FAULT);
  this->reply_(REPLY_FAULT_NONE);
  EXPECT_FLOAT_EQ(this->alarm_code_.state, 0.0f);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(GrundfosAlphaDiagnosticsTest, ErrorAckGivesNan) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_ENERGY);
  this->reply_(REPLY_PROCESS_ID_UNKNOWN);
  EXPECT_TRUE(std::isnan(this->energy_.state));
  ASSERT_EQ(this->next_request_(), FRAME_HISTORY);
}

TEST_F(GrundfosAlphaDiagnosticsTest, MissingObjectIsSkippedUntilReconnect) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_ENERGY);
  this->reply_(REPLY_OBJECT_MISSING);
  EXPECT_TRUE(std::isnan(this->energy_.state));
  ASSERT_EQ(this->next_request_(), FRAME_HISTORY);
  this->reply_(REPLY_HISTORY);
  ASSERT_EQ(this->next_request_(), FRAME_FAULT);
  this->reply_(REPLY_FAULT_NONE);
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_HISTORY);
  this->reply_(REPLY_HISTORY);
  ASSERT_EQ(this->next_request_(), FRAME_FAULT);
  this->reply_(REPLY_FAULT_NONE);
  EXPECT_TRUE(this->next_request_().empty());
  this->bus_.lose_connection();
  this->bus_.establish_connection();
  EXPECT_EQ(this->next_request_(), FRAME_ENERGY);
}

TEST_F(GrundfosAlphaDiagnosticsTest, DisconnectGivesNan) {
  this->read_all_(REPLY_FAULT_ACTIVE);
  this->bus_.lose_connection();
  for (auto *sensor :
       {&this->energy_, &this->operating_hours_, &this->starts_, &this->warning_code_, &this->alarm_code_})
    EXPECT_TRUE(std::isnan(sensor->state));
}

}  // namespace esphome::grundfos_alpha::testing
