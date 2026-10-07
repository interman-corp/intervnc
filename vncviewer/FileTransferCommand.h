// Copyright 2026. Distributed under the GNU GPL, version 2 or later.
#ifndef VNCVIEWER_FILE_TRANSFER_COMMAND_H
#define VNCVIEWER_FILE_TRANSFER_COMMAND_H

#include <string>

namespace filetransfer {
// Inputs are separate fields, never a URL or a command supplied by the server.
std::string sessionUrl(std::string host, const std::string& port,
                       const std::string& user);
std::wstring quoteArgument(const std::wstring& argument);
}

#endif
