include "CommonOpts.td"

def output : JoinedOrSeparate<["-"], "o">, HelpText<"Write output to <file>">,
             MetaVarName<"<file>">;

def s : Flag<["-"], "s">,
        Alias<strip_all>,
        HelpText<"Alias for --strip-all">;
def no_strip_all : Flag<["--"], "no-strip-all">,
                   HelpText<"Disable --strip-all">;

def d : Flag<["-"], "d">,
        Alias<strip_debug>,
        HelpText<"Alias for --strip-debug">;
def S : Flag<["-"], "S">,
        Alias<strip_debug>,
        HelpText<"Alias for --strip-debug">;

def strip_swift_symbols : Flag<["-"], "T">,
                          HelpText<"Remove Swift symbols">;
