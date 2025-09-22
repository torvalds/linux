//===-- CommandOptionArgumentTable.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_COMMANDOPTIONARGUMENTTABLE_H
#define LLDB_INTERPRETER_COMMANDOPTIONARGUMENTTABLE_H

#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

static constexpr OptionEnumValueElement g_corefile_save_style[] = {
    {lldb::eSaveCoreFull, "full", "Create a core file with all memory saved"},
    {lldb::eSaveCoreDirtyOnly, "modified-memory",
     "Create a corefile with only modified memory saved"},
    {lldb::eSaveCoreStackOnly, "stack",
     "Create a corefile with only stack  memory saved"},
};

static constexpr OptionEnumValueElement g_description_verbosity_type[] = {
    {
        eLanguageRuntimeDescriptionDisplayVerbosityCompact,
        "compact",
        "Only show the description string",
    },
    {
        eLanguageRuntimeDescriptionDisplayVerbosityFull,
        "full",
        "Show the full output, including persistent variable's name and type",
    },
};

static constexpr OptionEnumValueElement g_sort_option_enumeration[] = {
    {
        eSortOrderNone,
        "none",
        "No sorting, use the original symbol table order.",
    },
    {
        eSortOrderByAddress,
        "address",
        "Sort output by symbol address.",
    },
    {
        eSortOrderByName,
        "name",
        "Sort output by symbol name.",
    },
    {
        eSortOrderBySize,
        "size",
        "Sort output by symbol byte size.",
    },
};

// Note that the negation in the argument name causes a slightly confusing
// mapping of the enum values.
static constexpr OptionEnumValueElement g_dependents_enumeration[] = {
    {
        eLoadDependentsDefault,
        "default",
        "Only load dependents when the target is an executable.",
    },
    {
        eLoadDependentsNo,
        "true",
        "Don't load dependents, even if the target is an executable.",
    },
    {
        eLoadDependentsYes,
        "false",
        "Load dependents, even if the target is not an executable.",
    },
};

// FIXME: "script-type" needs to have its contents determined dynamically, so
// somebody can add a new scripting language to lldb and have it pickable here
// without having to change this enumeration by hand and rebuild lldb proper.
static constexpr OptionEnumValueElement g_script_option_enumeration[] = {
    {
        lldb::eScriptLanguageNone,
        "command",
        "Commands are in the lldb command interpreter language",
    },
    {
        lldb::eScriptLanguagePython,
        "python",
        "Commands are in the Python language.",
    },
    {
        lldb::eScriptLanguageLua,
        "lua",
        "Commands are in the Lua language.",
    },
    {
        lldb::eScriptLanguageNone,
        "default",
        "Commands are in the default scripting language.",
    },
};

static constexpr OptionEnumValueElement g_log_handler_type[] = {
    {
        eLogHandlerDefault,
        "default",
        "Use the default (stream) log handler",
    },
    {
        eLogHandlerStream,
        "stream",
        "Write log messages to the debugger output stream or to a file if one "
        "is specified. A buffer size (in bytes) can be specified with -b. If "
        "no buffer size is specified the output is unbuffered.",
    },
    {
        eLogHandlerCircular,
        "circular",
        "Write log messages to a fixed size circular buffer. A buffer size "
        "(number of messages) must be specified with -b.",
    },
    {
        eLogHandlerSystem,
        "os",
        "Write log messages to the operating system log.",
    },
};

static constexpr OptionEnumValueElement g_script_synchro_type[] = {
    {
        eScriptedCommandSynchronicitySynchronous,
        "synchronous",
        "Run synchronous",
    },
    {
        eScriptedCommandSynchronicityAsynchronous,
        "asynchronous",
        "Run asynchronous",
    },
    {
        eScriptedCommandSynchronicityCurrentValue,
        "current",
        "Do not alter current setting",
    },
};

static constexpr OptionEnumValueElement g_running_mode[] = {
    {lldb::eOnlyThisThread, "this-thread", "Run only this thread"},
    {lldb::eAllThreads, "all-threads", "Run all threads"},
    {lldb::eOnlyDuringStepping, "while-stepping",
     "Run only this thread while stepping"},
};

