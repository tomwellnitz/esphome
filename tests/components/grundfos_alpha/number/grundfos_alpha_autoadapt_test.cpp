#include "../select/select_fixture.h"

#include "esphome/components/number/number.h"

namespace esphome::grundfos_alpha::testing {

namespace {

class TestNumber : public number::Number {
 protected:
  void control(float value) override {}
};

constexpr auto RADIATOR = NumberSetting::NUMBER_SETTING_AUTOADAPT_RADIATOR;
constexpr auto UNDERFLOOR_SET_POINT = NumberSetting::NUMBER_SETTING_AUTOADAPT_UNDERFLOOR;
constexpr auto RADIATOR_AND_UNDERFLOOR = NumberSetting::NUMBER_SETTING_AUTOADAPT_RADIATOR_AND_UNDERFLOOR;

// The AutoAdapt objects as a real ALPHA3 sent them. Its limits replies are the same frames as those of constant
// pressure (86/21: 1.5 to 4.5 m) and proportional pressure (86/19, 86/23: 2 to 4.5 m).
const Bytes FRAME_GET_86_19 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x56, 0x00, 0x13, 0x87, 0xCE};
const Bytes FRAME_GET_86_20 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x56, 0x00, 0x14, 0xF7, 0x29};
const Bytes FRAME_GET_86_21 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x56, 0x00, 0x15, 0xE7, 0x08};
const Bytes FRAME_GET_86_22 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x56, 0x00, 0x16, 0xD7, 0x6B};
const Bytes FRAME_GET_86_23 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x56, 0x00, 0x17, 0xC7, 0x4A};
const Bytes FRAME_GET_86_24 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x56, 0x00, 0x18, 0x36, 0xA5};
// 86/10 in control mode 15 (radiator and underfloor heating), as a real ALPHA3 sent it
const Bytes REPLY_CONTROL_MODE_RADIATOR_AND_UNDERFLOOR = {0x24, 0x12, 0xF8, 0xE7, 0x0A, 0x0E, 0x00, 0x01,
                                                          0x2F, 0x01, 0x00, 0x00, 0x07, 0x00, 0x06, 0x0F,
                                                          0x7F, 0xFF, 0xFF, 0xFF, 0xD7, 0xEF};
const Bytes &REPLY_RADIATOR_LIMITS = REPLY_PP_LIMITS;
const Bytes &REPLY_UNDERFLOOR_LIMITS = REPLY_CP_LIMITS;
const Bytes &REPLY_RADIATOR_AND_UNDERFLOOR_LIMITS = REPLY_PP_LIMITS;
// 86/20: 29412 Pa (3 m)
const Bytes REPLY_RADIATOR_SETPOINT = {0x24, 0x1D, 0xF8, 0xE7, 0x0A, 0x19, 0x00, 0x01, 0x2E, 0x01, 0x00,
                                       0x00, 0x12, 0x00, 0x04, 0x46, 0xE5, 0xC8, 0x00, 0x3F, 0x00, 0x00,
                                       0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x59, 0x1B};
// 86/22: 16916.7 Pa
const Bytes REPLY_UNDERFLOOR_SETPOINT = {0x24, 0x1D, 0xF8, 0xE7, 0x0A, 0x19, 0x00, 0x01, 0x2E, 0x01, 0x00,
                                         0x00, 0x12, 0x00, 0x04, 0x46, 0x84, 0x29, 0x69, 0x3F, 0x00, 0x00,
                                         0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE3, 0x14};
// 86/22 set to 14706 Pa (1.5 m); built with the frame tool
const Bytes FRAME_SET_UNDERFLOOR_1_5 = {0x27, 0x1F, 0xE7, 0xF8, 0x0A, 0x9B, 0x56, 0x00, 0x16, 0x01, 0x2E, 0x01,
                                        0x00, 0x00, 0x12, 0x00, 0x04, 0x46, 0x65, 0xC8, 0x00, 0x3F, 0x00, 0x00,
                                        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x6A};
// A write of 86/24 from 20588.4 Pa back to 19608 Pa (2 m), as a real ALPHA3 took it (in control mode 15)
const Bytes REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT = {0x24, 0x1D, 0xF8, 0xE7, 0x0A, 0x19, 0x00, 0x01, 0x2E, 0x01, 0x00,
                                                      0x00, 0x12, 0x00, 0x04, 0x46, 0xA0, 0xD8, 0xCD, 0x3F, 0x00, 0x00,
                                                      0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x4F};
