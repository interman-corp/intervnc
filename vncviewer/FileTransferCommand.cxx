// Copyright 2026. Distributed under the GNU GPL, version 2 or later.
#include "FileTransferCommand.h"

#include <stdexcept>

namespace {
bool alphaNumeric(unsigned char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}

std::string encode(const std::string& value)
{
  static const char hex[] = "0123456789ABCDEF";
  std::string result;
  for (unsigned char c : value) {
    if (c < 32 || c == 127)
      throw std::invalid_argument("Control characters are not allowed");
    if (alphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == '~')
      result += c;
    else {
      result += '%';
      result += hex[c >> 4];
      result += hex[c & 15];
    }
  }
  return result;
}
}

std::string filetransfer::sessionUrl(std::string host, const std::string& port,
                                     const std::string& user)
{
  if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
    host = host.substr(1, host.size() - 2);
  if (host.empty())
    throw std::invalid_argument("Enter the SSH host name");

  // Reject URL syntax, whitespace and switches. Accept DNS names, IPv4 and
  // IPv6 literals (including a scope ID); WinSCP performs address resolution.
  bool ipv6 = host.find(':') != std::string::npos;
  for (unsigned char c : host) {
    if (!alphaNumeric(c) && c != '.' && c != '-' && c != '_' &&
        !(ipv6 && (c == ':' || c == '%')))
      throw std::invalid_argument("Enter a host name or IP address, not a URL");
  }
  if (!alphaNumeric(host.front()) && !(ipv6 && host.front() == ':'))
    throw std::invalid_argument("Invalid SSH host name");

  unsigned number = 0;
  if (port.empty() || port.size() > 5)
    throw std::invalid_argument("SSH port must be between 1 and 65535");
  for (char c : port) {
    if (c < '0' || c > '9')
      throw std::invalid_argument("SSH port must be between 1 and 65535");
    number = number * 10 + c - '0';
  }
  if (number == 0 || number > 65535)
    throw std::invalid_argument("SSH port must be between 1 and 65535");

  std::string authority;
  if (ipv6) {
    // Encode a scope delimiter without encoding IPv6's colons.
    for (char c : host)
      authority += c == '%' ? "%25" : std::string(1, c);
    authority = "[" + authority + "]";
  } else
    authority = host;

  return "sftp://" + (user.empty() ? "" : encode(user) + "@") +
         authority + ":" + std::to_string(number) + "/";
}

std::wstring filetransfer::quoteArgument(const std::wstring& argument)
{
  // Windows command-line quoting: backslashes before a quote or the closing
  // quote must be doubled. CreateProcess is called without a command shell.
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t c : argument) {
    if (c == L'\0')
      throw std::invalid_argument("NUL in process argument");
    if (c == L'\\') {
      ++slashes;
      continue;
    }
    result.append(slashes * (c == L'\"' ? 2 : 1), L'\\');
    slashes = 0;
    if (c == L'\"')
      result += L'\\';
    result += c;
  }
  result.append(slashes * 2, L'\\');
  result += L'\"';
  return result;
}
