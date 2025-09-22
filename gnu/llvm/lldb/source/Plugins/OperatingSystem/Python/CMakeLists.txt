add_lldb_library(lldbPluginOperatingSystemPython PLUGIN
  OperatingSystemPython.cpp

  LINK_LIBS
    lldbCore
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbPluginProcessUtility
  )
