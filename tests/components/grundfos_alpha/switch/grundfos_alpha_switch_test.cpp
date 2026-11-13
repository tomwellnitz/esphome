#include "../select/select_fixture.h"

#include "esphome/components/switch/switch.h"

namespace esphome::grundfos_alpha::testing {

namespace {

class TestSwitch : public switch_::Switch {
 protected:
  void write_state(bool state) override {}
};

constexpr auto PUMP_KICK = SwitchSetting::SWITCH_SETTING_PUMP_KICK;
constexpr auto CLOCK_PROGRAM = SwitchSetting::SWITCH_SETTING_CLOCK_PROGRAM;

}  // namespace

class SwitchTest : public SelectTest {
 protected:
  // Identify the pump and answer the read of the pump kick
  void read_pump_kick_(const Bytes &reply) {
    this->pump_.set_switch(PUMP_KICK, &this->pump_kick_);
    this->identify_();
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
    this->reply_(reply);
    ASSERT_TRUE(this->next_request_().empty());
  }

  // Turn the pump kick off from 25 h and 60 s and answer up to the SET
  void turn_off_until_set_() {
    this->read_pump_kick_(REPLY_PUMP_KICK_ON_25H_60S);
    this->pump_.write_switch(PUMP_KICK, false);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
    this->reply_(REPLY_PUMP_KICK_ON_25H_60S);
    ASSERT_EQ(this->next_request_(), FRAME_SET_PUMP_KICK_OFF_25H_60S);
  }

  TestSwitch pump_kick_, clock_program_;
};

TEST_F(SwitchTest, ReadsThePumpKick) {
  this->read_pump_kick_(REPLY_PUMP_KICK_ON_25H_120S);
  EXPECT_TRUE(this->pump_kick_.state);
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_OFF_25H_60S);
  EXPECT_FALSE(this->pump_kick_.state);
}

TEST_F(SwitchTest, NotPolledWithoutTheSwitches) {
  this->identify_();
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->wrote_(FRAME_GET_86_2));
  EXPECT_FALSE(this->wrote_(FRAME_GET_84_1));
}

TEST_F(SwitchTest, PollsBothObjectsInOrder) {
  this->pump_.set_switch(PUMP_KICK, &this->pump_kick_);
  this->pump_.set_switch(CLOCK_PROGRAM, &this->clock_program_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_ON);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_TRUE(this->pump_kick_.state);
  EXPECT_TRUE(this->clock_program_.state);
}

TEST_F(SwitchTest, TurnsThePumpKickOffAndReadsBack) {
  this->turn_off_until_set_();
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_OFF_25H_60S);
  EXPECT_FALSE(this->pump_kick_.state);
  EXPECT_FALSE(this->pump_.status_has_warning());
  // The refresh after the write
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_OFF_25H_60S);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(SwitchTest, TurnsThePumpKickOn) {
  this->read_pump_kick_(REPLY_PUMP_KICK_OFF_25H_60S);
  this->pump_.write_switch(PUMP_KICK, true);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_OFF_25H_60S);
  ASSERT_EQ(this->next_request_(), FRAME_SET_PUMP_KICK_ON_25H_60S);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_60S);
  EXPECT_TRUE(this->pump_kick_.state);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(SwitchTest, ChangesOnlyBitZero) {
  this->read_pump_kick_(REPLY_PUMP_KICK_BITS_03);
  EXPECT_TRUE(this->pump_kick_.state);
  this->pump_.write_switch(PUMP_KICK, false);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_BITS_03);
  ASSERT_EQ(this->next_request_(), FRAME_SET_PUMP_KICK_BITS_02);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_BITS_02);
  EXPECT_FALSE(this->pump_kick_.state);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(SwitchTest, OtherBitChangedSinceThePollIsNotWritten) {
  this->read_pump_kick_(REPLY_PUMP_KICK_ON_25H_120S);
  this->pump_.write_switch(PUMP_KICK, false);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_BITS_03);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->wrote_(FRAME_SET_PUMP_KICK_BITS_02));
}

TEST_F(SwitchTest, AlreadySetIsNotWritten) {
  this->read_pump_kick_(REPLY_PUMP_KICK_ON_25H_120S);
  this->pump_.write_switch(PUMP_KICK, true);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  // Only the refresh follows
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(SwitchTest, ObjectChangedSinceThePollIsNotWritten) {
  this->read_pump_kick_(REPLY_PUMP_KICK_ON_25H_60S);
  this->pump_.write_switch(PUMP_KICK, false);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_120S);  // the duration changed meanwhile
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->wrote_(FRAME_SET_PUMP_KICK_OFF_25H_60S));
}

TEST_F(SwitchTest, ReadBackWithTheOldValueWarns) {
  this->turn_off_until_set_();
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_60S);
  EXPECT_TRUE(this->pump_kick_.state);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(SwitchTest, BusyPumpWarnsWithoutReadBack) {
  this->turn_off_until_set_();
  this->reply_(REPLY_SET_BUSY);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_TRUE(this->pump_kick_.state);
  // The refresh, not a read-back
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_60S);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(SwitchTest, AnotherVersionIsNeitherShownNorWritten) {
  this->read_pump_kick_(REPLY_PUMP_KICK_V1);
  EXPECT_FALSE(this->pump_kick_.has_state());
  this->pump_.write_switch(PUMP_KICK, false);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(SwitchTest, NoWriteWithoutTheSwitch) {
  this->identify_();
  this->pump_.write_switch(PUMP_KICK, false);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(SwitchTest, TurnsTheClockProgramOnAndOff) {
  this->pump_.set_switch(CLOCK_PROGRAM, &this->clock_program_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_OFF);
  EXPECT_FALSE(this->clock_program_.state);
  this->pump_.write_switch(CLOCK_PROGRAM, true);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_OFF);
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK_PROGRAM_ON);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_ON);
  EXPECT_TRUE(this->clock_program_.state);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_ON);
  this->pump_.write_switch(CLOCK_PROGRAM, false);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_ON);
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK_PROGRAM_OFF);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_OFF);
  EXPECT_FALSE(this->clock_program_.state);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(SwitchTest, BothSwitchesQueueTheirWrites) {
  this->pump_.set_switch(PUMP_KICK, &this->pump_kick_);
  this->pump_.set_switch(CLOCK_PROGRAM, &this->clock_program_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_60S);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_OFF);
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);  // a poll is on the wire, so both writes wait
  this->pump_.write_switch(PUMP_KICK, false);
  this->pump_.write_switch(CLOCK_PROGRAM, true);
  this->reply_(REPLY_PUMP_KICK_ON_25H_60S);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_60S);
  ASSERT_EQ(this->next_request_(), FRAME_SET_PUMP_KICK_OFF_25H_60S);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_OFF_25H_60S);
  // A write can change other objects: the clock program waits until the poll has read 84/1 again
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_OFF_25H_60S);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_OFF);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_OFF);
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK_PROGRAM_ON);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_ON);
  EXPECT_FALSE(this->pump_kick_.state);
  EXPECT_TRUE(this->clock_program_.state);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

}  // namespace esphome::grundfos_alpha::testing
