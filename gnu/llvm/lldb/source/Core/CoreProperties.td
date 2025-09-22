include "../../include/lldb/Core/PropertiesBase.td"

let Definition = "modulelist" in {
  def EnableExternalLookup: Property<"enable-external-lookup", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"Control the use of external tools and repositories to locate symbol files. Directories listed in target.debug-file-search-paths and directory of the executable are always checked first for separate debug info files. Then depending on this setting: On macOS, Spotlight would be also used to locate a matching .dSYM bundle based on the UUID of the executable. On NetBSD, directory /usr/libdata/debug would be also searched. On platforms other than NetBSD directory /usr/lib/debug would be also searched. If all other methods fail there may be symbol-locator plugins that, if configured properly, will also attempt to acquire symbols. The debuginfod plugin defaults to the DEGUFINFOD_URLS environment variable which is configurable through the 'plugin.symbol-locator.debuginfod.server_urls' setting.">;
  def EnableBackgroundLookup: Property<"enable-background-lookup", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"Alias for backward compatibility: when enabled this is the equivalent to 'symbols.auto-download background'.">;
  def AutoDownload: Property<"auto-download", "Enum">,
    Global,
    DefaultEnumValue<"eSymbolDownloadOff">,
    EnumValues<"OptionEnumValues(g_auto_download_enum_values)">,
    Desc<"On macOS, automatically download symbols with dsymForUUID (or an equivalent script/binary) for relevant images in the debug session.">;
  def ClangModulesCachePath: Property<"clang-modules-cache-path", "FileSpec">,
    Global,
    DefaultStringValue<"">,
    Desc<"The path to the clang modules cache directory (-fmodules-cache-path).">;
  def SymLinkPaths: Property<"debug-info-symlink-paths", "FileSpecList">,
    Global,
    DefaultStringValue<"">,
    Desc<"Debug info path which should be resolved while parsing, relative to the host filesystem.">;
  def EnableLLDBIndexCache: Property<"enable-lldb-index-cache", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"Enable caching for debug sessions in LLDB. LLDB can cache data for each module for improved performance in subsequent debug sessions.">;
  def LLDBIndexCachePath: Property<"lldb-index-cache-path", "FileSpec">,
    Global,
    DefaultStringValue<"">,
    Desc<"The path to the LLDB index cache directory.">;
  def LLDBIndexCacheMaxByteSize: Property<"lldb-index-cache-max-byte-size", "UInt64">,
    Global,
    DefaultUnsignedValue<0>,
    Desc<"The maximum size for the LLDB index cache directory in bytes. A value over the amount of available space on the disk will be reduced to the amount of available space. A value of 0 disables the absolute size-based pruning.">;
  def LLDBIndexCacheMaxPercent: Property<"lldb-index-cache-max-percent", "UInt64">,
    Global,
    DefaultUnsignedValue<0>,
    Desc<"The maximum size for the cache directory in terms of percentage of the available space on the disk. Set to 100 to indicate no limit, 50 to indicate that the cache size will not be left over half the available disk space. A value over 100 will be reduced to 100. A value of 0 disables the percentage size-based pruning.">;
  def LLDBIndexCacheExpirationDays: Property<"lldb-index-cache-expiration-days", "UInt64">,
    Global,
    DefaultUnsignedValue<7>,
    Desc<"The expiration time in days for a file. When a file hasn't been accessed for the specified amount of days, it is removed from the cache. A value of 0 disables the expiration-based pruning.">;
  def LoadSymbolOnDemand: Property<"load-on-demand", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"Enable on demand symbol loading in LLDB. LLDB will load debug info on demand for each module based on various conditions (e.g. matched breakpoint, resolved stack frame addresses and matched global variables/function symbols in symbol table) to improve performance. Please refer to docs/use/ondemand.rst for details.">;
}

