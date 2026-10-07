// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "MacClipboardWin32.h"
#include "FileTransferCommand.h"
#include "parameters.h"
#include "fltk/util.h"

#include <cstring>
#include <stdexcept>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <core/i18n.h>
#include <core/string.h>

namespace {
// false means temporarily busy; callers retry without losing the sequence.
bool readClipboard(std::string& text, bool& present, DWORD& sequence)
{
  if (!OpenClipboard(nullptr))
    return false;
  struct Close { ~Close() { CloseClipboard(); } } close;
  sequence = GetClipboardSequenceNumber();
  present = IsClipboardFormatAvailable(CF_UNICODETEXT) &&
            !IsClipboardFormatAvailable(CF_HDROP);
  if (!present)
    return true;
  HANDLE data = GetClipboardData(CF_UNICODETEXT);
  if (!data)
    return false;
  SIZE_T bytes = GlobalSize(data);
  if (bytes > 2 * macclipboard::maxText + sizeof(wchar_t))
    throw std::runtime_error("Windows clipboard exceeds 1 MiB of text");
  auto* value = static_cast<const wchar_t*>(GlobalLock(data));
  if (!value)
    return false;
  struct Unlock {
    HANDLE handle;
    ~Unlock() { GlobalUnlock(handle); }
  } unlock{data};
  size_t length = 0;
  while (length < bytes / sizeof(wchar_t) && value[length]) ++length;
  if (length == bytes / sizeof(wchar_t) || !core::isValidUTF16(value, length))
    throw std::runtime_error("Invalid Unicode in Windows clipboard");
  text = core::utf16ToUTF8(value, length);
  text = core::convertLF(text.c_str(), text.size());
  if (text.size() > macclipboard::maxText)
    throw std::runtime_error("Windows clipboard exceeds 1 MiB of text");
  return true;
}

bool writeClipboard(HWND owner, const std::string& text, DWORD expected, DWORD& sequence)
{
  std::string crlf = core::convertCRLF(text.c_str(), text.size());
  std::wstring value = core::utf8ToUTF16(crlf.c_str(), crlf.size());
  size_t bytes = (value.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!memory)
    throw std::runtime_error("Cannot allocate clipboard memory");
  void* buffer = GlobalLock(memory);
  if (!buffer) {
    GlobalFree(memory);
    throw std::runtime_error("Cannot lock clipboard memory");
  }
  memcpy(buffer, value.c_str(), bytes);
  GlobalUnlock(memory);
  if (!owner || !OpenClipboard(owner)) {
    GlobalFree(memory);
    return false;
  }
  // Check again under the clipboard lock: another application might copy
  // between the timer's sequence check and OpenClipboard.
  if (GetClipboardSequenceNumber() != expected) {
    CloseClipboard();
    GlobalFree(memory);
    return false;
  }
  bool success = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
  sequence = GetClipboardSequenceNumber();
  CloseClipboard();
  if (!success) {
    GlobalFree(memory);
    throw std::runtime_error("Cannot write Windows clipboard");
  }
  return true;
}
}

MacClipboardWin32* MacClipboardWin32::active = nullptr;

MacClipboardWin32::MacClipboardWin32(std::function<bool()> focused_,
                                     std::function<HWND()> owner_)
  : focused(focused_), owner(owner_) {}

MacClipboardWin32::~MacClipboardWin32()
{
  stop();
  delete settings;
}

void MacClipboardWin32::status(const std::string& text)
{
  if (statusBox)
    statusBox->copy_label(fltk_escape(text).c_str());
}

void MacClipboardWin32::stop()
{
  Fl::remove_timeout(timer, this);
  ssh.cancel();
  configured = false;
  failed = false;
  if (active == this) active = nullptr;
  status(_("SSH clipboard is off."));
}

void MacClipboardWin32::start()
{
  std::string newHost = hostInput->value();
  std::string newPort = portInput->value();
  std::string newUser = userInput->value();
  filetransfer::sessionUrl(newHost, newPort, newUser);
  if (newUser.empty())
    throw std::invalid_argument("Enter the active Mac desktop user's short name");
  stop();
  if (active) active->stop(); // Only one remote may own the shared clipboard.
  active = this;
  host = newHost;
  port = newPort;
  user = newUser;
  configured = true;
  ready = wasFocused = finalRead = false;
  previousSend = sendClipboard;
  previousAccept = acceptClipboard;
  revisions.reset();
  localSequence = GetClipboardSequenceNumber();
  nextRead = 0;
  status(_("Connecting via SSH..."));
  Fl::add_timeout(0.1, timer, this);
}

void MacClipboardWin32::request(bool write, const std::string& text, bool receive)
{
  requestSequence = GetClipboardSequenceNumber();
  reading = !write;
  mayReceive = receive;
  ssh.start(host, port, user, write, text);
  nextRead = GetTickCount64() + 1000;
}

