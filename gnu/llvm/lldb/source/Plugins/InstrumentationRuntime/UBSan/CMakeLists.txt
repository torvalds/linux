add_lldb_library(lldbPluginInstrumentationRuntimeUBSan PLUGIN
  InstrumentationRuntimeUBSan.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbExpression
    lldbInterpreter
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    Support
  )
