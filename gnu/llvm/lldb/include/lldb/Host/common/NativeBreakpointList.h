//===-- NativeBreakpointList.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_COMMON_NATIVEBREAKPOINTLIST_H
#define LLDB_HOST_COMMON_NATIVEBREAKPOINTLIST_H

#include "lldb/lldb-private-forward.h"
#include "lldb/lldb-types.h"
#include <map>

namespace lldb_private {

struct HardwareBreakpoint {
  lldb::addr_t m_addr;
  size_t m_size;
};

using HardwareBreakpointMap = std::map<lldb::addr_t, HardwareBreakpoint>;
}

#endif // LLDB_HOST_COMMON_NATIVEBREAKPOINTLIST_H
