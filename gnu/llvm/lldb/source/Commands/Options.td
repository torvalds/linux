include "OptionsBase.td"

let Command = "target modules dump symtab" in {
  def tm_sort : Option<"sort", "s">, Group<1>,
    Desc<"Supply a sort order when dumping the symbol table.">,
    EnumArg<"SortOrder">;
  def tm_smn : Option<"show-mangled-names", "m">, Group<1>,
    Desc<"Do not demangle symbol names before showing them.">;
}

let Command = "target modules dump separate debug info" in {
  def tm_json : Option<"json", "j">, Group<1>,
    Desc<"Output the details in JSON format.">;
  def tm_errors_only : Option<"errors-only", "e">, Group<1>,
    Desc<"Filter to show only debug info files with errors.">;
}

let Command = "help" in {
  def help_hide_aliases : Option<"hide-aliases", "a">,
    Desc<"Hide aliases in the command list.">;
  def help_hide_user : Option<"hide-user-commands", "u">,
    Desc<"Hide user-defined commands from the list.">;
  def help_show_hidden : Option<"show-hidden-commands", "h">,
    Desc<"Include commands prefixed with an underscore.">;
}

let Command = "settings set" in {
  def setset_global : Option<"global", "g">,
    Desc<"Apply the new value to the global default value.">;
  def setset_force : Option<"force", "f">,
    Desc<"Force an empty value to be accepted as the default.">;
  def setset_exists : Option<"exists", "e">,
    Desc<"Set the setting if it exists, but do not cause the command to raise "
    "an error if it does not exist.">;
}

let Command = "settings write" in {
  def setwrite_file : Option<"file", "f">, Required, Arg<"Filename">,
    Completion<"DiskFile">,
    Desc<"The file into which to write the settings.">;
  def setwrite_append : Option<"append", "a">,
    Desc<"Append to saved settings file if it exists.">;
}

let Command = "settings read" in {
  def setread_file : Option<"file", "f">, Required, Arg<"Filename">,
    Completion<"DiskFile">,
    Desc<"The file from which to read the settings.">;
}

let Command = "settings clear" in {
  def setclear_all : Option<"all", "a">,
    Desc<"Clear all settings.">;
}

let Command = "breakpoint list" in {
  // FIXME: We need to add an "internal" command, and then add this sort of
  // thing to it. But I need to see it for now, and don't want to wait.
  def blist_internal : Option<"internal", "i">,
    Desc<"Show debugger internal breakpoints">;
  def blist_brief : Option<"brief", "b">, Group<1>,
    Desc<"Give a brief description of the breakpoint (no location info).">;
  def blist_full : Option<"full", "f">, Group<2>,
    Desc<"Give a full description of the breakpoint and its locations.">;
  def blist_verbose : Option<"verbose", "v">, Group<3>,
    Desc<"Explain everything we know about the breakpoint (for debugging "
    "debugger bugs).">;
  def blist_dummy_bp : Option<"dummy-breakpoints", "D">,
    Desc<"List Dummy breakpoints - i.e. breakpoints set before a file is "
    "provided, which prime new targets.">;
}

let Command = "breakpoint modify" in {
  def breakpoint_modify_ignore_count : Option<"ignore-count", "i">, Group<1>,
    Arg<"Count">,
    Desc<"Set the number of times this breakpoint is skipped before stopping.">;
  def breakpoint_modify_one_shot : Option<"one-shot", "o">, Group<1>,
    Arg<"Boolean">,
    Desc<"The breakpoint is deleted the first time it stop causes a stop.">;
  def breakpoint_modify_thread_index : Option<"thread-index", "x">, Group<1>,
    Arg<"ThreadIndex">, Desc<"The breakpoint stops only for the thread whose "
    "index matches this argument.">;
  def breakpoint_modify_thread_id : Option<"thread-id", "t">, Group<1>,
    Arg<"ThreadID">, Desc<"The breakpoint stops only for the thread whose TID "
    "matches this argument.  The token 'current' resolves to the current thread's ID.">;
  def breakpoint_modify_thread_name : Option<"thread-name", "T">, Group<1>,
    Arg<"ThreadName">, Desc<"The breakpoint stops only for the thread whose "
    "thread name matches this argument.">;
  def breakpoint_modify_queue_name : Option<"queue-name", "q">, Group<1>,
    Arg<"QueueName">, Desc<"The breakpoint stops only for threads in the queue "
    "whose name is given by this argument.">;
  def breakpoint_modify_condition : Option<"condition", "c">, Group<1>,
    Arg<"Expression">, Desc<"The breakpoint stops only if this condition "
    "expression evaluates to true.">;
  def breakpoint_modify_auto_continue : Option<"auto-continue", "G">, Group<1>,
    Arg<"Boolean">,
    Desc<"The breakpoint will auto-continue after running its commands.">;
  def breakpoint_modify_enable : Option<"enable", "e">, Group<2>,
    Desc<"Enable the breakpoint.">;
  def breakpoint_modify_disable : Option<"disable", "d">, Group<3>,
    Desc<"Disable the breakpoint.">;
  def breakpoint_modify_command : Option<"command", "C">, Group<4>,
    Arg<"Command">,
    Desc<"A command to run when the breakpoint is hit, can be provided more "
    "than once, the commands will be run in left-to-right order.">;
}

let Command = "breakpoint dummy" in {
  def breakpoint_dummy_options_dummy_breakpoints :
    Option<"dummy-breakpoints", "D">, Group<1>,
    Desc<"Act on Dummy breakpoints - i.e. breakpoints set before a file is "
    "provided, which prime new targets.">;
}

let Command = "breakpoint set" in {
  def breakpoint_set_shlib : Option<"shlib", "s">, Arg<"ShlibName">,
    Completion<"Module">, Groups<[1,2,3,4,5,6,7,8,9,11,12]>, // *not* in group 10
    Desc<"Set the breakpoint only in this shared library.  Can repeat this "
    "option multiple times to specify multiple shared libraries.">;
  def breakpoint_set_hardware : Option<"hardware", "H">,
    Desc<"Require the breakpoint to use hardware breakpoints.">;
  def breakpoint_set_file : Option<"file", "f">, Arg<"Filename">,
    Completion<"SourceFile">, Groups<[1,3,4,5,6,7,8,9,11]>,
    Desc<"Specifies the source file in which to set this breakpoint.  Note, by "
    "default lldb only looks for files that are #included if they use the "
    "standard include file extensions.  To set breakpoints on .c/.cpp/.m/.mm "
    "files that are #included, set target.inline-breakpoint-strategy to "
    "\"always\".">;
  def breakpoint_set_line : Option<"line", "l">, Group<1>, Arg<"LineNum">,
    Required,
    Desc<"Specifies the line number on which to set this breakpoint.">;
  def breakpoint_set_column : Option<"column", "u">, Group<1>, Arg<"ColumnNum">,
    Desc<"Specifies the column number on which to set this breakpoint.">;
  def breakpoint_set_address : Option<"address", "a">, Group<2>,
    Arg<"AddressOrExpression">, Required,
    Desc<"Set the breakpoint at the specified address.  If the address maps "
    "uniquely to a particular binary, then the address will be converted to "
    "a \"file\"address, so that the breakpoint will track that binary+offset "
    "no matter where the binary eventually loads.  Alternately, if you also "
    "specify the module - with the -s option - then the address will be "
    "treated as a file address in that module, and resolved accordingly.  "
    "Again, this will allow lldb to track that offset on subsequent reloads.  "
    "The module need not have been loaded at the time you specify this "
    "breakpoint, and will get resolved when the module is loaded.">;
  def breakpoint_set_name : Option<"name", "n">, Group<3>, Arg<"FunctionName">,
    Completion<"Symbol">, Required,
    Desc<"Set the breakpoint by function name.  Can be repeated multiple times "
    "to make one breakpoint for multiple names.">;
  def breakpoint_set_source_regexp_function :
    Option<"source-regexp-function", "X">, Group<9>, Arg<"FunctionName">,
    Completion<"Symbol">,
    Desc<"When used with '-p' limits the source regex to source contained in "
    "the named functions.  Can be repeated multiple times.">;
  def breakpoint_set_fullname : Option<"fullname", "F">, Group<4>,
    Arg<"FullName">, Required, Completion<"Symbol">,
    Desc<"Set the breakpoint by fully qualified function names. For C++ this "
    "means namespaces and all arguments, and for Objective-C this means a full "
    "function prototype with class and selector.  Can be repeated multiple times"
    " to make one breakpoint for multiple names.">;
  def breakpoint_set_selector : Option<"selector", "S">, Group<5>,
    Arg<"Selector">, Required,
    Desc<"Set the breakpoint by Objective-C selector name.  Can be repeated "
    "multiple times to make one breakpoint for multiple Selectors.">;
  def breakpoint_set_method : Option<"method", "M">, Group<6>, Arg<"Method">,
    Required, Desc<"Set the breakpoint by C++ method names.  Can be repeated "
    "multiple times to make one breakpoint for multiple methods.">;
  def breakpoint_set_func_regex : Option<"func-regex", "r">, Group<7>,
    Arg<"RegularExpression">, Required, Desc<"Set the breakpoint by function "
    "name, evaluating a regular-expression to find the function name(s).">;
  def breakpoint_set_basename : Option<"basename", "b">, Group<8>,
    Arg<"FunctionName">, Required, Completion<"Symbol">,
    Desc<"Set the breakpoint by function basename (C++ namespaces and arguments"
    " will be ignored).  Can be repeated multiple times to make one breakpoint "
    "for multiple symbols.">;
  def breakpoint_set_source_pattern_regexp :
    Option<"source-pattern-regexp", "p">, Group<9>, Arg<"RegularExpression">,
    Required, Desc<"Set the breakpoint by specifying a regular expression which"
    " is matched against the source text in a source file or files specified "
    "with the -f can be specified more than once.  If no source files "
    "are specified, uses the current \"default source file\".  If you want to "
    "match against all source files, pass the \"--all-files\" option.">;
  def breakpoint_set_all_files : Option<"all-files", "A">, Group<9>,
    Desc<"All files are searched for source pattern matches.">;
  def breakpoint_set_language_exception : Option<"language-exception", "E">,
    Group<10>, Arg<"Language">, Required,
    Desc<"Set the breakpoint on exceptions thrown by the specified language "
    "(without options, on throw but not catch.)">;
  def breakpoint_set_on_throw : Option<"on-throw", "w">, Group<10>,
    Arg<"Boolean">, Desc<"Set the breakpoint on exception throW.">;
  def breakpoint_set_on_catch : Option<"on-catch", "h">, Group<10>,
    Arg<"Boolean">, Desc<"Set the breakpoint on exception catcH.">;
  def breakpoint_set_language : Option<"language", "L">, GroupRange<3, 8>,
    Arg<"Language">,
    Desc<"Specifies the Language to use when interpreting the breakpoint's "
    "expression (note: currently only implemented for setting breakpoints on "
    "identifiers). If not set the target.language setting is used.">;
  def breakpoint_set_skip_prologue : Option<"skip-prologue", "K">,
    Arg<"Boolean">, Groups<[1,3,4,5,6,7,8,12]>,
    Desc<"sKip the prologue if the breakpoint is at the beginning of a "
    "function. If not set the target.skip-prologue setting is used.">;
  def breakpoint_set_breakpoint_name : Option<"breakpoint-name", "N">,
    Arg<"BreakpointName">,
    Desc<"Adds this to the list of names for this breakpoint.">;
  def breakpoint_set_address_slide : Option<"address-slide", "R">,
    Arg<"Address">, Groups<[1,3,4,5,6,7,8,12]>,
    Desc<"Add the specified offset to whatever address(es) the breakpoint "
    "resolves to. At present this applies the offset directly as given, and "
    "doesn't try to align it to instruction boundaries.">;
  def breakpoint_set_move_to_nearest_code : Option<"move-to-nearest-code", "m">,
    Groups<[1,9,12]>, Arg<"Boolean">,
    Desc<"Move breakpoints to nearest code. If not set the "
    "target.move-to-nearest-code setting is used.">;
  def breakpoint_set_file_colon_line : Option<"joint-specifier", "y">, Group<12>, Arg<"FileLineColumn">,
    Required, Completion<"SourceFile">,
    Desc<"A specifier in the form filename:line[:column] for setting file & line breakpoints.">;
  /* Don't add this option till it actually does something useful...
  def breakpoint_set_exception_typename : Option<"exception-typename", "O">,
    Arg<"TypeName">, Desc<"The breakpoint will only stop if an "
    "exception Object of this type is thrown.  Can be repeated multiple times "
    "to stop for multiple object types">;
   */
}

