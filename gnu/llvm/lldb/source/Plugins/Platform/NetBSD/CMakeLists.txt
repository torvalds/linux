add_lldb_library(lldbPluginPlatformNetBSD PLUGIN
  PlatformNetBSD.cpp

   LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbHost
    lldbTarget
  )
