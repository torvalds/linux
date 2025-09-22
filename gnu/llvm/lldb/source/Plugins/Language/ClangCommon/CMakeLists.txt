add_lldb_library(lldbPluginClangCommon
  ClangHighlighter.cpp

  LINK_LIBS
    lldbCore
    lldbUtility
  LINK_COMPONENTS
    Support
)
