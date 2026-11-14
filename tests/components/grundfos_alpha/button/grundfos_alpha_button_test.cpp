#include "../select/select_fixture.h"

namespace esphome::grundfos_alpha::testing {

using CommandTest = SelectTest;

// 86/100 in Stop with control mode 2 (constant curve) requested
static const Bytes REPLY_OPERATING_MODE_STOP_CURVE = {0x24, 0x0F, 0xF8, 0xE7, 0x0A, 0x0B, 0x00, 0x01, 0x54, 0x01,
                                                      0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x01, 0x18, 0xA7};

TEST_F(CommandTest, SendsTheCommands) {
  this->identify_();
  EXPECT_TRUE(this->next_request_().empty());
  // Start and stop are read back over 86/100; an alarm reset has nothing to read back
  this->pump_.send_command(Command::COMMAND_START);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_6);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_FALSE(this->pump_.status_has_warning());
  this->pump_.send_command(Command::COMMAND_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_STOP);
  EXPECT_FALSE(this->pump_.status_has_warning());
  this->pump_.send_command(Command::COMMAND_RESET_ALARM);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_2);
  this->reply_(REPLY_COMMAND_ACK);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->pump_.status_has_warning());
  // Clearing the alarm log has nothing to read back either
  this->pump_.send_command(Command::COMMAND_RESET_ALARM_LOG);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_51);
  this->reply_(REPLY_COMMAND_ACK);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->pump_.status_has_warning());
}

// The acknowledgement alone proves nothing: a stop that 86/100 does not show is warned
TEST_F(CommandTest, StopThatDoesNotShowIsWarned) {
  this->identify_();
  this->pump_.send_command(Command::COMMAND_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(CommandTest, StartThatDoesNotShowIsWarned) {
  this->identify_();
  this->pump_.send_command(Command::COMMAND_START);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_6);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_STOP);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(CommandTest, CommandThatIsNotSentIsWarned) {
  this->identify_();
  this->bus_.write_ok = false;
  this->pump_.send_command(Command::COMMAND_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  EXPECT_TRUE(this->pump_.status_has_warning());
  // No read-back and no second try
  this->bus_.write_ok = true;
  this->run_(QUIET_MS);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_EQ(std::count(this->bus_.written.begin(), this->bus_.written.end(), FRAME_COMMAND_5), 1);
}

TEST_F(CommandTest, CommandAckWithBadCrcIsWarned) {
  // REPLY_COMMAND_ACK with the last CRC byte changed
  static const Bytes REPLY_COMMAND_ACK_BAD_CRC = {0x24, 0x04, 0xF8, 0xE7, 0x03, 0x00, 0x0D, 0x88};
  this->identify_();
  this->pump_.send_command(Command::COMMAND_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK_BAD_CRC);
  EXPECT_TRUE(this->pump_.status_has_warning());
  this->run_(QUIET_MS);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(CommandTest, TimeoutOfTheReadBackIsWarned) {
  this->identify_();
  this->pump_.send_command(Command::COMMAND_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->run_(TIMEOUT_MS);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(CommandTest, OnlyTheAppsCommandsAreSent) {
  this->identify_();
  // 1 is not one of them; 7 is REMOTE, which leaves an ALPHA deaf to Bluetooth for a while
  for (uint8_t id : {1, 4, 7, 8, 22, 52}) {
    this->pump_.send_command(static_cast<Command>(id));
    EXPECT_TRUE(this->next_request_().empty());
  }
}

TEST_F(CommandTest, StartsAndStopsAirVenting) {
  this->identify_();
  this->pump_.write_air_venting(true);
  ASSERT_EQ(this->next_request_(), FRAME_AIR_VENTING_ON);
  this->reply_(REPLY_REFERENCE_ACK);
  EXPECT_FALSE(this->pump_.status_has_warning());
  this->pump_.write_air_venting(false);
  ASSERT_EQ(this->next_request_(), FRAME_AIR_VENTING_OFF);
  this->reply_(REPLY_REFERENCE_ACK);
  EXPECT_FALSE(this->pump_.status_has_warning());
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(CommandTest, BadAirVentingRepliesAreWarned) {
  // ACK 3, a reply with data, a class 3 reply, and none at all
  this->identify_();
  for (const Bytes &reply : {REPLY_REFERENCE_ACK_ILLEGAL, REPLY_REFERENCE_WITH_DATA, REPLY_COMMAND_ACK, Bytes{}}) {
    this->pump_.write_air_venting(true);
    ASSERT_EQ(this->next_request_(), FRAME_AIR_VENTING_ON);
    if (reply.empty()) {
      this->run_(TIMEOUT_MS);
    } else {
      this->reply_(reply);
    }
    EXPECT_TRUE(this->pump_.status_has_warning());
    this->pump_.update();
    EXPECT_FALSE(this->pump_.status_has_warning());
  }
}

// A stop that comes while the start still waits replaces it
TEST_F(CommandTest, WaitingAirVentingIsReplaced) {
  this->read_selects_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->pump_.write_air_venting(true);
  this->pump_.write_air_venting(false);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  EXPECT_EQ(this->next_request_(), FRAME_AIR_VENTING_OFF);
  EXPECT_FALSE(this->wrote_(FRAME_AIR_VENTING_ON));
}

TEST_F(CommandTest, AirVentingIsRefusedWithoutConnection) {
  this->identify_();
  this->bus_.lose_connection();
  this->pump_.write_air_venting(true);
  this->bus_.establish_connection();
  this->run_();
  EXPECT_FALSE(this->wrote_(FRAME_AIR_VENTING_ON));
}

// The bus refuses the write when the link is gone before the bus has noticed; the warning comes at once, not only
// when the bus reports the loss
TEST_F(CommandTest, AirVentingNotSentIsWarned) {
  this->read_selects_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->pump_.write_air_venting(true);
  this->bus_.connected = false;
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  EXPECT_TRUE(this->pump_.status_has_warning());
  this->run_();
  EXPECT_FALSE(this->wrote_(FRAME_AIR_VENTING_ON));
}

TEST_F(CommandTest, RefusedCommandIsWarned) {
  this->identify_();
  this->pump_.send_command(Command::COMMAND_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK_ILLEGAL);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

// With only buttons no settings are polled; the warning clears with the next poll cycle
TEST_F(CommandTest, WarningClearsWithoutSettingsToRead) {
  this->identify_();
  this->pump_.send_command(Command::COMMAND_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK_ILLEGAL);
  ASSERT_TRUE(this->pump_.status_has_warning());
  this->pump_.update();
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(CommandTest, CommandReplyWithDataIsWarned) {
  this->identify_();
  this->pump_.send_command(Command::COMMAND_START);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_6);
  this->reply_(REPLY_COMMAND_ACK_WITH_DATA);
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(CommandTest, CommandRefreshesTheSettings) {
  this->read_selects_();
  this->pump_.send_command(Command::COMMAND_START);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_6);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_10);
}

// The stop command sets the requested operating mode (86/100) as well; its read-back shows Stop
TEST_F(CommandTest, StopShowsInTheOperatingModeSelect) {
  this->read_selects_();
  this->pump_.send_command(Command::COMMAND_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_STOP);
  EXPECT_EQ(this->operating_mode_.active_index(), STOP);
  EXPECT_FALSE(this->pump_.status_has_warning());
  // Then the refresh
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_STOP);
  EXPECT_EQ(this->operating_mode_.active_index(), STOP);
}

TEST_F(CommandTest, CommandWaitsForARunningWrite) {
  this->read_selects_();
  this->write_curve_until_set_();
  this->pump_.send_command(Command::COMMAND_STOP);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  EXPECT_EQ(this->next_request_(), FRAME_COMMAND_5);
}

TEST_F(CommandTest, CommandBehindAWriteWithoutValidReadWaitsUntilTheWriteIsDropped) {
  this->use_selects_();
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_V2);  // no valid read of 86/10: its write waits
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  this->pump_.send_command(Command::COMMAND_START);
  EXPECT_TRUE(this->next_request_().empty());
  for (uint8_t i = 1; i < WRITE_WAIT_CYCLES; i++) {
    this->pump_.update();
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
    this->reply_(REPLY_CONTROL_MODE_V2);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
    this->reply_(REPLY_OPERATING_MODE_NORMAL);
  }
  this->pump_.update();
  EXPECT_EQ(this->next_request_(), FRAME_COMMAND_6);
}

TEST_F(CommandTest, WaitingCommandIsDroppedOnDisconnect) {
  this->read_selects_();
  this->write_curve_until_set_();
  this->pump_.send_command(Command::COMMAND_STOP);
  this->bus_.lose_connection();
  EXPECT_TRUE(this->pump_.status_has_warning());
  this->bus_.establish_connection();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_ALPHA3);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->wrote_(FRAME_COMMAND_5));
}

TEST_F(CommandTest, DisconnectDuringCommandIsWarned) {
  this->identify_();
  this->pump_.send_command(Command::COMMAND_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->bus_.lose_connection();
  EXPECT_TRUE(this->pump_.status_has_warning());
}

TEST_F(CommandTest, FullQueueRefusesMore) {
  this->read_selects_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);  // a poll is on the wire, so the writes wait
  this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
  this->pump_.write_select(OPERATING_MODE, MAX);
  this->pump_.send_command(Command::COMMAND_STOP);
  this->pump_.send_command(Command::COMMAND_START);
  this->pump_.send_command(Command::COMMAND_RESET_ALARM);  // the fifth: refused
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_SET_CONTROL_MODE_CURVE);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  // 86/100 is polled again after the control mode write
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_SET_OPERATING_MODE_MAX_CURVE);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_MAX_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_STOP_CURVE);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_6);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL_CURVE);
  EXPECT_FALSE(this->pump_.status_has_warning());
  // Then the refresh; the refused command never comes
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_10);
  EXPECT_FALSE(this->wrote_(FRAME_COMMAND_2));
}

