add_lldb_library(lldbPluginABIMips PLUGIN
  ABIMips.cpp
  ABISysV_mips.cpp
  ABISysV_mips64.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    Support
    TargetParser
  )
