include "llvm/Option/OptParser.td"

// All the switches can be preceded by either '/' or '-'.

def DEFINE : Joined<["/", "-"], "DEFINE:">, HelpText<"Not implemented">, MetaVarName<"symbol">;
def FOLDDUPS : Flag<["/", "-"], "FOLDDUPS:">, HelpText<"Not implemented">;
def MACHINE : Joined<["/", "-"], "MACHINE:">, HelpText<"Machine architecture">, MetaVarName<"{ARM|ARM64|EBC|IA64|X64|X86}">;
def NOLOGO : Flag<["/", "-"], "NOLOGO">, HelpText<"Not implemented">;
def OUT : Joined<["/", "-"], "OUT:">, HelpText<"Output file">, MetaVarName<"filename">;
def READONLY : Flag<["/", "-"], "READONLY">, HelpText<"Not implemented">;
def VERBOSE : Flag<["/", "-"], "VERBOSE">, HelpText<"Use verbose output">;
def HELP : Flag<["/", "-"], "HELP">, HelpText<"Display available options">;
def H : Flag<["/", "-"], "H">, Alias<HELP>;
def HELP_Q : Flag<["/?", "-?"], "">, Alias<HELP>;

// Extensions.

def TIMESTAMP : Joined<["/", "-"], "TIMESTAMP:">,
    HelpText<"Timestamp for coff header, defaults to current time">;
