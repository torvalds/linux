add_lldb_library(lldbPluginABISystemZ PLUGIN
  ABISysV_s390x.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    Support
    TargetParser
  )
