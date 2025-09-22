include "llvm/Option/OptParser.td"

class F<string name>: Flag<["--", "-"], name>;

def grp_general : OptionGroup<"Dsymutil">, HelpText<"Dsymutil Options">;

def help: F<"help">,
  HelpText<"Prints this help output.">,
  Group<grp_general>;
def: Flag<["-"], "h">,
  Alias<help>,
  HelpText<"Alias for --help">,
  Group<grp_general>;

def version: F<"version">,
  HelpText<"Prints the dsymutil version.">,
  Group<grp_general>;
def: Flag<["-"], "v">,
  Alias<version>,
  HelpText<"Alias for --version">,
  Group<grp_general>;

def verbose: F<"verbose">,
  HelpText<"Enable verbose mode.">,
  Group<grp_general>;

def quiet: F<"quiet">,
  HelpText<"Enable quiet mode.">,
  Group<grp_general>;
def: Flag<["-"], "q">,
  Alias<quiet>,
  HelpText<"Alias for --quiet">,
  Group<grp_general>;

def keep_func_for_static: F<"keep-function-for-static">,
  HelpText<"Make a static variable keep the enclosing function even if it would have been omitted otherwise.">,
  Group<grp_general>;

def statistics: F<"statistics">,
  HelpText<"Print statistics about the contribution of each object file to "
           "the linked debug info. This prints a table after linking with the "
           "object file name, the size of the debug info in the object file "
           "(in bytes) and the size contributed (in bytes) to the linked dSYM. "
           "The table is sorted by the output size listing the object files "
           "with the largest contribution first.">,
  Group<grp_general>;

def verify: F<"verify">,
  HelpText<"Alias for --verify-dwarf=output">,
  Group<grp_general>;

def verify_dwarf: Separate<["--", "-"], "verify-dwarf">,
  MetaVarName<"<verification mode>">,
  HelpText<"Run the DWARF verifier on the input and/or output. "
           "Valid options are 'none, 'input', 'output', 'all' or 'auto' "
           "which runs the output verifier only if input verification passed.">,
  Group<grp_general>;
def: Joined<["--", "-"], "verify-dwarf=">, Alias<verify_dwarf>;

def no_output: F<"no-output">,
  HelpText<"Do the link in memory, but do not emit the result file.">,
  Group<grp_general>;

def no_swiftmodule_timestamp: F<"no-swiftmodule-timestamp">,
  HelpText<"Don't check timestamp for swiftmodule files.">,
  Group<grp_general>;

def no_odr: F<"no-odr">,
  HelpText<"Do not use ODR (One Definition Rule) for type uniquing.">,
  Group<grp_general>;

def dump_debug_map: F<"dump-debug-map">,
  HelpText<"Parse and dump the debug map to standard output. No DWARF link will take place.">,
  Group<grp_general>;

def yaml_input: Flag<["-", "--"], "y">,
  HelpText<"Treat the input file is a YAML debug map rather than a binary.">,
  Group<grp_general>;

def assembly: Flag<["-", "--"], "S">,
  HelpText<"Output textual assembly instead of a binary dSYM companion file.">,
  Group<grp_general>;

def symtab: F<"symtab">,
  HelpText<"Dumps the symbol table found in executable or object file(s) and exits.">,
  Group<grp_general>;
def: Flag<["-"], "s">,
  Alias<symtab>,
  HelpText<"Alias for --symtab">,
  Group<grp_general>;

def flat: F<"flat">,
  HelpText<"Produce a flat dSYM file (not a bundle).">,
  Group<grp_general>;
def: Flag<["-"], "f">,
  Alias<flat>,
  HelpText<"Alias for --flat">,
  Group<grp_general>;

def fat64: F<"fat64">,
  HelpText<"Use a 64-bit header when emitting universal binaries.">,
  Group<grp_general>;

def update: F<"update">,
  HelpText<"Updates existing dSYM files to contain the latest accelerator tables and other DWARF optimizations.">,
  Group<grp_general>;
def: Flag<["-"], "u">,
  Alias<update>,
  HelpText<"Alias for --update">,
  Group<grp_general>;

def output: Separate<["-", "--"], "o">,
  MetaVarName<"<filename>">,
  HelpText<"Specify the output file. Defaults to <input file>.dwarf">,
  Group<grp_general>;
def: Separate<["--", "-"], "out">,
  MetaVarName<"<filename>">,
  Alias<output>,
  HelpText<"Alias for -o">,
  Group<grp_general>;
def: Joined<["--", "-"], "out=">, Alias<output>;
def: Joined<["-", "--"], "o=">, Alias<output>;

def oso_prepend_path: Separate<["--", "-"], "oso-prepend-path">,
  MetaVarName<"<path>">,
  HelpText<"Specify a directory to prepend to the paths of object files.">,
  Group<grp_general>;
