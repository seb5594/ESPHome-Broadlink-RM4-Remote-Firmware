// Host tests for the frequency sweep, the detection of repeated frames and the
// settle gate. No ESPHome and no hardware needed:
//
//   g++ -std=c++20 -Wall -Wextra -I.. test_sweep.cpp -o test_sweep && ./test_sweep

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "edge_ring.h"
#include "frame_match.h"
#include "frequency_options.h"
#include "settle_gate.h"
#include "sweep.h"

using namespace esphome::bl3361;

static int failures = 0;

#define CHECK(condition)                                               \
  do {                                                                 \
    if (!(condition)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      failures++;                                                      \
    }                                                                  \
  } while (0)

// A module with one transmitter on `signal_khz`. A channel hears it when the
// transmitter is inside the channel, which is as wide as the step.
struct FakeRadio {
  explicit FakeRadio(double signal_khz) : signal_khz(signal_khz) {}

  bool tune(const FrequencyPlan &p) {
    tunings++;
    if (fail_tune || tunings <= failing_tunings)
      return false;
    plan = p;
    return true;
  }

  bool read_rssi(uint8_t channel, uint8_t *rssi) {
    reads++;
    if (lose_all_reads || (lose_every != 0 && reads % lose_every == 0))
      return false;
    seed = seed * 1103515245u + 12345u;
    const int noise = static_cast<int>((seed >> 16) % 3) - 1;  // quiet band: 20 to 22
    const double centre = plan.channel_khz(channel);
    const bool hears = signal_khz > 0 && std::fabs(centre - signal_khz) <= plan.step_khz / 2.0;
    *rssi = static_cast<uint8_t>(21 + noise + (hears ? 6 : 0));
    return true;
  }

  double signal_khz;
  FrequencyPlan plan{};
  bool fail_tune{false};
  int failing_tunings{0};   // the first tunings are not answered
  int lose_every{0};        // every n-th reading is not answered
  bool lose_all_reads{false};
  uint32_t seed{1};
  int tunings{0};
  int reads{0};
};

// Runs the sweep with 8 ms per call, which is about what the main loop gives it.
static SweepState run(FrequencySweep &sweep, FakeRadio &radio, uint32_t *elapsed_ms = nullptr) {
  uint32_t now = 1000;
  sweep.start(now);
  while (sweep.running()) {
    now += 8;
    sweep.step(radio, now);
  }
  if (elapsed_ms != nullptr)
    *elapsed_ms = now - 1000;
  return sweep.state();
}

static void check_finds(double khz, double tolerance_khz) {
  FakeRadio radio(khz);
  FrequencySweep sweep;
  CHECK(run(sweep, radio) == SweepState::FOUND);
  const double error = std::fabs(static_cast<double>(sweep.result_khz()) - khz);
  if (error > tolerance_khz)
    std::printf("  signal %.2f kHz found at %u kHz\n", khz, (unsigned) sweep.result_khz());
  CHECK(error <= tolerance_khz);
}

static void test_sweep_finds_the_transmitter() {
  // Where 10 kHz steps exist the result is exact to 5 kHz...
  check_finds(433920, 5);
  check_finds(433920 + 4, 5);
  check_finds(433050, 5);
  check_finds(433950, 5);
  check_finds(315000, 5);
  check_finds(315500, 5);
  // ...elsewhere to the 100 kHz channel.
  check_finds(320500, 50);
  check_finds(305120, 50);
  check_finds(339500, 50);  // the coarse scan reaches up to 339.5 MHz, as in the stock firmware
  check_finds(431230, 50);
  check_finds(438300, 50);
  check_finds(434420, 50);
}

