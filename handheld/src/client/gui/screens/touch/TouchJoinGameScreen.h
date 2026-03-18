#ifndef NET_MINECRAFT_CLIENT_GUI_SCREENS_TOUCH__TouchJoinGameScreen_H__
#define NET_MINECRAFT_CLIENT_GUI_SCREENS_TOUCH__TouchJoinGameScreen_H__

#include "../../../../network/RakNetInstance.h"
#include "../../../../platform/input/Multitouch.h"
#include "../../../Minecraft.h"
#include "../../Screen.h"
#include "../../components/Button.h"
#include "../../components/RolledSelectionListV.h"
#include "../../components/SmallButton.h"

namespace Touch {

class JoinGameScreen;

class AvailableGamesList : public RolledSelectionListV {
  int startSelected;
  int selectedItem;
  ServerList copiedServerList;

  friend class JoinGameScreen;

public:
  AvailableGamesList(Minecraft *_minecraft, int _width, int _height)
      : RolledSelectionListV(
            _minecraft, _width, _height, 0, _width, 24, _height, 34),
        selectedItem(-1), startSelected(-1) {}

  void setBounds(int top, int bottom) {
    y0 = (float)top;
    y1 = (float)bottom;
    capYPosition();
  }

protected:
  virtual int getNumberOfItems() { return (int)copiedServerList.size(); }

  virtual void selectCancel();
  virtual void selectStart(int item);
  virtual void selectItem(int item, bool doubleClick);

  virtual bool isSelectedItem(int item) { return item == selectedItem; }

  virtual void renderBackground() {}
  virtual void renderItem(int i, int x, int y, int h, Tesselator &t);
};

class JoinGameScreen : public Screen {
public:
  JoinGameScreen();
  virtual ~JoinGameScreen();

  void init();
  void setupPositions();

  virtual bool handleBackEvent(bool isDown);

  virtual bool isIndexValid(int index);

  virtual void tick();
  void render(int xm, int ym, float a);

  void buttonClicked(Button *button);

  bool isInGameScreen();

protected:
  virtual void keyPressed(int eventKey);
  virtual void keyboardNewChar(char inputChar);
  virtual void mouseClicked(int x, int y, int buttonNum);
  virtual void removed();

private:
  void connectDirect();
  void applyUsername();

  TButton bJoin;
  TButton bBack;
  THeader bHeader;
  AvailableGamesList *gamesList;

  std::string usernameText;
  bool usernameFocused;
  int usernameX;
  int usernameY;
  int usernameW;
  int usernameH;

  std::string directConnectText;
  bool directConnectFocused;
  int directConnectX;
  int directConnectY;
  int directConnectW;
  int directConnectH;
};

}; // namespace Touch

#endif /*NET_MINECRAFT_CLIENT_GUI_SCREENS_TOUCH__TouchJoinGameScreen_H__*/