let Command = "breakpoint clear" in {
  def breakpoint_clear_file : Option<"file", "f">, Group<1>, Arg<"Filename">,
    Completion<"SourceFile">,
    Desc<"Specify the breakpoint by source location in this particular file.">;
  def breakpoint_clear_line : Option<"line", "l">, Group<1>, Arg<"LineNum">,
    Required,
    Desc<"Specify the breakpoint by source location at this particular line.">;
}

let Command = "breakpoint delete" in {
  def breakpoint_delete_force : Option<"force", "f">, Group<1>,
    Desc<"Delete all breakpoints without querying for confirmation.">;
  def breakpoint_delete_dummy_breakpoints : Option<"dummy-breakpoints", "D">,
    Group<1>, Desc<"Delete Dummy breakpoints - i.e. breakpoints set before a "
    "file is provided, which prime new targets.">;
  def breakpoint_delete_disabled : Option<"disabled", "d">, Group<1>,
    Desc<"Delete all breakpoints which are currently disabled.  When using the disabled option "
    "any breakpoints listed on the command line are EXCLUDED from deletion.">;
}

let Command = "breakpoint name" in {
  def breakpoint_name_name : Option<"name", "N">, Group<1>,
    Arg<"BreakpointName">, Desc<"Specifies a breakpoint name to use.">;
  def breakpoint_name_breakpoint_id : Option<"breakpoint-id", "B">, Group<2>,
    Arg<"BreakpointID">, Desc<"Specify a breakpoint ID to use.">;
  def breakpoint_name_dummy_breakpoints : Option<"dummy-breakpoints", "D">,
    Group<3>, Desc<"Operate on Dummy breakpoints - i.e. breakpoints set before "
    "a file is provided, which prime new targets.">;
  def breakpoint_name_help_string : Option<"help-string", "H">, Group<4>,
    Arg<"None">, Desc<"A help string describing the purpose of this name.">;
}

let Command = "breakpoint access" in {
  def breakpoint_access_allow_list : Option<"allow-list", "L">, Group<1>,
    Arg<"Boolean">, Desc<"Determines whether the breakpoint will show up in "
    "break list if not referred to explicitly.">;
  def breakpoint_access_allow_disable : Option<"allow-disable", "A">, Group<2>,
    Arg<"Boolean">, Desc<"Determines whether the breakpoint can be disabled by "
    "name or when all breakpoints are disabled.">;
  def breakpoint_access_allow_delete : Option<"allow-delete", "D">, Group<3>,
    Arg<"Boolean">, Desc<"Determines whether the breakpoint can be deleted by "
    "name or when all breakpoints are deleted.">;
}

let Command = "breakpoint read" in {
  def breakpoint_read_file : Option<"file", "f">, Arg<"Filename">, Required,
    Completion<"DiskFile">,
    Desc<"The file from which to read the breakpoints.">;
  def breakpoint_read_breakpoint_name : Option<"breakpoint-name", "N">,
    Arg<"BreakpointName">, Desc<"Only read in breakpoints with this name.">;
}

let Command = "breakpoint write" in {
  def breakpoint_write_file : Option<"file", "f">, Arg<"Filename">, Required,
    Completion<"DiskFile">,
    Desc<"The file into which to write the breakpoints.">;
  def breakpoint_write_append : Option<"append", "a">,
    Desc<"Append to saved breakpoints file if it exists.">;
}

let Command = "breakpoint command add" in {
  def breakpoint_add_one_liner : Option<"one-liner", "o">, Group<1>,
    Arg<"OneLiner">, Desc<"Specify a one-line breakpoint command inline. Be "
    "sure to surround it with quotes.">;
  def breakpoint_add_stop_on_error : Option<"stop-on-error", "e">,
    Arg<"Boolean">, Desc<"Specify whether breakpoint command execution should "
    "terminate on error.">;
  def breakpoint_add_script_type : Option<"script-type", "s">,
    EnumArg<"ScriptLang">,
    Desc<"Specify the language for the commands - if none is specified, the "
    "lldb command interpreter will be used.">;
  def breakpoint_add_dummy_breakpoints : Option<"dummy-breakpoints", "D">,
    Desc<"Sets Dummy breakpoints - i.e. breakpoints set before a file is "
    "provided, which prime new targets.">;
}

let Command = "breakpoint command delete" in {
  def breakpoint_command_delete_dummy_breakpoints :
    Option<"dummy-breakpoints", "D">, Group<1>,
    Desc<"Delete commands from Dummy breakpoints - i.e. breakpoints set before "
    "a file is provided, which prime new targets.">;
}

let Command = "disassemble" in {
  def disassemble_options_bytes : Option<"bytes", "b">,
    Desc<"Show opcode bytes when disassembling.">;
  def disassemble_options_kind : Option<"kind", "k">,
    Desc<"Show instruction control flow kind. Refer to the enum "
    "`InstructionControlFlowKind` for a list of control flow kind. "
    "As an important note, far jumps, far calls and far returns often indicate "
    "calls to and from kernel.">;
  def disassemble_options_context : Option<"context", "C">, Arg<"NumLines">,
    Desc<"Number of context lines of source to show.">;
  def disassemble_options_mixed : Option<"mixed", "m">,
    Desc<"Enable mixed source and assembly display.">;
  def disassemble_options_raw : Option<"raw", "r">,
    Desc<"Print raw disassembly with no symbol information.">;
  def disassemble_options_plugin : Option<"plugin", "P">, Arg<"Plugin">,
    Desc<"Name of the disassembler plugin you want to use.">;
  def disassemble_options_flavor : Option<"flavor", "F">,
    Arg<"DisassemblyFlavor">, Desc<"Name of the disassembly flavor you want to "
    "use. Currently the only valid options are default, and for Intel "
    "architectures, att and intel.">;
  def disassemble_options_arch : Option<"arch", "A">, Arg<"Architecture">,
    Desc<"Specify the architecture to use from cross disassembly.">;
  def disassemble_options_start_address : Option<"start-address", "s">,
    Groups<[1,2]>, Arg<"AddressOrExpression">, Required,
    Desc<"Address at which to start disassembling.">;
  def disassemble_options_end_address : Option<"end-address", "e">, Group<1>,
    Arg<"AddressOrExpression">, Desc<"Address at which to end disassembling.">;
  def disassemble_options_count : Option<"count", "c">, Groups<[2,3,4,5,7]>,
    Arg<"NumLines">, Desc<"Number of instructions to display.">;
  def disassemble_options_name : Option<"name", "n">, Group<3>,
    Arg<"FunctionName">, Completion<"Symbol">,
    Desc<"Disassemble entire contents of the given function name.">;
  def disassemble_options_frame : Option<"frame", "f">, Group<4>,
    Desc<"Disassemble from the start of the current frame's function.">;
  def disassemble_options_pc : Option<"pc", "p">, Group<5>,
    Desc<"Disassemble around the current pc.">;
  def disassemble_options_line : Option<"line", "l">, Group<6>,
    Desc<"Disassemble the current frame's current source line instructions if "
    "there is debug line table information, else disassemble around the pc.">;
  def disassemble_options_address : Option<"address", "a">, Group<7>,
    Arg<"AddressOrExpression">,
    Desc<"Disassemble function containing this address.">;
  def disassemble_options_force : Option<"force", "\\x01">, Groups<[2,3,4,5,7]>,
    Desc<"Force disassembly of large functions.">;
}

