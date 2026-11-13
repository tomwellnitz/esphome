#include <gtest/gtest.h>

#include <cmath>

#include "../common.h"

namespace esphome::grundfos_alpha::testing {

TEST_F(GrundfosAlphaTest, IdentifiesThenChainsBothObjects) {
  this->read_all_();
  EXPECT_FLOAT_EQ(this->flow_.state, 3600.0f / 4096.0f);  // m³/h
  EXPECT_FLOAT_EQ(this->head_.state, 4.0f);               // 39216 Pa at 9804 Pa/m
  EXPECT_FLOAT_EQ(this->medium_temperature_.state, 25.5f);
  EXPECT_FLOAT_EQ(this->voltage_.state, 230.0f);
  EXPECT_FLOAT_EQ(this->current_.state, 0.125f);
  EXPECT_FLOAT_EQ(this->power_.state, 19.5f);  // dc_power_consumption, not power_consumption
  EXPECT_FLOAT_EQ(this->speed_.state, 3000.0f);
  EXPECT_TRUE(this->next_request_().empty());
}

// Both objects go in one request, as recorded on a real ALPHA3, and each APDU of the reply goes to its object
TEST_F(GrundfosAlphaTest, DecodesTheRecordedReplyOfBothObjects) {
  EXPECT_EQ(FRAME_PROCESS_MOTOR, FRAME_RECORDED_MEASUREMENTS);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_RECORDED_MEASUREMENTS);
  this->reply_(REPLY_RECORDED_MEASUREMENTS);
  EXPECT_FLOAT_EQ(this->flow_.state, 0.00017030404706019908f * 3600.0f);
  EXPECT_FLOAT_EQ(this->head_.state, 12077.2412109375f / 9804.0f);
  EXPECT_FLOAT_EQ(this->medium_temperature_.state, 39.32610321044922f);
  EXPECT_FLOAT_EQ(this->voltage_.state, 220.60073852539062f);
  EXPECT_FLOAT_EQ(this->current_.state, 0.03800658881664276f);
  EXPECT_FLOAT_EQ(this->power_.state, 6.338653564453125f);
  EXPECT_FLOAT_EQ(this->speed_.state, 2402.9296875f);
  EXPECT_FALSE(this->pump_.status_has_warning());
  EXPECT_TRUE(this->next_request_().empty());
}

// GENIbus replies carry no object IDs: a reply with fewer or more APDUs than asked for is not spread over the objects
TEST_F(GrundfosAlphaTest, ReplyWithAnotherApduCountGivesNan) {
  this->read_all_();
  for (const Bytes &reply : {REPLY_PROCESS, bundle({REPLY_PROCESS, REPLY_MOTOR, REPLY_MOTOR})}) {
    this->pump_.update();
    ASSERT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);
    this->reply_(reply);
    EXPECT_TRUE(std::isnan(this->flow_.state));
    EXPECT_TRUE(std::isnan(this->power_.state));
    EXPECT_TRUE(this->next_request_().empty());
    this->pump_.update();
    ASSERT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);
    this->reply_(REPLY_PROCESS_MOTOR);
    EXPECT_FLOAT_EQ(this->power_.state, 19.5f);
  }
}

TEST_F(GrundfosAlphaTest, IdentifiesOnlyOnce) {
  this->read_all_();
  this->pump_.update();
  EXPECT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);
}

TEST_F(GrundfosAlphaTest, ReadsOnlyObjectsWithSensors) {
  this->pump_.set_flow_sensor(nullptr);
  this->pump_.set_head_sensor(nullptr);
  this->pump_.set_medium_temperature_sensor(nullptr);
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_ALPHA3);
  ASSERT_EQ(this->next_request_(), FRAME_MOTOR);
  this->reply_(REPLY_MOTOR);
  EXPECT_FLOAT_EQ(this->power_.state, 19.5f);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(GrundfosAlphaTest, UnsupportedModelReadsNothing) {
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_ALPHA15);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_TRUE(this->pump_.status_has_warning());
  this->pump_.update();
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->flow_.has_state());
  EXPECT_FALSE(this->power_.has_state());
}

TEST_F(GrundfosAlphaTest, IdentityErrorIsRetried) {
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_UNKNOWN_ID);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_TRUE(this->pump_.status_has_warning());
  this->pump_.update();
  EXPECT_EQ(this->next_request_(), FRAME_IDENTITY);
}

TEST_F(GrundfosAlphaTest, ShortIdentityIsRetriedAndWarningClearsOnSuccess) {
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_SHORT);
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_TRUE(this->pump_.status_has_warning());
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_SHORT);
  EXPECT_TRUE(this->next_request_().empty());
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_ALPHA3);
  EXPECT_FALSE(this->pump_.status_has_warning());
  EXPECT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);
}

