add_lldb_library(lldbPluginSymbolFileSymtab PLUGIN
  SymbolFileSymtab.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
  )
