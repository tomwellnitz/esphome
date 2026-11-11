#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "esphome/components/genibus/genibus.h"
#include "esphome/core/application.h"

// Every byte sequence here comes from or was checked with the geni.py frame tool
namespace esphome::genibus::testing {

namespace {

using Bytes = std::vector<uint8_t>;

const Bytes GET_FLOW_HEAD = {0x0A, 0x03, 0x5D, 0x01, 0x21};
const Bytes GET_POWER = {0x0A, 0x03, 0x57, 0x00, 0x45};
const Bytes FRAME_FLOW_HEAD = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x5D, 0x01, 0x21, 0x52, 0x1F};
const Bytes FRAME_POWER = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x57, 0x00, 0x45, 0x8A, 0xCD};
const Bytes REPLY_012F = {0x24, 0x12, 0xF8, 0xE7, 0x0A, 0x0E, 0x00, 0x01, 0x2F, 0x01, 0x00,
                          0x00, 0x07, 0x00, 0x00, 0x00, 0x46, 0xE5, 0xC6, 0x00, 0xC5, 0x36};
const Bytes REPLY_ACK = {0x24, 0x05, 0xF8, 0xE7, 0x0A, 0x01, 0x00, 0xAE, 0xA2};
const Bytes REPLY_ID_UNKNOWN = {0x24, 0x04, 0xF8, 0xE7, 0x0A, 0x80, 0x26, 0x99};
const Bytes REPLY_APDU_TOO_LONG = {0x24, 0x05, 0xF8, 0xE7, 0x0A, 0x03, 0x00, 0xC8, 0xC0};
const Bytes REPLY_OTHER_NODE = {0x24, 0x05, 0xF8, 0x20, 0x0A, 0x01, 0x00, 0x4C, 0x2B};

class TestBus : public GenibusBus {
 public:
  bool is_connected() const override { return this->connected; }
  void lose_connection() {
    this->connected = false;
    this->connection_lost_();
  }

  bool connected{true};
  bool write_ok{true};
  std::vector<Bytes> written;

 protected:
  bool write_frame(std::span<const uint8_t> frame) override {
    this->written.emplace_back(frame.begin(), frame.end());
    return this->write_ok;
  }
};

struct Received {
  GenibusError error;
  Bytes apdus;
};

class TestDevice : public GenibusDevice {
 public:
  void update() override {}
  void on_response(const GenibusResponse &response) override {
    this->responses.push_back({response.error, Bytes(response.apdus.begin(), response.apdus.end())});
    if (this->send_next) {
      this->send_next = false;
      this->parent_->send(this, GET_POWER);
    }
  }
  bool request_flow_head() { return this->request_object_(93, 289); }

  std::vector<Received> responses;
  bool send_next{false};
};

class GenibusBusTest : public ::testing::Test {
 protected:
  void SetUp() override {
    for (auto *device : {&this->pump_, &this->other_}) {
      device->set_parent(&this->bus_);
      this->bus_.register_device(device);
    }
    this->other_.set_address(0x20);
  }

  void run_(uint32_t ms = 16) {
    this->now_ += ms;
    LoopBlockingGuard guard{nullptr, nullptr, this->now_};
    this->bus_.loop();
  }

  void receive_(const Bytes &frame, size_t chunk = 20) {
    for (size_t at = 0; at < frame.size(); at += chunk)
      this->bus_.receive_bytes(std::span<const uint8_t>(frame).subspan(at, std::min(chunk, frame.size() - at)));
  }

  TestBus bus_;
  TestDevice pump_;
  TestDevice other_;
  uint32_t now_{1000};
};

}  // namespace

TEST_F(GenibusBusTest, SendsRequestAndDeliversReply) {
  ASSERT_TRUE(this->pump_.request_flow_head());
  EXPECT_TRUE(this->bus_.written.empty());  // the wire is written from loop()
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 1u);
  EXPECT_EQ(this->bus_.written[0], FRAME_FLOW_HEAD);
  this->receive_(REPLY_012F, 3);
  ASSERT_EQ(this->pump_.responses.size(), 1u);
  EXPECT_EQ(this->pump_.responses[0].error, GenibusError::GENIBUS_ERROR_NONE);
  EXPECT_EQ(this->pump_.responses[0].apdus, Bytes(REPLY_012F.begin() + 4, REPLY_012F.end() - 2));
  EXPECT_FALSE(this->bus_.status_has_warning());
}

