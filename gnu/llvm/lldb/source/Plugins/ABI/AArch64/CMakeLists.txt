add_lldb_library(lldbPluginABIAArch64 PLUGIN
  ABIAArch64.cpp
  ABIMacOSX_arm64.cpp
  ABISysV_arm64.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    Support
    TargetParser
  )
