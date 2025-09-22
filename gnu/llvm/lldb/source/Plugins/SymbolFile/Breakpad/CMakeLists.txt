add_lldb_library(lldbPluginSymbolFileBreakpad PLUGIN
  SymbolFileBreakpad.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbUtility
    lldbPluginObjectFileBreakpad
  LINK_COMPONENTS
    Support
  )
