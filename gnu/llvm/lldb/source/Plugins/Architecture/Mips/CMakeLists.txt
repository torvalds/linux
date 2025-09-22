add_lldb_library(lldbPluginArchitectureMips PLUGIN
  ArchitectureMips.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
    lldbUtility
  LINK_COMPONENTS
    Support
  )
