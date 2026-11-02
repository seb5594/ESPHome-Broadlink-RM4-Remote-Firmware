// Host tests for the pure logic of the bl3361 component. No ESPHome needed:
//
//   g++ -std=c++20 -Wall -Wextra -I.. test_bl3361.cpp -o test_bl3361 && ./test_bl3361
//
// The expected byte strings were produced by an independent script that
// follows the stock firmware's tables and rules, so a typo in the tables of
// radio_config.h shows up here.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "edge_ring.h"
#include "protocol.h"
#include "pulse_framer.h"
#include "radio_config.h"

using namespace esphome::bl3361;

static int failures = 0;

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
      failures++;                                                           \
    }                                                                       \
  } while (0)

static std::string to_hex(const uint8_t *data, size_t size) {
  std::string out;
  char byte[3];
  for (size_t i = 0; i < size; i++) {
    std::snprintf(byte, sizeof(byte), "%02x", data[i]);
    out += byte;
  }
  return out;
}

static void test_frames() {
  uint8_t frame[FRAME_MAX_SIZE];
  size_t size = encode_frame(COMMAND_IDLE, nullptr, 0, frame, sizeof(frame));
  CHECK(to_hex(frame, size) == "a5a55a5ac1c00e0600000000");

  const uint8_t channel[] = {2, DIRECTION_TX};
  size = encode_frame(COMMAND_SET_CHANNEL, channel, sizeof(channel), frame, sizeof(frame));
  CHECK(to_hex(frame, size) == "a5a55a5abbc00206020000000202");

  uint8_t tiny[8];
  CHECK(encode_frame(COMMAND_IDLE, nullptr, 0, tiny, sizeof(tiny)) == 0);
}

static void test_decoder() {
  uint8_t frame[FRAME_MAX_SIZE];
  const uint8_t payload[] = {0x42};
  const size_t size = encode_frame(reply_id(COMMAND_GET_RSSI), payload, 1, frame, sizeof(frame));

  FrameDecoder decoder;
  // Noise in front, including a run of the first magic byte.
  const uint8_t noise[] = {0x00, 0xA5, 0xA5, 0xA5, 0x13};
  for (uint8_t byte : noise)
    CHECK(!decoder.push(byte));
  bool done = false;
  for (size_t i = 0; i < size; i++)
    done = decoder.push(frame[i]);
  CHECK(done);
  CHECK(decoder.command() == 0x607);
  CHECK(decoder.payload_size() == 1);
  CHECK(decoder.payload()[0] == 0x42);

  // The same frame with a flipped payload bit must be rejected.
  decoder.reset();
  frame[size - 1] ^= 0x01;
  done = false;
  for (size_t i = 0; i < size; i++)
    done = decoder.push(frame[i]) || done;
  CHECK(!done);
  CHECK(decoder.checksum_errors() == 1);

  // A run of 0xA5 directly in front of the magic still finds the frame.
  decoder.reset();
  frame[size - 1] ^= 0x01;
  CHECK(!decoder.push(0xA5));
  done = false;
  for (size_t i = 0; i < size; i++)
    done = decoder.push(frame[i]);
  CHECK(done);
}

static void test_decoder_accepts_cut_replies() {
  // The reply of a module that lost the end of its frames and put a stray zero in front.
  const uint8_t cut_reply[] = {0x00, 0xA5, 0xA5, 0x5A, 0x5A, 0xB4, 0xC0, 0x01, 0x06, 0x00, 0x00};
  FrameDecoder decoder;
  bool done = false;
  for (uint8_t byte : cut_reply)
    done = decoder.push(byte);
  CHECK(done);
  CHECK(decoder.command() == reply_id(COMMAND_CONFIGURE));
  CHECK(decoder.payload_size() == 0);

  // A reply with a payload has to be complete, the payload is what it is about.
  uint8_t frame[FRAME_MAX_SIZE];
  const uint8_t payload[] = {0x42};
  const size_t size = encode_frame(reply_id(COMMAND_GET_RSSI), payload, 1, frame, sizeof(frame));
  decoder.reset();
  done = false;
  for (size_t i = 0; i + 1 < size; i++)
    done = decoder.push(frame[i]) || done;
  CHECK(!done);

  // Damage in a reply without payload is still caught.
  decoder.reset();
  uint8_t bad[] = {0xA5, 0xA5, 0x5A, 0x5A, 0xB5, 0xC0, 0x01, 0x06, 0x00, 0x00};
  done = false;
  for (uint8_t byte : bad)
    done = decoder.push(byte) || done;
  CHECK(!done);
}

