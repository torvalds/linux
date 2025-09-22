add_lldb_library(lldbPluginInstrumentationRuntimeMainThreadChecker PLUGIN
  InstrumentationRuntimeMainThreadChecker.cpp

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
