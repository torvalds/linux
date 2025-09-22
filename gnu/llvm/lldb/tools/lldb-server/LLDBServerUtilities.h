#ifndef LLDB_TOOLS_LLDB_SERVER_LLDBSERVERUTILITIES_H

#define LLDB_TOOLS_LLDB_SERVER_LLDBSERVERUTILITIES_H

//===-- LLDBServerUtilities.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"

#include <string>

namespace lldb_private {
namespace lldb_server {

class LLDBServerUtilities {
public:
  static bool SetupLogging(const std::string &log_file,
                           const llvm::StringRef &log_channels,
                           uint32_t log_options);
};
}
}

#endif
