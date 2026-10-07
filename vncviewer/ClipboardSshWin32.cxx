// SPDX-License-Identifier: GPL-2.0-or-later
#include "ClipboardSshWin32.h"
#include "FileTransferCommand.h"
#include "MacClipboardProtocol.h"

#include <algorithm>
#include <stdexcept>
#include <vector>
#include <core/string.h>

namespace {
struct Handle {
  HANDLE value = nullptr;
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  HANDLE release() { HANDLE result = value; value = nullptr; return result; }
};

void check(bool success, const char* operation)
{
  if (!success)
    throw std::runtime_error(std::string(operation) + " (Windows error " +
                             std::to_string(GetLastError()) + ")");
}

std::wstring sshExecutable()
{
  wchar_t directory[MAX_PATH];
  UINT count = GetWindowsDirectoryW(directory, MAX_PATH);
  check(count > 0 && count < MAX_PATH, "Cannot find Windows directory");
  // Sysnative is available to 32-bit callers on 64-bit Windows.
  for (const wchar_t* suffix : {L"\\Sysnative\\OpenSSH\\ssh.exe",
                                L"\\System32\\OpenSSH\\ssh.exe"}) {
    std::wstring path = std::wstring(directory) + suffix;
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
      return path;
  }
  throw std::runtime_error("Install the Windows OpenSSH Client optional feature");
}

void drain(HANDLE pipe, std::string& output, size_t limit)
{
  // Limit work per tick even if a faulty peer continuously produces output.
  for (unsigned i = 0; i < 32; ++i) {
    DWORD available;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
      if (GetLastError() == ERROR_BROKEN_PIPE)
        return;
      check(false, "Cannot read SSH output");
    }
    if (!available)
      return;
    char buffer[4096];
    DWORD received;
    check(ReadFile(pipe, buffer, (std::min)(available, DWORD(sizeof(buffer))),
                   &received, nullptr) != FALSE, "Cannot read SSH output");
    if (output.size() + received > limit)
      throw std::runtime_error("SSH clipboard response exceeds the size limit");
    output.append(buffer, received);
  }
}
}

ClipboardSshWin32::~ClipboardSshWin32() { cancel(); }

void ClipboardSshWin32::cancel()
{
  // Closing the job kills SSH and any proxy children, releasing pipe readers
  // before joining the stdin writer (which may be blocked in WriteFile).
  if (job) CloseHandle(job);
  job = nullptr;
  if (process) {
    TerminateProcess(process, 1);
    CloseHandle(process);
  }
  process = nullptr;
  if (writer.joinable()) writer.join();
  if (stdoutPipe) CloseHandle(stdoutPipe);
  if (stderrPipe) CloseHandle(stderrPipe);
  stdoutPipe = stderrPipe = nullptr;
}