let Command = "diagnostics dump" in {
  def diagnostics_dump_directory : Option<"directory", "d">, Group<1>,
    Arg<"Path">, Desc<"Dump the diagnostics to the given directory.">;
}

let Command = "expression" in {
  def expression_options_all_threads : Option<"all-threads", "a">,
    Groups<[1,2]>, Arg<"Boolean">, Desc<"Should we run all threads if the "
    "execution doesn't complete on one thread.">;
  def expression_options_ignore_breakpoints : Option<"ignore-breakpoints", "i">,
    Groups<[1,2]>, Arg<"Boolean">,
    Desc<"Ignore breakpoint hits while running expressions">;
  def expression_options_timeout : Option<"timeout", "t">, Groups<[1,2]>,
    Arg<"UnsignedInteger">,
    Desc<"Timeout value (in microseconds) for running the expression.">;
  def expression_options_unwind_on_error : Option<"unwind-on-error", "u">,
    Groups<[1,2]>, Arg<"Boolean">,
    Desc<"Clean up program state if the expression causes a crash, or raises a "
    "signal. Note, unlike gdb hitting a breakpoint is controlled by another "
    "option (-i).">;
  def expression_options_debug : Option<"debug", "g">, Groups<[1,2]>,
    Desc<"When specified, debug the JIT code by setting a breakpoint on the "
    "first instruction and forcing breakpoints to not be ignored (-i0) and no "
    "unwinding to happen on error (-u0).">;
  def expression_options_language : Option<"language", "l">, Groups<[1,2,3]>,
    Arg<"Language">, Desc<"Specifies the Language to use when parsing the "
    "expression.  If not set the target.language setting is used.">;
  def expression_options_apply_fixits : Option<"apply-fixits", "X">,
    Groups<[1,2]>, Arg<"Boolean">, Desc<"If true, simple fix-it hints will be "
    "automatically applied to the expression.">;
  def expression_options_description_verbosity :
    Option<"description-verbosity", "v">, Group<1>,
    OptionalEnumArg<"DescriptionVerbosity">,
    Desc<"How verbose should the output of this expression be, if the object "
    "description is asked for.">;
  def expression_options_top_level : Option<"top-level", "p">, Groups<[1,2]>,
    Desc<"Interpret the expression as a complete translation unit, without "
    "injecting it into the local context.  Allows declaration of persistent, "
    "top-level entities without a $ prefix.">;
  def expression_options_allow_jit : Option<"allow-jit", "j">, Groups<[1,2]>,
    Arg<"Boolean">,
    Desc<"Controls whether the expression can fall back to being JITted if it's "
    "not supported by the interpreter (defaults to true).">;
  def persistent_result : Option<"persistent-result", "\\x01">, Groups<[1,2]>,
    Arg<"Boolean">,
    Desc<"Persist expression result in a variable for subsequent use. "
    "Expression results will be labeled with $-prefixed variables, e.g. $0, "
    "$1, etc.">;
}

let Command = "frame diag" in {
  def frame_diag_register : Option<"register", "r">, Group<1>,
    Arg<"RegisterName">, Desc<"A register to diagnose.">;
  def frame_diag_address : Option<"address", "a">, Group<1>, Arg<"Address">,
    Desc<"An address to diagnose.">;
  def frame_diag_offset : Option<"offset", "o">, Group<1>, Arg<"Offset">,
    Desc<"An optional offset.  Requires --register.">;
}

let Command = "frame select" in {
  def frame_select_relative : Option<"relative", "r">, Group<1>, Arg<"Offset">,
    Desc<"A relative frame index offset from the current frame index.">;
}

let Command = "frame recognizer add" in {
  def frame_recognizer_shlib : Option<"shlib", "s">, Arg<"ShlibName">,
    Completion<"Module">,
    Desc<"Name of the module or shared library that this recognizer applies "
    "to.">;
  def frame_recognizer_function : Option<"function", "n">, Arg<"Name">,
    Completion<"Symbol">,
    Desc<"Name of the function that this recognizer applies to. "
         "Can be specified more than once except if -x|--regex is provided.">;
  def frame_recognizer_python_class : Option<"python-class", "l">, Group<2>,
    Arg<"PythonClass">,
    Desc<"Give the name of a Python class to use for this frame recognizer.">;
  def frame_recognizer_regex : Option<"regex", "x">,
    Desc<"Function name and module name are actually regular expressions.">;
  def frame_recognizer_first_instruction_only : Option<"first-instruction-only", "f">, Arg<"Boolean">,
    Desc<"If true, only apply this recognizer to frames whose PC currently points to the "
    "first instruction of the specified function. If false, the recognizer "
    "will always be applied, regardless of the current position within the specified function. The "
    "implementer should keep in mind that some features, e.g. accessing function argument "
    "values via $arg<N>, are not guaranteed to work reliably in this case, so extra care must "
    "be taken to make the recognizer operate correctly. Defaults to true.">;
}

let Command = "history" in {
  def history_count : Option<"count", "c">, Group<1>, Arg<"UnsignedInteger">,
    Desc<"How many history commands to print.">;
  def history_start_index : Option<"start-index", "s">, Group<1>,
    Arg<"UnsignedInteger">, Desc<"Index at which to start printing history "
    "commands (or end to mean tail mode).">;
  def history_end_index : Option<"end-index", "e">, Group<1>,
    Arg<"UnsignedInteger">,
    Desc<"Index at which to stop printing history commands.">;
  def history_clear : Option<"clear", "C">, Group<2>,
    Desc<"Clears the current command history.">;
}

let Command = "log enable" in {
  def log_file : Option<"file", "f">, Group<1>, Arg<"Filename">,
    Desc<"Set the destination file to log to.">;
  def log_handler : Option<"log-handler", "h">, Group<1>,
    EnumArg<"LogHandler">, Desc<"Specify a log handler which determines where log messages are written.">;
  def log_buffer_size : Option<"buffer", "b">, Group<1>, Arg<"UnsignedInteger">,
    Desc<"Set the log to be buffered, using the specified buffer size, if supported by the log handler.">;
  def log_verbose : Option<"verbose", "v">, Group<1>,
    Desc<"Enable verbose logging.">;
  def log_sequence : Option<"sequence", "s">, Group<1>,
    Desc<"Prepend all log lines with an increasing integer sequence id.">;
  def log_timestamp : Option<"timestamp", "T">, Group<1>,
    Desc<"Prepend all log lines with a timestamp.">;
  def log_pid_tid : Option<"pid-tid", "p">, Group<1>,
    Desc<"Prepend all log lines with the process and thread ID that generates "
    "the log line.">;
  def log_thread_name : Option<"thread-name", "n">, Group<1>,
    Desc<"Prepend all log lines with the thread name for the thread that "
    "generates the log line.">;

  def log_stack : Option<"stack", "S">, Group<1>,
    Desc<"Append a stack backtrace to each log line.">;
  def log_append : Option<"append", "a">, Group<1>,
    Desc<"Append to the log file instead of overwriting.">;
  def log_file_function : Option<"file-function", "F">, Group<1>,
    Desc<"Prepend the names of files and function that generate the logs.">;
}

let Command = "log dump" in {
  def log_dump_file : Option<"file", "f">, Group<1>, Arg<"Filename">,
    Desc<"Set the destination file to dump to.">;
}

let Command = "memory read" in {
  def memory_read_num_per_line : Option<"num-per-line", "l">, Group<1>,
    Arg<"NumberPerLine">, Desc<"The number of items per line to display.">;
  def memory_read_binary : Option<"binary", "b">, Group<2>,
    Desc<"If true, memory will be saved as binary. If false, the memory is "
    "saved save as an ASCII dump that uses the format, size, count and number "
    "per line settings.">;
  def memory_read_type : Option<"type", "t">, Groups<[3,4]>, Arg<"Name">,
    Required, Desc<"The name of a type to view memory as.">;
  def memory_read_language : Option<"language", "x">, Group<4>, Arg<"Language">,
    Desc<"The language of the type to view memory as.">;
  def memory_read_offset : Option<"offset", "E">, Group<3>, Arg<"Count">,
    Desc<"How many elements of the specified type to skip before starting to "
    "display data.">;
  def memory_read_force : Option<"force", "r">, Groups<[1,2,3]>,
    Desc<"Necessary if reading over target.max-memory-read-size bytes.">;
}

let Command = "memory find" in {
  def memory_find_expression : Option<"expression", "e">, Group<1>,
    Arg<"Expression">, Required,
    Desc<"Evaluate an expression to obtain a byte pattern.">;
  def memory_find_string : Option<"string", "s">, Group<2>, Arg<"Name">,
    Required, Desc<"Use text to find a byte pattern.">;
  def memory_find_count : Option<"count", "c">, Arg<"Count">,
    Desc<"How many times to perform the search.">;
  def memory_find_dump_offset : Option<"dump-offset", "o">, Arg<"Offset">,
    Desc<"When dumping memory for a match, an offset from the match location to"
    " start dumping from.">;
}

let Command = "memory write" in {
  def memory_write_infile : Option<"infile", "i">, Group<1>, Arg<"Filename">,
    Required, Desc<"Write memory using the contents of a file.">;
  def memory_write_offset : Option<"offset", "o">, Group<1>, Arg<"Offset">,
    Desc<"Start writing bytes from an offset within the input file.">;
}

