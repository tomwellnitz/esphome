#include "../common.h"

#include "esphome/components/time/real_time_clock.h"

namespace esphome::grundfos_alpha::testing {

// Clock objects 94/100 and 94/101. Requests and the replies marked as recorded are frames of a real ALPHA3; the
// others were built with the geni.py frame tool in the same layout.
inline const Bytes FRAME_GET_94_100 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x5E, 0x00, 0x64, 0x20, 0x1F};
inline const Bytes FRAME_GET_94_101 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x5E, 0x00, 0x65, 0x30, 0x3E};
// Recorded: 94/101 at 2026-11-12 20:48:48 on the Mac, the pump showed 20:48:02.36 (46 s slow)
inline const Bytes REPLY_ACTUAL_SLOW = {0x24, 0x17, 0xF8, 0xE7, 0x0A, 0x13, 0x00, 0x01, 0x42,
                                        0x01, 0x00, 0x00, 0x0C, 0x07, 0xEA, 0x0B, 0x0C, 0x14,
                                        0x30, 0x02, 0x24, 0x00, 0x04, 0x01, 0x02, 0x18, 0x61};
// Recorded: 94/101 right after the clock was set at 20:49:31; the pump showed 20:49:31.93
inline const Bytes REPLY_ACTUAL_20_49_31 = {0x24, 0x17, 0xF8, 0xE7, 0x0A, 0x13, 0x00, 0x01, 0x42,
                                            0x01, 0x00, 0x00, 0x0C, 0x07, 0xEA, 0x0B, 0x0C, 0x14,
                                            0x31, 0x1F, 0x5D, 0x00, 0x04, 0x01, 0x02, 0x41, 0x3D};
// 94/101 at 20:48:48 and one hour later
inline const Bytes REPLY_ACTUAL_20_48_48 = {0x24, 0x17, 0xF8, 0xE7, 0x0A, 0x13, 0x00, 0x01, 0x42,
                                            0x01, 0x00, 0x00, 0x0C, 0x07, 0xEA, 0x0B, 0x0C, 0x14,
                                            0x30, 0x30, 0x00, 0x00, 0x04, 0x01, 0x02, 0x3D, 0x1F};
inline const Bytes REPLY_ACTUAL_21_48_48 = {0x24, 0x17, 0xF8, 0xE7, 0x0A, 0x13, 0x00, 0x01, 0x42,
                                            0x01, 0x00, 0x00, 0x0C, 0x07, 0xEA, 0x0B, 0x0C, 0x15,
                                            0x30, 0x30, 0x00, 0x00, 0x04, 0x01, 0x02, 0x7A, 0xCC};
// Recorded: 94/100 holding the time of the last setting (2026-01-04 12:29:46.78), sync on, 24 h
inline const Bytes REPLY_CONFIG_OLD = {0x24, 0x15, 0xF8, 0xE7, 0x0A, 0x11, 0x00, 0x01, 0x41, 0x01, 0x00, 0x00, 0x0A,
                                       0x01, 0x07, 0xEA, 0x01, 0x04, 0x0C, 0x1D, 0x2E, 0x4E, 0x00, 0x0F, 0x20};
// Recorded: 94/100 after a refused write of 12 h; it still shows hour format 1
inline const Bytes REPLY_CONFIG_12H = {0x24, 0x15, 0xF8, 0xE7, 0x0A, 0x11, 0x00, 0x01, 0x41, 0x01, 0x00, 0x00, 0x0A,
                                       0x01, 0x07, 0xEA, 0x0B, 0x0C, 0x15, 0x12, 0x06, 0x4C, 0x01, 0xD0, 0xC0};
// 94/100 as REPLY_CONFIG_OLD with sync off
inline const Bytes REPLY_CONFIG_OLD_NO_SYNC = {0x24, 0x15, 0xF8, 0xE7, 0x0A, 0x11, 0x00, 0x01, 0x41,
                                               0x01, 0x00, 0x00, 0x0A, 0x00, 0x07, 0xEA, 0x01, 0x04,
                                               0x0C, 0x1D, 0x2E, 0x4E, 0x00, 0x60, 0x65};
