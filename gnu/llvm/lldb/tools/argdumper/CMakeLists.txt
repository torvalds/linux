add_lldb_tool(lldb-argdumper ADD_TO_FRAMEWORK
  argdumper.cpp

  LINK_COMPONENTS
    Support
)

add_dependencies(liblldb lldb-argdumper)
