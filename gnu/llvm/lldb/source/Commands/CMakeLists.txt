lldb_tablegen(CommandOptions.inc -gen-lldb-option-defs
  SOURCE Options.td
  TARGET LLDBOptionsGen)

add_lldb_library(lldbCommands NO_PLUGIN_DEPENDENCIES
  CommandCompletions.cpp
  CommandObjectApropos.cpp
  CommandObjectBreakpoint.cpp
  CommandObjectBreakpointCommand.cpp
  CommandObjectCommands.cpp
  CommandObjectDiagnostics.cpp
  CommandObjectDisassemble.cpp
  CommandObjectDWIMPrint.cpp
  CommandObjectExpression.cpp
  CommandObjectFrame.cpp
  CommandObjectGUI.cpp
  CommandObjectHelp.cpp
  CommandObjectLanguage.cpp
  CommandObjectLog.cpp
  CommandObjectMemory.cpp
  CommandObjectMemoryTag.cpp
  CommandObjectMultiword.cpp
  CommandObjectPlatform.cpp
  CommandObjectPlugin.cpp
  CommandObjectProcess.cpp
  CommandObjectQuit.cpp
  CommandObjectRegexCommand.cpp
  CommandObjectRegister.cpp
  CommandObjectScripting.cpp
  CommandObjectSession.cpp
  CommandObjectSettings.cpp
  CommandObjectSource.cpp
  CommandObjectStats.cpp
  CommandObjectTarget.cpp
  CommandObjectThread.cpp
  CommandObjectThreadUtil.cpp
  CommandObjectTrace.cpp
  CommandObjectType.cpp
  CommandObjectVersion.cpp
  CommandObjectWatchpoint.cpp
  CommandObjectWatchpointCommand.cpp
  CommandOptionArgumentTable.cpp
  CommandOptionsProcessAttach.cpp
  CommandOptionsProcessLaunch.cpp

  LINK_LIBS
    lldbBreakpoint
    lldbCore
    lldbDataFormatters
    lldbExpression
    lldbHost
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbUtility
    lldbVersion

  LINK_COMPONENTS
    Support
  )

add_dependencies(lldbCommands LLDBOptionsGen)