// SET 94/100 to 2026-11-12 20:48:48, 1/100 s 0, 24 h, with sync on and off, and the read-backs
inline const Bytes FRAME_SET_CLOCK = {0x27, 0x17, 0xE7, 0xF8, 0x0A, 0x93, 0x5E, 0x00, 0x64,
                                      0x01, 0x41, 0x01, 0x00, 0x00, 0x0A, 0x01, 0x07, 0xEA,
                                      0x0B, 0x0C, 0x14, 0x30, 0x30, 0x00, 0x00, 0xCD, 0xB8};
inline const Bytes FRAME_SET_CLOCK_NO_SYNC = {0x27, 0x17, 0xE7, 0xF8, 0x0A, 0x93, 0x5E, 0x00, 0x64,
                                              0x01, 0x41, 0x01, 0x00, 0x00, 0x0A, 0x00, 0x07, 0xEA,
                                              0x0B, 0x0C, 0x14, 0x30, 0x30, 0x00, 0x00, 0xA2, 0xFD};
inline const Bytes REPLY_CONFIG_SET = {0x24, 0x15, 0xF8, 0xE7, 0x0A, 0x11, 0x00, 0x01, 0x41, 0x01, 0x00, 0x00, 0x0A,
                                       0x01, 0x07, 0xEA, 0x0B, 0x0C, 0x14, 0x30, 0x30, 0x00, 0x00, 0x8F, 0x32};
inline const Bytes REPLY_CONFIG_SET_NO_SYNC = {0x24, 0x15, 0xF8, 0xE7, 0x0A, 0x11, 0x00, 0x01, 0x41,
                                               0x01, 0x00, 0x00, 0x0A, 0x00, 0x07, 0xEA, 0x0B, 0x0C,
                                               0x14, 0x30, 0x30, 0x00, 0x00, 0xE0, 0x77};

// 2026-11-12 20:48:48 and 20:49:31 UTC; without a time zone the local time is UTC
inline constexpr time_t TIME_20_48_48 = 1794516528;
inline constexpr time_t TIME_20_49_31 = TIME_20_48_48 + 43;
inline constexpr uint32_t HOUR_MS = 60 * 60 * 1000;
inline constexpr uint32_t DAY_MS = 24 * HOUR_MS;

class FakeClock : public time::RealTimeClock {
 public:
  time_t timestamp_now() override { return this->now; }
  void update() override {}
  time_t now{TIME_20_48_48};
};

class ClockTest : public PumpTest {
 protected:
  void SetUp() override {
    PumpTest::SetUp();
    this->pump_.set_time_id(&this->clock_);
    this->pump_.set_clock_deviation_sensor(&this->deviation_);
    this->pump_.set_clock_writes(true);
  }

  // Identify the pump; without other entities its first poll ends at once, and the next update checks the clock
  void connect_() {
    this->identify_();
    ASSERT_TRUE(this->next_request_().empty());
    this->pump_.update();
  }

  // Identify the pump and answer the check with the recorded slow clock, up to the SET
  void check_until_set_(const Bytes &config = REPLY_CONFIG_OLD) {
    this->connect_();
    ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
    this->reply_(REPLY_ACTUAL_SLOW);
    ASSERT_EQ(this->next_request_(), FRAME_GET_94_100);
    this->reply_(config);
  }

