add_lldb_library(lldbExpression NO_PLUGIN_DEPENDENCIES
  DiagnosticManager.cpp
  DWARFExpression.cpp
  DWARFExpressionList.cpp
  Expression.cpp
  ExpressionParser.cpp
  ExpressionTypeSystemHelper.cpp
  ExpressionVariable.cpp
  FunctionCaller.cpp
  IRExecutionUnit.cpp
  IRInterpreter.cpp
  IRMemoryMap.cpp
  LLVMUserExpression.cpp
  Materializer.cpp
  ObjectFileJIT.cpp
  REPL.cpp
  UserExpression.cpp
  UtilityFunction.cpp

  DEPENDS
  intrinsics_gen

  LINK_LIBS
    lldbCore
    lldbHost
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbUtility

  LINK_COMPONENTS
    Core
    ExecutionEngine
    Support
  )
