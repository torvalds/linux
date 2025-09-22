include "llvm/Option/OptParser.td"

class F<string name, string help> : Flag<["-", "--"], name>, HelpText<help>;

multiclass B<string name, string help1, string help2> {
  def NAME: Flag<["-", "--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["-", "--"], "no-" # name>, HelpText<help2>;
}

multiclass Eq<string name, string help> {
  def NAME #_EQ : Joined<["-", "--"], name #"=">,
                  HelpText<help>;
  def : Separate<["-", "--"], name>, Alias<!cast<Joined>(NAME #_EQ)>;
}

def generic_grp : OptionGroup<"Genric Options">, HelpText<"Generic Options">;
def help : F<"help", "Display this help">, Group<generic_grp>;
def : Flag<["-"], "h">, Alias<help>, HelpText<"Alias for --help">, Group<generic_grp>;
def version : F<"version", "Display the version">, Group<generic_grp>;
def : Flag<["-"], "v">, Alias<version>, HelpText<"Alias for --version">, Group<generic_grp>;

def action_grp : OptionGroup<"Action">, HelpText<"Action (required)">;
def print : F<"print", "Print coverage addresses">,
  Group<action_grp>;
def printCoveragePcs : F<"print-coverage-pcs", "Print coverage instrumentation points addresses.">,
  Group<action_grp>;
def coveredFunctions : F<"covered-functions", "Print all covered funcions.">,
  Group<action_grp>;
def notCoveredFunctions : F<"not-covered-functions", "Print all not covered funcions.">,
  Group<action_grp>;
def printCoverageStats : F<"print-coverage-stats", "Print coverage statistics.">,
  Group<action_grp>;
def htmlReport : F<"html-report", "REMOVED. Use -symbolize & coverage-report-server.py.">,
  Group<action_grp>;
def symbolize : F<"symbolize", "Produces a symbolized JSON report from binary report.">,
  Group<action_grp>;
def merge : F<"merge", "Merges reports.">,
  Group<action_grp>;

defm demangle : B<"demangle", "Demangle function names", "Do not demangle function names">;
defm skipDeadFiles : B<"skip-dead-files", "Do not list dead source files in reports",
                                          "List dead source files in reports">;
defm useDefaultIgnoreList :
  B<"use_default_ignorelist", "Use the default ignore list", "Don't use the default ignore list">,
  Flags<[HelpHidden]>;

// Compatibility aliases
def : Flag<["-"], "demangle=0">, Alias<no_demangle>, HelpText<"Alias for --no-demangle">;
def : Flag<["-"], "skip-dead-files=0">, Alias<no_skipDeadFiles>, HelpText<"Alias for --no-skip-dead-files">;
def : Flag<["-"], "use_default_ignorelist=0">, Alias<no_useDefaultIgnoreList>, HelpText<"Alias for --no-use_default_ignore_list">;

defm stripPathPrefix
    : Eq<"strip_path_prefix", "Strip this prefix from files paths in reports">,
      MetaVarName<"<string>">;

defm ignorelist
    : Eq<"ignorelist", "Ignorelist file (sanitizer ignorelist format)">,
      MetaVarName<"<string>">;
