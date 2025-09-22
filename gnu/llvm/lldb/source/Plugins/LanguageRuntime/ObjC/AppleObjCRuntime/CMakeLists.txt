add_lldb_library(lldbPluginAppleObjCRuntime PLUGIN
  AppleObjCRuntime.cpp
  AppleObjCRuntimeV1.cpp
  AppleObjCRuntimeV2.cpp
  AppleObjCTrampolineHandler.cpp
  AppleObjCDeclVendor.cpp
  AppleThreadPlanStepThroughObjCTrampoline.cpp
  AppleObjCClassDescriptorV2.cpp
  AppleObjCTypeEncodingParser.cpp

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
    lldbPluginCPPRuntime
    lldbPluginTypeSystemClang
  CLANG_LIBS
    clangAST
  LINK_COMPONENTS
    Support
  )
