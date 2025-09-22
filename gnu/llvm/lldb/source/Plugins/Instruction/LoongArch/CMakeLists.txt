add_lldb_library(lldbPluginInstructionLoongArch PLUGIN
  EmulateInstructionLoongArch.cpp

  LINK_LIBS
    lldbCore
    lldbInterpreter
    lldbPluginProcessUtility
    lldbSymbol
  LINK_COMPONENTS
    Support
  )
