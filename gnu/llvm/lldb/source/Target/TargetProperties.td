include "../../include/lldb/Core/PropertiesBase.td"

let Definition = "target_experimental" in {
  def InjectLocalVars : Property<"inject-local-vars", "Boolean">,
    Global, DefaultTrue,
    Desc<"If true, inject local variables explicitly into the expression text. This will fix symbol resolution when there are name collisions between ivars and local variables. But it can make expressions run much more slowly.">;
}

let Definition = "target" in {
  def DefaultArch: Property<"default-arch", "Arch">,
    Global,
    DefaultStringValue<"">,
    Desc<"Default architecture to choose, when there's a choice.">;
  def MoveToNearestCode: Property<"move-to-nearest-code", "Boolean">,
    DefaultTrue,
    Desc<"Move breakpoints to nearest code.">;
  def Language: Property<"language", "Language">,
    DefaultEnumValue<"eLanguageTypeUnknown">,
    Desc<"The language to use when interpreting expressions entered in commands.">;
  def ExprPrefix: Property<"expr-prefix", "FileSpec">,
    DefaultStringValue<"">,
    Desc<"Path to a file containing expressions to be prepended to all expressions.">;
  def ExprErrorLimit: Property<"expr-error-limit", "UInt64">,
    DefaultUnsignedValue<5>,
    Desc<"The maximum amount of errors to emit while parsing an expression. "
         "A value of 0 means to always continue parsing if possible.">;
  def ExprAllocAddress: Property<"expr-alloc-address", "UInt64">,
    DefaultUnsignedValue<0>,
    Desc<"Start address within the process address space of memory allocation for expression evaluation.">;
  def ExprAllocSize: Property<"expr-alloc-size", "UInt64">,
    DefaultUnsignedValue<0>,
    Desc<"Amount of memory in bytes to allocate for expression evaluation.">;
  def ExprAllocAlign: Property<"expr-alloc-align", "UInt64">,
    DefaultUnsignedValue<0>,
    Desc<"Alignment for each memory allocation for expression evaluation.">;
  def PreferDynamic: Property<"prefer-dynamic-value", "Enum">,
    DefaultEnumValue<"eDynamicDontRunTarget">,
    EnumValues<"OptionEnumValues(g_dynamic_value_types)">,
    Desc<"Should printed values be shown as their dynamic value.">;
  def EnableSynthetic: Property<"enable-synthetic-value", "Boolean">,
    DefaultTrue,
    Desc<"Should synthetic values be used by default whenever available.">;
  def SkipPrologue: Property<"skip-prologue", "Boolean">,
    DefaultTrue,
    Desc<"Skip function prologues when setting breakpoints by name.">;
  def SourceMap: Property<"source-map", "PathMap">,
    DefaultStringValue<"">,
    Desc<"Source path remappings apply substitutions to the paths of source files, typically needed to debug from a different host than the one that built the target.  The source-map property consists of an array of pairs, the first element is a path prefix, and the second is its replacement.  The syntax is `prefix1 replacement1 prefix2 replacement2...`.  The pairs are checked in order, the first prefix that matches is used, and that prefix is substituted with the replacement.  A common pattern is to use source-map in conjunction with the clang -fdebug-prefix-map flag.  In the build, use `-fdebug-prefix-map=/path/to/build_dir=.` to rewrite the host specific build directory to `.`.  Then for debugging, use `settings set target.source-map . /path/to/local_dir` to convert `.` to a valid local path.">;
  def AutoSourceMapRelative: Property<"auto-source-map-relative", "Boolean">,
    DefaultTrue,
    Desc<"Automatically deduce source path mappings based on source file breakpoint resolution. It only deduces source mapping if source file breakpoint request is using full path and if the debug info contains relative paths.">;
  def ExecutableSearchPaths: Property<"exec-search-paths", "FileSpecList">,
    DefaultStringValue<"">,
    Desc<"Executable search paths to use when locating executable files whose paths don't match the local file system.">;
  def DebugFileSearchPaths: Property<"debug-file-search-paths", "FileSpecList">,
    DefaultStringValue<"">,
    Desc<"List of directories to be searched when locating debug symbol files. See also symbols.enable-external-lookup.">;
  def ClangModuleSearchPaths: Property<"clang-module-search-paths", "FileSpecList">,
    DefaultStringValue<"">,
    Desc<"List of directories to be searched when locating modules for Clang.">;
  def AutoImportClangModules: Property<"auto-import-clang-modules", "Boolean">,
    DefaultTrue,
    Desc<"Automatically load Clang modules referred to by the program.">;
  def ImportStdModule: Property<"import-std-module", "Enum">,
    DefaultEnumValue<"eImportStdModuleFalse">,
    EnumValues<"OptionEnumValues(g_import_std_module_value_types)">,
    Desc<"Import the 'std' C++ module to improve expression parsing involving "
         " C++ standard library types.">;
  def DynamicClassInfoHelper: Property<"objc-dynamic-class-extractor", "Enum">,
    DefaultEnumValue<"eDynamicClassInfoHelperAuto">,
    EnumValues<"OptionEnumValues(g_dynamic_class_info_helper_value_types)">,
    Desc<"Configure how LLDB parses dynamic Objective-C class metadata. By default LLDB will choose the most appropriate method for the target OS.">;
  def AutoApplyFixIts: Property<"auto-apply-fixits", "Boolean">,
    DefaultTrue,
    Desc<"Automatically apply fix-it hints to expressions.">;
  def RetriesWithFixIts: Property<"retries-with-fixits", "UInt64">,
    DefaultUnsignedValue<1>,
    Desc<"Maximum number of attempts to fix an expression with Fix-Its">;
  def NotifyAboutFixIts: Property<"notify-about-fixits", "Boolean">,
    DefaultTrue,
    Desc<"Print the fixed expression text.">;
  def SaveObjectsDir: Property<"save-jit-objects-dir", "FileSpec">,
    DefaultStringValue<"">,
    Desc<"If specified, the directory to save intermediate object files generated by the LLVM JIT">;
  def ShowHexVariableValuesWithLeadingZeroes: Property<"show-hex-variable-values-with-leading-zeroes", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"Whether to display leading zeroes when printing variable values in hex format.">;
  def MaxZeroPaddingInFloatFormat: Property<"max-zero-padding-in-float-format", "UInt64">,
    DefaultUnsignedValue<6>,
    Desc<"The maximum number of zeroes to insert when displaying a very small float before falling back to scientific notation.">;
  def MaxChildrenCount: Property<"max-children-count", "SInt64">,
    DefaultUnsignedValue<256>,
    Desc<"Maximum number of children to expand in any level of depth.">;
  def MaxChildrenDepth: Property<"max-children-depth", "UInt64">,
    DefaultUnsignedValue<0xFFFFFFFF>,
    Desc<"Maximum depth to expand children.">;
  def MaxSummaryLength: Property<"max-string-summary-length", "UInt64">,
    DefaultUnsignedValue<1024>,
    Desc<"Maximum number of characters to show when using %s in summary strings.">;
  def MaxMemReadSize: Property<"max-memory-read-size", "SInt64">,
    DefaultUnsignedValue<1024>,
    Desc<"Maximum number of bytes that 'memory read' will fetch before --force must be specified.">;
  def BreakpointUseAvoidList: Property<"breakpoints-use-platform-avoid-list", "Boolean">,
    DefaultTrue,
    Desc<"Consult the platform module avoid list when setting non-module specific breakpoints.">;
  def Arg0: Property<"arg0", "String">,
    DefaultStringValue<"">,
    Desc<"The first argument passed to the program in the argument array which can be different from the executable itself.">;
  def RunArgs: Property<"run-args", "Args">,
    DefaultStringValue<"">,
    Desc<"A list containing all the arguments to be passed to the executable when it is run. Note that this does NOT include the argv[0] which is in target.arg0.">;
  def EnvVars: Property<"env-vars", "Dictionary">,
    ElementType<"String">,
    Desc<"A list of user provided environment variables to be passed to the executable's environment, and their values.">;
  def UnsetEnvVars: Property<"unset-env-vars", "Array">,
    ElementType<"String">,
    Desc<"A list of environment variable names to be unset in the inferior's environment. This is most useful to unset some host environment variables when target.inherit-env is true. target.env-vars takes precedence over target.unset-env-vars.">;
  def InheritEnv: Property<"inherit-env", "Boolean">,
    DefaultTrue,
    Desc<"Inherit the environment from the process that is running LLDB.">;
  def InputPath: Property<"input-path", "FileSpec">,
    DefaultStringValue<"">,
    Desc<"The file/path to be used by the executable program for reading its standard input.">;
  def OutputPath: Property<"output-path", "FileSpec">,
    DefaultStringValue<"">,
    Desc<"The file/path to be used by the executable program for writing its standard output.">;
  def ErrorPath: Property<"error-path", "FileSpec">,
    DefaultStringValue<"">,
    Desc<"The file/path to be used by the executable program for writing its standard error.">;
  def DetachOnError: Property<"detach-on-error", "Boolean">,
    DefaultTrue,
    Desc<"debugserver will detach (rather than killing) a process if it loses connection with lldb.">;
  def PreloadSymbols: Property<"preload-symbols", "Boolean">,
    DefaultTrue,
    Desc<"Enable loading of symbol tables before they are needed.">;
  def DisableASLR: Property<"disable-aslr", "Boolean">,
    DefaultTrue,
    Desc<"Disable Address Space Layout Randomization (ASLR)">;
  def DisableSTDIO: Property<"disable-stdio", "Boolean">,
    DefaultFalse,
    Desc<"Disable stdin/stdout for process (e.g. for a GUI application)">;
  def InheritTCC: Property<"inherit-tcc", "Boolean">,
    DefaultFalse,
    Desc<"Inherit the TCC permissions from the inferior's parent instead of making the process itself responsible.">;
  def InlineStrategy: Property<"inline-breakpoint-strategy", "Enum">,
    DefaultEnumValue<"eInlineBreakpointsAlways">,
    EnumValues<"OptionEnumValues(g_inline_breakpoint_enums)">,
    Desc<"The strategy to use when settings breakpoints by file and line. Breakpoint locations can end up being inlined by the compiler, so that a compile unit 'a.c' might contain an inlined function from another source file. Usually this is limited to breakpoint locations from inlined functions from header or other include files, or more accurately non-implementation source files. Sometimes code might #include implementation files and cause inlined breakpoint locations in inlined implementation files. Always checking for inlined breakpoint locations can be expensive (memory and time), so if you have a project with many headers and find that setting breakpoints is slow, then you can change this setting to headers. This setting allows you to control exactly which strategy is used when setting file and line breakpoints.">;
  def DisassemblyFlavor: Property<"x86-disassembly-flavor", "Enum">,
    DefaultEnumValue<"eX86DisFlavorDefault">,
    EnumValues<"OptionEnumValues(g_x86_dis_flavor_value_types)">,
    Desc<"The default disassembly flavor to use for x86 or x86-64 targets.">;
  def UseHexImmediates: Property<"use-hex-immediates", "Boolean">,
    DefaultTrue,
    Desc<"Show immediates in disassembly as hexadecimal.">;
  def HexImmediateStyle: Property<"hex-immediate-style", "Enum">,
    DefaultEnumValue<"Disassembler::eHexStyleC">,
    EnumValues<"OptionEnumValues(g_hex_immediate_style_values)">,
    Desc<"Which style to use for printing hexadecimal disassembly values.">;
  def UseFastStepping: Property<"use-fast-stepping", "Boolean">,
    DefaultTrue,
    Desc<"Use a fast stepping algorithm based on running from branch to branch rather than instruction single-stepping.">;
  def LoadScriptFromSymbolFile: Property<"load-script-from-symbol-file", "Enum">,
    DefaultEnumValue<"eLoadScriptFromSymFileWarn">,
    EnumValues<"OptionEnumValues(g_load_script_from_sym_file_values)">,
    Desc<"Allow LLDB to load scripting resources embedded in symbol files when available.">;
  def LoadCWDlldbinitFile: Property<"load-cwd-lldbinit", "Enum">,
    DefaultEnumValue<"eLoadCWDlldbinitWarn">,
    EnumValues<"OptionEnumValues(g_load_cwd_lldbinit_values)">,
    Desc<"Allow LLDB to .lldbinit files from the current directory automatically.">;
  def MemoryModuleLoadLevel: Property<"memory-module-load-level", "Enum">,
    DefaultEnumValue<"eMemoryModuleLoadLevelComplete">,
    EnumValues<"OptionEnumValues(g_memory_module_load_level_values)">,
    Desc<"Loading modules from memory can be slow as reading the symbol tables and other data can take a long time depending on your connection to the debug target. This setting helps users control how much information gets loaded when loading modules from memory.'complete' is the default value for this setting which will load all sections and symbols by reading them from memory (slowest, most accurate). 'partial' will load sections and attempt to find function bounds without downloading the symbol table (faster, still accurate, missing symbol names). 'minimal' is the fastest setting and will load section data with no symbols, but should rarely be used as stack frames in these memory regions will be inaccurate and not provide any context (fastest). ">;
  def DisplayExpressionsInCrashlogs: Property<"display-expression-in-crashlogs", "Boolean">,
    DefaultFalse,
    Desc<"Expressions that crash will show up in crash logs if the host system supports executable specific crash log strings and this setting is set to true.">;
  def TrapHandlerNames: Property<"trap-handler-names", "Array">,
    Global,
    ElementType<"String">,
    Desc<"A list of trap handler function names, e.g. a common Unix user process one is _sigtramp.">;
  def DisplayRuntimeSupportValues: Property<"display-runtime-support-values", "Boolean">,
    DefaultFalse,
    Desc<"If true, LLDB will show variables that are meant to support the operation of a language's runtime support.">;
  def DisplayRecognizedArguments: Property<"display-recognized-arguments", "Boolean">,
    DefaultFalse,
    Desc<"Show recognized arguments in variable listings by default.">;
  def RequireHardwareBreakpoints: Property<"require-hardware-breakpoint", "Boolean">,
    DefaultFalse,
    Desc<"Require all breakpoints to be hardware breakpoints.">;
  def AutoInstallMainExecutable: Property<"auto-install-main-executable", "Boolean">,
    DefaultTrue,
    Desc<"Always install the main executable when connected to a remote platform.">;
  def DebugUtilityExpression: Property<"debug-utility-expression", "Boolean">,
    DefaultFalse,
    Desc<"Enable debugging of LLDB-internal utility expressions.">;
}

