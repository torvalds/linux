add_lldb_library(lldbPluginDynamicLoaderHexagonDYLD PLUGIN
  HexagonDYLDRendezvous.cpp
  DynamicLoaderHexagonDYLD.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbSymbol
    lldbTarget
  )
