include "llvm/Option/OptParser.td"

// All the switches can be preceded by either '/' or '-'.
// These options seem to be important for the tool
// and should be implemented.

class S<string name, string help> :
      Separate<["/", "-"], name>, HelpText<help>;

class JS<string name, string help> :
      JoinedOrSeparate<["/", "-"], name>, HelpText<help>;

class F<string name, string help> : Flag<["/", "-"], name>, HelpText<help>;

class F_nodoc<string name> : Flag<["/", "-"], name>;
class S_nodoc<string name> : Separate<["/", "-"], name>;

def fileout : JS<"FO", "Change the output file location.">;

def define : JS<"D", "Define a symbol for the C preprocessor.">;
def undef : JS<"U", "Undefine a symbol for the C preprocessor.">;

def lang_id : JS<"L", "Set the default language identifier.">;
def lang_name : S<"LN", "Set the default language name.">;

def includepath : JS<"I", "Add an include path.">;
def noinclude : F<"X", "Ignore 'include' variable.">;

def add_null : F<"N", "Null-terminate all strings in the string table.">;

def dupid_nowarn : F<"Y", "Suppress warnings on duplicate resource IDs.">;

def verbose : F<"V", "Be verbose.">;
def help : F<"?", "Display this help and exit.">;
def h : F<"H", "Display this help and exit.">, Alias<help>;

def codepage : JS<"C", "Set the codepage used for input strings.">;

// llvm-rc specific options:

def dry_run : F<"dry-run", "Don't compile the input; only try to parse it.">;

def no_preprocess : F<"no-preprocess", "Don't try to preprocess the input file.">;

// Print (but do not run) the commands to run for preprocessing
def _HASH_HASH_HASH : F_nodoc<"###">;

// Unused switches (at least for now). These will stay unimplemented
// in an early stage of development and can be ignored. However, we need to
// parse them in order to preserve the compatibility with the original tool.

def nologo : F_nodoc<"NOLOGO">;
def r : F_nodoc<"R">;
def sl : F_nodoc<"SL">;

// (Codepages support.)
def w : F_nodoc<"W">;

// (Support of MUI and similar.)
def fm : S_nodoc<"FM">;
def q : S_nodoc<"Q">;
def g : F_nodoc<"G">;
def gn : F_nodoc<"GN">;
def g1 : F_nodoc<"G1">;
def g2 : F_nodoc<"G2">;
