include "llvm/Option/OptParser.td"

multiclass BB<string name, string help1, string help2> {
  def NAME: Flag<["--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--"], "no-" # name>, HelpText<help2>;
}

// Flags that lld/MachO understands but ld64 doesn't. These take
// '--' instead of '-' and use dashes instead of underscores, so
// they don't collide with the ld64 compat options.
def grp_lld : OptionGroup<"kind">, HelpText<"LLD-SPECIFIC">;

def help : Flag<["-", "--"], "help">,
    Group<grp_lld>;
def help_hidden : Flag<["--"], "help-hidden">,
    HelpText<"Display help for hidden options">,
    Group<grp_lld>;
def verbose : Flag<["--"], "verbose">,
    Group<grp_lld>;
def error_limit_eq : Joined<["--"], "error-limit=">,
    HelpText<"Maximum number of errors to print before exiting (default: 20)">,
    Group<grp_lld>;
def color_diagnostics: Flag<["--"], "color-diagnostics">,
    HelpText<"Alias for --color-diagnostics=always">,
    Group<grp_lld>;
def no_color_diagnostics: Flag<["--"], "no-color-diagnostics">,
    HelpText<"Alias for --color-diagnostics=never">,
    Group<grp_lld>;
def color_diagnostics_eq: Joined<["--"], "color-diagnostics=">,
    HelpText<"Use colors in diagnostics (default: auto)">,
    MetaVarName<"[auto,always,never]">,
    Group<grp_lld>;
def threads_eq : Joined<["--"], "threads=">,
    HelpText<"Number of threads. '1' disables multi-threading. By default all available hardware threads are used">,
    Group<grp_lld>;
def thinlto_emit_imports_files: Flag<["--"], "thinlto-emit-imports-files">,
    Group<grp_lld>;
def thinlto_emit_index_files: Flag<["--"], "thinlto-emit-index-files">,
    Group<grp_lld>;
def thinlto_index_only: Flag<["--"], "thinlto-index-only">,
    Group<grp_lld>;
def thinlto_index_only_eq: Joined<["--"], "thinlto-index-only=">,
    Group<grp_lld>;
def thinlto_jobs_eq : Joined<["--"], "thinlto-jobs=">,
    HelpText<"Number of ThinLTO jobs. Default to --threads=">,
    Group<grp_lld>;
def thinlto_object_suffix_replace_eq:
    Joined<["--"], "thinlto-object-suffix-replace=">,
    Group<grp_lld>;
def thinlto_prefix_replace_eq: Joined<["--"], "thinlto-prefix-replace=">,
    Group<grp_lld>;
def reproduce: Separate<["--"], "reproduce">,
    Group<grp_lld>;
def reproduce_eq: Joined<["--"], "reproduce=">,
    Alias<!cast<Separate>(reproduce)>,
    HelpText<"Write tar file containing inputs and command to reproduce link">,
    Group<grp_lld>;
def version: Flag<["--"], "version">,
    HelpText<"Display the version number and exit">,
    Group<grp_lld>;
def time_trace_eq: Joined<["--"], "time-trace=">,
    HelpText<"Record time trace to <file>">,
    MetaVarName<"<file>">,
    Group<grp_lld>;
def : Flag<["--"], "time-trace">,
    Alias<time_trace_eq>,
    HelpText<"Record time trace to file next to output">,
    Group<grp_lld>;
def time_trace_granularity_eq: Joined<["--"], "time-trace-granularity=">,
    HelpText<"Minimum time granularity (in microseconds) traced by time profiler">,
    Group<grp_lld>;
def deduplicate_strings: Flag<["--"], "deduplicate-strings">,
    HelpText<"Enable string deduplication">,
    Group<grp_lld>;
def no_deduplicate_strings: Flag<["--"], "no-deduplicate-strings">,
    HelpText<"Disable string deduplication. This helps uncover cases of comparing string addresses instead of equality and might have a link time performance benefit.">,
    Group<grp_lld>;
def dead_strip_duplicates: Flag<["--"], "dead-strip-duplicates">,
    HelpText<"Do not error on duplicate symbols that will be dead stripped.">,
    Group<grp_lld>;
def print_dylib_search: Flag<["--"], "print-dylib-search">,
    HelpText<"Print which paths lld searched when trying to find dylibs">,
    Group<grp_lld>;
def icf_eq: Joined<["--"], "icf=">,
    HelpText<"Set level for identical code folding (default: none)">,
    MetaVarName<"[none,safe,all]">,
    Group<grp_lld>;
def keep_icf_stabs: Joined<["--"], "keep-icf-stabs">,
    HelpText<"Generate STABS entries for symbols folded by ICF. These entries can then be used by dsymutil to discover the address range where folded symbols are located.">,
    Group<grp_lld>;
def lto_O: Joined<["--"], "lto-O">,
    HelpText<"Set optimization level for LTO (default: 2)">,
    MetaVarName<"<opt-level>">,
    Group<grp_lld>;
def lto_CGO: Joined<["--"], "lto-CGO">,
    HelpText<"Set codegen optimization level for LTO (default: 2)">,
    MetaVarName<"<cgopt-level>">,
    Group<grp_lld>;
def thinlto_cache_policy_eq: Joined<["--"], "thinlto-cache-policy=">,
    HelpText<"Pruning policy for the ThinLTO cache">,
    Group<grp_lld>;
def O : JoinedOrSeparate<["-"], "O">,
    HelpText<"Optimize output file size">;
def start_lib: Flag<["--"], "start-lib">,
    HelpText<"Start a grouping of objects that should be treated as if they were together in an archive">;
def end_lib: Flag<["--"], "end-lib">,
    HelpText<"End a grouping of objects that should be treated as if they were together in an archive">;
def no_warn_dylib_install_name: Flag<["--"], "no-warn-dylib-install-name">,
    HelpText<"Do not warn on -install_name if -dylib is not passed (default)">,
    Group<grp_lld>;
def warn_dylib_install_name: Flag<["--"], "warn-dylib-install-name">,
    HelpText<"Warn on -install_name if -dylib is not passed">,
    Group<grp_lld>;
def warn_duplicate_rpath: Flag<["--"], "warn-duplicate-rpath">,
    HelpText<"Warn if the same -rpath is specified multiple times (default)">,
    Group<grp_lld>;
def no_warn_duplicate_rpath: Flag<["--"], "no-warn-duplicate-rpath">,
    HelpText<"Do not warn if the same -rpath is specified multiple times">,
    Group<grp_lld>;
def call_graph_profile_sort: Flag<["--"], "call-graph-profile-sort">,
    HelpText<"Reorder sections with call graph profile (default)">,
    Group<grp_lld>;
def no_call_graph_profile_sort : Flag<["--"], "no-call-graph-profile-sort">,
    HelpText<"Do not reorder sections with call graph profile">,
    Group<grp_lld>;
def print_symbol_order_eq: Joined<["--"], "print-symbol-order=">,
    HelpText<"Print a symbol order specified by --call-graph-profile-sort into the specified file">,
    Group<grp_lld>;
def ignore_auto_link_option : Separate<["--"], "ignore-auto-link-option">,
    Group<grp_lld>;
def ignore_auto_link_option_eq : Joined<["--"], "ignore-auto-link-option=">,
    Alias<!cast<Separate>(ignore_auto_link_option)>,
    HelpText<"Ignore a single auto-linked library or framework. Useful to ignore invalid options that ld64 ignores">,
    Group<grp_lld>;
def strict_auto_link : Flag<["--"], "strict-auto-link">,
    HelpText<"Always warn for missing frameworks or libraries if they are loaded via LC_LINKER_OPTIONS">,
    Group<grp_lld>;
