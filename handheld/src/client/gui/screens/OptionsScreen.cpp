#include "OptionsScreen.h"

#include "../../../AppPlatform.h"
#include "../../Minecraft.h"
#include "DialogDefinitions.h"
#include "StartMenuScreen.h"

#include "../components/ImageButton.h"
#include "../components/OptionsGroup.h"
#include "../components/OptionsPane.h"

OptionsScreen::OptionsScreen()
    : btnClose(NULL), bHeader(NULL), selectedCategory(0) {}

OptionsScreen::~OptionsScreen() {
  if (btnClose != NULL) {
    delete btnClose;
    btnClose = NULL;
  }
  if (bHeader != NULL) {
    delete bHeader, bHeader = NULL;
  }
  for (std::vector<Touch::TButton *>::iterator it = categoryButtons.begin();
       it != categoryButtons.end(); ++it) {
    if (*it != NULL) {
      delete *it;
      *it = NULL;
    }
  }
  for (std::vector<OptionsPane *>::iterator it = optionPanes.begin();
       it != optionPanes.end(); ++it) {
    if (*it != NULL) {
      delete *it;
      *it = NULL;
    }
  }
  categoryButtons.clear();
}

void OptionsScreen::init() {
  bHeader = new Touch::THeader(0, "Options");
  btnClose = new ImageButton(1, "");
  ImageDef def;
  def.name = "gui/touchgui.png";
  def.width = 34;
  def.height = 26;

  def.setSrc(IntRectangle(150, 0, (int)def.width, (int)def.height));
  btnClose->setImageDef(def, true);

  categoryButtons.push_back(new Touch::TButton(2, "Login"));
  categoryButtons.push_back(new Touch::TButton(3, "Game"));
  categoryButtons.push_back(new Touch::TButton(4, "Controls"));
  categoryButtons.push_back(new Touch::TButton(5, "Graphics"));
  buttons.push_back(bHeader);
  buttons.push_back(btnClose);
  for (std::vector<Touch::TButton *>::iterator it = categoryButtons.begin();
       it != categoryButtons.end(); ++it) {
    buttons.push_back(*it);
    tabButtons.push_back(*it);
  }
  generateOptionScreens();
}
void OptionsScreen::setupPositions() {
  int buttonHeight = btnClose->height;
  btnClose->x = width - btnClose->width;
  btnClose->y = 0;
  int offsetNum = 1;
  for (std::vector<Touch::TButton *>::iterator it = categoryButtons.begin();
       it != categoryButtons.end(); ++it) {
    (*it)->x = 0;
    (*it)->y = offsetNum * buttonHeight;
    (*it)->selected = false;
    offsetNum++;
  }
  bHeader->x = 0;
  bHeader->y = 0;
  bHeader->width = width - btnClose->width;
  bHeader->height = btnClose->height;
  for (std::vector<OptionsPane *>::iterator it = optionPanes.begin();
       it != optionPanes.end(); ++it) {
    if (categoryButtons.size() > 0 && categoryButtons[0] != NULL) {
      (*it)->x = categoryButtons[0]->width;
      (*it)->y = bHeader->height;
      (*it)->width = width - categoryButtons[0]->width;
      (*it)->setupPositions();
    }
  }
  selectCategory(0);
}

void OptionsScreen::render(int xm, int ym, float a) {
  renderBackground();
  super::render(xm, ym, a);
  int xmm = xm * width / minecraft->width;
  int ymm = ym * height / minecraft->height - 1;
  if (currentOptionPane != NULL)
    currentOptionPane->render(minecraft, xmm, ymm);
}

void OptionsScreen::removed() {
  if (minecraft) {
    minecraft->options.save();
    minecraft->reloadOptions();
  }
}
void OptionsScreen::buttonClicked(Button *button) {
  if (button == btnClose) {
    minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
  } else if (button->id > 1 && button->id < 7) {
    // This is a category button
    int categoryButton = button->id - categoryButtons[0]->id;
    selectCategory(categoryButton);
  }
}

void OptionsScreen::selectCategory(int index) {
  int currentIndex = 0;
  for (std::vector<Touch::TButton *>::iterator it = categoryButtons.begin();
       it != categoryButtons.end(); ++it) {
    if (index == currentIndex) {
      (*it)->selected = true;
    } else {
      (*it)->selected = false;
    }
    currentIndex++;
  }
  if (index < (int)optionPanes.size())
    currentOptionPane = optionPanes[index];
}

void OptionsScreen::generateOptionScreens() {
  optionPanes.push_back(new OptionsPane());
  optionPanes.push_back(new OptionsPane());
  optionPanes.push_back(new OptionsPane());
  optionPanes.push_back(new OptionsPane());

  // Login / General Pane
  optionPanes[0]
      ->createOptionsGroup("options.group.mojang")
      .addOptionItem(&Options::Option::MUSIC, minecraft)
      .addOptionItem(&Options::Option::SOUND, minecraft)
      .addOptionItem(&Options::Option::SENSITIVITY, minecraft);

  // Game Pane
  optionPanes[1]
      ->createOptionsGroup("options.group.game")
      .addOptionItem(&Options::Option::THIRD_PERSON, minecraft)
      .addOptionItem(&Options::Option::HIDE_GUI, minecraft)
      .addOptionItem(&Options::Option::SERVER_VISIBLE, minecraft)
      .addOptionItem(&Options::Option::VIEW_BOBBING, minecraft)
      .addOptionItem(&Options::Option::DIFFICULTY, minecraft)
      .addOptionItem(&Options::Option::RENDER_DISTANCE, minecraft)
      .addOptionItem(&Options::Option::GUI_SCALE, minecraft);

  // Controls Pane
  optionPanes[2]
      ->createOptionsGroup("options.group.controls")
      .addOptionItem(&Options::Option::INVERT_MOUSE, minecraft)
      .addOptionItem(&Options::Option::LEFT_HANDED, minecraft)
      .addOptionItem(&Options::Option::USE_TOUCHSCREEN, minecraft)
      .addOptionItem(&Options::Option::USE_TOUCH_JOYPAD, minecraft)
      .addOptionItem(&Options::Option::DESTROY_VIBRATION, minecraft);

  // Graphics Pane
  optionPanes[3]
      ->createOptionsGroup("options.group.graphics")
      .addOptionItem(&Options::Option::GRAPHICS, minecraft)
      .addOptionItem(&Options::Option::AMBIENT_OCCLUSION, minecraft)
      .addOptionItem(&Options::Option::ANAGLYPH, minecraft)
      .addOptionItem(&Options::Option::LIMIT_FRAMERATE, minecraft)
      .addOptionItem(&Options::Option::PIXELS_PER_MILLIMETER, minecraft);
}

void OptionsScreen::mouseClicked(int x, int y, int buttonNum) {
  if (currentOptionPane != NULL)
    currentOptionPane->mouseClicked(minecraft, x, y, buttonNum);
  super::mouseClicked(x, y, buttonNum);
}

void OptionsScreen::mouseReleased(int x, int y, int buttonNum) {
  if (currentOptionPane != NULL)
    currentOptionPane->mouseReleased(minecraft, x, y, buttonNum);
  super::mouseReleased(x, y, buttonNum);
}

void OptionsScreen::tick() {
  if (currentOptionPane != NULL)
    currentOptionPane->tick(minecraft);
  super::tick();
}