let Definition = "process_experimental" in {
  def OSPluginReportsAllThreads: Property<"os-plugin-reports-all-threads", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"Set to False if your OS Plugins doesn't report all threads on each stop.">;
}

let Definition = "process" in {
  def DisableMemCache: Property<"disable-memory-cache", "Boolean">,
    DefaultFalse,
    Desc<"Disable reading and caching of memory in fixed-size units.">;
  def ExtraStartCommand: Property<"extra-startup-command", "Array">,
    ElementType<"String">,
    Desc<"A list containing extra commands understood by the particular process plugin used.  For instance, to turn on debugserver logging set this to 'QSetLogging:bitmask=LOG_DEFAULT;'">;
  def IgnoreBreakpointsInExpressions: Property<"ignore-breakpoints-in-expressions", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, breakpoints will be ignored during expression evaluation.">;
  def UnwindOnErrorInExpressions: Property<"unwind-on-error-in-expressions", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, errors in expression evaluation will unwind the stack back to the state before the call.">;
  def PythonOSPluginPath: Property<"python-os-plugin-path", "FileSpec">,
    DefaultUnsignedValue<1>,
    Desc<"A path to a python OS plug-in module file that contains a OperatingSystemPlugIn class.">;
  def StopOnSharedLibraryEvents: Property<"stop-on-sharedlibrary-events", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"If true, stop when a shared library is loaded or unloaded.">;
  def DisableLangRuntimeUnwindPlans: Property<"disable-language-runtime-unwindplans", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"If true, language runtime augmented/overidden backtraces will not be used when printing a stack trace.">;
  def DetachKeepsStopped: Property<"detach-keeps-stopped", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"If true, detach will attempt to keep the process stopped.">;
  def MemCacheLineSize: Property<"memory-cache-line-size", "UInt64">,
    DefaultUnsignedValue<512>,
    Desc<"The memory cache line size">;
  def WarningOptimization: Property<"optimization-warnings", "Boolean">,
    DefaultTrue,
    Desc<"If true, warn when stopped in code that is optimized where stepping and variable availability may not behave as expected.">;
  def WarningUnsupportedLanguage: Property<"unsupported-language-warnings", "Boolean">,
    DefaultTrue,
    Desc<"If true, warn when stopped in code that is written in a source language that LLDB does not support.">;
  def StopOnExec: Property<"stop-on-exec", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, stop when the inferior exec's.">;
  def UtilityExpressionTimeout: Property<"utility-expression-timeout", "UInt64">,
#ifdef LLDB_SANITIZED
    DefaultUnsignedValue<60>,
#else
    DefaultUnsignedValue<15>,
#endif
    Desc<"The time in seconds to wait for LLDB-internal utility expressions.">;
  def InterruptTimeout: Property<"interrupt-timeout", "UInt64">,
#ifdef LLDB_SANITIZED
    DefaultUnsignedValue<60>,
#else
    DefaultUnsignedValue<20>,
#endif
    Desc<"The time in seconds to wait for an interrupt succeed in stopping the target.">;
  def SteppingRunsAllThreads: Property<"run-all-threads", "Boolean">,
    DefaultFalse,
    Desc<"If true, stepping operations will run all threads.  This is equivalent to setting the run-mode option to 'all-threads'.">;
  def VirtualAddressableBits: Property<"virtual-addressable-bits", "UInt64">,
    DefaultUnsignedValue<0>,
    Desc<"The number of bits used for addressing. If the value is 39, then bits 0..38 are used for addressing. The default value of 0 means unspecified.">;
  def HighmemVirtualAddressableBits: Property<"highmem-virtual-addressable-bits", "UInt64">,
    DefaultUnsignedValue<0>,
    Desc<"The number of bits used for addressing high memory, when it differs from low memory in the same Process. When this is non-zero, target.process.virtual-addressable-bits will be the value for low memory (0x000... addresses) and this setting will be the value for high memory (0xfff... addresses). When this is zero, target.process.virtual-addressable-bits applies to all addresses. It is very uncommon to use this setting.">;
  def FollowForkMode: Property<"follow-fork-mode", "Enum">,
    DefaultEnumValue<"eFollowParent">,
    EnumValues<"OptionEnumValues(g_follow_fork_mode_values)">,
    Desc<"Debugger's behavior upon fork or vfork.">;
}

