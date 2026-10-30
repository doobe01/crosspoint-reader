#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>
#include <TrustedTime.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem {
  ITEM_TODAY = 0,
  ITEM_LAST_7_DAYS,
  ITEM_STREAK,
  ITEM_TOTAL_TIME,
  ITEM_PAGES,
  ITEM_SESSIONS,
  ITEM_AVG_PER_PAGE,
  ITEM_LAST_BOOK,
  ITEM_BOOK_TIME,
  ITEM_BOOK_PAGES,
  ITEM_BOOK_SESSIONS,
  ITEM_RESET,
};

const StrId menuNames[ReadingStatsActivity::ITEM_COUNT] = {
    StrId::STR_STATS_TODAY,        StrId::STR_STATS_LAST_7_DAYS,   StrId::STR_STATS_STREAK,
    StrId::STR_STATS_TOTAL_TIME,   StrId::STR_STATS_PAGES_TURNED,  StrId::STR_STATS_SESSIONS,
    StrId::STR_STATS_AVG_PER_PAGE, StrId::STR_STATS_LAST_BOOK,     StrId::STR_STATS_BOOK_TIME,
    StrId::STR_STATS_BOOK_PAGES,   StrId::STR_STATS_BOOK_SESSIONS, StrId::STR_STATS_RESET,
};

void copyText(char* buf, const size_t size, const char* text) { snprintf(buf, size, "%s", text); }

void formatDuration(char* buf, const size_t size, const uint32_t seconds) {
  if (seconds >= 3600) {
    snprintf(buf, size, tr(STR_STATS_HOURS_MINUTES_FORMAT), static_cast<unsigned>(seconds / 3600),
             static_cast<unsigned>((seconds % 3600) / 60));
  } else if (seconds >= 60) {
    snprintf(buf, size, tr(STR_STATS_MINUTES_FORMAT), static_cast<unsigned>(seconds / 60));
  } else {
    snprintf(buf, size, tr(STR_STATS_SECONDS_FORMAT), static_cast<unsigned>(seconds));
  }
}

void formatCount(char* buf, const size_t size, const uint32_t count) {
  snprintf(buf, size, "%lu", static_cast<unsigned long>(count));
}

void formatDays(char* buf, const size_t size, const int days) {
  if (days == 1) {
    copyText(buf, size, tr(STR_STATS_ONE_DAY));
    return;
  }
  const StrId format =
      days >= readingstats::DAY_HISTORY ? StrId::STR_STATS_DAYS_MAX_FORMAT : StrId::STR_STATS_DAYS_FORMAT;
  snprintf(buf, size, I18N.get(format), days);
}

}  // namespace

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ReadingStats", renderer, mappedInput) {}

const char* ReadingStatsActivity::headerTitle() const { return tr(STR_READING_STATS); }

void ReadingStatsActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
  loadRecords();
  fillRows();
}

void ReadingStatsActivity::loadRecords() {
  readingstats::loadTotals(totals_);
  today_ = readingstats::localDayNumber(trustedtime::trustedNow());

  book_ = readingstats::BookStats{};
  bookTitle_.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  hasBook_ = !books.empty();
  if (!hasBook_) return;
  const auto& recent = books.front();
  if (recent.title.empty()) {
    const size_t slash = recent.path.rfind('/');
    bookTitle_ = slash == std::string::npos ? recent.path : recent.path.substr(slash + 1);
  } else {
    bookTitle_ = recent.title;
  }
  // The record is keyed by content hash, so a renamed book still finds its data.
  readingstats::loadBook(KOReaderDocumentId::calculate(recent.path), book_);
}

void ReadingStatsActivity::fillRows() {
  const bool clockKnown = today_ != 0;
  if (clockKnown) {
    formatDuration(rowValues_[ITEM_TODAY], VALUE_LENGTH, readingstats::secondsForDay(totals_, today_));
    formatDuration(rowValues_[ITEM_LAST_7_DAYS], VALUE_LENGTH, readingstats::secondsForLastDays(totals_, today_, 7));
    formatDays(rowValues_[ITEM_STREAK], VALUE_LENGTH, readingstats::streakDays(totals_, today_));
  } else {
    copyText(rowValues_[ITEM_TODAY], VALUE_LENGTH, tr(STR_STATS_NO_CLOCK));
    copyText(rowValues_[ITEM_LAST_7_DAYS], VALUE_LENGTH, tr(STR_STATS_NO_CLOCK));
    copyText(rowValues_[ITEM_STREAK], VALUE_LENGTH, tr(STR_STATS_NO_CLOCK));
  }
  formatDuration(rowValues_[ITEM_TOTAL_TIME], VALUE_LENGTH, totals_.seconds);
  formatCount(rowValues_[ITEM_PAGES], VALUE_LENGTH, totals_.turns);
  formatCount(rowValues_[ITEM_SESSIONS], VALUE_LENGTH, totals_.sessions);
  formatDuration(rowValues_[ITEM_AVG_PER_PAGE], VALUE_LENGTH, totals_.turns > 0 ? totals_.seconds / totals_.turns : 0);
  formatDuration(rowValues_[ITEM_BOOK_TIME], VALUE_LENGTH, book_.seconds);
  formatCount(rowValues_[ITEM_BOOK_PAGES], VALUE_LENGTH, book_.turns);
  formatCount(rowValues_[ITEM_BOOK_SESSIONS], VALUE_LENGTH, book_.sessions);
  rowValues_[ITEM_RESET][0] = '\0';
}

void ReadingStatsActivity::activateIndex(const int index) {
  nav.selected = index;
  if (index != ITEM_RESET) return;
  app.clearTapFlash();
  const char* const options[] = {tr(STR_CANCEL), tr(STR_STATS_RESET)};
  confirmPopup_.showMessage(tr(STR_STATS_RESET), tr(STR_STATS_RESET_CONFIRM), options, 2, 0, [this](const int idx) {
    if (idx == 1) {
      if (!readingstats::resetAll()) LOG_ERR("STATS", "Reset left some statistics files behind");
      // buildScreen points at these buffers from the render task.
      RenderLock lock(*this);
      loadRecords();
      fillRows();
    }
    requestUpdate();
  });
  requestUpdate();
}

bool ReadingStatsActivity::handleCustomInput() {
  return confirmPopup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

void ReadingStatsActivity::render(RenderLock&& lock) {
  if (confirmPopup_.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void ReadingStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].value = rowValues_[i];
  }
  rowItems_[ITEM_LAST_BOOK].value = hasBook_ ? bookTitle_.c_str() : tr(STR_NONE_OPT);
  rowItems_[ITEM_RESET].value = nullptr;

  fui::ListProps props;
  props.items = rowItems_;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}