static void test_sweep_stays_inside_the_tuning_tables() {
  // Every request the sweep makes has to be one the module can be configured with.
  for (double khz : {305000.0, 339500.0, 430000.0, 439950.0, 433920.0, 315950.0, 339000.0}) {
    struct CheckingRadio : FakeRadio {
      using FakeRadio::FakeRadio;
      bool tune(const FrequencyPlan &p) {
        uint8_t blob[CONFIG_BLOB_SIZE];
        size_t size = 0;
        if (!build_config(p, blob, sizeof(blob), &size)) {
          std::printf("  no tuning for base %u step %u channels %u\n", (unsigned) p.base_khz, (unsigned) p.step_khz,
                      (unsigned) p.channels);
          bad_plans++;
        }
        return FakeRadio::tune(p);
      }
      int bad_plans{0};
    } radio(khz);
    FrequencySweep sweep;
    run(sweep, radio);
    CHECK(radio.bad_plans == 0);
  }
}

static void test_sweep_without_a_signal_gives_up() {
  FakeRadio radio(0);
  FrequencySweep sweep;
  uint32_t elapsed = 0;
  CHECK(run(sweep, radio, &elapsed) == SweepState::TIMEOUT);
  CHECK(elapsed > FrequencySweep::TIMEOUT_MS);
  CHECK(elapsed < FrequencySweep::TIMEOUT_MS + 100);
  // It kept listening through the bands instead of stopping after one round.
  CHECK(sweep.passes() > 3);
}

static void test_sweep_reports_a_broken_module() {
  FakeRadio radio(433920);
  radio.fail_tune = true;
  FrequencySweep sweep;
  CHECK(run(sweep, radio) == SweepState::FAILED);
}

static void test_sweep_survives_lost_answers() {
  // A module that now and then misses a request is still searched through.
  FakeRadio flaky(433920);
  flaky.lose_every = 7;
  FrequencySweep sweep;
  CHECK(run(sweep, flaky) == SweepState::FOUND);
  CHECK(std::fabs(static_cast<double>(sweep.result_khz()) - 433920.0) <= 10);

  // The first tuning of a window may fail as well.
  FakeRadio slow_start(315000);
  slow_start.failing_tunings = FrequencySweep::MAX_TUNE_ATTEMPTS - 1;
  FrequencySweep second;
  CHECK(run(second, slow_start) == SweepState::FOUND);

  // One more and it is a failure, and the log can tell where.
  FakeRadio gone(433920);
  gone.failing_tunings = FrequencySweep::MAX_TUNE_ATTEMPTS;
  FrequencySweep third;
  CHECK(run(third, gone) == SweepState::FAILED);
  CHECK(third.failed_while_tuning());
}

static void test_sweep_gives_up_on_a_silent_module() {
  FakeRadio radio(433920);
  radio.lose_all_reads = true;
  FrequencySweep sweep;
  CHECK(run(sweep, radio) == SweepState::FAILED);
  CHECK(!sweep.failed_while_tuning());
  // It stopped after a few lost readings, not after a whole band of them.
  CHECK(radio.reads <= FrequencySweep::MAX_LOST_READINGS + FrequencySweep::CHANNELS_PER_STEP);
}

static void test_sweep_does_not_block_the_loop() {
  // One call reads a handful of channels at most.
  FakeRadio radio(433920);
  FrequencySweep sweep;
  sweep.start(0);
  int worst = 0;
  while (sweep.running()) {
    const int before = radio.reads + radio.tunings;
    sweep.step(radio, 10);
    worst = std::max(worst, radio.reads + radio.tunings - before);
  }
  CHECK(worst <= FrequencySweep::CHANNELS_PER_STEP);
}

static std::vector<int32_t> frame(std::initializer_list<int32_t> signal, int32_t gap) {
  std::vector<int32_t> out(signal);
  out.push_back(gap);
  return out;
}

static void test_frame_matching() {
  const auto a = frame({+330, -990, +980, -340, +320, -1000}, -12000);
  // The same signal with jitter and a different closing pause.
  const auto b = frame({+300, -1040, +1010, -310, +350, -960}, -15000);
  CHECK(frames_match(a, b));
  // Different pulses, different length, different polarity.
  CHECK(!frames_match(a, frame({+330, -990, +980, -340, +320, -2500}, -12000)));
  CHECK(!frames_match(a, frame({+330, -990, +980, -340, +320}, -12000)));
  CHECK(!frames_match(a, frame({-330, +990, -980, +340, -320, +1000}, -12000)));
  CHECK(!frames_match(a, std::vector<int32_t>{}));
  CHECK(!frames_match(std::vector<int32_t>{-5000}, std::vector<int32_t>{-5000}));
}

