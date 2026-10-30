#include <HalStorage.h>
#include <ReadingStatsStore.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>

namespace rs = readingstats;

namespace {

constexpr char TOTALS[] = "/.crosspoint/stats/totals.bin";
constexpr char BOOK_A[] = "/.crosspoint/stats/books/aaaa.bin";

class ReadingStatsStore : public ::testing::Test {
 protected:
  void SetUp() override {
    setenv("TZ", "UTC0", 1);
    tzset();
    fake::reset();
  }
};

rs::Totals ringWith(const uint32_t latestDay, const std::initializer_list<uint32_t> secondsNewestFirst) {
  rs::Totals totals;
  uint32_t day = latestDay - secondsNewestFirst.size() + 1;
  for (auto it = std::rbegin(secondsNewestFirst); it != std::rend(secondsNewestFirst); ++it) {
    rs::addToDay(totals, day++, *it);
  }
  return totals;
}

}  // namespace

TEST_F(ReadingStatsStore, LocalDayNumberFollowsCivilDate) {
  EXPECT_EQ(rs::localDayNumber(0), 0u);
  EXPECT_EQ(rs::localDayNumber(-5), 0u);
  EXPECT_EQ(rs::localDayNumber(86399), 0u);           // 1970-01-01 maps to day 0, reported as "unknown"
  EXPECT_EQ(rs::localDayNumber(86400), 1u);           // 1970-01-02
  EXPECT_EQ(rs::localDayNumber(1735689600), 20089u);  // 2025-01-01
  EXPECT_EQ(rs::localDayNumber(1735689600 + 86399), 20089u);
  EXPECT_EQ(rs::localDayNumber(1735776000), 20090u);  // 2025-01-02
}

TEST_F(ReadingStatsStore, LocalDayNumberHonoursTimezone) {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  // 2025-01-01 23:30 UTC is already 2025-01-02 in CET.
  EXPECT_EQ(rs::localDayNumber(1735689600 + 23 * 3600 + 1800), 20090u);
}

TEST_F(ReadingStatsStore, AddToDayRollsForwardAndClearsSkippedDays) {
  rs::Totals totals;
  rs::addToDay(totals, 100, 60);
  rs::addToDay(totals, 100, 30);
  EXPECT_EQ(totals.latestDay, 100u);
  EXPECT_EQ(rs::secondsForDay(totals, 100), 90u);

  // Pre-fill the slot day 103 will reuse, then skip to it: the stale value must go.
  totals.daySeconds[103 % rs::DAY_HISTORY] = 999;
  rs::addToDay(totals, 103, 10);
  EXPECT_EQ(totals.latestDay, 103u);
  EXPECT_EQ(rs::secondsForDay(totals, 103), 10u);
  EXPECT_EQ(rs::secondsForDay(totals, 102), 0u);
  EXPECT_EQ(rs::secondsForDay(totals, 101), 0u);
  EXPECT_EQ(rs::secondsForDay(totals, 100), 90u);
}

TEST_F(ReadingStatsStore, AddToDayAcceptsRecentPastAndRestartsOnAncientDays) {
  rs::Totals totals;
  rs::addToDay(totals, 200, 10);
  rs::addToDay(totals, 199, 20);  // clock stepped back a day: still inside the ring
  rs::addToDay(totals, 0, 40);    // unknown day
  EXPECT_EQ(totals.latestDay, 200u);
  EXPECT_EQ(rs::secondsForDay(totals, 199), 20u);
  EXPECT_EQ(rs::secondsForDay(totals, 201), 0u);

  // A day older than the ring means latestDay is garbage (a trusted clock never
  // steps back that far): the ring restarts from the new day.
  rs::addToDay(totals, 200 - rs::DAY_HISTORY, 30);
  EXPECT_EQ(totals.latestDay, 200u - rs::DAY_HISTORY);
  EXPECT_EQ(rs::secondsForDay(totals, 200 - rs::DAY_HISTORY), 30u);
  EXPECT_EQ(rs::secondsForDay(totals, 200), 0u);
}

TEST_F(ReadingStatsStore, CorruptLatestDayRecoversOnNextSession) {
  rs::Totals totals;
  totals.latestDay = 4000000000u;
  rs::addToDay(totals, 20090, 60);
  EXPECT_EQ(totals.latestDay, 20090u);
  EXPECT_EQ(rs::secondsForDay(totals, 20090), 60u);
}

