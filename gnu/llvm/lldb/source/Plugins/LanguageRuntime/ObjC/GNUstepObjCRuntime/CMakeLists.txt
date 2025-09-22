add_lldb_library(lldbPluginGNUstepObjCRuntime PLUGIN
  GNUstepObjCRuntime.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbExpression
    lldbHost
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginExpressionParserClang
    lldbPluginTypeSystemClang
  CLANG_LIBS
    clangAST
  LINK_COMPONENTS
    Support
  )