static void check_config(uint32_t frequency_khz, uint32_t base, uint32_t step, uint8_t channel, const char *expected_hex) {
  FrequencyPlan plan{};
  CHECK(plan_frequency(frequency_khz, &plan));
  CHECK(plan.base_khz == base);
  CHECK(plan.step_khz == step);
  CHECK(plan.channels == 20);
  CHECK(plan.channel == channel);
  CHECK(plan.frequency_khz() == frequency_khz - frequency_khz % step);
  uint8_t blob[CONFIG_BLOB_SIZE];
  size_t size = 0;
  CHECK(build_config(plan, blob, sizeof(blob), &size));
  CHECK(size == CONFIG_BLOB_SIZE);
  CHECK(to_hex(blob, size) == expected_hex);
}

static void test_config() {
  check_config(433920, 433900, 10, 2,
               "01c00000016602ec031d0470058006140708089109020a020bd00cae0de00e350f00100011f4121013e21442152016211781184"
               "219d71ac11b1c1c421dc01e0f1f1c20d3216422a023772400250026002700280029002a002b292cc02d282e0a2f533017310732"
               "3c332f3400352636053705381039083a003baa3c023d003e003f0040004100420043d4442d4500461f4700480049004a004b004c"
               "004d004e004f6050ff51005200531f5410555556265703580059035a705b005c8a5d185e3f5f7f06020a04070110080100");
  check_config(434000, 434000, 100, 0,
               "01c00000016602ec031d0470058006140708089109020a020bd00cae0de00e350f00100011f4121013e21442152016211781184"
               "219db1a001b1d1c421dc41e4e1f1c20d3216422a023772400250026002700280029002a002b292cc02d282e0a2f533017310732"
               "3c332f3400352636053705381039083a003baa3c023d003e003f0040004100420043d4442d4500461f4700480049004a004b004c"
               "004d004e004f6050ff51005200531f5410555556265703580059035a705b005c8a5d185e3f5f7f06020a28070110080100");
  check_config(315000, 315000, 10, 0,
               "01c00000016602ec031d0470058006140708089109020a020bd00cae0de00e350f00100011f4121013e21442152016211781184"
               "819d21a1e1b5c1c481db11e131f1b20d3216422a023772400250026002700280029002a002b292cc02d282e0a2f533017310732"
               "3c332f3400352636053705381039083a003baa3c023d003e003f0040004100420043d4442d4500461f4700480049004a004b004c"
               "004d004e004f6050ff51005200531f5410555556b95704580059035a705b005cb45d1e5e3f5f7f06020b04070110080100");
  check_config(438500, 438000, 100, 5,
               "01c00000016602ec031d0470058006140708089109020a020bd00cae0de00e350f00100011f4121013e21442152016211781184"
               "319781ad91b161c431d621e271f1620d3216422a023772400250026002700280029002a002b292cc02d282e0a2f533017310732"
               "3c332f3400352636053705381039083a003baa3c023d003e003f0040004100420043d4442d4500461f4700480049004a004b004c"
               "004d004e004f6050ff51005200531f5410555556265703580059035a705b005c8a5d185e3f5f7f06020b28070110080100");
}

static void test_unsupported_frequencies() {
  FrequencyPlan plan{};
  CHECK(!plan_frequency(868000, &plan));
  CHECK(!plan_frequency(304999, &plan));
  CHECK(!plan_frequency(340000, &plan));
  CHECK(!plan_frequency(440000, &plan));
  CHECK(plan_frequency(305000, &plan));
  CHECK(plan_frequency(439999, &plan));
}

static void test_every_supported_frequency_has_a_config() {
  uint8_t blob[CONFIG_BLOB_SIZE];
  size_t size = 0;
  unsigned supported = 0;
  for (uint32_t khz = 300000; khz < 450000; khz += 10) {
    FrequencyPlan plan{};
    if (!plan_frequency(khz, &plan))
      continue;
    supported++;
    CHECK(build_config(plan, blob, sizeof(blob), &size));
    CHECK(plan.channel < plan.channels);
    CHECK(plan.channel_khz(plan.channel) <= khz);
  }
  CHECK(supported > 0);
}

