#ifndef NET_MINECRAFT_CLIENT_GUI_SCREENS__JoinGameScreen_H__
#define NET_MINECRAFT_CLIENT_GUI_SCREENS__JoinGameScreen_H__

#include "../../../network/RakNetInstance.h"
#include "../../Minecraft.h"
#include "../Screen.h"
#include "../components/Button.h"
#include "../components/ScrolledSelectionList.h"
#include "../components/SmallButton.h"
#include <string>

class JoinGameScreen;

class AvailableGamesList : public ScrolledSelectionList {
  int selectedItem;
  ServerList copiedServerList;

  friend class JoinGameScreen;

public:
  AvailableGamesList(Minecraft *_minecraft, int _width, int _height)
      : ScrolledSelectionList(_minecraft, _width, _height, 24, _height - 30,
                              28) {}

  void setBounds(int top, int bottom) {
    y0 = (float)top;
    y1 = (float)bottom;
    capYPosition();
  }

protected:
  virtual int getNumberOfItems() { return (int)copiedServerList.size(); }

  virtual void selectItem(int item, bool doubleClick) { selectedItem = item; }
  virtual bool isSelectedItem(int item) { return item == selectedItem; }

  virtual void renderBackground() {}
  virtual void renderItem(int i, int x, int y, int h, Tesselator &t) {
    const PingedCompatibleServer &s = copiedServerList[i];
    unsigned int color = s.isSpecial ? 0xff00b0 : 0xffffa0;
    drawString(minecraft->font, s.name.C_String(), x, y + 2, color);
    drawString(minecraft->font, s.address.ToString(false), x, y + 16, 0xffffa0);
  }
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

  Button bJoin;
  Button bBack;
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

#endif /*NET_MINECRAFT_CLIENT_GUI_SCREENS__JoinGameScreen_H__*/