static void test_collector() {
  // The test frames are short, so the minimum length is lowered for them.
  FrameCollector collector(2, 12, 4);
  const auto noise1 = frame({+150, -7626, +119, -7638, +100, -4382}, -21558);
  const auto noise2 = frame({+325, -12526, +125, -2126, +312, -14221}, -19684);
  const auto signal = frame({+263, -325, +262, -319, +275, -312, +269, -331}, -12958);
  auto repeat = signal;
  repeat[1] = -340;
  repeat.back() = -16000;

  CHECK(!collector.push(noise1));
  CHECK(!collector.push(noise2));
  CHECK(!collector.push(signal));
  CHECK(collector.push(repeat));
  CHECK(collector.repeats() == 2);
  CHECK(frames_match(collector.code(), signal));

  // Noise alone never completes a session, however much of it there is.
  FrameCollector only_noise(2, 12, 4);
  uint32_t seed = 7;
  for (int i = 0; i < 40; i++) {
    std::vector<int32_t> f;
    for (int p = 0; p < 20; p++) {
      seed = seed * 1103515245u + 12345u;
      const int32_t length = 100 + static_cast<int32_t>((seed >> 16) % 9000);
      f.push_back(p % 2 ? -length : length);
    }
    f.push_back(-20000);
    CHECK(!only_noise.push(f));
  }

  // A signal that has to be seen three times.
  FrameCollector strict(3, 12, 4);
  CHECK(!strict.push(signal));
  CHECK(!strict.push(repeat));
  CHECK(strict.push(signal));
  CHECK(strict.repeats() == 3);
}

// A frame from a real RM4 Pro log: a remote held down repeats its code with a
// pause of about 10 ms, which is shorter than the 15 ms that end a frame, so one
// received frame holds the code over and over.
static std::vector<int32_t> real_code(int32_t gap, int jitter) {
  std::vector<int32_t> code = {+294, -294, +294, -300, +581, -594, +287, -296, +298, -294, +607, -608, +548, -300,
                               +287, -294, +294, -593, +582, -300, +288, -598, +298, -297, +581, -299, +289, -306,
                               +294, -577, +292, -294, +587, -300, +288, -300, +288, -594, +287, -306, +582, -587,
                               +294, -300, +581, -294, +294, -594, +287, -300, +588, -293, +288, -300, +300, -582,
                               +293, -294, +300, -294, +294, -293, +588, -300, +281, -594, +294, -294, +293};
  for (size_t i = 0; i < code.size(); i++)
    code[i] += (i % 2 ? -1 : 1) * jitter;
  code.push_back(gap);
  return code;
}

static void test_collector_ignores_tiny_signals() {
  // Two noise frames that share one short pulse are not a learned code.
  FrameCollector collector;
  CHECK(!collector.push(frame({+300, -500, +300, -500}, -9000)));
  CHECK(!collector.push(frame({+300, -500, +300, -500}, -9000)));
  CHECK(collector.code().empty());
}

static void test_split_burst() {
  // Three repetitions glued together, as the receiver delivers them.
  std::vector<int32_t> burst;
  for (const auto &piece : {real_code(-9827, 0), real_code(-10093, 6), real_code(-10045, -5)})
    burst.insert(burst.end(), piece.begin(), piece.end());
  const auto pieces = split_burst(burst);
  CHECK(pieces.size() == 3);
  for (const auto &piece : pieces)
    CHECK(piece.back() <= -9000);

  // A trailing repetition that never got its pause is not a complete signal.
  auto cut = burst;
  cut.insert(cut.end(), {+287, -300, +294});
  CHECK(split_burst(cut).size() == 3);

  // No long pause: one piece at most, none if the frame ends on a mark.
  CHECK(split_burst(std::vector<int32_t>{+300, -300, +300}).empty());
  // A pause of exactly the limit counts, one microsecond less does not.
  CHECK(split_burst(std::vector<int32_t>{+300, -4000}).size() == 1);
  CHECK(split_burst(std::vector<int32_t>{+300, -3999}).empty());
}

