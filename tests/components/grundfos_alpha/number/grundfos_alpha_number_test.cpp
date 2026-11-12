#include "../select/select_fixture.h"

#include "esphome/components/number/number.h"

namespace esphome::grundfos_alpha::testing {

namespace {

class TestNumber : public number::Number {
 protected:
  void control(float value) override {}
};

constexpr auto CONSTANT_PRESSURE = NumberSetting::NUMBER_SETTING_CONSTANT_PRESSURE;
constexpr auto PROPORTIONAL_PRESSURE = NumberSetting::NUMBER_SETTING_PROPORTIONAL_PRESSURE;
constexpr auto CONSTANT_SPEED = NumberSetting::NUMBER_SETTING_CONSTANT_SPEED;

}  // namespace

class NumberTest : public SelectTest {
 protected:
  // Identify the pump in constant pressure, then read the limits (1.5 to 4.5 m) and the set point (39214 Pa)
  void read_constant_pressure_(const Bytes &control_mode = REPLY_CONTROL_MODE_CP) {
    this->pump_.set_number(CONSTANT_PRESSURE, &this->number_);
    this->identify_();
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
    this->reply_(control_mode);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_15);
    this->reply_(REPLY_CP_LIMITS);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
    this->reply_(REPLY_CP_SETPOINT);
    ASSERT_TRUE(this->next_request_().empty());
  }

  void write_3_5_until_set_() {
    this->pump_.write_number(CONSTANT_PRESSURE, 3.5f);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
    this->reply_(REPLY_CP_SETPOINT);
    ASSERT_EQ(this->next_request_(), FRAME_SET_CP_3_5);
  }

  TestNumber number_;
};

TEST_F(NumberTest, ReadsControlModeLimitsOnceAndTheSetPoint) {
  this->read_constant_pressure_();
  EXPECT_NEAR(this->number_.state, 39214.0f / 9804.0f, 1e-4f);
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CP);
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_16);
}

TEST_F(NumberTest, WritesOnlyTheConstantPressureSetPoint) {
  this->read_constant_pressure_();
  // feedback_id and the PID values go back as read
  this->write_3_5_until_set_();
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT_3_5);
  EXPECT_FLOAT_EQ(this->number_.state, 3.5f);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(NumberTest, WritesTheProportionalPressureSetPoint) {
  this->pump_.set_number(PROPORTIONAL_PRESSURE, &this->number_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_PP);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_17);
  this->reply_(REPLY_PP_LIMITS);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_18);
  this->reply_(REPLY_PP_SETPOINT);
  this->pump_.write_number(PROPORTIONAL_PRESSURE, 3.0f);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_18);
  this->reply_(REPLY_PP_SETPOINT);
  ASSERT_EQ(this->next_request_(), FRAME_SET_PP_3);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_18);
  this->reply_(REPLY_PP_SETPOINT_3);
  EXPECT_FLOAT_EQ(this->number_.state, 3.0f);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(NumberTest, WritesTheConstantSpeedSetPointInRpm) {
  this->pump_.set_number(CONSTANT_SPEED, &this->number_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_13);
  this->reply_(REPLY_CS_LIMITS);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_14);
  this->reply_(REPLY_CS_SETPOINT);
  EXPECT_FLOAT_EQ(this->number_.state, 5260.0f);
  this->pump_.write_number(CONSTANT_SPEED, 3000.0f);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_14);
  this->reply_(REPLY_CS_SETPOINT);
  ASSERT_EQ(this->next_request_(), FRAME_SET_CS_3000);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_14);
  this->reply_(REPLY_CS_SETPOINT_3000);
  EXPECT_FLOAT_EQ(this->number_.state, 3000.0f);
}

TEST_F(NumberTest, NoWriteOutsideItsControlMode) {
  this->read_constant_pressure_(REPLY_CONTROL_MODE_UNDERFLOOR);
  this->pump_.write_number(CONSTANT_PRESSURE, 3.5f);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(NumberTest, ControlModeWrittenJustBeforeCounts) {
  this->use_selects_();
  this->pump_.set_number(CONSTANT_PRESSURE, &this->number_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_15);
  this->reply_(REPLY_CP_LIMITS);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT);
  ASSERT_TRUE(this->next_request_().empty());
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);  // a poll is on the wire, so both writes wait
  this->pump_.write_select(CONTROL_MODE, 0);
  this->pump_.write_number(CONSTANT_PRESSURE, 3.5f);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_SET_CONTROL_MODE_CP);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CP);
  // Now in constant pressure: the set point follows once the poll has read its object again
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CP);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT);
  EXPECT_EQ(this->next_request_(), FRAME_SET_CP_3_5);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

