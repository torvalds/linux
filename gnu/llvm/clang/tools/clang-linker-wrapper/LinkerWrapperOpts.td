include "llvm/Option/OptParser.td"

def WrapperOnlyOption : OptionFlag;
def DeviceOnlyOption : OptionFlag;

def help : Flag<["--"], "help">,
  HelpText<"Display available options (--help-hidden for more)">;

def help_hidden : Flag<["--"], "help-hidden">,
  HelpText<"Display all available options">;

// Flags for the linker wrapper.
def linker_path_EQ : Joined<["--"], "linker-path=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<path>">,
  HelpText<"The linker executable to invoke">;
def cuda_path_EQ : Joined<["--"], "cuda-path=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<dir>">,
  HelpText<"Set the system CUDA path">;
def host_triple_EQ : Joined<["--"], "host-triple=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<triple>">,
  HelpText<"Triple to use for the host compilation">;
def opt_level : Joined<["--"], "opt-level=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<O0, O1, O2, or O3>">,
  HelpText<"Optimization level for LTO">;
def bitcode_library_EQ : Joined<["--"], "bitcode-library=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<kind>-<triple>-<arch>=<path>">,
  HelpText<"Extra bitcode library to link">;
def builtin_bitcode_EQ : Joined<["--"], "builtin-bitcode=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<triple>=<path>">,
  HelpText<"Perform a special internalizing link on the bitcode file. "
           "This is necessary for some vendor libraries to be linked correctly">;
def device_linker_args_EQ : Joined<["--"], "device-linker=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<value> or <triple>=<value>">,
  HelpText<"Arguments to pass to the device linker invocation">;
def clang_backend : Flag<["--"], "clang-backend">,
  Flags<[WrapperOnlyOption]>,
  HelpText<"Run the backend using clang rather than the LTO backend">;
def dry_run : Flag<["--"], "dry-run">,
  Flags<[WrapperOnlyOption]>,
  HelpText<"Print program arguments without running">;
def verbose : Flag<["--"], "wrapper-verbose">,
  Flags<[WrapperOnlyOption]>, HelpText<"Verbose output from tools">;
def embed_bitcode : Flag<["--"], "embed-bitcode">,
  Flags<[WrapperOnlyOption]>, HelpText<"Embed linked bitcode in the module">;
def debug : Flag<["--"], "device-debug">, Flags<[WrapperOnlyOption]>, 
  HelpText<"Use debugging">;
def ptxas_arg : Joined<["--"], "ptxas-arg=">,
  Flags<[WrapperOnlyOption]>,
  HelpText<"Argument to pass to the 'ptxas' invocation">;
def pass_remarks_EQ : Joined<["--"], "pass-remarks=">,
  Flags<[WrapperOnlyOption]>, HelpText<"Pass remarks for LTO">;
def pass_remarks_missed_EQ : Joined<["--"], "pass-remarks-missed=">,
  Flags<[WrapperOnlyOption]>, HelpText<"Pass remarks for LTO">;
def pass_remarks_analysis_EQ : Joined<["--"], "pass-remarks-analysis=">,
  Flags<[WrapperOnlyOption]>, HelpText<"Pass remarks for LTO">;
def print_wrapped_module : Flag<["--"], "print-wrapped-module">,
  Flags<[WrapperOnlyOption]>,
  HelpText<"Print the wrapped module's IR for testing">;
def save_temps : Flag<["--"], "save-temps">,
  Flags<[WrapperOnlyOption]>, HelpText<"Save intermediate results">;
def compress : Flag<["--"], "compress">,
  Flags<[WrapperOnlyOption]>, HelpText<"Compress bundled files">;
def compression_level_eq : Joined<["--"], "compression-level=">,
  Flags<[WrapperOnlyOption]>, HelpText<"Specify the compression level (integer)">;

def wrapper_time_trace_eq : Joined<["--"], "wrapper-time-trace=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<file>">,
  HelpText<"Enable time-trace and write the output to <file>">;
def wrapper_time_trace_granularity : Joined<["--"], "wrapper-time-trace-granularity=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<number>">,
  HelpText<"Set the granularity of time-trace updates">;

def wrapper_jobs : Joined<["--"], "wrapper-jobs=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<number>">,
  HelpText<"Sets the number of parallel jobs to use for device linking">;

