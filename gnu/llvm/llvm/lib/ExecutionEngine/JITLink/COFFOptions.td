include "llvm/Option/OptParser.td"

// link.exe accepts options starting with either a dash or a slash.

// Flag that takes no arguments.
class F<string name> : Flag<["/", "-", "/?", "-?"], name>;

// Flag that takes one argument after ":".
class P<string name> :
      Joined<["/", "-", "/?", "-?"], name#":">;

// Boolean flag which can be suffixed by ":no". Using it unsuffixed turns the
// flag on and using it suffixed by ":no" turns it off.
multiclass B_priv<string name> {
  def "" : F<name>;
  def _no : F<name#":no">;
}

def export  : P<"export">;
def alternatename : P<"alternatename">;
def incl : Joined<["/", "-", "/?", "-?"], "include:">;