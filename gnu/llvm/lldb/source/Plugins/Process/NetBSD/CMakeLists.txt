add_lldb_library(lldbPluginProcessNetBSD
  NativeProcessNetBSD.cpp
  NativeRegisterContextNetBSD.cpp
  NativeRegisterContextNetBSD_x86_64.cpp
  NativeThreadNetBSD.cpp

  LINK_LIBS
    lldbHost
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginProcessPOSIX
    lldbPluginProcessUtility
    util
  LINK_COMPONENTS
    Support
  )
