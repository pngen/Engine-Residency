#ifndef ENGINE_RESIDENCY_CLOCK_HPP
#define ENGINE_RESIDENCY_CLOCK_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace engine_residency {

// ---------------------------------------------------------------------------
// Time and clock model.
//
// Freshness that depends on elapsed time must use an injectable monotonic
// clock. We never persist a monotonic clock value and compare it as though it
// belonged to the new process's clock domain (monotonic clocks are not
// meaningful across processes). Clock ownership must be explicit: who is the
// wall-clock source is determined by the owner of the runtime instance.
// ---------------------------------------------------------------------------

// Nanosecond count. Non-negative.
using DurationNs = std::uint64_t;

inline constexpr DurationNs kNanosecondsPerSecond = 1'000'000'000ULL;

// Abstract monotonic clock. Subclasses provide wall-time in nanoseconds.
class Clock {
 public:
  virtual ~Clock() = default;
  // Returns the current time in nanoseconds on a monotonic ordinal.
  virtual DurationNs now_ns() const = 0;
};

// Real steady clock using std::chrono::steady_clock.
class SteadyClock final : public Clock {
 public:
  DurationNs now_ns() const override {
    return static_cast<DurationNs>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
  }
};

// A manually-advanceable clock for deterministic tests. Caller controls time.
class ManualClock final : public Clock {
 public:
  explicit ManualClock(DurationNs start = 0) : now_(start) {}
  DurationNs now_ns() const override { return now_; }
  void advance(DurationNs delta) { now_ += delta; }
  void set(DurationNs t) { now_ = t; }

 private:
  DurationNs now_;
};

// Convenience helper for producing an unowned clock reference.
inline std::shared_ptr<Clock> make_steady_clock() { return std::make_shared<SteadyClock>(); }

}  // namespace engine_residency

#endif  // ENGINE_RESIDENCY_CLOCK_HPP
