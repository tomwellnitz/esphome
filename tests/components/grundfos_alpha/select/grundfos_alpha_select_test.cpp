#include "select_fixture.h"

#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::grundfos_alpha::testing {

TEST_F(SelectTest, ReadsBothSelects) {
  this->read_selects_();
  EXPECT_EQ(this->control_mode_.active_index(), UNDERFLOOR);
  EXPECT_EQ(this->operating_mode_.active_index(), 0u);
}

TEST_F(SelectTest, WritesControlModeAndReadsItBack) {
  this->read_selects_();
  this->write_curve_until_set_();
  EXPECT_EQ(this->control_mode_.active_index(), UNDERFLOOR);  // not before the read-back
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  EXPECT_EQ(this->control_mode_.active_index(), CONSTANT_CURVE);
  EXPECT_FALSE(this->pump_.status_has_warning());
  // Then the settings are read again
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_10);
}

TEST_F(SelectTest, WritesOperatingMode) {
  this->read_selects_();
  this->pump_.write_select(OPERATING_MODE, MAX);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  ASSERT_EQ(this->next_request_(), FRAME_SET_OPERATING_MODE_MAX);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_MAX);
  EXPECT_EQ(this->operating_mode_.active_index(), MAX);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

// The write is confirmed over 86/100, which the pump changes at once; 86/8 shows the mode in effect seconds later
TEST_F(SelectTest, OperatingModeIsConfirmedOverItsOwnObject) {
  text_sensor::TextSensor in_effect;
  this->pump_.set_operating_mode_text_sensor(&in_effect);
  this->use_selects_();
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_8);
  this->reply_(REPLY_REALIZED);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  this->pump_.write_select(OPERATING_MODE, MAX);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  ASSERT_EQ(this->next_request_(), FRAME_SET_OPERATING_MODE_MAX);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_MAX);
  EXPECT_FALSE(this->pump_.status_has_warning());
  // The refresh: 86/8 still says Normal, which neither undoes the select nor warns
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_8);
  this->reply_(REPLY_REALIZED);
  EXPECT_EQ(in_effect.state, "Normal");
  EXPECT_EQ(this->operating_mode_.active_index(), MAX);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(SelectTest, ValueAlreadySetIsNotWritten) {
  this->read_selects_();
  this->pump_.write_select(CONTROL_MODE, UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  // Straight to the refresh, no SET
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_10);
  EXPECT_EQ(this->control_mode_.active_index(), UNDERFLOOR);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(SelectTest, AckErrorEndsWrite) {
  this->read_selects_();
  this->write_curve_until_set_();
  this->reply_(REPLY_SET_ID_UNKNOWN);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_EQ(this->control_mode_.active_index(), UNDERFLOOR);
  // No read-back of a refused write; the refresh reads the settings
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
}

TEST_F(SelectTest, BusyStatusEndsWrite) { this->expect_refused_(REPLY_SET_BUSY); }
TEST_F(SelectTest, FailedStatusEndsWrite) { this->expect_refused_(REPLY_SET_FAILED); }
TEST_F(SelectTest, AckWithoutStatusEndsWrite) { this->expect_refused_(REPLY_SET_NO_STATUS); }

TEST_F(SelectTest, WarningClearsOnAValidReadOfTheNextCycle) {
  this->read_selects_();
  this->write_curve_until_set_();
  this->reply_(REPLY_SET_BUSY);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  EXPECT_TRUE(this->pump_.status_has_warning());  // the refresh right after the write keeps it
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(SelectTest, UnexpectedVersionIsNotWritten) {
  this->read_selects_();
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_V2);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_10);  // the refresh
  EXPECT_FALSE(this->wrote_(FRAME_SET_CONTROL_MODE_CURVE));
}

// 86/8 and 86/10 have the same type, version and size: a late 86/8 reply taken for the read must not be written back
TEST_F(SelectTest, LateReplyOfAnotherObjectIsNotWrittenBack) {
  this->read_selects_();
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_REALIZED);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_10);  // the refresh
  for (const auto &frame : this->bus_.written)
    EXPECT_NE(frame[5] & 0xC0, 0x80) << "a SET was sent";
}

TEST_F(SelectTest, ObjectChangedSinceTheLastPollIsNotWritten) {
  this->read_selects_();
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR_SOURCE_1);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->wrote_(FRAME_SET_CONTROL_MODE_CURVE));
}

