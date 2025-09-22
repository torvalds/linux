include "llvm/Option/OptParser.td"

class F<string name, string help> : Flag<["--"], name>, HelpText<help>;
multiclass Eq<string name, string metavar, string help> {
  def NAME #_EQ : Joined<["--"], name #"=">,
                  HelpText<help>, MetaVarName<metavar>;
  def : Separate<["--"], name>, Alias<!cast<Joined>(NAME #_EQ)>;
}

def help : F<"help", "Display available options">;
def : Flag<["-"], "h">, HelpText<"Alias for --help">, Alias<help>;
def dump_tli : F<"dump-tli", "Dump TLI's list of functions and whether they are available">;
defm triple : Eq<"triple", "<triple>", "Target triple">;
defm libdir : Eq<"libdir", "<directory>", "Root directory for finding library files">;
def separate : F<"separate", "Report on each library file separately">;
def report_EQ : Joined<["--"], "report=">, HelpText<"Level of detail to report">, Values<"summary,discrepancy,full">;
