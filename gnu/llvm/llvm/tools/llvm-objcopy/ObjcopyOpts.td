include "CommonOpts.td"

multiclass B<string name, string help1, string help2> {
  def NAME: Flag<["--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--"], "no-" # name>, HelpText<help2>;
}

defm binary_architecture
    : Eq<"binary-architecture", "Ignored for compatibility">;
def B : JoinedOrSeparate<["-"], "B">,
        Alias<binary_architecture>,
        HelpText<"Alias for --binary-architecture">;

defm target : Eq<"target", "Equivalent to --input-target and --output-target for the specified format">,
              Values<"binary">;
def F : JoinedOrSeparate<["-"], "F">,
        Alias<target>,
        HelpText<"Alias for --target">;

defm input_target : Eq<"input-target", "Read the input as the specified format">, MetaVarName<"format">;
def I : JoinedOrSeparate<["-"], "I">,
        Alias<input_target>,
        HelpText<"Alias for --input-target">;

defm output_target : Eq<"output-target", "Write the output as the specified format">, MetaVarName<"format">;
def O : JoinedOrSeparate<["-"], "O">,
        Alias<output_target>,
        HelpText<"Alias for --output-target">;

defm new_symbol_visibility
    : Eq<"new-symbol-visibility", "Specify the visibility of symbols automatically "
         "created when using binary input or --add-symbol. The default is 'default'">;

def compress_debug_sections
    : Joined<["--"], "compress-debug-sections=">,
      MetaVarName<"format">,
      HelpText<"Compress DWARF debug sections using specified format. Supported "
               "formats: zlib, zstd. Select zlib if <format> is omitted">;
def : Flag<["--"], "compress-debug-sections">, Alias<compress_debug_sections>,
      AliasArgs<["zlib"]>;
def decompress_debug_sections : Flag<["--"], "decompress-debug-sections">,
                                HelpText<"Decompress DWARF debug sections">;
defm compress_sections
    : Eq<"compress-sections",
         "Compress or decompress sections using specified format. Supported "
         "formats: zlib, zstd. Specify 'none' for decompression">,
      MetaVarName<"<section-glob>=<format>">;

defm split_dwo
    : Eq<"split-dwo", "Equivalent to --extract-dwo and <dwo-file> as the output file and no other options, "
                      "and then --strip-dwo on the input file">,
      MetaVarName<"dwo-file">;

defm add_gnu_debuglink
    : Eq<"add-gnu-debuglink", "Add a .gnu_debuglink for <debug-file>">,
      MetaVarName<"debug-file">;

defm rename_section
    : Eq<"rename-section",
         "Rename sections called <old> to <new>, and apply any specified <flag> values. "
         "See --set-section-flags for a list of supported flags">,
      MetaVarName<"old=new[,flag1,...]">;
defm redefine_symbol
    : Eq<"redefine-sym", "Change the name of a symbol old to new">,
      MetaVarName<"old=new">;
defm redefine_symbols
    : Eq<"redefine-syms",
         "Read a list of symbol pairs from <filename> and run as if "
         "--redefine-sym=<old>=<new> is set for each one. <filename> "
         "contains two symbols per line separated with whitespace and may "
         "contain comments beginning with '#'. Leading and trailing "
         "whitespace is stripped from each line. May be repeated to read "
         "symbols from many files">,         
      MetaVarName<"filename">;

defm only_section : Eq<"only-section", "Remove all but <section>">,
                    MetaVarName<"section">;
def j : JoinedOrSeparate<["-"], "j">,
        Alias<only_section>,
        HelpText<"Alias for --only-section">;
defm add_section
    : Eq<"add-section",
         "Add a section named <section> with the contents of <file>">,
      MetaVarName<"section=file">;

defm set_section_alignment
    : Eq<"set-section-alignment", "Set alignment for a given section">,
      MetaVarName<"section=align">;

defm set_section_flags
    : Eq<"set-section-flags",
         "Set section properties based on the specified <flags>. Supported flag names are: "
         "alloc, load, noload, readonly, exclude, debug, code, "
         "data, rom, share, contents, merge, strings, large">,
      MetaVarName<"section=flag1[,flag2,...]">;

