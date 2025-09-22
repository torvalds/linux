add_lldb_library(lldbPluginObjectFilePDB PLUGIN
  ObjectFilePDB.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbUtility
  LINK_COMPONENTS
    Support
  )