let Definition = "platform" in {
  def UseModuleCache: Property<"use-module-cache", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"Use module cache.">;
  def ModuleCacheDirectory: Property<"module-cache-directory", "FileSpec">,
    Global,
    DefaultStringValue<"">,
    Desc<"Root directory for cached modules.">;
}

let Definition = "thread" in {
  def StepInAvoidsNoDebug: Property<"step-in-avoid-nodebug", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, step-in will not stop in functions with no debug information.">;
  def StepOutAvoidsNoDebug: Property<"step-out-avoid-nodebug", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"If true, when step-in/step-out/step-over leave the current frame, they will continue to step out till they come to a function with debug information. Passing a frame argument to step-out will override this option.">;
  def StepAvoidRegex: Property<"step-avoid-regexp", "Regex">,
    Global,
    DefaultStringValue<"^std::">,
    Desc<"A regular expression defining functions step-in won't stop in.">;
  def StepAvoidLibraries: Property<"step-avoid-libraries", "FileSpecList">,
    Global,
    DefaultStringValue<"">,
    Desc<"A list of libraries that source stepping won't stop in.">;
  def EnableThreadTrace: Property<"trace-thread", "Boolean">,
    DefaultFalse,
    Desc<"If true, this thread will single-step and log execution.">;
  def MaxBacktraceDepth: Property<"max-backtrace-depth", "UInt64">,
    DefaultUnsignedValue<600000>,
    Desc<"Maximum number of frames to backtrace.">;
}

let Definition = "language" in {
  def EnableFilterForLineBreakpoints: Property<"enable-filter-for-line-breakpoints", "Boolean">,
    DefaultTrue,
    Desc<"If true, allow Language plugins to filter locations when setting breakpoints by line number or regex.">;
}
