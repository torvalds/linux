add_lldb_library(lldbPluginObjectFileMachO PLUGIN
  ObjectFileMachO.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginProcessUtility
  LINK_COMPONENTS
    Support
  )
