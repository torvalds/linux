include "llvm/Option/OptParser.td"

// link.exe accepts options starting with either a dash or a slash.

// Flag that takes no arguments.
class F<string name> : Flag<["/", "-", "/?", "-?"], name>;

// Flag that takes one argument after ":".
class P<string name, string help> :
      Joined<["/", "-", "/?", "-?"], name#":">, HelpText<help>;

// Same as P<> above, but without help texts, for private undocumented
// options.
class P_priv<string name> :
      Joined<["/", "-", "/?", "-?"], name#":">;

// Boolean flag which can be suffixed by ":no". Using it unsuffixed turns the
// flag on and using it suffixed by ":no" turns it off.
multiclass B<string name, string help_on, string help_off> {
  def "" : F<name>, HelpText<help_on>;
  def _no : F<name#":no">, HelpText<help_off>;
}

// Same as B<> above, but without help texts, for private undocumented
// options.
multiclass B_priv<string name> {
  def "" : F<name>;
  def _no : F<name#":no">;
}

def align   : P<"align", "Section alignment">;
def aligncomm : P<"aligncomm", "Set common symbol alignment">;
def alternatename : P<"alternatename", "Define weak alias">;
def base    : P<"base", "Base address of the program">;
def color_diagnostics: Flag<["--"], "color-diagnostics">,
    HelpText<"Alias for --color-diagnostics=always">;
def no_color_diagnostics: Flag<["--"], "no-color-diagnostics">,
    HelpText<"Alias for --color-diagnostics=never">;
def color_diagnostics_eq: Joined<["--"], "color-diagnostics=">,
    HelpText<"Use colors in diagnostics (default: auto)">,
    MetaVarName<"[auto,always,never]">;
def defaultlib : P<"defaultlib", "Add the library to the list of input files">;
def delayload : P<"delayload", "Delay loaded DLL name">;
def diasdkdir : P<"diasdkdir", "Set the location of the DIA SDK">;
def dwodir : P<"dwodir",
    "Directory to store .dwo files when LTO and debug fission are used">;
def entry   : P<"entry", "Name of entry point symbol">;
def errorlimit : P<"errorlimit",
    "Maximum number of errors to emit before stopping (0 = no limit)">;
def exclude_symbols  : P<"exclude-symbols", "Exclude symbols from automatic export">,
    MetaVarName<"<symbol[,symbol,...]>">;
def export  : P<"export", "Export a function">;
// No help text because /failifmismatch is not intended to be used by the user.
def failifmismatch : P<"failifmismatch", "">;
def filealign : P<"filealign", "Section alignment in the output file">;
def functionpadmin : F<"functionpadmin">;
def functionpadmin_opt : P<"functionpadmin",
    "Prepares an image for hotpatching">;
def dependentloadflag : F<"dependentloadflag">;
def dependentloadflag_opt : P<"dependentloadflag",
    "Sets the default load flags used to resolve the statically linked imports of a module">;
def guard   : P<"guard", "Control flow guard">;
def heap    : P<"heap", "Size of the heap">;
def ignore : P<"ignore", "Specify warning codes to ignore">;
def implib  : P<"implib", "Import library name">;
def noimplib : F<"noimplib">,
    HelpText<"Don't output an import lib">;
def lib : F<"lib">,
    HelpText<"Act like lib.exe; must be first argument if present">;
def libpath : P<"libpath", "Additional library search path">;
def linkrepro : Joined<["/", "-", "/?", "-?"], "linkrepro:">,
    MetaVarName<"directory">,
    HelpText<"Write repro.tar containing inputs and command to reproduce link">;
def lldignoreenv : F<"lldignoreenv">,
    HelpText<"Ignore environment variables like %LIB%">;
def lldltocache : P<"lldltocache",
    "Path to ThinLTO cached object file directory">;
def lldltocachepolicy : P<"lldltocachepolicy",
    "Pruning policy for the ThinLTO cache">;
def lldsavetemps : F<"lldsavetemps">,
    HelpText<"Save intermediate LTO compilation results">;
