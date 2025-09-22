add_lldb_library(lldbPluginArchitectureArm PLUGIN
  ArchitectureArm.cpp

  LINK_LIBS
    lldbPluginProcessUtility
    lldbCore
    lldbTarget
    lldbUtility
  LINK_COMPONENTS
    Support
  )