TEST_F(GrundfosAlphaTest, ResponseWithoutOpenRequestIsIgnored) {
  // Before the identity is known
  this->pump_.on_response({genibus::GenibusError::GENIBUS_ERROR_NONE, {}});
  EXPECT_TRUE(this->next_request_().empty());
  // After the chain has ended
  this->read_all_();
  ASSERT_TRUE(this->next_request_().empty());
  this->pump_.on_response({genibus::GenibusError::GENIBUS_ERROR_NONE, {}});
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FLOAT_EQ(this->power_.state, 19.5f);
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(GrundfosAlphaTest, UnexpectedVersionIsNotDecoded) {
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_ALPHA3);
  ASSERT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);
  this->reply_(bundle({REPLY_PROCESS, REPLY_MOTOR_V2}));
  EXPECT_FLOAT_EQ(this->head_.state, 4.0f);
  EXPECT_TRUE(this->power_.has_state());
  EXPECT_TRUE(std::isnan(this->power_.state));
  EXPECT_TRUE(std::isnan(this->voltage_.state));
  EXPECT_TRUE(std::isnan(this->speed_.state));
}

TEST_F(GrundfosAlphaTest, ErrorAckGivesNanForItsObjectOnly) {
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_ALPHA3);
  ASSERT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);
  this->reply_(bundle({REPLY_PROCESS_ID_UNKNOWN, REPLY_MOTOR}));
  EXPECT_TRUE(std::isnan(this->flow_.state));
  EXPECT_TRUE(std::isnan(this->head_.state));
  EXPECT_FLOAT_EQ(this->power_.state, 19.5f);
}

TEST_F(GrundfosAlphaTest, MissingObjectGivesNanAndIsSkippedUntilReconnect) {
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->reply_(REPLY_IDENTITY_ALPHA3);
  ASSERT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);
  this->reply_(bundle({REPLY_OBJECT_MISSING, REPLY_MOTOR}));
  EXPECT_TRUE(this->flow_.has_state());
  EXPECT_TRUE(std::isnan(this->flow_.state));
  EXPECT_TRUE(std::isnan(this->head_.state));
  EXPECT_TRUE(std::isnan(this->medium_temperature_.state));
  EXPECT_FLOAT_EQ(this->voltage_.state, 230.0f);
  EXPECT_FALSE(this->pump_.status_has_warning());
  // The next update asks only for the object the pump has
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_MOTOR);
  this->reply_(REPLY_MOTOR);
  EXPECT_TRUE(this->next_request_().empty());
  // A new connection may be to a pump with other firmware
  this->bus_.lose_connection();
  this->bus_.establish_connection();
  EXPECT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);
}

TEST_F(GrundfosAlphaTest, TimeoutGivesNanForEveryObjectOfTheRequest) {
  this->read_all_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);
  this->run_(TIMEOUT_MS);
  EXPECT_TRUE(std::isnan(this->flow_.state));
  EXPECT_TRUE(std::isnan(this->power_.state));
  this->now_ += QUIET_MS;
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(GrundfosAlphaTest, SkipsUpdateWhileBusy) {
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_IDENTITY);
  this->pump_.update();
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(GrundfosAlphaTest, NotConnectedSkipsQuietly) {
  this->bus_.connected = false;
  this->pump_.update();
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->flow_.has_state());
  EXPECT_FALSE(this->bus_.status_has_warning());
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(GrundfosAlphaTest, DisconnectBetweenPollsGivesNanAtOnce) {
  this->read_all_();
  this->bus_.lose_connection();
  // No update() needed: the values turn unknown when the connection drops
  for (auto *sensor : {&this->flow_, &this->head_, &this->medium_temperature_, &this->voltage_, &this->current_,
                       &this->power_, &this->speed_})
    EXPECT_TRUE(std::isnan(sensor->state));
  EXPECT_FALSE(this->pump_.status_has_warning());
}

TEST_F(GrundfosAlphaTest, DisconnectDuringRequestGivesNanAndRecovers) {
  this->read_all_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);
  this->bus_.lose_connection();
  EXPECT_TRUE(std::isnan(this->flow_.state));
  EXPECT_TRUE(std::isnan(this->power_.state));
  this->bus_.connected = true;
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);  // identity stays known
  this->reply_(REPLY_PROCESS_MOTOR);
  EXPECT_FLOAT_EQ(this->head_.state, 4.0f);
}

TEST_F(GrundfosAlphaTest, FirstConnectionIdentifiesAtOnce) {
  this->bus_.connected = false;
  this->bus_.establish_connection();
  EXPECT_EQ(this->next_request_(), FRAME_IDENTITY);
}

TEST_F(GrundfosAlphaTest, ReconnectReadsAtOnce) {
  this->read_all_();
  this->bus_.lose_connection();
  EXPECT_TRUE(std::isnan(this->flow_.state));
  this->bus_.establish_connection();
  ASSERT_EQ(this->next_request_(), FRAME_PROCESS_MOTOR);  // identity stays known
  this->reply_(REPLY_PROCESS_MOTOR);
  EXPECT_FLOAT_EQ(this->head_.state, 4.0f);
}

}  // namespace esphome::grundfos_alpha::testing