static constexpr OptionEnumValueElement g_completion_type[] = {
    {lldb::eNoCompletion, "none", "No completion."},
    {lldb::eSourceFileCompletion, "source-file", "Completes to a source file."},
    {lldb::eDiskFileCompletion, "disk-file", "Completes to a disk file."},
    {lldb::eDiskDirectoryCompletion, "disk-directory",
     "Completes to a disk directory."},
    {lldb::eSymbolCompletion, "symbol", "Completes to a symbol."},
    {lldb::eModuleCompletion, "module", "Completes to a module."},
    {lldb::eSettingsNameCompletion, "settings-name",
     "Completes to a settings name."},
    {lldb::ePlatformPluginCompletion, "platform-plugin",
     "Completes to a platform plugin."},
    {lldb::eArchitectureCompletion, "architecture",
     "Completes to a architecture."},
    {lldb::eVariablePathCompletion, "variable-path",
     "Completes to a variable path."},
    {lldb::eRegisterCompletion, "register", "Completes to a register."},
    {lldb::eBreakpointCompletion, "breakpoint", "Completes to a breakpoint."},
    {lldb::eProcessPluginCompletion, "process-plugin",
     "Completes to a process plugin."},
    {lldb::eDisassemblyFlavorCompletion, "disassembly-flavor",
     "Completes to a disassembly flavor."},
    {lldb::eTypeLanguageCompletion, "type-language",
     "Completes to a type language."},
    {lldb::eFrameIndexCompletion, "frame-index", "Completes to a frame index."},
    {lldb::eModuleUUIDCompletion, "module-uuid", "Completes to a module uuid."},
    {lldb::eStopHookIDCompletion, "stophook-id", "Completes to a stophook id."},
    {lldb::eThreadIndexCompletion, "thread-index",
     "Completes to a thread index."},
    {lldb::eWatchpointIDCompletion, "watchpoint-id",
     "Completes to a watchpoint id."},
    {lldb::eBreakpointNameCompletion, "breakpoint-name",
     "Completes to a breakpoint name."},
    {lldb::eProcessIDCompletion, "process-id", "Completes to a process id."},
    {lldb::eProcessNameCompletion, "process-name",
     "Completes to a process name."},
    {lldb::eRemoteDiskFileCompletion, "remote-disk-file",
     "Completes to a remote disk file."},
    {lldb::eRemoteDiskDirectoryCompletion, "remote-disk-directory",
     "Completes to a remote disk directory."},
    {lldb::eTypeCategoryNameCompletion, "type-category-name",
     "Completes to a type category name."},
    {lldb::eCustomCompletion, "custom", "Custom completion."},
    {lldb::eThreadIDCompletion, "thread-id", "Completes to a thread ID."},
};

llvm::StringRef RegisterNameHelpTextCallback();
llvm::StringRef BreakpointIDHelpTextCallback();
llvm::StringRef BreakpointIDRangeHelpTextCallback();
llvm::StringRef BreakpointNameHelpTextCallback();
llvm::StringRef GDBFormatHelpTextCallback();
llvm::StringRef FormatHelpTextCallback();
llvm::StringRef LanguageTypeHelpTextCallback();
llvm::StringRef SummaryStringHelpTextCallback();
llvm::StringRef ExprPathHelpTextCallback();
llvm::StringRef arch_helper();