void MacClipboardWin32::tick()
{
  if (viewOnly || previousSend != bool(sendClipboard) ||
      previousAccept != bool(acceptClipboard)) {
    ssh.cancel();
    ready = false;
    revisions.reset();
    localSequence = GetClipboardSequenceNumber();
    previousSend = sendClipboard;
    previousAccept = acceptClipboard;
    wasFocused = finalRead = false;
    if (viewOnly) {
      status(_("SSH clipboard paused in view-only mode."));
      return;
    }
  }
  if (!sendClipboard && !acceptClipboard) {
    status(_("Clipboard send and receive are disabled in Options."));
    return;
  }
  bool focus = focused();
  if (wasFocused && !focus) finalRead = true;
  wasFocused = focus;
  if (!sendClipboard && focus) localSequence = GetClipboardSequenceNumber();

  if (ssh.running()) {
    if (!ssh.poll()) return;
    macclipboard::Snapshot snapshot = macclipboard::parse(ssh.output());
    auto updated = revisions;
    bool changed = updated.observe(snapshot);
    if (reading && mayReceive && changed && acceptClipboard &&
        GetClipboardSequenceNumber() == requestSequence &&
        localSequence == requestSequence) {
      DWORD sequence;
      if (!writeClipboard(owner(), snapshot.value, requestSequence, sequence)) {
        // Keep the old revision so a busy Windows clipboard can be retried.
        finalRead = true;
        return;
      }
      localSequence = sequence;
    }
    revisions = updated;
    ready = true;
    status(_("SSH clipboard active. Copy text after closing this dialog."));
  }

  if (!ready) {
    // Establish a baseline without overwriting either existing clipboard.
    request(false, "", false);
    return;
  }
  if (focus && sendClipboard && GetClipboardSequenceNumber() != localSequence) {
    std::string text;
    bool present;
    DWORD sequence;
    if (!readClipboard(text, present, sequence)) return;
    localSequence = sequence;
    if (present) {
      request(true, text, false);
      return;
    }
  }
  // One final read after leaving the VNC window catches a copy immediately
  // followed by switching to a local app. Local changes always win races.
  if (acceptClipboard && (focus || finalRead) && GetTickCount64() >= nextRead) {
    finalRead = false;
    if (!focus && GetClipboardSequenceNumber() != localSequence)
      return;
    request(false, "", true);
  }
}

void MacClipboardWin32::timer(void* data)
{
  auto* self = static_cast<MacClipboardWin32*>(data);
  try {
    self->tick();
  } catch (const std::exception& e) {
    self->ssh.cancel();
    self->failed = true;
    self->status(std::string("SSH clipboard stopped: ") + e.what());
    // No modal wait or automatic reconnect loop. Settings offers explicit retry.
    self->settings->show();
  }
  if (self->configured && !self->failed)
    Fl::repeat_timeout(0.1, timer, self);
}

void MacClipboardWin32::startCallback(Fl_Widget*, void* data)
{
  auto* self = static_cast<MacClipboardWin32*>(data);
  try { self->start(); }
  catch (const std::exception& e) { self->status(e.what()); }
}

void MacClipboardWin32::stopCallback(Fl_Widget*, void* data)
{
  static_cast<MacClipboardWin32*>(data)->stop();
}

void MacClipboardWin32::showSettings(const char* serverHost)
{
  if (!settings) {
    settings = new Fl_Window(660, 395, _("Mac clipboard (SSH)"));
    auto* info = new Fl_Box(20, 12, 620, 64,
      _("Share plain text with macOS Screen Sharing over SSH.\nRequires Windows OpenSSH, key authentication and a verified host key.\nUse the same Mac account as the active desktop."));
    info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    hostInput = new Fl_Input(150, 90, 485, 28, _("SSH host:"));
    hostInput->value(serverHost);
    portInput = new Fl_Input(150, 130, 100, 28, _("SSH port:"));
    portInput->value("22");
    userInput = new Fl_Input(150, 170, 485, 28, _("Mac username:"));
    statusBox = new Fl_Box(20, 213, 620, 125, _("SSH clipboard is off."));
    statusBox->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
    auto* enable = new Fl_Button(260, 350, 120, 28, _("Enable / Retry"));
    enable->callback(startCallback, this);
    auto* disable = new Fl_Button(390, 350, 110, 28, _("Disable"));
    disable->callback(stopCallback, this);
    auto* close = new Fl_Return_Button(510, 350, 125, 28, _("Close"));
    close->callback([](Fl_Widget*, void* data) { static_cast<Fl_Window*>(data)->hide(); }, settings);
    settings->end();
  }
  settings->show();
}
