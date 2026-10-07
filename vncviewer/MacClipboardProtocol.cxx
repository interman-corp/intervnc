// SPDX-License-Identifier: GPL-2.0-or-later
#include "MacClipboardProtocol.h"

#include <stdexcept>
#include <core/string.h>

const char* macclipboard::helperScript()
{
  // Explicit plain-text pasteboard access avoids pbcopy's RTF/EPS detection
  // and pbpaste's fallback to non-text formats. No persistent Mac install.
  return R"JXA(ObjC.import('AppKit');
ObjC.import('Foundation');
function run(args) {
  var p = $.NSPasteboard.generalPasteboard;
  var type = $.NSPasteboardTypeString;
  if (args[0] === 'write') {
    var input = $.NSFileHandle.fileHandleWithStandardInput.readDataToEndOfFile;
    if (input.length > 1048576) throw Error('Clipboard exceeds 1 MiB');
    var str = $.NSString.alloc.initWithDataEncoding(input, $.NSUTF8StringEncoding);
    if (str.isNil()) throw Error('Invalid UTF-8');
    p.clearContents;
    if (!p.setStringForType(str, type)) throw Error('Cannot write Mac clipboard');
  }
  for (var attempt = 0; attempt < 3; ++attempt) {
    var revision = p.changeCount;
    var types = p.types;
    var files = !types.isNil() && (types.containsObject($.NSPasteboardTypeFileURL) ||
                                   types.containsObject($('NSFilenamesPboardType')));
    var text = p.stringForType(type);
    var present = !files && !text.isNil();
    var value = present ? ObjC.unwrap(text) : '';
    if (p.changeCount !== revision) continue;
    var data = $(value).dataUsingEncoding($.NSUTF8StringEncoding);
    if (data.length > 1048576) throw Error('Clipboard exceeds 1 MiB');
    var header = $('IVNC1 ' + revision + ' ' + (present ? 'T' : 'N') + '\n');
    var out = $.NSFileHandle.fileHandleWithStandardOutput;
    out.writeData(header.dataUsingEncoding($.NSUTF8StringEncoding));
    out.writeData(data);
    return;
  }
  throw Error('Mac clipboard changed during read; reconnect to retry');
}
)JXA";
}

std::string macclipboard::remoteCommand(bool write)
{
  std::string quoted = "'";
  for (char c : std::string(helperScript()))
    quoted += c == '\'' ? "'\\''" : std::string(1, c);
  quoted += "'";
  // Do not read a different logged-in user's pasteboard or require sudo.
  return "test \"$(/usr/bin/id -u)\" = \"$(/usr/bin/stat -f %u /dev/console)\" "
         "|| { echo 'SSH user must be the active Mac desktop user' >&2; exit 1; }; "
         "exec /usr/bin/osascript -l JavaScript -e " + quoted +
         (write ? " write" : " read");
}

macclipboard::Snapshot macclipboard::parse(const std::string& data)
{
  size_t end = data.find('\n');
  if (end == std::string::npos || end > 40 || data.compare(0, 6, "IVNC1 "))
    throw std::runtime_error("Invalid Mac clipboard response");
  size_t space = data.find(' ', 6);
  if (space == std::string::npos || space == 6 || space + 2 != end ||
      (data[space + 1] != 'T' && data[space + 1] != 'N'))
    throw std::runtime_error("Invalid Mac clipboard header");
  std::string revision = data.substr(6, space - 6);
  for (char c : revision) {
    if (c < '0' || c > '9')
      throw std::runtime_error("Invalid Mac clipboard revision");
  }
  std::string value = data.substr(end + 1);
  bool present = data[space + 1] == 'T';
  if (value.size() > maxText || (!present && !value.empty()) ||
      value.find('\0') != std::string::npos || !core::isValidUTF8(value.c_str(), value.size()))
    throw std::runtime_error("Invalid or oversized Mac clipboard text");
  return {revision, present, core::convertLF(value.c_str(), value.size())};
}

bool macclipboard::Revisions::observe(const Snapshot& snapshot)
{
  bool changed = !revision.empty() && revision != snapshot.revision;
  revision = snapshot.revision;
  return changed && snapshot.text;
}
