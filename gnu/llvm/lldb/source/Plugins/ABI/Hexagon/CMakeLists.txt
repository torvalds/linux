add_lldb_library(lldbPluginABIHexagon PLUGIN
  ABISysV_hexagon.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    Support
    TargetParser
  )
