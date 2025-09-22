add_lldb_library(lldbPluginArchitecturePPC64 PLUGIN
  ArchitecturePPC64.cpp

  LINK_LIBS
    lldbPluginProcessUtility
    lldbCore
    lldbTarget
    lldbUtility
  LINK_COMPONENTS
    Support
  )