def lto_sample_profile: P<"lto-sample-profile", "Sample profile file path">;
def machine : P<"machine", "Specify target platform">;
def merge   : P<"merge", "Combine sections">;
def mllvm   : P<"mllvm", "Options to pass to LLVM">;
def nodefaultlib : P<"nodefaultlib", "Remove a default library">;
def opt     : P<"opt", "Control optimizations">;
def order   : P<"order", "Put functions in order">;
def out     : P<"out", "Path to file to write output">;
def natvis : P<"natvis", "Path to natvis file to embed in the PDB">;
def pdb : P<"pdb", "PDB file path">;
def pdbaltpath : P<"pdbaltpath", "PDB file path to embed in the image">;
def pdbpagesize : P<"pdbpagesize", "PDB page size">;
def pdbstripped : P<"pdbstripped", "Stripped PDB file path">;
def pdbstream : Joined<["/", "-", "/?", "-?"], "pdbstream:">,
    MetaVarName<"<name>=<file>">,
    HelpText<"Embed the contents of <file> in the PDB as named stream <name>">;
def section : P<"section", "Specify section attributes">;
def stack   : P<"stack", "Size of the stack">;
def stub    : P<"stub", "Specify DOS stub file">;
def subsystem : P<"subsystem", "Specify subsystem">;
def timestamp : P<"timestamp", "Specify the PE header timestamp">;
def vctoolsdir : P<"vctoolsdir", "Set the location of the VC tools">;
def vctoolsversion : P<"vctoolsversion",
    "Specify which VC tools version to use">;
def version : P<"version", "Specify a version number in the PE header">;
def wholearchive_file : P<"wholearchive",
    "Include all object files from this library">;
def winsdkdir : P<"winsdkdir", "Set the location of the Windows SDK">;
def winsdkversion : P<"winsdkversion", "Specify which SDK version to use">;
def winsysroot : P<"winsysroot",
    "Adds several subdirectories to the library search paths">;

def disallowlib : Joined<["/", "-", "/?", "-?"], "disallowlib:">,
    Alias<nodefaultlib>;

def manifest : F<"manifest">, HelpText<"Create .manifest file">;
def manifest_colon : P<
    "manifest",
    "NO disables manifest output; EMBED[,ID=#] embeds manifest as resource in the image">;
def manifestuac : P<"manifestuac", "User access control">;
def manifestfile : P<"manifestfile", "Manifest output path, with /manifest">;
def manifestdependency : P<
    "manifestdependency",
    "Attributes for <dependency> element in manifest file; implies /manifest">;
def manifestinput : P<
    "manifestinput",
    "Additional manifest inputs; only valid with /manifest:embed">;

// We cannot use multiclass P because class name "incl" is different
// from its command line option name. We do this because "include" is
// a reserved keyword in tablegen.
def incl : Joined<["/", "-", "/?", "-?"], "include:">,
    HelpText<"Force symbol to be added to symbol table as undefined one">;

// "def" is also a keyword.
def deffile : Joined<["/", "-", "/?", "-?"], "def:">,
    HelpText<"Use module-definition file">;

def debug : F<"debug">, HelpText<"Embed a symbol table in the image">;
def debug_opt : P<"debug", "Embed a symbol table in the image with option">;
def debugtype : P<"debugtype", "Debug Info Options">;
def dll : F<"dll">, HelpText<"Create a DLL">;
def driver : F<"driver">, HelpText<"Generate a Windows NT Kernel Mode Driver">;
def driver_wdm : F<"driver:wdm">,
    HelpText<"Set IMAGE_FILE_UP_SYSTEM_ONLY bit in PE header">;
def driver_uponly : F<"driver:uponly">,
    HelpText<"Set IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER bit in PE header">;
def driver_wdm_uponly : F<"driver:wdm,uponly">;
def driver_uponly_wdm : F<"driver:uponly,wdm">;
def nodefaultlib_all : F<"nodefaultlib">,
    HelpText<"Remove all default libraries">;
def noentry : F<"noentry">,
    HelpText<"Don't add reference to DllMainCRTStartup; only valid with /dll">;
def profile : F<"profile">;
def repro : F<"Brepro">,
    HelpText<"Use a hash of the executable as the PE header timestamp">;
def reproduce : Joined<["/", "-", "/?", "-?"], "reproduce:">,
    MetaVarName<"filename">,
    HelpText<"Write tar file containing inputs and command to reproduce link">;
def swaprun : P<"swaprun",
  "Comma-separated list of 'cd' or 'net'">;
def swaprun_cd : F<"swaprun:cd">, Alias<swaprun>, AliasArgs<["cd"]>,
  HelpText<"Make loader run output binary from swap instead of from CD">;
