add_lldb_library(lldbPluginPlatformWindows PLUGIN
  PlatformWindows.cpp

   LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbHost
    lldbTarget
    lldbPluginPlatformGDB

   LINK_COMPONENTS
    Support
  )
