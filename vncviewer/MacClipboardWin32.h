// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNCVIEWER_MAC_CLIPBOARD_WIN32_H
#define VNCVIEWER_MAC_CLIPBOARD_WIN32_H

#include "ClipboardSshWin32.h"
#include "MacClipboardProtocol.h"
#include <functional>

class Fl_Window;
class Fl_Input;
class Fl_Box;
class Fl_Widget;

class MacClipboardWin32 {
public:
  MacClipboardWin32(std::function<bool()> focused, std::function<HWND()> owner);
  ~MacClipboardWin32();
  void showSettings(const char* host);
  bool enabled() const { return configured; }
private:
  static void timer(void* data);
  static void startCallback(Fl_Widget*, void* data);
  static void stopCallback(Fl_Widget*, void* data);
  void start();
  void stop();
  void tick();
  void request(bool write, const std::string& text, bool mayReceive);
  void status(const std::string& text);

  std::function<bool()> focused;
  std::function<HWND()> owner;
  ClipboardSshWin32 ssh;
  macclipboard::Revisions revisions;
  bool configured = false, failed = false, ready = false;
  bool reading = false, mayReceive = false, wasFocused = false, finalRead = false;
  bool previousSend = false, previousAccept = false;
  DWORD localSequence = 0, requestSequence = 0;
  ULONGLONG nextRead = 0;
  std::string host, port, user;
  Fl_Window* settings = nullptr;
  Fl_Input *hostInput = nullptr, *portInput = nullptr, *userInput = nullptr;
  Fl_Box* statusBox = nullptr;
  static MacClipboardWin32* active;
};
#endif
