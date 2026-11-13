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

// Class 7 GET of product_name (1), device_prod_no (8), sw_ver_str (50) and ble_sw_ver_str (58), and the replies of a
// real ALPHA3
inline const Bytes FRAME_TEXT_1 = {0x27, 0x05, 0xE7, 0xF8, 0x07, 0x01, 0x01, 0x52, 0x38};
inline const Bytes FRAME_TEXT_8 = {0x27, 0x05, 0xE7, 0xF8, 0x07, 0x01, 0x08, 0xC3, 0x11};
inline const Bytes FRAME_TEXT_50 = {0x27, 0x05, 0xE7, 0xF8, 0x07, 0x01, 0x32, 0x54, 0x08};
inline const Bytes FRAME_TEXT_58 = {0x27, 0x05, 0xE7, 0xF8, 0x07, 0x01, 0x3A, 0xD5, 0x00};
inline const Bytes REPLY_PRODUCT_NAME = {0x24, 0x15, 0xF8, 0xE7, 0x07, 0x11, 0x41, 0x4C, 0x50, 0x48, 0x41, 0x33, 0x20,
                                         0x32, 0x35, 0x2D, 0x36, 0x30, 0x20, 0x31, 0x38, 0x30, 0x00, 0xCE, 0x11};
inline const Bytes REPLY_PRODUCT_NUMBER = {0x24, 0x0D, 0xF8, 0xE7, 0x07, 0x09, 0x39, 0x39, 0x33,
                                           0x37, 0x31, 0x39, 0x32, 0x39, 0x00, 0xDB, 0x55};
inline const Bytes REPLY_SOFTWARE_VERSION = {0x24, 0x1C, 0xF8, 0xE7, 0x07, 0x18, 0x39, 0x39, 0x33, 0x36, 0x35,
                                             0x30, 0x35, 0x32, 0x56, 0x30, 0x31, 0x2E, 0x30, 0x30, 0x2E, 0x30,
                                             0x36, 0x2E, 0x30, 0x30, 0x34, 0x32, 0x39, 0x00, 0x58, 0x30};
inline const Bytes REPLY_BLUETOOTH_SOFTWARE_VERSION = {0x24, 0x1C, 0xF8, 0xE7, 0x07, 0x18, 0x39, 0x39, 0x33, 0x36, 0x35,
                                                       0x30, 0x36, 0x38, 0x56, 0x30, 0x33, 0x2E, 0x30, 0x30, 0x2E, 0x30,
                                                       0x36, 0x2E, 0x30, 0x30, 0x30, 0x30, 0x31, 0x00, 0x25, 0x6C};
// Test vectors: "ALPHA3" without the closing 0, and class 7 ACK 2 (ID unknown)
inline const Bytes REPLY_TEXT_NO_ZERO = {0x24, 0x0A, 0xF8, 0xE7, 0x07, 0x06, 0x41,
                                         0x4C, 0x50, 0x48, 0x41, 0x33, 0x84, 0x3B};
inline const Bytes REPLY_TEXT_ID_UNKNOWN = {0x24, 0x04, 0xF8, 0xE7, 0x07, 0x80, 0x50, 0xC5};

// 88/10199 EventLogInfo type 0x00F3 v1 and 88/10200 + n EventLogEntry type 0x00F4 v1
inline const Bytes FRAME_GET_88_10199 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x58, 0x27, 0xD7, 0x9A, 0x76};
inline const Bytes FRAME_GET_88_10200 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x58, 0x27, 0xD8, 0x6B, 0x99};
inline const Bytes FRAME_GET_88_10205 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x58, 0x27, 0xDD, 0x3B, 0x3C};
inline const Bytes FRAME_GET_88_10206 = {0x27, 0x07, 0xE7, 0xF8, 0x0A, 0x03, 0x58, 0x27, 0xDE, 0x0B, 0x5F};
// A real ALPHA3: latest event 6, 6 entries, room for 40; entries 5 and 6: code 57 (dry running), type 1 and 2
inline const Bytes REPLY_EVENT_LOG_6 = {0x24, 0x12, 0xF8, 0xE7, 0x0A, 0x0E, 0x00, 0x00, 0xF3, 0x01, 0x00,
                                        0x00, 0x07, 0x00, 0x06, 0x00, 0x06, 0x00, 0x28, 0x00, 0x4D, 0x99};
