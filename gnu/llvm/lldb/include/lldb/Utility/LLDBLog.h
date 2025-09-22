//===-- LLDBLog.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_LLDBLOG_H
#define LLDB_UTILITY_LLDBLOG_H

#include "lldb/Utility/Log.h"
#include "llvm/ADT/BitmaskEnum.h"
#include <cstdint>

namespace lldb_private {

enum class LLDBLog : Log::MaskType {
  API = Log::ChannelFlag<0>,
  AST = Log::ChannelFlag<1>,
  Breakpoints = Log::ChannelFlag<2>,
  Commands = Log::ChannelFlag<3>,
  Communication = Log::ChannelFlag<4>,
  Connection = Log::ChannelFlag<5>,
  DataFormatters = Log::ChannelFlag<6>,
  Demangle = Log::ChannelFlag<7>,
  DynamicLoader = Log::ChannelFlag<8>,
  Events = Log::ChannelFlag<9>,
  Expressions = Log::ChannelFlag<10>,
  Host = Log::ChannelFlag<11>,
  JITLoader = Log::ChannelFlag<12>,
  Language = Log::ChannelFlag<13>,
  MMap = Log::ChannelFlag<14>,
  Modules = Log::ChannelFlag<15>,
  Object = Log::ChannelFlag<16>,
  OS = Log::ChannelFlag<17>,
  Platform = Log::ChannelFlag<18>,
  Process = Log::ChannelFlag<19>,
  Script = Log::ChannelFlag<20>,
  State = Log::ChannelFlag<21>,
  Step = Log::ChannelFlag<22>,
  Symbols = Log::ChannelFlag<23>,
  SystemRuntime = Log::ChannelFlag<24>,
  Target = Log::ChannelFlag<25>,
  Temporary = Log::ChannelFlag<26>,
  Thread = Log::ChannelFlag<27>,
  Types = Log::ChannelFlag<28>,
  Unwind = Log::ChannelFlag<29>,
  Watchpoints = Log::ChannelFlag<30>,
  OnDemand = Log::ChannelFlag<31>,
  Source = Log::ChannelFlag<32>,
  LLVM_MARK_AS_BITMASK_ENUM(OnDemand),
};

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

void InitializeLldbChannel();

template <> Log::Channel &LogChannelFor<LLDBLog>();
} // namespace lldb_private

#endif // LLDB_UTILITY_LLDBLOG_H
