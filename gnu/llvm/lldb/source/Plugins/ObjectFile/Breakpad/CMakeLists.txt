add_lldb_library(lldbPluginObjectFileBreakpad PLUGIN
  BreakpadRecords.cpp
  ObjectFileBreakpad.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbUtility
  LINK_COMPONENTS
    Support
    TargetParser
  )
