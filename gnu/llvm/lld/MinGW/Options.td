include "llvm/Option/OptParser.td"

class F<string name>: Flag<["--", "-"], name>;
class J<string name>: Joined<["--", "-"], name>;
class S<string name>: Separate<["--", "-"], name>;

// Convenience classes for long options which only accept two dashes. For lld
// specific or newer long options, we prefer two dashes to avoid collision with
// short options. For many others, we have to accept both forms to be compatible
// with GNU ld.
class FF<string name> : Flag<["--"], name>;
class JJ<string name>: Joined<["--"], name>;

multiclass Eq<string name, string help> {
  def NAME: Separate<["--", "-"], name>;
  def NAME # _eq: Joined<["--", "-"], name # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
}

multiclass EEq<string name, string help> {
  def NAME: Separate<["--"], name>;
  def NAME # _eq: Joined<["--"], name # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
}

multiclass EqLong<string name, string help> {
  def NAME: Separate<["--"], name>;
  def NAME # _eq: Joined<["--"], name # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
}

multiclass EqNoHelp<string name> {
  def NAME: Separate<["--", "-"], name>;
  def NAME # _eq: Joined<["--", "-"], name # "=">, Alias<!cast<Separate>(NAME)>;
}

multiclass B<string name, string help1, string help2> {
  def NAME: Flag<["--", "-"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--", "-"], "no-" # name>, HelpText<help2>;
}

multiclass B_disable<string name, string help1, string help2> {
  def NAME: Flag<["--", "-"], name>, HelpText<help1>;
  def disable_ # NAME: Flag<["--", "-"], "disable-" # name>, HelpText<help2>;
}

multiclass B_enable_disable<string name, string help1, string help2> {
  def enable_ # NAME: Flag<["--", "-"], "enable-" # name>, HelpText<help1>;
  def disable_ # NAME: Flag<["--", "-"], "disable-" # name>, HelpText<help2>;
}

def L: JoinedOrSeparate<["-"], "L">, MetaVarName<"<dir>">,
  HelpText<"Add a directory to the library search path">;
defm allow_multiple_definition: B<"allow-multiple-definition",
    "Allow multiple definitions",
    "Do not allow multiple definitions (default)">;
def Bdynamic: F<"Bdynamic">, HelpText<"Link against shared libraries">;
def Bstatic: F<"Bstatic">, HelpText<"Do not link against shared libraries">;
defm demangle: B<"demangle",
    "Demangle symbol names (default)",
    "Do not demangle symbol names">;
def disable_auto_import: F<"disable-auto-import">,
    HelpText<"Don't automatically import data symbols from other DLLs without dllimport">;
def disable_runtime_pseudo_reloc: F<"disable-runtime-pseudo-reloc">,
    HelpText<"Don't do automatic imports that require runtime fixups">;
def disable_stdcall_fixup: F<"disable-stdcall-fixup">,
    HelpText<"Don't resolve stdcall/fastcall/vectorcall to undecorated symbols">;
defm dynamicbase: B_disable<"dynamicbase", "Enable ASLR", "Disable ASLR">;
def enable_auto_import: F<"enable-auto-import">,
    HelpText<"Automatically import data symbols from other DLLs where needed">;
def enable_runtime_pseudo_reloc: F<"enable-runtime-pseudo-reloc">,
    HelpText<"Allow automatic imports that require runtime fixups">;
def enable_stdcall_fixup: F<"enable-stdcall-fixup">,
    HelpText<"Resolve stdcall/fastcall/vectorcall to undecorated symbols without warnings">;
defm entry: Eq<"entry", "Name of entry point symbol">, MetaVarName<"<entry>">;
def exclude_all_symbols: F<"exclude-all-symbols">,
    HelpText<"Don't automatically export any symbols">;
defm exclude_symbols: Eq<"exclude-symbols",
    "Exclude symbols from automatic export">, MetaVarName<"<symbol[,symbol,...]>">;
def export_all_symbols: F<"export-all-symbols">,
    HelpText<"Export all symbols even if a def file or dllexport attributes are used">;
defm fatal_warnings: B<"fatal-warnings",
    "Treat warnings as errors",
    "Do not treat warnings as errors (default)">;
defm file_alignment: Eq<"file-alignment", "Set file alignment">;
defm gc_sections: B<"gc-sections",
    "Remove unused sections",
    "Don't remove unused sections">;
defm heap: Eq<"heap", "Set size of the initial heap">;
def help: F<"help">, HelpText<"Print option help">;
defm high_entropy_va: B_disable<"high-entropy-va",
    "Set the 'high entropy VA' flag", "Don't set the 'high entropy VA' flag">;
defm icf: Eq<"icf", "Identical code folding">;
defm image_base: Eq<"image-base", "Base address of the program">;
defm insert_timestamp: B<"insert-timestamp",
    "Include PE header timestamp",
    "Don't include PE header timestamp">;
def kill_at: F<"kill-at">, HelpText<"Remove @n from exported symbols">;
def l: JoinedOrSeparate<["-"], "l">, MetaVarName<"<libName>">,
  HelpText<"Root name of library to use">;
def m: JoinedOrSeparate<["-"], "m">, HelpText<"Set target emulation">;
defm major_os_version: EqLong<"major-os-version",
     "Set the OS and subsystem major version">;
defm major_subsystem_version: EqLong<"major-subsystem-version",
     "Set the OS and subsystem major version">;
defm map: Eq<"Map", "Output a linker map">;
defm minor_os_version: EqLong<"minor-os-version",
     "Set the OS and subsystem minor version">;
defm minor_subsystem_version: EqLong<"minor-subsystem-version",
     "Set the OS and subsystem minor version">;
defm no_seh: B_disable<"no-seh",
     "Set the 'no SEH' flag in the executable", "Don't set the 'no SEH' flag">;
defm nxcompat: B_disable<"nxcompat",
    "Set the 'nxcompat' flag in the executable", "Don't set the 'nxcompat' flag">;
def large_address_aware: Flag<["--"], "large-address-aware">,
    HelpText<"Enable large addresses">;
def o: JoinedOrSeparate<["-"], "o">, MetaVarName<"<path>">,
  HelpText<"Path to file to write output">;
defm out_implib: Eq<"out-implib", "Import library name">;
defm output_def: Eq<"output-def", "Output def file">;
defm reloc_section: B_enable_disable<"reloc-section",
     "Enable base relocations", "Disable base relocations">;
defm section_alignment: Eq<"section-alignment", "Set section alignment">;
def shared: F<"shared">, HelpText<"Build a shared object">;
defm subs: Eq<"subsystem", "Specify subsystem">;
defm stack: Eq<"stack", "Set size of the initial stack">;
def strip_all: F<"strip-all">,
    HelpText<"Omit all symbol information from the output binary">;
def strip_debug: F<"strip-debug">,
    HelpText<"Omit all debug information, but keep symbol information">;
defm reproduce: Eq<"reproduce",
     "Write a tar file containing input files and command line options to reproduce link">;
defm require_defined: Eq<"require-defined",
     "Force symbol to be added to symbol table as an undefined one">;
defm threads
    : EEq<"threads",
         "Number of threads. '1' disables multi-threading. By default all "
         "available hardware threads are used">;
defm tsaware: B_disable<"tsaware",
    "Set the 'Terminal Server aware' flag", "Don't set the 'Terminal Server aware' flag">;
defm undefined: Eq<"undefined", "Include symbol in the link, if available">;
defm whole_archive: B<"whole-archive",
    "Include all object files for following archives",
    "No longer include all object files for following archives">;
def v: Flag<["-"], "v">, HelpText<"Display the version number">;
def verbose: F<"verbose">, HelpText<"Verbose mode">;
def version: F<"version">, HelpText<"Display the version number and exit">;
defm wrap: Eq<"wrap", "Use wrapper functions for symbol">,
     MetaVarName<"<symbol>">;


// LLD specific options, for LTO, shared with the ELF backend
def lto_O: JJ<"lto-O">, MetaVarName<"<opt-level>">,
  HelpText<"Optimization level for LTO">;
def lto_CGO: JJ<"lto-CGO">, MetaVarName<"<cgopt-level>">,
  HelpText<"Codegen optimization level for LTO">;
def lto_cs_profile_generate: FF<"lto-cs-profile-generate">,
  HelpText<"Perform context sensitive PGO instrumentation">;
def lto_cs_profile_file: JJ<"lto-cs-profile-file=">,
  HelpText<"Context sensitive profile file path">;
def lto_emit_asm: FF<"lto-emit-asm">,
  HelpText<"Emit assembly code">;
def lto_sample_profile: JJ<"lto-sample-profile=">,
  HelpText<"Sample profile file path">;

def thinlto_cache_dir: JJ<"thinlto-cache-dir=">,
  HelpText<"Path to ThinLTO cached object file directory">;
defm thinlto_cache_policy: EEq<"thinlto-cache-policy", "Pruning policy for the ThinLTO cache">;
def thinlto_emit_imports_files: FF<"thinlto-emit-imports-files">;
def thinlto_index_only: FF<"thinlto-index-only">;
def thinlto_index_only_eq: JJ<"thinlto-index-only=">;
def thinlto_jobs_eq: JJ<"thinlto-jobs=">,
  HelpText<"Number of ThinLTO jobs. Default to --threads=">;
def thinlto_object_suffix_replace_eq: JJ<"thinlto-object-suffix-replace=">;
def thinlto_prefix_replace_eq: JJ<"thinlto-prefix-replace=">;

def plugin_opt_eq_minus: J<"plugin-opt=-">,
  HelpText<"Specify an LLVM option for compatibility with LLVMgold.so">;
def: J<"plugin-opt=thinlto">;

def: J<"plugin-opt=O">, Alias<lto_O>, HelpText<"Alias for --lto-O">;
def: F<"plugin-opt=cs-profile-generate">,
  Alias<lto_cs_profile_generate>, HelpText<"Alias for --lto-cs-profile-generate">;
def: J<"plugin-opt=cs-profile-path=">,
  Alias<lto_cs_profile_file>, HelpText<"Alias for --lto-cs-profile-file">;
def plugin_opt_dwo_dir_eq: J<"plugin-opt=dwo_dir=">,
  HelpText<"Directory to store .dwo files when LTO and debug fission are used">;
def plugin_opt_emit_asm: F<"plugin-opt=emit-asm">,
  Alias<lto_emit_asm>, HelpText<"Alias for --lto-emit-asm">;
def plugin_opt_emit_llvm: F<"plugin-opt=emit-llvm">;
def: J<"plugin-opt=jobs=">, Alias<thinlto_jobs_eq>, HelpText<"Alias for --thinlto-jobs=">;
def plugin_opt_mcpu_eq: J<"plugin-opt=mcpu=">;

// This may be either an unhandled LLVMgold.so feature or GCC passed
// -plugin-opt=path/to/{liblto_plugin.so,lto-wrapper}
def plugin_opt_eq : J<"plugin-opt=">;

// LLD specific options
def _HASH_HASH_HASH : Flag<["-"], "###">,
    HelpText<"Print (but do not run) the commands to run for this compilation">;
def appcontainer: F<"appcontainer">, HelpText<"Set the appcontainer flag in the executable">;
defm delayload: Eq<"delayload", "DLL to load only on demand">;
defm mllvm: EqNoHelp<"mllvm">;
defm pdb: Eq<"pdb", "Output PDB debug info file, chosen implicitly if the argument is empty">;
defm Xlink : Eq<"Xlink", "Pass <arg> to the COFF linker">, MetaVarName<"<arg>">;
defm guard_cf : B<"guard-cf", "Enable Control Flow Guard" ,
  "Do not enable Control Flow Guard (default)">;
defm guard_longjmp : B<"guard-longjmp",
  "Enable Control Flow Guard long jump hardening (default for --guard-cf)" ,
  "Do not enable Control Flow Guard long jump hardening">;
defm error_limit:
  EqLong<"error-limit", "Maximum number of errors to emit before stopping (0 = no limit)">;
def build_id: J<"build-id=">, HelpText<"Generate build ID note (pass none to disable)">, 
  MetaVarName<"<arg>">;
def : F<"build-id">, Alias<build_id>, HelpText<"Alias for --build-id=">;

// Alias
def alias_Bdynamic_call_shared: Flag<["-"], "call_shared">, Alias<Bdynamic>;
def alias_Bdynamic_dy: Flag<["-"], "dy">, Alias<Bdynamic>;
def alias_Bstatic_dn: Flag<["-"], "dn">, Alias<Bstatic>;
def alias_Bstatic_non_shared: Flag<["-"], "non_shared">, Alias<Bstatic>;
def alias_Bstatic_static: Flag<["-"], "static">, Alias<Bstatic>;
def alias_dll: F<"dll">, Alias<shared>;
def alias_entry_e: JoinedOrSeparate<["-"], "e">, Alias<entry>;
def alias_no_dynamicbase: F<"no-dynamicbase">, Alias<disable_dynamicbase>;
def alias_strip_s: Flag<["-"], "s">, Alias<strip_all>;
def alias_strip_S: Flag<["-"], "S">, Alias<strip_debug>;
def alias_undefined_u: JoinedOrSeparate<["-"], "u">, Alias<undefined>;

// Ignored options
def: Joined<["-"], "O">;
def: F<"as-needed">;
def: F<"disable-auto-image-base">;
def: F<"enable-auto-image-base">;
def: F<"end-group">;
def: Flag<["--"], "full-shutdown">;
defm: EqNoHelp<"major-image-version">;
defm: EqNoHelp<"minor-image-version">;
def: F<"no-undefined">;
def: F<"pic-executable">;
defm: EqNoHelp<"plugin">;
defm: EqNoHelp<"sysroot">;
def: F<"sort-common">;
def: F<"start-group">;

// Ignored options, that produce warnings
defm rpath: EqNoHelp<"rpath">;

// Ignore GCC collect2 LTO plugin related options. Note that we don't support
// GCC LTO, but GCC collect2 passes these options even in non-LTO mode.
def: J<"plugin-opt=-fresolution=">;
def: J<"plugin-opt=-pass-through=">;
