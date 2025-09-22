add_lldb_library(lldbPluginInstrumentationRuntimeASan PLUGIN
  InstrumentationRuntimeASan.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbSymbol
    lldbTarget
    lldbPluginInstrumentationRuntimeUtility
  )
