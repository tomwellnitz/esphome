#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "esphome/components/genibus/sensor/genibus_sensor.h"
#include "esphome/core/application.h"

// The request and the replies are frames of a real ALPHA3, checked with the geni.py frame tool, unless a comment says
// otherwise.
namespace esphome::genibus::testing {

namespace {

using Bytes = std::vector<uint8_t>;

const Bytes FRAME_GET_87_1 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x57, 0x00, 0x01, 0x82, 0x8D};
// 87/1 type 0x00E8 v1, 26 bytes: double energy 235705441.4 Ws, float power 6.3397 W, uint16 count 1, ...
const Bytes REPLY_87_1 = {0x24, 0x25, 0xF8, 0xE7, 0x0A, 0x21, 0x00, 0x00, 0xE8, 0x01, 0x00, 0x00, 0x1A, 0x41,
                          0xAC, 0x19, 0x28, 0xC2, 0xCC, 0x58, 0xC0, 0x40, 0xCA, 0xDF, 0x00, 0x00, 0x01, 0x40,
                          0xCA, 0xDF, 0x00, 0x41, 0xAC, 0x19, 0x28, 0xC2, 0xCC, 0x58, 0xC0, 0x3F, 0x83};
// Class 10 ACK 0 with the single data byte 4: object missing
const Bytes REPLY_OBJECT_MISSING = {0x24, 0x05, 0xF8, 0xE7, 0x0A, 0x01, 0x04, 0xEE, 0x26};
// Class 10 SET acknowledgement with status 0: no data object
const Bytes REPLY_ACK = {0x24, 0x05, 0xF8, 0xE7, 0x0A, 0x01, 0x00, 0xAE, 0xA2};

constexpr uint32_t TIMEOUT_MS = 1000;
constexpr uint32_t QUIET_MS = 250;

class TestBus : public GenibusBus {
 public:
  bool is_connected() const override { return this->connected; }
  void lose_connection() {
    this->connected = false;
    this->connection_lost_();
  }
  void establish_connection() {
    this->connected = true;
    this->connection_established_();
  }

  bool connected{true};
  std::vector<Bytes> written;

 protected:
  bool write_frame(std::span<const uint8_t> frame) override {
    this->written.emplace_back(frame.begin(), frame.end());
    return true;
  }
};

class GenibusSensorTest : public ::testing::Test {
 protected:
  void SetUp() override { this->bus_.set_master_address(0xF8); }

  // A sensor of object 87/1 at address 0xE7, registered on the bus
  GenibusSensor &make_(uint8_t offset, ValueType type) {
    this->sensor_ = std::make_unique<GenibusSensor>(87, 1, offset, type);
    this->sensor_->set_parent(&this->bus_);
    this->sensor_->set_address(0xE7);
    this->bus_.register_device(this->sensor_.get());
    return *this->sensor_;
  }

  void run_(uint32_t ms = 16) {
    this->now_ += ms;
    LoopBlockingGuard guard{nullptr, nullptr, this->now_};
    this->bus_.loop();
  }

  // Poll sensor once and answer with reply
  void poll_(GenibusSensor &sensor, const Bytes &reply) {
    sensor.update();
    this->run_();
    this->bus_.receive_bytes(reply);
  }

  TestBus bus_;
  std::unique_ptr<GenibusSensor> sensor_;
  uint32_t now_{1000};
};

}  // namespace

TEST(GenibusReadValue, DecodesEveryTypeBigEndian) {
  const Bytes body(REPLY_87_1.begin() + 13, REPLY_87_1.end() - 2);
  ASSERT_EQ(body.size(), 26u);
  EXPECT_FLOAT_EQ(read_value(body, 0, ValueType::VALUE_TYPE_DOUBLE), 235705441.4f);
  EXPECT_FLOAT_EQ(read_value(body, 8, ValueType::VALUE_TYPE_FLOAT), 6.3397216796875f);
  EXPECT_EQ(read_value(body, 12, ValueType::VALUE_TYPE_UINT16), 1.0f);
  EXPECT_EQ(read_value(body, 13, ValueType::VALUE_TYPE_UINT8), 1.0f);
  EXPECT_EQ(read_value(body, 0, ValueType::VALUE_TYPE_UINT32), 1101797672.0f);
  EXPECT_EQ(read_value(body, 4, ValueType::VALUE_TYPE_INT8), -62.0f);
  EXPECT_EQ(read_value(body, 4, ValueType::VALUE_TYPE_INT16), -15668.0f);
  EXPECT_EQ(read_value(body, 4, ValueType::VALUE_TYPE_INT32), -1026795328.0f);
}

