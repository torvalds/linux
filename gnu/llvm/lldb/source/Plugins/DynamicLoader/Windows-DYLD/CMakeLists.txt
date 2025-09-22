add_lldb_library(lldbPluginDynamicLoaderWindowsDYLD PLUGIN
  DynamicLoaderWindowsDYLD.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
  LINK_COMPONENTS
    Support
    TargetParser
  )
