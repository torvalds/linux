//===-- TraceIntelPTConstants.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_CONSTANTS_H
#define LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_CONSTANTS_H

#include "lldb/lldb-types.h"
#include <cstddef>
#include <optional>

namespace lldb_private {
namespace trace_intel_pt {

const size_t kDefaultIptTraceSize = 4 * 1024;                  // 4KB
const size_t kDefaultProcessBufferSizeLimit = 5 * 1024 * 1024; // 500MB
const bool kDefaultEnableTscValue = false;
const std::optional<size_t> kDefaultPsbPeriod;
const bool kDefaultPerCpuTracing = false;
const bool kDefaultDisableCgroupFiltering = false;

// Physical address where the kernel is loaded in x86 architecture. Refer to
// https://github.com/torvalds/linux/blob/master/Documentation/x86/x86_64/mm.rst
// for the start address of kernel text section.
// The kernel entry point is 0x1000000 by default when KASLR is disabled.
const lldb::addr_t kDefaultKernelLoadAddress = 0xffffffff81000000;
const lldb::pid_t kDefaultKernelProcessID = 1;

} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_CONSTANTS_H
