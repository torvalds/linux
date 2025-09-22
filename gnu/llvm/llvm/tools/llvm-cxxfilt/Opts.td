include "llvm/Option/OptParser.td"

class F<string letter, string help> : Flag<["-"], letter>, HelpText<help>;
class FF<string name, string help> : Flag<["--"], name>, HelpText<help>;

multiclass BB<string name, string help1, string help2> {
  def NAME: Flag<["--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--"], "no-" # name>, HelpText<help2>;
}

multiclass Eq<string name, string help> {
  def NAME #_EQ : Joined<["--"], name #"=">,
                  HelpText<help>;
  def : Separate<["--"], name>, Alias<!cast<Joined>(NAME #_EQ)>;
}

def help : FF<"help", "Display this help">;
defm strip_underscore : BB<"strip-underscore", "Strip the leading underscore", "Don't strip the leading underscore">;
def types : FF<"types", "Attempt to demangle types as well as function names">;
def no_params : FF<"no-params", "Skip function parameters and return types">;
def version : FF<"version", "Display the version">;

defm : Eq<"format", "Specify mangling format. Currently ignored because only 'gnu' is supported">;
def : F<"s", "Alias for --format">;

def : F<"_", "Alias for --strip-underscore">, Alias<strip_underscore>;
def : F<"h", "Alias for --help">, Alias<help>;
def : F<"n", "Alias for --no-strip-underscore">, Alias<no_strip_underscore>;
def : F<"p", "Alias for --no-params">, Alias<no_params>;
def : F<"t", "Alias for --types">, Alias<types>;
