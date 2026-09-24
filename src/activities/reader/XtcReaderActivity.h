#pragma once

#include <HalMemory.h>
#include <Xtc.h>

#include <memory>
#include <string>

#include "ReaderActivity.h"

class XtcReaderActivity final : public ReaderActivity {
  std::shared_ptr<Xtc> xtc;
  uint32_t currentPage = 0;
  // The page just shown was mostly ink: flash white before the next one.
  bool prevPageDark = false;
  static constexpr uint32_t DARK_PAGE_PERCENT = 45;
  // 16-level (4bpp) frame for panels that take one (IT8951): a panel-sized
  // XTH page is expanded straight into it and sent as a single grey frame.
  // PSRAM, allocated on the first such page, kept for the session.
  HalMemory::PsramBuffer gray4Frame;
  size_t gray4FrameSize = 0;
  uint8_t* ensureGray4Frame(size_t bytes);

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  void renderPage();
  void openMenu();
  void openChapterSelection();
  void renderStatusBarOverlay(GfxRenderer& renderer, StatusBarOverlayPosition position) const;
  StatusBarInfo getStatusBarInfo() const;
  void saveProgress() const;
  void loadProgress();

  bool loadBook() override;
  std::string getBookTitle() const override { return xtc ? xtc->getTitle() : ""; }
  std::string getBookAuthor() const override { return xtc ? xtc->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return xtc ? xtc->getThumbBmpPath() : ""; }
  bool handleFormatInput() override;
  void renderBook() override;
  void applyInitialOrientation() override;

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("XtcReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~XtcReaderActivity() override = default;

  void onExit() override;

  bool pageTurn(bool isForward) override;
  bool isRightToLeft() const override { return xtc && xtc->isRightToLeft(); }
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
