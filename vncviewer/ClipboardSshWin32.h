// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNCVIEWER_CLIPBOARD_SSH_WIN32_H
#define VNCVIEWER_CLIPBOARD_SSH_WIN32_H

#include <windows.h>
#include <string>
#include <thread>

// One bounded, asynchronous SSH request. All clipboard bytes travel in pipes.
class ClipboardSshWin32 {
public:
  ClipboardSshWin32() = default;
  ~ClipboardSshWin32();
  void start(const std::string& host, const std::string& port,
             const std::string& user, bool write, const std::string& input);
  bool poll();
  bool running() const { return process != nullptr; }
  void cancel();
  const std::string& output() const { return response; }
private:
  ClipboardSshWin32(const ClipboardSshWin32&) = delete;
  ClipboardSshWin32& operator=(const ClipboardSshWin32&) = delete;
  HANDLE process = nullptr, job = nullptr, stdoutPipe = nullptr, stderrPipe = nullptr;
  std::thread writer;
  ULONGLONG started = 0;
  std::string response, errors;
};
#endif
