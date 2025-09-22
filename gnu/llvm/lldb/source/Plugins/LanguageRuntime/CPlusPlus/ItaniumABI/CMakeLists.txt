add_lldb_library(lldbPluginCXXItaniumABI PLUGIN
  ItaniumABILanguageRuntime.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbPluginCPPRuntime
    lldbPluginTypeSystemClang
  )
