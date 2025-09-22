include "llvm/Option/OptParser.td"

class F<string letter, string help> : Flag<["-"], letter>, HelpText<help>;
class FF<string name, string help> : Flag<["--"], name>, HelpText<help>;

multiclass Eq<string name, string help> {
  def NAME #_EQ : Joined<["--"], name #"=">,
                  HelpText<help>;
  def : Separate<["--"], name>, Alias<!cast<Joined>(NAME #_EQ)>;
}

def common : FF<"common", "Print common symbols in the ELF file. When using Berkeley format, this is added to bss">;
defm format : Eq<"format", "Specify output format">;
def help : FF<"help", "Display this help">;
defm radix : Eq<"radix", "Print size in radix">;
def totals : FF<"totals", "Print totals of all objects - Berkeley format only">;
def version : FF<"version", "Display the version">;

// Mach-O specific options.
def grp_mach_o : OptionGroup<"kind">, HelpText<"OPTIONS (Mach-O specific)">;
def arch_EQ : Joined<["--"], "arch=">, HelpText<"architecture(s) from a Mach-O file to dump">, Group<grp_mach_o>;
def : Separate<["--", "-"], "arch">, Alias<arch_EQ>;
def l : F<"l", "When format is darwin, use long format to include addresses and offsets">, Group<grp_mach_o>;

def : F<"A", "Alias for --format">, Alias<format_EQ>, AliasArgs<["sysv"]>;
def : F<"B", "Alias for --format">, Alias<format_EQ>, AliasArgs<["berkeley"]>;
def : F<"d", "Alias for --radix=10">, Alias<radix_EQ>, AliasArgs<["10"]>;
def : F<"h", "Alias for --help">, Alias<help>;
def : F<"m", "Alias for --format">, Alias<format_EQ>, AliasArgs<["darwin"]>;
def : F<"o", "Alias for --radix=8">, Alias<radix_EQ>, AliasArgs<["8"]>;
def : F<"t", "Alias for --totals">, Alias<totals>;
def : F<"x", "Alias for --radix=16">, Alias<radix_EQ>, AliasArgs<["16"]>;