TEST_F(GenibusBusTest, UsesConfiguredAddresses) {
  this->bus_.set_master_address(0x01);
  this->pump_.set_address(0x02);
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 1u);
  EXPECT_EQ(this->bus_.written[0][2], 0x02);
  EXPECT_EQ(this->bus_.written[0][3], 0x01);
  this->receive_(REPLY_ACK);  // addressed to 0xF8 from 0xE7, so not the answer
  EXPECT_TRUE(this->pump_.responses.empty());
}

TEST_F(GenibusBusTest, OneTransactionOnTheWire) {
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->bus_.send(&this->other_, GET_POWER);
  this->run_();
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 1u);
  this->receive_(REPLY_ACK);
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 2u);
  EXPECT_EQ(this->bus_.written[1][2], 0x20);
  EXPECT_EQ(this->pump_.responses.size(), 1u);
  EXPECT_TRUE(this->other_.responses.empty());
}

TEST_F(GenibusBusTest, TimeoutEndsTransaction) {
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->bus_.send(&this->other_, GET_POWER);
  this->run_();
  this->receive_({0x24, 0x12, 0xF8});  // a partial answer is dropped with the transaction
  this->run_(RESPONSE_TIMEOUT_MS - 1);
  EXPECT_TRUE(this->pump_.responses.empty());
  this->run_(1);
  ASSERT_EQ(this->pump_.responses.size(), 1u);
  EXPECT_EQ(this->pump_.responses[0].error, GenibusError::GENIBUS_ERROR_TIMEOUT);
  EXPECT_TRUE(this->pump_.responses[0].apdus.empty());
  EXPECT_TRUE(this->bus_.status_has_warning());
  ASSERT_EQ(this->bus_.written.size(), 2u);  // the next request went out in the same pass
  this->receive_(REPLY_OTHER_NODE);
  ASSERT_EQ(this->other_.responses.size(), 1u);
  EXPECT_EQ(this->other_.responses[0].error, GenibusError::GENIBUS_ERROR_NONE);
  EXPECT_FALSE(this->bus_.status_has_warning());
}

TEST_F(GenibusBusTest, CrcErrorEndsTransaction) {
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->run_();
  Bytes bad = REPLY_012F;
  bad[12] ^= 0x10;
  this->receive_(bad);
  ASSERT_EQ(this->pump_.responses.size(), 1u);
  EXPECT_EQ(this->pump_.responses[0].error, GenibusError::GENIBUS_ERROR_CRC);
  EXPECT_TRUE(this->bus_.status_has_warning());
}

TEST_F(GenibusBusTest, MalformedReplyEndsTransaction) {
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->run_();
  this->receive_(REPLY_APDU_TOO_LONG);
  ASSERT_EQ(this->pump_.responses.size(), 1u);
  EXPECT_EQ(this->pump_.responses[0].error, GenibusError::GENIBUS_ERROR_MALFORMED);
}

TEST_F(GenibusBusTest, AckErrorReachesDevice) {
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->run_();
  this->receive_(REPLY_ID_UNKNOWN);
  ASSERT_EQ(this->pump_.responses.size(), 1u);
  EXPECT_EQ(this->pump_.responses[0].error, GenibusError::GENIBUS_ERROR_NONE);
  ApduReader reader(this->pump_.responses[0].apdus);
  Apdu apdu{};
  ASSERT_TRUE(reader.next(apdu));
  EXPECT_EQ(apdu.ack, 2);
  DataObject object{};
  EXPECT_FALSE(parse_data_object(apdu, object));
}

TEST_F(GenibusBusTest, IgnoresOtherTraffic) {
  this->receive_(REPLY_ACK);  // no request open
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->run_();
  this->receive_(FRAME_FLOW_HEAD);   // an echo of the request
  this->receive_(REPLY_OTHER_NODE);  // another node's answer
  EXPECT_TRUE(this->pump_.responses.empty());
  this->receive_(REPLY_ACK);
  ASSERT_EQ(this->pump_.responses.size(), 1u);
  EXPECT_EQ(this->pump_.responses[0].error, GenibusError::GENIBUS_ERROR_NONE);
}

