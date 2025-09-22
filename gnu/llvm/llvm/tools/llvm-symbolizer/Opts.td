include "llvm/Option/OptParser.td"

multiclass B<string name, string help1, string help2> {
  def NAME: Flag<["--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--"], "no-" # name>, HelpText<help2>;
}

multiclass Eq<string name, string help> {
  def NAME #_EQ : Joined<["--"], name #"=">,
                  HelpText<help>;
  def : Separate<["--"], name>, Alias<!cast<Joined>(NAME #_EQ)>;
}

class F<string name, string help>: Flag<["--"], name>, HelpText<help>;

def grp_mach_o : OptionGroup<"kind">,
                 HelpText<"llvm-symbolizer Mach-O Specific Options">;

def addresses : F<"addresses", "Show address before line information">;
defm adjust_vma
    : Eq<"adjust-vma", "Add specified offset to object file addresses">,
      MetaVarName<"<offset>">;
def basenames : Flag<["--"], "basenames">, HelpText<"Strip directory names from paths">;
defm build_id : Eq<"build-id", "Build ID used to look up the object file">;
defm cache_size : Eq<"cache-size", "Max size in bytes of the in-memory binary cache.">;
def color : F<"color", "Use color when symbolizing log markup.">;
def color_EQ : Joined<["--"], "color=">, HelpText<"Whether to use color when symbolizing log markup: always, auto, never">, Values<"always,auto,never">;
defm debug_file_directory : Eq<"debug-file-directory", "Path to directory where to look for debug files">, MetaVarName<"<dir>">;
defm debuginfod : B<"debuginfod", "Use debuginfod to find debug binaries", "Don't use debuginfod to find debug binaries">;
defm default_arch
    : Eq<"default-arch", "Default architecture (for multi-arch objects)">,
      Group<grp_mach_o>;
defm demangle : B<"demangle", "Demangle function names", "Don't demangle function names">;
def filter_markup : Flag<["--"], "filter-markup">, HelpText<"Filter symbolizer markup from stdin.">;
def functions : F<"functions", "Print function name for a given address">;
def functions_EQ : Joined<["--"], "functions=">, HelpText<"Print function name for a given address">, Values<"none,short,linkage">;
def help : F<"help", "Display this help">;
defm dwp : Eq<"dwp", "Path to DWP file to be use for any split CUs">, MetaVarName<"<file>">;
defm dsym_hint
    : Eq<"dsym-hint",
         "Path to .dSYM bundles to search for debug info for the object files">,
      MetaVarName<"<dir>">,
      Group<grp_mach_o>;
defm fallback_debug_path : Eq<"fallback-debug-path", "Fallback path for debug binaries">, MetaVarName<"<dir>">;
defm inlines : B<"inlines", "Print all inlined frames for a given address",
                 "Do not print inlined frames">;
defm obj
    : Eq<"obj", "Path to object file to be symbolized (if not provided, "
                "object file should be specified for each input line)">, MetaVarName<"<file>">;
defm output_style
    : Eq<"output-style", "Specify print style. Supported styles: LLVM, GNU, JSON">,
      MetaVarName<"style">,
      Values<"LLVM,GNU,JSON">;
def pretty_print : F<"pretty-print", "Make the output more human friendly">;
defm print_source_context_lines : Eq<"print-source-context-lines", "Print N lines of source file context">;
def relative_address : F<"relative-address", "Interpret addresses as addresses relative to the image base">;
def relativenames : F<"relativenames", "Strip the compilation directory from paths">;
defm untag_addresses : B<"untag-addresses", "", "Remove memory tags from addresses before symbolization">;
def use_dia: F<"dia", "Use the DIA library to access symbols (Windows only)">;
def verbose : F<"verbose", "Print verbose line info">;
def version : F<"version", "Display the version">;

def : Flag<["-"], "a">, Alias<addresses>, HelpText<"Alias for --addresses">;
def : F<"print-address", "Alias for --addresses">, Alias<addresses>;
def : Flag<["-"], "C">, Alias<demangle>, HelpText<"Alias for --demangle">;
def : Joined<["--"], "exe=">, Alias<obj_EQ>, HelpText<"Alias for --obj">, MetaVarName<"<file>">;
def : Separate<["--"], "exe">, Alias<obj_EQ>, HelpText<"Alias for --obj">, MetaVarName<"<file>">;
def : JoinedOrSeparate<["-"], "e">, Alias<obj_EQ>, HelpText<"Alias for --obj">, MetaVarName<"<file>">;
def : Joined<["-"], "e=">, Alias<obj_EQ>, HelpText<"Alias for --obj">, MetaVarName<"<file>">;
def : Flag<["-"], "f">, Alias<functions>, HelpText<"Alias for --functions">;
def : Joined<["-"], "f=">, Alias<functions_EQ>, HelpText<"Alias for --functions=">;
def : Flag<["-"], "h">, Alias<help>;
def : Flag<["-"], "i">, Alias<inlines>, HelpText<"Alias for --inlines">;
def : F<"inlining", "Alias for --inlines">, Alias<inlines>;
def : Flag<["-"], "p">, Alias<pretty_print>, HelpText<"Alias for --pretty-print">;
def : Flag<["-"], "s">, Alias<basenames>, HelpText<"Alias for --basenames">;
def : Flag<["-"], "v">, Alias<version>, HelpText<"Alias for --version">;

// Compatibility aliases for old asan_symbolize.py and sanitizer binaries (before 2020-08).
def : Flag<["--"], "inlining=true">, Alias<inlines>, HelpText<"Alias for --inlines">;
def : Flag<["--"], "inlining=false">, Alias<no_inlines>, HelpText<"Alias for --no-inlines">;
// Compatibility aliases for pprof's symbolizer.
def : Flag<["-"], "demangle=true">, Alias<demangle>, HelpText<"Alias for --demangle">;
def : Flag<["-"], "demangle=false">, Alias<no_demangle>, HelpText<"Alias for --no-demangle">;
