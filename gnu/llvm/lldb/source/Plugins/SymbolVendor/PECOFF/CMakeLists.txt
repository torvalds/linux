add_lldb_library(lldbPluginSymbolVendorPECOFF PLUGIN
  SymbolVendorPECOFF.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbPluginObjectFilePECOFF
  )