def check_category_conflicts : Flag<["--"], "check-category-conflicts">,
    HelpText<"Check for conflicts between category & class methods">,
    Group<grp_lld>;
def objc_category_merging : Flag<["-"], "objc_category_merging">,
    HelpText<"Merge Objective-C categories that share the same base class">,
    Group<grp_lld>;
def no_objc_category_merging : Flag<["-"], "no_objc_category_merging">,
    HelpText<"Do not merge Objective-C categories">,
    Group<grp_lld>;
def lto_debug_pass_manager: Flag<["--"], "lto-debug-pass-manager">,
    HelpText<"Debug new pass manager">, Group<grp_lld>;
def cs_profile_generate: Flag<["--"], "cs-profile-generate">,
    HelpText<"Perform context sensitive PGO instrumentation">, Group<grp_lld>;
def cs_profile_path: Joined<["--"], "cs-profile-path=">,
    HelpText<"Context sensitive profile file path">, Group<grp_lld>;
defm pgo_warn_mismatch: BB<"pgo-warn-mismatch",
  "turn on warnings about profile cfg mismatch (default)",
  "turn off warnings about profile cfg mismatch">, Group<grp_lld>;
defm warn_thin_archive_missing_members : BB<"warn-thin-archive-missing-members",
  "Warn on missing object files referenced by thin archives (default)",
  "Do not warn on missing object files referenced by thin archives">, Group<grp_lld>;

// This is a complete Options.td compiled from Apple's ld(1) manpage
// dated 2018-03-07 and cross checked with ld64 source code in repo
// https://github.com/apple-opensource/ld64 at git tag "512.4" dated
// 2018-03-18.

// Flags<[HelpHidden]> marks options that are not yet ported to lld,
// and serve as a scoreboard for annotating our progress toward
// implementing ld64 options in lld. As you add implementions to
// Driver.cpp, please remove the hidden flag here.

def grp_kind : OptionGroup<"kind">, HelpText<"OUTPUT KIND">;

def execute : Flag<["-"], "execute">,
    HelpText<"Produce a main executable (default)">,
    Group<grp_kind>;
def dylib : Flag<["-"], "dylib">,
    HelpText<"Produce a shared library">,
    Group<grp_kind>;
def bundle : Flag<["-"], "bundle">,
    HelpText<"Produce a bundle">,
    Group<grp_kind>;
def r : Flag<["-"], "r">,
    HelpText<"Merge multiple object files into one, retaining relocations">,
    Flags<[HelpHidden]>,
    Group<grp_kind>;
def dylinker : Flag<["-"], "dylinker">,
    HelpText<"Produce a dylinker only used when building dyld">,
    Flags<[HelpHidden]>,
    Group<grp_kind>;
def dynamic : Flag<["-"], "dynamic">,
    HelpText<"Link dynamically (default)">,
    Group<grp_kind>;
def static : Flag<["-"], "static">,
    HelpText<"Link statically">,
    Flags<[HelpHidden]>,
    Group<grp_kind>;
def preload : Flag<["-"], "preload">,
    HelpText<"Produce an unsegmented binary for embedded systems">,
    Flags<[HelpHidden]>,
    Group<grp_kind>;
def arch : Separate<["-"], "arch">,
    MetaVarName<"<arch_name>">,
    HelpText<"The architecture (e.g. ppc, ppc64, i386, x86_64)">,
    Group<grp_kind>;
def o : Separate<["-"], "o">,
    MetaVarName<"<path>">,
    HelpText<"The name of the output file (default: `a.out')">,
    Group<grp_kind>;

def grp_libs : OptionGroup<"libs">, HelpText<"LIBRARIES">;

def l : Joined<["-"], "l">,
    MetaVarName<"<name>">,
    HelpText<"Search for lib<name>.dylib or lib<name>.a on the library search path">,
    Group<grp_libs>;
def weak_l : Joined<["-"], "weak-l">,
    MetaVarName<"<name>">,
    HelpText<"Like -l<name>, but mark library and its references as weak imports">,
    Group<grp_libs>;
def weak_library : Separate<["-"], "weak_library">,
    MetaVarName<"<path>">,
    HelpText<"Like bare <path>, but mark library and its references as weak imports">,
    Group<grp_libs>;
def needed_l : Joined<["-"], "needed-l">,
    MetaVarName<"<name>">,
    HelpText<"Like -l<name>, but link library even if its symbols are not used and -dead_strip_dylibs is active">,
    Group<grp_libs>;
def needed_library : Separate<["-"], "needed_library">,
    MetaVarName<"<path>">,
    HelpText<"Like bare <path>, but link library even if its symbols are not used and -dead_strip_dylibs is active">,
    Group<grp_libs>;
def reexport_l : Joined<["-"], "reexport-l">,
    MetaVarName<"<name>">,
    HelpText<"Like -l<name>, but export all symbols of <name> from newly created library">,
    Group<grp_libs>;
def reexport_library : Separate<["-"], "reexport_library">,
    MetaVarName<"<path>">,
    HelpText<"Like bare <path>, but export all symbols of <path> from newly created library">,
    Group<grp_libs>;
def upward_l : Joined<["-"], "upward-l">,
    MetaVarName<"<name>">,
    HelpText<"Like -l<name>, but specify dylib as an upward dependency">,
    Flags<[HelpHidden]>,
    Group<grp_libs>;
def upward_library : Separate<["-"], "upward_library">,
    MetaVarName<"<path>">,
    HelpText<"Like bare <path>, but specify dylib as an upward dependency">,
    Flags<[HelpHidden]>,
    Group<grp_libs>;
def L : JoinedOrSeparate<["-"], "L">,
    MetaVarName<"<dir>">,
    HelpText<"Add dir to the library search path">,
    Group<grp_libs>;
def Z : Flag<["-"], "Z">,
    HelpText<"Remove standard directories from the library and framework search paths">,
    Group<grp_libs>;
def syslibroot : Separate<["-"], "syslibroot">,
    MetaVarName<"<rootdir>">,
    HelpText<"Prepend <rootdir> to all library and framework search paths">,
    Group<grp_libs>;
def search_paths_first : Flag<["-"], "search_paths_first">,
    HelpText<"Search for lib<name>.dylib and lib<name>.a at each step in traversing search path (default for Xcode 4 and later)">,
    Group<grp_libs>;
def search_dylibs_first : Flag<["-"], "search_dylibs_first">,
    HelpText<"Search for lib<name>.dylib on first pass, then for lib<name>.a on second pass through search path (default for Xcode 3 and earlier)">,
    Group<grp_libs>;
def framework : Separate<["-"], "framework">,
    MetaVarName<"<name>">,
    HelpText<"Search for <name>.framework/<name> on the framework search path">,
    Group<grp_libs>;
def weak_framework : Separate<["-"], "weak_framework">,
    MetaVarName<"<name>">,
    HelpText<"Like -framework <name>, but mark framework and its references as weak imports">,
    Group<grp_libs>;
def needed_framework : Separate<["-"], "needed_framework">,
    MetaVarName<"<name>">,
    HelpText<"Like -framework <name>, but link <name> even if none of its symbols are used and -dead_strip_dylibs is active">,
    Group<grp_libs>;
def reexport_framework : Separate<["-"], "reexport_framework">,
    MetaVarName<"<name>">,
    HelpText<"Like -framework <name>, but export all symbols of <name> from the newly created library">,
    Group<grp_libs>;
def upward_framework : Separate<["-"], "upward_framework">,
    MetaVarName<"<name>">,
    HelpText<"Like -framework <name>, but specify the framework as an upward dependency">,
    Flags<[HelpHidden]>,
    Group<grp_libs>;
