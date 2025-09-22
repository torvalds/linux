add_lldb_library(lldbPluginInstrumentationRuntimeUtility
  ReportRetriever.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbExpression
    lldbSymbol
    lldbTarget
  )
