add_lldb_library(lldbPluginInstructionPPC64 PLUGIN
  EmulateInstructionPPC64.cpp

  LINK_LIBS
    lldbCore
    lldbInterpreter
    lldbSymbol
    lldbPluginProcessUtility
  LINK_COMPONENTS
    Support
  )
