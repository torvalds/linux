add_lldb_library(lldbPluginSymbolFileJSON PLUGIN
  SymbolFileJSON.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
  )
