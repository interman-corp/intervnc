// Copyright 2026. Distributed under the GNU GPL, version 2 or later.
#include <gtest/gtest.h>

#include <stdexcept>

#include "FileTransferCommand.h"

using filetransfer::sessionUrl;
using filetransfer::quoteArgument;

TEST(FileTransferCommand, SftpHostAndIndependentPort)
{
  EXPECT_EQ(sessionUrl("mac.local", "22", "alice"),
            "sftp://alice@mac.local:22/");
  EXPECT_EQ(sessionUrl("192.168.1.10", "02222", ""),
            "sftp://192.168.1.10:2222/");
  EXPECT_EQ(sessionUrl("::1", "22", "alice"),
            "sftp://alice@[::1]:22/");
  EXPECT_EQ(sessionUrl("[2001:db8::1]", "22", ""),
            "sftp://[2001:db8::1]:22/");
  EXPECT_EQ(sessionUrl("fe80::1%12", "22", ""),
            "sftp://[fe80::1%2512]:22/");
}

TEST(FileTransferCommand, UsernameCannotInjectCredentialsOrOptions)
{
  EXPECT_EQ(sessionUrl("mac.local", "22", "a@b:secret;x=y /\"%"),
            "sftp://a%40b%3Asecret%3Bx%3Dy%20%2F%22%25@mac.local:22/");
  EXPECT_EQ(sessionUrl("mac.local", "22", u8"利用者"),
            "sftp://%E5%88%A9%E7%94%A8%E8%80%85@mac.local:22/");
  EXPECT_THROW(sessionUrl("mac.local", "22", "a\nb"), std::invalid_argument);
  EXPECT_THROW(sessionUrl("mac.local", "22", std::string("a\0b", 3)),
               std::invalid_argument);
}

TEST(FileTransferCommand, RejectsUrlAndOptionInjection)
{
  for (const char* host : {"", "sftp://mac.local", "user@host", "host/path",
                           "host?x=y", "host#fragment", "host;x=y",
                           "host\" /command x", "host\n", "host\\path",
                           "/command", "-host", "[::1", "::1]", "host%40evil"})
    EXPECT_THROW(sessionUrl(host, "22", "alice"), std::invalid_argument) << host;
}

TEST(FileTransferCommand, RejectsInvalidPorts)
{
  for (const char* port : {"", "0", "65536", "99999999999999999999", "-1",
                           "+22", " 22", "22 ", "22/", "22\n", "22x"})
    EXPECT_THROW(sessionUrl("mac.local", port, ""), std::invalid_argument) << port;
  EXPECT_EQ(sessionUrl("mac.local", "65535", ""), "sftp://mac.local:65535/");
}

TEST(FileTransferCommand, WindowsArgumentQuoting)
{
  EXPECT_EQ(quoteArgument(L""), L"\"\"");
  EXPECT_EQ(quoteArgument(L"C:\\Program Files\\WinSCP\\WinSCP.exe"),
            L"\"C:\\Program Files\\WinSCP\\WinSCP.exe\"");
  EXPECT_EQ(quoteArgument(L"a\"b"), L"\"a\\\"b\"");
  EXPECT_EQ(quoteArgument(L"a\\"), L"\"a\\\\\"");
  EXPECT_EQ(quoteArgument(L"a\\\"b"), L"\"a\\\\\\\"b\"");
  EXPECT_EQ(quoteArgument(L"日本語 & name"), L"\"日本語 & name\"");
  EXPECT_THROW(quoteArgument(std::wstring(L"a\0b", 3)), std::invalid_argument);
}
