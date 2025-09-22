
add_lldb_library(lldbPluginProcessWindowsCommon PLUGIN
  DebuggerThread.cpp
  LocalDebugDelegate.cpp
  NativeProcessWindows.cpp
  NativeRegisterContextWindows.cpp
  NativeRegisterContextWindows_arm.cpp
  NativeRegisterContextWindows_arm64.cpp
  NativeRegisterContextWindows_i386.cpp
  NativeRegisterContextWindows_WoW64.cpp
  NativeRegisterContextWindows_x86_64.cpp
  NativeThreadWindows.cpp
  ProcessDebugger.cpp
  ProcessWindows.cpp
  ProcessWindowsLog.cpp
  RegisterContextWindows.cpp
  TargetThreadWindows.cpp
  arm/RegisterContextWindows_arm.cpp
  arm64/RegisterContextWindows_arm64.cpp
  x64/RegisterContextWindows_x64.cpp
  x86/RegisterContextWindows_x86.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbInterpreter
    lldbPluginDynamicLoaderWindowsDYLD
    lldbSymbol
    lldbTarget
    ws2_32
    rpcrt4
    psapi
  LINK_COMPONENTS
    Support
  )