def F : JoinedOrSeparate<["-"], "F">,
    MetaVarName<"<dir>">,
    HelpText<"Add dir to the framework search path">,
    Group<grp_libs>;
def all_load : Flag<["-"], "all_load">,
    HelpText<"Load all members of all static archive libraries">,
    Group<grp_libs>;
def noall_load : Flag<["-"], "noall_load">,
    HelpText<"Don't load all static members from archives, this is the default, this negates -all_load">,
    Group<grp_libs>;
def ObjC : Flag<["-"], "ObjC">,
    HelpText<"Load all members of static archives that are an Objective-C class or category.">,
    Group<grp_libs>;
def force_load : Separate<["-"], "force_load">,
    MetaVarName<"<path>">,
    HelpText<"Load all members static archive library at <path>">,
    Group<grp_libs>;
def force_load_swift_libs : Flag<["-"], "force_load_swift_libs">,
    HelpText<"Apply -force_load to libraries listed in LC_LINKER_OPTIONS whose names start with 'swift'">,
    Group<grp_libs>;
def load_hidden : Separate<["-"], "load_hidden">,
    MetaVarName<"<path>">,
    HelpText<"Load all symbols from static library with hidden visibility">,
    Group<grp_libs>;
def hidden_l : Joined<["-"], "hidden-l">,
    MetaVarName<"<name>">,
    HelpText<"Like -l<name>, but load all symbols with hidden visibility">,
    Group<grp_libs>;

def grp_content : OptionGroup<"content">, HelpText<"ADDITIONAL CONTENT">;

def sectcreate : MultiArg<["-"], "sectcreate", 3>,
    MetaVarName<"<segment> <section> <file>">,
    HelpText<"Create <section> in <segment> from the contents of <file>">,
    Group<grp_content>;
def segcreate : MultiArg<["-"], "segcreate", 3>,
    MetaVarName<"<segment> <section> <file>">,
    Alias<sectcreate>,
    HelpText<"Alias for -sectcreate">,
    Group<grp_content>;
def add_empty_section : MultiArg<["-"], "add_empty_section", 2>,
    MetaVarName<"<segment> <section>">,
    HelpText<"Create an empty <section> in <segment>">,
    Group<grp_content>;
def filelist : Separate<["-"], "filelist">,
    MetaVarName<"<file>">,
    HelpText<"Read names of files to link from <file>">,
    Group<grp_content>;
def dtrace : Separate<["-"], "dtrace">,
    MetaVarName<"<script>">,
    HelpText<"Enable DTrace static probes according to declarations in <script>">,
    Flags<[HelpHidden]>,
    Group<grp_content>;

def grp_opts : OptionGroup<"opts">, HelpText<"OPTIMIZATIONS">;

def dead_strip : Flag<["-"], "dead_strip">,
    HelpText<"Remove unreachable functions and data">,
    Group<grp_opts>;
def order_file : Separate<["-"], "order_file">,
    MetaVarName<"<file>">,
    HelpText<"Layout functions and data according to specification in <file>">,
    Group<grp_opts>;
def no_order_inits : Flag<["-"], "no_order_inits">,
    HelpText<"Disable default reordering of initializer and terminator functions">,
    Flags<[HelpHidden]>,
    Group<grp_opts>;
def no_order_data : Flag<["-"], "no_order_data">,
    HelpText<"Disable default reordering of global data accessed at launch time">,
    Flags<[HelpHidden]>,
    Group<grp_opts>;
def image_base : Separate<["-"], "image_base">,
    MetaVarName<"<address>">,
    HelpText<"Preferred hex load address for a dylib or bundle.">,
    Flags<[HelpHidden]>,
    Group<grp_opts>;
def seg1addr : Separate<["-"], "seg1addr">,
    MetaVarName<"<address>">,
    Alias<image_base>,
    HelpText<"Alias for -image_base">,
    Flags<[HelpHidden]>,
    Group<grp_opts>;
def no_implicit_dylibs : Flag<["-"], "no_implicit_dylibs">,
    HelpText<"Do not optimize public dylib transitive symbol references">,
    Group<grp_opts>;
def exported_symbols_order : Separate<["-"], "exported_symbols_order">,
    MetaVarName<"<file>">,
    HelpText<"Specify frequently-used symbols in <file> to optimize symbol exports">,
    Flags<[HelpHidden]>,
    Group<grp_opts>;
def no_zero_fill_sections : Flag<["-"], "no_zero_fill_sections">,
    HelpText<"Explicitly store zeroed data in the final image">,
    Flags<[HelpHidden]>,
    Group<grp_opts>;
def merge_zero_fill_sections : Flag<["-"], "merge_zero_fill_sections">,
    HelpText<"Merge all zeroed data into the __zerofill section">,
    Flags<[HelpHidden]>,
    Group<grp_opts>;
def no_branch_islands : Flag<["-"], "no_branch_islands">,
    HelpText<"Disable infra for branches beyond the maximum branch distance.">,
    Flags<[HelpHidden]>,
    Group<grp_opts>;
def no_deduplicate : Flag<["-"], "no_deduplicate">,
    HelpText<"Disable code deduplication (synonym for `--icf=none')">,
    Alias<icf_eq>, AliasArgs<["none"]>,
    Group<grp_opts>;

def grp_version : OptionGroup<"version">, HelpText<"VERSION TARGETING">;

def platform_version : MultiArg<["-"], "platform_version", 3>,
    MetaVarName<"<platform> <min_version> <sdk_version>">,
    HelpText<"Platform (e.g., macos, ios, tvos, watchos, xros, bridgeos, mac-catalyst, ios-sim, tvos-sim, watchos-sim, xros-sim, driverkit) and version numbers">,
    Group<grp_version>;
def sdk_version : Separate<["-"], "sdk_version">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def macos_version_min : Separate<["-"], "macos_version_min">,
    MetaVarName<"<version>">,
    HelpText<"Oldest macOS version for which linked output is usable">,
    Group<grp_version>;
def macosx_version_min : Separate<["-"], "macosx_version_min">,
    MetaVarName<"<version>">,
    Alias<macos_version_min>,
    HelpText<"Alias for -macos_version_min">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def ios_version_min : Separate<["-"], "ios_version_min">,
    MetaVarName<"<version>">,
    HelpText<"Oldest iOS version for which linked output is usable">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def ios_simulator_version_min : Separate<["-"], "ios_simulator_version_min">,
    MetaVarName<"<version>">,
    HelpText<"Oldest iOS simulator version for which linked output is usable">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def iphoneos_version_min : Separate<["-"], "iphoneos_version_min">,
    MetaVarName<"<version>">,
    Alias<ios_version_min>,
    HelpText<"Alias for -ios_version_min">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def maccatalyst_version_min : Separate<["-"], "maccatalyst_version_min">,
    MetaVarName<"<version>">,
    HelpText<"Oldest MacCatalyst version for which linked output is usable">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def iosmac_version_min : Separate<["-"], "iosmac_version_min">,
    MetaVarName<"<version>">,
    Alias<maccatalyst_version_min>,
    HelpText<"Alias for -maccatalyst_version_min">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def uikitformac_version_min : Separate<["-"], "uikitformac_version_min">,
    MetaVarName<"<version>">,
    Alias<maccatalyst_version_min>,
    HelpText<"Alias for -maccatalyst_version_min">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def tvos_version_min : Separate<["-"], "tvos_version_min">,
    MetaVarName<"<version>">,
    HelpText<"Oldest tvOS version for which linked output is usable">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def watchos_version_min : Separate<["-"], "watchos_version_min">,
    MetaVarName<"<version>">,
    HelpText<"Oldest watchOS version for which linked output is usable">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def bridgeos_version_min : Separate<["-"], "bridgeos_version_min">,
    MetaVarName<"<version>">,
    HelpText<"Oldest bridgeOS version for which linked output is usable">,
    Flags<[HelpHidden]>,
    Group<grp_version>;
