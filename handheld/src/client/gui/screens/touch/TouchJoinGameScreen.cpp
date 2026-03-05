#include "TouchJoinGameScreen.h"
#include "../../../Minecraft.h"
#include "../../../renderer/Textures.h"
#include "../../Font.h"
#include "../ProgressScreen.h"
#include "../StartMenuScreen.h"
#include "network/ClientSideNetworkHandler.h"
#include "network/RakNetInstance.h"
#include "platform/input/Keyboard.h"
#include "platform/input/Mouse.h"
#include "raknet/RakPeerInterface.h"
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iomanip>
#include <sstream>

namespace {
static const int kDefaultPort = 19132;
static std::string s_cachedUsername = "";

static std::string makeUniqueUsername(const std::string &base,
                                      Minecraft *minecraft) {
  std::string suffix;

  if (minecraft && minecraft->raknetInstance) {
    RakNet::RakPeerInterface *peer = minecraft->raknetInstance->getPeer();
    if (peer) {
      RakNet::RakNetGUID guid = peer->GetMyGUID();
      std::string guidStr = guid.ToString();
      std::size_t hash = std::hash<std::string>{}(guidStr);
      unsigned int v = (unsigned int)(hash & 0xFFFF);
      std::ostringstream out;
      out << std::hex << std::setw(4) << std::setfill('0') << v;
      suffix = out.str();
    }
  }

  if (suffix.empty()) {
    unsigned int t = (unsigned int)std::time(NULL);
    static unsigned int counter = 0;
    ++counter;
    unsigned int v = (t + counter) & 0xFFFF;
    std::ostringstream out;
    out << std::hex << std::setw(4) << std::setfill('0') << v;
    suffix = out.str();
  }

  std::string trimmed = base;
  if (trimmed.size() > 12)
    trimmed = trimmed.substr(0, 12);

  return trimmed + suffix;
}

static std::string trim(const std::string &value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

static bool isDigits(const std::string &value) {
  if (value.empty())
    return false;
  for (size_t i = 0; i < value.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(value[i])))
      return false;
  }
  return true;
}

static bool parseHostPort(const std::string &input, std::string &host,
                          int &port) {
  std::string cleaned = trim(input);
  if (cleaned.empty())
    return false;

  host = cleaned;
  port = kDefaultPort;

  size_t colon = cleaned.rfind(':');
  if (colon != std::string::npos && colon + 1 < cleaned.size()) {
    std::string portStr = cleaned.substr(colon + 1);
    if (isDigits(portStr)) {
      host = cleaned.substr(0, colon);
      port = std::atoi(portStr.c_str());
    }
  }

  host = trim(host);
  return !host.empty();
}

} // namespace

