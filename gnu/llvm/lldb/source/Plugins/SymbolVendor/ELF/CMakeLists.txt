add_lldb_library(lldbPluginSymbolVendorELF PLUGIN
  SymbolVendorELF.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbPluginObjectFileELF
  )
