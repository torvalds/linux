add_lldb_library(lldbPluginPlatformPOSIX
  PlatformPOSIX.cpp

   LINK_LIBS
    lldbCore
    lldbExpression
    lldbHost
    lldbInterpreter
    lldbTarget
    lldbPluginPlatformGDB
    lldbPluginTypeSystemClang
  )
