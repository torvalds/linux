add_lldb_library(lldbPluginClangREPL PLUGIN
  ClangREPL.cpp

  LINK_LIBS
    lldbCore
    lldbDataFormatters
    lldbHost
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginClangCommon
    lldbPluginCPPRuntime
    lldbPluginTypeSystemClang

  LINK_COMPONENTS
    Support
)