// The pump would limit 4.51 m to 4.5 m without saying so and still acknowledge the write
TEST_F(NumberTest, LimitsAreExact) {
  this->read_constant_pressure_();
  this->pump_.write_number(CONSTANT_PRESSURE, 4.51f);
  this->pump_.write_number(CONSTANT_PRESSURE, 1.49f);
  EXPECT_TRUE(this->next_request_().empty());
  this->pump_.write_number(CONSTANT_PRESSURE, 4.5f);
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_16);
}

TEST_F(NumberTest, NanIsRefused) {
  this->read_constant_pressure_();
  this->pump_.write_number(CONSTANT_PRESSURE, NAN);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(NumberTest, NoWriteWithoutLimits) {
  this->pump_.set_number(CONSTANT_PRESSURE, &this->number_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CP);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_15);
  this->pump_.write_number(CONSTANT_PRESSURE, 3.5f);  // the limits are not read yet
  this->reply_(REPLY_CP_LIMITS_NAN);                  // and the pump reports none
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT);
  this->pump_.write_number(CONSTANT_PRESSURE, 3.5f);
  EXPECT_TRUE(this->next_request_().empty());
}

// 86/14, 86/16 and 86/18 have the same type, version and size: a late 86/14 reply must not be written to 86/16
TEST_F(NumberTest, LateReplyOfAnotherSetPointIsNotWrittenBack) {
  this->read_constant_pressure_();
  this->pump_.write_number(CONSTANT_PRESSURE, 3.5f);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CS_SETPOINT);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->wrote_(FRAME_SET_CP_3_5));
  for (const auto &frame : this->bus_.written)
    EXPECT_NE(frame[5] & 0xC0, 0x80) << "a SET was sent";
}

TEST_F(NumberTest, ReadBackWithTheOldValueIsWarned) {
  this->read_constant_pressure_();
  this->write_3_5_until_set_();
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_NEAR(this->number_.state, 39214.0f / 9804.0f, 1e-4f);
}

// The pump stores a set point exactly, so even one Pa more means it kept another value
TEST_F(NumberTest, ReadBackOffByOnePascalIsWarned) {
  this->read_constant_pressure_();
  this->write_3_5_until_set_();
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT_3_5_PLUS_1_PA);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(NumberTest, ReadBackWithAnotherFieldChangedIsWarned) {
  this->read_constant_pressure_();
  this->write_3_5_until_set_();
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT_3_5_FB5);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(NumberTest, WrongVersionOnTheReadIsNotWritten) {
  this->read_constant_pressure_();
  this->pump_.write_number(CONSTANT_PRESSURE, 3.5f);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_LIMITS);  // type 0x012D, 28 bytes
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->wrote_(FRAME_SET_CP_3_5));
}

TEST_F(NumberTest, SetPointWaitsForThePollsAfterReconnect) {
  this->read_constant_pressure_();
  this->bus_.lose_connection();
  this->bus_.establish_connection();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);  // the poll after connecting is on the wire
  this->pump_.write_number(CONSTANT_PRESSURE, 3.5f);  // the limits are kept, so it is queued
  this->reply_(REPLY_CONTROL_MODE_CP);
  // 86/16 first, then the write; no warning for the wait
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT);
  EXPECT_EQ(this->next_request_(), FRAME_SET_CP_3_5);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(NumberTest, SetPointWaitsForTheControlMode) {
  this->pump_.set_number(CONSTANT_PRESSURE, &this->number_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->run_(TIMEOUT_MS);  // the control mode is not known
  this->run_(QUIET_MS);
  ASSERT_EQ(this->bus_.written.back(), FRAME_GET_86_15);
  this->reply_(REPLY_CP_LIMITS);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);
  this->reply_(REPLY_CP_SETPOINT);
  this->pump_.write_number(CONSTANT_PRESSURE, 3.5f);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->pump_.status_has_warning());
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CP);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_16);  // the write
  this->reply_(REPLY_CP_SETPOINT);
  EXPECT_EQ(this->next_request_(), FRAME_SET_CP_3_5);
}

TEST_F(NumberTest, UnknownOnDisconnect) {
  this->read_constant_pressure_();
  this->bus_.lose_connection();
  EXPECT_TRUE(std::isnan(this->number_.state));
}

}  // namespace esphome::grundfos_alpha::testing
