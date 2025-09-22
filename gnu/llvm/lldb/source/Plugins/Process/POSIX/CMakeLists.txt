add_lldb_library(lldbPluginProcessPOSIX
  CrashReason.cpp
  NativeProcessELF.cpp
  ProcessPOSIXLog.cpp

  LINK_LIBS
    lldbPluginProcessUtility
    lldbUtility
  LINK_COMPONENTS
    Support
  )