def: Joined<["--", "-"], "oso-prepend-path=">, Alias<oso_prepend_path>;

def object_prefix_map: Separate<["--", "-"], "object-prefix-map">,
    MetaVarName<"<prefix=remapped>">,
    HelpText<"Remap object file paths (but no source paths) before processing."
             "Use this for Clang objects where the module cache location was"
             "remapped using -fdebug-prefix-map; to help dsymutil"
             "find the Clang module cache.">,
    Group<grp_general>;
def: Joined<["--", "-"], "object-prefix-map=">, Alias<object_prefix_map>;

def arch: Separate<["--", "-"], "arch">,
  MetaVarName<"<arch>">,
  HelpText<"Link DWARF debug information only for specified CPU architecture"
           "types. This option can be specified multiple times, once for each"
           "desired architecture. All CPU architectures will be linked by"
           "default.">,
  Group<grp_general>;
def: Joined<["--", "-"], "arch=">, Alias<arch>;

def accelerator: Separate<["--", "-"], "accelerator">,
  MetaVarName<"<accelerator type>">,
  HelpText<"Specify the desired type of accelerator table. Valid options are 'Apple' (.apple_names, .apple_namespaces, .apple_types, .apple_objc), 'Dwarf' (.debug_names), 'Pub' (.debug_pubnames, .debug_pubtypes), 'Default' and 'None'">,
  Group<grp_general>;
def: Joined<["--", "-"], "accelerator=">, Alias<accelerator>;

def toolchain: Separate<["--", "-"], "toolchain">,
  MetaVarName<"<toolchain>">,
  HelpText<"Embed toolchain information in dSYM bundle.">,
  Group<grp_general>;

def threads: Separate<["--", "-"], "num-threads">,
  MetaVarName<"<threads>">,
  HelpText<"Specifies the maximum number of simultaneous threads to use when linking multiple architectures.">,
  Group<grp_general>;
def: Separate<["-"], "j">,
  MetaVarName<"<threads>">,
  HelpText<"Alias for --num-threads">,
  Group<grp_general>;

def reproducer: Separate<["--", "-"], "reproducer">,
  MetaVarName<"<mode>">,
  HelpText<"Specify the reproducer generation mode. Valid options are 'GenerateOnExit', 'GenerateOnCrash', 'Off'.">,
  Group<grp_general>;
def: Joined<["--", "-"], "reproducer=">, Alias<reproducer>;

def gen_reproducer: F<"gen-reproducer">,
  HelpText<"Generate a reproducer consisting of the input object files. Alias for --reproducer=GenerateOnExit.">,
  Group<grp_general>;

def use_reproducer: Separate<["--", "-"], "use-reproducer">,
  MetaVarName<"<path>">,
  HelpText<"Use the object files from the given reproducer path.">,
  Group<grp_general>;
def: Joined<["--", "-"], "use-reproducer=">, Alias<use_reproducer>;

def remarks_prepend_path: Separate<["--", "-"], "remarks-prepend-path">,
  MetaVarName<"<path>">,
  HelpText<"Specify a directory to prepend to the paths of the external remark files.">,
  Group<grp_general>;
def: Joined<["--", "-"], "remarks-prepend-path=">, Alias<remarks_prepend_path>;

def remarks_output_format: Separate<["--", "-"], "remarks-output-format">,
  MetaVarName<"<format>">,
  HelpText<"Specify the format to be used when serializing the linked remarks.">,
  Group<grp_general>;
def: Joined<["--", "-"], "remarks-output-format=">, Alias<remarks_output_format>;

def remarks_drop_without_debug: Flag<["--", "-"], "remarks-drop-without-debug">,
  HelpText<"Drop remarks without valid debug locations. Without this flags, "
           "all remarks are kept.">,
  Group<grp_general>;

def linker: Separate<["--", "-"], "linker">,
  MetaVarName<"<DWARF linker type>">,
  HelpText<"Specify the desired type of DWARF linker. Defaults to 'classic'">,
  Group<grp_general>;
def: Joined<["--", "-"], "linker=">, Alias<linker>;

def build_variant_suffix: Separate<["--", "-"], "build-variant-suffix">,
  MetaVarName<"<suffix=buildvariant>">,
  HelpText<"Specify the build variant suffix used to build the executabe file.">,
  Group<grp_general>;
def: Joined<["--", "-"], "build-variant-suffix=">, Alias<build_variant_suffix>;

def dsym_search_path: Separate<["-", "--"], "D">,
  MetaVarName<"<path>">,
  HelpText<"Specify a directory that contain dSYM files to search for.">,
  Group<grp_general>;