TEST_F(SelectTest, ReadBackMustMatchTheWholeObject) {
  this->read_selects_();
  this->write_curve_until_set_();
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  // The control mode is right, but another byte changed
  this->reply_(REPLY_CONTROL_MODE_CURVE_SOURCE_1);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(SelectTest, ReadBackThatDiffersIsShownAndWarned) {
  this->read_selects_();
  this->write_curve_until_set_();
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  EXPECT_EQ(this->control_mode_.active_index(), UNDERFLOOR);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(SelectTest, TimeoutOfTheWriteEndsIt) {
  this->read_selects_();
  this->write_curve_until_set_();
  this->run_(TIMEOUT_MS);
  EXPECT_TRUE(this->pump_.status_has_warning());
  // No read-back and no refresh: a late answer may still come; the next update() goes on
  this->run_(QUIET_MS);
  EXPECT_EQ(this->bus_.written.back(), FRAME_SET_CONTROL_MODE_CURVE);
  this->pump_.update();
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_10);
}

TEST_F(SelectTest, TimeoutOfTheReadEndsTheWriteWithoutChaining) {
  this->read_selects_();
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->run_(TIMEOUT_MS);
  EXPECT_TRUE(this->pump_.status_has_warning());
  this->run_(QUIET_MS);
  EXPECT_EQ(this->bus_.written.back(), FRAME_GET_86_10);
  EXPECT_FALSE(this->wrote_(FRAME_SET_CONTROL_MODE_CURVE));
}

// GENIbus replies carry no object ID: a late 86/8 reply taken for the 86/10 read would shift every later answer by one
// object. The bus drops it in the quiet period after the timeout, so each object still gets its own reply.
TEST_F(SelectTest, LateReplyAfterATimeoutIsNotTakenForTheNextObject) {
  text_sensor::TextSensor in_effect;
  this->pump_.set_operating_mode_text_sensor(&in_effect);
  this->use_selects_();
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_8);
  this->run_(TIMEOUT_MS);
  this->reply_(REPLY_REALIZED);  // control mode 14, underfloor heating
  this->run_(QUIET_MS);
  ASSERT_EQ(this->bus_.written.back(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_MAX);
  EXPECT_FALSE(in_effect.has_state());
  EXPECT_EQ(this->control_mode_.active_index(), CONSTANT_CURVE);
  EXPECT_EQ(this->operating_mode_.active_index(), MAX);
  EXPECT_TRUE(this->next_request_().empty());
}

// A reply later than the quiet period cannot be told apart: 86/8 is taken for 86/10 and the real 86/10 reply for
// 86/100, which its type refuses. The wrong control mode shows until the next poll and is never written back.
TEST_F(SelectTest, ReplyAfterTheQuietPeriodShiftsOnlyUntilTheNextPoll) {
  text_sensor::TextSensor in_effect;
  this->pump_.set_operating_mode_text_sensor(&in_effect);
  this->use_selects_();
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_8);
  this->run_(TIMEOUT_MS);
  this->run_(QUIET_MS);
  ASSERT_EQ(this->bus_.written.back(), FRAME_GET_86_10);
  this->reply_(REPLY_REALIZED);  // control mode 14, underfloor heating
  EXPECT_EQ(this->control_mode_.active_index(), UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  EXPECT_FALSE(this->operating_mode_.has_state());
  EXPECT_TRUE(this->next_request_().empty());
  // The read before the write differs from the shifted poll: no SET
  this->pump_.write_select(CONTROL_MODE, 0);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->wrote_(FRAME_SET_CONTROL_MODE_CP));
  // The refresh after the failed write shows the real control mode
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_8);
  this->reply_(REPLY_REALIZED);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  EXPECT_EQ(this->control_mode_.active_index(), CONSTANT_CURVE);
}

// A failed reply may still be answered late; a write waits for the next clean reply or update()
TEST_F(SelectTest, WriteWaitsForACleanReplyAfterAFailedOne) {
  this->read_selects_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  this->run_(TIMEOUT_MS);
  // The poll chain goes on; the write does not start
  this->run_(QUIET_MS);
  ASSERT_EQ(this->bus_.written.back(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  EXPECT_EQ(this->next_request_(), FRAME_SET_CONTROL_MODE_CURVE);
}

TEST_F(SelectTest, WriteStartsFromUpdate) {
  this->read_selects_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  this->run_(TIMEOUT_MS);  // 86/10 times out
  this->run_(QUIET_MS);
  ASSERT_EQ(this->bus_.written.back(), FRAME_GET_86_100);
  this->run_(TIMEOUT_MS);  // 86/100 times out too; the chain ends
  this->run_(QUIET_MS);
  EXPECT_EQ(this->bus_.written.back(), FRAME_GET_86_100);
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  EXPECT_EQ(this->next_request_(), FRAME_SET_CONTROL_MODE_CURVE);
}

TEST_F(SelectTest, DisconnectDuringWriteEndsIt) {
  this->read_selects_();
  this->write_curve_until_set_();
  this->pump_.write_select(OPERATING_MODE, MAX);  // waits
  this->bus_.lose_connection();
  EXPECT_TRUE(this->pump_.status_has_warning());
  this->bus_.establish_connection();
  // Reading starts over; nothing of the old writes is repeated
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_TRUE(this->next_request_().empty());
}

// A control mode write also changes 86/100 byte 2, so the waiting operating mode write waits for a fresh poll of it
TEST_F(SelectTest, WritesQueueInOrder) {
  this->read_selects_();
  this->write_curve_until_set_();
  this->pump_.write_select(OPERATING_MODE, MAX);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL_CURVE);
  // The waiting write comes next
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_SET_OPERATING_MODE_MAX_CURVE);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_MAX_CURVE);
  EXPECT_EQ(this->operating_mode_.active_index(), MAX);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

