add_lldb_library(lldbPluginSymbolVendorWasm PLUGIN
  SymbolVendorWasm.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbPluginObjectFileWasm
  )
