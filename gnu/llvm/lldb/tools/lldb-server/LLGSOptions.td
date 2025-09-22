include "llvm/Option/OptParser.td"

class F<string name>: Flag<["--", "-"], name>;
class R<list<string> prefixes, string name>
  : Option<prefixes, name, KIND_REMAINING_ARGS>;

multiclass SJ<string name, string help> {
  def NAME: Separate<["--", "-"], name>,
    HelpText<help>;
  def NAME # _eq: Joined<["--", "-"], name # "=">,
    Alias<!cast<Separate>(NAME)>;
}

def grp_connect : OptionGroup<"connection">, HelpText<"CONNECTION">;

defm fd: SJ<"fd", "Communicate over the given file descriptor.">,
  MetaVarName<"<fd>">,
  Group<grp_connect>;

defm named_pipe: SJ<"named-pipe", "Write port lldb-server will listen on to the given named pipe.">,
  MetaVarName<"<name>">,
  Group<grp_connect>;

defm pipe: SJ<"pipe", "Write port lldb-server will listen on to the given file descriptor.">,
  MetaVarName<"<fd>">,
  Group<grp_connect>;

def reverse_connect: F<"reverse-connect">,
  HelpText<"Connect to the client instead of passively waiting for a connection. In this case [host]:port denotes the remote address to connect to.">,
  Group<grp_connect>;

def grp_general : OptionGroup<"general options">, HelpText<"GENERAL OPTIONS">;

defm log_channels: SJ<"log-channels", "Channels to log. A colon-separated list of entries. Each entry starts with a channel followed by a space-separated list of categories.">,
  MetaVarName<"<channel1 categories...:channel2 categories...>">,
  Group<grp_general>;

defm log_file: SJ<"log-file", "Destination file to log to. If empty, log to stderr.">,
  MetaVarName<"<file>">,
  Group<grp_general>;

def setsid: F<"setsid">, HelpText<"Run lldb-server in a new session.">,
  Group<grp_general>;
def: Flag<["-"], "S">, Alias<setsid>,
  Group<grp_general>;

def help: F<"help">, HelpText<"Prints out the usage information for lldb-server.">,
  Group<grp_general>;
def: Flag<["-"], "h">, Alias<help>,
  Group<grp_general>;

def grp_target : OptionGroup<"target selection">, HelpText<"TARGET SELECTION">;

defm attach: SJ<"attach", "Attach to the process given by a (numeric) process id or a name.">,
  MetaVarName<"<pid-or-name>">,
  Group<grp_target>;

def REM : R<["--"], "">, HelpText<"Launch program for debugging.">,
  MetaVarName<"program args">,
  Group<grp_target>;

def: F<"native-regs">; // Noop. Present for backwards compatibility only.
