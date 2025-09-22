include "llvm/Option/OptParser.td"

class F<string letter, string help> : Flag<["-"], letter>, HelpText<help>;
class FF<string name, string help> : Flag<["--"], name>, HelpText<help>;

multiclass BB<string name, string help1, string help2> {
  def NAME: Flag<["--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--"], "no-" # name>, HelpText<help2>;
}

multiclass Eq<string name, string help> {
  def NAME #_EQ : Joined<["--"], name #"=">, HelpText<help>;
  def : Separate<["--"], name>, Alias<!cast<Joined>(NAME #_EQ)>;
}

def X : JoinedOrSeparate<["-"], "X">, HelpText<"Specifies the type of ELF, XCOFF, or IR object file to examine. The value must be one of: 32, 64, 32_64, any (default)">;
def debug_syms : FF<"debug-syms", "Show all symbols, even debugger only">;
def defined_only : FF<"defined-only", "Show only defined symbols">;
defm demangle : BB<"demangle", "Demangle C++ symbol names", "Don't demangle symbol names">;
def dynamic : FF<"dynamic", "Display dynamic symbols instead of normal symbols">;
def export_symbols : FF<"export-symbols", "Export symbol list for all inputs">;
def extern_only : FF<"extern-only", "Show only external symbols">;
defm format : Eq<"format", "Specify output format: bsd (default), posix, sysv, darwin, just-symbols">, MetaVarName<"<format>">;
def help : FF<"help", "Display this help">;
def line_numbers : FF<"line-numbers", "Use debugging information to print symbols' filenames and line numbers">;
def no_llvm_bc : FF<"no-llvm-bc", "Disable LLVM bitcode reader">;
def no_sort : FF<"no-sort", "Show symbols in order encountered">;
def no_weak : FF<"no-weak", "Show only non-weak symbols">;
def numeric_sort : FF<"numeric-sort", "Sort symbols by address">;
def print_armap : FF<"print-armap", "Print the archive map">;
def print_file_name : FF<"print-file-name", "Precede each symbol with the object file it came from">;
def print_size : FF<"print-size", "Show symbol size as well as address">;
def quiet : FF<"quiet", "Suppress 'no symbols' diagnostic">;
defm radix : Eq<"radix", "Radix (o/d/x) for printing symbol Values">, MetaVarName<"<radix>">;
def reverse_sort : FF<"reverse-sort", "Sort in reverse order">;
def size_sort : FF<"size-sort", "Sort symbols by size">;
def special_syms : FF<"special-syms", "Do not filter special symbols from the output">;
def undefined_only : FF<"undefined-only", "Show only undefined symbols">;
def version : FF<"version", "Display the version">;
def without_aliases : FF<"without-aliases", "Exclude aliases from output">, Flags<[HelpHidden]>;

// Mach-O specific options.
def grp_mach_o : OptionGroup<"kind">, HelpText<"llvm-nm Mach-O Specific Options">;

def add_dyldinfo : FF<"add-dyldinfo", "Add symbols from the dyldinfo not already in the symbol table">, Group<grp_mach_o>;
def add_inlinedinfo : FF<"add-inlinedinfo", "Add symbols from the inlined libraries, TBD only">, Group<grp_mach_o>;
def arch_EQ : Joined<["--"], "arch=">, HelpText<"architecture(s) from a Mach-O file to dump">, Group<grp_mach_o>;
def : Separate<["--", "-"], "arch">, Alias<arch_EQ>;
def dyldinfo_only : FF<"dyldinfo-only", "Show only symbols from the dyldinfo">, Group<grp_mach_o>;
def no_dyldinfo : FF<"no-dyldinfo", "Don't add any symbols from the dyldinfo">, Group<grp_mach_o>;
def s : F<"s", "Dump only symbols from this segment and section name">, Group<grp_mach_o>;
def x : F<"x", "Print symbol entry in hex">, Group<grp_mach_o>;

// XCOFF specific options.
def grp_xcoff_o : OptionGroup<"kind">, HelpText<"llvm-nm XCOFF Specific Options">;

def no_rsrc : FF<"no-rsrc", "Exclude resource file symbols (__rsrc) from the export symbol list.">, Group<grp_xcoff_o>;

def : FF<"just-symbol-name", "Alias for --format=just-symbols">, Alias<format_EQ>, AliasArgs<["just-symbols"]>, Flags<[HelpHidden]>;
def : FF<"portability", "Alias for --format=posix">, Alias<format_EQ>, AliasArgs<["posix"]>;

def : F<"a", "Alias for --debug-syms">, Alias<debug_syms>;
def : F<"A", "Alias for --print-file-name">, Alias<print_file_name>;
def : F<"B", "Alias for --format=bsd">, Alias<format_EQ>, AliasArgs<["bsd"]>;
def : F<"C", "Alias for --demangle">, Alias<demangle>;
def : F<"D", "Alias for --dynamic">, Alias<dynamic>;
def : JoinedOrSeparate<["-"], "f">, HelpText<"Alias for --format">, Alias<format_EQ>, MetaVarName<"<format>">;
def : F<"h", "Alias for --help">, Alias<help>;
def : F<"g", "Alias for --extern-only">, Alias<extern_only>;
def : F<"j", "Alias for --format=just-symbols">, Alias<format_EQ>, AliasArgs<["just-symbols"]>;
def : F<"l", "Alias for --line-numbers">, Alias<line_numbers>;
def : F<"m", "Alias for --format=darwin">, Alias<format_EQ>, AliasArgs<["darwin"]>;
def : F<"M", "Deprecated alias for --print-armap">, Alias<print_armap>, Flags<[HelpHidden]>;
def : F<"n", "Alias for --numeric-sort">, Alias<numeric_sort>;
def : F<"o", "Alias for --print-file-name">, Alias<print_file_name>;
def : F<"p", "Alias for --no-sort">, Alias<no_sort>;
def : F<"P", "Alias for --format=posix">, Alias<format_EQ>, AliasArgs<["posix"]>;
def : F<"r", "Alias for --reverse-sort">, Alias<reverse_sort>;
def : F<"S", "Alias for --print-size">, Alias<print_size>;
def : JoinedOrSeparate<["-"], "t">, HelpText<"Alias for --radix">, Alias<radix_EQ>, MetaVarName<"<radix>">;
def : F<"u", "Alias for --undefined-only">, Alias<undefined_only>;
def : F<"U", "Alias for --defined-only">, Alias<defined_only>;
def : F<"v", "Alias for --numeric-sort">, Alias<numeric_sort>;
def : F<"V", "Alias for --version">, Alias<version>;
def : F<"W", "Alias for --no-weak">, Alias<no_weak>;