def driverkit_version_min : Separate<["-"], "driverkit_version_min">,
    MetaVarName<"<version>">,
    HelpText<"Oldest DriverKit version for which linked output is usable">,
    Flags<[HelpHidden]>,
    Group<grp_version>;

def grp_dylib : OptionGroup<"dylib">, HelpText<"DYNAMIC LIBRARIES (DYLIB)">;

def install_name : Separate<["-"], "install_name">,
    MetaVarName<"<name>">,
    HelpText<"Set an internal install path in a dylib">,
    Group<grp_dylib>;
def dylib_install_name : Separate<["-"], "dylib_install_name">,
    MetaVarName<"<name>">,
    Alias<install_name>,
    HelpText<"Alias for -install_name">,
    Group<grp_dylib>;
def dylinker_install_name : Separate<["-"], "dylinker_install_name">,
    MetaVarName<"<name>">,
    Alias<install_name>,
    HelpText<"Alias for -install_name">,
    Group<grp_dylib>;
def mark_dead_strippable_dylib : Flag<["-"], "mark_dead_strippable_dylib">,
    HelpText<"Mark output dylib as dead-strippable: When a client links against it but does not use any of its symbols, the dylib will not be added to the client's list of needed dylibs">,
    Group<grp_dylib>;
def compatibility_version : Separate<["-"], "compatibility_version">,
    MetaVarName<"<version>">,
    HelpText<"Compatibility <version> of this library">,
    Group<grp_dylib>;
def dylib_compatibility_version : Separate<["-"], "dylib_compatibility_version">,
    MetaVarName<"<version>">,
    Alias<compatibility_version>,
    HelpText<"Alias for -compatibility_version">,
    Flags<[HelpHidden]>,
    Group<grp_dylib>;
def current_version : Separate<["-"], "current_version">,
    MetaVarName<"<version>">,
    HelpText<"Current <version> of this library">,
    Group<grp_dylib>;
def dylib_current_version : Separate<["-"], "dylib_current_version">,
    MetaVarName<"<version>">,
    Alias<current_version>,
    HelpText<"Alias for -current_version">,
    Flags<[HelpHidden]>,
    Group<grp_dylib>;

def grp_main : OptionGroup<"main">, HelpText<"MAIN EXECUTABLE">;

def pie : Flag<["-"], "pie">,
    HelpText<"Build a position independent executable (default)">,
    Group<grp_main>;
def no_pie : Flag<["-"], "no_pie">,
    HelpText<"Do not build a position independent executable">,
    Group<grp_main>;
def pagezero_size : Separate<["-"], "pagezero_size">,
    MetaVarName<"<size>">,
    HelpText<"Size of unreadable segment at address zero is hex <size> (default is 4KB on 32-bit and 4GB on 64-bit)">,
    Group<grp_main>;
def stack_size : Separate<["-"], "stack_size">,
    MetaVarName<"<size>">,
    HelpText<"Maximum hex stack size for the main thread in a program. (default is 8MB)">,
    Flags<[HelpHidden]>,
    Group<grp_main>;
def allow_stack_execute : Flag<["-"], "allow_stack_execute">,
    HelpText<"Mark stack segment as executable">,
    Flags<[HelpHidden]>,
    Group<grp_main>;
def export_dynamic : Flag<["-"], "export_dynamic">,
    HelpText<"Preserve all global symbols during LTO and when dead-stripping executables">,
    Group<grp_main>;

def grp_bundle : OptionGroup<"bundle">, HelpText<"CREATING A BUNDLE">;

def bundle_loader : Separate<["-"], "bundle_loader">,
    MetaVarName<"<executable>">,
    HelpText<"Resolve undefined symbols from <executable>">,
    Group<grp_bundle>;

def grp_object : OptionGroup<"object">, HelpText<"CREATING AN OBJECT FILE">;

def keep_private_externs : Flag<["-"], "keep_private_externs">,
    HelpText<"Do not convert private external symbols to static symbols (only valid with -r)">,
    Flags<[HelpHidden]>,
    Group<grp_object>;
def d : Flag<["-"], "d">,
    HelpText<"Force tentative into real definitions for common symbols">,
    Flags<[HelpHidden]>,
    Group<grp_object>;

def grp_resolve : OptionGroup<"resolve">, HelpText<"SYMBOL RESOLUTION">;

def exported_symbol : Separate<["-"], "exported_symbol">,
    MetaVarName<"<symbol>">,
    HelpText<"<symbol> remains global, while others become private externs">,
    Group<grp_resolve>;
def exported_symbols_list : Separate<["-"], "exported_symbols_list">,
    MetaVarName<"<file>">,
    HelpText<"Symbols specified in <file> remain global, while others become private externs">,
    Group<grp_resolve>;
def no_exported_symbols : Flag<["-"], "no_exported_symbols">,
    HelpText<"Don't export any symbols from the binary, useful for main executables that don't have plugins">,
    Group<grp_resolve>;
def unexported_symbol : Separate<["-"], "unexported_symbol">,
    MetaVarName<"<symbol>">,
    HelpText<"Global <symbol> becomes private extern">,
    Group<grp_resolve>;
def unexported_symbols_list : Separate<["-"], "unexported_symbols_list">,
    MetaVarName<"<file>">,
    HelpText<"Global symbols specified in <file> become private externs">,
    Group<grp_resolve>;
def reexported_symbols_list : Separate<["-"], "reexported_symbols_list">,
    MetaVarName<"<file>">,
    HelpText<"Symbols from dependent dylibs specified in <file> are reexported by this dylib">,
    Flags<[HelpHidden]>,
    Group<grp_resolve>;
def alias : MultiArg<["-"], "alias", 2>,
    MetaVarName<"<symbol_name> <alternate_name>">,
    HelpText<"Create a symbol alias with default global visibility">,
    Group<grp_resolve>;
def alias_list : Separate<["-"], "alias_list">,
    MetaVarName<"<file>">,
    HelpText<"Create symbol aliases specified in <file>">,
    Flags<[HelpHidden]>,
    Group<grp_resolve>;
def flat_namespace : Flag<["-"], "flat_namespace">,
    HelpText<"Resolve symbols from all dylibs, both direct and transitive. Do not record source libraries: dyld must re-search at runtime and use the first definition found">,
    Group<grp_resolve>;
def twolevel_namespace : Flag<["-"], "twolevel_namespace">,
    HelpText<"Make dyld look up symbols by (dylib,name) pairs (default)">,
    Group<grp_resolve>;
def u : Separate<["-"], "u">,
    MetaVarName<"<symbol>">,
    HelpText<"Require that <symbol> be defined for the link to succeed">,
    Group<grp_resolve>;
def U : Separate<["-"], "U">,
    MetaVarName<"<symbol>">,
    HelpText<"Allow <symbol> to have no definition">,
    Group<grp_resolve>;
def undefined : Separate<["-"], "undefined">,
    MetaVarName<"<treatment>">,
    HelpText<"Handle undefined symbols according to <treatment>: error, warning, suppress, or dynamic_lookup (default is error)">,
    Group<grp_resolve>;
def rpath : Separate<["-"], "rpath">,
    MetaVarName<"<path>">,
    HelpText<"Add <path> to dyld search list for dylibs with load path prefix `@rpath/'">,
    Group<grp_resolve>;