let Command = "memory region" in {
  def memory_region_all : Option<"all", "a">, Group<2>, Required,
    Desc<"Show all memory regions. This is equivalent to starting from address "
         "0 and repeating the command. Unmapped areas are included.">;
}

let Command = "memory tag write" in {
  def memory_write_end_addr : Option<"end-addr", "e">, Group<1>,
  Arg<"AddressOrExpression">, Desc<
    "Set tags for start address to end-addr, repeating tags as needed"
    " to cover the range. (instead of calculating the range from the"
    " number of tags given)">;
}

let Command = "register read" in {
  def register_read_alternate : Option<"alternate", "A">,
    Desc<"Display register names using the alternate register name if there "
    "is one.">;
  def register_read_set : Option<"set", "s">, Group<1>, Arg<"Index">,
    Desc<"Specify which register sets to dump by index.">;
  def register_read_all : Option<"all", "a">, Group<2>,
    Desc<"Show all register sets.">;
}

let Command = "source" in {
  def source_stop_on_error : Option<"stop-on-error", "e">, Arg<"Boolean">,
    Desc<"If true, stop executing commands on error.">;
  def source_stop_on_continue : Option<"stop-on-continue", "c">, Arg<"Boolean">,
    Desc<"If true, stop executing commands on continue.">;
  def source_silent_run : Option<"silent-run", "s">, Arg<"Boolean">,
    Desc<"If true don't echo commands while executing.">;
  def cmd_relative_to_command_file : Option<"relative-to-command-file", "C">,
    Desc<"Resolve non-absolute paths relative to the location of the "
    "current command file. This argument can only be used when the command is "
    "being sourced from a file.">;
}

let Command = "alias" in {
  def alias_help : Option<"help", "h">, Arg<"HelpText">,
    Desc<"Help text for this command">;
  def alias_long_help : Option<"long-help", "H">, Arg<"HelpText">,
    Desc<"Long help text for this command">;
}

let Command = "regex" in {
  def regex_help : Option<"help", "h">, Group<1>, Arg<"None">,
    Desc<"The help text to display for this command.">;
  def regex_syntax : Option<"syntax", "s">, Group<1>, Arg<"None">,
    Desc<"A syntax string showing the typical usage syntax.">;
}

let Command = "permissions" in {
  def permissions_permissions_value : Option<"permissions-value", "v">,
    Arg<"PermissionsNumber">,
    Desc<"Give out the numeric value for permissions (e.g. 757)">;
  def permissions_permissions_string : Option<"permissions-string", "s">,
    Arg<"PermissionsString">,
    Desc<"Give out the string value for permissions (e.g. rwxr-xr--).">;
  def permissions_user_read : Option<"user-read", "r">,
    Desc<"Allow user to read.">;
  def permissions_user_write : Option<"user-write", "w">,
    Desc<"Allow user to write.">;
  def permissions_user_exec : Option<"user-exec", "x">,
    Desc<"Allow user to execute.">;
  def permissions_group_read : Option<"group-read", "R">,
    Desc<"Allow group to read.">;
  def permissions_group_write : Option<"group-write", "W">,
    Desc<"Allow group to write.">;
  def permissions_group_exec : Option<"group-exec", "X">,
    Desc<"Allow group to execute.">;
  def permissions_world_read : Option<"world-read", "d">,
    Desc<"Allow world to read.">;
  def permissions_world_write : Option<"world-write", "t">,
    Desc<"Allow world to write.">;
  def permissions_world_exec : Option<"world-exec", "e">,
    Desc<"Allow world to execute.">;
}

let Command = "platform fread" in {
  def platform_fread_offset : Option<"offset", "o">, Group<1>, Arg<"Index">,
    Desc<"Offset into the file at which to start reading.">;
  def platform_fread_count : Option<"count", "c">, Group<1>, Arg<"Count">,
    Desc<"Number of bytes to read from the file.">;
}

let Command = "platform fwrite" in {
  def platform_fwrite_offset : Option<"offset", "o">, Group<1>, Arg<"Index">,
    Desc<"Offset into the file at which to start reading.">;
  def platform_fwrite_data : Option<"data", "d">, Group<1>, Arg<"Value">,
    Desc<"Text to write to the file.">;
}

let Command = "platform process list" in {
  def platform_process_list_pid : Option<"pid", "p">, Group<1>, Arg<"Pid">,
    Desc<"List the process info for a specific process ID.">;
  def platform_process_list_name : Option<"name", "n">, Group<2>,
    Arg<"ProcessName">, Required,
    Desc<"Find processes with executable basenames that match a string.">;
  def platform_process_list_ends_with : Option<"ends-with", "e">, Group<3>,
  Arg<"ProcessName">, Required,
    Desc<"Find processes with executable basenames that end with a string.">;
  def platform_process_list_starts_with : Option<"starts-with", "s">, Group<4>,
    Arg<"ProcessName">, Required,
    Desc<"Find processes with executable basenames that start with a string.">;
  def platform_process_list_contains : Option<"contains", "c">, Group<5>,
    Arg<"ProcessName">, Required,
    Desc<"Find processes with executable basenames that contain a string.">;
  def platform_process_list_regex : Option<"regex", "r">, Group<6>,
    Arg<"RegularExpression">, Required,
    Desc<"Find processes with executable basenames that match a regular "
    "expression.">;
  def platform_process_list_parent : Option<"parent", "P">, GroupRange<2, 6>,
    Arg<"Pid">, Desc<"Find processes that have a matching parent process ID.">;
  def platform_process_list_uid : Option<"uid", "u">, GroupRange<2, 6>,
    Arg<"UnsignedInteger">, Validator<"&posix_validator">,
    Desc<"Find processes that have a matching user ID.">;
  def platform_process_list_euid : Option<"euid", "U">, GroupRange<2, 6>,
    Arg<"UnsignedInteger">, Validator<"&posix_validator">,
    Desc<"Find processes that have a matching effective user ID.">;
  def platform_process_list_gid : Option<"gid", "g">, GroupRange<2, 6>,
    Arg<"UnsignedInteger">, Validator<"&posix_validator">,
    Desc<"Find processes that have a matching group ID.">;
  def platform_process_list_egid : Option<"egid", "G">, GroupRange<2, 6>,
    Arg<"UnsignedInteger">, Validator<"&posix_validator">,
    Desc<"Find processes that have a matching effective group ID.">;
  def platform_process_list_arch : Option<"arch", "a">, GroupRange<2, 6>,
    Arg<"Architecture">,
    Desc<"Find processes that have a matching architecture.">;
  def platform_process_list_show_args : Option<"show-args", "A">,
    GroupRange<1, 6>,
    Desc<"Show process arguments instead of the process executable basename.">;
  def platform_process_list_all_users: Option<"all-users", "x">,
    GroupRange<1,6>,
    Desc<"Show processes matching all user IDs.">;
  def platform_process_list_verbose : Option<"verbose", "v">, GroupRange<1, 6>,
    Desc<"Enable verbose output.">;
}

let Command = "platform process attach" in {
  def platform_process_attach_plugin : Option<"plugin", "P">, Arg<"Plugin">,
    Desc<"Name of the process plugin you want to use.">;
  def platform_process_attach_pid : Option<"pid", "p">, Group<1>, Arg<"Pid">,
    Desc<"The process ID of an existing process to attach to.">;
  def platform_process_attach_name : Option<"name", "n">, Group<2>,
    Arg<"ProcessName">, Desc<"The name of the process to attach to.">;
  def platform_process_attach_waitfor : Option<"waitfor", "w">, Group<2>,
    Desc<"Wait for the process with <process-name> to launch.">;
}

let Command = "platform shell" in {
  def platform_shell_host : Option<"host", "h">,
    Desc<"Run the commands on the host shell when enabled.">;
  def platform_shell_timeout : Option<"timeout", "t">, Arg<"Value">,
    Desc<"Seconds to wait for the remote host to finish running the command.">;
  def platform_shell_interpreter : Option<"shell", "s">, Arg<"Path">,
    Desc<"Shell interpreter path. This is the binary used to run the command.">;
}

let Command = "process launch" in {
  def process_launch_stop_at_entry : Option<"stop-at-entry", "s">,
    Desc<"Stop at the entry point of the program when launching a process.">;
  def process_launch_stop_at_user_entry : Option<"stop-at-user-entry", "m">,
    Desc<"Stop at the user entry point when launching a process. For C based "
    "languages this will be the 'main' function, but this might differ for "
    "other languages.">;
  def process_launch_disable_aslr : Option<"disable-aslr", "A">, Arg<"Boolean">,
    Desc<"Set whether to disable address space layout randomization when launching a process.">;
  def process_launch_plugin : Option<"plugin", "P">, Arg<"Plugin">,
    Desc<"Name of the process plugin you want to use.">;
  def process_launch_working_dir : Option<"working-dir", "w">, Arg<"DirectoryName">,
    Desc<"Set the current working directory to <path> when running the inferior.">;
  def process_launch_arch : Option<"arch", "a">, Arg<"Architecture">,
    Desc<"Set the architecture for the process to launch when ambiguous.">;
  def process_launch_environment : Option<"environment", "E">,
    Arg<"None">, Desc<"Specify an environment variable name/value string "
    "(--environment NAME=VALUE). Can be specified multiple times for subsequent "
    "environment entries.">;
  def process_launch_shell : Option<"shell", "c">, GroupRange<1,3>,
    OptionalArg<"Filename">, Desc<"Run the process in a shell (not supported on all platforms).">;
  def process_launch_stdin : Option<"stdin", "i">, Group<1>,
    Arg<"Filename">, Desc<"Redirect stdin for the process to <filename>.">;
  def process_launch_stdout : Option<"stdout", "o">, Group<1>,
    Arg<"Filename">, Desc<"Redirect stdout for the process to <filename>.">;
  def process_launch_stderr : Option<"stderr", "e">, Group<1>,
    Arg<"Filename">, Desc<"Redirect stderr for the process to <filename>.">;
  def process_launch_tty : Option<"tty", "t">, Group<2>,
    Desc<"Start the process in a terminal (not supported on all platforms).">;
  def process_launch_no_stdio : Option<"no-stdio", "n">, Group<3>,
    Desc<"Do not set up for terminal I/O to go to running process.">;
  def process_launch_shell_expand_args : Option<"shell-expand-args", "X">, Group<4>,
    Arg<"Boolean">, Desc<"Set whether to shell expand arguments to the process when launching.">;
}

