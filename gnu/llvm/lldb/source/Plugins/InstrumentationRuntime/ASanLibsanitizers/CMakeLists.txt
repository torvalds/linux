add_lldb_library(lldbPluginInstrumentationRuntimeASanLibsanitizers PLUGIN
  InstrumentationRuntimeASanLibsanitizers.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbSymbol
    lldbTarget
    lldbPluginInstrumentationRuntimeUtility
  )
