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
// REPLY_87_1 with status 1 (test vector)
const Bytes REPLY_87_1_STATUS_1 = {0x24, 0x25, 0xF8, 0xE7, 0x0A, 0x21, 0x01, 0x00, 0xE8, 0x01, 0x00, 0x00, 0x1A, 0x41,
                                   0xAC, 0x19, 0x28, 0xC2, 0xCC, 0x58, 0xC0, 0x40, 0xCA, 0xDF, 0x00, 0x00, 0x01, 0x40,
                                   0xCA, 0xDF, 0x00, 0x41, 0xAC, 0x19, 0x28, 0xC2, 0xCC, 0x58, 0xC0, 0xB2, 0x5F};
// 93/1 type 0x00F8 v2, 28 bytes: the reply to another object
const Bytes REPLY_93_1 = {0x24, 0x27, 0xF8, 0xE7, 0x0A, 0x23, 0x00, 0x00, 0xF8, 0x02, 0x00, 0x00, 0x1C, 0x00, 0x00,
                          0x07, 0x39, 0x00, 0x01, 0x00, 0x01, 0x02, 0x01, 0x61, 0xCA, 0x00, 0x00, 0x0B, 0xF3, 0x00,
                          0x00, 0x0B, 0xF3, 0x00, 0x00, 0x07, 0x36, 0x02, 0x01, 0x62, 0x03, 0xC4, 0x05};
const Bytes FRAME_GET_93_78 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x5D, 0x00, 0x4E, 0xFC, 0x67};
// 93/78 type 0x011A v1, size 80 in the header, cut short to 46 bytes: float 0.0013889 at 0, float 98041.4 at 4,
// uint16 13280 at 44
const Bytes REPLY_93_78_CUT = {0x24, 0x39, 0xF8, 0xE7, 0x0A, 0x35, 0x00, 0x01, 0x1A, 0x01, 0x00, 0x00, 0x50,
                               0x3A, 0xB6, 0x0B, 0x61, 0x47, 0xBF, 0x7C, 0xB3, 0x3C, 0xBB, 0xAF, 0xD9, 0x3F,
                               0x66, 0xB6, 0x5B, 0x3C, 0x14, 0x6F, 0x23, 0x3B, 0x71, 0x74, 0x4C, 0xC7, 0x62,
                               0xE0, 0xCD, 0xC7, 0x73, 0x5A, 0xB8, 0x3E, 0x8E, 0x19, 0xB9, 0x3E, 0x92, 0xEB,
                               0x1C, 0x33, 0xC9, 0x57, 0x75, 0x33, 0xE0, 0xB7, 0x5D};
// Class 10 ACK 0 with the single data byte 4: object missing (86/999); also a failed SET status
const Bytes REPLY_OBJECT_MISSING = {0x24, 0x05, 0xF8, 0xE7, 0x0A, 0x01, 0x04, 0xEE, 0x26};
// Class 10 ACK 2 (unknown ID) as in the specification; built with the geni.py frame tool
const Bytes REPLY_UNKNOWN_ID = {0x24, 0x04, 0xF8, 0xE7, 0x0A, 0x80, 0x26, 0x99};
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
  void SetUp() override {
    this->bus_.set_master_address(0xF8);
    // Connect as the transport does, before a sensor is registered, so that a later loss reaches the sensors
    this->bus_.connected = false;
    this->bus_.establish_connection();
  }

  // A sensor of object 87/1 (type 0x00E8 v1) at address 0xE7, registered on the bus
  GenibusSensor &make_(uint8_t offset, ValueType type, uint8_t object = 87, uint16_t sub_id = 1,
                       uint16_t expected_type = 0x00E8, uint8_t expected_version = 1) {
    this->sensor_ = std::make_unique<GenibusSensor>(object, sub_id, offset, type, expected_type, expected_version);
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

TEST_F(GenibusSensorTest, ReadsValue) {
  auto &sensor = this->make_(0, ValueType::VALUE_TYPE_DOUBLE);
  sensor.update();
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 1u);
  EXPECT_EQ(this->bus_.written[0], FRAME_GET_87_1);
  this->bus_.receive_bytes(REPLY_87_1);
  EXPECT_FLOAT_EQ(sensor.state, 235705441.4f);
}

TEST_F(GenibusSensorTest, IgnoresObjectWithAStatus) {
  auto &sensor = this->make_(0, ValueType::VALUE_TYPE_DOUBLE);
  sensor.update();
  this->run_();
  this->bus_.receive_bytes(REPLY_87_1_STATUS_1);
  EXPECT_TRUE(std::isnan(sensor.state));
}

