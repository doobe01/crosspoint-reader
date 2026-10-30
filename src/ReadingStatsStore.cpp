#include "ReadingStatsStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <algorithm>
#include <ctime>

namespace readingstats {
namespace {

constexpr uint8_t TOTALS_VERSION = 1;
constexpr uint8_t BOOK_VERSION = 1;
constexpr char STATS_DIR[] = "/.crosspoint/stats";
constexpr char BOOKS_DIR[] = "/.crosspoint/stats/books";
constexpr char TOTALS_PATH[] = "/.crosspoint/stats/totals.bin";
constexpr char TOTALS_TMP_PATH[] = "/.crosspoint/stats/totals.bin.tmp";

// The on-disk layout is the in-memory layout: keep the records padding-free.
static_assert(sizeof(Totals) == 4 * sizeof(uint32_t) + DAY_HISTORY * sizeof(uint32_t));
static_assert(sizeof(BookStats) == 3 * sizeof(uint32_t));

std::string bookFilePath(const std::string& documentId) { return std::string(BOOKS_DIR) + "/" + documentId + ".bin"; }

uint32_t saturatingAdd(const uint32_t a, const uint32_t b) { return a > UINT32_MAX - b ? UINT32_MAX : a + b; }

uint32_t& slot(Totals& totals, const uint32_t day) { return totals.daySeconds[day % DAY_HISTORY]; }

// Reads `version` then the whole record. Returns false on a short read or an
// unknown version; the caller resets `out`.
template <typename T>
bool readRecord(const char* path, const uint8_t expectedVersion, T& out) {
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file)) return false;
  uint8_t version = 0;
  const bool ok = serialization::tryReadPod(file, version) && version == expectedVersion &&
                  file.read(&out, sizeof(T)) == static_cast<int>(sizeof(T));
  file.close();
  if (!ok) LOG_ERR("STATS", "Unreadable %s (version %u)", path, version);
  return ok;
}

// Writes to `<path>.tmp` and moves it into place only after a successful close,
// so a failed sync can never replace a good record with a torn one.
template <typename T>
bool writeRecord(const std::string& path, const uint8_t version, const T& record) {
  Storage.mkdir(BOOKS_DIR);  // creates the parents too
  const std::string tmpPath = path + ".tmp";
  bool ok = false;
  {
    HalFile file;
    if (!Storage.openFileForWrite("STATS", tmpPath, file)) return false;
    ok = serialization::tryWritePod(file, version) && file.write(&record, sizeof(T)) == sizeof(T);
    file.flush();
    ok = file.close() && ok;
  }
  if (!ok) {
    LOG_ERR("STATS", "Failed to write %s", tmpPath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!Storage.replaceFile(tmpPath.c_str(), path.c_str())) {
    LOG_ERR("STATS", "Failed to move %s into place", path.c_str());
    return false;
  }
  return true;
}

}  // namespace

uint32_t localDayNumber(const int64_t epoch) {
  if (epoch <= 0) return 0;
  const time_t t = static_cast<time_t>(epoch);
  struct tm local{};
  if (!localtime_r(&t, &local)) return 0;
  // days_from_civil (H. Hinnant): proleptic Gregorian days since 1970-01-01.
  int64_t y = local.tm_year + 1900;
  const unsigned m = static_cast<unsigned>(local.tm_mon + 1);
  const unsigned d = static_cast<unsigned>(local.tm_mday);
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097 + static_cast<int64_t>(doe) - 719468;
  return days > 0 ? static_cast<uint32_t>(days) : 0;
}

void addToDay(Totals& totals, const uint32_t day, const uint32_t seconds) {
  if (day == 0 || seconds == 0) return;
  const bool farAhead = day >= totals.latestDay + DAY_HISTORY;
  const bool farBehind = day < totals.latestDay && totals.latestDay - day >= DAY_HISTORY;
  if (totals.latestDay == 0 || farAhead || farBehind) {
    std::fill(std::begin(totals.daySeconds), std::end(totals.daySeconds), 0u);
    totals.latestDay = day;
  } else if (day > totals.latestDay) {
    for (uint32_t d = totals.latestDay + 1; d <= day; d++) slot(totals, d) = 0;
    totals.latestDay = day;
  }
  uint32_t& bucket = slot(totals, day);
  bucket = saturatingAdd(bucket, seconds);
}

uint32_t secondsForDay(const Totals& totals, const uint32_t day) {
  if (day == 0 || totals.latestDay == 0 || day > totals.latestDay || totals.latestDay - day >= DAY_HISTORY) return 0;
  return totals.daySeconds[day % DAY_HISTORY];
}

uint32_t secondsForLastDays(const Totals& totals, const uint32_t today, const int days) {
  uint32_t sum = 0;
  for (int i = 0; i < days && today >= static_cast<uint32_t>(i); i++) {
    sum = saturatingAdd(sum, secondsForDay(totals, today - i));
  }
  return sum;
}

int streakDays(const Totals& totals, const uint32_t today) {
  if (today == 0) return 0;
  uint32_t day = secondsForDay(totals, today) > 0 ? today : today - 1;
  int streak = 0;
  while (day > 0 && streak < DAY_HISTORY && secondsForDay(totals, day) > 0) {
    streak++;
    day--;
  }
  return streak;
}

bool loadTotals(Totals& out) {
  if (readRecord(TOTALS_PATH, TOTALS_VERSION, out)) return true;
  out = Totals{};
  return false;
}

bool saveTotals(const Totals& totals) { return writeRecord(TOTALS_PATH, TOTALS_VERSION, totals); }

bool loadBook(const std::string& documentId, BookStats& out) {
  if (!documentId.empty() && readRecord(bookFilePath(documentId).c_str(), BOOK_VERSION, out)) return true;
  out = BookStats{};
  return false;
}

bool saveBook(const std::string& documentId, const BookStats& stats) {
  if (documentId.empty()) return false;
  return writeRecord(bookFilePath(documentId), BOOK_VERSION, stats);
}

bool recordSession(const std::string& documentId, const uint32_t seconds, const uint16_t turns,
                   const int64_t endEpoch) {
  if (seconds < MIN_SESSION_SECONDS) return true;

  // 240 bytes: too large for this frame's 256-byte local budget, so it lives
  // on the heap for the duration of the call.
  auto totals = makeUniqueNoThrow<Totals>();
  if (!totals) {
    LOG_ERR("STATS", "OOM: totals record (%u bytes)", static_cast<unsigned>(sizeof(Totals)));
    return false;
  }
  loadTotals(*totals);  // a missing or damaged file starts over from zero
  totals->seconds = saturatingAdd(totals->seconds, seconds);
  totals->turns = saturatingAdd(totals->turns, turns);
  totals->sessions = saturatingAdd(totals->sessions, 1);
  addToDay(*totals, localDayNumber(endEpoch), seconds);
  const bool ok = saveTotals(*totals);
  totals.reset();

  if (documentId.empty()) return ok;
  BookStats book;
  loadBook(documentId, book);
  book.seconds = saturatingAdd(book.seconds, seconds);
  book.turns = saturatingAdd(book.turns, turns);
  book.sessions = saturatingAdd(book.sessions, 1);
  return saveBook(documentId, book) && ok;
}

bool resetAll() {
  if (!Storage.exists(STATS_DIR)) return true;
  if (Storage.removeDir(STATS_DIR)) return true;
  LOG_ERR("STATS", "Failed to remove %s", STATS_DIR);
  return false;
}

}  // namespace readingstats
