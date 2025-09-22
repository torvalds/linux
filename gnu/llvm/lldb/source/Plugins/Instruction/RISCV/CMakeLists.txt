add_lldb_library(lldbPluginInstructionRISCV PLUGIN
  EmulateInstructionRISCV.cpp

  LINK_LIBS
    lldbCore
    lldbInterpreter
    lldbSymbol
    lldbPluginProcessUtility
  LINK_COMPONENTS
    Support
  )
