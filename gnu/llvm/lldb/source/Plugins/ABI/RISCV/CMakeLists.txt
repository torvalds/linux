add_lldb_library(lldbPluginABIRISCV PLUGIN
  ABISysV_riscv.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbPluginProcessUtility
  LINK_COMPONENTS
    Support
    TargetParser
  )