defm set_section_type
    : Eq<"set-section-type",
         "Set the type of section <section> to the integer <type>">,
      MetaVarName<"section=type">;

defm set_symbol_visibility
    : Eq<"set-symbol-visibility",
         "Change the visibility of a symbol to the specified value">,
      MetaVarName<"symbol=visibility">;
defm set_symbols_visibility
    : Eq<"set-symbols-visibility",
         "Read a list of symbols from <filename> and change their "
         "visibility to the specified value. Visibility values: default, "
         "internal, hidden, protected">,
      MetaVarName<"filename=visibility">;

def S : Flag<["-"], "S">,
        Alias<strip_all>,
        HelpText<"Alias for --strip-all">;
def strip_dwo : Flag<["--"], "strip-dwo">,
                HelpText<"Remove all DWARF .dwo sections">;
def strip_non_alloc
    : Flag<["--"], "strip-non-alloc">,
      HelpText<"Remove all non-allocated sections that are not within segments">;
defm strip_unneeded_symbol
    : Eq<"strip-unneeded-symbol",
         "Remove all symbols named <symbol> that are local or undefined and "
         "are not required by any relocation">,
      MetaVarName<"symbol">;
defm strip_unneeded_symbols
    : Eq<"strip-unneeded-symbols",
         "Remove all symbols whose names appear in the file <file>, if they "
         "are local or undefined and are not required by any relocation">,
      MetaVarName<"filename">;

defm subsystem
    : Eq<"subsystem",
         "Set the PE subsystem, and optionally subsystem version">,
      MetaVarName<"name[:version]">, Group<grp_coff>;

def extract_dwo
    : Flag<["--"], "extract-dwo">,
      HelpText<
          "Remove all sections that are not DWARF .dwo sections from file">;

defm extract_partition
    : Eq<"extract-partition", "Extract named partition from input file">,
      MetaVarName<"name">;
def extract_main_partition
    : Flag<["--"], "extract-main-partition">,
      HelpText<"Extract main partition from the input file">;

def localize_hidden
    : Flag<["--"], "localize-hidden">,
      HelpText<
          "Mark all symbols that have hidden or internal visibility as local">;
defm localize_symbol
    : Eq<"localize-symbol", "Mark any defined non-common symbol named <symbol> as local">,
      MetaVarName<"symbol">;
defm localize_symbols
    : Eq<"localize-symbols",
         "Read a list of names from <filename> and mark any defined non-common "
         "symbols with those names as local">,
      MetaVarName<"filename">;

def L : JoinedOrSeparate<["-"], "L">,
        Alias<localize_symbol>,
        HelpText<"Alias for --localize-symbol">;

defm globalize_symbol : Eq<"globalize-symbol", "Mark <symbol> as global">,
                        MetaVarName<"symbol">;

defm globalize_symbols
    : Eq<"globalize-symbols",
         "Read a list of symbols from <filename> and mark defined symbols"
         " with those names as global">,
      MetaVarName<"filename">;

defm keep_global_symbol
    : Eq<"keep-global-symbol",
         "Mark all symbols local, except for symbols with the name <symbol>. "
         "Can be specified multiple times to ignore multiple symbols">,
      MetaVarName<"symbol">;
def G : JoinedOrSeparate<["-"], "G">,
        Alias<keep_global_symbol>,
        HelpText<"Alias for --keep-global-symbol">;

defm verify_note_sections : B<"verify-note-sections",
                              "Validate note sections",
                              "Don't validate note sections">;

defm keep_global_symbols
    : Eq<"keep-global-symbols",
         "Read a list of symbols from <filename> and run as if "
         "--keep-global-symbol=<symbol> is set for each one. <filename> "
         "contains one symbol per line and may contain comments beginning with "
         "'#'. Leading and trailing whitespace is stripped from each line. May "
         "be repeated to read symbols from many files">,
      MetaVarName<"filename">;

defm weaken_symbol : Eq<"weaken-symbol", "Mark global symbols named <symbol> as weak">,
                     MetaVarName<"symbol">;
