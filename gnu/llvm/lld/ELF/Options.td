include "llvm/Option/OptParser.td"

// Convenience classes for long options which only accept two dashes. For lld
// specific or newer long options, we prefer two dashes to avoid collision with
// short options. For many others, we have to accept both forms to be compatible
// with GNU ld.
class FF<string name> : Flag<["--"], name>;
class JJ<string name>: Joined<["--"], name>;

multiclass EEq<string name, string help> {
  def NAME: Separate<["--"], name>;
  def NAME # _eq: Joined<["--"], name # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
}

multiclass BB<string name, string help1, string help2> {
  def NAME: Flag<["--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--"], "no-" # name>, HelpText<help2>;
}

// For options whose names are multiple letters, either one dash or
// two can precede the option name except those that start with 'o'.
class F<string name>: Flag<["--", "-"], name>;
class J<string name>: Joined<["--", "-"], name>;

multiclass Eq<string name, string help> {
  def NAME: Separate<["--", "-"], name>;
  def NAME # _eq: Joined<["--", "-"], name # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
}

multiclass B<string name, string help1, string help2> {
  def NAME: Flag<["--", "-"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--", "-"], "no-" # name>, HelpText<help2>;
}

defm auxiliary: Eq<"auxiliary", "Set DT_AUXILIARY field to the specified name">;

def be8: F<"be8">, HelpText<"write a Big Endian ELF file using BE8 format (AArch32 only)">;

def Bno_symbolic: F<"Bno-symbolic">, HelpText<"Don't bind default visibility defined symbols locally for -shared (default)">;

def Bsymbolic: F<"Bsymbolic">, HelpText<"Bind default visibility defined symbols locally for -shared">;

def Bsymbolic_non_weak: F<"Bsymbolic-non-weak">,
  HelpText<"Bind default visibility defined STB_GLOBAL symbols locally for -shared">;

def Bsymbolic_functions: F<"Bsymbolic-functions">,
  HelpText<"Bind default visibility defined function symbols locally for -shared">;

def Bsymbolic_non_weak_functions: F<"Bsymbolic-non-weak-functions">,
  HelpText<"Bind default visibility defined STB_GLOBAL function symbols locally for -shared">;

def Bdynamic: F<"Bdynamic">, HelpText<"Link against shared libraries (default)">;

def Bstatic: F<"Bstatic">, HelpText<"Do not link against shared libraries">;

def build_id: J<"build-id=">, HelpText<"Generate build ID note">,
  MetaVarName<"[fast,md5,sha1,uuid,0x<hexstring>]">;
def : F<"build-id">, Alias<build_id>, AliasArgs<["sha1"]>, HelpText<"Alias for --build-id=sha1">;

defm check_sections: B<"check-sections",
    "Check section addresses for overlaps (default)",
    "Do not check section addresses for overlaps">;

defm compress_debug_sections:
  Eq<"compress-debug-sections", "Compress DWARF debug sections">,
  MetaVarName<"[none,zlib,zstd]">;

defm compress_sections: EEq<"compress-sections",
  "Compress output sections that match the glob and do not have the SHF_ALLOC flag. "
  "The sections remain uncompressed if compressed content would be larger. "
  "The compression level is <level> (if specified) or a default speed-focused level">,
  MetaVarName<"<section-glob>={none,zlib,zstd}[:level]">;

defm defsym: Eq<"defsym", "Define a symbol alias">, MetaVarName<"<symbol>=<value>">;

defm optimize_bb_jumps: BB<"optimize-bb-jumps",
    "Remove direct jumps at the end to the next basic block",
    "Do not remove any direct jumps at the end to the next basic block (default)">;

defm fortran_common : BB<"fortran-common",
    "Search archive members for definitions to override COMMON symbols (default)",
    "Do not search archive members for definitions to override COMMON symbols">;

defm split_stack_adjust_size
    : Eq<"split-stack-adjust-size",
         "Specify adjustment to stack size when a split-stack function calls a "
         "non-split-stack function">,
      MetaVarName<"<value>">;

def O: JoinedOrSeparate<["-"], "O">, HelpText<"Optimize output file size">;

def cmse_implib: FF<"cmse-implib">,
  HelpText<"Make the output library to be a CMSE secure code import library">;

defm in_implib: EEq<"in-implib",
    "Read an existing CMSE secure code import library and preserve entry function addresses in the "
    "resulting new CMSE secure code import library (optional when creating a CMSE secure image)">,
  MetaVarName<"<file>">;

defm out_implib: EEq<"out-implib",
    "Output the CMSE secure code import library to <file> (required when creating a CMSE secure image)">,
  MetaVarName<"<file>">;

defm Tbss: Eq<"Tbss", "Same as --section-start with .bss as the sectionname">;

defm Tdata: Eq<"Tdata", "Same as --section-start with .data as the sectionname">;

defm Ttext: Eq<"Ttext", "Same as --section-start with .text as the sectionname">;

def Ttext_segment: Separate<["-", "--"], "Ttext-segment">;

defm allow_multiple_definition: B<"allow-multiple-definition",
    "Allow multiple definitions",
    "Do not allow multiple definitions (default)">;

defm allow_shlib_undefined: B<"allow-shlib-undefined",
    "Allow unresolved references in shared libraries (default when linking a shared library)",
    "Do not allow unresolved references in shared libraries (default when linking an executable)">;

defm apply_dynamic_relocs: BB<"apply-dynamic-relocs",
    "Apply link-time values for dynamic relocations",
    "Do not apply link-time values for dynamic relocations (default)">;

defm dependent_libraries: BB<"dependent-libraries",
    "Process dependent library specifiers from input files (default)",
    "Ignore dependent library specifiers from input files">;

defm as_needed: B<"as-needed",
    "Only set DT_NEEDED for shared libraries if used",
    "Always set DT_NEEDED for shared libraries (default)">;

defm call_graph_ordering_file:
  Eq<"call-graph-ordering-file", "Layout sections to optimize the given callgraph">;

def call_graph_profile_sort: JJ<"call-graph-profile-sort=">,
  HelpText<"Reorder input sections with call graph profile using the specified algorithm (default: cdsort)">,
  MetaVarName<"[none,hfsort,cdsort]">,
  Values<"none,hfsort,cdsort">;
def : FF<"no-call-graph-profile-sort">, Alias<call_graph_profile_sort>, AliasArgs<["none"]>,
  Flags<[HelpHidden]>;

// --chroot doesn't have a help text because it is an internal option.
def chroot: Separate<["--"], "chroot">;

def color_diagnostics: JJ<"color-diagnostics=">,
  HelpText<"Use colors in diagnostics (default: auto)">,
  MetaVarName<"[auto,always,never]">;
def : Flag<["--"], "color-diagnostics">, Alias<color_diagnostics>, AliasArgs<["always"]>,
  HelpText<"Alias for --color-diagnostics=always">;
def : Flag<["--"], "no-color-diagnostics">, Alias<color_diagnostics>, AliasArgs<["never"]>,
  HelpText<"Alias for --color-diagnostics=never">;

def cref: FF<"cref">,
  HelpText<"Output cross reference table. If -Map is specified, print to the map file">;

defm debug_names: BB<"debug-names",
    "Generate a merged .debug_names section",
    "Do not generate a merged .debug_names section (default)">;

defm default_script: EEq<"default-script", "In the absence of --script, read this default linker script">;

defm demangle: B<"demangle",
    "Demangle symbol names (default)",
    "Do not demangle symbol names">;

defm dependency_file: EEq<"dependency-file", "Write a dependency file">,
  MetaVarName<"<file>">;

def disable_new_dtags: F<"disable-new-dtags">,
  HelpText<"Disable new dynamic tags">;

def discard_all: F<"discard-all">, HelpText<"Delete all local symbols">;

def discard_locals: F<"discard-locals">,
  HelpText<"Delete temporary local symbols">;

def discard_none: F<"discard-none">,
  HelpText<"Keep all symbols in the symbol table">;

defm dynamic_linker: Eq<"dynamic-linker", "Which dynamic linker to use">;

defm dynamic_list : Eq<"dynamic-list",
   "Similar to --export-dynamic-symbol-list. When creating a shared object, "
   "this additionally implies -Bsymbolic but does not set DF_SYMBOLIC">,
   MetaVarName<"<file>">;

def eb: F<"EB">, HelpText<"Select the big-endian format in OUTPUT_FORMAT">;
def el: F<"EL">, HelpText<"Select the little-endian format in OUTPUT_FORMAT">;

defm eh_frame_hdr: B<"eh-frame-hdr",
    "Request creation of .eh_frame_hdr section and PT_GNU_EH_FRAME segment header",
    "Do not create .eh_frame_hdr section">;

def emit_relocs: F<"emit-relocs">, HelpText<"Generate relocations in output">;

def enable_new_dtags: F<"enable-new-dtags">,
  HelpText<"Enable new dynamic tags (default)">;

def enable_non_contiguous_regions : FF<"enable-non-contiguous-regions">,
  HelpText<"Spill input sections to later matching output sections to avoid memory region overflow">;

def end_group: F<"end-group">,
  HelpText<"Ignored for compatibility with GNU unless you pass --warn-backrefs">;

def end_lib: F<"end-lib">,
  HelpText<"End a grouping of objects that should be treated as if they were together in an archive">;

defm entry: Eq<"entry", "Name of entry point symbol">,
  MetaVarName<"<entry>">;

defm error_limit:
  EEq<"error-limit", "Maximum number of errors to emit before stopping (0 = no limit)">;

def error_unresolved_symbols: F<"error-unresolved-symbols">,
  HelpText<"Report unresolved symbols as errors">;

defm error_handling_script: EEq<"error-handling-script",
    "Specify an error handling script">;

defm exclude_libs: Eq<"exclude-libs", "Exclude static libraries from automatic export">;

defm execute_only: BB<"execute-only",
    "Mark executable sections unreadable",
    "Mark executable sections readable (default)">;

defm export_dynamic: B<"export-dynamic",
    "Put symbols in the dynamic symbol table",
    "Do not put symbols in the dynamic symbol table (default)">;

defm export_dynamic_symbol : EEq<"export-dynamic-symbol",
    "(executable) Put matched symbols in the dynamic symbol table. "
    "(shared object) References to matched non-local STV_DEFAULT symbols "
    "shouldn't be bound to definitions within the shared object. "
    "Does not imply -Bsymbolic.">,
    MetaVarName<"glob">;

defm export_dynamic_symbol_list : EEq<"export-dynamic-symbol-list",
   "Read a list of dynamic symbol patterns. Apply --export-dynamic-symbol on each pattern">,
    MetaVarName<"file">;

defm fatal_warnings: B<"fatal-warnings",
    "Treat warnings as errors",
    "Do not treat warnings as errors (default)">;

defm filter: Eq<"filter", "Set DT_FILTER field to the specified name">;

defm fini: Eq<"fini", "Specify a finalizer function">, MetaVarName<"<symbol>">;

def fix_cortex_a53_843419: F<"fix-cortex-a53-843419">,
  HelpText<"Apply fixes for AArch64 Cortex-A53 erratum 843419">;

def fix_cortex_a8: F<"fix-cortex-a8">,
  HelpText<"Apply fixes for ARM Cortex-A8 erratum 657417">;

def force_group_allocation: FF<"force-group-allocation">,
  HelpText<"Only meaningful for -r. Section groups are discarded. If two section group members are placed to the same output section, combine their relocations as well">;

defm format: Eq<"format", "Change the input format of the inputs following this option">,
  MetaVarName<"[default,elf,binary]">;

defm gc_sections: B<"gc-sections",
    "Enable garbage collection of unused sections",
    "Disable garbage collection of unused sections (default)">;

defm gdb_index: BB<"gdb-index",
    "Generate .gdb_index section",
    "Do not generate .gdb_index section (default)">;

defm gnu_unique: BB<"gnu-unique",
  "Enable STB_GNU_UNIQUE symbol binding (default)",
  "Disable STB_GNU_UNIQUE symbol binding">;

defm hash_style: Eq<"hash-style", "Specify hash style (sysv, gnu or both)">;

def help: F<"help">, HelpText<"Print option help">;

def icf_all: F<"icf=all">, HelpText<"Enable identical code folding">;

def icf_safe: F<"icf=safe">, HelpText<"Enable safe identical code folding">;

def icf_none: F<"icf=none">, HelpText<"Disable identical code folding (default)">;

defm ignore_function_address_equality: B<"ignore-function-address-equality",
  "lld can break the address equality of functions",
  "lld cannot break the address equality of functions">;

def ignore_data_address_equality: FF<"ignore-data-address-equality">,
  HelpText<"lld can break the address equality of data">;

defm image_base: EEq<"image-base", "Set the base address">;

defm init: Eq<"init", "Specify an initializer function">,
  MetaVarName<"<symbol>">;

defm just_symbols: Eq<"just-symbols", "Just link symbols">;

defm keep_unique: Eq<"keep-unique", "Do not fold this symbol during ICF">;

def library: JoinedOrSeparate<["-"], "l">, MetaVarName<"<libname>">,
  HelpText<"Search for library <libname>">;
def library_path: JoinedOrSeparate<["-"], "L">, MetaVarName<"<dir>">,
  HelpText<"Add <dir> to the library search path">;

def m: JoinedOrSeparate<["-"], "m">, HelpText<"Set target emulation">;

defm Map: Eq<"Map", "Print a link map to the specified file">;

defm merge_exidx_entries: B<"merge-exidx-entries",
    "Enable merging .ARM.exidx entries (default)",
    "Disable merging .ARM.exidx entries">;

defm mmap_output_file: BB<"mmap-output-file",
    "Mmap the output file for writing (default)",
    "Do not mmap the output file for writing">;

def nmagic: F<"nmagic">, MetaVarName<"<magic>">,
  HelpText<"Do not page align sections, link against static libraries.">;

def nostdlib: F<"nostdlib">,
  HelpText<"Only search directories specified on the command line">;

def no_dynamic_linker: F<"no-dynamic-linker">,
  HelpText<"Inhibit output of .interp section">;

def noinhibit_exec: F<"noinhibit-exec">,
  HelpText<"Retain the executable output file whenever it is still usable">;

def no_warn_mismatch: F<"no-warn-mismatch">,
  HelpText<"Suppress errors for certain unknown seciton types">;

def no_nmagic: F<"no-nmagic">, MetaVarName<"<magic>">,
  HelpText<"Page align sections (default)">;

def no_omagic: F<"no-omagic">, MetaVarName<"<magic>">,
  HelpText<"Do not set the text data sections to be writable, page align sections (default)">;

def no_undefined: F<"no-undefined">,
  HelpText<"Report unresolved symbols even if the linker is creating a shared library">;

def no_warnings: FF<"no-warnings">, HelpText<"Suppress warnings and cancel --fatal-warnings">;
def : Flag<["-"], "w">, Alias<no_warnings>, HelpText<"Alias for --no-warnings">;

def o: JoinedOrSeparate<["-"], "o">, MetaVarName<"<path>">,
  HelpText<"Path to file to write output">;

defm oformat: EEq<"oformat", "Specify the binary format for the output object file">,
  MetaVarName<"[elf,binary]">;

def omagic: FF<"omagic">, MetaVarName<"<magic>">,
  HelpText<"Set the text and data sections to be readable and writable, do not page align sections, link against static libraries">;

defm orphan_handling:
  Eq<"orphan-handling", "Control how orphan sections are handled when linker script used">;

defm pack_dyn_relocs:
  EEq<"pack-dyn-relocs", "Pack dynamic relocations in the given format">,
  MetaVarName<"[none,android,relr,android+relr]">;

defm use_android_relr_tags: BB<"use-android-relr-tags",
    "Use SHT_ANDROID_RELR / DT_ANDROID_RELR* tags instead of SHT_RELR / DT_RELR*",
    "Use SHT_RELR / DT_RELR* tags (default)">;

def pic_veneer: F<"pic-veneer">,
  HelpText<"Always generate position independent thunks (veneers)">;

defm pie: B<"pie",
    "Create a position independent executable",
    "Do not create a position independent executable (default)">;

defm print_gc_sections: B<"print-gc-sections",
    "List removed unused sections",
    "Do not list removed unused sections (default)">;

defm print_icf_sections: B<"print-icf-sections",
    "List identical folded sections",
    "Do not list identical folded sections (default)">;

def print_archive_stats: J<"print-archive-stats=">,
  HelpText<"Write archive usage statistics to the specified file. "
           "Print the numbers of members and extracted members for each archive">;

defm print_symbol_order: Eq<"print-symbol-order",
  "Print a symbol order specified by --call-graph-ordering-file into the specified file">;

def pop_state: F<"pop-state">,
  HelpText<"Restore the states saved by --push-state">;

def push_state: F<"push-state">,
  HelpText<"Save the current state of --as-needed, -static and --whole-archive">;

def print_map: F<"print-map">,
  HelpText<"Print a link map to the standard output">;

def print_memory_usage: F<"print-memory-usage">,
  HelpText<"Report target memory usage">;

defm relax: BB<"relax",
  "Enable target-specific relaxations if supported (default)",
  "Disable target-specific relaxations">;

defm relax_gp: BB<"relax-gp",
  "Enable global pointer relaxation",
  "Disable global pointer relaxation (default)">;

defm remap_inputs: EEq<"remap-inputs",
  "Remap input files matching <from-glob> to <to-file>">,
  MetaVarName<"<from-glob>=<to-file>">;

def remap_inputs_file: JJ<"remap-inputs-file=">,
  HelpText<"Each line contains 'from-glob=to-file'. An input file matching <from-glob> is remapped to <to-file>">,
  MetaVarName<"<file>">;

defm reproduce:
  EEq<"reproduce",
     "Write tar file containing inputs and command to reproduce link">;

defm rosegment: BB<"rosegment",
  "Put read-only non-executable sections in their own segment (default)",
  "Do not put read-only non-executable sections in their own segment">;

defm rpath: Eq<"rpath", "Add a DT_RUNPATH to the output">;

def relocatable: F<"relocatable">, HelpText<"Create relocatable object file">;

defm retain_symbols_file:
  Eq<"retain-symbols-file", "Retain only the symbols listed in the file">,
  MetaVarName<"<file>">;

defm script: Eq<"script", "Read linker script">;

defm section_start: Eq<"section-start", "Set address of section">,
  MetaVarName<"<address>">;

def shared: F<"shared">, HelpText<"Build a shared object">;

defm soname: Eq<"soname", "Set DT_SONAME">;

defm sort_section:
  Eq<"sort-section", "Specifies sections sorting rule when linkerscript is used">;

def start_group: F<"start-group">,
  HelpText<"Ignored for compatibility with GNU unless you pass --warn-backrefs">;

def start_lib: F<"start-lib">,
  HelpText<"Start a grouping of objects that should be treated as if they were together in an archive">;

def strip_all: F<"strip-all">, HelpText<"Strip all symbols. Implies --strip-debug">;

def strip_debug: F<"strip-debug">, HelpText<"Strip debugging information">;

defm symbol_ordering_file:
  EEq<"symbol-ordering-file", "Layout sections to place symbols in the order specified by symbol ordering file">;

defm sysroot: Eq<"sysroot", "Set the system root">;

def target1_rel: F<"target1-rel">, HelpText<"Interpret R_ARM_TARGET1 as R_ARM_REL32">;

def target1_abs: F<"target1-abs">, HelpText<"Interpret R_ARM_TARGET1 as R_ARM_ABS32 (default)">;

defm target2:
  Eq<"target2", "Interpret R_ARM_TARGET2 as <type>, where <type> is one of rel, abs, or got-rel">,
  MetaVarName<"<type>">;

defm threads
    : EEq<"threads",
         "Number of threads. '1' disables multi-threading. By default all "
         "available hardware threads are used">;

def time_trace_eq: JJ<"time-trace=">, MetaVarName<"<file>">,
  HelpText<"Record time trace to <file>">;
def : FF<"time-trace">, Alias<time_trace_eq>,
  HelpText<"Record time trace to file next to output">;

defm time_trace_granularity: EEq<"time-trace-granularity",
  "Minimum time granularity (in microseconds) traced by time profiler">;

defm toc_optimize : BB<"toc-optimize",
    "(PowerPC64) Enable TOC related optimizations (default)",
    "(PowerPC64) Disable TOC related optimizations">;

defm pcrel_optimize : BB<"pcrel-optimize",
    "(PowerPC64) Enable PC-relative optimizations (default)",
    "(PowerPC64) Disable PC-relative optimizations">;

def trace: F<"trace">, HelpText<"Print the names of the input files">;

defm trace_symbol: Eq<"trace-symbol", "Trace references to symbols">;

defm undefined: Eq<"undefined", "Force undefined symbol during linking">,
  MetaVarName<"<symbol>">;

defm undefined_glob: EEq<"undefined-glob", "Force undefined symbol during linking">,
  MetaVarName<"<pattern>">;

def unique: F<"unique">, HelpText<"Creates a separate output section for every orphan input section">;

defm unresolved_symbols:
  Eq<"unresolved-symbols", "Determine how to handle unresolved symbols">;

defm undefined_version: B<"undefined-version",
  "Allow unused version in version script (disabled by default)",
  "Report version scripts that refer undefined symbols">;

defm rsp_quoting: EEq<"rsp-quoting", "Quoting style for response files">,
  MetaVarName<"[posix,windows]">;

def v: Flag<["-"], "v">, HelpText<"Display the version number">;

def verbose: F<"verbose">, HelpText<"Verbose mode">;

def version: F<"version">, HelpText<"Display the version number and exit">;

def power10_stubs_eq: JJ<"power10-stubs=">, MetaVarName<"<mode>">,
  HelpText<"Whether to use Power10 instructions in call stubs for R_PPC64_REL24_NOTOC and TOC/NOTOC "
  "interworking (yes (default): use; no: don't use). \"auto\" is currently the same as \"yes\"">;
def power10_stubs: FF<"power10-stubs">, Alias<power10_stubs_eq>, AliasArgs<["yes"]>,
  HelpText<"Alias for --power10-stubs=auto">;
def no_power10_stubs: FF<"no-power10-stubs">, Alias<power10_stubs_eq>, AliasArgs<["no"]>,
  HelpText<"Alias for --power10-stubs=no">;

defm version_script: Eq<"version-script", "Read a version script">;

defm warn_backrefs: BB<"warn-backrefs",
    "Warn about backward symbol references to extract archive members",
    "Do not warn about backward symbol references to extract archive members (default)">;

defm warn_backrefs_exclude
    : EEq<"warn-backrefs-exclude",
         "Glob describing an archive (or an object file within --start-lib) "
         "which should be ignored for --warn-backrefs.">,
      MetaVarName<"<glob>">;

defm warn_common: B<"warn-common",
    "Warn about duplicate common symbols",
    "Do not warn about duplicate common symbols (default)">;

defm warn_ifunc_textrel: BB<"warn-ifunc-textrel",
    "Warn about using ifunc symbols with text relocations",
    "Do not warn about using ifunc symbols with text relocations (default)">;

defm warn_symbol_ordering: BB<"warn-symbol-ordering",
    "Warn about problems with the symbol ordering file (default)",
    "Do not warn about problems with the symbol ordering file">;

def warn_unresolved_symbols: F<"warn-unresolved-symbols">,
  HelpText<"Report unresolved symbols as warnings">;

defm whole_archive: B<"whole-archive",
    "Force load of all members in a static library",
    "Do not force load of all members in a static library (default)">;

def why_extract: JJ<"why-extract=">, HelpText<"Print to a file about why archive members are extracted">;

defm wrap : Eq<"wrap", "Redirect symbol references to __wrap_symbol and "
                       "__real_symbol references to symbol">,
            MetaVarName<"<symbol>">;

def z: JoinedOrSeparate<["-"], "z">, MetaVarName<"<option>">,
  HelpText<"Linker option extensions">;

def visual_studio_diagnostics_format : FF<"vs-diagnostics">,
HelpText<"Format diagnostics for Visual Studio compatibility">;

def package_metadata: JJ<"package-metadata=">, HelpText<"Emit package metadata note">;

// Aliases
def: Separate<["-"], "dT">, Alias<default_script>, HelpText<"Alias for --default-script">;
def: Separate<["-"], "f">, Alias<auxiliary>, HelpText<"Alias for --auxiliary">;
def: F<"call_shared">, Alias<Bdynamic>, HelpText<"Alias for --Bdynamic">;
def: F<"dy">, Alias<Bdynamic>, HelpText<"Alias for --Bdynamic">;
def: F<"dn">, Alias<Bstatic>, HelpText<"Alias for --Bstatic">;
def: F<"non_shared">, Alias<Bstatic>, HelpText<"Alias for --Bstatic">;
def: F<"static">, Alias<Bstatic>, HelpText<"Alias for --Bstatic">;
def: Flag<["-"], "x">, Alias<discard_all>, HelpText<"Alias for --discard-all">;
def: Flag<["-"], "X">, Alias<discard_locals>, HelpText<"Alias for --discard-locals">;
def: Flag<["-"], "q">, Alias<emit_relocs>, HelpText<"Alias for --emit-relocs">;
def: Flag<["-"], ")">, Alias<end_group>, HelpText<"Alias for --end-group">;
def: JoinedOrSeparate<["-"], "e">, Alias<entry>, HelpText<"Alias for --entry">;
def: Flag<["-"], "E">, Alias<export_dynamic>, HelpText<"Alias for --export-dynamic">;
def: Separate<["-"], "F">, Alias<filter>, HelpText<"Alias for --filter">;
def: Separate<["-"], "b">, Alias<format>, HelpText<"Alias for --format">;
def: Separate<["--", "-"], "library">, Alias<library>;
def: Joined<["--", "-"], "library=">, Alias<library>;
def: Separate<["--", "-"], "library-path">, Alias<library_path>;
def: Joined<["--", "-"], "library-path=">, Alias<library_path>;
def: Flag<["-"], "n">, Alias<nmagic>, HelpText<"Alias for --nmagic">;
def: Flag<["-"], "N">, Alias<omagic>, HelpText<"Alias for --omagic">;
def: Joined<["--"], "output=">, Alias<o>, HelpText<"Alias for -o">;
def: Separate<["--"], "output">, Alias<o>, HelpText<"Alias for -o">;
def: F<"nopie">, Alias<no_pie>, HelpText<"Alias for --no-pie">;
def: F<"pic-executable">, Alias<pie>, HelpText<"Alias for --pie">;
def: Flag<["-"], "M">, Alias<print_map>, HelpText<"Alias for --print-map">;
def: Flag<["-"], "r">, Alias<relocatable>, HelpText<"Alias for --relocatable">;
def: JoinedOrSeparate<["-"], "R">, Alias<rpath>, HelpText<"Alias for --rpath">;
def: JoinedOrSeparate<["-"], "T">, Alias<script>, HelpText<"Alias for --script">;
def: F<"Bshareable">, Alias<shared>, HelpText<"Alias for --shared">;
def: JoinedOrSeparate<["-"], "h">, Alias<soname>, HelpText<"Alias for --soname">;
def: Flag<["-"], "(">, Alias<start_group>, HelpText<"Alias for --start-group">;
def: Flag<["-"], "s">, Alias<strip_all>, HelpText<"Alias for --strip-all">;
def: Flag<["-"], "S">, Alias<strip_debug>, HelpText<"Alias for --strip-debug">;
def: Flag<["-"], "t">, Alias<trace>, HelpText<"Alias for --trace">;
def: Joined<["-", "--"], "Ttext-segment=">, Alias<Ttext_segment>;
def: JoinedOrSeparate<["-"], "y">, Alias<trace_symbol>, HelpText<"Alias for --trace-symbol">;
def: JoinedOrSeparate<["-"], "u">, Alias<undefined>, HelpText<"Alias for --undefined">;
def: Flag<["-"], "V">, Alias<v>, HelpText<"Alias for -v">;

// LTO-related options.

def lto: JJ<"lto=">, HelpText<"Set LTO backend">,
                 MetaVarName<"[full,thin]">;
def lto_aa_pipeline: JJ<"lto-aa-pipeline=">,
  HelpText<"AA pipeline to run during LTO. Used in conjunction with -lto-newpm-passes">;
def lto_debug_pass_manager: FF<"lto-debug-pass-manager">,
  HelpText<"Debug new pass manager">;
def lto_emit_asm: FF<"lto-emit-asm">,
  HelpText<"Emit assembly code">;
def lto_emit_llvm: FF<"lto-emit-llvm">,
  HelpText<"Emit LLVM-IR bitcode">;
def lto_newpm_passes: JJ<"lto-newpm-passes=">,
  HelpText<"Passes to run during LTO">;
def lto_O: JJ<"lto-O">, MetaVarName<"<opt-level>">,
  HelpText<"Optimization level for LTO">;
def lto_CGO: JJ<"lto-CGO">, MetaVarName<"<cgopt-level>">,
  HelpText<"Codegen optimization level for LTO">;
def lto_partitions: JJ<"lto-partitions=">,
  HelpText<"Number of LTO codegen partitions">;
def lto_cs_profile_generate: FF<"lto-cs-profile-generate">,
  HelpText<"Perform context sensitive PGO instrumentation">;
def lto_cs_profile_file: JJ<"lto-cs-profile-file=">,
  HelpText<"Context sensitive profile file path">;
defm lto_pgo_warn_mismatch: BB<"lto-pgo-warn-mismatch",
  "turn on warnings about profile cfg mismatch (default)",
  "turn off warnings about profile cfg mismatch">;
defm lto_known_safe_vtables : EEq<"lto-known-safe-vtables",
  "When --lto-validate-all-vtables-have-type-infos is enabled, skip validation on these vtables (_ZTV symbols)">;
def lto_obj_path_eq: JJ<"lto-obj-path=">;
def lto_sample_profile: JJ<"lto-sample-profile=">,
  HelpText<"Sample profile file path">;
defm lto_validate_all_vtables_have_type_infos: BB<"lto-validate-all-vtables-have-type-infos",
  "Validate that all vtables have type infos for LTO link",
  "Do not validate that all vtables have type infos for LTO link">;
defm lto_whole_program_visibility: BB<"lto-whole-program-visibility",
  "Asserts that the LTO link has whole program visibility",
  "Asserts that the LTO link does not have whole program visibility">;
def disable_verify: F<"disable-verify">;
defm mllvm: Eq<"mllvm", "Additional arguments to forward to LLVM's option processing">;
def opt_remarks_filename: Separate<["--"], "opt-remarks-filename">,
  HelpText<"YAML output file for optimization remarks">;
defm opt_remarks_hotness_threshold: EEq<"opt-remarks-hotness-threshold",
  "Minimum profile count required for an optimization remark to be output."
  " Use 'auto' to apply the threshold from profile summary.">,
  MetaVarName<"<value>">;
def opt_remarks_passes: Separate<["--"], "opt-remarks-passes">,
  HelpText<"Regex for the passes that need to be serialized to the output file">;
def opt_remarks_with_hotness: FF<"opt-remarks-with-hotness">,
  HelpText<"Include hotness information in the optimization remarks file">;
def opt_remarks_format: Separate<["--"], "opt-remarks-format">,
  HelpText<"The format used for serializing remarks (default: YAML)">;
def save_temps: F<"save-temps">, HelpText<"Save intermediate LTO compilation results">;
def save_temps_eq: JJ<"save-temps=">, HelpText<"Save select intermediate LTO compilation results">,
  Values<"resolution,preopt,promote,internalize,import,opt,precodegen,prelink,combinedindex">;
def lto_basic_block_sections: JJ<"lto-basic-block-sections=">,
  HelpText<"Enable basic block sections for LTO">;
defm lto_basic_block_address_map: BB<"lto-basic-block-address-map",
  "Emit basic block address map for LTO",
  "Do not emit basic block address map for LTO (default)">;
defm lto_unique_basic_block_section_names: BB<"lto-unique-basic-block-section-names",
    "Give unique names to every basic block section for LTO",
    "Do not give unique names to every basic block section for LTO (default)">;
defm shuffle_sections: EEq<"shuffle-sections",
  "Shuffle matched sections using the given seed before mapping them to the output sections. "
  "If -1, reverse the section order. If 0, use a random seed">,
  MetaVarName<"<section-glob>=<seed>">;
def thinlto_cache_dir: JJ<"thinlto-cache-dir=">,
  HelpText<"Path to ThinLTO cached object file directory">;
defm thinlto_cache_policy: EEq<"thinlto-cache-policy", "Pruning policy for the ThinLTO cache">;
def thinlto_emit_imports_files: FF<"thinlto-emit-imports-files">;
def thinlto_emit_index_files: FF<"thinlto-emit-index-files">;
def thinlto_index_only: FF<"thinlto-index-only">;
def thinlto_index_only_eq: JJ<"thinlto-index-only=">;
def thinlto_jobs_eq: JJ<"thinlto-jobs=">,
  HelpText<"Number of ThinLTO jobs. Default to --threads=">;
def thinlto_object_suffix_replace_eq: JJ<"thinlto-object-suffix-replace=">;
def thinlto_prefix_replace_eq: JJ<"thinlto-prefix-replace=">;
def thinlto_single_module_eq: JJ<"thinlto-single-module=">,
  HelpText<"Specify a single module to compile in ThinLTO mode, for debugging only">;

defm fat_lto_objects: BB<"fat-lto-objects",
    "Use the .llvm.lto section, which contains LLVM bitcode, in fat LTO object files to perform LTO.",
    "Ignore the .llvm.lto section in relocatable object files (default).">;

def: J<"plugin-opt=O">, Alias<lto_O>, HelpText<"Alias for --lto-O">;
def: F<"plugin-opt=debug-pass-manager">,
  Alias<lto_debug_pass_manager>, HelpText<"Alias for --lto-debug-pass-manager">;
def: F<"plugin-opt=disable-verify">, Alias<disable_verify>, HelpText<"Alias for --disable-verify">;
def plugin_opt_dwo_dir_eq: J<"plugin-opt=dwo_dir=">,
  HelpText<"Directory to store .dwo files when LTO and debug fission are used">;
def plugin_opt_emit_asm: F<"plugin-opt=emit-asm">,
  Alias<lto_emit_asm>, HelpText<"Alias for --lto-emit-asm">;
def plugin_opt_emit_llvm: F<"plugin-opt=emit-llvm">,
  Alias<lto_emit_llvm>, HelpText<"Alias for --lto-emit-llvm">;
def: J<"plugin-opt=jobs=">, Alias<thinlto_jobs_eq>, HelpText<"Alias for --thinlto-jobs=">;
def: J<"plugin-opt=lto-partitions=">, Alias<lto_partitions>, HelpText<"Alias for --lto-partitions">;
def plugin_opt_mcpu_eq: J<"plugin-opt=mcpu=">;
def: F<"plugin-opt=cs-profile-generate">,
  Alias<lto_cs_profile_generate>, HelpText<"Alias for --lto-cs-profile-generate">;
def: J<"plugin-opt=cs-profile-path=">,
  Alias<lto_cs_profile_file>, HelpText<"Alias for --lto-cs-profile-file">;
def: J<"plugin-opt=obj-path=">,
  Alias<lto_obj_path_eq>,
  HelpText<"Alias for --lto-obj-path=">;
def: J<"plugin-opt=opt-remarks-filename=">,
  Alias<opt_remarks_filename>,
  HelpText<"Alias for --opt-remarks-filename">;
def: J<"plugin-opt=opt-remarks-passes=">,
  Alias<opt_remarks_passes>,
  HelpText<"Alias for --opt-remarks-passes">;
def: J<"plugin-opt=opt-remarks-format=">,
  Alias<opt_remarks_format>,
  HelpText<"Alias for --opt-remarks-format">;
def: F<"plugin-opt=opt-remarks-with-hotness">,
  Alias<opt_remarks_with_hotness>,
  HelpText<"Alias for --opt-remarks-with_hotness">;
def: J<"plugin-opt=opt-remarks-hotness-threshold=">,
  Alias<opt_remarks_hotness_threshold>,
  HelpText<"Alias for --opt-remarks-hotness-threshold">;
def: J<"plugin-opt=sample-profile=">,
  Alias<lto_sample_profile>, HelpText<"Alias for --lto-sample-profile">;
def: F<"plugin-opt=save-temps">, Alias<save_temps>, HelpText<"Alias for --save-temps">;
def plugin_opt_stats_file: J<"plugin-opt=stats-file=">,
  HelpText<"Filename to write LTO statistics to">;
def: F<"plugin-opt=thinlto-emit-imports-files">,
  Alias<thinlto_emit_imports_files>,
  HelpText<"Alias for --thinlto-emit-imports-files">;
def: F<"plugin-opt=thinlto-index-only">,
  Alias<thinlto_index_only>,
  HelpText<"Alias for --thinlto-index-only">;
def: J<"plugin-opt=thinlto-index-only=">,
  Alias<thinlto_index_only_eq>,
  HelpText<"Alias for --thinlto-index-only=">;
def: J<"plugin-opt=thinlto-object-suffix-replace=">,
 Alias<thinlto_object_suffix_replace_eq>,
 HelpText<"Alias for --thinlto-object-suffix-replace=">;
def: J<"plugin-opt=thinlto-prefix-replace=">,
  Alias<thinlto_prefix_replace_eq>,
  HelpText<"Alias for --thinlto-prefix-replace=">;

// Ignore LTO plugin-related options.
// clang -flto passes -plugin and -plugin-opt to the linker. This is required
// for ld.gold and ld.bfd to get LTO working. But it's not for lld which doesn't
// rely on a plugin. Instead of detecting which linker is used on clang side we
// just ignore the option on lld side as it's easier. In fact, the linker could
// be called 'ld' and understanding which linker is used would require parsing of
// --version output.
defm plugin: Eq<"plugin", "Ignored for compatibility with GNU linkers">;

def plugin_opt_eq_minus: J<"plugin-opt=-">,
  HelpText<"Specify an LLVM option for compatibility with LLVMgold.so">;
def: J<"plugin-opt=thinlto">;

// Ignore GCC collect2 LTO plugin related options. Note that we don't support
// GCC LTO, but GCC collect2 passes these options even in non-LTO mode.
def: J<"plugin-opt=-fresolution=">;
def: J<"plugin-opt=-pass-through=">;
// This may be either an unhandled LLVMgold.so feature or GCC passed
// -plugin-opt=path/to/{liblto_plugin.so,lto-wrapper}
def plugin_opt_eq : J<"plugin-opt=">;

// Options listed below are silently ignored for now for compatibility.
def: Flag<["-"], "d">;
def: Flag<["-"], "g">;
def: F<"long-plt">;
def: FF<"no-add-needed">;
def: F<"no-copy-dt-needed-entries">;
def: F<"no-ctors-in-init-array">;
def: F<"no-keep-memory">;
def: Separate<["--", "-"], "rpath-link">;
def: J<"rpath-link=">;
def: F<"secure-plt">;
def: F<"sort-common">;
def: F<"stats">;
def: F<"warn-execstack">;
def: F<"warn-once">;
def: F<"warn-shared-textrel">;
def: JoinedOrSeparate<["-"], "G">;

// Hidden option used for testing MIPS multi-GOT implementation.
defm mips_got_size:
  Eq<"mips-got-size", "Max size of a single MIPS GOT. 0x10000 by default.">,
  Flags<[HelpHidden]>;

// Hidden option used to opt-in to additional output checks.
defm check_dynamic_relocations: BB<"check-dynamic-relocations",
    "Perform additional validation of the written dynamic relocations",
    "Do not perform additional validation of the written dynamic relocations">,
  Flags<[HelpHidden]>;

defm load_pass_plugins: EEq<"load-pass-plugin", "Load passes from plugin library">;

// Hidden options, used by clang's -fsanitize=memtag-* options to emit an ELF
// note to designate what kinds of memory (stack/heap) should be protected using
// ARM's MTE on armv8.5+. A binary's desire for stack MTE can't be obtained
// implicitly, so we have a specific bit in the note to signal to the loader to
// remap the stack as PROT_MTE.
defm android_memtag_stack: BB<"android-memtag-stack",
    "Instruct the dynamic loader to prepare for MTE stack instrumentation", "">;
defm android_memtag_heap: BB<"android-memtag-heap",
    "Instruct the dynamic loader to enable MTE protection for the heap", "">;
defm android_memtag_mode: EEq<"android-memtag-mode",
    "Instruct the dynamic loader to start under MTE mode {async, sync, none}">;
