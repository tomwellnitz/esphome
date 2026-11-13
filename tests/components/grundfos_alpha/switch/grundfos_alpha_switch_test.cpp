#include "../select/select_fixture.h"

#include "esphome/components/switch/switch.h"

namespace esphome::grundfos_alpha::testing {

namespace {

class TestSwitch : public switch_::Switch {
 protected:
  void write_state(bool state) override {}
};

}  // namespace

class NightSetbackTest : public SelectTest {
 protected:
  // Identify the pump and answer the read of the switch: off
  void read_switch_() {
    this->pump_.set_automatic_night_setback_switch(&this->night_setback_);
    this->identify_();
    ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
    this->reply_(REPLY_NIGHT_SETBACK_OFF);
    ASSERT_TRUE(this->next_request_().empty());
  }

  // Turn it on and answer up to the SET
  void turn_on_until_set_() {
    this->pump_.write_automatic_night_setback(true);
    ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
    this->reply_(REPLY_NIGHT_SETBACK_OFF);
    ASSERT_EQ(this->next_request_(), FRAME_SET_NIGHT_SETBACK_ON);
  }

  TestSwitch night_setback_;
};

TEST_F(NightSetbackTest, ReadsTheState) {
  this->read_switch_();
  EXPECT_FALSE(this->night_setback_.state);
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_ON);
  EXPECT_TRUE(this->night_setback_.state);
}

TEST_F(NightSetbackTest, NotPolledWithoutTheSwitch) {
  this->identify_();
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->wrote_(FRAME_GET_90_8));
}

TEST_F(NightSetbackTest, TurnsOnAndReadsBack) {
  this->read_switch_();
  this->turn_on_until_set_();
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_ON);
  EXPECT_TRUE(this->night_setback_.state);
  EXPECT_FALSE(this->pump_.status_has_warning());
  // The refresh after the write
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_ON);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(NightSetbackTest, TurnsOff) {
  this->pump_.set_automatic_night_setback_switch(&this->night_setback_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_ON);
  this->pump_.write_automatic_night_setback(false);
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_ON);
  ASSERT_EQ(this->next_request_(), FRAME_SET_NIGHT_SETBACK_OFF);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_OFF);
  EXPECT_FALSE(this->night_setback_.state);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(NightSetbackTest, AlreadySetIsNotWritten) {
  this->read_switch_();
  this->pump_.write_automatic_night_setback(false);
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_OFF);
  this->run_();
  EXPECT_FALSE(this->wrote_(FRAME_SET_NIGHT_SETBACK_OFF));
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(NightSetbackTest, KeptValueIsWarned) {
  this->read_switch_();
  this->turn_on_until_set_();
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_OFF);
  EXPECT_FALSE(this->night_setback_.state);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(NightSetbackTest, FailedSetIsWarned) {
  this->read_switch_();
  this->turn_on_until_set_();
  this->reply_(REPLY_SET_FAILED);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->night_setback_.state);
  // No read-back, only the refresh
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_OFF);
  EXPECT_TRUE(this->next_request_().empty());
}

// Another version of the object is neither shown nor written
TEST_F(NightSetbackTest, OtherVersionIsNotWritten) {
  this->read_switch_();
  this->pump_.write_automatic_night_setback(true);
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_ON_V2);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->night_setback_.state);
  this->run_();
  EXPECT_FALSE(this->wrote_(FRAME_SET_NIGHT_SETBACK_ON));
}

TEST_F(NightSetbackTest, WriteWaitsForThePoll) {
  this->pump_.set_automatic_night_setback_switch(&this->night_setback_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->pump_.write_automatic_night_setback(true);
  this->reply_(REPLY_NIGHT_SETBACK_OFF);
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_OFF);
  EXPECT_EQ(this->next_request_(), FRAME_SET_NIGHT_SETBACK_ON);
}

TEST_F(NightSetbackTest, RefusedBeforeThePumpIsKnown) {
  this->pump_.set_automatic_night_setback_switch(&this->night_setback_);
  this->pump_.write_automatic_night_setback(true);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_90_8);
  this->reply_(REPLY_NIGHT_SETBACK_OFF);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->wrote_(FRAME_SET_NIGHT_SETBACK_ON));
}

class SwitchTest : public SelectTest {
 protected:
  // Identify the pump and answer the read of the pump kick
  void read_pump_kick_(const Bytes &reply) {
    this->pump_.set_pump_kick_switch(&this->pump_kick_);
    this->identify_();
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
    this->reply_(reply);
    ASSERT_TRUE(this->next_request_().empty());
  }

  // Turn the pump kick off from 25 h and 60 s and answer up to the SET
  void turn_off_until_set_() {
    this->read_pump_kick_(REPLY_PUMP_KICK_ON_25H_60S);
    this->pump_.write_pump_kick(false);
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
  this->pump_.set_pump_kick_switch(&this->pump_kick_);
  this->pump_.set_clock_program_switch(&this->clock_program_);
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
  this->pump_.write_pump_kick(true);
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
  this->pump_.write_pump_kick(false);
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
  this->pump_.write_pump_kick(false);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_BITS_03);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->wrote_(FRAME_SET_PUMP_KICK_BITS_02));
}

TEST_F(SwitchTest, AlreadySetIsNotWritten) {
  this->read_pump_kick_(REPLY_PUMP_KICK_ON_25H_120S);
  this->pump_.write_pump_kick(true);
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
  this->pump_.write_pump_kick(false);
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
  this->pump_.write_pump_kick(false);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(SwitchTest, NoWriteWithoutTheSwitch) {
  this->identify_();
  this->pump_.write_pump_kick(false);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(SwitchTest, TurnsTheClockProgramOnAndOff) {
  this->pump_.set_clock_program_switch(&this->clock_program_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_OFF);
  EXPECT_FALSE(this->clock_program_.state);
  this->pump_.write_clock_program(true);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_OFF);
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK_PROGRAM_ON);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_ON);
  EXPECT_TRUE(this->clock_program_.state);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_ON);
  this->pump_.write_clock_program(false);
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
  this->pump_.set_pump_kick_switch(&this->pump_kick_);
  this->pump_.set_clock_program_switch(&this->clock_program_);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);
  this->reply_(REPLY_PUMP_KICK_ON_25H_60S);
  ASSERT_EQ(this->next_request_(), FRAME_GET_84_1);
  this->reply_(REPLY_CLOCK_PROGRAM_OFF);
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_2);  // a poll is on the wire, so both writes wait
  this->pump_.write_pump_kick(false);
  this->pump_.write_clock_program(true);
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