let Command = "process attach" in {
  def process_attach_continue : Option<"continue", "c">,
    Desc<"Immediately continue the process once attached.">;
  def process_attach_plugin : Option<"plugin", "P">, Arg<"Plugin">,
    Desc<"Name of the process plugin you want to use.">;
  def process_attach_pid : Option<"pid", "p">, Group<1>, Arg<"Pid">,
    Desc<"The process ID of an existing process to attach to.">;
  def process_attach_name : Option<"name", "n">, Group<2>, Arg<"ProcessName">,
    Desc<"The name of the process to attach to.">;
  def process_attach_include_existing : Option<"include-existing", "i">,
    Group<2>, Desc<"Include existing processes when doing attach -w.">;
  def process_attach_waitfor : Option<"waitfor", "w">, Group<2>,
    Desc<"Wait for the process with <process-name> to launch.">;
}

let Command = "process continue" in {
  def process_continue_ignore_count : Option<"ignore-count", "i">, Group<1>,
    Arg<"UnsignedInteger">, Desc<"Ignore <N> crossings of the breakpoint (if it"
    " exists) for the currently selected thread.">;
  def process_continue_run_to_bkpt : Option<"continue-to-bkpt", "b">, Group<2>,
    Arg<"BreakpointIDRange">, Desc<"Specify a breakpoint to continue to, temporarily "
    "ignoring other breakpoints.  Can be specified more than once.  "
    "The continue action will be done synchronously if this option is specified.">;
}

let Command = "process detach" in {
  def process_detach_keep_stopped : Option<"keep-stopped", "s">, Group<1>,
    Arg<"Boolean">, Desc<"Whether or not the process should be kept stopped on"
    " detach (if possible).">;
}

let Command = "process connect" in {
  def process_connect_plugin : Option<"plugin", "p">, Arg<"Plugin">,
    Desc<"Name of the process plugin you want to use.">;
}

let Command = "process load" in {
  def process_load_install : Option<"install", "i">, OptionalArg<"Path">,
    Desc<"Install the shared library to the target. If specified without an "
    "argument then the library will installed in the current working "
    "directory.">;
}

let Command = "process handle" in {
  def process_handle_clear : Option<"clear", "c">, Group<2>,
    Desc<"Removes the signals listed from the Target signal handlers">;
  def process_handle_stop : Option<"stop", "s">, Group<1>, Arg<"Boolean">,
    Desc<"Whether or not the process should be stopped if the signal is "
    "received.">;
  def process_handle_notify : Option<"notify", "n">, Group<1>, Arg<"Boolean">,
    Desc<"Whether or not the debugger should notify the user if the signal is "
    "received.">;
  def process_handle_pass : Option<"pass", "p">, Group<1>, Arg<"Boolean">,
    Desc<"Whether or not the signal should be passed to the process.">;
  def process_handle_only_target : Option<"target", "t">, Group<1>,
    Desc<"Show only the signals with behaviors modified in this target">;
  def process_handle_dummy : Option<"dummy", "d">, Group<2>,
    Desc<"Also clear the values in the dummy target so they won't be inherited by new targets.">;
}

let Command = "process status" in {
  def process_status_verbose : Option<"verbose", "v">, Group<1>,
    Desc<"Show verbose process status including extended crash information.">;
}

let Command = "process save_core" in {
  def process_save_core_style : Option<"style", "s">, Group<1>,
    EnumArg<"SaveCoreStyle">, Desc<"Request a specific style "
    "of corefile to be saved.">;
  def process_save_core_plugin_name : Option<"plugin-name", "p">,
    OptionalArg<"Plugin">, Desc<"Specify a plugin name to create the core file. "
    "This allows core files to be saved in different formats.">;
}

let Command = "script import" in {
  def script_import_allow_reload : Option<"allow-reload", "r">, Group<1>,
    Desc<"Allow the script to be loaded even if it was already loaded before. "
    "This argument exists for backwards compatibility, but reloading is always "
    "allowed, whether you specify it or not.">;
  def relative_to_command_file : Option<"relative-to-command-file", "c">,
    Group<1>, Desc<"Resolve non-absolute paths relative to the location of the "
    "current command file. This argument can only be used when the command is "
    "being sourced from a file.">;
  def silent : Option<"silent", "s">, Group<1>,
    Desc<"If true don't print any script output while importing.">;
}

let Command = "script add" in {
  def script_add_function : Option<"function", "f">, Group<1>,
    Arg<"PythonFunction">,
    Desc<"Name of the Python function to bind to this command name.">;
  def script_add_class : Option<"class", "c">, Groups<[2,3]>,
    Arg<"PythonClass">,
    Desc<"Name of the Python class to bind to this command name.">;
  def script_add_help : Option<"help", "h">, Group<1>, Arg<"HelpText">,
    Desc<"The help text to display for this command.">;
    def script_add_overwrite : Option<"overwrite", "o">,
    Desc<"Overwrite an existing command at this node.">;
  def script_add_synchronicity : Option<"synchronicity", "s">,
    EnumArg<"ScriptedCommandSynchronicity">,
    Desc<"Set the synchronicity of this command's executions with regard to "
    "LLDB event system.">;
  def script_add_completion_type : Option<"completion-type", "C">,
    Groups<[1,2]>, EnumArg<"CompletionType">,
    Desc<"Specify which completion type the command should use - if none is "
    "specified, the command won't use auto-completion.">;
  def script_add_parsed_command : Option<"parsed", "p">, Group<3>,
    Desc<"Make a parsed command. The command class will provide the command "
    "definition by implementing get_options and get_arguments.">;

}

let Command = "container add" in {
  def container_add_help : Option<"help", "h">, Arg<"HelpText">,
    Desc<"Help text for this command">;
  def container_add_long_help : Option<"long-help", "H">, Arg<"HelpText">,
    Desc<"Long help text for this command">;
  def container_add_overwrite : Option<"overwrite", "o">, Group<1>,
  Desc<"Overwrite an existing command at this node.">;
}

let Command = "scripting run" in {
  def script_language : Option<"language", "l">,
    EnumArg<"ScriptLang">, Desc<"Specify the scripting "
    " language. If none is specific the default scripting language is used.">;
}

let Command = "source info" in {
  def source_info_count : Option<"count", "c">, Arg<"Count">,
    Desc<"The number of line entries to display.">;
  def source_info_shlib : Option<"shlib", "s">, Groups<[1,2]>, Arg<"ShlibName">,
    Completion<"Module">, Desc<"Look up the source in the given module or "
    "shared library (can be specified more than once).">;
  def source_info_file : Option<"file", "f">, Group<1>, Arg<"Filename">,
    Completion<"SourceFile">, Desc<"The file from which to display source.">;
  def source_info_line : Option<"line", "l">, Group<1>, Arg<"LineNum">,
    Desc<"The line number at which to start the displaying lines.">;
  def source_info_end_line : Option<"end-line", "e">, Group<1>, Arg<"LineNum">,
    Desc<"The line number at which to stop displaying lines.">;
  def source_info_name : Option<"name", "n">, Group<2>, Arg<"Symbol">,
    Completion<"Symbol">,
    Desc<"The name of a function whose source to display.">;
  def source_info_address : Option<"address", "a">, Group<3>,
    Arg<"AddressOrExpression">, Desc<"Lookup the address and display the source"
    " information for the corresponding file and line.">;
}

let Command = "source list" in {
  def source_list_count : Option<"count", "c">, Arg<"Count">,
    Desc<"The number of source lines to display.">;
  def source_list_shlib : Option<"shlib", "s">, Groups<[1,2,5]>, Arg<"ShlibName">,
    Completion<"Module">,
    Desc<"Look up the source file in the given shared library.">;
  def source_list_show_breakpoints : Option<"show-breakpoints", "b">,
    Desc<"Show the line table locations from the debug information that "
    "indicate valid places to set source level breakpoints.">;
  def source_list_file : Option<"file", "f">, Group<1>, Arg<"Filename">,
    Completion<"SourceFile">, Desc<"The file from which to display source.">;
  def source_list_line : Option<"line", "l">, Group<1>, Arg<"LineNum">,
    Desc<"The line number at which to start the display source.">;
  def source_list_name : Option<"name", "n">, Group<2>, Arg<"Symbol">,
    Completion<"Symbol">,
    Desc<"The name of a function whose source to display.">;
  def source_list_address : Option<"address", "a">, Group<3>,
    Arg<"AddressOrExpression">, Desc<"Lookup the address and display the source"
    " information for the corresponding file and line.">;
  def source_list_reverse : Option<"reverse", "r">, Group<4>, Desc<"Reverse the"
    " listing to look backwards from the last displayed block of source.">;
  def source_list_file_colon_line : Option<"joint-specifier", "y">, Group<5>,
    Arg<"FileLineColumn">, Completion<"SourceFile">,
    Desc<"A specifier in the form filename:line[:column] from which to display"
         " source.">;
}

let Command = "target dependents" in {
  def dependents_no_dependents : Option<"no-dependents", "d">, Group<1>,
    OptionalEnumArg<"Value">,
    Desc<"Whether or not to load dependents when creating a target. If the "
         "option is not specified, the value is implicitly 'default'. If the "
         "option is specified but without a value, the value is implicitly "
         "'true'.">;
}

