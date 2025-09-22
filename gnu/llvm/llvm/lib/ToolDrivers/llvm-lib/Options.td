include "llvm/Option/OptParser.td"

// lib.exe accepts options starting with either a dash or a slash.

// Flag that takes no arguments.
class F<string name> : Flag<["/", "-", "/?", "-?"], name>;

// Flag that takes one argument after ":".
class P<string name, string help> :
      Joined<["/", "-", "/?", "-?"], name#":">, HelpText<help>;

// Boolean flag which can be suffixed by ":no". Using it unsuffixed turns the
// flag on and using it suffixed by ":no" turns it off.
multiclass B<string name, string help_on, string help_off> {
  def "" : F<name>, HelpText<help_on>;
  def _no : F<name#":no">, HelpText<help_off>;
}

def ignore : P<"ignore", "Specify warning codes to ignore">;
def libpath: P<"libpath", "Object file search path">;

// Can't be called "list" since that's a keyword.
def lst    : F<"list">, HelpText<"List contents of .lib file on stdout">;
def out    : P<"out", "Path to file to write output">;
def deffile : P<"def", "def file to use to generate import library">;
def nativedeffile : P<"defArm64Native", "def file to use to generate native ARM64 symbols in ARM64EC import library">;

def llvmlibthin : F<"llvmlibthin">,
    HelpText<"Make .lib point to .obj files instead of copying their contents">;

def llvmlibempty : F<"llvmlibempty">,
    HelpText<"When given no contents, produce an empty .lib file">;

def machine: P<"machine", "Specify target platform">;

defm WX : B<"WX", "Treat warnings as errors",
            "Don't treat warnings as errors (default)">;

def help : F<"help">;

// /?? and -?? must be before /? and -? to not confuse lib/Options.
def help_q : Flag<["/??", "-??", "/?", "-?"], "">, Alias<help>;

//==============================================================================
// The flags below do nothing. They are defined only for lib.exe compatibility.
//==============================================================================

def ltcg : F<"ltcg">;
def nodefaultlib: P<"nodefaultlib", "">;
def nodefaultlib_all: F<"nodefaultlib">;
def nologo : F<"nologo">;
def subsystem : P<"subsystem", "">;
def verbose : F<"verbose">;
