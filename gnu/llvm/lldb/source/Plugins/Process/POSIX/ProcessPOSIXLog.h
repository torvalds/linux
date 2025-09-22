//===-- ProcessPOSIXLog.h -----------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ProcessPOSIXLog_h_
#define liblldb_ProcessPOSIXLog_h_

#include "lldb/Utility/Log.h"
#include "llvm/ADT/BitmaskEnum.h"

namespace lldb_private {

enum class POSIXLog : Log::MaskType {
  Breakpoints = Log::ChannelFlag<0>,
  Memory = Log::ChannelFlag<1>,
  Process = Log::ChannelFlag<2>,
  Ptrace = Log::ChannelFlag<3>,
  Registers = Log::ChannelFlag<4>,
  Thread = Log::ChannelFlag<5>,
  Watchpoints = Log::ChannelFlag<6>,
  Trace = Log::ChannelFlag<7>,
  LLVM_MARK_AS_BITMASK_ENUM(Trace)
};
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

class ProcessPOSIXLog {
public:
  static void Initialize();
};

template <> Log::Channel &LogChannelFor<POSIXLog>();
} // namespace lldb_private

#endif // liblldb_ProcessPOSIXLog_h_