void ClipboardSshWin32::start(const std::string& host, const std::string& port,
                              const std::string& user, bool write,
                              const std::string& input)
{
  cancel();
  // Reuse strict field validation, but pass SSH arguments separately.
  filetransfer::sessionUrl(host, port, user);
  if (user.empty())
    throw std::invalid_argument("Enter the active Mac desktop user's short name");
  if (input.size() > macclipboard::maxText)
    throw std::invalid_argument("Clipboard exceeds 1 MiB");
  std::string address = host;
  if (address.front() == '[' && address.back() == ']')
    address = address.substr(1, address.size() - 2);
  std::wstring executable = sshExecutable();
  std::wstring command = filetransfer::quoteArgument(executable);
  for (const std::string& arg : {
         std::string("-T"), std::string("-oBatchMode=yes"),
         std::string("-oStrictHostKeyChecking=yes"), std::string("-oConnectTimeout=10"),
         std::string("-oClearAllForwardings=yes"), std::string("-oPermitLocalCommand=no"),
         std::string("-oRemoteCommand=none"), std::string("-p"), port,
         std::string("-l"), user, std::string("--"), address,
         macclipboard::remoteCommand(write)})
    command += L" " + filetransfer::quoteArgument(core::utf8ToUTF16(arg.c_str()));
  std::vector<wchar_t> line(command.begin(), command.end());
  line.push_back(0);

  SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
  Handle inRead, inWrite, outRead, outWrite, errRead, errWrite;
  check(CreatePipe(&inRead.value, &inWrite.value, &security, 0), "Cannot create SSH stdin");
  check(CreatePipe(&outRead.value, &outWrite.value, &security, 0), "Cannot create SSH stdout");
  check(CreatePipe(&errRead.value, &errWrite.value, &security, 0), "Cannot create SSH stderr");
  for (HANDLE parent : {inWrite.value, outRead.value, errRead.value})
    check(SetHandleInformation(parent, HANDLE_FLAG_INHERIT, 0), "Cannot protect pipe handle");

  // Whitelist inherited handles; do not leak VNC sockets or unrelated pipes.
  SIZE_T bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
  std::vector<unsigned char> storage(bytes);
  auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  check(InitializeProcThreadAttributeList(attributes, 1, 0, &bytes), "Cannot initialize SSH handles");
  struct Attributes {
    LPPROC_THREAD_ATTRIBUTE_LIST value;
    ~Attributes() { DeleteProcThreadAttributeList(value); }
  } cleanup{attributes};
  HANDLE inherited[] = {inRead.value, outWrite.value, errWrite.value};
  check(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                  inherited, sizeof(inherited), nullptr, nullptr),
         "Cannot restrict SSH handles");
  Handle newJob;
  newJob.value = CreateJobObjectW(nullptr, nullptr);
  check(newJob.value != nullptr, "Cannot create SSH job");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  check(SetInformationJobObject(newJob.value, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits)), "Cannot configure SSH job");
  STARTUPINFOEXW startup = {};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = inRead.value;
  startup.StartupInfo.hStdOutput = outWrite.value;
  startup.StartupInfo.hStdError = errWrite.value;
  startup.lpAttributeList = attributes;
  PROCESS_INFORMATION child = {};
  check(CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                        nullptr, nullptr, &startup.StartupInfo, &child), "Cannot start SSH");
  Handle childProcess, childThread;
  childProcess.value = child.hProcess;
  childThread.value = child.hThread;
  if (!AssignProcessToJobObject(newJob.value, child.hProcess)) {
    DWORD error = GetLastError();
    TerminateProcess(child.hProcess, 1);
    SetLastError(error);
    check(false, "Cannot manage SSH process");
  }
  check(ResumeThread(child.hThread) != DWORD(-1), "Cannot resume SSH");
  process = childProcess.release();
  job = newJob.release();
  stdoutPipe = outRead.release();
  stderrPipe = errRead.release();
  response.clear();
  errors.clear();
  started = GetTickCount64();
  HANDLE writePipe = inWrite.value;
  try {
    writer = std::thread([writePipe, input]() {
      size_t offset = 0;
      while (offset < input.size()) {
        DWORD sent;
        DWORD count = static_cast<DWORD>((std::min)(size_t(4096), input.size() - offset));
        if (!WriteFile(writePipe, input.data() + offset, count, &sent, nullptr) || !sent)
          break;
        offset += sent;
      }
      CloseHandle(writePipe); // EOF, including empty text
    });
    inWrite.release();
  } catch (...) {
    cancel();
    throw;
  }
}

bool ClipboardSshWin32::poll()
{
  if (!process)
    return false;
  if (GetTickCount64() - started > 15000)
    throw std::runtime_error("SSH clipboard request timed out");
  drain(stdoutPipe, response, macclipboard::maxText + 64);
  drain(stderrPipe, errors, 8192);
  DWORD state = WaitForSingleObject(process, 0);
  check(state != WAIT_FAILED, "Cannot wait for SSH");
  if (state == WAIT_TIMEOUT)
    return false;
  // The pipe may hold more than a tick's drain budget after exit.
  DWORD available;
  if (PeekNamedPipe(stdoutPipe, nullptr, 0, nullptr, &available, nullptr) && available)
    return false;
  DWORD code = 1;
  check(GetExitCodeProcess(process, &code), "Cannot get SSH status");
  cancel();
  if (code != 0)
    throw std::runtime_error("SSH clipboard failed. Check key authentication, known_hosts, "
                            "and the active Mac user.\n" + errors);
  return true;
}
