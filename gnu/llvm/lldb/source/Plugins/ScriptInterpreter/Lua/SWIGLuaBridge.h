//===-- SWIGLuaBridge.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_LUA_SWIGLUABRIDGE_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_LUA_SWIGLUABRIDGE_H

#include "lldb/lldb-forward.h"
#include "lua.hpp"
#include "llvm/Support/Error.h"

namespace lldb_private {

namespace lua {

class SWIGBridge {
public:
  static llvm::Expected<bool> LLDBSwigLuaBreakpointCallbackFunction(
      lua_State *L, lldb::StackFrameSP stop_frame_sp,
      lldb::BreakpointLocationSP bp_loc_sp,
      const StructuredDataImpl &extra_args_impl);

  static llvm::Expected<bool> LLDBSwigLuaWatchpointCallbackFunction(
      lua_State *L, lldb::StackFrameSP stop_frame_sp, lldb::WatchpointSP wp_sp);
};

} // namespace lua

} // namespace lldb_private

#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_LUA_SWIGLUABRIDGE_H