let Command = "target modules dump" in {
  def target_modules_dump_verbose : Option<"verbose", "v">,
    Desc<"Enable verbose dump.">;
}

let Command = "target modules list" in {
  def target_modules_list_address : Option<"address", "a">, Group<1>,
    Arg<"AddressOrExpression">, Desc<"Display the image at this address.">;
  def target_modules_list_arch : Option<"arch", "A">, Group<1>,
    OptionalArg<"Width">, Desc<"Display the architecture when listing images.">;
  def target_modules_list_triple : Option<"triple", "t">, Group<1>,
    OptionalArg<"Width">, Desc<"Display the triple when listing images.">;
  def target_modules_list_header : Option<"header", "h">, Group<1>,
    Desc<"Display the image base address as a load address if debugging, a file"
    " address otherwise.">;
  def target_modules_list_offset : Option<"offset", "o">, Group<1>,
    Desc<"Display the image load address offset from the base file address "
    "(the slide amount).">;
  def target_modules_list_uuid : Option<"uuid", "u">, Group<1>,
    Desc<"Display the UUID when listing images.">;
  def target_modules_list_fullpath : Option<"fullpath", "f">, Group<1>,
    OptionalArg<"Width">,
    Desc<"Display the fullpath to the image object file.">;
  def target_modules_list_directory : Option<"directory", "d">, Group<1>,
    OptionalArg<"Width">, Desc<"Display the directory with optional width for "
    "the image object file.">;
  def target_modules_list_basename : Option<"basename", "b">, Group<1>,
    OptionalArg<"Width">, Desc<"Display the basename with optional width for "
    "the image object file.">;
  def target_modules_list_symfile : Option<"symfile", "s">, Group<1>,
    OptionalArg<"Width">, Desc<"Display the fullpath to the image symbol file "
    "with optional width.">;
  def target_modules_list_symfile_unique : Option<"symfile-unique", "S">,
    Group<1>, OptionalArg<"Width">, Desc<"Display the symbol file with optional"
    " width only if it is different from the executable object file.">;
  def target_modules_list_mod_time : Option<"mod-time", "m">, Group<1>,
    OptionalArg<"Width">, Desc<"Display the modification time with optional "
    "width of the module.">;
  def target_modules_list_ref_count : Option<"ref-count", "r">, Group<1>,
    OptionalArg<"Width">, Desc<"Display whether the module is still in the "
    "the shared module cache (Y/N), and its shared pointer use_count.">;
  def target_modules_list_pointer : Option<"pointer", "p">, Group<1>,
    OptionalArg<"None">, Desc<"Display the module pointer.">;
  def target_modules_list_global : Option<"global", "g">, Group<1>,
    Desc<"Display the modules from the global module list, not just the "
    "current target.">;
}

let Command = "target modules show unwind" in {
  def target_modules_show_unwind_name : Option<"name", "n">, Group<1>,
    Arg<"FunctionName">,
    Desc<"Show unwind instructions for a function or symbol name.">;
  def target_modules_show_unwind_address : Option<"address", "a">, Group<2>,
    Arg<"AddressOrExpression">, Desc<"Show unwind instructions for a function "
    "or symbol containing an address">;
}

let Command = "target modules lookup" in {
  def target_modules_lookup_address : Option<"address", "a">, Group<1>,
    Arg<"AddressOrExpression">, Required, Desc<"Lookup an address in one or "
    "more target modules.">;
  def target_modules_lookup_offset : Option<"offset", "o">, Group<1>,
    Arg<"Offset">, Desc<"When looking up an address subtract <offset> from any "
    "addresses before doing the lookup.">;
  // FIXME: re-enable regex for types when the LookupTypeInModule actually uses
  // the regex option by adding to group 6.
  def target_modules_lookup_regex : Option<"regex", "r">, Groups<[2,4,5]>,
    Desc<"The <name> argument for name lookups are regular expressions.">;
  def target_modules_lookup_symbol : Option<"symbol", "s">, Group<2>,
    Arg<"Symbol">, Required, Desc<"Lookup a symbol by name in the symbol tables"
    " in one or more target modules.">;
  def target_modules_lookup_file : Option<"file", "f">, Group<3>,
    Arg<"Filename">, Required, Desc<"Lookup a file by fullpath or basename in "
    "one or more target modules.">;
  def target_modules_lookup_line : Option<"line", "l">, Group<3>,
    Arg<"LineNum">, Desc<"Lookup a line number in a file (must be used in "
    "conjunction with --file).">;
  def target_modules_lookup_no_inlines : Option<"no-inlines", "i">,
    GroupRange<3,5>,
    Desc<"Ignore inline entries (must be used in conjunction with --file or "
    "--function).">;
  def target_modules_lookup_function : Option<"function", "F">, Group<4>,
    Arg<"FunctionName">, Required, Desc<"Lookup a function by name in the debug"
    " symbols in one or more target modules.">;
  def target_modules_lookup_name : Option<"name", "n">, Group<5>,
    Arg<"FunctionOrSymbol">, Required, Desc<"Lookup a function or symbol by "
    "name in one or more target modules.">;
  def target_modules_lookup_type : Option<"type", "t">, Group<6>, Arg<"Name">,
    Required, Desc<"Lookup a type by name in the debug symbols in one or more "
    "target modules.">;
  def target_modules_lookup_variables_ranges : Option<"show-variable-ranges",
    "\\x01">, GroupRange<1, 6>, Desc<"Dump valid ranges of variables (must be "
    "used in conjunction with --verbose">;
  def target_modules_lookup_verbose : Option<"verbose", "v">,
    Desc<"Enable verbose lookup information.">;
  def target_modules_lookup_all : Option<"all", "A">, Desc<"Print all matches, "
    "not just the best match, if a best match is available.">;
}

let Command = "target stop hook add" in {
  def target_stop_hook_add_one_liner : Option<"one-liner", "o">, GroupRange<1,3>,
    Arg<"OneLiner">, Desc<"Add a command for the stop hook.  Can be specified "
    "more than once, and commands will be run in the order they appear.">;
  def target_stop_hook_add_shlib : Option<"shlib", "s">, Arg<"ShlibName">,
    Completion<"Module">,
    Desc<"Set the module within which the stop-hook is to be run.">;
  def target_stop_hook_add_thread_index : Option<"thread-index", "x">,
    Arg<"ThreadIndex">, Desc<"The stop hook is run only for the thread whose "
    "index matches this argument.">;
  def target_stop_hook_add_thread_id : Option<"thread-id", "t">,
    Arg<"ThreadID">, Desc<"The stop hook is run only for the thread whose TID "
    "matches this argument.">;
  def target_stop_hook_add_thread_name : Option<"thread-name", "T">,
    Arg<"ThreadName">, Desc<"The stop hook is run only for the thread whose "
    "thread name matches this argument.">;
  def target_stop_hook_add_queue_name : Option<"queue-name", "q">,
    Arg<"QueueName">, Desc<"The stop hook is run only for threads in the queue "
    "whose name is given by this argument.">;
  def target_stop_hook_add_file : Option<"file", "f">, Groups<[1,4]>,
    Arg<"Filename">, Desc<"Specify the source file within which the stop-hook "
    "is to be run.">, Completion<"SourceFile">;
  def target_stop_hook_add_start_line : Option<"start-line", "l">, Groups<[1,4]>,
    Arg<"LineNum">, Desc<"Set the start of the line range for which the "
    "stop-hook is to be run.">;
  def target_stop_hook_add_end_line : Option<"end-line", "e">, Groups<[1,4]>,
    Arg<"LineNum">, Desc<"Set the end of the line range for which the stop-hook"
    " is to be run.">;
  def target_stop_hook_add_classname : Option<"classname", "c">, Groups<[2,5]>,
    Arg<"ClassName">,
    Desc<"Specify the class within which the stop-hook is to be run.">;
  def target_stop_hook_add_name : Option<"name", "n">, Groups<[3,6]>,
    Arg<"FunctionName">, Desc<"Set the function name within which the stop hook"
    " will be run.">, Completion<"Symbol">;
  def target_stop_hook_add_auto_continue : Option<"auto-continue", "G">,
    Arg<"Boolean">, Desc<"The breakpoint will auto-continue after running its"
    " commands.">;
}

let Command = "thread backtrace" in {
  def thread_backtrace_count : Option<"count", "c">, Group<1>, Arg<"Count">,
  Desc<"How many frames to display (0 for all)">;
  def thread_backtrace_start : Option<"start", "s">, Group<1>,
  Arg<"FrameIndex">, Desc<"Frame in which to start the backtrace">;
  def thread_backtrace_extended : Option<"extended", "e">, Group<1>,
  Arg<"Boolean">, Desc<"Show the extended backtrace, if available">;
}

let Command = "thread step scope" in {
  def thread_step_scope_step_in_avoids_no_debug :
    Option<"step-in-avoids-no-debug", "a">, Group<1>, Arg<"Boolean">,
    Desc<"A boolean value that sets whether stepping into functions will step "
    "over functions with no debug information.">;
  def thread_step_scope_step_out_avoids_no_debug :
    Option<"step-out-avoids-no-debug", "A">, Group<1>, Arg<"Boolean">,
    Desc<"A boolean value, if true stepping out of functions will continue to"
    " step out till it hits a function with debug information.">;
  def thread_step_scope_count : Option<"count", "c">, Group<1>, Arg<"Count">,
    Desc<"How many times to perform the stepping operation - currently only "
    "supported for step-inst and next-inst.">;
  def thread_step_scope_end_linenumber : Option<"end-linenumber", "e">,
    Group<1>, Arg<"LineNum">, Desc<"The line at which to stop stepping - "
      "defaults to the next line and only supported for step-in and step-over."
      "  You can also pass the string 'block' to step to the end of the current"
      " block.  This is particularly use  in conjunction with --step-target to"
      " step through a complex calling sequence.">;
  def thread_step_scope_run_mode : Option<"run-mode", "m">, Group<1>,
    EnumArg<"RunMode">, Desc<"Determine how to run other "
    "threads while stepping the current thread.">;
  def thread_step_scope_step_over_regexp : Option<"step-over-regexp", "r">,
    Group<1>, Arg<"RegularExpression">, Desc<"A regular expression that defines "
    "function names to not to stop at when stepping in.">;
  def thread_step_scope_step_in_target : Option<"step-in-target", "t">,
    Group<1>, Arg<"FunctionName">, Desc<"The name of the directly called "
    "function step in should stop at when stepping into.">;
}

