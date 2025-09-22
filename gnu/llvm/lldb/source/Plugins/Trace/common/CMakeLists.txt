add_lldb_library(lldbPluginTraceCommon
  ThreadPostMortemTrace.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
  )
