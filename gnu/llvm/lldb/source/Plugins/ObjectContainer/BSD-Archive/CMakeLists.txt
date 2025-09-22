add_lldb_library(lldbPluginObjectContainerBSDArchive PLUGIN
  ObjectContainerBSDArchive.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
  LINK_COMPONENTS
    Support
  )
