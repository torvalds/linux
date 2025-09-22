add_lldb_library(lldbPluginScriptInterpreterNone PLUGIN
  ScriptInterpreterNone.cpp

  LINK_LIBS
    lldbCore
    lldbInterpreter
  )