inline const Bytes REPLY_EVENT_5 = {0x24, 0x16, 0xF8, 0xE7, 0x0A, 0x12, 0x00, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x0B,
                                    0x00, 0x05, 0x00, 0x39, 0x00, 0x00, 0x01, 0x66, 0xAE, 0x53, 0xC7, 0x4E, 0x44};
inline const Bytes REPLY_EVENT_6 = {0x24, 0x16, 0xF8, 0xE7, 0x0A, 0x12, 0x00, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x0B,
                                    0x00, 0x06, 0x00, 0x39, 0x00, 0x00, 0x02, 0x66, 0xAE, 0x53, 0xEE, 0xA4, 0x12};
// The log as a real ALPHA3 sent it after it was cleared
inline const Bytes REPLY_EVENT_LOG_EMPTY = {0x24, 0x12, 0xF8, 0xE7, 0x0A, 0x0E, 0x00, 0x00, 0xF3, 0x01, 0x00,
                                            0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0xE7, 0xE1};
// Test vectors: latest event 41 of 40 entries, which wraps to 88/10200, and entry 41 (code 57, type 3); latest event 7
// and its entry with an unknown type 9; a log of size 0
inline const Bytes REPLY_EVENT_LOG_41 = {0x24, 0x12, 0xF8, 0xE7, 0x0A, 0x0E, 0x00, 0x00, 0xF3, 0x01, 0x00,
                                         0x00, 0x07, 0x00, 0x29, 0x00, 0x28, 0x00, 0x28, 0x00, 0x28, 0x86};
inline const Bytes REPLY_EVENT_41 = {0x24, 0x16, 0xF8, 0xE7, 0x0A, 0x12, 0x00, 0x00, 0xF4, 0x01, 0x00, 0x00, 0x0B,
                                     0x00, 0x29, 0x00, 0x39, 0x00, 0x00, 0x03, 0x66, 0xAE, 0x53, 0xEE, 0x61, 0xFF};
inline const Bytes REPLY_EVENT_LOG_7 = {0x24, 0x12, 0xF8, 0xE7, 0x0A, 0x0E, 0x00, 0x00, 0xF3, 0x01, 0x00,
                                        0x00, 0x07, 0x00, 0x07, 0x00, 0x07, 0x00, 0x28, 0x00, 0x7E, 0x8D};
inline const Bytes REPLY_EVENT_7_TYPE_9 = {0x24, 0x16, 0xF8, 0xE7, 0x0A, 0x12, 0x00, 0x00, 0xF4,
                                           0x01, 0x00, 0x00, 0x0B, 0x00, 0x07, 0x00, 0x39, 0x00,
                                           0x00, 0x09, 0x66, 0xAE, 0x53, 0xEE, 0x27, 0xA8};
inline const Bytes REPLY_EVENT_LOG_SIZE_0 = {0x24, 0x12, 0xF8, 0xE7, 0x0A, 0x0E, 0x00, 0x00, 0xF3, 0x01, 0x00,
                                             0x00, 0x07, 0x00, 0x06, 0x00, 0x06, 0x00, 0x00, 0x00, 0xC2, 0xD6};

class ProductTextTest : public PumpTest {
 protected:
  void SetUp() override {
    PumpTest::SetUp();
    this->pump_.set_product_name_text_sensor(&this->product_name_);
    this->pump_.set_product_number_text_sensor(&this->product_number_);
    this->pump_.set_software_version_text_sensor(&this->software_version_);
    this->pump_.set_bluetooth_software_version_text_sensor(&this->bluetooth_software_version_);
  }

