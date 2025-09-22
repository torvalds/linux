//===-- Lua.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Lua.h"
#include "SWIGLuaBridge.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/FileSpec.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"

using namespace lldb_private;
using namespace lldb;

static int lldb_print(lua_State *L) {
  int n = lua_gettop(L);
  lua_getglobal(L, "io");
  lua_getfield(L, -1, "stdout");
  lua_getfield(L, -1, "write");
  for (int i = 1; i <= n; i++) {
    lua_pushvalue(L, -1); // write()
    lua_pushvalue(L, -3); // io.stdout
    luaL_tolstring(L, i, nullptr);
    lua_pushstring(L, i != n ? "\t" : "\n");
    lua_call(L, 3, 0);
  }
  return 0;
}

Lua::Lua() : m_lua_state(luaL_newstate()) {
  assert(m_lua_state);
  luaL_openlibs(m_lua_state);
  luaopen_lldb(m_lua_state);
  lua_pushcfunction(m_lua_state, lldb_print);
  lua_setglobal(m_lua_state, "print");
}

Lua::~Lua() {
  assert(m_lua_state);
  lua_close(m_lua_state);
}

llvm::Error Lua::Run(llvm::StringRef buffer) {
  int error =
      luaL_loadbuffer(m_lua_state, buffer.data(), buffer.size(), "buffer") ||
      lua_pcall(m_lua_state, 0, 0, 0);
  if (error == LUA_OK)
    return llvm::Error::success();

  llvm::Error e = llvm::make_error<llvm::StringError>(
      llvm::formatv("{0}\n", lua_tostring(m_lua_state, -1)),
      llvm::inconvertibleErrorCode());
  // Pop error message from the stack.
  lua_pop(m_lua_state, 1);
  return e;
}

llvm::Error Lua::RegisterBreakpointCallback(void *baton, const char *body) {
  lua_pushlightuserdata(m_lua_state, baton);
  const char *fmt_str = "return function(frame, bp_loc, ...) {0} end";
  std::string func_str = llvm::formatv(fmt_str, body).str();
  if (luaL_dostring(m_lua_state, func_str.c_str()) != LUA_OK) {
    llvm::Error e = llvm::make_error<llvm::StringError>(
        llvm::formatv("{0}", lua_tostring(m_lua_state, -1)),
        llvm::inconvertibleErrorCode());
    // Pop error message from the stack.
    lua_pop(m_lua_state, 2);
    return e;
  }
  lua_settable(m_lua_state, LUA_REGISTRYINDEX);
  return llvm::Error::success();
}

llvm::Expected<bool>
Lua::CallBreakpointCallback(void *baton, lldb::StackFrameSP stop_frame_sp,
                            lldb::BreakpointLocationSP bp_loc_sp,
                            StructuredData::ObjectSP extra_args_sp) {

  lua_pushlightuserdata(m_lua_state, baton);
  lua_gettable(m_lua_state, LUA_REGISTRYINDEX);
  StructuredDataImpl extra_args_impl(std::move(extra_args_sp));
  return lua::SWIGBridge::LLDBSwigLuaBreakpointCallbackFunction(
      m_lua_state, stop_frame_sp, bp_loc_sp, extra_args_impl);
}

llvm::Error Lua::RegisterWatchpointCallback(void *baton, const char *body) {
  lua_pushlightuserdata(m_lua_state, baton);
  const char *fmt_str = "return function(frame, wp, ...) {0} end";
  std::string func_str = llvm::formatv(fmt_str, body).str();
  if (luaL_dostring(m_lua_state, func_str.c_str()) != LUA_OK) {
    llvm::Error e = llvm::make_error<llvm::StringError>(
        llvm::formatv("{0}", lua_tostring(m_lua_state, -1)),
        llvm::inconvertibleErrorCode());
    // Pop error message from the stack.
    lua_pop(m_lua_state, 2);
    return e;
  }
  lua_settable(m_lua_state, LUA_REGISTRYINDEX);
  return llvm::Error::success();
}

llvm::Expected<bool>
Lua::CallWatchpointCallback(void *baton, lldb::StackFrameSP stop_frame_sp,
                            lldb::WatchpointSP wp_sp) {

  lua_pushlightuserdata(m_lua_state, baton);
  lua_gettable(m_lua_state, LUA_REGISTRYINDEX);
  return lua::SWIGBridge::LLDBSwigLuaWatchpointCallbackFunction(
      m_lua_state, stop_frame_sp, wp_sp);
}

llvm::Error Lua::CheckSyntax(llvm::StringRef buffer) {
  int error =
      luaL_loadbuffer(m_lua_state, buffer.data(), buffer.size(), "buffer");
  if (error == LUA_OK) {
    // Pop buffer
    lua_pop(m_lua_state, 1);
    return llvm::Error::success();
  }

  llvm::Error e = llvm::make_error<llvm::StringError>(
      llvm::formatv("{0}\n", lua_tostring(m_lua_state, -1)),
      llvm::inconvertibleErrorCode());
  // Pop error message from the stack.
  lua_pop(m_lua_state, 1);
  return e;
}

llvm::Error Lua::LoadModule(llvm::StringRef filename) {
  const FileSpec file(filename);
  if (!FileSystem::Instance().Exists(file)) {
    return llvm::make_error<llvm::StringError>("invalid path",
                                               llvm::inconvertibleErrorCode());
  }

  if (file.GetFileNameExtension() != ".lua") {
    return llvm::make_error<llvm::StringError>("invalid extension",
                                               llvm::inconvertibleErrorCode());
  }

  int error = luaL_loadfile(m_lua_state, filename.data()) ||
              lua_pcall(m_lua_state, 0, 1, 0);
  if (error != LUA_OK) {
    llvm::Error e = llvm::make_error<llvm::StringError>(
        llvm::formatv("{0}\n", lua_tostring(m_lua_state, -1)),
        llvm::inconvertibleErrorCode());
    // Pop error message from the stack.
    lua_pop(m_lua_state, 1);
    return e;
  }

  ConstString module_name = file.GetFileNameStrippingExtension();
  lua_setglobal(m_lua_state, module_name.GetCString());
  return llvm::Error::success();
}

llvm::Error Lua::ChangeIO(FILE *out, FILE *err) {
  assert(out != nullptr);
  assert(err != nullptr);

  lua_getglobal(m_lua_state, "io");

  lua_getfield(m_lua_state, -1, "stdout");
  if (luaL_Stream *s = static_cast<luaL_Stream *>(
          luaL_testudata(m_lua_state, -1, LUA_FILEHANDLE))) {
    s->f = out;
    lua_pop(m_lua_state, 1);
  } else {
    lua_pop(m_lua_state, 2);
    return llvm::make_error<llvm::StringError>("could not get stdout",
                                               llvm::inconvertibleErrorCode());
  }

  lua_getfield(m_lua_state, -1, "stderr");
  if (luaL_Stream *s = static_cast<luaL_Stream *>(
          luaL_testudata(m_lua_state, -1, LUA_FILEHANDLE))) {
    s->f = out;
    lua_pop(m_lua_state, 1);
  } else {
    lua_pop(m_lua_state, 2);
    return llvm::make_error<llvm::StringError>("could not get stderr",
                                               llvm::inconvertibleErrorCode());
  }

  lua_pop(m_lua_state, 1);
  return llvm::Error::success();
}
