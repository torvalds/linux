add_lldb_library(lldbPluginObjectFileCOFF PLUGIN
  ObjectFileCOFF.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbTarget

  LINK_COMPONENTS
    BinaryFormat
    Object
    Support)