let Definition = "debugger" in {
  def AutoConfirm: Property<"auto-confirm", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"If true all confirmation prompts will receive their default reply.">;
  def DisassemblyFormat: Property<"disassembly-format", "FormatEntity">,
    Global,
    DefaultStringValue<"{${function.initial-function}{${module.file.basename}`}{${function.name-without-args}}:\\\\n}{${function.changed}\\\\n{${module.file.basename}`}{${function.name-without-args}}:\\\\n}{${ansi.fg.yellow}${current-pc-arrow}${ansi.normal} }${addr-file-or-load}{ <${function.concrete-only-addr-offset-no-padding}>}: ">,
    Desc<"The default disassembly format string to use when disassembling instruction sequences.">;
  def FrameFormat: Property<"frame-format", "FormatEntity">,
    Global,
    DefaultStringValue<"frame #${frame.index}: ${ansi.fg.yellow}${frame.pc}${ansi.normal}{ ${module.file.basename}{`${function.name-with-args}{${frame.no-debug}${function.pc-offset}}}}{ at ${ansi.fg.cyan}${line.file.basename}${ansi.normal}:${ansi.fg.yellow}${line.number}${ansi.normal}{:${ansi.fg.yellow}${line.column}${ansi.normal}}}{${function.is-optimized} [opt]}{${frame.is-artificial} [artificial]}\\\\n">,
    Desc<"The default frame format string to use when displaying stack frame information for threads.">;
  def NotiftVoid: Property<"notify-void", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"Notify the user explicitly if an expression returns void (default: false).">;
  def Prompt: Property<"prompt", "String">,
    Global,
    DefaultEnumValue<"OptionValueString::eOptionEncodeCharacterEscapeSequences">,
    DefaultStringValue<"(lldb) ">,
    Desc<"The debugger command line prompt displayed for the user.">;
  def PromptAnsiPrefix: Property<"prompt-ansi-prefix", "String">,
    Global,
    DefaultStringValue<"${ansi.faint}">,
    Desc<"When in a color-enabled terminal, use the ANSI terminal code specified in this format immediately before the prompt.">;
  def PromptAnsiSuffix: Property<"prompt-ansi-suffix", "String">,
    Global,
    DefaultStringValue<"${ansi.normal}">,
    Desc<"When in a color-enabled terminal, use the ANSI terminal code specified in this format immediately after the prompt.">;
  def ScriptLanguage: Property<"script-lang", "Enum">,
    Global,
    DefaultEnumValue<"eScriptLanguagePython">,
    EnumValues<"OptionEnumValues(g_language_enumerators)">,
    Desc<"The script language to be used for evaluating user-written scripts.">;
  def REPLLanguage: Property<"repl-lang", "Language">,
    Global,
    DefaultEnumValue<"eLanguageTypeUnknown">,
    Desc<"The language to use for the REPL.">;
  def StopDisassemblyCount: Property<"stop-disassembly-count", "UInt64">,
    Global,
    DefaultUnsignedValue<4>,
    Desc<"The number of disassembly lines to show when displaying a stopped context.">;
  def StopDisassemblyDisplay: Property<"stop-disassembly-display", "Enum">,
    Global,
    DefaultEnumValue<"Debugger::eStopDisassemblyTypeNoDebugInfo">,
    EnumValues<"OptionEnumValues(g_show_disassembly_enum_values)">,
    Desc<"Control when to display disassembly when displaying a stopped context.">;
  def StopDisassemblyMaxSize: Property<"stop-disassembly-max-size", "UInt64">,
    Global,
    DefaultUnsignedValue<32000>,
    Desc<"The size limit to use when disassembling large functions (default: 32KB).">;
  def StopLineCountAfter: Property<"stop-line-count-after", "UInt64">,
    Global,
    DefaultUnsignedValue<3>,
    Desc<"The number of sources lines to display that come after the current source line when displaying a stopped context.">;
  def StopLineCountBefore: Property<"stop-line-count-before", "UInt64">,
    Global,
    DefaultUnsignedValue<3>,
    Desc<"The number of sources lines to display that come before the current source line when displaying a stopped context.">;
  def HighlightSource: Property<"highlight-source", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, LLDB will highlight the displayed source code.">;
  def StopShowColumn: Property<"stop-show-column", "Enum">,
    DefaultEnumValue<"eStopShowColumnAnsiOrCaret">,
    EnumValues<"OptionEnumValues(s_stop_show_column_values)">,
    Desc<"If true, LLDB will use the column information from the debug info to mark the current position when displaying a stopped context.">;
  def StopShowColumnAnsiPrefix: Property<"stop-show-column-ansi-prefix", "String">,
    Global,
    DefaultStringValue<"${ansi.underline}">,
    Desc<"When displaying the column marker in a color-enabled terminal, use the ANSI terminal code specified in this format at the immediately before the column to be marked.">;
  def StopShowColumnAnsiSuffix: Property<"stop-show-column-ansi-suffix", "String">,
    Global,
    DefaultStringValue<"${ansi.normal}">,
    Desc<"When displaying the column marker in a color-enabled terminal, use the ANSI terminal code specified in this format immediately after the column to be marked.">;
  def StopShowLineMarkerAnsiPrefix: Property<"stop-show-line-ansi-prefix", "String">,
    Global,
    DefaultStringValue<"${ansi.fg.yellow}">,
    Desc<"When displaying the line marker in a color-enabled terminal, use the ANSI terminal code specified in this format at the immediately before the line to be marked.">;
  def StopShowLineMarkerAnsiSuffix: Property<"stop-show-line-ansi-suffix", "String">,
    Global,
    DefaultStringValue<"${ansi.normal}">,
    Desc<"When displaying the line marker in a color-enabled terminal, use the ANSI terminal code specified in this format immediately after the line to be marked.">;
  def TerminalWidth: Property<"term-width", "UInt64">,
    Global,
    DefaultUnsignedValue<80>,
    Desc<"The maximum number of columns to use for displaying text.">;
  def ThreadFormat: Property<"thread-format", "FormatEntity">,
    Global,
    DefaultStringValue<"thread #${thread.index}: tid = ${thread.id%tid}{, ${frame.pc}}{ ${module.file.basename}{`${function.name-with-args}{${frame.no-debug}${function.pc-offset}}}}{ at ${ansi.fg.cyan}${line.file.basename}${ansi.normal}:${ansi.fg.yellow}${line.number}${ansi.normal}{:${ansi.fg.yellow}${line.column}${ansi.normal}}}{, name = ${ansi.fg.green}'${thread.name}'${ansi.normal}}{, queue = ${ansi.fg.green}'${thread.queue}'${ansi.normal}}{, activity = ${ansi.fg.green}'${thread.info.activity.name}'${ansi.normal}}{, ${thread.info.trace_messages} messages}{, stop reason = ${ansi.fg.red}${thread.stop-reason}${ansi.normal}}{\\\\nReturn value: ${thread.return-value}}{\\\\nCompleted expression: ${thread.completed-expression}}\\\\n">,
    Desc<"The default thread format string to use when displaying thread information.">;
  def ThreadStopFormat: Property<"thread-stop-format", "FormatEntity">,
    Global,
    DefaultStringValue<"thread #${thread.index}{, name = '${thread.name}'}{, queue = ${ansi.fg.green}'${thread.queue}'${ansi.normal}}{, activity = ${ansi.fg.green}'${thread.info.activity.name}'${ansi.normal}}{, ${thread.info.trace_messages} messages}{, stop reason = ${ansi.fg.red}${thread.stop-reason}${ansi.normal}}{\\\\nReturn value: ${thread.return-value}}{\\\\nCompleted expression: ${thread.completed-expression}}\\\\n">,
    Desc<"The default thread format  string to use when displaying thread information as part of the stop display.">;
  def UseExternalEditor: Property<"use-external-editor", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"Whether to use an external editor or not.">;
  def ExternalEditor: Property<"external-editor", "String">,
    Global,
    DefaultStringValue<"">,
    Desc<"External editor to use when use-external-editor is enabled.">;
  def UseColor: Property<"use-color", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"Whether to use Ansi color codes or not.">;
  def ShowProgress: Property<"show-progress", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"Whether to show progress or not if the debugger's output is an interactive color-enabled terminal.">;
  def ShowProgressAnsiPrefix: Property<"show-progress-ansi-prefix", "String">,
    Global,
    DefaultStringValue<"${ansi.faint}">,
    Desc<"When displaying progress in a color-enabled terminal, use the ANSI terminal code specified in this format immediately before the progress message.">;
  def ShowProgressAnsiSuffix: Property<"show-progress-ansi-suffix", "String">,
    Global,
    DefaultStringValue<"${ansi.normal}">,
    Desc<"When displaying progress in a color-enabled terminal, use the ANSI terminal code specified in this format immediately after the progress message.">;
  def UseSourceCache: Property<"use-source-cache", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"Whether to cache source files in memory or not.">;
  def AutoOneLineSummaries: Property<"auto-one-line-summaries", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, LLDB will automatically display small structs in one-liner format (default: true).">;
  def AutoIndent: Property<"auto-indent", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, LLDB will auto indent/outdent code. Currently only supported in the REPL (default: true).">;
  def PrintDecls: Property<"print-decls", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, LLDB will print the values of variables declared in an expression. Currently only supported in the REPL (default: true).">;
  def TabSize: Property<"tab-size", "UInt64">,
    Global,
    DefaultUnsignedValue<2>,
    Desc<"The tab size to use when indenting code in multi-line input mode (default: 2).">;
  def EscapeNonPrintables: Property<"escape-non-printables", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, LLDB will automatically escape non-printable and escape characters when formatting strings.">;
  def FrameFormatUnique: Property<"frame-format-unique", "FormatEntity">,
    Global,
    DefaultStringValue<"frame #${frame.index}: ${ansi.fg.yellow}${frame.pc}${ansi.normal}{ ${module.file.basename}{`${function.name-without-args}{${frame.no-debug}${function.pc-offset}}}}{ at ${ansi.fg.cyan}${line.file.basename}${ansi.normal}:${ansi.fg.yellow}${line.number}${ansi.normal}{:${ansi.fg.yellow}${line.column}${ansi.normal}}}{${function.is-optimized} [opt]}{${frame.is-artificial} [artificial]}\\\\n">,
    Desc<"The default frame format string to use when displaying stack frame information for threads from thread backtrace unique.">;
  def ShowAutosuggestion: Property<"show-autosuggestion", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"If true, LLDB will show suggestions to complete the command the user typed. Suggestions may be accepted using Ctrl-F.">;
  def ShowAutosuggestionAnsiPrefix: Property<"show-autosuggestion-ansi-prefix", "String">,
    Global,
    DefaultStringValue<"${ansi.faint}">,
    Desc<"When displaying suggestion in a color-enabled terminal, use the ANSI terminal code specified in this format immediately before the suggestion.">;
  def ShowAutosuggestionAnsiSuffix: Property<"show-autosuggestion-ansi-suffix", "String">,
    Global,
    DefaultStringValue<"${ansi.normal}">,
    Desc<"When displaying suggestion in a color-enabled terminal, use the ANSI terminal code specified in this format immediately after the suggestion.">;
  def ShowRegexMatchAnsiPrefix: Property<"show-regex-match-ansi-prefix", "String">,
    Global,
    DefaultStringValue<"${ansi.fg.red}">,
    Desc<"When displaying a regex match in a color-enabled terminal, use the ANSI terminal code specified in this format immediately before the match.">;
  def ShowRegexMatchAnsiSuffix: Property<"show-regex-match-ansi-suffix", "String">,
    Global,
    DefaultStringValue<"${ansi.normal}">,
    Desc<"When displaying a regex match in a color-enabled terminal, use the ANSI terminal code specified in this format immediately after the match.">;
  def ShowDontUsePoHint: Property<"show-dont-use-po-hint", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, and object description was requested for a type that does not implement it, LLDB will print a hint telling the user to consider using p instead.">;
  def DWIMPrintVerbosity: Property<"dwim-print-verbosity", "Enum">,
    Global,
    DefaultEnumValue<"eDWIMPrintVerbosityNone">,
    EnumValues<"OptionEnumValues(g_dwim_print_verbosities)">,
    Desc<"The verbosity level used by dwim-print.">;
}