def commons : Separate<["-"], "commons">,
    MetaVarName<"<treatment>">,
    HelpText<"Resolve tentative definitions in dylibs according to <treatment>: ignore_dylibs, use_dylibs, error (default is ignore_dylibs)">,
    Flags<[HelpHidden]>,
    Group<grp_resolve>;

def grp_introspect : OptionGroup<"introspect">, HelpText<"INTROSPECTING THE LINKER">;

def why_load : Flag<["-"], "why_load">,
    HelpText<"Log why each object file is loaded from a static library">,
    Group<grp_introspect>;
def whyload : Flag<["-"], "whyload">,
    Alias<why_load>,
    HelpText<"Alias for -why_load">,
    Group<grp_introspect>;
def why_live : Separate<["-"], "why_live">,
    MetaVarName<"<symbol>">,
    HelpText<"Log a chain of references to <symbol>, for use with -dead_strip">,
    Group<grp_introspect>;
def print_statistics : Flag<["-"], "print_statistics">,
    HelpText<"Log the linker's memory and CPU usage">,
    Flags<[HelpHidden]>,
    Group<grp_introspect>;
def t : Flag<["-"], "t">,
    HelpText<"Log every file the linker loads: object, archive, and dylib">,
    Group<grp_introspect>;
def whatsloaded : Flag<["-"], "whatsloaded">,
    HelpText<"Logs only the object files the linker loads">,
    Flags<[HelpHidden]>,
    Group<grp_introspect>;
def order_file_statistics : Flag<["-"], "order_file_statistics">,
    HelpText<"Logs information about -order_file">,
    Flags<[HelpHidden]>,
    Group<grp_introspect>;
def map : Separate<["-"], "map">,
    MetaVarName<"<path>">,
    HelpText<"Writes all symbols and their addresses to <path>">,
    Group<grp_introspect>;
def dependency_info : Separate<["-"], "dependency_info">,
    MetaVarName<"<path>">,
    HelpText<"Dump dependency info">,
    Group<grp_introspect>;
def save_temps : Flag<["-"], "save-temps">,
    HelpText<"Save intermediate LTO compilation results">,
    Group<grp_introspect>;

def grp_symtab : OptionGroup<"symtab">, HelpText<"SYMBOL TABLE">;

def S : Flag<["-"], "S">,
    HelpText<"Strip debug information (STABS or DWARF) from the output">,
    Group<grp_symtab>;
def x : Flag<["-"], "x">,
    HelpText<"Exclude non-global symbols from the output symbol table">,
    Group<grp_symtab>;
def non_global_symbols_strip_list : Separate<["-"], "non_global_symbols_strip_list">,
    MetaVarName<"<path>">,
    HelpText<"Specify in <path> the non-global symbols that should be removed from the output symbol table">,
    Group<grp_symtab>;
def non_global_symbols_no_strip_list : Separate<["-"], "non_global_symbols_no_strip_list">,
    MetaVarName<"<path>">,
    HelpText<"Specify in <path> the non-global symbols that should remain in the output symbol table">,
    Group<grp_symtab>;
def oso_prefix : Separate<["-"], "oso_prefix">,
    MetaVarName<"<path>">,
    HelpText<"Remove the prefix <path> from OSO symbols in the debug map">,
    Group<grp_symtab>;
def add_ast_path : Separate<["-"], "add_ast_path">,
    MetaVarName<"<path>">,
    HelpText<"AST paths will be emitted as STABS">,
    Group<grp_symtab>;

def grp_rare : OptionGroup<"rare">, HelpText<"RARELY USED">;

def v : Flag<["-"], "v">,
    HelpText<"Print the linker version and search paths in addition to linking">,
    Group<grp_rare>;
def adhoc_codesign : Flag<["-"], "adhoc_codesign">,
    HelpText<"Write an ad-hoc code signature to the output file (default for arm64 binaries)">,
    Group<grp_rare>;
def no_adhoc_codesign : Flag<["-"], "no_adhoc_codesign">,
    HelpText<"Do not write an ad-hoc code signature to the output file (default for x86_64 binaries)">,
    Group<grp_rare>;
def reproducible : Flag<["-"], "reproducible">,
    HelpText<"Make the output reproducible by removing timestamps and other non-deterministic data. This is the default behavior.">,
    Group<grp_rare>;
def version_details : Flag<["-"], "version_details">,
    HelpText<"Print the linker version in JSON form">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def no_weak_imports : Flag<["-"], "no_weak_imports">,
    HelpText<"Fail if any symbols are weak imports, allowed to be NULL at runtime">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def verbose_deduplicate : Flag<["-"], "verbose_deduplicate">,
    HelpText<"Print function names eliminated by deduplication and the total size of code savings">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def no_inits : Flag<["-"], "no_inits">,
    HelpText<"Fail if the output contains static initializers">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def no_warn_inits : Flag<["-"], "no_warn_inits">,
    HelpText<"Suppress warnings for static initializers in the output">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def unaligned_pointers : Separate<["-"], "unaligned_pointers">,
    MetaVarName<"<treatment>">,
    HelpText<"Handle unaligned pointers in __DATA segments according to <treatment>: warning, error, or suppress (default for arm64e is error, otherwise suppress)">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def dirty_data_list : Separate<["-"], "dirty_data_list">,
    MetaVarName<"<path>">,
    HelpText<"Specify data symbols in <path> destined for the __DATA_DIRTY segment">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def max_default_common_align : Separate<["-"], "max_default_common_align">,
    MetaVarName<"<boundary>">,
    HelpText<"Reduce maximum alignment for common symbols to a hex power-of-2 <boundary>">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def move_to_rw_segment : MultiArg<["-"], "move_to_rw_segment", 2>,
    MetaVarName<"<segment> <path>">,
    HelpText<"Move data symbols listed in <path> to another <segment>">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def move_to_ro_segment : MultiArg<["-"], "move_to_ro_segment", 2>,
    MetaVarName<"<segment> <path>">,
    HelpText<"Move code symbols listed in <path> to another <segment>">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def rename_section : MultiArg<["-"], "rename_section", 4>,
    MetaVarName<"<from_segment> <from_section> <to_segment> <to_section>">,
    HelpText<"Rename <from_segment>/<from_section> as <to_segment>/<to_section>">,
    Group<grp_rare>;
def rename_segment : MultiArg<["-"], "rename_segment", 2>,
    MetaVarName<"<from_segment> <to_segment>">,
    HelpText<"Rename <from_segment> as <to_segment>">,
    Group<grp_rare>;
def trace_symbol_layout : Flag<["-"], "trace_symbol_layout">,
    HelpText<"Show where and why symbols move, as specified by -move_to_ro_segment, -move_to_rw_segment, -rename_section, and -rename_segment">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def data_const : Flag<["-"], "data_const">,
    HelpText<"Force migration of readonly data into __DATA_CONST segment">,
    Group<grp_rare>;
def no_data_const : Flag<["-"], "no_data_const">,
    HelpText<"Block migration of readonly data away from __DATA segment">,
    Group<grp_rare>;
def text_exec : Flag<["-"], "text_exec">,
    HelpText<"Rename __segment TEXT to __TEXT_EXEC for sections __text and __stubs">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def section_order : MultiArg<["-"], "section_order", 2>,
    MetaVarName<"<segment> <sections>">,
    HelpText<"With -preload, specify layout sequence of colon-separated <sections> in <segment>">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def segment_order : Separate<["-"], "segment_order">,
    MetaVarName<"<colon_separated_segment_list>">,
    HelpText<"With -preload, specify layout sequence of colon-separated <segments>">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def allow_heap_execute : Flag<["-"], "allow_heap_execute">,
    HelpText<"On i386, allow any page to execute code">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def application_extension : Flag<["-"], "application_extension">,
    HelpText<"Mark output as safe for use in an application extension, and validate that linked dylibs are safe">,
    Group<grp_rare>;