// 86/100 byte 2 still shows the old requested control mode: a SET would write it back
TEST_F(SelectTest, OperatingModeWaitsForTheRequestedControlMode) {
  this->read_selects_();
  this->write_curve_until_set_();
  this->pump_.write_select(OPERATING_MODE, MAX);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);  // still underfloor heating
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->wrote_(FRAME_SET_OPERATING_MODE_MAX));
  EXPECT_FALSE(this->wrote_(FRAME_SET_OPERATING_MODE_MAX_CURVE));
}

// In Max 86/100 byte 2 keeps the requested control mode, so the SET writes it back unchanged
TEST_F(SelectTest, OperatingModeWritesFromMaxWithTheRequestedControlMode) {
  this->read_selects_();
  this->pump_.write_select(OPERATING_MODE, MAX);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  ASSERT_EQ(this->next_request_(), FRAME_SET_OPERATING_MODE_MAX);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_MAX);
  EXPECT_EQ(this->operating_mode_.active_index(), MAX);
  // The refresh after the write
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_MAX);
  this->pump_.write_select(OPERATING_MODE, 0);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_MAX);
  ASSERT_EQ(this->next_request_(), FRAME_SET_OPERATING_MODE_NORMAL);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_EQ(this->operating_mode_.active_index(), 0u);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

// The check holds in Max too: 86/100 byte 2 apart from 86/10 byte 2 would write another control mode
TEST_F(SelectTest, OperatingModeInMaxWaitsForTheRequestedControlMode) {
  this->use_selects_();
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_MAX_CURVE);
  ASSERT_TRUE(this->next_request_().empty());
  this->pump_.write_select(OPERATING_MODE, 0);
  EXPECT_TRUE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->wrote_(FRAME_SET_OPERATING_MODE_NORMAL));
}

TEST_F(SelectTest, WaitingWriteOfTheSameSettingIsReplaced) {
  this->read_selects_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);  // a poll is on the wire
  this->pump_.write_select(CONTROL_MODE, 0);
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  // The write goes before the rest of the poll chain
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_SET_CONTROL_MODE_CURVE);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  // No second write
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_10);
}

TEST_F(SelectTest, NoWriteBeforeThePumpIsIdentified) {
  this->use_selects_();
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  EXPECT_TRUE(this->next_request_().empty());
  // The refused write is not kept for later
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(SelectTest, NoWriteWhileDisconnected) {
  this->read_selects_();
  this->bus_.lose_connection();
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  this->bus_.connected = true;
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(SelectTest, WriteWaitsUntilTheObjectWasPolledAfterReconnect) {
  this->read_selects_();
  this->bus_.lose_connection();
  this->bus_.establish_connection();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);  // the poll after connecting is on the wire
  this->pump_.write_select(OPERATING_MODE, MAX);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  // 86/100 was not polled since the connection came back: the poll first, then the write
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_EQ(this->next_request_(), FRAME_SET_OPERATING_MODE_MAX);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(SelectTest, WriteWaitsWhenTheFirstPollFailed) {
  this->use_selects_();
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  this->run_(TIMEOUT_MS);  // the first poll of 86/10 times out
  this->run_(QUIET_MS);
  ASSERT_EQ(this->bus_.written.back(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->pump_.status_has_warning());
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);  // now the write
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  EXPECT_EQ(this->next_request_(), FRAME_SET_CONTROL_MODE_CURVE);
}

TEST_F(SelectTest, WriteWithoutAValidReadIsDroppedAfterSomeUpdates) {
  this->use_selects_();
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_V2);  // never a valid read of 86/10
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  for (uint8_t i = 1; i < WRITE_WAIT_CYCLES; i++) {
    this->pump_.update();
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
    this->reply_(REPLY_CONTROL_MODE_V2);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
    this->reply_(REPLY_OPERATING_MODE_NORMAL);
  }
  EXPECT_FALSE(this->pump_.status_has_warning());
  this->pump_.update();
  EXPECT_TRUE(this->pump_.status_has_warning());
  // The write is gone: a valid read of 86/10 now starts nothing
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->wrote_(FRAME_SET_CONTROL_MODE_CURVE));
}

TEST_F(SelectTest, UnsupportedModelOffersNoWrite) {
  this->use_selects_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_ALPHA15);
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  EXPECT_TRUE(this->next_request_().empty());
}

}  // namespace esphome::grundfos_alpha::testing
