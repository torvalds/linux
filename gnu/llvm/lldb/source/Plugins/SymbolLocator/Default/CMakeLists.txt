add_lldb_library(lldbPluginSymbolLocatorDefault PLUGIN
  SymbolLocatorDefault.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
  )
