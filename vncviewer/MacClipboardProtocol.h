// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNCVIEWER_MAC_CLIPBOARD_PROTOCOL_H
#define VNCVIEWER_MAC_CLIPBOARD_PROTOCOL_H

#include <cstddef>
#include <string>

namespace macclipboard {
const size_t maxText = 1024 * 1024;
struct Snapshot {
  std::string revision;
  bool text;
  std::string value;
};
Snapshot parse(const std::string& data);
std::string remoteCommand(bool write);
const char* helperScript();

// Tracks remote revisions, including our own writes, to prevent echo loops.
class Revisions {
public:
  bool observe(const Snapshot& snapshot);
  void reset() { revision.clear(); }
private:
  std::string revision;
};
}
#endif