  // A whole check that sets the clock
  void set_clock_() {
    this->check_until_set_();
    ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK);
    this->reply_(REPLY_SET_ACK);
    ASSERT_EQ(this->next_request_(), FRAME_GET_94_100);
    this->reply_(REPLY_CONFIG_SET);
    ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
    this->reply_(REPLY_ACTUAL_20_48_48);
    ASSERT_TRUE(this->next_request_().empty());
  }

  // Run an update after ms; whether it starts a clock check
  bool checks_after_(uint32_t ms) {
    this->run_(ms);
    this->pump_.update();
    return this->next_request_() == FRAME_GET_94_101;
  }

  // Answer the steps of a check that sets the clock, up to the request of the given step: 0 the check, 1 the read of
  // 94/100, 2 the SET, 3 the read-back, 4 the check after the write
  void run_to_step_(int step) {
    this->connect_();
    ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
    const Bytes replies[] = {REPLY_ACTUAL_SLOW, REPLY_CONFIG_OLD, REPLY_SET_ACK, REPLY_CONFIG_SET};
    const Bytes requests[] = {FRAME_GET_94_100, FRAME_SET_CLOCK, FRAME_GET_94_100, FRAME_GET_94_101};
    for (int i = 0; i < step; i++) {
      this->reply_(replies[i]);
      ASSERT_EQ(this->next_request_(), requests[i]);
    }
  }

  // After a failure without a valid answer from the pump: nothing more is sent, the next check comes after an hour
  // and sets the clock again
  void expect_retry_after_an_hour_() {
    EXPECT_TRUE(this->next_request_().empty());
    EXPECT_FALSE(this->pump_.status_has_warning());
    EXPECT_FALSE(this->checks_after_(HOUR_MS - 1000));
    ASSERT_TRUE(this->checks_after_(1000));
    this->reply_(REPLY_ACTUAL_21_48_48);
    ASSERT_EQ(this->next_request_(), FRAME_GET_94_100);
    this->reply_(REPLY_CONFIG_OLD);
    EXPECT_EQ(this->next_request_(), FRAME_SET_CLOCK);
  }

  // After the pump answered a write but the clock is still off: checks go on, but nothing is written until restart
  void expect_no_more_writes_() {
    EXPECT_TRUE(this->next_request_().empty());
    EXPECT_FALSE(this->pump_.status_has_warning());
    EXPECT_FALSE(this->checks_after_(HOUR_MS));
    this->bus_.lose_connection();
    this->bus_.establish_connection();
    // The first poll ends at once without other entities; the next update checks the clock
    ASSERT_TRUE(this->next_request_().empty());
    this->pump_.update();
    ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
    this->reply_(REPLY_ACTUAL_SLOW);
    EXPECT_FLOAT_EQ(this->deviation_.state, -46.0f);
    EXPECT_TRUE(this->next_request_().empty());
    ASSERT_TRUE(this->checks_after_(DAY_MS));
    this->reply_(REPLY_ACTUAL_SLOW);
    EXPECT_TRUE(this->next_request_().empty());
    EXPECT_FALSE(this->wrote_twice_(FRAME_SET_CLOCK));
  }

  // The reply to the given step comes while the bus has lost the connection, so the next request is not sent
  void expect_not_sent_(int step) {
    this->run_to_step_(step);
    const Bytes replies[] = {REPLY_ACTUAL_SLOW, REPLY_CONFIG_OLD, REPLY_SET_ACK, REPLY_CONFIG_SET};
    this->bus_.connected = false;
    this->reply_(replies[step]);
    this->bus_.connected = true;
    this->expect_retry_after_an_hour_();
  }

  void expect_timeout_(int step) {
    this->run_to_step_(step);
    this->run_(TIMEOUT_MS + QUIET_MS + 100);
    this->expect_retry_after_an_hour_();
  }

  bool wrote_twice_(const Bytes &frame) const {
    return std::count(this->bus_.written.begin(), this->bus_.written.end(), frame) > 1;
  }

  FakeClock clock_;
  sensor::Sensor deviation_;
};

