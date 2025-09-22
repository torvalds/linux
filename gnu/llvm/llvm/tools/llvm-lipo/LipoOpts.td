include "llvm/Option/OptParser.td"

def help : Flag<["-", "--"], "help">;
def h : Flag<["-"], "h">, Alias<help>;

def version : Flag<["-", "--"], "version">,
              HelpText<"Print the version and exit.">;

def segalign
    : MultiArg<["-", "--"], "segalign", 2>,
      HelpText<"Specifies the segment alignment for the specified "
               "architecture when creating a universal binary file. The "
               "alignment is a hexadecimal number that is a power of 2.">;

def arch
    : MultiArg<["-", "--"], "arch", 2>,
      HelpText<"Specifies the architecture and the corresponding input file">;

def action_group : OptionGroup<"action group">;

def verify_arch
    : Option<["-", "--"], "verify_arch", KIND_REMAINING_ARGS>,
      Group<action_group>,
      HelpText<
          "Verify that the specified arch_types are present in the input file">;

def archs : Option<["-", "--"], "archs", KIND_FLAG>,
            Group<action_group>,
            HelpText<"Display the arch_types present in the input file">;

def info : Option<["-", "--"], "info", KIND_FLAG>,
           Group<action_group>,
           HelpText<"Display descriptions of each input file including "
                    "filename and arch_types. Groups universal binaries "
                    "together followed by thin files">;

def thin : Option<["-", "--"], "thin", KIND_SEPARATE>,
           Group<action_group>,
           HelpText<"Create a thin output file of specified arch_type from the "
                    "fat input file. Requires -output option">;

def extract : Option<["-", "--"], "extract", KIND_SEPARATE>,
           Group<action_group>,
           HelpText<"Create a universal output file containing only the specified "
                    "arch_type from the fat input file. Requires -output option">;

def create : Option<["-", "--"], "create", KIND_FLAG>,
             Group<action_group>,
             HelpText<"Create a universal binary output file from the input "
                      "files. Requires -output option">;

def replace
    : MultiArg<["-", "--"], "replace", 2>,
      Group<action_group>,
      HelpText<"Replace the specified arch type with the contents of the "
               "input_file in a universal binary. Requires -output option">;

def output : Option<["-", "--"], "output", KIND_SEPARATE>,
             HelpText<"Create output file with specified name">;
def o : JoinedOrSeparate<["-"], "o">, Alias<output>;

def fat64 : Option<["-", "--"], "fat64", KIND_FLAG>,
            HelpText<"Use 64 bits Universal Mach-O format">;