TEST(GenibusReadValue, NanPastTheEnd) {
  const Bytes body(REPLY_87_1.begin() + 13, REPLY_87_1.end() - 2);
  EXPECT_FALSE(std::isnan(read_value(body, 18, ValueType::VALUE_TYPE_DOUBLE)));
  EXPECT_TRUE(std::isnan(read_value(body, 19, ValueType::VALUE_TYPE_DOUBLE)));
  EXPECT_TRUE(std::isnan(read_value(body, 25, ValueType::VALUE_TYPE_UINT16)));
  EXPECT_TRUE(std::isnan(read_value(body, 255, ValueType::VALUE_TYPE_UINT8)));
  EXPECT_TRUE(std::isnan(read_value({}, 0, ValueType::VALUE_TYPE_UINT8)));
}

TEST_F(GenibusSensorTest, ReadsAndScalesValue) {
  auto &sensor = this->make_(0, ValueType::VALUE_TYPE_DOUBLE);
  sensor.set_multiply(1.0f / 3600000.0f);
  sensor.update();
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 1u);
  EXPECT_EQ(this->bus_.written[0], FRAME_GET_87_1);
  this->bus_.receive_bytes(REPLY_87_1);
  EXPECT_NEAR(sensor.state, 65.4737f, 0.0001f);
}

TEST_F(GenibusSensorTest, ChecksExpectedTypeAndVersion) {
  auto &sensor = this->make_(8, ValueType::VALUE_TYPE_FLOAT);
  sensor.set_expected_type(0x00E8);
  sensor.set_expected_version(1);
  this->poll_(sensor, REPLY_87_1);
  EXPECT_FLOAT_EQ(sensor.state, 6.3397216796875f);
  sensor.set_expected_version(2);
  this->poll_(sensor, REPLY_87_1);
  EXPECT_TRUE(std::isnan(sensor.state));
  sensor.set_expected_version(1);
  sensor.set_expected_type(0x0130);
  this->poll_(sensor, REPLY_87_1);
  EXPECT_TRUE(std::isnan(sensor.state));
}

TEST_F(GenibusSensorTest, UnknownOnOffsetPastBodyAndOnReplyWithoutObject) {
  auto &sensor = this->make_(24, ValueType::VALUE_TYPE_DOUBLE);
  this->poll_(sensor, REPLY_87_1);
  EXPECT_TRUE(std::isnan(sensor.state));
  this->poll_(sensor, REPLY_ACK);
  EXPECT_TRUE(std::isnan(sensor.state));
}

TEST_F(GenibusSensorTest, MissingObjectIsNotAskedAgainUntilReconnect) {
  auto &sensor = this->make_(8, ValueType::VALUE_TYPE_FLOAT);
  this->poll_(sensor, REPLY_OBJECT_MISSING);
  EXPECT_TRUE(std::isnan(sensor.state));
  sensor.update();
  this->run_();
  EXPECT_EQ(this->bus_.written.size(), 1u);
  this->bus_.lose_connection();
  this->bus_.establish_connection();  // on_connected() reads at once
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 2u);
  EXPECT_EQ(this->bus_.written[1], FRAME_GET_87_1);
}

TEST_F(GenibusSensorTest, TimeoutMakesValueUnknown) {
  auto &sensor = this->make_(8, ValueType::VALUE_TYPE_FLOAT);
  this->poll_(sensor, REPLY_87_1);
  ASSERT_FALSE(std::isnan(sensor.state));
  sensor.update();
  this->run_();
  this->run_(TIMEOUT_MS);
  EXPECT_TRUE(std::isnan(sensor.state));
  this->run_(QUIET_MS);
  this->poll_(sensor, REPLY_87_1);
  EXPECT_FLOAT_EQ(sensor.state, 6.3397216796875f);
}

TEST_F(GenibusSensorTest, DisconnectMakesValueUnknownAndStopsRequests) {
  auto &sensor = this->make_(8, ValueType::VALUE_TYPE_FLOAT);
  this->poll_(sensor, REPLY_87_1);
  ASSERT_FALSE(std::isnan(sensor.state));
  sensor.update();
  this->bus_.lose_connection();
  EXPECT_TRUE(std::isnan(sensor.state));
  sensor.update();
  this->run_();
  EXPECT_EQ(this->bus_.written.size(), 1u);
}

TEST_F(GenibusSensorTest, OneOpenRequestPerSensor) {
  auto &sensor = this->make_(8, ValueType::VALUE_TYPE_FLOAT);
  sensor.update();
  sensor.update();
  this->run_();
  sensor.update();
  this->run_();
  EXPECT_EQ(this->bus_.written.size(), 1u);
}

}  // namespace esphome::genibus::testing
