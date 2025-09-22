//===-- SysSignal.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/18/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_SYSSIGNAL_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_SYSSIGNAL_H

class SysSignal {
public:
  static const char *Name(int signal);
};

#endif
