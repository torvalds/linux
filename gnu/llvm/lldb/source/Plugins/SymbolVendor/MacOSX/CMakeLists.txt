add_lldb_library(lldbPluginSymbolVendorMacOSX PLUGIN
  SymbolVendorMacOSX.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbPluginObjectFileMachO
  )