TEST_F(ClockTest, SetsASlowClockAfterTheConnection) {
  this->check_until_set_();
  EXPECT_FLOAT_EQ(this->deviation_.state, -46.0f);
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_100);
  this->reply_(REPLY_CONFIG_SET);
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->reply_(REPLY_ACTUAL_20_48_48);
  EXPECT_TRUE(this->next_request_().empty());
  // The sensor shows the clock after the write
  EXPECT_FLOAT_EQ(this->deviation_.state, 0.0f);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(ClockTest, RightClockIsNotWritten) {
  this->clock_.now = TIME_20_49_31;
  this->connect_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->reply_(REPLY_ACTUAL_20_49_31);
  EXPECT_FLOAT_EQ(this->deviation_.state, 0.0f);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(ClockTest, WaitsForAValidTime) {
  this->clock_.now = 0;
  this->identify_();
  EXPECT_TRUE(this->next_request_().empty());
  this->pump_.update();
  EXPECT_TRUE(this->next_request_().empty());
  this->clock_.now = TIME_20_48_48;
  EXPECT_TRUE(this->checks_after_(0));
}

TEST_F(ClockTest, KeepsTheSyncFlag) {
  this->check_until_set_(REPLY_CONFIG_OLD_NO_SYNC);
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK_NO_SYNC);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_100);
  this->reply_(REPLY_CONFIG_SET_NO_SYNC);
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
}

// The pump refuses 12 h; a 1 left in 94/100 by a refused write is written as 24 h
TEST_F(ClockTest, WritesTwentyFourHours) {
  this->check_until_set_(REPLY_CONFIG_12H);
  EXPECT_EQ(this->next_request_(), FRAME_SET_CLOCK);
}

TEST_F(ClockTest, ChecksOnceADay) {
  this->set_clock_();
  EXPECT_FALSE(this->checks_after_(DAY_MS - 1000));
  EXPECT_TRUE(this->checks_after_(1000));
}

TEST_F(ClockTest, ChecksAgainAfterAReconnection) {
  this->set_clock_();
  this->bus_.lose_connection();
  this->bus_.establish_connection();
  // The first poll ends at once without other entities; the next update checks the clock
  ASSERT_TRUE(this->next_request_().empty());
  this->pump_.update();
  EXPECT_EQ(this->next_request_(), FRAME_GET_94_101);
}

TEST_F(ClockTest, ConnectionLostDuringTheCheck) {
  this->check_until_set_();
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK);
  this->bus_.lose_connection();
  this->bus_.establish_connection();
  // The first poll ends at once without other entities; the next update checks the clock
  ASSERT_TRUE(this->next_request_().empty());
  this->pump_.update();
  EXPECT_EQ(this->next_request_(), FRAME_GET_94_101);
}

// A failed write is only logged; the pump would refuse the same write again
TEST_F(ClockTest, FailedWriteIsNotRepeated) {
  this->run_to_step_(2);
  this->reply_(REPLY_SET_FAILED);
  this->expect_no_more_writes_();
}

TEST_F(ClockTest, WriteWithoutStatusIsNotRepeated) {
  this->run_to_step_(2);
  this->reply_(REPLY_SET_NO_STATUS);
  this->expect_no_more_writes_();
}

// A busy pump may take the write later
TEST_F(ClockTest, BusyPumpIsRetriedAfterAnHour) {
  this->run_to_step_(2);
  this->reply_(REPLY_SET_BUSY);
  this->expect_retry_after_an_hour_();
}

TEST_F(ClockTest, DifferentReadBackIsNotRepeated) {
  this->run_to_step_(3);
  this->reply_(REPLY_CONFIG_OLD);
  this->expect_no_more_writes_();
}

// For example a daylight saving rule in the pump that does not match the time source: writing again would only wear
// the pump's memory, at each retry and each connection all summer
TEST_F(ClockTest, ClockStillOffAfterTheWriteIsNotRepeated) {
  this->run_to_step_(4);
  this->reply_(REPLY_ACTUAL_21_48_48);
  EXPECT_FLOAT_EQ(this->deviation_.state, 3600.0f);
  this->expect_no_more_writes_();
}

TEST_F(ClockTest, NoValidTimeAfterTheWriteIsRetried) {
  this->run_to_step_(4);
  this->reply_(REPLY_OBJECT_MISSING);
  this->expect_retry_after_an_hour_();
}

TEST_F(ClockTest, MissingReadBackIsRetried) {
  this->run_to_step_(3);
  this->reply_(REPLY_OBJECT_MISSING);
  this->expect_retry_after_an_hour_();
}

