include "llvm/Option/OptParser.td"

class F<string name, string help> : Flag<["-", "--"], name>, HelpText<help>;
class S<string name, string help> : Separate<["-", "--"], name>, HelpText<help>;

def help : F<"help", "Display this help">;
def : F<"h", "Alias for --help">, Alias<help>;
def version : F<"version", "Display the version of this program">;

def execFileNames : S<"e", "Specify the executable/library files to get the list of *.dwo from.">, MetaVarName<"<filename>">;
def outputFileName : S<"o", "Specify the output file.">, MetaVarName<"<filename>">;
def continueOnCuIndexOverflow : Flag<["-", "--"], "continue-on-cu-index-overflow">;
def continueOnCuIndexOverflow_EQ : Joined<["-", "--"], "continue-on-cu-index-overflow=">,
  HelpText<"default = continue, This turns an error when offset \n"
    "\t\tfor .debug_*.dwo sections overfolws into a warning. = soft-stop, This produces a \n"
    "\t\ttruncated but valid DWP file, discarding any DWO files that would not fit within \n"
    "\t\tthe 32 bit/4GB limits of the format.">,
  Values<"continue,soft-stop">;
