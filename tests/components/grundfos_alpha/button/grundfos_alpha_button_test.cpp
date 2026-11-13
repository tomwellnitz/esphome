#include "../select/select_fixture.h"

namespace esphome::grundfos_alpha::testing {

using CommandTest = SelectTest;

TEST_F(CommandTest, SendsTheThreeCommands) {
  this->identify_();
  EXPECT_TRUE(this->next_request_().empty());
  const std::pair<Command, Bytes> commands[] = {
      {Command::COMMAND_START, FRAME_COMMAND_6},
      {Command::COMMAND_STOP, FRAME_COMMAND_5},
      {Command::COMMAND_RESET_ALARM, FRAME_COMMAND_2},
  };
  for (const auto &[command, frame] : commands) {
    this->pump_.send_command(command);
    ASSERT_EQ(this->next_request_(), frame);
    this->reply_(REPLY_COMMAND_ACK);
    EXPECT_FALSE(this->pump_.status_has_warning());
  }
}

TEST_F(CommandTest, OnlyTheAppsCommandsAreSent) {
  this->identify_();
  this->pump_.send_command(static_cast<Command>(1));
  EXPECT_TRUE(this->next_request_().empty());
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
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_10);
}

// The stop command sets the requested operating mode (86/100) as well; the refresh after it shows Stop
TEST_F(CommandTest, StopShowsInTheOperatingModeSelect) {
  this->read_selects_();
  this->pump_.send_command(Command::COMMAND_STOP);
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_5);
  this->reply_(REPLY_COMMAND_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
  this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
  this->reply_(REPLY_OPERATING_MODE_STOP);
  EXPECT_EQ(this->operating_mode_.active_index(), STOP);
  EXPECT_FALSE(this->pump_.status_has_warning());
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
  ASSERT_EQ(this->next_request_(), FRAME_COMMAND_6);
  this->reply_(REPLY_COMMAND_ACK);
  // Then the refresh; the refused command never comes
  EXPECT_EQ(this->next_request_(), FRAME_GET_86_10);
  EXPECT_FALSE(this->wrote_(FRAME_COMMAND_2));
}

}  // namespace esphome::grundfos_alpha::testing
