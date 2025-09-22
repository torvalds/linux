add_lldb_library(lldbPluginPlatformLinux PLUGIN
  PlatformLinux.cpp

   LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbHost
    lldbInterpreter
    lldbTarget
    lldbPluginPlatformPOSIX
  )
