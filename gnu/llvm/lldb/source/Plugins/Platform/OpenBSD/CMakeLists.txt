add_lldb_library(lldbPluginPlatformOpenBSD PLUGIN
  PlatformOpenBSD.cpp

   LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbHost
    lldbTarget
  )
