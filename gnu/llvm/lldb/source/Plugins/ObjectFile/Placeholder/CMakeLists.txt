add_lldb_library(lldbPluginObjectFilePlaceholder PLUGIN
  ObjectFilePlaceholder.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    Object
    Support
  )
