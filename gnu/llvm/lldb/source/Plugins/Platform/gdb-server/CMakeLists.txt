add_lldb_library(lldbPluginPlatformGDB PLUGIN
  PlatformRemoteGDBServer.cpp

   LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbHost
    lldbTarget
    lldbPluginProcessUtility
    lldbPluginProcessGDBRemote
  )