const Bytes FRAME_SET_RADIATOR_AND_UNDERFLOOR_2 = {
    0x27, 0x1F, 0xE7, 0xF8, 0x0A, 0x9B, 0x56, 0x00, 0x18, 0x01, 0x2E, 0x01, 0x00, 0x00, 0x12, 0x00, 0x04, 0x46,
    0x99, 0x30, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB7, 0xF1};
const Bytes REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT_2 = {
    0x24, 0x1D, 0xF8, 0xE7, 0x0A, 0x19, 0x00, 0x01, 0x2E, 0x01, 0x00, 0x00, 0x12, 0x00, 0x04, 0x46, 0x99,
    0x30, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD0, 0xE9};

}  // namespace

class AutoAdaptTest : public SelectTest {
 protected:
  // Identify the pump and read the control mode, the limits and the set point of radiator and underfloor heating
  void read_radiator_and_underfloor_(const Bytes &control_mode = REPLY_CONTROL_MODE_CP) {
    this->pump_.set_number(RADIATOR_AND_UNDERFLOOR, &this->number_);
    this->identify_();
    ASSERT_EQ(this->next_request_(), bundle({FRAME_GET_86_10, FRAME_GET_86_23, FRAME_GET_86_24}));
    this->reply_(bundle({control_mode, REPLY_RADIATOR_AND_UNDERFLOOR_LIMITS, REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT}));
    ASSERT_TRUE(this->next_request_().empty());
  }

  void write_2_until_set_() {
    this->pump_.write_number(RADIATOR_AND_UNDERFLOOR, 2.0f);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_24);
    this->reply_(REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT);
    ASSERT_EQ(this->next_request_(), FRAME_SET_RADIATOR_AND_UNDERFLOOR_2);
    this->reply_(REPLY_SET_ACK);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_24);
  }

  // A write in an AutoAdapt control mode is refused before anything is sent
  void expect_refused_in_(const Bytes &control_mode) {
    this->read_radiator_and_underfloor_(control_mode);
    this->pump_.write_number(RADIATOR_AND_UNDERFLOOR, 2.0f);
    EXPECT_TRUE(this->next_request_().empty());
    EXPECT_TRUE(this->pump_.status_has_warning());
    EXPECT_NEAR(this->number_.state, 20588.4f / 9804.0f, 1e-4f);
  }

  TestNumber number_;
};

TEST_F(AutoAdaptTest, ReadsLimitsOnceAndTheSetPointWithTheControlMode) {
  this->read_radiator_and_underfloor_();
  EXPECT_NEAR(this->number_.state, 20588.4f / 9804.0f, 1e-4f);
  this->pump_.update();
  EXPECT_EQ(this->next_request_(), bundle({FRAME_GET_86_10, FRAME_GET_86_24}));
}

TEST_F(AutoAdaptTest, WritesTheRadiatorAndUnderfloorSetPointInConstantPressure) {
  this->read_radiator_and_underfloor_();
  this->write_2_until_set_();
  this->reply_(REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT_2);
  EXPECT_FLOAT_EQ(this->number_.state, 2.0f);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(AutoAdaptTest, ReadBackWithTheOldValueIsWarned) {
  this->read_radiator_and_underfloor_();
  this->write_2_until_set_();
  this->reply_(REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_NEAR(this->number_.state, 20588.4f / 9804.0f, 1e-4f);
}

// Not written in any AutoAdapt mode, its own (15) or another (14)
TEST_F(AutoAdaptTest, RefusedInItsOwnAutoAdaptMode) {
  this->expect_refused_in_(REPLY_CONTROL_MODE_RADIATOR_AND_UNDERFLOOR);
}
TEST_F(AutoAdaptTest, RefusedInAnotherAutoAdaptMode) { this->expect_refused_in_(REPLY_CONTROL_MODE_UNDERFLOOR); }

// A write can change the control mode too, so the next write waits until it was read again
TEST_F(AutoAdaptTest, NextWriteWaitsForAFreshControlMode) {
  this->read_radiator_and_underfloor_();
  this->write_2_until_set_();
  this->pump_.write_number(RADIATOR_AND_UNDERFLOOR, 2.5f);
  this->reply_(REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT_2);
  ASSERT_EQ(this->next_request_(), bundle({FRAME_GET_86_10, FRAME_GET_86_24}));
  this->reply_(bundle({REPLY_CONTROL_MODE_UNDERFLOOR, REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT_2}));
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(AutoAdaptTest, WritesInProportionalPressure) {
  this->use_selects_();
  this->pump_.set_number(UNDERFLOOR_SET_POINT, &this->number_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), bundle({FRAME_GET_86_10, FRAME_GET_86_100, FRAME_GET_86_21, FRAME_GET_86_22}));
  this->reply_(
      bundle({REPLY_CONTROL_MODE_PP, REPLY_OPERATING_MODE_NORMAL, REPLY_UNDERFLOOR_LIMITS, REPLY_UNDERFLOOR_SETPOINT}));
  ASSERT_TRUE(this->next_request_().empty());
  this->pump_.write_number(UNDERFLOOR_SET_POINT, 1.5f);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_22);
  this->reply_(REPLY_UNDERFLOOR_SETPOINT);
  EXPECT_EQ(this->next_request_(), FRAME_SET_UNDERFLOOR_1_5);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

// The limits differ by mode: 1.5 m is allowed for underfloor heating but below the radiator range
TEST_F(AutoAdaptTest, UsesTheLimitsOfItsMode) {
  this->pump_.set_number(RADIATOR, &this->number_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), bundle({FRAME_GET_86_10, FRAME_GET_86_19, FRAME_GET_86_20}));
  this->reply_(bundle({REPLY_CONTROL_MODE_CP, REPLY_RADIATOR_LIMITS, REPLY_RADIATOR_SETPOINT}));
  EXPECT_FLOAT_EQ(this->number_.state, 3.0f);
  this->pump_.write_number(RADIATOR, 1.5f);
  this->pump_.write_number(RADIATOR, 4.6f);
  EXPECT_TRUE(this->next_request_().empty());
  this->pump_.write_number(RADIATOR, 2.0f);
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_20);
}