def swaprun_net : F<"swaprun:net">, Alias<swaprun>, AliasArgs<["net"]>,
  HelpText<"Make loader run output binary from swap instead of from network">;
def verbose : F<"verbose">;
def wholearchive_flag : F<"wholearchive">,
    HelpText<"Include all object files from all libraries">;
def release : F<"release">,
    HelpText<"Set the Checksum in the header of an PE file">;

def force : F<"force">,
    HelpText<"Allow undefined and multiply defined symbols">;
def force_unresolved : F<"force:unresolved">,
    HelpText<"Allow undefined symbols when creating executables">;
def force_multiple : F<"force:multiple">,
    HelpText<"Allow multiply defined symbols when creating executables">;
def force_multipleres : F<"force:multipleres">,
    HelpText<"Allow multiply defined resources when creating executables">;
defm WX : B<"WX", "Treat warnings as errors",
                  "Don't treat warnings as errors (default)">;

defm allowbind : B<"allowbind", "Enable DLL binding (default)",
                   "Disable DLL binding">;
defm allowisolation : B<"allowisolation", "Enable DLL isolation (default)",
                        "Disable DLL isolation">;
defm appcontainer : B<"appcontainer",
                      "Image can only be run in an app container",
                      "Image can run outside an app container (default)">;
defm cetcompat : B<"cetcompat", "Mark executable image as compatible with Control-flow Enforcement Technology (CET) Shadow Stack",
                   "Don't mark executable image as compatible with Control-flow Enforcement Technology (CET) Shadow Stack (default)">;
defm dynamicbase : B<"dynamicbase", "Enable ASLR (default unless /fixed)",
                     "Disable ASLR (default when /fixed)">;
defm fixed : B<"fixed", "Disable base relocations",
               "Enable base relocations (default)">;
defm highentropyva : B<"highentropyva",
                       "Enable 64-bit ASLR (default on 64-bit)",
                       "Disable 64-bit ASLR">;
defm incremental : B<"incremental",
                     "Keep original import library if contents are unchanged",
                     "Overwrite import library even if contents are unchanged">;
defm inferasanlibs : B<"inferasanlibs",
                       "Unused, generates a warning",
                       "No effect (default)">;
defm integritycheck : B<"integritycheck",
                        "Set FORCE_INTEGRITY bit in PE header",
                        "No effect (default)">;
defm largeaddressaware : B<"largeaddressaware",
                           "Enable large addresses (default on 64-bit)",
                           "Disable large addresses (default on 32-bit)">;
defm nxcompat : B<"nxcompat", "Enable data execution prevention (default)",
                  "Disable data execution provention">;
defm safeseh : B<"safeseh",
                 "Produce an image with Safe Exception Handler (only for x86)",
                 "Don't produce an image with Safe Exception Handler">;
defm tsaware  : B<"tsaware",
                  "Create Terminal Server aware executable (default)",
                  "Create non-Terminal Server aware executable">;

def help : F<"help">;

// /?? and -?? must be before /? and -? to not confuse lib/Options.
def help_q : Flag<["/??", "-??", "/?", "-?"], "">, Alias<help>;

// LLD extensions
defm auto_import : B_priv<"auto-import">;
defm runtime_pseudo_reloc : B_priv<"runtime-pseudo-reloc">;
def end_lib : F<"end-lib">,
  HelpText<"End group of objects treated as if they were in a library">;
def exclude_all_symbols : F<"exclude-all-symbols">;
def export_all_symbols : F<"export-all-symbols">;
defm demangle : B<"demangle",
    "Demangle symbols in output (default)",
    "Do not demangle symbols in output">;
def include_optional : Joined<["/", "-", "/?", "-?"], "includeoptional:">,
    HelpText<"Add symbol as undefined, but allow it to remain undefined">;
def kill_at : F<"kill-at">;
defm lld_allow_duplicate_weak : B_priv<"lld-allow-duplicate-weak">;
def lldemit : P<"lldemit", "Specify output type">;
def lldmingw : F<"lldmingw">;
def noseh : F<"noseh">;
def osversion : P_priv<"osversion">;
def output_def : Joined<["/", "-", "/?", "-?"], "output-def:">;
def pdb_source_path : P<"pdbsourcepath",
    "Base path used to make relative source file path absolute in PDB">;
def rsp_quoting : Joined<["--"], "rsp-quoting=">,
  HelpText<"Quoting style for response files, 'windows' (default) or 'posix'">;