let Command = "thread until" in {
  def thread_until_frame : Option<"frame", "f">, Group<1>, Arg<"FrameIndex">,
    Desc<"Frame index for until operation - defaults to 0">;
  def thread_until_thread : Option<"thread", "t">, Group<1>, Arg<"ThreadIndex">,
    Desc<"Thread index for the thread for until operation">;
  def thread_until_run_mode : Option<"run-mode", "m">, Group<1>,
    EnumArg<"RunMode">, Desc<"Determine how to run other "
    "threads while stepping this one">;
  def thread_until_address : Option<"address", "a">, Group<1>,
    Arg<"AddressOrExpression">, Desc<"Run until we reach the specified address, "
    "or leave the function - can be specified multiple times.">;
}

let Command = "thread info" in {
  def thread_info_json : Option<"json", "j">, Desc<"Display the thread info in"
    " JSON format.">;
  def thread_info_stop_info : Option<"stop-info", "s">, Desc<"Display the "
    "extended stop info in JSON format.">;
}

let Command = "thread return" in {
  def thread_return_from_expression : Option<"from-expression", "x">,
    Desc<"Return from the innermost expression evaluation.">;
}

let Command = "thread jump" in {
  def thread_jump_file : Option<"file", "f">, Group<1>, Arg<"Filename">,
    Completion<"SourceFile">, Desc<"Specifies the source file to jump to.">;
  def thread_jump_line : Option<"line", "l">, Group<1>, Arg<"LineNum">,
    Required, Desc<"Specifies the line number to jump to.">;
  def thread_jump_by : Option<"by", "b">, Group<2>, Arg<"Offset">, Required,
    Desc<"Jumps by a relative line offset from the current line.">;
  def thread_jump_address : Option<"address", "a">, Group<3>,
    Arg<"AddressOrExpression">, Required, Desc<"Jumps to a specific address.">;
  def thread_jump_force : Option<"force", "r">, Groups<[1,2,3]>,
    Desc<"Allows the PC to leave the current function.">;
}

let Command = "thread plan list" in {
  def thread_plan_list_verbose : Option<"verbose", "v">, Group<1>,
    Desc<"Display more information about the thread plans">;
  def thread_plan_list_internal : Option<"internal", "i">, Group<1>,
    Desc<"Display internal as well as user thread plans">;
  def thread_plan_list_thread_id : Option<"thread-id", "t">, Group<1>,
    Arg<"ThreadID">, Desc<"List the thread plans for this TID, can be "
    "specified more than once.">;
  def thread_plan_list_unreported : Option<"unreported", "u">, Group<1>,
    Desc<"Display thread plans for unreported threads">;
}

let Command = "thread select" in {
  def thread_select_thread_id : Option<"thread-id", "t">, Group<2>,
    Arg<"ThreadID">, Completion<"ThreadID">,
    Desc<"Provide a thread ID instead of a thread index.">;
}

let Command = "thread trace dump function calls" in {
  def thread_trace_dump_function_calls_file : Option<"file", "F">, Group<1>,
    Arg<"Filename">,
    Desc<"Dump the function calls to a file instead of the standard output.">;
  def thread_trace_dump_function_calls_json: Option<"json", "j">,
    Group<1>,
    Desc<"Dump in simple JSON format.">;
  def thread_trace_dump_function_calls_pretty_json: Option<"pretty-json", "J">,
    Group<1>,
    Desc<"Dump in JSON format but pretty printing the output for easier "
    "readability.">;
}

let Command = "thread trace dump instructions" in {
  def thread_trace_dump_instructions_forwards: Option<"forwards", "f">,
    Group<1>,
    Desc<"If specified, the trace is traversed forwards chronologically "
    "starting at the oldest instruction. Otherwise, it starts at the most "
    "recent one and the traversal is backwards.">;
  def thread_trace_dump_instructions_count : Option<"count", "c">, Group<1>,
    Arg<"Count">,
    Desc<"The number of instructions to display starting at the most recent "
    "instruction, or the oldest if --forwards is provided.">;
  def thread_trace_dump_instructions_all : Option<"all", "a">, Group<1>,
    Desc<"From the starting point of the trace, dump all instructions "
    "available.">;
  def thread_trace_dump_instructions_id: Option<"id", "i">, Group<1>,
    Arg<"Index">,
    Desc<"Custom starting instruction id from where to start traversing. This "
    "id can be provided in decimal or hexadecimal representation.">;
  def thread_trace_dump_instructions_skip: Option<"skip", "s">, Group<1>,
    Arg<"Index">,
    Desc<"How many trace items (instructions, errors and events) to skip from "
    "the starting position of the trace before starting the traversal.">;
  def thread_trace_dump_instructions_raw : Option<"raw", "r">, Group<1>,
    Desc<"Dump only instruction address without disassembly nor symbol "
    "information.">;
  def thread_trace_dump_instructions_file : Option<"file", "F">, Group<1>,
    Arg<"Filename">,
    Desc<"Dump the instruction to a file instead of the standard output.">;
  def thread_trace_dump_instructions_json: Option<"json", "j">,
    Group<1>,
    Desc<"Dump in simple JSON format.">;
  def thread_trace_dump_instructions_pretty_print: Option<"pretty-json", "J">,
    Group<1>,
    Desc<"Dump in JSON format but pretty printing the output for easier "
    "readability.">;
  def thread_trace_dump_instructions_show_kind : Option<"kind", "k">, Group<1>,
    Desc<"Show instruction control flow kind. Refer to the enum "
    "`InstructionControlFlowKind` for a list of control flow kind. "
    "As an important note, far jumps, far calls and far returns often indicate "
    "calls to and from kernel.">;
  def thread_trace_dump_instructions_show_timestamps: Option<"time", "t">,
    Group<1>,
    Desc<"For each trace item, print the corresponding wall clock timestamp "
    "if available.">;
  def thread_trace_dump_instructions_show_events : Option<"events", "e">,
    Group<1>,
    Desc<"Dump the events that happened during the execution of the target.">;
  def thread_trace_dump_instruction_only_events : Option<"only-events", "E">,
    Group<1>,
    Desc<"Dump only the events that happened during the execution of the "
    "target. No instrutions are dumped.">;
  def thread_trace_dump_instructions_continue: Option<"continue", "C">,
    Group<1>,
    Desc<"Continue dumping instructions right where the previous invocation of "
    "this command was left, or from the beginning if this is the first "
    "invocation. The --skip argument is discarded and the other arguments are "
    "preserved from the previous invocation when possible.">;
}

let Command = "thread trace dump info" in {
  def thread_trace_dump_info_verbose : Option<"verbose", "v">, Group<1>,
    Desc<"show verbose thread trace dump info">;
  def thread_trace_dump_info_json: Option<"json", "j">, Group<1>,
    Desc<"Dump in JSON format.">;
}

let Command = "type summary add" in {
  def type_summary_add_category : Option<"category", "w">, Arg<"Name">,
    Desc<"Add this to the given category instead of the default one.">;
  def type_summary_add_cascade : Option<"cascade", "C">, Arg<"Boolean">,
    Desc<"If true, cascade through typedef chains.">;
  def type_summary_add_no_value : Option<"no-value", "v">,
    Desc<"Don't show the value, just show the summary, for this type.">;
  def type_summary_add_skip_pointers : Option<"skip-pointers", "p">,
    Desc<"Don't use this format for pointers-to-type objects.">;
  def type_summary_add_skip_references : Option<"skip-references", "r">,
    Desc<"Don't use this format for references-to-type objects.">;
  def type_summary_add_regex : Option<"regex", "x">,
    Desc<"Type names are actually regular expressions.">;
  def type_summary_add_recognizer_function :
    Option<"recognizer-function", "\\x01">,
    Desc<"The names in the argument list are actually the names of python "
    "functions that decide whether to use this summary for any given type. "
    "Cannot be specified at the same time as --regex (-x).">;
  def type_summary_add_inline_children : Option<"inline-children", "c">,
    Group<1>, Required,
    Desc<"If true, inline all child values into summary string.">;
  def type_summary_add_omit_names : Option<"omit-names", "O">, Group<1>,
    Desc<"If true, omit value names in the summary display.">;
  def type_summary_add_summary_string : Option<"summary-string", "s">, Group<2>,
    Arg<"SummaryString">, Required,
    Desc<"Summary string used to display text and object contents.">;
  def type_summary_add_python_script : Option<"python-script", "o">, Group<3>,
    Arg<"PythonScript">,
    Desc<"Give a one-liner Python script as part of the command.">;
  def type_summary_add_python_function : Option<"python-function", "F">,
    Group<3>, Arg<"PythonFunction">,
    Desc<"Give the name of a Python function to use for this type.">;
  def type_summary_add_input_python : Option<"input-python", "P">, Group<3>,
    Desc<"Input Python code to use for this type manually.">;
  def type_summary_add_expand : Option<"expand", "e">, Groups<[2,3]>,
    Desc<"Expand aggregate data types to show children on separate lines.">;
  def type_summary_add_hide_empty : Option<"hide-empty", "h">, Groups<[2,3]>,
    Desc<"Do not expand aggregate data types with no children.">;
  def type_summary_add_name : Option<"name", "n">, Groups<[2,3]>, Arg<"Name">,
    Desc<"A name for this summary string.">;
}

