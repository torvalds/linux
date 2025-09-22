add_lldb_library(lldbPluginInstructionARM PLUGIN
  EmulateInstructionARM.cpp
  EmulationStateARM.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbPluginProcessUtility
  LINK_COMPONENTS
    Support
  )