def no_application_extension : Flag<["-"], "no_application_extension">,
    HelpText<"Disable application extension functionality (default)">,
    Group<grp_rare>;
def fatal_warnings : Flag<["-"], "fatal_warnings">,
    HelpText<"Treat warnings as errors">,
    Group<grp_rare>;
def no_eh_labels : Flag<["-"], "no_eh_labels">,
    HelpText<"In -r mode, suppress .eh labels in the __eh_frame section">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def warn_compact_unwind : Flag<["-"], "warn_compact_unwind">,
    HelpText<"Warn for each FDE that cannot compact into the __unwind_info section and must remain in the __eh_frame section">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def warn_weak_exports : Flag<["-"], "warn_weak_exports">,
    HelpText<"Warn if the linked image contains weak external symbols">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def no_weak_exports : Flag<["-"], "no_weak_exports">,
    HelpText<"Fail if the linked image contains weak external symbols">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def objc_gc_compaction : Flag<["-"], "objc_gc_compaction">,
    HelpText<"Mark the Objective-C image as compatible with compacting garbage collection">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def objc_gc : Flag<["-"], "objc_gc">,
    HelpText<"Verify that all code was compiled with -fobjc-gc or -fobjc-gc-only">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def objc_gc_only : Flag<["-"], "objc_gc_only">,
    HelpText<"Verify that all code was compiled with -fobjc-gc-only">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def dead_strip_dylibs : Flag<["-"], "dead_strip_dylibs">,
    HelpText<"Remove dylibs that are unreachable by the entry point or exported symbols">,
    Group<grp_rare>;
def allow_sub_type_mismatches : Flag<["-"], "allow_sub_type_mismatches">,
    HelpText<"Permit mixing objects compiled for different ARM CPU subtypes">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def no_uuid : Flag<["-"], "no_uuid">,
    HelpText<"Do not generate the LC_UUID load command">,
    Group<grp_rare>;
def root_safe : Flag<["-"], "root_safe">,
    HelpText<"Set the MH_ROOT_SAFE bit in the mach-o header">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def setuid_safe : Flag<["-"], "setuid_safe">,
    HelpText<"Set the MH_SETUID_SAFE bit in the mach-o header">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def interposable : Flag<["-"], "interposable">,
    HelpText<"Indirects access to all to exported symbols in a dylib">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def multi_module : Flag<["-"], "multi_module">,
    Alias<interposable>,
    HelpText<"Alias for -interposable">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def init : Separate<["-"], "init">,
    MetaVarName<"<symbol>">,
    HelpText<"Run <symbol> as the first initializer in a dylib">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def sub_library : Separate<["-"], "sub_library">,
    MetaVarName<"<name>">,
    HelpText<"Re-export the dylib as <name>">,
    Group<grp_rare>;
def sub_umbrella : Separate<["-"], "sub_umbrella">,
    MetaVarName<"<name>">,
    HelpText<"Re-export the framework as <name>">,
    Group<grp_rare>;
def allowable_client : Separate<["-"], "allowable_client">,
    MetaVarName<"<name>">,
    HelpText<"Specify <name> of a dylib or framework that is allowed to link to this dylib">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def client_name : Separate<["-"], "client_name">,
    MetaVarName<"<name>">,
    HelpText<"Specifies a <name> this client should match with the -allowable_client <name> in a dependent dylib">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def umbrella : Separate<["-"], "umbrella">,
    MetaVarName<"<name>">,
    HelpText<"Re-export this dylib through the umbrella framework <name>">,
    Group<grp_rare>;
def headerpad : Separate<["-"], "headerpad">,
    MetaVarName<"<size>">,
    HelpText<"Allocate hex <size> extra space for future expansion of the load commands via install_name_tool (default is 0x20)">,
    Group<grp_rare>;
def headerpad_max_install_names : Flag<["-"], "headerpad_max_install_names">,
    HelpText<"Allocate extra space so all load-command paths can expand to MAXPATHLEN via install_name_tool">,
    Group<grp_rare>;
def bind_at_load : Flag<["-"], "bind_at_load">,
    HelpText<"Tell dyld to bind all symbols at load time, rather than lazily">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def force_flat_namespace : Flag<["-"], "force_flat_namespace">,
    HelpText<"Tell dyld to use a flat namespace on this executable and all its dependent dylibs & bundles">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def segalign : Separate<["-"], "segalign">,
    MetaVarName<"<boundary>">,
    HelpText<"Align all segments to hex power-of-2 <boundary>">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def sectalign : MultiArg<["-"], "sectalign", 3>,
    MetaVarName<"<segment> <section> <boundary>">,
    HelpText<"Align <section> within <segment> to hex power-of-2 <boundary>">,
    Group<grp_rare>;
def stack_addr : Separate<["-"], "stack_addr">,
    MetaVarName<"<address>">,
    HelpText<"Initialize stack pointer to hex <address> rounded to a page boundary">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def segprot : MultiArg<["-"], "segprot", 3>,
    MetaVarName<"<segment> <max> <init>">,
    HelpText<"Specifies the <max> and <init> virtual memory protection of <segment> as r/w/x/-seg_addr_table path">,
    Group<grp_rare>;
def segs_read_write_addr : Separate<["-"], "segs_read_write_addr">,
    MetaVarName<"<address>">,
    HelpText<"This option is obsolete">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def segs_read_only_addr : Separate<["-"], "segs_read_only_addr">,
    MetaVarName<"<address>">,
    HelpText<"This option is obsolete">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def segaddr : MultiArg<["-"], "segaddr", 2>,
    MetaVarName<"<segment> <address>">,
    HelpText<"Specify the starting hex <address> at a 4KiB page boundary for <segment>">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def seg_page_size : MultiArg<["-"], "seg_page_size", 2>,
    MetaVarName<"<segment> <size>">,
    HelpText<"Specifies the page <size> for <segment>. Segment size will be a multiple of its page size">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def dylib_file : Separate<["-"], "dylib_file">,
    MetaVarName<"<install_path:current_path>">,
    HelpText<"Specify <current_path> as different from where a dylib normally resides at <install_path>">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def weak_reference_mismatches : Separate<["-"], "weak_reference_mismatches">,
    MetaVarName<"<treatment>">,
    HelpText<"Resolve symbol imports of conflicting weakness according to <treatment> as weak, non-weak, or error (default is non-weak)">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def read_only_relocs : Separate<["-"], "read_only_relocs">,
    MetaVarName<"<treatment>">,
    HelpText<"Handle relocations that modify read-only pages according to <treatment> of warning, error, or suppress (i.e., allow)">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def force_cpusubtype_ALL : Flag<["-"], "force_cpusubtype_ALL">,
    HelpText<"Mark binary as runnable on any PowerPC, ignoring any PowerPC cpu requirements encoded in the object files">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def no_arch_warnings : Flag<["-"], "no_arch_warnings">,
    HelpText<"Suppresses warnings about inputs whose architecture does not match the -arch option">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def arch_errors_fatal : Flag<["-"], "arch_errors_fatal">,
    HelpText<"Escalate to errors any warnings about inputs whose architecture does not match the -arch option">,
    Group<grp_rare>;
def e : Separate<["-"], "e">,
    MetaVarName<"<symbol>">,
    HelpText<"Make <symbol> the entry point of an executable (default is \"start\" from crt1.o)">,
    Group<grp_rare>;
def w : Flag<["-"], "w">,
    HelpText<"Suppress all warnings">,
    Group<grp_rare>;
