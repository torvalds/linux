include "llvm/Option/OptParser.td"

class F<string name, string help> : Flag<["-"], name>, HelpText<help>;
class FF<string name, string help>: Flag<["--"], name>, HelpText<help>;
class S<string name, string meta, string help>: Separate<["-"], name>, HelpText<help>, MetaVarName<meta>;

def help : FF<"help", "Display available options">;
def : F<"h", "Alias for --help">, Alias<help>;
def max_concurrency :
   S<"c", "<ulong>", "Maximum number of files to scan concurrently. "
                     "If 0, use the hardware concurrency.">;
def host_interface : S<"i", "<string>", "Host interface to bind to.">;
def min_interval :
    S<"m", "<number>", "Minimum number of seconds to wait before an on-demand update can be"
                       "triggered by a request for a buildid which is not in the collection.">;
def port : S<"p", "<uint>", "Port to listen on. Set to 0 to bind to any available port.">;
def scan_interval :
    S<"t", "<int>", "Number of seconds to wait between subsequent "
                    "automated scans of the filesystem.">;
def verbose_logging : F<"v", "Enable verbose logging.">;
