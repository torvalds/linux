//===-- LogChannelDWARF.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_LOGCHANNELDWARF_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_LOGCHANNELDWARF_H

#include "lldb/Utility/Log.h"
#include "llvm/ADT/BitmaskEnum.h"

namespace lldb_private {

enum class DWARFLog : Log::MaskType {
  DebugInfo = Log::ChannelFlag<0>,
  DebugLine = Log::ChannelFlag<1>,
  DebugMap = Log::ChannelFlag<2>,
  Lookups = Log::ChannelFlag<3>,
  TypeCompletion = Log::ChannelFlag<4>,
  SplitDwarf = Log::ChannelFlag<5>,
  LLVM_MARK_AS_BITMASK_ENUM(TypeCompletion)
};
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

class LogChannelDWARF {
public:
  static void Initialize();
  static void Terminate();
};

template <> Log::Channel &LogChannelFor<DWARFLog>();
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_LOGCHANNELDWARF_H