namespace Touch {

//
// Games list
//

void AvailableGamesList::selectStart(int item) { startSelected = item; }

void AvailableGamesList::selectCancel() { startSelected = -1; }

void AvailableGamesList::selectItem(int item, bool doubleClick) {
  LOGI("selected an item! %d\n", item);
  selectedItem = item;
}

void AvailableGamesList::renderItem(int i, int x, int y, int h, Tesselator &t) {
  if (startSelected == i && Multitouch::getFirstActivePointerIdEx() >= 0) {
    fill((int)x0, y, (int)x1, y + h, 0x809E684F);
  }

  // static int colors[2] = {0xffffb0, 0xcccc90};
  const PingedCompatibleServer &s = copiedServerList[i];
  unsigned int color = s.isSpecial ? 0x6090a0 : 0xffffb0;
  unsigned int color2 = 0xffffa0; // colors[i&1];

  int xx1 = (int)x0 + 24;
  int xx2 = xx1;

  if (s.isSpecial) {
    xx1 += 50;

    glEnable2(GL_TEXTURE_2D);
    glColor4f2(1, 1, 1, 1);
    glEnable2(GL_BLEND);
    minecraft->textures->loadAndBindTexture("gui/badge/minecon140.png");
    blit(xx2, y + 6, 0, 0, 37, 8, 140, 240);
  }

  drawString(minecraft->font, s.name.C_String(), xx1, y + 4 + 2, color);
  drawString(minecraft->font, s.address.ToString(false), xx2, y + 18, color2);

  /*
  drawString(minecraft->font, copiedServerList[i].name.C_String(), (int)x0 + 24,
  y + 4, color); drawString(minecraft->font,
  copiedServerList[i].address.ToString(false), (int)x0 + 24, y + 18, color);
  */
}

//
// Join Game screen
//
JoinGameScreen::JoinGameScreen()
    : bJoin(2, "Join Game"), bBack(3, "Back"), bHeader(0, ""), gamesList(NULL) {
  bJoin.active = false;
  // gamesList->yInertia = 0.5f;
}

JoinGameScreen::~JoinGameScreen() { delete gamesList; }

void JoinGameScreen::init() {
  buttons.push_back(&bJoin);
  buttons.push_back(&bBack);
  buttons.push_back(&bHeader);

  directConnectFocused = false;
  directConnectText = "";
  usernameFocused = false;
  if (!s_cachedUsername.empty())
    usernameText = s_cachedUsername;
  else
    usernameText = minecraft ? minecraft->options.username : "";

  minecraft->raknetInstance->clearServerList();
  gamesList = new AvailableGamesList(minecraft, width, height);

#ifdef ANDROID
  tabButtons.push_back(&bJoin);
  tabButtons.push_back(&bBack);
#endif
}

void JoinGameScreen::setupPositions() {
  bJoin.y = 0;
  bBack.y = 0;
  bHeader.y = 0;

  bBack.x = 0;
  bJoin.x = width - bJoin.width;
  bHeader.x = bBack.width;
  bHeader.width = width - (bBack.width + bJoin.width);
  bHeader.height = bJoin.height;

  directConnectH = 18;
  directConnectX = 20;
  directConnectW = width - 40;
  directConnectY = height - directConnectH - 10;

  usernameH = 18;
  usernameX = 20;
  usernameW = width - 40;
  usernameY = directConnectY - usernameH - 6;

  if (gamesList) {
    gamesList->setBounds(24, usernameY - 6);
  }
}

void JoinGameScreen::buttonClicked(Button *button) {
  if (button->id == bJoin.id) {
    applyUsername();
    if (isIndexValid(gamesList->selectedItem)) {
      PingedCompatibleServer selectedServer =
          gamesList->copiedServerList[gamesList->selectedItem];
      minecraft->joinMultiplayer(selectedServer);
      {
        bJoin.active = false;
        bBack.active = false;
        minecraft->setScreen(new ProgressScreen());
      }
    } else if (!trim(directConnectText).empty()) {
      bJoin.active = false;
      bBack.active = false;
      connectDirect();
    }
    // minecraft->locateMultiplayer();
    // minecraft->setScreen(new JoinGameScreen());
  }
  if (button->id == bBack.id) {
    minecraft->platform()->hideKeyboard();
    minecraft->cancelLocateMultiplayer();
    minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
  }
}

bool JoinGameScreen::handleBackEvent(bool isDown) {
  if (!isDown) {
    minecraft->platform()->hideKeyboard();
    minecraft->cancelLocateMultiplayer();
    minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
  }
  return true;
}

void JoinGameScreen::removed() {
  directConnectFocused = false;
  usernameFocused = false;
  if (!usernameText.empty())
    s_cachedUsername = trim(usernameText);
  minecraft->platform()->hideKeyboard();
}

void JoinGameScreen::mouseClicked(int x, int y, int buttonNum) {
  Screen::mouseClicked(x, y, buttonNum);
  if (buttonNum != MouseAction::ACTION_LEFT)
    return;

  bool insideUsername = (x >= usernameX && x <= usernameX + usernameW &&
                         y >= usernameY && y <= usernameY + usernameH);
  bool insideDirect =
      (x >= directConnectX && x <= directConnectX + directConnectW &&
       y >= directConnectY && y <= directConnectY + directConnectH);

  if (insideUsername) {
    if (gamesList)
      gamesList->selectItem(-1, false);
    usernameFocused = true;
    directConnectFocused = false;
    minecraft->platform()->showKeyboard();
  } else if (insideDirect) {
    if (gamesList)
      gamesList->selectItem(-1, false);
    directConnectFocused = true;
    usernameFocused = false;
    minecraft->platform()->showKeyboard();
  } else {
    if (directConnectFocused || usernameFocused)
      minecraft->platform()->hideKeyboard();
    directConnectFocused = false;
    usernameFocused = false;
  }
}

void JoinGameScreen::keyPressed(int eventKey) {
  if (usernameFocused) {
    if (eventKey == Keyboard::KEY_BACKSPACE) {
      if (!usernameText.empty()) {
        usernameText.erase(usernameText.size() - 1, 1);
      }
      return;
    }
    if (eventKey == Keyboard::KEY_RETURN) {
      usernameFocused = false;
      directConnectFocused = true;
      minecraft->platform()->showKeyboard();
      return;
    }
  }

  if (directConnectFocused) {
    if (eventKey == Keyboard::KEY_BACKSPACE) {
      if (!directConnectText.empty()) {
        directConnectText.erase(directConnectText.size() - 1, 1);
      }
      return;
    }
    if (eventKey == Keyboard::KEY_RETURN) {
      if (!trim(directConnectText).empty()) {
        bJoin.active = false;
        bBack.active = false;
        connectDirect();
      }
      return;
    }
  }
  Screen::keyPressed(eventKey);
}

void JoinGameScreen::keyboardNewChar(char inputChar) {
  if (inputChar < 32 || inputChar >= 127)
    return;

  if (usernameFocused) {
    if (usernameText.size() < 16) {
      usernameText.push_back(inputChar);
    }
    return;
  }

  if (directConnectFocused) {
    if (directConnectText.size() < 128) {
      directConnectText.push_back(inputChar);
    }
  }
}

void JoinGameScreen::applyUsername() {
  if (!minecraft || !minecraft->user)
    return;

  std::string name = trim(usernameText);
  if (name.empty())
    name = minecraft->options.username;

  if (name.empty())
    return;

  if (name == minecraft->options.username) {
    name = makeUniqueUsername(name, minecraft);
  }

  minecraft->user->name = name;
  usernameText = name;
  s_cachedUsername = name;
}

void JoinGameScreen::connectDirect() {
  applyUsername();

  std::string host;
  int port = kDefaultPort;
  if (!parseHostPort(directConnectText, host, port))
    return;

  if (!minecraft->netCallback) {
    minecraft->netCallback =
        new ClientSideNetworkHandler(minecraft, minecraft->raknetInstance);
  }

  minecraft->isLookingForMultiplayer = false;
  minecraft->raknetInstance->connect(host.c_str(), port);
  minecraft->setScreen(new ProgressScreen());
}

bool JoinGameScreen::isIndexValid(int index) {
  return gamesList && index >= 0 && index < gamesList->getNumberOfItems();
}

void JoinGameScreen::tick() {
  if (isIndexValid(gamesList->selectedItem)) {
    buttonClicked(&bJoin);
    return;
  }

  // gamesList->tick();

  const ServerList &orgServerList = minecraft->raknetInstance->getServerList();
  ServerList serverList;
  for (unsigned int i = 0; i < orgServerList.size(); ++i)
    if (orgServerList[i].name.GetLength() > 0)
      serverList.push_back(orgServerList[i]);

  if (serverList.size() != gamesList->copiedServerList.size()) {
    // copy the currently selected item
    PingedCompatibleServer selectedServer;
    bool hasSelection = false;
    if (isIndexValid(gamesList->selectedItem)) {
      selectedServer = gamesList->copiedServerList[gamesList->selectedItem];
      hasSelection = true;
    }

    gamesList->copiedServerList = serverList;
    gamesList->selectItem(-1, false);

    // re-select previous item if it still exists
    if (hasSelection) {
      for (unsigned int i = 0; i < gamesList->copiedServerList.size(); i++) {
        if (gamesList->copiedServerList[i].address == selectedServer.address) {
          gamesList->selectItem(i, false);
          break;
        }
      }
    }
  } else {
    for (int i = (int)gamesList->copiedServerList.size() - 1; i >= 0; --i) {
      for (int j = 0; j < (int)serverList.size(); ++j)
        if (serverList[j].address == gamesList->copiedServerList[i].address)
          gamesList->copiedServerList[i].name = serverList[j].name;
    }
  }

  bJoin.active =
      isIndexValid(gamesList->selectedItem) || !trim(directConnectText).empty();
}

void JoinGameScreen::render(int xm, int ym, float a) {
  bool hasNetwork = minecraft->platform()->isNetworkEnabled(true);
#ifdef WIN32
  hasNetwork = hasNetwork && !GetAsyncKeyState(VK_TAB);
#endif

  renderBackground();
  if (hasNetwork)
    gamesList->render(xm, ym, a);
  else
    gamesList->renderDirtBackground();
  Screen::render(xm, ym, a);

  const int userLeft = usernameX;
  const int userRight = usernameX + usernameW;
  const int userTop = usernameY;
  const int userBottom = usernameY + usernameH;

  fill(userLeft, userTop, userRight, userBottom, 0x80000000);
  fill(userLeft - 1, userTop - 1, userLeft, userBottom + 1, 0xffa0a0a0);
  fill(userRight, userTop - 1, userRight + 1, userBottom + 1, 0xffa0a0a0);
  fill(userLeft, userTop - 1, userRight, userTop, 0xffa0a0a0);
  fill(userLeft, userBottom, userRight, userBottom + 1, 0xffa0a0a0);

  std::string userText = usernameText;
  int userColor = 0xffffffff;
  if (userText.empty()) {
    userText = "Username";
    userColor = 0xff909090;
  } else if (usernameFocused && ((int)(getTimeS() * 2) % 2 == 0)) {
    userText += "_";
  }

  drawString(minecraft->font, userText, userLeft + 4, userTop + 4, userColor);

  const int fieldLeft = directConnectX;
  const int fieldRight = directConnectX + directConnectW;
  const int fieldTop = directConnectY;
  const int fieldBottom = directConnectY + directConnectH;

  fill(fieldLeft, fieldTop, fieldRight, fieldBottom, 0x80000000);
  fill(fieldLeft - 1, fieldTop - 1, fieldLeft, fieldBottom + 1, 0xffa0a0a0);
  fill(fieldRight, fieldTop - 1, fieldRight + 1, fieldBottom + 1, 0xffa0a0a0);
  fill(fieldLeft, fieldTop - 1, fieldRight, fieldTop, 0xffa0a0a0);
  fill(fieldLeft, fieldBottom, fieldRight, fieldBottom + 1, 0xffa0a0a0);

  std::string displayText = directConnectText;
  int textColor = 0xffffffff;
  if (displayText.empty()) {
    displayText = "Direct connect (host:port)";
    textColor = 0xff909090;
  } else if (directConnectFocused && ((int)(getTimeS() * 2) % 2 == 0)) {
    displayText += "_";
  }

  drawString(minecraft->font, displayText, fieldLeft + 4, fieldTop + 4,
             textColor);

  const int baseX = bHeader.x + bHeader.width / 2;

  if (hasNetwork) {
#ifdef SDL3
    std::string s = "Scanning for Local Network Games...";
#else
    std::string s = "Scanning for WiFi Games...";
#endif
    drawCenteredString(minecraft->font, s, baseX, 8, 0xffffffff);

    const int textWidth = minecraft->font->width(s);
    const int spinnerX = baseX + textWidth / 2 + 6;

    static const char *spinnerTexts[] = {"-", "\\", "|", "/"};
    int n = ((int)(5.5f * getTimeS()) % 4);
    drawCenteredString(minecraft->font, spinnerTexts[n], spinnerX, 8,
                       0xffffffff);
  } else {
    drawCenteredString(minecraft->font, "WiFi is disabled", baseX, 8,
                       0xffffffff);
  }
}

bool JoinGameScreen::isInGameScreen() { return false; }

}; // namespace Touch