TEST_F(GenibusBusTest, NewRequestReplacesWaitingOne) {
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->bus_.send(&this->pump_, GET_POWER);
  this->run_();
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 1u);
  EXPECT_EQ(this->bus_.written[0], FRAME_POWER);
  this->receive_(REPLY_ACK);
  EXPECT_EQ(this->pump_.responses.size(), 1u);
}

TEST_F(GenibusBusTest, NewRequestReplacesOneOnTheWire) {
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->run_();
  this->bus_.send(&this->pump_, GET_POWER);
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 1u);  // still waiting for the first answer
  this->receive_(REPLY_012F);
  EXPECT_TRUE(this->pump_.responses.empty());  // the answer to the replaced request is dropped
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 2u);
  EXPECT_EQ(this->bus_.written[1], FRAME_POWER);
  this->receive_(REPLY_ACK);
  EXPECT_EQ(this->pump_.responses.size(), 1u);
}

TEST_F(GenibusBusTest, DeviceCanSendFromResponse) {
  this->pump_.send_next = true;
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->run_();
  this->receive_(REPLY_012F);
  this->run_();
  ASSERT_EQ(this->bus_.written.size(), 2u);
  EXPECT_EQ(this->bus_.written[1], FRAME_POWER);
}

TEST_F(GenibusBusTest, DisconnectEndsEveryRequest) {
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->run_();
  this->bus_.send(&this->other_, GET_POWER);
  this->receive_({0x24, 0x12, 0xF8, 0xE7});
  this->bus_.lose_connection();
  ASSERT_EQ(this->pump_.responses.size(), 1u);
  EXPECT_EQ(this->pump_.responses[0].error, GenibusError::GENIBUS_ERROR_DISCONNECTED);
  ASSERT_EQ(this->other_.responses.size(), 1u);
  EXPECT_EQ(this->other_.responses[0].error, GenibusError::GENIBUS_ERROR_DISCONNECTED);
  EXPECT_FALSE(this->bus_.send(&this->pump_, GET_FLOW_HEAD));
  this->run_();
  EXPECT_EQ(this->bus_.written.size(), 1u);
  this->bus_.connected = true;
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->run_();
  this->receive_(REPLY_ACK);  // the partial frame from before the drop is gone
  ASSERT_EQ(this->pump_.responses.size(), 2u);
  EXPECT_EQ(this->pump_.responses[1].error, GenibusError::GENIBUS_ERROR_NONE);
}

TEST_F(GenibusBusTest, TransportDownWithoutNotice) {
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->bus_.connected = false;
  this->run_();
  EXPECT_TRUE(this->bus_.written.empty());
  ASSERT_EQ(this->pump_.responses.size(), 1u);
  EXPECT_EQ(this->pump_.responses[0].error, GenibusError::GENIBUS_ERROR_DISCONNECTED);
}

TEST_F(GenibusBusTest, WriteFailureEndsTransaction) {
  this->bus_.write_ok = false;
  this->bus_.send(&this->pump_, GET_FLOW_HEAD);
  this->run_();
  ASSERT_EQ(this->pump_.responses.size(), 1u);
  EXPECT_EQ(this->pump_.responses[0].error, GenibusError::GENIBUS_ERROR_WRITE_FAILED);
}

TEST_F(GenibusBusTest, RefusesBadRequests) {
  EXPECT_FALSE(this->bus_.send(&this->pump_, {}));
  const Bytes too_long(MAX_REQUEST_SIZE + 1, 0);
  EXPECT_FALSE(this->bus_.send(&this->pump_, too_long));
  TestDevice stranger;
  stranger.set_parent(&this->bus_);
  EXPECT_FALSE(this->bus_.send(&stranger, GET_FLOW_HEAD));
  this->run_();
  EXPECT_TRUE(this->bus_.written.empty());
}

}  // namespace esphome::genibus::testing
