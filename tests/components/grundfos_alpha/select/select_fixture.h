#pragma once

#include "../common.h"

#include "esphome/components/select/select.h"

namespace esphome::grundfos_alpha::testing {

class TestSelect : public select::Select {
 protected:
  void control(size_t index) override {}
};

inline constexpr size_t CONSTANT_CURVE = 2;
inline constexpr size_t UNDERFLOOR = 4;
inline constexpr size_t STOP = 1;
inline constexpr size_t MAX = 3;
inline constexpr auto CONTROL_MODE = SelectSetting::SELECT_SETTING_CONTROL_MODE;
inline constexpr auto OPERATING_MODE = SelectSetting::SELECT_SETTING_OPERATING_MODE;

class SelectTest : public PumpTest {
 protected:
  void SetUp() override {
    PumpTest::SetUp();
    this->control_mode_.traits.set_options({"Constant pressure", "Proportional pressure", "Constant curve", "Radiator",
                                            "Underfloor heating", "Radiator and underfloor heating"});
    this->operating_mode_.traits.set_options({"Normal", "Stop", "Min", "Max"});
  }

  void use_selects_() {
    this->pump_.set_select(CONTROL_MODE, &this->control_mode_);
    this->pump_.set_select(OPERATING_MODE, &this->operating_mode_);
  }

  // Identify the pump and answer the reads of both selects: underfloor heating, normal
  void read_selects_() {
    this->use_selects_();
    this->identify_();
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
    this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_100);
    this->reply_(REPLY_OPERATING_MODE_NORMAL);
    ASSERT_TRUE(this->next_request_().empty());
  }

  // Start a control mode write to constant curve and answer its read
  void write_curve_until_set_() {
    this->pump_.write_select(CONTROL_MODE, CONSTANT_CURVE);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
    this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
    ASSERT_EQ(this->next_request_(), FRAME_SET_CONTROL_MODE_CURVE);
  }

  // A write refused with reply ends without a read-back; the refresh follows
  void expect_refused_(const Bytes &reply) {
    this->read_selects_();
    this->write_curve_until_set_();
    this->reply_(reply);
    EXPECT_TRUE(this->pump_.status_has_warning());
    EXPECT_EQ(this->control_mode_.active_index(), UNDERFLOOR);
    ASSERT_EQ(this->next_request_(), FRAME_GET_86_10);
    this->reply_(REPLY_CONTROL_MODE_UNDERFLOOR);
    EXPECT_EQ(this->next_request_(), FRAME_GET_86_100);
  }

  bool wrote_(const Bytes &frame) const {
    return std::find(this->bus_.written.begin(), this->bus_.written.end(), frame) != this->bus_.written.end();
  }

  TestSelect control_mode_, operating_mode_;
};

}  // namespace esphome::grundfos_alpha::testing
