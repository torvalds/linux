add_lldb_library(lldbPluginDynamicLoaderFreeBSDKernel PLUGIN
  DynamicLoaderFreeBSDKernel.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbHost
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginObjectFileELF
  )
