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
  }

  // Identify the pump and answer the check with the recorded slow clock, up to the SET
  void check_until_set_(const Bytes &config = REPLY_CONFIG_OLD) {
    this->identify_();
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

  FakeClock clock_;
  sensor::Sensor deviation_;
};

TEST_F(ClockTest, SetsASlowClockAfterTheConnection) {
  this->set_clock_();
  EXPECT_FLOAT_EQ(this->deviation_.state, -46.0f);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(ClockTest, RightClockIsNotWritten) {
  this->clock_.now = TIME_20_49_31;
  this->identify_();
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
  EXPECT_EQ(this->next_request_(), FRAME_GET_94_101);
}

TEST_F(ClockTest, ConnectionLostDuringTheCheck) {
  this->check_until_set_();
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK);
  this->bus_.lose_connection();
  this->bus_.establish_connection();
  EXPECT_EQ(this->next_request_(), FRAME_GET_94_101);
}

// A failed write is only logged; the next check comes after an hour
TEST_F(ClockTest, RefusedWriteIsRetriedAfterAnHour) {
  this->check_until_set_();
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK);
  this->reply_(REPLY_SET_FAILED);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->pump_.status_has_warning());
  EXPECT_FALSE(this->checks_after_(HOUR_MS - 1000));
  EXPECT_TRUE(this->checks_after_(1000));
}

TEST_F(ClockTest, DifferentReadBackFails) {
  this->check_until_set_();
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_100);
  this->reply_(REPLY_CONFIG_OLD);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->checks_after_(HOUR_MS - 1000));
  EXPECT_TRUE(this->checks_after_(1000));
}

// For example a daylight saving rule in the pump that does not match the time source
TEST_F(ClockTest, ClockStillOffAfterTheWriteFails) {
  this->check_until_set_();
  ASSERT_EQ(this->next_request_(), FRAME_SET_CLOCK);
  this->reply_(REPLY_SET_ACK);
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_100);
  this->reply_(REPLY_CONFIG_SET);
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->reply_(REPLY_ACTUAL_21_48_48);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FLOAT_EQ(this->deviation_.state, -46.0f);
  EXPECT_FALSE(this->checks_after_(HOUR_MS - 1000));
  EXPECT_TRUE(this->checks_after_(1000));
}

// Another object in the reply to the check is never taken for the time
TEST_F(ClockTest, WrongObjectInTheCheckIsNotWritten) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->reply_(REPLY_CONFIG_OLD);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_TRUE(std::isnan(this->deviation_.state));
}

TEST_F(ClockTest, MissingObjectIsNotWritten) {
  this->identify_();
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
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->run_(TIMEOUT_MS + QUIET_MS + 100);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->checks_after_(0));
}

// The poll goes on after the check
TEST_F(ClockTest, MeasurementsFollowTheCheck) {
  sensor::Sensor flow;
  this->pump_.set_flow_sensor(&flow);
  this->clock_.now = TIME_20_49_31;
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_94_101);
  this->reply_(REPLY_ACTUAL_20_49_31);
  EXPECT_EQ(this->next_request_(), FRAME_PROCESS);
}

// Without a time source the pump's clock is left alone
TEST_F(PumpTest, ClockIsLeftAloneWithoutATimeSource) {
  this->identify_();
  EXPECT_TRUE(this->next_request_().empty());
}

}  // namespace esphome::grundfos_alpha::testing