  void read_texts_() {
    ASSERT_EQ(this->next_request_(), FRAME_TEXT_1);
    this->reply_(REPLY_PRODUCT_NAME);
    ASSERT_EQ(this->next_request_(), FRAME_TEXT_8);
    this->reply_(REPLY_PRODUCT_NUMBER);
    ASSERT_EQ(this->next_request_(), FRAME_TEXT_50);
    this->reply_(REPLY_SOFTWARE_VERSION);
    ASSERT_EQ(this->next_request_(), FRAME_TEXT_58);
    this->reply_(REPLY_BLUETOOTH_SOFTWARE_VERSION);
  }

  text_sensor::TextSensor product_name_, product_number_, software_version_, bluetooth_software_version_;
};

TEST_F(ProductTextTest, TextsAreReadOncePerConnection) {
  this->identify_();
  this->read_texts_();
  EXPECT_EQ(this->product_name_.state, "ALPHA3 25-60 180");
  EXPECT_EQ(this->product_number_.state, "99371929");
  EXPECT_EQ(this->software_version_.state, "99365052V01.00.06.00429");
  EXPECT_EQ(this->bluetooth_software_version_.state, "99365068V03.00.06.00001");
  EXPECT_TRUE(this->next_request_().empty());
  this->pump_.update();
  EXPECT_TRUE(this->next_request_().empty());
  // They stay while the pump is away, and are read again when it is back
  this->bus_.lose_connection();
  EXPECT_EQ(this->product_name_.state, "ALPHA3 25-60 180");
  this->bus_.establish_connection();
  this->read_texts_();
}

TEST_F(ProductTextTest, TextWithoutAnswerIsAskedAgain) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_1);
  this->run_(TIMEOUT_MS);
  EXPECT_FALSE(this->product_name_.has_state());
  this->now_ += QUIET_MS;
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_8);
  this->reply_(REPLY_PRODUCT_NUMBER);
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_50);
  this->reply_(REPLY_SOFTWARE_VERSION);
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_58);
  this->reply_(REPLY_BLUETOOTH_SOFTWARE_VERSION);
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_1);
  this->reply_(REPLY_PRODUCT_NAME);
  EXPECT_EQ(this->product_name_.state, "ALPHA3 25-60 180");
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(ProductTextTest, RefusedTextIsNotAskedAgain) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_1);
  this->reply_(REPLY_TEXT_ID_UNKNOWN);
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_8);
  this->reply_(REPLY_TEXT_NO_ZERO);
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_50);
  this->reply_(REPLY_SOFTWARE_VERSION);
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_58);
  this->reply_(REPLY_BLUETOOTH_SOFTWARE_VERSION);
  EXPECT_FALSE(this->product_name_.has_state());
  EXPECT_FALSE(this->product_number_.has_state());
  EXPECT_EQ(this->software_version_.state, "99365052V01.00.06.00429");
  this->pump_.update();
  EXPECT_TRUE(this->next_request_().empty());
}