TEST_F(ReadingStatsStore, JumpPastTheRingStartsOver) {
  rs::Totals totals;
  rs::addToDay(totals, 100, 10);
  rs::addToDay(totals, 100 + rs::DAY_HISTORY, 5);
  EXPECT_EQ(totals.latestDay, 100u + rs::DAY_HISTORY);
  EXPECT_EQ(rs::secondsForDay(totals, 100), 0u);
  for (int i = 1; i < rs::DAY_HISTORY; i++) EXPECT_EQ(rs::secondsForDay(totals, 100 + i), 0u);
}

TEST_F(ReadingStatsStore, LastDaysSumsOnlyTheWindow) {
  const rs::Totals totals = ringWith(300, {10, 20, 30, 40, 50, 60, 70, 80});
  EXPECT_EQ(rs::secondsForLastDays(totals, 300, 7), 10u + 20 + 30 + 40 + 50 + 60 + 70);
  EXPECT_EQ(rs::secondsForLastDays(totals, 300, 1), 10u);
  EXPECT_EQ(rs::secondsForLastDays(totals, 302, 7), 10u + 20 + 30 + 40 + 50);  // two idle days since
  EXPECT_EQ(rs::secondsForLastDays(totals, 0, 7), 0u);
}

TEST_F(ReadingStatsStore, StreakCountsBackFromTodayOrYesterday) {
  EXPECT_EQ(rs::streakDays(ringWith(300, {10, 20, 30}), 300), 3);
  EXPECT_EQ(rs::streakDays(ringWith(300, {10, 20, 30}), 301), 3);  // nothing yet today: still alive
  EXPECT_EQ(rs::streakDays(ringWith(300, {10, 20, 30}), 302), 0);  // a full day missed
  EXPECT_EQ(rs::streakDays(ringWith(300, {10, 0, 30, 40}), 300), 1);
  EXPECT_EQ(rs::streakDays(ringWith(300, {10}), 0), 0);
}

TEST_F(ReadingStatsStore, StreakSaturatesAtRingSize) {
  rs::Totals totals;
  for (int i = 0; i < rs::DAY_HISTORY + 10; i++) rs::addToDay(totals, 1000 + i, 1);
  EXPECT_EQ(rs::streakDays(totals, 1000 + rs::DAY_HISTORY + 9), rs::DAY_HISTORY);
}

TEST_F(ReadingStatsStore, TotalsRoundTrip) {
  rs::Totals totals;
  totals.seconds = 1234;
  totals.turns = 56;
  totals.sessions = 7;
  rs::addToDay(totals, 20089, 600);
  rs::addToDay(totals, 20090, 300);
  ASSERT_TRUE(rs::saveTotals(totals));
  EXPECT_TRUE(fake::files.count(TOTALS));
  EXPECT_FALSE(fake::files.count(std::string(TOTALS) + ".tmp"));

  rs::Totals loaded;
  ASSERT_TRUE(rs::loadTotals(loaded));
  EXPECT_EQ(loaded.seconds, 1234u);
  EXPECT_EQ(loaded.turns, 56u);
  EXPECT_EQ(loaded.sessions, 7u);
  EXPECT_EQ(loaded.latestDay, 20090u);
  EXPECT_EQ(rs::secondsForDay(loaded, 20089), 600u);
  EXPECT_EQ(rs::secondsForDay(loaded, 20090), 300u);
}

TEST_F(ReadingStatsStore, MissingOrDamagedTotalsReadAsEmpty) {
  rs::Totals loaded;
  loaded.seconds = 99;
  EXPECT_FALSE(rs::loadTotals(loaded));
  EXPECT_EQ(loaded.seconds, 0u);

  rs::Totals totals;
  totals.seconds = 5;
  ASSERT_TRUE(rs::saveTotals(totals));
  fake::files.at(TOTALS)->bytes.pop_back();
  loaded.seconds = 99;
  EXPECT_FALSE(rs::loadTotals(loaded));
  EXPECT_EQ(loaded.seconds, 0u);

  fake::files.at(TOTALS)->bytes[0] = 77;  // unknown version
  EXPECT_FALSE(rs::loadTotals(loaded));
}

