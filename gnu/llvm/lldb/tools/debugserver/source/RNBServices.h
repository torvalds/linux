//===-- RNBServices.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Christopher Friesen on 3/21/08.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBSERVICES_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBSERVICES_H

#include "RNBDefs.h"
#include <string>

#define DTSERVICES_APP_FRONTMOST_KEY CFSTR("isFrontApp")
#define DTSERVICES_APP_PATH_KEY CFSTR("executablePath")
#define DTSERVICES_APP_ICON_PATH_KEY CFSTR("iconPath")
#define DTSERVICES_APP_DISPLAY_NAME_KEY CFSTR("displayName")
#define DTSERVICES_APP_PID_KEY CFSTR("pid")

int ListApplications(std::string &plist, bool opt_runningApps,
                     bool opt_debuggable);

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBSERVICES_H
