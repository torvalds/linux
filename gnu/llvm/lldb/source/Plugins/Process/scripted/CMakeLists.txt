add_lldb_library(lldbPluginScriptedProcess PLUGIN
  ScriptedProcess.cpp
  ScriptedThread.cpp

  LINK_LIBS
    lldbCore
    lldbTarget
    lldbUtility
    lldbPluginProcessUtility
  LINK_COMPONENTS
    BinaryFormat
    Object
    Support
  )