TEST_F(ReadingStatsStore, BookRoundTrip) {
  rs::BookStats book;
  book.seconds = 100;
  book.turns = 9;
  book.sessions = 2;
  ASSERT_TRUE(rs::saveBook("aaaa", book));
  EXPECT_TRUE(fake::files.count(BOOK_A));
  EXPECT_FALSE(fake::files.count(std::string(BOOK_A) + ".tmp"));
  EXPECT_EQ(fake::files.at(BOOK_A)->bytes.size(), 1u + 3 * sizeof(uint32_t));

  rs::BookStats loaded;
  ASSERT_TRUE(rs::loadBook("aaaa", loaded));
  EXPECT_EQ(loaded.seconds, 100u);
  EXPECT_EQ(loaded.turns, 9u);
  EXPECT_EQ(loaded.sessions, 2u);

  EXPECT_FALSE(rs::loadBook("", loaded));
  EXPECT_FALSE(rs::saveBook("", book));
  EXPECT_FALSE(rs::loadBook("missing", loaded));
  EXPECT_EQ(loaded.seconds, 0u);
}

TEST_F(ReadingStatsStore, FailedCloseKeepsTheOldRecord) {
  rs::Totals totals;
  totals.seconds = 5;
  ASSERT_TRUE(rs::saveTotals(totals));

  totals.seconds = 6;
  fake::failClosePath = std::string(TOTALS) + ".tmp";
  EXPECT_FALSE(rs::saveTotals(totals));
  EXPECT_FALSE(fake::files.count(std::string(TOTALS) + ".tmp"));

  rs::Totals loaded;
  ASSERT_TRUE(rs::loadTotals(loaded));
  EXPECT_EQ(loaded.seconds, 5u);
}

TEST_F(ReadingStatsStore, RecordSessionFoldsIntoTotalsAndBook) {
  const int64_t day1 = 1735689600 + 3600;  // 2025-01-01 01:00 UTC
  const int64_t day2 = day1 + 86400;
  EXPECT_TRUE(rs::recordSession("aaaa", 120, 5, day1));
  EXPECT_TRUE(rs::recordSession("aaaa", 60, 2, day2));
  EXPECT_TRUE(rs::recordSession("aaaa", 5, 1, day2));  // too short: ignored
  EXPECT_TRUE(rs::recordSession("", 30, 1, 0));        // no id, no clock

  rs::Totals totals;
  ASSERT_TRUE(rs::loadTotals(totals));
  EXPECT_EQ(totals.seconds, 210u);
  EXPECT_EQ(totals.turns, 8u);
  EXPECT_EQ(totals.sessions, 3u);
  EXPECT_EQ(rs::secondsForDay(totals, 20089), 120u);
  EXPECT_EQ(rs::secondsForDay(totals, 20090), 60u);
  EXPECT_EQ(rs::secondsForLastDays(totals, 20090, 7), 180u);  // the clockless 30 s is not in any day

  rs::BookStats book;
  ASSERT_TRUE(rs::loadBook("aaaa", book));
  EXPECT_EQ(book.seconds, 180u);
  EXPECT_EQ(book.turns, 7u);
  EXPECT_EQ(book.sessions, 2u);
  EXPECT_EQ(fake::files.count("/.crosspoint/stats/books/.bin"), 0u);
}

TEST_F(ReadingStatsStore, RecordSessionSurvivesDamagedTotals) {
  fake::add(TOTALS, "garbage");
  EXPECT_TRUE(rs::recordSession("aaaa", 100, 3, 0));
  rs::Totals totals;
  ASSERT_TRUE(rs::loadTotals(totals));
  EXPECT_EQ(totals.seconds, 100u);
  EXPECT_EQ(totals.sessions, 1u);
}

TEST_F(ReadingStatsStore, ResetRemovesEveryFile) {
  EXPECT_TRUE(rs::recordSession("aaaa", 100, 3, 0));
  EXPECT_TRUE(rs::recordSession("bbbb", 100, 3, 0));
  ASSERT_TRUE(fake::files.count(TOTALS));
  ASSERT_TRUE(fake::files.count("/.crosspoint/stats/books/bbbb.bin"));

  EXPECT_TRUE(rs::resetAll());
  EXPECT_EQ(fake::files.count(TOTALS), 0u);
  EXPECT_EQ(fake::files.count(BOOK_A), 0u);
  EXPECT_EQ(fake::files.count("/.crosspoint/stats/books/bbbb.bin"), 0u);
  rs::Totals totals;
  EXPECT_FALSE(rs::loadTotals(totals));

  EXPECT_EQ(fake::files.count("/.crosspoint/stats"), 0u);
  EXPECT_TRUE(rs::resetAll());  // nothing left is not a failure

  // The next session recreates the directories.
  EXPECT_TRUE(rs::recordSession("aaaa", 100, 3, 0));
  EXPECT_TRUE(fake::files.count(TOTALS));
}
