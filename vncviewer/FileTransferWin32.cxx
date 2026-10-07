// Copyright 2026. Distributed under the GNU GPL, version 2 or later.
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <windows.h>

#include <stdexcept>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Window.H>
#include <FL/fl_ask.H>

#include <core/i18n.h>
#include <core/string.h>

#include "FileTransferCommand.h"
#include "FileTransferWin32.h"
#include "fltk/util.h"

namespace {
std::wstring lastExecutable;

bool executableExists(const std::wstring& path)
{
  // Never resolve a relative executable against the current directory/PATH.
  bool absolute = (path.size() > 3 && path[1] == L':' &&
                   (path[2] == L'\\' || path[2] == L'/')) ||
                  (path.size() > 2 && path[0] == L'\\' && path[1] == L'\\');
  if (!absolute)
    return false;
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring registeredExecutable(HKEY root, REGSAM view)
{
  HKEY key;
  if (RegOpenKeyExW(root,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\WinSCP.exe",
                   0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS)
    return {};
  wchar_t buffer[32768] = {};
  DWORD type = 0, size = sizeof(buffer) - sizeof(wchar_t);
  LONG result = RegQueryValueExW(key, nullptr, nullptr, &type,
                                 reinterpret_cast<BYTE*>(buffer), &size);
  RegCloseKey(key);
  if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
    return {};
  std::wstring path(buffer);
  if (type == REG_EXPAND_SZ) {
    DWORD count = ExpandEnvironmentStringsW(path.c_str(), buffer, 32768);
    if (!count || count > 32768)
      return {};
    path = buffer;
  }
  if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
    path = path.substr(1, path.size() - 2);
  return executableExists(path) ? path : std::wstring();
}

std::wstring findExecutable()
{
  if (executableExists(lastExecutable))
    return lastExecutable;
  for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
    for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
      std::wstring path = registeredExecutable(root, view);
      if (!path.empty())
        return path;
    }
  }
  for (const wchar_t* location : {
         L"%ProgramFiles%\\WinSCP\\WinSCP.exe",
         L"%ProgramFiles(x86)%\\WinSCP\\WinSCP.exe",
         L"%LOCALAPPDATA%\\Programs\\WinSCP\\WinSCP.exe"}) {
    wchar_t buffer[32768];
    DWORD count = ExpandEnvironmentStringsW(location, buffer, 32768);
    if (count && count <= 32768 && executableExists(buffer))
      return buffer;
  }
  return {};
}

void launch(const std::wstring& executable, const std::string& url)
{
  if (!executableExists(executable))
    throw std::invalid_argument("Install WinSCP and select the full path to WinSCP.exe");
  std::wstring name = executable.substr(executable.find_last_of(L"\\/") + 1);
  if (_wcsicmp(name.c_str(), L"WinSCP.exe") != 0)
    throw std::invalid_argument("Select WinSCP.exe");

  std::wstring command = filetransfer::quoteArgument(executable) +
                         L" /newinstance " +
                         filetransfer::quoteArgument(core::utf8ToUTF16(url.c_str()));
  std::vector<wchar_t> buffer(command.begin(), command.end());
  buffer.push_back(0);
  std::wstring directory = executable.substr(0, executable.find_last_of(L"\\/"));
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  // Passwords, VNC credentials and host-key bypass flags are never passed.
  if (!CreateProcessW(executable.c_str(), buffer.data(), nullptr, nullptr,
                      FALSE, 0, nullptr, directory.c_str(), &startup, &process)) {
    DWORD error = GetLastError();
    throw std::runtime_error("Unable to start WinSCP (Windows error " +
                             std::to_string(error) + ")");
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
}

class TransferDialog : public Fl_Window {
public:
  explicit TransferDialog(const char* serverHost)
    : Fl_Window(620, 330, _("File transfer (WinSCP)"))
  {
    auto* info = new Fl_Box(20, 12, 580, 48,
      _("Open an SFTP connection in WinSCP.\nDrag files between its local and remote panels in either direction."));
    info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    host = new Fl_Input(155, 75, 445, 28, _("SSH host:"));
    host->value(serverHost);
    port = new Fl_Input(155, 115, 100, 28, _("SSH port:"));
    port->value("22");
    user = new Fl_Input(155, 155, 445, 28, _("SSH username:"));
    user->tooltip(_("Leave empty to enter the username in WinSCP"));
    executable = new Fl_Input(155, 195, 340, 28, _("WinSCP.exe:"));
    executable->value(core::utf16ToUTF8(findExecutable().c_str()).c_str());
    auto* browse = new Fl_Button(505, 195, 95, 28, _("Browse..."));
    browse->callback(browseCallback, this);
    auto* note = new Fl_Box(20, 233, 580, 40,
      _("WinSCP must be installed separately.\nEnter SSH credentials and verify the host key in WinSCP."));
    note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    auto* cancel = new Fl_Button(380, 285, 100, 28, _("Cancel"));
    cancel->callback(closeCallback, this);
    auto* open = new Fl_Return_Button(490, 285, 110, 28, _("Open"));
    open->callback(openCallback, this);
    callback(closeCallback, this);
    end();
    set_modal();
  }

private:
  static void closeCallback(Fl_Widget*, void* data)
  {
    static_cast<TransferDialog*>(data)->hide();
  }

  static void browseCallback(Fl_Widget*, void* data)
  {
    auto* self = static_cast<TransferDialog*>(data);
    Fl_Native_File_Chooser chooser;
    chooser.title(_("Select WinSCP.exe"));
    chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
    chooser.filter("WinSCP\tWinSCP.exe");
    if (chooser.show() == 0 && chooser.filename())
      self->executable->value(chooser.filename());
  }

  static void openCallback(Fl_Widget*, void* data)
  {
    auto* self = static_cast<TransferDialog*>(data);
    try {
      std::string url = filetransfer::sessionUrl(self->host->value(),
                                                 self->port->value(),
                                                 self->user->value());
      std::wstring path = core::utf8ToUTF16(self->executable->value());
      launch(path, url);
      lastExecutable = path;
      self->hide();
    } catch (const std::exception& e) {
      fl_alert("%s", fltk_escape(e.what()).c_str());
    }
  }

  Fl_Input *host, *port, *user, *executable;
};
}

void showFileTransfer(const char* host)
{
  // Own all input data: the VNC connection may close while this dialog is open.
  TransferDialog dialog(host);
  dialog.show();
  while (dialog.shown())
    Fl::wait();
}
