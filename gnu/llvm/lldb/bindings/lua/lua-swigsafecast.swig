template <typename SBClass> void PushSBClass(lua_State *L, SBClass *obj);

void PushSBClass(lua_State *L, lldb::SBFrame *frame_sb) {
  SWIG_NewPointerObj(L, frame_sb, SWIGTYPE_p_lldb__SBFrame, 0);
}

void PushSBClass(lua_State *L,
                 lldb::SBBreakpointLocation *breakpoint_location_sb) {
  SWIG_NewPointerObj(L, breakpoint_location_sb,
                     SWIGTYPE_p_lldb__SBBreakpointLocation, 0);
}

void PushSBClass(lua_State *L, lldb::SBWatchpoint *watchpoint_sb) {
  SWIG_NewPointerObj(L, watchpoint_sb, SWIGTYPE_p_lldb__SBWatchpoint, 0);
}

void PushSBClass(lua_State *L, lldb::SBStructuredData *structured_data_sb) {
  SWIG_NewPointerObj(L, structured_data_sb, SWIGTYPE_p_lldb__SBStructuredData,
                     0);
}