def override_image : Joined<["--"], "override-image=">,
  Flags<[WrapperOnlyOption]>, MetaVarName<"<kind=file>">,
  HelpText<"Uses the provided file as if it were the output of the device link step">;

// Flags passed to the device linker.
def arch_EQ : Joined<["--"], "arch=">,
  Flags<[DeviceOnlyOption, HelpHidden]>, MetaVarName<"<arch>">,
  HelpText<"The device subarchitecture">;
def triple_EQ : Joined<["--"], "triple=">,
  Flags<[DeviceOnlyOption, HelpHidden]>, MetaVarName<"<triple>">,
  HelpText<"The device target triple">;
def whole_program : Flag<["--"], "whole-program">,
  Flags<[DeviceOnlyOption, HelpHidden]>,
  HelpText<"LTO has visibility of all input files">;
def linker_arg_EQ : Joined<["--"], "linker-arg=">,
  Flags<[DeviceOnlyOption, HelpHidden]>,
  HelpText<"An extra argument to be passed to the linker">;

// Arguments for the LLVM backend.
def mllvm : Separate<["-"], "mllvm">, Flags<[WrapperOnlyOption]>,
  MetaVarName<"<arg>">,
  HelpText<"Arguments passed to LLVM, including Clang invocations, for which "
           "the '-mllvm' prefix is preserved. Use '-mllvm --help' for a list "
           "of options.">;
def offload_opt_eq_minus : Joined<["--", "-"], "offload-opt=-">, Flags<[HelpHidden, WrapperOnlyOption]>,
  HelpText<"Options passed to LLVM, not including the Clang invocation. Use "
           "'--offload-opt=--help' for a list of options.">;

// Standard linker flags also used by the linker wrapper.
def sysroot_EQ : Joined<["--"], "sysroot=">, HelpText<"Set the system root">;

def o : JoinedOrSeparate<["-"], "o">, MetaVarName<"<path>">,
  HelpText<"Path to file to write output">;
def output_EQ : Joined<["--"], "output=">, Alias<o>, Flags<[HelpHidden]>,
  HelpText<"Alias for -o">;
def output : Separate<["--"], "output">, Alias<o>, Flags<[HelpHidden]>,
  HelpText<"Alias for -o">;

def library_path : JoinedOrSeparate<["-"], "L">, MetaVarName<"<dir>">,
  HelpText<"Add <dir> to the library search path">;
def library_path_S : Separate<["--", "-"], "library-path">, Flags<[HelpHidden]>,
  Alias<library_path>;
def library_path_EQ : Joined<["--", "-"], "library-path=">, Flags<[HelpHidden]>,
  Alias<library_path>;

def library : JoinedOrSeparate<["-"], "l">, MetaVarName<"<libname>">,
  HelpText<"Search for library <libname>">;
def library_S : Separate<["--", "-"], "library">, Flags<[HelpHidden]>,
  Alias<library_path>;
def library_EQ : Joined<["--", "-"], "library=">, Flags<[HelpHidden]>,
  Alias<library_path>;

def rpath : Separate<["--", "-"], "rpath">;
def rpath_EQ : Joined<["--", "-"], "rpath=">, Flags<[HelpHidden]>, Alias<rpath>;

def v : Flag<["--", "-"], "v">, HelpText<"Display the version number and exit">;
def version : Flag<["--", "-"], "version">, Flags<[HelpHidden]>, Alias<v>;

def whole_archive : Flag<["--", "-"], "whole-archive">, Flags<[HelpHidden]>;
def no_whole_archive : Flag<["--", "-"], "no-whole-archive">, Flags<[HelpHidden]>;

def relocatable : Flag<["--", "-"], "relocatable">, 
    HelpText<"Link device code to create a relocatable offloading application">;
def r : Flag<["-"], "r">, Alias<relocatable>;

// link.exe-style linker options.
def out : Joined<["/", "-", "/?", "-?"], "out:">, Flags<[HelpHidden]>;
def libpath : Joined<["/", "-", "/?", "-?"], "libpath:">, Flags<[HelpHidden]>;
def wholearchive_flag : Joined<["/", "-", "/?", "-?"], "wholearchive">, Flags<[HelpHidden]>;