// The time source loses its time between the check and the write
TEST_F(ClockTest, TimeSourceLostBeforeTheWrite) {
  this->run_to_step_(1);
  this->clock_.now = 0;
  this->reply_(REPLY_CONFIG_OLD);
  EXPECT_TRUE(this->next_request_().empty());
  this->clock_.now = TIME_20_48_48;
  this->expect_retry_after_an_hour_();
}

TEST_F(ClockTest, ReadNotSent) { this->expect_not_sent_(0); }
TEST_F(ClockTest, WriteNotSent) { this->expect_not_sent_(1); }
TEST_F(ClockTest, ReadBackNotSent) { this->expect_not_sent_(2); }
TEST_F(ClockTest, CheckAfterTheWriteNotSent) { this->expect_not_sent_(3); }

TEST_F(ClockTest, TimeoutOfTheRead) { this->expect_timeout_(1); }
TEST_F(ClockTest, TimeoutOfTheWrite) { this->expect_timeout_(2); }
TEST_F(ClockTest, TimeoutOfTheReadBack) { this->expect_timeout_(3); }
TEST_F(ClockTest, TimeoutOfTheCheckAfterTheWrite) { this->expect_timeout_(4); }

#ifdef USE_GRUNDFOS_ALPHA_WRITE
// A command pressed during the check waits for it and then goes first
TEST_F(ClockTest, CommandWaitsForTheCheck) {
  this->clock_.now = TIME_20_49_31;
  this->connect_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->pump_.send_command(Command::COMMAND_RESET_ALARM);
  EXPECT_TRUE(this->next_request_().empty());
  this->reply_(REPLY_ACTUAL_20_49_31);
  EXPECT_EQ(this->next_request_(), FRAME_COMMAND_2);
}
#endif

// Another object in the reply to the check is never taken for the time
TEST_F(ClockTest, WrongObjectInTheCheckIsNotWritten) {
  this->connect_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->reply_(REPLY_CONFIG_OLD);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_TRUE(std::isnan(this->deviation_.state));
}

TEST_F(ClockTest, MissingObjectIsNotWritten) {
  this->connect_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->reply_(REPLY_OBJECT_MISSING);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(ClockTest, WrongObjectBeforeTheWriteIsNotWritten) {
  this->check_until_set_(REPLY_ACTUAL_20_48_48);
  EXPECT_TRUE(this->next_request_().empty());
}

// After a timeout a late answer may still come: nothing more is sent until the next update
TEST_F(ClockTest, TimeoutWaitsForTheNextUpdate) {
  this->connect_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->run_(TIMEOUT_MS + QUIET_MS + 100);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->checks_after_(0));
}

// The clock is checked only after the first poll of a connection, and the poll goes on after the check
TEST_F(ClockTest, CheckFollowsTheFirstPollAndThePollGoesOn) {
  sensor::Sensor flow;
  this->pump_.set_flow_sensor(&flow);
  this->clock_.now = TIME_20_49_31;
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_PROCESS);
  this->reply_(REPLY_PROCESS);
  ASSERT_TRUE(this->next_request_().empty());
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->reply_(REPLY_ACTUAL_20_49_31);
  EXPECT_EQ(this->next_request_(), FRAME_PROCESS);
}

// Without set_clock the clock is only compared, never written
TEST_F(ClockTest, OnlyChecksWithoutSetClock) {
  this->pump_.set_clock_writes(false);
  this->connect_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->reply_(REPLY_ACTUAL_SLOW);
  EXPECT_FLOAT_EQ(this->deviation_.state, -46.0f);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(ClockTest, DeviationIsUnknownWhileDisconnected) {
  this->set_clock_();
  this->bus_.lose_connection();
  EXPECT_TRUE(std::isnan(this->deviation_.state));
}

// Without a time source the pump's clock is left alone
TEST_F(PumpTest, ClockIsLeftAloneWithoutATimeSource) {
  this->identify_();
  EXPECT_TRUE(this->next_request_().empty());
}

}  // namespace esphome::grundfos_alpha::testing
