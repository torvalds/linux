add_lldb_library(lldbPluginDynamicLoaderWasmDYLD PLUGIN
  DynamicLoaderWasmDYLD.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
  LINK_COMPONENTS
    Support
  )