defm weaken_symbols
    : Eq<"weaken-symbols",
         "Read a list of symbols from <filename> and mark global symbols with those names as weak">,
      MetaVarName<"filename">;

def W : JoinedOrSeparate<["-"], "W">,
        Alias<weaken_symbol>,
        HelpText<"Alias for --weaken-symbol">;
def weaken : Flag<["--"], "weaken">,
             HelpText<"Mark all defined global symbols as weak">;

defm strip_symbols
    : Eq<"strip-symbols",
         "Remove all symbols whose names appear in the file <filename>">,
      MetaVarName<"filename">;

defm keep_symbols
    : Eq<"keep-symbols",
         "Read a list of symbols from <filename> and run as if "
         "--keep-symbol=<symbol> is set for each one. <filename> "
         "contains one symbol per line and may contain comments beginning with "
         "'#'. Leading and trailing whitespace is stripped from each line. May "
         "be repeated to read symbols from many files">,
      MetaVarName<"filename">;

defm skip_symbol : Eq<"skip-symbol", "Do not change parameters of symbol <symbol> "
                        "when executing other options that can change the symbol's "
                        "name, binding or visibility">,
                     MetaVarName<"symbol">;

defm skip_symbols
    : Eq<"skip-symbols",
         "Read a list of symbols from <filename> and run as if "
         "--skip-symbol=<symbol> is set for each one. <filename> "
         "contains one symbol per line and may contain comments beginning with "
         "'#'. Leading and trailing whitespace is stripped from each line. May "
         "be repeated to read symbols from many files">,
      MetaVarName<"filename">;

defm dump_section
    : Eq<"dump-section",
         "Dump contents of section named <section> into file <file>">,
      MetaVarName<"section=file">;
defm prefix_symbols
    : Eq<"prefix-symbols", "Add <prefix> to the start of every symbol name">,
      MetaVarName<"prefix">;
defm remove_symbol_prefix
    : Eq<"remove-symbol-prefix",
         "Remove <prefix> from the start of every symbol name. No-op for symbols that do not start "
         "with <prefix>">,
      MetaVarName<"prefix">;

defm prefix_alloc_sections
    : Eq<"prefix-alloc-sections", "Add <prefix> to the start of every allocated section name">,
      MetaVarName<"prefix">;

defm set_start : Eq<"set-start", "Set the start address to <addr>. Overrides "
                    "any previous --change-start or --adjust-start values">,
                 MetaVarName<"addr">;
defm change_start : Eq<"change-start", "Add <incr> to the start address. Can be "                        
                       "specified multiple times, all values will be applied "
                       "cumulatively">,
                    MetaVarName<"incr">;
def adjust_start : JoinedOrSeparate<["--"], "adjust-start">,
                   Alias<change_start>,
                   HelpText<"Alias for --change-start">;

defm change_section_lma
    : Eq<"change-section-lma", "Shift LMA of non-zero-sized sections in the program header by <val>">,
      MetaVarName<"*{+|-}val">;

defm add_symbol
    : Eq<"add-symbol", "Add new symbol <name> to .symtab. Accepted flags: "
         "global, local, weak, default, hidden, protected, file, section, object, "
         "function, indirect-function. Accepted but ignored for "
         "compatibility: debug, constructor, warning, indirect, synthetic, "
         "unique-object, before">,
      MetaVarName<"name=[section:]value[,flags]">;

defm update_section
    : Eq<"update-section", "Replace the contents of section <name> with contents from a file <file>">,
      MetaVarName<"name=file">;

defm gap_fill
    : Eq<"gap-fill", "Fill the gaps between sections with <value> instead of zero. "
                     "<value> must be an unsigned 8-bit integer. "
                     "This option is only supported for ELF input and binary output">,
      MetaVarName<"value">;

defm pad_to
    : Eq<"pad-to", "Pad the output up to the load address <address>, using a value "
                   "of zero or the value specified by the --gap-fill option. "
                   "This option is only supported for ELF input and binary output">,
      MetaVarName<"address">;
