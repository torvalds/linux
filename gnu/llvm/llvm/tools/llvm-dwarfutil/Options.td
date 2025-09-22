include "llvm/Option/OptParser.td"

multiclass BB<string name, string help1, string help2> {
  def NAME: Flag<["--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--"], "no-" # name>, HelpText<help2>;
}

def build_accelerator: Separate<["--", "-"], "build-accelerator">,
  MetaVarName<"[none,DWARF]">,
  HelpText<"Build accelerator tables(default: none)\n"
  "    =none - Do not build accelerators\n"
  "    =DWARF - .debug_names are generated for all DWARF versions\n"
  >;
def: Joined<["--", "-"], "build-accelerator=">, Alias<build_accelerator>;

def help : Flag<["--"], "help">,
  HelpText<"Prints this help output">;

def h : Flag<["-"], "h">,
  Alias<help>,
  HelpText<"Alias for --help">;

def linker: Separate<["--", "-"], "linker">,
  MetaVarName<"<DWARF linker type>">,
  HelpText<"Specify the desired type of DWARF linker. Defaults to 'classic'">;
def: Joined<["--", "-"], "linker=">, Alias<linker>;

defm odr_deduplication : BB<"odr-deduplication",
  "Do ODR deduplication for debug types(default)",
  "Don`t do ODR deduplication for debug types">;

def odr : Flag<["--"], "odr">,
  Alias<odr_deduplication>,
  HelpText<"Alias for --odr-deduplication">;

def no_odr : Flag<["--"], "no-odr">,
  Alias<no_odr_deduplication>,
  HelpText<"Alias for --no-odr-deduplication">;

defm garbage_collection : BB<"garbage-collection",
  "Do garbage collection for debug info(default)",
  "Don`t do garbage collection for debug info">;

defm separate_debug_file : BB<"separate-debug-file",
  "Create two output files: file w/o debug tables and file with debug tables",
  "Create single output file, containing debug tables(default)">;

def tombstone: Separate<["--", "-"], "tombstone">,
  MetaVarName<"[bfd,maxpc,exec,universal]">,
  HelpText<"Tombstone value used as a marker of invalid address(default: universal)\n"
  "    =bfd - Zero for all addresses and [1,1] for DWARF v4 (or less) address ranges and exec\n"
  "    =maxpc - Minus 1 for all addresses and minus 2 for DWARF v4 (or less) address ranges\n"
  "    =exec - Match with address ranges of executable sections\n"
  "    =universal - Both: bfd and maxpc"
  >;
def: Joined<["--", "-"], "tombstone=">, Alias<tombstone>;

def threads: Separate<["--", "-"], "num-threads">,
  MetaVarName<"<threads>">,
  HelpText<"Number of available threads for multi-threaded execution. "
  "Defaults to the number of cores on the current machine">;

def: Separate<["-"], "j">,
  Alias<threads>,
  HelpText<"Alias for --num-threads">;

def verbose : Flag<["--"], "verbose">,
  HelpText<"Enable verbose logging">;

def verify : Flag<["--"], "verify">,
  HelpText<"Run the DWARF verifier on the resulting debug info">;

def version : Flag<["--"], "version">,
  HelpText<"Print the version and exit">;

def V : Flag<["-"], "V">,
  Alias<version>,
  HelpText<"Alias for --version">;
