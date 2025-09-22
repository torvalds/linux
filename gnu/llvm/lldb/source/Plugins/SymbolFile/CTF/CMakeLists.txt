add_lldb_library(lldbPluginSymbolFileCTF PLUGIN
  SymbolFileCTF.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
  )