TEST_F(AutoAdaptTest, NoWriteBeforeTheLimits) {
  this->pump_.set_number(RADIATOR_AND_UNDERFLOOR, &this->number_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), bundle({FRAME_GET_86_10, FRAME_GET_86_23, FRAME_GET_86_24}));
  this->pump_.write_number(RADIATOR_AND_UNDERFLOOR, 2.0f);
  this->reply_(
      bundle({REPLY_CONTROL_MODE_CP, REPLY_RADIATOR_AND_UNDERFLOOR_LIMITS, REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT}));
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->wrote_(FRAME_SET_RADIATOR_AND_UNDERFLOOR_2));
}

// A late reply of another set point object (here 86/18, other PID values) must not be written to 86/24. The AutoAdapt
// objects of this pump differ only in the set point, so a late 86/22 reply would be written with the same bytes.
TEST_F(AutoAdaptTest, LateReplyOfAnotherSetPointIsNotWrittenBack) {
  this->read_radiator_and_underfloor_();
  this->pump_.write_number(RADIATOR_AND_UNDERFLOOR, 2.0f);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_24);
  this->reply_(REPLY_PP_SETPOINT);
  EXPECT_TRUE(this->pump_.status_has_warning());
  for (const auto &frame : this->bus_.written)
    EXPECT_NE(frame[5] & 0xC0, 0x80) << "a SET was sent";
}

// In proportional pressure the constant pressure set point is refused, the AutoAdapt one is written
TEST_F(AutoAdaptTest, OtherSetPointsKeepTheirControlModeCheck) {
  TestNumber constant_pressure;
  this->pump_.set_number(NumberSetting::NUMBER_SETTING_CONSTANT_PRESSURE, &constant_pressure);
  this->pump_.set_number(RADIATOR_AND_UNDERFLOOR, &this->number_);
  this->identify_();
  // With 86/23 the reply would have 117 bytes
  ASSERT_EQ(this->next_request_(), bundle({FRAME_GET_86_10, FRAME_GET_86_15, FRAME_GET_86_16}));
  this->reply_(bundle({REPLY_CONTROL_MODE_PP, REPLY_CP_LIMITS, REPLY_CP_SETPOINT}));
  ASSERT_EQ(this->next_request_(), bundle({FRAME_GET_86_23, FRAME_GET_86_24}));
  this->reply_(bundle({REPLY_RADIATOR_AND_UNDERFLOOR_LIMITS, REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT}));
  ASSERT_TRUE(this->next_request_().empty());
  this->pump_.write_number(NumberSetting::NUMBER_SETTING_CONSTANT_PRESSURE, 3.5f);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_TRUE(this->next_request_().empty());
  this->pump_.write_number(RADIATOR_AND_UNDERFLOOR, 2.0f);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_24);
  this->reply_(REPLY_RADIATOR_AND_UNDERFLOOR_SETPOINT);
  EXPECT_EQ(this->next_request_(), FRAME_SET_RADIATOR_AND_UNDERFLOOR_2);
}

TEST_F(AutoAdaptTest, UnknownOnDisconnect) {
  this->read_radiator_and_underfloor_();
  this->bus_.lose_connection();
  EXPECT_TRUE(std::isnan(this->number_.state));
}

}  // namespace esphome::grundfos_alpha::testing
