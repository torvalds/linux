include "llvm/Option/OptParser.td"

class F<string letter, string help> : Flag<["-"], letter>, HelpText<help>;
class FF<string name, string help> : Flag<["--"], name>, HelpText<help>;

multiclass Eq<string name, string help> {
  def NAME #_EQ : Joined<["--"], name #"=">, HelpText<help>;
  def : Separate<["--"], name>, Alias<!cast<Joined>(NAME #_EQ)>;
}

defm arch : Eq<"arch", "Specify the architecture, e.g. x86_64">;
defm bitwidth : Eq<"bitwidth", "Specify the bit width">;
defm endianness : Eq<"endianness", "Specify the endianness">;
defm exclude : Eq<"exclude", "Remove symbols which match the pattern. Can be specified multiple times">;
def help : FF<"help", "Display this help">;
def : F<"h", "Alias for --help">, Alias<help>;
defm hint_ifs_target : Eq<"hint-ifs-target", "When --output-format is 'IFS', this flag will hint the expected target triple for IFS output">;
defm input : Eq<"input", "input">;
defm input_format : Eq<"input-format", "Specify the input file format">;
defm output : Eq<"output", "Output file **DEPRECATED**">;
def : Separate<["-"], "o">, HelpText<"Alias for --output">, Alias<output_EQ>;
defm output_elf : Eq<"output-elf", "Output path for ELF file">;
defm output_format : Eq<"output-format", "Specify the output file format **DEPRECATED**">;
defm output_ifs : Eq<"output-ifs", "Output path for IFS file">;
defm output_tbd : Eq<"output-tbd", "Output path for TBD file">;
defm soname : Eq<"soname", "name">;
def strip_ifs_arch : FF<"strip-ifs-arch", "Strip target architecture information away from IFS output">;
def strip_ifs_bitwidth : FF<"strip-ifs-bitwidth", "Strip target bit width information away from IFS output">;
def strip_ifs_endianness : FF<"strip-ifs-endianness", "Strip target endianness information away from IFS output">;
def strip_ifs_target : FF<"strip-ifs-target", "Strip all target information away from IFS output">;
def strip_needed : FF<"strip-needed", "Strip needed libs from output">;
def strip_size : FF<"strip-size", "Remove object size from the output">;
def strip_undefined : FF<"strip-undefined", "Strip undefined symbols from IFS output">;
defm target : Eq<"target", "Specify the target triple, e.g. x86_64-linux-gnu">;
def version : FF<"version", "Display the version">;
def : F<"V", "Alias for --version">, Alias<version>;
def write_if_changed : FF<"write-if-changed", "Write the output file only if it is new or has changed">;
