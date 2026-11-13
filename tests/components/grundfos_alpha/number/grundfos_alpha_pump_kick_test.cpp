#include "../common.h"

#include "esphome/components/number/number.h"

namespace esphome::grundfos_alpha::testing {

namespace {

class PumpKickNumber : public number::Number {
 protected:
  void control(float value) override {}
};

constexpr auto WAIT = NumberSetting::NUMBER_SETTING_PUMP_KICK_WAIT;
constexpr auto DURATION = NumberSetting::NUMBER_SETTING_PUMP_KICK_DURATION;

}  // namespace

class PumpKickTest : public PumpTest {
 protected:
  void SetUp() override {
    PumpTest::SetUp();
    // The ranges of number.py
    this->wait_.traits.set_min_value(1.0f);
    this->wait_.traits.set_max_value(168.0f);
    this->duration_.traits.set_min_value(10.0f);
    this->duration_.traits.set_max_value(600.0f);
  }

  // Identify the pump and answer the one read of both numbers: 25 h, 120 s
  void read_both_() {
    this->pump_.set_number(WAIT, &this->wait_);
    this->pump_.set_number(DURATION, &this->duration_);
    this->identify_();
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
    this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
    ASSERT_TRUE(this->next_request_().empty());
  }

  bool wrote_set_300_() const {
    return std::find(this->bus_.written.begin(), this->bus_.written.end(), FRAME_SET_PUMP_KICK_ON_25H_300S) !=
           this->bus_.written.end();
  }

  PumpKickNumber wait_, duration_;
};

TEST_F(PumpKickTest, ReadsTheWaitInHoursAndTheDurationInSeconds) {
  this->read_both_();
  EXPECT_FLOAT_EQ(this->wait_.state, 25.0f);
  EXPECT_FLOAT_EQ(this->duration_.state, 120.0f);
}

TEST_F(PumpKickTest, WritesTheDuration) {
  this->read_both_();
  this->pump_.write_number(DURATION, 60.0f);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  ASSERT_EQ(this->next_request_(), FRAME_SET_PUMP_KICK_ON_25H_60S);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_60S);
  EXPECT_FLOAT_EQ(this->duration_.state, 60.0f);
  EXPECT_FLOAT_EQ(this->wait_.state, 25.0f);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(PumpKickTest, WritesTheWaitInSeconds) {
  this->read_both_();
  this->pump_.write_number(WAIT, 48.0f);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  ASSERT_EQ(this->next_request_(), FRAME_SET_PUMP_KICK_ON_48H_120S);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_48H_120S);
  EXPECT_FLOAT_EQ(this->wait_.state, 48.0f);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(PumpKickTest, WritesOfTwoValuesOfOneObjectDoNotReplaceEachOther) {
  this->read_both_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);  // a poll is on the wire, so both writes wait
  this->pump_.write_number(WAIT, 25.0f);
  this->pump_.write_number(DURATION, 60.0f);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  // The wait is already set: read, not written
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  ASSERT_EQ(this->next_request_(), FRAME_SET_PUMP_KICK_ON_25H_60S);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_60S);
  EXPECT_FLOAT_EQ(this->duration_.state, 60.0f);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(PumpKickTest, ALaterWriteOfTheSameValueReplacesAWaitingOne) {
  this->read_both_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->pump_.write_number(DURATION, 300.0f);
  this->pump_.write_number(DURATION, 60.0f);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  ASSERT_EQ(this->next_request_(), FRAME_SET_PUMP_KICK_ON_25H_60S);
  EXPECT_FALSE(this->wrote_set_300_());
}

TEST_F(PumpKickTest, ValueThePumpDidNotKeepWarns) {
  this->read_both_();
  this->pump_.write_number(DURATION, 300.0f);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  ASSERT_EQ(this->next_request_(), FRAME_SET_PUMP_KICK_ON_25H_300S);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  EXPECT_FLOAT_EQ(this->duration_.state, 120.0f);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(PumpKickTest, ValueOutsideTheRangeIsNotWritten) {
  this->read_both_();
  this->pump_.write_number(WAIT, 169.0f);
  this->pump_.write_number(WAIT, 0.0f);
  this->pump_.write_number(DURATION, 5.0f);
  this->pump_.write_number(DURATION, NAN);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(PumpKickTest, NoWriteWithoutTheEntity) {
  this->pump_.set_number(WAIT, &this->wait_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  this->pump_.write_number(DURATION, 60.0f);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(PumpKickTest, AnotherVersionIsNeitherShownNorWritten) {
  this->pump_.set_number(DURATION, &this->duration_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_V1);
  EXPECT_FALSE(this->duration_.has_state());
  this->pump_.write_number(DURATION, 60.0f);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(PumpKickTest, UnknownAfterTheConnectionIsLost) {
  this->read_both_();
  this->bus_.lose_connection();
  EXPECT_TRUE(std::isnan(this->wait_.state));
  EXPECT_TRUE(std::isnan(this->duration_.state));
}

}  // namespace esphome::grundfos_alpha::testing
