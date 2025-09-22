add_lldb_library(lldbPluginSystemRuntimeMacOSX PLUGIN
  AppleGetItemInfoHandler.cpp
  AppleGetPendingItemsHandler.cpp
  AppleGetQueuesHandler.cpp
  AppleGetThreadItemInfoHandler.cpp
  SystemRuntimeMacOSX.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbExpression
    lldbHost
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbPluginProcessUtility
    lldbPluginTypeSystemClang
  )
