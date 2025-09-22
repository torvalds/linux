include "llvm/Option/OptParser.td"

class F<string letter, string help> : Flag<["-"], letter>, HelpText<help>;
class FF<string name, string help> : Flag<["--"], name>, HelpText<help>;

multiclass Eq<string name, string help> {
  def NAME #_EQ : Joined<["--"], name #"=">,
                  HelpText<help>;
  def : Separate<["--"], name>, Alias<!cast<Joined>(NAME #_EQ)>;
}

def all : FF<"all", "Silently ignored. Present for GNU strings compatibility">;
defm bytes : Eq<"bytes", "Print sequences of the specified length">;
def help : FF<"help", "Display this help">;
def print_file_name : Flag<["--"], "print-file-name">, HelpText<"Print the name of the file before each string">;
defm radix : Eq<"radix", "Print the offset within the file with the specified radix: o (octal), d (decimal), x (hexadecimal)">, MetaVarName<"<radix>">;
def version : FF<"version", "Display the version">;

def : F<"a", "Alias for --all">, Alias<all>;
def : F<"f", "Alias for --print-file-name">, Alias<print_file_name>;
def : F<"h", "Alias for --help">, Alias<help>;
def : JoinedOrSeparate<["-"], "n">, Alias<bytes_EQ>, HelpText<"Alias for --bytes">;
def : JoinedOrSeparate<["-"], "t">, Alias<radix_EQ>, HelpText<"Alias for --radix">, MetaVarName<"<radix>">;
