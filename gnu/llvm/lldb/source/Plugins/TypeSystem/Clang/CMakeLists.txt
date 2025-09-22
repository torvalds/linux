add_lldb_library(lldbPluginTypeSystemClang PLUGIN
  TypeSystemClang.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginExpressionParserClang
    lldbPluginSymbolFileDWARF
    lldbPluginSymbolFilePDB
    lldbPluginObjCRuntime
  CLANG_LIBS
    clangAST
    clangBasic
    clangFrontend
    clangSema
  LINK_COMPONENTS
    Support
)
