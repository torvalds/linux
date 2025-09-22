add_lldb_library(lldbPluginMemoryHistoryASan PLUGIN
  MemoryHistoryASan.cpp

  LINK_LIBS
    lldbCore
    lldbExpression
    lldbTarget
    lldbPluginProcessUtility
  )
