add_lldb_library(lldbPluginObjectFileWasm PLUGIN
  ObjectFileWasm.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbUtility
  LINK_COMPONENTS
    Support
  )