def start_lib : F<"start-lib">,
  HelpText<"Start group of objects treated as if they were in a library">;
defm stdcall_fixup : B_priv<"stdcall-fixup">;
def thinlto_emit_imports_files :
    F<"thinlto-emit-imports-files">,
    HelpText<"Emit .imports files with -thinlto-index-only">;
def thinlto_index_only :
    F<"thinlto-index-only">,
    HelpText<"Instead of linking, emit ThinLTO index files">;
def thinlto_index_only_arg : P<
    "thinlto-index-only",
    "-thinlto-index-only and also write native module names to file">;
def thinlto_object_suffix_replace : P<
    "thinlto-object-suffix-replace",
    "'old;new' replace old suffix with new suffix in ThinLTO index">;
def thinlto_prefix_replace: P<
    "thinlto-prefix-replace",
    "'old;new' replace old prefix with new prefix in ThinLTO outputs">;
def lto_obj_path : P<
    "lto-obj-path",
    "output native object for merged LTO unit to this path">;
def lto_cs_profile_generate: F<"lto-cs-profile-generate">,
    HelpText<"Perform context sensitive PGO instrumentation">;
def lto_cs_profile_file : P<"lto-cs-profile-file",
    "Context sensitive profile file path">;
defm lto_pgo_warn_mismatch: B<
     "lto-pgo-warn-mismatch",
     "turn on warnings about profile cfg mismatch (default)>",
     "turn off warnings about profile cfg mismatch">;
def dash_dash_version : Flag<["--"], "version">,
  HelpText<"Display the version number and exit">;
def threads
    : P<"threads", "Number of threads. '1' disables multi-threading. By "
                   "default all available hardware threads are used">;
def call_graph_ordering_file: P<
    "call-graph-ordering-file",
    "Layout sections to optimize the given callgraph">;
defm call_graph_profile_sort: B<
    "call-graph-profile-sort",
    "Reorder sections with call graph profile (default)",
    "Do not reorder sections with call graph profile">;
def print_symbol_order: P<
    "print-symbol-order",
    "Print a symbol order specified by /call-graph-ordering-file and "
    "/call-graph-profile-sort into the specified file">;
def wrap : P_priv<"wrap">;

def vfsoverlay : P<"vfsoverlay", "Path to a vfsoverlay yaml file to optionally look for /defaultlib's in">;

def time_trace_eq: Joined<["--"], "time-trace=">, MetaVarName<"<file>">,
  HelpText<"Record time trace to <file>">;
def : Flag<["--"], "time-trace">, Alias<time_trace_eq>,
  HelpText<"Record time trace to file next to output">;

def time_trace_granularity_eq: Joined<["--"], "time-trace-granularity=">,
    HelpText<"Minimum time granularity (in microseconds) traced by time profiler">;

defm build_id: B<
     "build-id", 
     "Generate build ID (always on when generating PDB)",
     "Do not Generate build ID">;

// Flags for debugging
def lldmap : F<"lldmap">;
def lldmap_file : P_priv<"lldmap">;
def map : F<"map">;
def map_file : P_priv<"map">;
def map_info : P<"mapinfo", "Include the specified information in a map file">;
def print_search_paths : F<"print-search-paths">;
def show_timing : F<"time">;
def summary : F<"summary">;

//==============================================================================
// The flags below do nothing. They are defined only for link.exe compatibility.
//==============================================================================

def ignoreidl : F<"ignoreidl">;
def ltcg : F<"ltcg">;
def assemblydebug : F<"assemblydebug">;
def nologo : F<"nologo">;
def throwingnew : F<"throwingnew">;
def editandcontinue : F<"editandcontinue">;
def fastfail : F<"fastfail">;
def kernel : F<"kernel">;
def pdbcompress : F<"pdbcompress">;
def emitpogophaseinfo : F<"emitpogophaseinfo">;

def delay : P_priv<"delay">;
def errorreport : P_priv<"errorreport">;
def idlout : P_priv<"idlout">;
def ilk : P_priv<"ilk">;
def ltcg_opt : P_priv<"ltcg">;
def assemblydebug_opt : P_priv<"assemblydebug">;
def ltcgout : P_priv<"ltcgout">;
def maxilksize : P_priv<"maxilksize">;
def tlbid : P_priv<"tlbid">;
def tlbout : P_priv<"tlbout">;
def verbose_all : P_priv<"verbose">;
def guardsym : P_priv<"guardsym">;