static void test_edge_ring() {
  EdgeRing<8> ring;
  uint32_t duration = 0;
  bool level = false;
  CHECK(!ring.pop(&duration, &level));
  for (uint32_t i = 1; i <= 7; i++)
    ring.push(i * 100, i % 2 == 0);
  ring.push(999, true);  // queue holds 7 entries, this one is dropped
  CHECK(ring.overflows() == 1);
  for (uint32_t i = 1; i <= 7; i++) {
    CHECK(ring.pop(&duration, &level));
    CHECK(duration == i * 100);
    CHECK(level == (i % 2 == 0));
  }
  CHECK(!ring.pop(&duration, &level));
}

// Feeds a list of signed durations (+ mark, - space) into the framer.
static bool feed(PulseFramer &framer, const std::vector<int32_t> &pulses) {
  bool done = false;
  for (int32_t pulse : pulses)
    done = framer.push(static_cast<uint32_t>(std::abs(pulse)), pulse > 0) || done;
  return done;
}

static void test_framer() {
  // A key fob like frame: sync pulse and a 10.8 ms pause, then bits made of
  // short and long pulses. The idle time has to be longer than the sync pause.
  const std::vector<int32_t> burst = {350,  -10800, 350,  -1050, 1050, -350, 350,  -1050, 1050, -350,
                                      350,  -1050,  350,  -1050, 1050, -350, 350,  -1050, 1050, -350,
                                      350,  -1050};
  PulseFramer framer(100, 20000, 10, 512);

  // Pause, glitches, the frame, then silence ends it.
  CHECK(!feed(framer, {-100000, 40, -30, 25, -60000}));
  CHECK(!feed(framer, burst));
  CHECK(framer.has_pending());
  CHECK(framer.flush());
  const std::vector<int32_t> &frame = framer.frame();
  CHECK(frame.size() == burst.size());
  CHECK(frame.back() == -20000);
  CHECK(frame[0] == 350);
  CHECK(frame[1] == -10800);
  CHECK(!framer.has_pending());

  // A short glitch inside a space is folded into the surrounding level.
  PulseFramer folding(100, 8000, 2, 512);
  CHECK(!feed(folding, {-20000, 400, -500, 30, -40, 400, -500, 400}));
  CHECK(folding.flush());
  const std::vector<int32_t> &folded = folding.frame();
  CHECK(folded.size() == 6);
  CHECK(folded[0] == 400);
  CHECK(folded[1] == -570);
  CHECK(folded[2] == 400);
  CHECK(folded[3] == -500);
  CHECK(folded.back() == -8000);

  // A pause inside the stream closes the frame by itself, without flush().
  PulseFramer paused(100, 20000, 4, 512);
  std::vector<int32_t> stream(burst.begin(), burst.end() - 1);
  stream.push_back(-30000);
  CHECK(!feed(paused, stream));
  CHECK(feed(paused, {350}));
  CHECK(paused.frame().back() == -30000);
  CHECK(paused.frame().size() == burst.size());

  // Noise only: nothing comes out.
  PulseFramer quiet(100, 8000, 10, 512);
  CHECK(!feed(quiet, {-50000, 20, -30, 40, -25, 30, -90, 60, -45}));
  CHECK(!quiet.flush());
  CHECK(quiet.dropped_frames() == 0);

  // A frame that never pauses is cut at the maximum length.
  PulseFramer endless(100, 20000, 2, 8);
  std::vector<int32_t> toggling;
  for (int i = 0; i < 20; i++)
    toggling.push_back(i % 2 == 0 ? 400 : -400);
  CHECK(feed(endless, toggling));
  CHECK(endless.frame().size() == 8);
}

int main() {
  test_frames();
  test_decoder();
  test_decoder_accepts_cut_replies();
  test_config();
  test_unsupported_frequencies();
  test_every_supported_frequency_has_a_config();
  test_edge_ring();
  test_framer();
  if (failures == 0)
    std::printf("all tests passed\n");
  return failures == 0 ? 0 : 1;
}
