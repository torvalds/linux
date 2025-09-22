add_lldb_library(lldbPluginObjectFileELF PLUGIN
  ELFHeader.cpp
  ObjectFileELF.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    BinaryFormat
    Object
    Support
  )
