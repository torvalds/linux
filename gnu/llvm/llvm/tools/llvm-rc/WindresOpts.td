include "llvm/Option/OptParser.td"

multiclass Long<string name, string help> {
  def NAME: Separate<["--"], name>;
  def NAME # _eq: Joined<["--"], name # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
}

multiclass LongAlias<string name, Option orig> {
  def NAME: Separate<["--"], name>, Alias<orig>;
  def NAME # _eq: Joined<["--"], name # "=">, Alias<orig>;
}

multiclass LongShort<string short, string long, string help> {
  def NAME: Separate<["--"], long>;
  def NAME # _eq: Joined<["--"], long # "=">, Alias<!cast<Separate>(NAME)>,
    HelpText<help>;
  def NAME # _short: JoinedOrSeparate<["-"], short>, Alias<!cast<Separate>(NAME)>;
}

multiclass F<string short, string long, string help> {
  def NAME: Flag<["-"], short>;
  def NAME # _long: Flag<["--"], long>, Alias<!cast<Flag>(NAME)>,
    HelpText<help>;
}

defm input : LongShort<"i", "input", "Input file">;

defm output : LongShort<"o", "output", "Output file">;

defm input_format : LongShort<"J", "input-format", "Input format">;

defm output_format : LongShort<"O", "output-format", "Output format">;

defm preprocessor : Long<"preprocessor", "Custom preprocessor command">;
defm preprocessor_arg : Long<"preprocessor-arg", "Preprocessor command argument">;

defm target : LongShort<"F", "target", "Target BFD format name">;

defm include_dir : LongShort<"I", "include-dir", "Include directory">;
defm include_alias : LongAlias<"include", include_dir>;

defm define : LongShort<"D", "define", "Define to pass to the preprocessor">;

defm undef : LongShort<"U", "undefine", "Undefine to pass to the preprocessor">;

defm codepage : LongShort<"c", "codepage", "Default codepage to use">;

defm language : LongShort<"l", "language", "Default language to use (0x0-0xffff)">;

def use_temp_file: Flag<["--"], "use-temp-file">,
                   HelpText<"Mimic GNU windres preprocessor option handling "
                            "(don't unescape preprocessor options)">;

defm verbose : F<"v", "verbose", "Enable verbose output">;
defm version : F<"V", "version", "Display version">;

defm help : F<"h", "help", "Display this message and exit">;

// Print (but do not run) the commands to run for preprocessing
def _HASH_HASH_HASH : Flag<["-"], "###">;

def no_preprocess : Flag<["--"], "no-preprocess">;
