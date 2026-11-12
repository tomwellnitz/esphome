#include "../common.h"

#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::grundfos_alpha::testing {

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
