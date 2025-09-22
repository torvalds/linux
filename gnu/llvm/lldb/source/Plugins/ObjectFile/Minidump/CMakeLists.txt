add_lldb_library(lldbPluginObjectFileMinidump PLUGIN
  ObjectFileMinidump.cpp
  MinidumpFileBuilder.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginProcessUtility
  LINK_COMPONENTS
    Support
  )