def final_output : Separate<["-"], "final_output">,
    MetaVarName<"<name>">,
    HelpText<"Specify dylib install name if -install_name is not used; used by compiler driver for multiple -arch arguments">,
    Group<grp_rare>;
def arch_multiple : Flag<["-"], "arch_multiple">,
    HelpText<"Augment error and warning messages with the architecture name">,
    Group<grp_rare>;
def dot : Separate<["-"], "dot">,
    MetaVarName<"<path>">,
    HelpText<"Write a graph of symbol dependencies to <path> as a .dot file viewable with GraphViz">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def keep_relocs : Flag<["-"], "keep_relocs">,
    HelpText<"Retain section-based relocation records in the output, which are ignored at runtime by dyld">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def warn_stabs : Flag<["-"], "warn_stabs">,
    HelpText<"Warn when bad stab symbols inside a BINCL/EINCL prevent optimization">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def warn_commons : Flag<["-"], "warn_commons">,
    HelpText<"Warn when a tentative definition in an object file matches an external symbol in a dylib, which often means \"extern\" is missing from a variable declaration in a header file">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def read_only_stubs : Flag<["-"], "read_only_stubs">,
    HelpText<"On i386, make the __IMPORT segment of a final linked image read-only">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def interposable_list : Separate<["-"], "interposable_list">,
    MetaVarName<"<path>">,
    HelpText<"Access global symbols listed in <path> indirectly">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def no_function_starts : Flag<["-"], "no_function_starts">,
    HelpText<"Do not create table of function start addresses">,
    Group<grp_rare>;
def object_path_lto : Separate<["-"], "object_path_lto">,
    MetaVarName<"<path>">,
    HelpText<"Retain any temporary mach-o file in <path> that would otherwise be deleted during LTO">,
    Group<grp_rare>;
def cache_path_lto : Separate<["-"], "cache_path_lto">,
    MetaVarName<"<path>">,
    HelpText<"Use <path> as a directory for the incremental LTO cache">,
    Group<grp_rare>;
def prune_interval_lto : Separate<["-"], "prune_interval_lto">,
    MetaVarName<"<seconds>">,
    HelpText<"Prune the incremental LTO cache after <seconds> (-1 disables pruning)">,
    Group<grp_rare>;
def prune_after_lto : Separate<["-"], "prune_after_lto">,
    MetaVarName<"<seconds>">,
    HelpText<"Remove LTO cache entries after <seconds>">,
    Group<grp_rare>;
def max_relative_cache_size_lto : Separate<["-"], "max_relative_cache_size_lto">,
    MetaVarName<"<percent>">,
    HelpText<"Limit the incremental LTO cache growth to <percent> of free disk, space">,
    Group<grp_rare>;
def page_align_data_atoms : Flag<["-"], "page_align_data_atoms">,
    HelpText<"Distribute global variables on separate pages so page used/dirty status can guide creation of an order file to cluster commonly used/dirty globals">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def not_for_dyld_shared_cache : Flag<["-"], "not_for_dyld_shared_cache">,
    HelpText<"Prevent system dylibs from being placed into the dylib shared cache">,
    Flags<[HelpHidden]>,
    Group<grp_rare>;
def mllvm : Separate<["-"], "mllvm">,
    HelpText<"Options to pass to LLVM">,
    Group<grp_rare>;
def mcpu : Separate<["-"], "mcpu">,
    HelpText<"Processor family target for LTO code generation">,
    Group<grp_rare>;
def no_dtrace_dof : Flag<["-"], "no_dtrace_dof">,
    HelpText<"Disable dtrace-dof processing (default).">,
    Group<grp_rare>;
def objc_stubs_fast : Flag<["-"], "objc_stubs_fast">,
    HelpText<"Produce larger stubs for Objective-C method calls with fewer jumps (default).">,
    Group<grp_rare>;
def objc_stubs_small : Flag<["-"], "objc_stubs_small">,
    HelpText<"Produce smaller stubs for Objective-C method calls with more jumps.">,
    Group<grp_rare>;
def dyld_env : Separate<["-"], "dyld_env">,
    MetaVarName<"<dyld_env_var>">,
    HelpText<"Specifies a LC_DYLD_ENVIRONMENT variable value pair.">,
    Group<grp_rare>;
def ignore_auto_link : Flag<["-"], "ignore_auto_link">,
    HelpText<"Ignore LC_LINKER_OPTIONs">,
    Group<grp_rare>;

def grp_deprecated : OptionGroup<"deprecated">, HelpText<"DEPRECATED">;

def lazy_framework : Separate<["-"], "lazy_framework">,
    MetaVarName<"<name>">,
    HelpText<"This option is deprecated and is now an alias for -framework.">,
    Flags<[HelpHidden]>,
    Group<grp_deprecated>;
def lazy_library : Separate<["-"], "lazy_library">,
    MetaVarName<"<path>">,
    HelpText<"This option is deprecated and is now an alias for regular linking">,
    Flags<[HelpHidden]>,
    Group<grp_deprecated>;
def lazy_l : Joined<["-"], "lazy-l">,
    MetaVarName<"<name>">,
    HelpText<"This option is deprecated and is now an alias for -l<path>.">,
    Flags<[HelpHidden]>,
    Group<grp_deprecated>;
def single_module : Flag<["-"], "single_module">,
    HelpText<"Unnecessary option: this is already the default">,
    Flags<[HelpHidden]>,
    Group<grp_deprecated>;
def no_dead_strip_inits_and_terms : Flag<["-"], "no_dead_strip_inits_and_terms">,
    HelpText<"Unnecessary option: initialization and termination are roots of the dead strip graph, so never dead stripped">,
    Flags<[HelpHidden]>,
    Group<grp_deprecated>;

def grp_obsolete : OptionGroup<"obsolete">, HelpText<"OBSOLETE">;

def sectorder : MultiArg<["-"], "sectorder", 3>,
    MetaVarName<"<segname> <sectname> <orderfile>">,
    HelpText<"Obsolete. Replaced by more general -order_file option">,
    Group<grp_obsolete>;
def lto_library : Separate<["-"], "lto_library">,
    MetaVarName<"<path>">,
    HelpText<"Obsolete. LLD supports LTO directly, without using an external dylib.">,
    Group<grp_obsolete>;
def y : Joined<["-"], "y">,
    MetaVarName<"<symbol>">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def sectobjectsymbols : MultiArg<["-"], "sectobjectsymbols", 2>,
    MetaVarName<"<segname> <sectname>">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def nofixprebinding : Flag<["-"], "nofixprebinding">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def noprebind_all_twolevel_modules : Flag<["-"], "noprebind_all_twolevel_modules">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def prebind_all_twolevel_modules : Flag<["-"], "prebind_all_twolevel_modules">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def prebind_allow_overlap : Flag<["-"], "prebind_allow_overlap">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def noprebind : Flag<["-"], "noprebind">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def sect_diff_relocs : Separate<["-"], "sect_diff_relocs">,
    MetaVarName<"<treatment>">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def A : Separate<["-"], "A">,
    MetaVarName<"<basefile>">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def b : Flag<["-"], "b">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def Sn : Flag<["-"], "Sn">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def Si : Flag<["-"], "Si">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def Sp : Flag<["-"], "Sp">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def X : Flag<["-"], "X">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def s : Flag<["-"], "s">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def m : Flag<["-"], "m">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def Y : Separate<["-"], "Y">,
    MetaVarName<"<number>">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def nomultidefs : Flag<["-"], "nomultidefs">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def multiply_defined_unused : Separate<["-"], "multiply_defined_unused">,
    MetaVarName<"<treatment>">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def multiply_defined : Separate<["-"], "multiply_defined">,
    MetaVarName<"<treatment>">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def private_bundle : Flag<["-"], "private_bundle">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def seg_addr_table_filename : Separate<["-"], "seg_addr_table_filename">,
    MetaVarName<"<path>">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def sectorder_detail : Flag<["-"], "sectorder_detail">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def no_compact_linkedit : Flag<["-"], "no_compact_linkedit">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def dependent_dr_info : Flag<["-"], "dependent_dr_info">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def no_dependent_dr_info : Flag<["-"], "no_dependent_dr_info">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def seglinkedit : Flag<["-"], "seglinkedit">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def noseglinkedit : Flag<["-"], "noseglinkedit">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def fvmlib : Flag<["-"], "fvmlib">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def run_init_lazily : Flag<["-"], "run_init_lazily">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def prebind : Flag<["-"], "prebind">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def twolevel_namespace_hints : Flag<["-"], "twolevel_namespace_hints">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def slow_stubs : Flag<["-"], "slow_stubs">,
    HelpText<"This option is obsolete in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;