static constexpr CommandObject::ArgumentTableEntry g_argument_table[] = {
    // clang-format off
    { lldb::eArgTypeAddress, "address", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A valid address in the target program's execution space." },
    { lldb::eArgTypeAddressOrExpression, "address-expression", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "An expression that resolves to an address." },
    { lldb::eArgTypeAliasName, "alias-name", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The name of an abbreviation (alias) for a debugger command." },
    { lldb::eArgTypeAliasOptions, "options-for-aliased-command", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Command options to be used as part of an alias (abbreviation) definition.  (See 'help commands alias' for more information.)" },
    { lldb::eArgTypeArchitecture, "arch", lldb::eArchitectureCompletion, {}, { arch_helper, true }, "The architecture name, e.g. i386 or x86_64." },
    { lldb::eArgTypeBoolean, "boolean", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A Boolean value: 'true' or 'false'" },
    { lldb::eArgTypeBreakpointID, "breakpt-id", lldb::CompletionType::eNoCompletion, {}, { BreakpointIDHelpTextCallback, false }, nullptr },
    { lldb::eArgTypeBreakpointIDRange, "breakpt-id-list", lldb::CompletionType::eNoCompletion, {}, { BreakpointIDRangeHelpTextCallback, false }, nullptr },
    { lldb::eArgTypeBreakpointName, "breakpoint-name", lldb::eBreakpointNameCompletion, {}, { BreakpointNameHelpTextCallback, false }, nullptr },
    { lldb::eArgTypeByteSize, "byte-size", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Number of bytes to use." },
    { lldb::eArgTypeClassName, "class-name", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Then name of a class from the debug information in the program." },
    { lldb::eArgTypeCommandName, "cmd-name", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A debugger command (may be multiple words), without any options or arguments." },
    { lldb::eArgTypeCount, "count", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "An unsigned integer." },
    { lldb::eArgTypeDescriptionVerbosity, "description-verbosity", lldb::CompletionType::eNoCompletion, g_description_verbosity_type, { nullptr, false }, "How verbose the output of 'po' should be." },
    { lldb::eArgTypeDirectoryName, "directory", lldb::eDiskDirectoryCompletion, {}, { nullptr, false }, "A directory name." },
    { lldb::eArgTypeDisassemblyFlavor, "disassembly-flavor", lldb::eDisassemblyFlavorCompletion, {}, { nullptr, false }, "A disassembly flavor recognized by your disassembly plugin.  Currently the only valid options are \"att\" and \"intel\" for Intel targets" },
    { lldb::eArgTypeEndAddress, "end-address", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Help text goes here." },
    { lldb::eArgTypeExpression, "expr", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Help text goes here." },
    { lldb::eArgTypeExpressionPath, "expr-path", lldb::CompletionType::eNoCompletion, {}, { ExprPathHelpTextCallback, true }, nullptr },
    { lldb::eArgTypeExprFormat, "expression-format", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "[ [bool|b] | [bin] | [char|c] | [oct|o] | [dec|i|d|u] | [hex|x] | [float|f] | [cstr|s] ]" },
    { lldb::eArgTypeFileLineColumn, "linespec", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A source specifier in the form file:line[:column]" },
    { lldb::eArgTypeFilename, "filename", lldb::eDiskFileCompletion, {}, { nullptr, false }, "The name of a file (can include path)." },
    { lldb::eArgTypeFormat, "format", lldb::CompletionType::eNoCompletion, {}, { FormatHelpTextCallback, true }, nullptr },
    { lldb::eArgTypeFrameIndex, "frame-index", lldb::eFrameIndexCompletion, {}, { nullptr, false }, "Index into a thread's list of frames." },
    { lldb::eArgTypeFullName, "fullname", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Help text goes here." },
    { lldb::eArgTypeFunctionName, "function-name", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The name of a function." },
    { lldb::eArgTypeFunctionOrSymbol, "function-or-symbol", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The name of a function or symbol." },
    { lldb::eArgTypeGDBFormat, "gdb-format", lldb::CompletionType::eNoCompletion, {}, { GDBFormatHelpTextCallback, true }, nullptr },
    { lldb::eArgTypeHelpText, "help-text", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Text to be used as help for some other entity in LLDB" },
    { lldb::eArgTypeIndex, "index", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "An index into a list." },
    { lldb::eArgTypeLanguage, "source-language", lldb::eTypeLanguageCompletion, {}, { LanguageTypeHelpTextCallback, true }, nullptr },
    { lldb::eArgTypeLineNum, "linenum", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Line number in a source file." },
    { lldb::eArgTypeLogCategory, "log-category", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The name of a category within a log channel, e.g. all (try \"log list\" to see a list of all channels and their categories." },
    { lldb::eArgTypeLogChannel, "log-channel", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The name of a log channel, e.g. process.gdb-remote (try \"log list\" to see a list of all channels and their categories)." },
    { lldb::eArgTypeMethod, "method", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A C++ method name." },
    { lldb::eArgTypeName, "name", lldb::eTypeCategoryNameCompletion, {}, { nullptr, false }, "The name of a type category." },
    { lldb::eArgTypeNewPathPrefix, "new-path-prefix", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Help text goes here." },
    { lldb::eArgTypeNumLines, "num-lines", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The number of lines to use." },
    { lldb::eArgTypeNumberPerLine, "number-per-line", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The number of items per line to display." },
    { lldb::eArgTypeOffset, "offset", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Help text goes here." },
    { lldb::eArgTypeOldPathPrefix, "old-path-prefix", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Help text goes here." },
    { lldb::eArgTypeOneLiner, "one-line-command", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A command that is entered as a single line of text." },
    { lldb::eArgTypePath, "path", lldb::eDiskFileCompletion, {}, { nullptr, false }, "Path." },
    { lldb::eArgTypePermissionsNumber, "perms-numeric", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Permissions given as an octal number (e.g. 755)." },
    { lldb::eArgTypePermissionsString, "perms=string", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Permissions given as a string value (e.g. rw-r-xr--)." },
    { lldb::eArgTypePid, "pid", lldb::eProcessIDCompletion, {}, { nullptr, false }, "The process ID number." },
    { lldb::eArgTypePlugin, "plugin", lldb::eProcessPluginCompletion, {}, { nullptr, false }, "Help text goes here." },
    { lldb::eArgTypeProcessName, "process-name", lldb::eProcessNameCompletion, {}, { nullptr, false }, "The name of the process." },
    { lldb::eArgTypePythonClass, "python-class", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The name of a Python class." },
    { lldb::eArgTypePythonFunction, "python-function", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The name of a Python function." },
    { lldb::eArgTypePythonScript, "python-script", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Source code written in Python." },
    { lldb::eArgTypeQueueName, "queue-name", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The name of the thread queue." },
    { lldb::eArgTypeRegisterName, "register-name", lldb::CompletionType::eRegisterCompletion, {}, { RegisterNameHelpTextCallback, true }, nullptr },
    { lldb::eArgTypeRegularExpression, "regular-expression", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A POSIX-compliant extended regular expression." },
    { lldb::eArgTypeRunArgs, "run-args", lldb::CompletionType::eDiskFileCompletion, {}, { nullptr, false }, "Arguments to be passed to the target program when it starts executing." },
    { lldb::eArgTypeRunMode, "run-mode", lldb::CompletionType::eNoCompletion, g_running_mode, { nullptr, false }, "Help text goes here." },
    { lldb::eArgTypeScriptedCommandSynchronicity, "script-cmd-synchronicity", lldb::CompletionType::eNoCompletion, g_script_synchro_type, { nullptr, false }, "The synchronicity to use to run scripted commands with regard to LLDB event system." },
    { lldb::eArgTypeScriptLang, "script-language", lldb::CompletionType::eNoCompletion, g_script_option_enumeration, { nullptr, false }, "The scripting language to be used for script-based commands.  Supported languages are python and lua." },
    { lldb::eArgTypeSearchWord, "search-word", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Any word of interest for search purposes." },
    { lldb::eArgTypeSelector, "selector", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "An Objective-C selector name." },
    { lldb::eArgTypeSettingIndex, "setting-index", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "An index into a settings variable that is an array (try 'settings list' to see all the possible settings variables and their types)." },
    { lldb::eArgTypeSettingKey, "setting-key", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A key into a settings variables that is a dictionary (try 'settings list' to see all the possible settings variables and their types)." },
    { lldb::eArgTypeSettingPrefix, "setting-prefix", lldb::CompletionType::eSettingsNameCompletion, {}, { nullptr, false }, "The name of a settable internal debugger variable up to a dot ('.'), e.g. 'target.process.'" },
    { lldb::eArgTypeSettingVariableName, "setting-variable-name", lldb::CompletionType::eSettingsNameCompletion, {}, { nullptr, false }, "The name of a settable internal debugger variable.  Type 'settings list' to see a complete list of such variables." },
    { lldb::eArgTypeShlibName, "shlib-name", lldb::CompletionType::eDiskFileCompletion, {}, { nullptr, false }, "The name of a shared library." },
    { lldb::eArgTypeSourceFile, "source-file", lldb::eSourceFileCompletion, {}, { nullptr, false }, "The name of a source file.." },
    { lldb::eArgTypeSortOrder, "sort-order", lldb::CompletionType::eNoCompletion, g_sort_option_enumeration, { nullptr, false }, "Specify a sort order when dumping lists." },
    { lldb::eArgTypeStartAddress, "start-address", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Help text goes here." },
    { lldb::eArgTypeSummaryString, "summary-string", lldb::CompletionType::eNoCompletion, {}, { SummaryStringHelpTextCallback, true }, nullptr },
    { lldb::eArgTypeSymbol, "symbol", lldb::eSymbolCompletion, {}, { nullptr, false }, "Any symbol name (function name, variable, argument, etc.)" },
    { lldb::eArgTypeThreadID, "thread-id", lldb::CompletionType::eThreadIndexCompletion, {}, { nullptr, false }, "Thread ID number." },
    { lldb::eArgTypeThreadIndex, "thread-index", lldb::CompletionType::eThreadIndexCompletion, {}, { nullptr, false }, "Index into the process' list of threads." },
    { lldb::eArgTypeThreadName, "thread-name", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The thread's name." },
    { lldb::eArgTypeTypeName, "type-name", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A type name." },
    { lldb::eArgTypeUnsignedInteger, "unsigned-integer", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "An unsigned integer." },
    { lldb::eArgTypeUnixSignal, "unix-signal", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A valid Unix signal name or number (e.g. SIGKILL, KILL or 9)." },
    { lldb::eArgTypeVarName, "variable-name", lldb::CompletionType::eVariablePathCompletion, {} ,{ nullptr, false }, "The name of a variable in your program." },
    { lldb::eArgTypeValue, "value", lldb::CompletionType::eNoCompletion, g_dependents_enumeration, { nullptr, false }, "A value could be anything, depending on where and how it is used." },
    { lldb::eArgTypeWidth, "width", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Help text goes here." },
    { lldb::eArgTypeNone, "none", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "No help available for this." },
    { lldb::eArgTypePlatform, "platform-name", lldb::ePlatformPluginCompletion, {}, { nullptr, false }, "The name of an installed platform plug-in . Type 'platform list' to see a complete list of installed platforms." },
    { lldb::eArgTypeWatchpointID, "watchpt-id", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Watchpoint IDs are positive integers." },
    { lldb::eArgTypeWatchpointIDRange, "watchpt-id-list", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "For example, '1-3' or '1 to 3'." },
    { lldb::eArgTypeWatchType, "watch-type", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Specify the type for a watchpoint." },
    { lldb::eArgRawInput, "raw-input", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Free-form text passed to a command without prior interpretation, allowing spaces without requiring quotes.  To pass arguments and free form text put two dashes ' -- ' between the last argument and any raw input." },
    { lldb::eArgTypeCommand, "command", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "An LLDB Command line command element." },
    { lldb::eArgTypeColumnNum, "column", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "Column number in a source file." },
    { lldb::eArgTypeModuleUUID, "module-uuid", lldb::eModuleUUIDCompletion, {}, { nullptr, false }, "A module UUID value." },
    { lldb::eArgTypeSaveCoreStyle, "corefile-style", lldb::CompletionType::eNoCompletion, g_corefile_save_style, { nullptr, false }, "The type of corefile that lldb will try to create, dependant on this target's capabilities." },
    { lldb::eArgTypeLogHandler, "log-handler", lldb::CompletionType::eNoCompletion, g_log_handler_type ,{ nullptr, false }, "The log handle that will be used to write out log messages." },
    { lldb::eArgTypeSEDStylePair, "substitution-pair", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A sed-style pattern and target pair." },
    { lldb::eArgTypeRecognizerID, "frame-recognizer-id", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The ID for a stack frame recognizer." },
    { lldb::eArgTypeConnectURL, "process-connect-url", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "A URL-style specification for a remote connection." },
    { lldb::eArgTypeTargetID, "target-id", lldb::CompletionType::eNoCompletion, {}, { nullptr, false }, "The index ID for an lldb Target." },
    { lldb::eArgTypeStopHookID, "stop-hook-id", lldb::CompletionType::eStopHookIDCompletion, {}, { nullptr, false }, "The ID you receive when you create a stop-hook." },
    { lldb::eArgTypeCompletionType, "completion-type", lldb::CompletionType::eNoCompletion, g_completion_type, { nullptr, false }, "The completion type to use when adding custom commands. If none is specified, the command won't use auto-completion." },
    { lldb::eArgTypeRemotePath, "remote-path", lldb::CompletionType::eRemoteDiskFileCompletion, {}, { nullptr, false }, "A path on the system managed by the current platform." },
    { lldb::eArgTypeRemoteFilename, "remote-filename", lldb::CompletionType::eRemoteDiskFileCompletion, {}, { nullptr, false }, "A file on the system managed by the current platform." },
    { lldb::eArgTypeModule, "module", lldb::CompletionType::eModuleCompletion, {}, { nullptr, false }, "The name of a module loaded into the current target." },
    // clang-format on
};

static_assert((sizeof(g_argument_table) /
               sizeof(CommandObject::ArgumentTableEntry)) ==
                  lldb::eArgTypeLastArg,
              "number of elements in g_argument_table doesn't match "
              "CommandArgumentType enumeration");

} // namespace lldb_private

#endif // LLDB_INTERPRETER_COMMANDOPTIONARGUMENTTABLE_H
