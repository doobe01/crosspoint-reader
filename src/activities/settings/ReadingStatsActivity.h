#pragma once
#include <string>

#include "ReadingStatsStore.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Read-only reading statistics under Reader settings: today, last 7 days,
// streak, all-time totals, and the most recent book, plus a reset action.
// Records are loaded once in onEnter; repaints only point at member buffers.
class ReadingStatsActivity final : public UiListActivity {
 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int ITEM_COUNT = 12;

  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;

  void loadRecords();
  void fillRows();

  // Formatted values are short ("123h 45m", "12345"); the book title is the
  // one row that can be long and keeps its own storage.
  static constexpr size_t VALUE_LENGTH = 24;
  char rowValues_[ITEM_COUNT][VALUE_LENGTH] = {};
  std::string bookTitle_;
  readingstats::Totals totals_;
  readingstats::BookStats book_;
  bool hasBook_ = false;
  uint32_t today_ = 0;
  OptionPopup confirmPopup_;
  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
};
