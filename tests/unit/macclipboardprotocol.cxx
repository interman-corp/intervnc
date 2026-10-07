// SPDX-License-Identifier: GPL-2.0-or-later
#include <gtest/gtest.h>
#include <stdexcept>
#include "MacClipboardProtocol.h"

TEST(MacClipboard, PreservesUnicodeEmptyTextAndNewlines)
{
  auto text = macclipboard::parse(u8"IVNC1 42 T\n日本語 😀\r\nnext\n");
  EXPECT_EQ(text.revision, "42");
  EXPECT_TRUE(text.text);
  EXPECT_EQ(text.value, u8"日本語 😀\nnext\n");
  EXPECT_TRUE(macclipboard::parse("IVNC1 43 T\n").text);
  EXPECT_FALSE(macclipboard::parse("IVNC1 44 N\n").text);
  EXPECT_EQ(macclipboard::parse("IVNC1 45 T\n{\\rtf1 literal}").value,
            "{\\rtf1 literal}");
}

TEST(MacClipboard, RejectsMalformedAndOversizedResponses)
{
  for (const char* invalid : {"", "hello\n", "IVNC1  T\n", "IVNC1 x T\n",
                              "IVNC1 2 X\n", "IVNC1 2 T ", "IVNC1 2 N\nunexpected",
                              "IVNC1 2 T\n\xff"})
    EXPECT_THROW(macclipboard::parse(invalid), std::runtime_error);
  EXPECT_THROW(macclipboard::parse(std::string("IVNC1 2 T\na\0b", 14)), std::runtime_error);
  EXPECT_NO_THROW(macclipboard::parse("IVNC1 2 T\n" + std::string(macclipboard::maxText, 'a')));
  EXPECT_THROW(macclipboard::parse("IVNC1 2 T\n" + std::string(macclipboard::maxText + 1, 'a')),
               std::runtime_error);
}

TEST(MacClipboard, BaselineEchoSuppressionAndSameTextRecopy)
{
  macclipboard::Revisions revisions;
  EXPECT_FALSE(revisions.observe(macclipboard::parse("IVNC1 1 T\nexisting")));
  EXPECT_FALSE(revisions.observe(macclipboard::parse("IVNC1 1 T\nexisting")));
  EXPECT_TRUE(revisions.observe(macclipboard::parse("IVNC1 2 T\nnew")));
  EXPECT_TRUE(revisions.observe(macclipboard::parse("IVNC1 3 T\nnew")));
  // Acknowledging our write makes subsequent reads no-ops.
  revisions.observe(macclipboard::parse("IVNC1 4 T\nlocal"));
  EXPECT_FALSE(revisions.observe(macclipboard::parse("IVNC1 4 T\nlocal")));
  EXPECT_FALSE(revisions.observe(macclipboard::parse("IVNC1 5 N\n")));
  EXPECT_TRUE(revisions.observe(macclipboard::parse("IVNC1 6 T\n")));
  revisions.reset();
  EXPECT_FALSE(revisions.observe(macclipboard::parse("IVNC1 7 T\nreconnected")));
}

TEST(MacClipboard, RemoteCommandUsesFixedCodeNotClipboardText)
{
  std::string command = macclipboard::remoteCommand(true);
  EXPECT_NE(command.find("/usr/bin/osascript"), std::string::npos);
  EXPECT_NE(command.find("/dev/console"), std::string::npos);
  EXPECT_NE(command.find("'\\''"), std::string::npos);
  EXPECT_EQ(command.substr(command.size() - 5), "write");
}
