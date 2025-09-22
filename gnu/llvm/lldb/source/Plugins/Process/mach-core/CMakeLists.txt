add_lldb_library(lldbPluginProcessMachCore PLUGIN
  ProcessMachCore.cpp
  ThreadMachCore.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbHost
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginDynamicLoaderDarwinKernel
    lldbPluginDynamicLoaderMacOSXDYLD
    lldbPluginObjectFileMachO
  LINK_COMPONENTS
    Support
  )
