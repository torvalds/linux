add_lldb_library(lldbPluginObjectContainerMachOArchive PLUGIN
  ObjectContainerUniversalMachO.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbTarget
    lldbUtility
  )
