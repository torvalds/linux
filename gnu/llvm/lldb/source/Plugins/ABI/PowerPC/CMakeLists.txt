add_lldb_library(lldbPluginABIPowerPC PLUGIN
  ABIPowerPC.cpp
  ABISysV_ppc.cpp
  ABISysV_ppc64.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbPluginTypeSystemClang
  LINK_COMPONENTS
    Support
    TargetParser
  )
