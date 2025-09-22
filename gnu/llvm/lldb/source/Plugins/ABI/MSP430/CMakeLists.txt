add_lldb_library(lldbPluginABIMSP430 PLUGIN
  ABISysV_msp430.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    Support
    TargetParser
  )

