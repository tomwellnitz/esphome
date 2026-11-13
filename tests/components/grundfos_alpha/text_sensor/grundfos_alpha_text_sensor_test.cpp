#include "../common.h"

#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::grundfos_alpha::testing {

// 86/8 as REPLY_REALIZED, but with the control source 101 (clock program) and 2 (not seen on a pump)
static const Bytes REPLY_REALIZED_CLOCK = {0x24, 0x12, 0xF8, 0xE7, 0x0A, 0x0E, 0x00, 0x01, 0x2F, 0x01, 0x00,
                                           0x00, 0x07, 0x65, 0x00, 0x0E, 0x47, 0x19, 0x30, 0x00, 0x99, 0xC3};
static const Bytes REPLY_REALIZED_SOURCE_2 = {0x24, 0x12, 0xF8, 0xE7, 0x0A, 0x0E, 0x00, 0x01, 0x2F, 0x01, 0x00,
                                              0x00, 0x07, 0x02, 0x00, 0x0E, 0x47, 0x19, 0x30, 0x00, 0x33, 0x9D};

class TextSensorTest : public PumpTest {
 protected:
  void SetUp() override {
    PumpTest::SetUp();
    this->pump_.set_operating_mode_text_sensor(&this->operating_mode_);
    this->pump_.set_control_source_text_sensor(&this->control_source_);
  }

  text_sensor::TextSensor operating_mode_, control_source_;
};

TEST_F(TextSensorTest, RealizedStateIsNamed) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_8);
  this->reply_(REPLY_REALIZED);
  EXPECT_EQ(this->operating_mode_.state, "Normal");
  EXPECT_EQ(this->control_source_.state, "Panel");
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(TextSensorTest, OnlySeenControlSourcesAreNamed) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_8);
  this->reply_(REPLY_REALIZED_CLOCK);
  EXPECT_EQ(this->control_source_.state, "Clock program");
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_8);
  this->reply_(REPLY_REALIZED_SOURCE_2);
  EXPECT_EQ(this->control_source_.state, "Unknown");
}

TEST_F(TextSensorTest, ReplyWithStatusIsNotShown) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_8);
  this->reply_(REPLY_REALIZED_STATUS_1);
  EXPECT_FALSE(this->operating_mode_.has_state());
}

TEST_F(TextSensorTest, EmptyOnDisconnect) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_86_8);
  this->reply_(REPLY_REALIZED);
  this->bus_.lose_connection();
  EXPECT_EQ(this->operating_mode_.state, "");
  EXPECT_EQ(this->control_source_.state, "");
}

TEST_F(PumpTest, NoSettingsReadWithoutEntities) {
  this->identify_();
  EXPECT_TRUE(this->next_request_().empty());
}

}  // namespace esphome::grundfos_alpha::testing
