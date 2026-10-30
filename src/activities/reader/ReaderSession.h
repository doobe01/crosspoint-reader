#pragma once

#include <cstdint>

// Fixed-scalar active-reading summary. The owner reports turn outcomes before
// render and completed pages after render; no allocation or I/O occurs here.
class ReaderSession {
 public:
  void noteTurn(bool forward, bool succeeded);
  void onRenderComplete(uint32_t monotonicMs, int64_t trustedEpoch, int progressBp);
  void reset();

  bool isEmitWorthy() const { return startEpoch > 0 && duration >= 10 && endTime() > startEpoch; }
  int64_t startTime() const { return startEpoch; }
  int64_t endTime() const { return startEpoch > 0 ? startEpoch + duration : 0; }
  uint32_t durationSeconds() const { return duration; }
  uint16_t startProgressBp() const { return startProgress; }
  uint16_t endProgressBp() const { return endProgress; }
  // Counted dwell time and qualified forward turns, kept even when the wall
  // clock is untrusted. On-device statistics use these; the plugin event
  // keeps the trusted-time fields above.
  uint32_t activeSeconds() const { return activeTotal; }
  uint16_t countedTurns() const { return turns; }

 private:
  static uint16_t clampProgress(int progressBp);

  int64_t anchorEpoch = 0;
  int64_t startEpoch = 0;
  uint32_t anchorMs = 0;
  uint32_t duration = 0;
  uint32_t activeTotal = 0;
  uint16_t turns = 0;
  uint16_t anchorProgress = 0;
  uint16_t startProgress = 0;
  uint16_t endProgress = 0;
  bool hasAnchor = false;
  bool pendingForward = false;
};
