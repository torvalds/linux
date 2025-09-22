add_lldb_library(lldbPluginDynamicLoaderStatic PLUGIN
  DynamicLoaderStatic.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbTarget
    lldbUtility
  )