// Replies that look like late replies to an earlier request: another text, or a reply of another class
TEST_F(ProductTextTest, LateReplyIsNotShownAndAskedOnceMore) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_1);
  this->reply_(REPLY_PRODUCT_NUMBER);
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_8);
  this->reply_(REPLY_SOFTWARE_VERSION);
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_50);
  this->reply_(REPLY_EVENT_LOG_6);
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_58);
  this->reply_(REPLY_PRODUCT_NAME);
  EXPECT_FALSE(this->product_name_.has_state());
  EXPECT_FALSE(this->product_number_.has_state());
  EXPECT_FALSE(this->software_version_.has_state());
  EXPECT_FALSE(this->bluetooth_software_version_.has_state());
  this->pump_.update();
  this->read_texts_();
  EXPECT_EQ(this->product_name_.state, "ALPHA3 25-60 180");
  EXPECT_EQ(this->product_number_.state, "99371929");
  EXPECT_EQ(this->software_version_.state, "99365052V01.00.06.00429");
  EXPECT_EQ(this->bluetooth_software_version_.state, "99365068V03.00.06.00001");
  this->pump_.update();
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(ProductTextTest, TextThatDoesNotFitTwiceIsNotAskedAgain) {
  this->identify_();
  for (int round = 0; round < 2; round++) {
    if (round == 0) {
      ASSERT_EQ(this->next_request_(), FRAME_TEXT_1);
      this->reply_(REPLY_PRODUCT_NAME);
      ASSERT_EQ(this->next_request_(), FRAME_TEXT_8);
      this->reply_(REPLY_PRODUCT_NUMBER);
      ASSERT_EQ(this->next_request_(), FRAME_TEXT_50);
      this->reply_(REPLY_SOFTWARE_VERSION);
    }
    ASSERT_EQ(this->next_request_(), FRAME_TEXT_58);
    this->reply_(REPLY_PRODUCT_NUMBER);  // digits without a V
    this->pump_.update();
  }
  EXPECT_FALSE(this->bluetooth_software_version_.has_state());
  EXPECT_TRUE(this->next_request_().empty());
}

class LastEventTest : public PumpTest {
 protected:
  void SetUp() override {
    PumpTest::SetUp();
    this->pump_.set_last_event_text_sensor(&this->last_event_);
  }

  void read_event_6_() {
    ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
    this->reply_(REPLY_EVENT_LOG_6);
    ASSERT_EQ(this->next_request_(), FRAME_GET_88_10205);
    this->reply_(REPLY_EVENT_6);
  }

  text_sensor::TextSensor last_event_;
};

TEST_F(LastEventTest, LatestEventIsShownAndReadOnlyWhenNew) {
  this->identify_();
  this->read_event_6_();
  EXPECT_EQ(this->last_event_.state, "Alarm cleared (code 57)");
  EXPECT_TRUE(this->next_request_().empty());
  // The same latest event: the entry is not read again
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_6);
  EXPECT_TRUE(this->next_request_().empty());
  // A new event
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_7);
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10206);
  this->reply_(REPLY_EVENT_7_TYPE_9);
  EXPECT_EQ(this->last_event_.state, "Event (code 57)");
}

TEST_F(LastEventTest, EmptyLogShowsNoEvents) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_EMPTY);
  EXPECT_EQ(this->last_event_.state, "No events");
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(LastEventTest, LogWrapsAround) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_41);
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10200);
  this->reply_(REPLY_EVENT_41);
  EXPECT_EQ(this->last_event_.state, "Warning appeared (code 57)");
}

TEST_F(LastEventTest, EntryOfAnotherEventIsNotShownNorReadAgain) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_6);
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10205);
  this->reply_(REPLY_EVENT_5);
  EXPECT_FALSE(this->last_event_.has_state());
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_6);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(LastEventTest, EntryOfAnotherEventEmptiesTheShownEvent) {
  this->identify_();
  this->read_event_6_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_7);
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10206);
  this->reply_(REPLY_EVENT_6);
  EXPECT_EQ(this->last_event_.state, "");
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_7);
  EXPECT_TRUE(this->next_request_().empty());
}

TEST_F(LastEventTest, EntryWithoutAnswerIsReadAgain) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_6);
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10205);
  this->run_(TIMEOUT_MS);
  this->now_ += QUIET_MS;
  EXPECT_TRUE(this->next_request_().empty());
  this->pump_.update();
  this->read_event_6_();
  EXPECT_EQ(this->last_event_.state, "Alarm cleared (code 57)");
}