// A waiting command that is pressed again moves behind the commands queued after it: the last press wins
TEST_F(CommandTest, StartStopStartEndsStarted) {
  this->read_selects_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);  // a poll is on the wire, so the commands wait
  this->pump_.send_command(Command::COMMAND_START);
  this->pump_.send_command(Command::COMMAND_STOP);
  this->pump_.send_command(Command::COMMAND_START);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_6);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_EQ(this->operating_mode_.active_index(), 0u);
  EXPECT_FALSE(this->pump_.status_has_warning());
  // Each command went out once
  EXPECT_EQ(std::count(this->bus_.written.begin(), this->bus_.written.end(), FRAME_COMMAND_6), 1);
}

// The same for a select: Max, stop, Normal ends in Normal, and Max is never written
TEST_F(CommandTest, SelectStopSelectEndsWithTheLastSelect) {
  this->read_selects_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);  // a poll is on the wire, so the writes wait
  this->pump_.write_select(OPERATING_MODE, MAX);
  this->pump_.send_command(Command::COMMAND_STOP);
  this->pump_.write_select(OPERATING_MODE, 0);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_STOP);
  // The command may have changed the settings: the refresh polls them before the write
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_SET_OPERATING_MODE_NORMAL);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_NORMAL);
  EXPECT_EQ(this->operating_mode_.active_index(), 0u);
  EXPECT_FALSE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->wrote_(FRAME_SET_OPERATING_MODE_MAX));
}

}  // namespace esphome::grundfos_alpha::testing
