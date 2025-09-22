//===-- OsLogger.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_OSLOGGER_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_OSLOGGER_H

#include "DNBDefs.h"

class OsLogger {
public:
  static DNBCallbackLog GetLogFunction();
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_OSLOGGER_H
