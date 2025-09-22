%header %{

template <typename T> void PushSBClass(lua_State * L, T * obj);

// This function is called from Lua::CallBreakpointCallback
llvm::Expected<bool>
lldb_private::lua::SWIGBridge::LLDBSwigLuaBreakpointCallbackFunction(
    lua_State * L, lldb::StackFrameSP stop_frame_sp,
    lldb::BreakpointLocationSP bp_loc_sp,
    const StructuredDataImpl &extra_args_impl) {
  lldb::SBFrame sb_frame(stop_frame_sp);
  lldb::SBBreakpointLocation sb_bp_loc(bp_loc_sp);
  int nargs = 2;

  lldb::SBStructuredData extra_args(extra_args_impl);

  // Push the Lua wrappers
  PushSBClass(L, &sb_frame);
  PushSBClass(L, &sb_bp_loc);

  if (extra_args.IsValid()) {
    PushSBClass(L, &extra_args);
    nargs++;
  }

  // Call into the Lua callback passing 'sb_frame' and 'sb_bp_loc'.
  // Expects a boolean return.
  if (lua_pcall(L, nargs, 1, 0) != LUA_OK) {
    llvm::Error E = llvm::make_error<llvm::StringError>(
        llvm::formatv("{0}\n", lua_tostring(L, -1)),
        llvm::inconvertibleErrorCode());
    // Pop error message from the stack.
    lua_pop(L, 1);
    return std::move(E);
  }

  // Boolean return from the callback
  bool stop = lua_toboolean(L, -1);
  lua_pop(L, 1);

  return stop;
}

// This function is called from Lua::CallWatchpointCallback
llvm::Expected<bool>
lldb_private::lua::SWIGBridge::LLDBSwigLuaWatchpointCallbackFunction(
    lua_State * L, lldb::StackFrameSP stop_frame_sp, lldb::WatchpointSP wp_sp) {
  lldb::SBFrame sb_frame(stop_frame_sp);
  lldb::SBWatchpoint sb_wp(wp_sp);
  int nargs = 2;

  // Push the Lua wrappers
  PushSBClass(L, &sb_frame);
  PushSBClass(L, &sb_wp);

  // Call into the Lua callback passing 'sb_frame' and 'sb_wp'.
  // Expects a boolean return.
  if (lua_pcall(L, nargs, 1, 0) != LUA_OK) {
    llvm::Error E = llvm::make_error<llvm::StringError>(
        llvm::formatv("{0}\n", lua_tostring(L, -1)),
        llvm::inconvertibleErrorCode());
    // Pop error message from the stack.
    lua_pop(L, 1);
    return std::move(E);
  }

  // Boolean return from the callback
  bool stop = lua_toboolean(L, -1);
  lua_pop(L, 1);

  return stop;
}

static void LLDBSwigLuaCallLuaLogOutputCallback(const char *str, void *baton) {
  lua_State *L = (lua_State *)baton;

  lua_pushlightuserdata(L, (void *)&LLDBSwigLuaCallLuaLogOutputCallback);
  lua_gettable(L, LUA_REGISTRYINDEX);

  // FIXME: There's no way to report errors back to the user
  lua_pushstring(L, str);
  lua_pcall(L, 1, 0, 0);
}

static int LLDBSwigLuaCloseFileHandle(lua_State * L) {
  return luaL_error(L, "You cannot close a file handle used by lldb.");
}

%}