TEST_F(LastEventTest, BadLogIsNotUsed) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_SIZE_0);
  EXPECT_TRUE(this->next_request_().empty());
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_6);  // an entry instead of the log
  EXPECT_TRUE(this->next_request_().empty());
  EXPECT_FALSE(this->last_event_.has_state());
}

TEST_F(LastEventTest, BadLogEmptiesTheShownEvent) {
  this->identify_();
  this->read_event_6_();
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_EVENT_LOG_SIZE_0);
  EXPECT_EQ(this->last_event_.state, "");
  EXPECT_TRUE(this->next_request_().empty());
  // A good log again: the entry is read again
  this->pump_.update();
  this->read_event_6_();
  EXPECT_EQ(this->last_event_.state, "Alarm cleared (code 57)");
}

TEST_F(LastEventTest, MissingLogIsNotAskedForUntilReconnect) {
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10199);
  this->reply_(REPLY_OBJECT_MISSING);
  this->pump_.update();
  EXPECT_TRUE(this->next_request_().empty());
  this->bus_.lose_connection();
  this->bus_.establish_connection();
  EXPECT_EQ(this->next_request_(), FRAME_GET_88_10199);
}

TEST_F(LastEventTest, EmptyOnDisconnectAndShownAgain) {
  this->identify_();
  this->read_event_6_();
  this->bus_.lose_connection();
  EXPECT_EQ(this->last_event_.state, "");
  this->bus_.establish_connection();
  this->read_event_6_();
  EXPECT_EQ(this->last_event_.state, "Alarm cleared (code 57)");
}

TEST_F(PumpTest, NoSettingsReadWithoutEntities) {
  this->identify_();
  EXPECT_TRUE(this->next_request_().empty());
}

// The event log joins the measurements and settings in one request; the entry of its latest event is known only from
// the reply, so it is read in the next request
TEST_F(PumpTest, EventLogIsBundledAndTheEntryFollows) {
  sensor::Sensor flow;
  text_sensor::TextSensor operating_mode, last_event;
  this->pump_.set_flow_sensor(&flow);
  this->pump_.set_operating_mode_text_sensor(&operating_mode);
  this->pump_.set_last_event_text_sensor(&last_event);
  this->identify_();
  const Bytes poll = bundle({FRAME_PROCESS, FRAME_GET_86_8, FRAME_GET_88_10199});
  ASSERT_EQ(this->next_request_(), poll);
  this->reply_(bundle({REPLY_PROCESS, REPLY_REALIZED, REPLY_EVENT_LOG_6}));
  EXPECT_EQ(operating_mode.state, "Normal");
  ASSERT_EQ(this->next_request_(), FRAME_GET_88_10205);
  this->reply_(REPLY_EVENT_6);
  EXPECT_EQ(last_event.state, "Alarm cleared (code 57)");
  EXPECT_TRUE(this->next_request_().empty());
  // The same latest event: its entry is not read again
  this->pump_.update();
  ASSERT_EQ(this->next_request_(), poll);
  this->reply_(bundle({REPLY_PROCESS, REPLY_REALIZED, REPLY_EVENT_LOG_6}));
  EXPECT_TRUE(this->next_request_().empty());
}

// A class 7 text never shares a request: the objects before it go first, then the text alone
TEST_F(PumpTest, TextGoesInARequestOfItsOwn) {
  sensor::Sensor flow;
  text_sensor::TextSensor product_name;
  this->pump_.set_flow_sensor(&flow);
  this->pump_.set_product_name_text_sensor(&product_name);
  this->identify_();
  ASSERT_EQ(this->next_request_(), FRAME_PROCESS);
  this->reply_(REPLY_PROCESS);
  ASSERT_EQ(this->next_request_(), FRAME_TEXT_1);
  this->reply_(REPLY_PRODUCT_NAME);
  EXPECT_EQ(product_name.state, "ALPHA3 25-60 180");
  EXPECT_TRUE(this->next_request_().empty());
}

}  // namespace esphome::grundfos_alpha::testing
