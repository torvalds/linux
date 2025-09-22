add_lldb_library(lldbPluginSymbolLocatorDebugSymbols PLUGIN
  SymbolLocatorDebugSymbols.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
  )
