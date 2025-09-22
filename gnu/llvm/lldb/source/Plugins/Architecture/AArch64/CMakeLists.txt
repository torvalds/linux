add_lldb_library(lldbPluginArchitectureAArch64 PLUGIN
  ArchitectureAArch64.cpp

  LINK_LIBS
    lldbPluginProcessUtility
    lldbCore
    lldbTarget
    lldbUtility
  LINK_COMPONENTS
    Support
  )
