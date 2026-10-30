#pragma once

#include <cstdint>
#include <string>

// On-device reading statistics, written once per reading session (reader exit,
// sleep, or sync) and read back by the statistics screen. Nothing stays
// resident: callers own the records and the store only loads and saves them.
//
// Files under /.crosspoint/stats/:
//   totals.bin        all-time totals plus a ring of per-day seconds
//   books/<id>.bin    one record per book, keyed by the KOReader document id
//                     (content hash), so a renamed or moved book keeps its data
//
// Day numbers are local civil days since 1970-01-01, derived from the trusted
// clock when the session is flushed, so a session that crosses midnight is
// credited to the day it ends. Without a trusted clock a session still counts
// toward the totals and the book, but not toward any day.
namespace readingstats {

// Days of per-day history kept in the ring. Long enough for the 7-day view and
// a streak readout that saturates at the ring size.
constexpr int DAY_HISTORY = 56;
// A session shorter than this is not recorded anywhere.
constexpr uint32_t MIN_SESSION_SECONDS = 10;

// Both records are written to disk as-is after a version byte, so they must
// stay free of padding (all 32-bit fields).
struct Totals {
  uint32_t seconds = 0;
  uint32_t turns = 0;
  uint32_t sessions = 0;
  // Newest day that holds data; 0 means the ring is empty. Slot d % DAY_HISTORY
  // is valid for days in (latestDay - DAY_HISTORY, latestDay].
  uint32_t latestDay = 0;
  uint32_t daySeconds[DAY_HISTORY] = {};
};

struct BookStats {
  uint32_t seconds = 0;
  uint32_t turns = 0;
  uint32_t sessions = 0;
};

// --- pure helpers (host-testable) -------------------------------------------

// Local civil day number for an epoch, or 0 when the epoch is not trusted.
uint32_t localDayNumber(int64_t epoch);
// Add seconds to one day. Moving forward clears the skipped slots; a day
// inside the ring but before latestDay is accepted; a day older than the ring
// restarts it, since a trusted clock never steps back that far.
void addToDay(Totals& totals, uint32_t day, uint32_t seconds);
uint32_t secondsForDay(const Totals& totals, uint32_t day);
// Sum over the `days` days ending at `today` inclusive.
uint32_t secondsForLastDays(const Totals& totals, uint32_t today, int days);
// Consecutive days with reading ending today, or ending yesterday when today
// has none yet. Saturates at DAY_HISTORY.
int streakDays(const Totals& totals, uint32_t today);

// --- storage -----------------------------------------------------------------

// Load returns false and leaves `out` default-initialised when the file is
// missing or unreadable, so callers can treat both as "no data yet".
bool loadTotals(Totals& out);
bool saveTotals(const Totals& totals);
bool loadBook(const std::string& documentId, BookStats& out);
bool saveBook(const std::string& documentId, const BookStats& stats);

// Fold one finished session into the totals and, when documentId is non-empty,
// into the book record. `endEpoch` is the trusted clock at flush time or 0.
// Returns false when any file could not be written.
bool recordSession(const std::string& documentId, uint32_t seconds, uint16_t turns, int64_t endEpoch);

// Delete every statistics file. Returns false if something could not be removed.
bool resetAll();

}  // namespace readingstats
