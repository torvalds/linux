add_lldb_library(lldbPluginInstrumentationRuntimeTSan PLUGIN
  InstrumentationRuntimeTSan.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbExpression
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbPluginProcessUtility
  )
