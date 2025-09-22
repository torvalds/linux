include "llvm/Option/OptParser.td"

class F<string name, string help> : Flag<["-"], name>, HelpText<help>;
class Arg<string name, string help> : Separate<["-"], name>, HelpText<help>;

multiclass Eq<string name, string help> {
    def NAME #_EQ : Joined<["-", "--"], name #"=">, HelpText<help>;
    def : Separate<["-", "--"], name>, Alias<!cast<Joined>(NAME #_EQ)>;
}

def help : Flag<["--"], "help">, HelpText<"Display this help">;
def version : Flag<["--"], "version">, HelpText<"Display the version">;

def o : Arg<"o", "Destination of the primary output">;

defm mode : Eq<"mode", "The preprocessing mode used to compute the dependencies">;

defm format : Eq<"format", "The output format for the dependencies">;

defm module_files_dir : Eq<"module-files-dir",
    "The build directory for modules. Defaults to the value of '-fmodules-cache-path=' from command lines for implicit modules">;

def optimize_args_EQ : CommaJoined<["-", "--"], "optimize-args=">, HelpText<"Which command-line arguments of modules to optimize">;
def eager_load_pcm : F<"eager-load-pcm", "Load PCM files eagerly (instead of lazily on import)">;

def j : Arg<"j", "Number of worker threads to use (default: use all concurrent threads)">;

defm compilation_database : Eq<"compilation-database", "Compilation database">;
defm module_name : Eq<"module-name", "the module of which the dependencies are to be computed">;
defm dependency_target : Eq<"dependency-target", "The names of dependency targets for the dependency file">;

def deprecated_driver_command : F<"deprecated-driver-command", "use a single driver command to build the tu (deprecated)">;

defm resource_dir_recipe : Eq<"resource-dir-recipe", "How to produce missing '-resource-dir' argument">;

def print_timing : F<"print-timing", "Print timing information">;

def verbose : F<"v", "Use verbose output">;

def round_trip_args : F<"round-trip-args", "verify that command-line arguments are canonical by parsing and re-serializing">;

def DASH_DASH : Option<["--"], "", KIND_REMAINING_ARGS>;
