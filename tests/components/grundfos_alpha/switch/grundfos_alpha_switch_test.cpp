#include "../select/select_fixture.h"

#include "esphome/components/switch/switch.h"

namespace esphome::grundfos_alpha::testing {

class TestSwitch : public switch_::Switch {
 protected:
  void write_state(bool state) override {}
};

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

}  // namespace esphome::grundfos_alpha::testing