def bitcode_bundle : Flag<["-"], "bitcode_bundle">,
    HelpText<"Obsolete since the App Store no longer supports binaries with embedded bitcode">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def bitcode_hide_symbols : Flag<["-"], "bitcode_hide_symbols">,
    HelpText<"Obsolete since the App Store no longer supports binaries with embedded bitcode">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def bitcode_symbol_map : Separate<["-"], "bitcode_symbol_map">,
    MetaVarName<"<path>">,
    HelpText<"Obsolete since the App Store no longer supports binaries with embedded bitcode">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def bitcode_process_mode : Separate<["-"], "bitcode_process_mode">,
    HelpText<"Obsolete since the App Store no longer supports binaries with embedded bitcode">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;
def bitcode_verify : Flag<["-"], "bitcode_verify">,
    HelpText<"Obsolete since the App Store no longer supports binaries with embedded bitcode">,
    Flags<[HelpHidden]>,
    Group<grp_obsolete>;

def grp_undocumented : OptionGroup<"undocumented">, HelpText<"UNDOCUMENTED">;

def add_linker_option : Flag<["-"], "add_linker_option">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def add_source_version : Flag<["-"], "add_source_version">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def no_source_version : Flag<["-"], "no_source_version">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def add_split_seg_info : Flag<["-"], "add_split_seg_info">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def allow_dead_duplicates : Flag<["-"], "allow_dead_duplicates">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def allow_simulator_linking_to_macosx_dylibs : Flag<["-"], "allow_simulator_linking_to_macosx_dylibs">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def classic_linker : Flag<["-"], "classic_linker">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def data_in_code_info : Flag<["-"], "data_in_code_info">,
    HelpText<"Emit data-in-code information (default)">,
    Group<grp_undocumented>;
def no_data_in_code_info : Flag<["-"], "no_data_in_code_info">,
    HelpText<"Do not emit data-in-code information">,
    Group<grp_undocumented>;
def debug_snapshot : Flag<["-"], "debug_snapshot">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def demangle : Flag<["-"], "demangle">,
    HelpText<"Demangle symbol names in diagnostics">;
def encryptable : Flag<["-"], "encryptable">,
    HelpText<"Generate the LC_ENCRYPTION_INFO load command">,
    Group<grp_undocumented>;
def no_encryption : Flag<["-"], "no_encryption">,
    HelpText<"Do not generate the LC_ENCRYPTION_INFO load command">,
    Group<grp_undocumented>;
def executable_path : Flag<["-"], "executable_path">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def fixup_chains : Flag<["-"], "fixup_chains">,
    HelpText<"Emit chained fixups">,
    Group<grp_undocumented>;
def no_fixup_chains : Flag<["-"], "no_fixup_chains">,
    HelpText<"Emit fixup information as classic dyld opcodes">,
    Group<grp_undocumented>;
def fixup_chains_section : Flag<["-"], "fixup_chains_section">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def objc_relative_method_lists : Flag<["-"], "objc_relative_method_lists">,
    HelpText<"Emit relative method lists (more compact representation)">,
    Group<grp_undocumented>;
def no_objc_relative_method_lists : Flag<["-"], "no_objc_relative_method_lists">,
    HelpText<"Don't emit relative method lists (use traditional representation)">,
    Group<grp_undocumented>;
def flto_codegen_only : Flag<["-"], "flto-codegen-only">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def force_symbol_not_weak : Flag<["-"], "force_symbol_not_weak">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def force_symbols_coalesce_list : Flag<["-"], "force_symbols_coalesce_list">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def force_symbols_not_weak_list : Separate<["-"], "force_symbols_not_weak_list">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def force_symbols_weak_list : Separate<["-"], "force_symbols_weak_list">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def force_symbol_weak : Flag<["-"], "force_symbol_weak">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def function_starts : Flag<["-"], "function_starts">,
    HelpText<"Create table of function start addresses (default)">,
    Group<grp_undocumented>;
def i : Flag<["-"], "i">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def ignore_optimization_hints : Flag<["-"], "ignore_optimization_hints">,
    HelpText<"Ignore Linker Optimization Hints">,
    Group<grp_undocumented>;
def init_offsets : Flag<["-"], "init_offsets">,
    HelpText<"Store __TEXT segment offsets of static initializers">,
    Group<grp_undocumented>;
def keep_dwarf_unwind : Flag<["-"], "keep_dwarf_unwind">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def no_keep_dwarf_unwind : Flag<["-"], "no_keep_dwarf_unwind">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def kext : Flag<["-"], "kext">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def kext_objects_dir : Flag<["-"], "kext_objects_dir">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def no_kext_objects : Flag<["-"], "no_kext_objects">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def kexts_use_stubs : Flag<["-"], "kexts_use_stubs">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def no_compact_unwind : Flag<["-"], "no_compact_unwind">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def no_new_main : Flag<["-"], "no_new_main">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def pause : Flag<["-"], "pause">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def random_uuid : Flag<["-"], "random_uuid">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def simulator_support : Flag<["-"], "simulator_support">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def snapshot_dir : Flag<["-"], "snapshot_dir">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def source_version : Flag<["-"], "source_version">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def threaded_starts_section : Flag<["-"], "threaded_starts_section">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def verbose_optimization_hints : Flag<["-"], "verbose_optimization_hints">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;
def version_load_command : Flag<["-"], "version_load_command">,
    HelpText<"This option is undocumented in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_undocumented>;

def grp_ignored : OptionGroup<"ignored">, HelpText<"IGNORED">;

def M : Flag<["-"], "M">,
    HelpText<"This option is ignored in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_ignored>;
def new_linker : Flag<["-"], "new_linker">,
    HelpText<"This option is ignored in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_ignored>;

def grp_ignored_silently : OptionGroup<"ignored_silently">, HelpText<"IGNORED SILENTLY">;

def objc_abi_version : Separate<["-"], "objc_abi_version">,
    HelpText<"This option only applies to i386 in ld64">,
    Flags<[HelpHidden]>,
    Group<grp_ignored_silently>;
def debug_variant : Flag<["-"], "debug_variant">,
    HelpText<"Do not warn about issues that are only problems for binaries shipping to customers.">,
    Flags<[HelpHidden]>,
    Group<grp_ignored_silently>;

// NOTE: This flag should be respected if -warn_duplicate_libraries is ever implemented.
def no_warn_duplicate_libraries : Flag<["-"], "no_warn_duplicate_libraries">,
    HelpText<"Do not warn if the input contains duplicate library options.">,
    Flags<[HelpHidden]>,
    Group<grp_ignored_silently>;
