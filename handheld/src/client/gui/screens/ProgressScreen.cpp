#include "ProgressScreen.h"
#include "../../../SharedConstants.h"
#include "../../Minecraft.h"
#include "../Font.h"
#include "../Gui.h"
#include "DisconnectionScreen.h"

ProgressScreen::ProgressScreen() : ticks(0) {}

void ProgressScreen::render(int xm, int ym, float a) {
  if (minecraft->isLevelGenerated()) {
    minecraft->setScreen(NULL);
    return;
  }

  renderBackground();
  renderDirtBackground(0);

  int i = minecraft->progressStagePercentage;

  if (i >= 0) {
    int w = 100;
    int h = 2;
    int x = width / 2 - w / 2;
    int y = height / 2 + 16;

    // printf("%d, %d - %d, %d\n", x, y, x + w, y + h);

    fill((float)x, (float)y, (float)(x + w), (float)(y + h), 0xff808080);
    fill((float)x, (float)y, (float)(x + i), (float)(y + h), 0xff80ff80);
  }

  glEnable2(GL_BLEND);

  const char *title = "Generating world";
  minecraft->font->drawShadow(title,
      (float)((width - minecraft->font->width(title)) / 2),
      (float)(height / 2 - 4 - 16), 0xffffff);

  const char *status = minecraft->getProgressMessage();
  const int progressWidth = minecraft->font->width(status);
  const int progressLeft = (width - progressWidth) / 2;
  const int progressY = height / 2 - 4 + 8;
  minecraft->font->drawShadow(
      status, (float)progressLeft, (float)progressY, 0xffffff);

#if APPLE_DEMO_PROMOTION
  drawCenteredString(minecraft->font, "This demonstration version", width / 2,
      progressY + 36, 0xffffff);
  drawCenteredString(minecraft->font, "does not allow saving games", width / 2,
      progressY + 46, 0xffffff);
#endif

  // If we're locating the server, show our famous spinner!
  bool isLocating = (minecraft->getProgressStatusId() == 0);
  if (isLocating) {
    const int spinnerX = progressLeft + progressWidth + 6;
    static const char *spinnerTexts[] = {"-", "\\", "|", "/"};
    int n = ((int)(5.5f * getTimeS()) % 4);
    drawCenteredString(
        minecraft->font, spinnerTexts[n], spinnerX, progressY, 0xffffffff);
  }

  glDisable2(GL_BLEND);
  sleepMs(50);
}

bool ProgressScreen::isInGameScreen() { return false; }

void ProgressScreen::tick() {
  // After 10 seconds of not connecting -> write an error message and go back
  if (++ticks == 10 * SharedConstants::TicksPerSecond &&
      minecraft->getProgressStatusId() == 0 && !minecraft->isOnline()) {
    minecraft->setScreen(
        new DisconnectionScreen("Could not connect to server. Try again."));
  }
}