TEST_F(GenibusSensorTest, IgnoresOtherVersion) {
  auto &sensor = this->make_(8, ValueType::VALUE_TYPE_FLOAT, 87, 1, 0x00E8, 2);
  this->poll_(sensor, REPLY_87_1);
  EXPECT_TRUE(std::isnan(sensor.state));
}

TEST_F(GenibusSensorTest, IgnoresOtherType) {
  auto &sensor = this->make_(8, ValueType::VALUE_TYPE_FLOAT, 87, 1, 0x0130, 1);
  this->poll_(sensor, REPLY_87_1);
  EXPECT_TRUE(std::isnan(sensor.state));
}

TEST_F(GenibusSensorTest, IgnoresLateReplyToAnotherObject) {
  auto &sensor = this->make_(8, ValueType::VALUE_TYPE_FLOAT);
  this->poll_(sensor, REPLY_93_1);
  EXPECT_TRUE(std::isnan(sensor.state));
  this->poll_(sensor, REPLY_87_1);
  EXPECT_FLOAT_EQ(sensor.state, 6.3397216796875f);
}

TEST_F(GenibusSensorTest, ReadsBodyCutShort) {
  auto &sensor = this->make_(44, ValueType::VALUE_TYPE_UINT16, 93, 78, 0x011A, 1);
  sensor.update();
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 1u);
  EXPECT_EQ(this->bus_.written[0], FRAME_GET_93_78);
  this->bus_.receive_bytes(REPLY_93_78_CUT);
  EXPECT_EQ(sensor.state, 13280.0f);
}

TEST_F(GenibusSensorTest, UnknownPastTheBytesSent) {
  // 46 bytes sent of 80: a float at 44 lies within the object but past the bytes sent
  auto &sensor = this->make_(44, ValueType::VALUE_TYPE_FLOAT, 93, 78, 0x011A, 1);
  this->poll_(sensor, REPLY_93_78_CUT);
  EXPECT_TRUE(std::isnan(sensor.state));
}

TEST(GenibusParseObjectPrefix, AcceptsCutBodyRejectsLongBody) {
  DataObject object{};
  const Bytes cut(REPLY_93_78_CUT.begin() + 6, REPLY_93_78_CUT.end() - 2);
  ASSERT_TRUE(parse_object_prefix(Apdu{10, 0, cut}, object));
  EXPECT_EQ(object.type, 0x011A);
  EXPECT_EQ(object.version, 1);
  EXPECT_EQ(object.body.size(), 46u);
  // Header size 25 below the 26 body bytes: broken (changed from the 87/1 reply)
  Bytes long_body(REPLY_87_1.begin() + 6, REPLY_87_1.end() - 2);
  long_body[6] = 25;
  EXPECT_FALSE(parse_object_prefix(Apdu{10, 0, long_body}, object));
  EXPECT_FALSE(parse_object_prefix(Apdu{10, 2, cut}, object));
  EXPECT_FALSE(parse_object_prefix(Apdu{3, 0, cut}, object));
  EXPECT_FALSE(parse_object_prefix(Apdu{10, 0, std::span<const uint8_t>(cut).first(6)}, object));
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
  this->poll_(sensor, REPLY_OBJECT_MISSING);
  EXPECT_TRUE(std::isnan(sensor.state));
  ASSERT_EQ(this->bus_.written.size(), 2u);
  sensor.update();
  this->run_();
  EXPECT_EQ(this->bus_.written.size(), 2u);
  this->bus_.lose_connection();
  this->bus_.establish_connection();  // on_connected() reads at once
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 3u);
  EXPECT_EQ(this->bus_.written[2], FRAME_GET_87_1);
}

TEST_F(GenibusSensorTest, OneMissingReplyAloneKeepsAsking) {
  // A late failed SET acknowledgement of another device looks the same
  auto &sensor = this->make_(8, ValueType::VALUE_TYPE_FLOAT);
  this->poll_(sensor, REPLY_OBJECT_MISSING);
  this->poll_(sensor, REPLY_87_1);
  EXPECT_FLOAT_EQ(sensor.state, 6.3397216796875f);
  this->poll_(sensor, REPLY_OBJECT_MISSING);
  this->poll_(sensor, REPLY_87_1);
  EXPECT_FLOAT_EQ(sensor.state, 6.3397216796875f);
  EXPECT_EQ(this->bus_.written.size(), 4u);
}

TEST_F(GenibusSensorTest, UnknownIdTwiceMarksObjectMissing) {
  auto &sensor = this->make_(8, ValueType::VALUE_TYPE_FLOAT);
  this->poll_(sensor, REPLY_UNKNOWN_ID);
  this->poll_(sensor, REPLY_UNKNOWN_ID);
  EXPECT_TRUE(std::isnan(sensor.state));
  sensor.update();
  this->run_();
  EXPECT_EQ(this->bus_.written.size(), 2u);
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
