//===-- cli-wrapper.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// CLI Wrapper for hardware features of Intel(R) architecture based processors
// to enable them to be used through LLDB's CLI. For details, please refer to
// cli wrappers of each individual feature, residing in their respective
// folders.
//
// Compile this into a shared lib and load by placing at appropriate locations
// on disk or by using "plugin load" command at the LLDB command line.
//
//===----------------------------------------------------------------------===//

#ifdef BUILD_INTEL_MPX
#include "intel-mpx/cli-wrapper-mpxtable.h"
#endif

#include "lldb/API/SBDebugger.h"

namespace lldb {
bool PluginInitialize(lldb::SBDebugger debugger);
}

bool lldb::PluginInitialize(lldb::SBDebugger debugger) {

#ifdef BUILD_INTEL_MPX
  MPXPluginInitialize(debugger);
#endif

  return true;
}
