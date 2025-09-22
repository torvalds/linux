add_lldb_library(lldbPluginObjectContainerMachOFileset PLUGIN
  ObjectContainerMachOFileset.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbTarget
    lldbUtility
  )
