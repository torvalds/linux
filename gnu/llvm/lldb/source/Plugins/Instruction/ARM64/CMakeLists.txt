add_lldb_library(lldbPluginInstructionARM64 PLUGIN
  EmulateInstructionARM64.cpp

  LINK_LIBS
    lldbCore
    lldbInterpreter
    lldbSymbol
    lldbPluginProcessUtility
  LINK_COMPONENTS
    Support
  )
