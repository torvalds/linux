add_lldb_library(lldbPluginRegisterTypeBuilderClang PLUGIN
  RegisterTypeBuilderClang.cpp

  LINK_LIBS
    lldbCore
    lldbTarget

  LINK_COMPONENTS
    Support
  )