static void test_collector_learns_from_a_burst() {
  // The whole first press arrives as one frame of repeated codes.
  std::vector<int32_t> burst;
  for (const auto &piece : {real_code(-9827, 0), real_code(-10093, 6), real_code(-10045, -5)})
    burst.insert(burst.end(), piece.begin(), piece.end());

  FrameCollector collector;
  CHECK(collector.push_burst(burst));
  CHECK(collector.repeats() >= 2);
  CHECK(collector.code().size() == real_code(-9827, 0).size());
  CHECK(frames_match(collector.code(), real_code(-9900, 0)));

  // The same burst cut at an arbitrary place (the framer caps frames at 512
  // pulses) still yields the code from the complete repetitions inside.
  std::vector<int32_t> chopped(burst.begin() + 30, burst.end());
  FrameCollector second;
  CHECK(second.push_burst(chopped));

  // Noise frames, each with several long pauses, do not learn anything.
  FrameCollector noise;
  uint32_t seed = 11;
  for (int n = 0; n < 20; n++) {
    std::vector<int32_t> frame;
    for (int p = 0; p < 60; p++) {
      seed = seed * 1103515245u + 12345u;
      const int32_t length = 100 + static_cast<int32_t>((seed >> 16) % 8000);
      frame.push_back(p % 2 ? -length : length);
    }
    CHECK(!noise.push_burst(frame));
  }
}

static void test_settle_gate() {
  SettleGate gate;
  CHECK(!gate.is_closed(0));  // never started: open
  gate.close_for(1000, 1500);
  CHECK(gate.is_closed(1000));
  CHECK(gate.is_closed(2499));
  CHECK(!gate.is_closed(2500));
  CHECK(!gate.is_closed(2501));  // stays open once it has opened

  gate.close_for(5000, 0);  // a zero time disables it
  CHECK(!gate.is_closed(5000));

  // The millisecond counter wraps after 49 days.
  gate.close_for(0xFFFFFF00u, 1000);
  CHECK(gate.is_closed(0xFFFFFF80u));
  CHECK(gate.is_closed(10));
  CHECK(!gate.is_closed(0x300));
}

static void test_elapsed_time() {
  CHECK(elapsed_us(1500, 1000) == 500);
  CHECK(elapsed_us(1000, 1000) == 0);
  // The counter wrapped between the two readings: still the real interval.
  CHECK(elapsed_us(100, 0xFFFFFF00u) == 356);
  // The clock stepped back by 3 us: a glitch, not 71 minutes.
  CHECK(elapsed_us(997, 1000) == 0);
  CHECK(elapsed_us(5, 12) == 0);
  // A long quiet time is still a long quiet time (a bit more than 17 minutes).
  CHECK(elapsed_us(1000000000u, 0) == 1000000000u);
}

static void test_find_option() {
  const std::vector<uint32_t> options = {315000, 433920};
  CHECK(find_option(options, 315000) == 0);
  CHECK(find_option(options, 433920) == 1);
  CHECK(find_option(options, 433925) == 1);  // what a search can report
  CHECK(find_option(options, 433915) == 1);
  CHECK(find_option(options, 433930) == -1);
  CHECK(find_option(options, 868300) == -1);
  CHECK(find_option(std::vector<uint32_t>{}, 433920) == -1);
}

int main() {
  test_find_option();
  test_elapsed_time();
  test_sweep_finds_the_transmitter();
  test_sweep_stays_inside_the_tuning_tables();
  test_sweep_without_a_signal_gives_up();
  test_sweep_reports_a_broken_module();
  test_sweep_survives_lost_answers();
  test_sweep_gives_up_on_a_silent_module();
  test_sweep_does_not_block_the_loop();
  test_frame_matching();
  test_collector();
  test_collector_ignores_tiny_signals();
  test_split_burst();
  test_collector_learns_from_a_burst();
  test_settle_gate();
  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("all tests passed\n");
  return EXIT_SUCCESS;
}
