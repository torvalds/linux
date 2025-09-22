
# Create an LC_SEGMENT with the special name ExecExtraSuspend which
# debugserver can detect - it tells debugserver that it will exec a
# process and that process will start suspended, so debugserver will
# need to double-resume it to make it run.  A random file is copied
# into the segment.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-sectcreate,ExecExtraSuspend,ExecExtraSuspend,/dev/null")

add_lldb_tool(darwin-debug ADD_TO_FRAMEWORK
  darwin-debug.cpp
)
