add_lldb_library(lldbPluginObjectFileJSON PLUGIN
  ObjectFileJSON.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbUtility
  LINK_COMPONENTS
    Support
    TargetParser
  )