let Command = "type synth add" in {
  def type_synth_add_cascade : Option<"cascade", "C">, Arg<"Boolean">,
    Desc<"If true, cascade through typedef chains.">;
  def type_synth_add_skip_pointers : Option<"skip-pointers", "p">,
    Desc<"Don't use this format for pointers-to-type objects.">;
  def type_synth_add_skip_references : Option<"skip-references", "r">,
    Desc<"Don't use this format for references-to-type objects.">;
  def type_synth_add_category : Option<"category", "w">, Arg<"Name">,
    Desc<"Add this to the given category instead of the default one.">;
  def type_synth_add_python_class : Option<"python-class", "l">, Group<2>,
    Arg<"PythonClass">,
    Desc<"Use this Python class to produce synthetic children.">;
  def type_synth_add_input_python : Option<"input-python", "P">, Group<3>,
    Desc<"Type Python code to generate a class that provides synthetic "
    "children.">;
  def type_synth_add_regex : Option<"regex", "x">,
    Desc<"Type names are actually regular expressions.">;
  def type_synth_add_recognizer_function :
    Option<"recognizer-function", "\\x01">,
    Desc<"The names in the argument list are actually the names of python "
    "functions that decide whether to use this summary for any given type. "
    "Cannot be specified at the same time as --regex (-x).">;
}

let Command = "type format add" in {
  def type_format_add_category : Option<"category", "w">, Arg<"Name">,
    Desc<"Add this to the given category instead of the default one.">;
  def type_format_add_cascade : Option<"cascade", "C">, Arg<"Boolean">,
    Desc<"If true, cascade through typedef chains.">;
  def type_format_add_skip_pointers : Option<"skip-pointers", "p">,
    Desc<"Don't use this format for pointers-to-type objects.">;
  def type_format_add_skip_references : Option<"skip-references", "r">,
    Desc<"Don't use this format for references-to-type objects.">;
  def type_format_add_regex : Option<"regex", "x">,
    Desc<"Type names are actually regular expressions.">;
  def type_format_add_type : Option<"type", "t">, Group<2>, Arg<"Name">,
    Desc<"Format variables as if they were of this type.">;
}

let Command = "type formatter delete" in {
  def type_formatter_delete_all : Option<"all", "a">, Group<1>,
    Desc<"Delete from every category.">;
  def type_formatter_delete_category : Option<"category", "w">, Group<2>,
    Arg<"Name">, Desc<"Delete from given category.">;
  def type_formatter_delete_language : Option<"language", "l">, Group<3>,
    Arg<"Language">, Desc<"Delete from given language's category.">;
}

let Command = "type formatter clear" in {
  def type_formatter_clear_all : Option<"all", "a">,
    Desc<"Clear every category.">;
}

let Command = "type formatter list" in {
  def type_formatter_list_category_regex : Option<"category-regex", "w">,
    Group<1>, Arg<"Name">, Desc<"Only show categories matching this filter.">;
  def type_formatter_list_language : Option<"language", "l">, Group<2>,
    Arg<"Language">, Desc<"Only show the category for a specific language.">;
}

let Command = "type category define" in {
  def type_category_define_enabled : Option<"enabled", "e">,
    Desc<"If specified, this category will be created enabled.">;
  def type_category_define_language : Option<"language", "l">, Arg<"Language">,
    Desc<"Specify the language that this category is supported for.">;
}

let Command = "type category enable" in {
  def type_category_enable_language : Option<"language", "l">, Arg<"Language">,
    Desc<"Enable the category for this language.">;
}

let Command = "type category disable" in {
  def type_category_disable_language : Option<"language", "l">, Arg<"Language">,
    Desc<"Enable the category for this language.">;
}

let Command = "type filter add" in {
  def type_filter_add_cascade : Option<"cascade", "C">, Arg<"Boolean">,
    Desc<"If true, cascade through typedef chains.">;
  def type_filter_add_skip_pointers : Option<"skip-pointers", "p">,
    Desc<"Don't use this format for pointers-to-type objects.">;
  def type_filter_add_skip_references : Option<"skip-references", "r">,
    Desc<"Don't use this format for references-to-type objects.">;
  def type_filter_add_category : Option<"category", "w">, Arg<"Name">,
    Desc<"Add this to the given category instead of the default one.">;
  def type_filter_add_child : Option<"child", "c">, Arg<"ExpressionPath">,
    Desc<"Include this expression path in the synthetic view.">;
  def type_filter_add_regex : Option<"regex", "x">,
    Desc<"Type names are actually regular expressions.">;
}

let Command = "type lookup" in {
  def type_lookup_show_help : Option<"show-help", "h">,
    Desc<"Display available help for types">;
  def type_lookup_language : Option<"language", "l">, Arg<"Language">,
    Desc<"Which language's types should the search scope be">;
}

let Command = "watchpoint list" in {
  def watchpoint_list_brief : Option<"brief", "b">, Group<1>, Desc<"Give a "
    "brief description of the watchpoint (no location info).">;
  def watchpoint_list_full : Option<"full", "f">, Group<2>, Desc<"Give a full "
    "description of the watchpoint and its locations.">;
  def watchpoint_list_verbose : Option<"verbose", "v">, Group<3>, Desc<"Explain "
    "everything we know about the watchpoint (for debugging debugger bugs).">;
}

let Command = "watchpoint ignore" in {
  def watchpoint_ignore_ignore_count : Option<"ignore-count", "i">,
    Arg<"Count">, Required, Desc<"Set the number of times this watchpoint is"
    " skipped before stopping.">;
}

let Command = "watchpoint modify" in {
  def watchpoint_modify_condition : Option<"condition", "c">, Arg<"Expression">,
    Desc<"The watchpoint stops only if this condition expression evaluates "
    "to true.">;
}

let Command = "watchpoint command add" in {
  def watchpoint_command_add_one_liner : Option<"one-liner", "o">, Group<1>,
    Arg<"OneLiner">, Desc<"Specify a one-line watchpoint command inline. Be "
    "sure to surround it with quotes.">;
  def watchpoint_command_add_stop_on_error : Option<"stop-on-error", "e">,
    Arg<"Boolean">, Desc<"Specify whether watchpoint command execution should "
    "terminate on error.">;
  def watchpoint_command_add_script_type : Option<"script-type", "s">,
    EnumArg<"ScriptLang">, Desc<"Specify the language for the"
    " commands - if none is specified, the lldb command interpreter will be "
    "used.">;
  def watchpoint_command_add_python_function : Option<"python-function", "F">,
    Group<2>, Arg<"PythonFunction">, Desc<"Give the name of a Python function "
    "to run as command for this watchpoint. Be sure to give a module name if "
    "appropriate.">;
}

let Command = "watchpoint delete" in {
  def watchpoint_delete_force : Option<"force", "f">, Group<1>,
    Desc<"Delete all watchpoints without querying for confirmation.">;
}

let Command = "trace load" in {
  def trace_load_verbose : Option<"verbose", "v">, Group<1>,
    Desc<"Show verbose trace load logging for debugging the plug-in "
    "implementation.">;
}

let Command = "trace save" in {
  def trace_save_compact: Option<"compact", "c">,
    Group<1>,
    Desc<"Try not to save to disk information irrelevant to the traced "
        "processes. Each trace plug-in implements this in a different "
        "fashion.">;
}

let Command = "trace dump" in {
  def trace_dump_verbose : Option<"verbose", "v">, Group<1>,
    Desc<"Show verbose trace information.">;
}

let Command = "trace schema" in {
  def trace_schema_verbose : Option<"verbose", "v">, Group<1>,
    Desc<"Show verbose trace schema logging for debugging the plug-in.">;
}

let Command = "statistics dump" in {
  def statistics_dump_all: Option<"all-targets", "a">, Group<1>,
    Desc<"Include statistics for all targets.">;
  def statistics_dump_summary: Option<"summary", "s">, Group<1>,
    Desc<"Dump only high-level summary statistics. "
         "Exclude targets, modules, breakpoints etc... details.">;
  def statistics_dump_force: Option<"load-all-debug-info", "f">, Group<1>,
    Desc<"Dump the total possible debug info statistics. "
    "Force loading all the debug information if not yet loaded, and collect "
    "statistics with those.">;
  def statistics_dump_targets: Option<"targets", "r">, Group<1>,
    Arg<"Boolean">,
    Desc<"Dump statistics for the targets, including breakpoints, expression "
    "evaluations, frame variables, etc. "
    "Defaults to true in both default mode and summary mode. "
    "In default mode, if both '--targets' and '--modules' are 'true', a list "
    "of module identifiers will be added to the 'targets' section.">;
  def statistics_dump_modules: Option<"modules", "m">, Group<1>,
    Arg<"Boolean">,
    Desc<"Dump statistics for the modules, including time and size of various "
    "aspects of the module and debug information, type system, path, etc. "
    "Defaults to true, unless the '--summary' mode is enabled, in which case "
    "this is turned off unless specified. "
    "In default mode, if both '--targets' and '--modules' are 'true', a list "
    "of module identifiers will be added to the 'targets' section.">;
  def statistics_dump_transcript: Option<"transcript", "t">, Group<1>,
    Arg<"Boolean">,
    Desc<"If the setting interpreter.save-transcript is enabled and this "
    "option is 'true', include a JSON array with all commands the user and/or "
    "scripts executed during a debug session. "
    "Defaults to true, unless the '--summary' mode is enabled, in which case "
    "this is turned off unless specified.">;

}
