#pragma once

#include <stdint.h>

namespace Show {
// A new invitation requires a debounced release, then a new debounced press.
struct MayorContact {
  uint32_t show = 0, changedAt = 0;
  bool down = false, released = false, sent = false;

  constexpr bool poll(uint32_t invitation, bool pressed, uint32_t now, uint32_t debounce) {
    if (!invitation || invitation != show) {
      show = invitation; changedAt = now; down = pressed; released = false; sent = false;
      return false;
    }
    if (pressed != down) { down = pressed; changedAt = now; }
    if (now - changedAt < debounce || sent) return false;
    if (!down) { released = true; return false; }
    if (!released) return false;
    sent = true;
    return true;
  }
};

enum OpeningSource : uint8_t { NoSource, MayorButton, WebFallback, LocalTact, SerialStart };
enum OpeningOutput : uint8_t { NoOutput, SentFrame, SimulatedFrame, OutputFault };
enum FallbackState : uint8_t { NoConfirmation, Counting, CancelledByFrame, FallbackRequested,
                               FallbackBlocked, Discarded, Available, Expired };

struct OpeningGate {
  static constexpr uint32_t DELAY_MS = 2000;
  static constexpr uint32_t WINDOW_MS = 30000;
  static constexpr uint32_t HOLD_MS = 1000, GAP_MS = 600, HOLD_TTL_MS = 4000;
  static constexpr uint32_t MIN_HOLD_UPDATES = 3;

  uint32_t show = 0, generation = 1, confirmedAt = 0, acceptedAt = 0, outputAt = 0, observedAt = 0;
  bool armed = false, confirmed = false, accepted = false;
  OpeningSource source = NoSource;
  OpeningOutput output = NoOutput;
  FallbackState fallback = NoConfirmation;
  uint8_t observed = 0;
  uint32_t holdPage = 0, holdId = 0, holdStep = 0, holdStartedAt = 0, holdUpdatedAt = 0;
  bool holding = false;

  constexpr void prepare(uint32_t id) {
    *this = OpeningGate{};
    show = id;
    armed = id != 0;
  }

  constexpr void disarm() {
    armed = false;
    holding = false;
    if (fallback == Counting || fallback == Available) fallback = Discarded;
  }

  constexpr bool matches(uint32_t id) const { return armed && show == id; }
  constexpr bool matchesConfirmation(uint32_t id, uint32_t revision) const {
    return matches(id) && generation == revision;
  }

  constexpr bool confirm(uint32_t id, uint32_t revision, uint32_t receivedAt) {
    if (!matchesConfirmation(id, revision) || confirmed) return false;
    confirmed = true;
    confirmedAt = receivedAt;
    fallback = output == SentFrame ? CancelledByFrame : Counting;
    return true;
  }

  constexpr bool cancelConfirmation(uint32_t id, uint32_t revision) {
    if (!matchesConfirmation(id, revision) || !confirmed || source != NoSource) return false;
    confirmed = false;
    confirmedAt = 0;
    fallback = NoConfirmation;
    ++generation;
    holding = false;
    holdPage = holdId = 0;
    return true;
  }

  constexpr bool reserve(uint32_t id, OpeningSource via) {
    if (!matches(id) || source != NoSource || via == NoSource) return false;
    source = via;
    holding = false;
    if (via == WebFallback) fallback = FallbackRequested;
    else if (fallback == Available) fallback = FallbackBlocked;
    return true;
  }

  constexpr void recordAcceptance(uint32_t at) { accepted = true; acceptedAt = at; }

  constexpr void recordOutput(OpeningOutput result, uint32_t at) {
    output = result;
    outputAt = at;
    if (result == SentFrame && source != WebFallback &&
        (fallback == Counting || fallback == FallbackBlocked)) fallback = CancelledByFrame;
  }

  constexpr bool canHold(uint32_t id, uint32_t now) const {
    return matches(id) && confirmed && source == NoSource && fallback == Available &&
        now - confirmedAt < DELAY_MS + WINDOW_MS;
  }

  constexpr void tick(uint32_t now, uint32_t page) {
    if (fallback == Counting && now - confirmedAt >= DELAY_MS) {
      fallback = source == NoSource ? Available : FallbackBlocked;
    }
    if (fallback == Available && now - confirmedAt >= DELAY_MS + WINDOW_MS) fallback = Expired;
    if (holding && (!canHold(show, now) || holdPage != page ||
        now - holdUpdatedAt >= GAP_MS || now - holdStartedAt >= HOLD_TTL_MS)) holding = false;
  }

  constexpr bool beginHold(uint32_t id, uint32_t page, uint32_t attempt, uint32_t now) {
    tick(now, page);
    if (!canHold(id, now) || !page || !attempt || (page == holdPage && attempt <= holdId)) return false;
    holdPage = page;
    holdId = attempt;
    holdStartedAt = holdUpdatedAt = now;
    holdStep = 0;
    holding = true;
    return true;
  }

  constexpr void cancelHold(uint32_t id, uint32_t page, uint32_t attempt) {
    if (!matches(id) || !page || !attempt || (page == holdPage && attempt < holdId)) return;
    holdPage = page;
    holdId = attempt;
    holding = false;
  }

  constexpr bool trackHold(uint32_t id, uint32_t page, uint32_t attempt, uint32_t step, uint32_t now) {
    tick(now, page);
    if (!holding || show != id || holdId != attempt || step != holdStep + 1) return false;
    holdStep = step;
    holdUpdatedAt = now;
    return true;
  }

  constexpr bool finishHold(uint32_t id, uint32_t page, uint32_t attempt, uint32_t step, uint32_t now) {
    tick(now, page);
    if (!holding || show != id || holdId != attempt) return false;
    holding = false;
    return step == holdStep + 1 && holdStep >= MIN_HOLD_UPDATES && now - holdStartedAt >= HOLD_MS;
  }

  constexpr uint32_t remaining(uint32_t now) const {
    return fallback == Counting && now - confirmedAt < DELAY_MS ? DELAY_MS - (now - confirmedAt) : 0;
  }
};
}
