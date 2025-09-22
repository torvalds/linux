include "llvm/Option/OptParser.td"

def help : Flag<["--"], "help">, HelpText<"print help">;
def help_hidden : Flag<["--"], "help-hidden">,
  HelpText<"print help for hidden flags">;

def arch : Separate<["-"], "arch">,
  HelpText<"select slice of universal Mach-O file">;
def C : Flag<["-"], "C">, HelpText<"print linker optimization hints">;
def d : Flag<["-"], "d">, HelpText<"print data section">;
def D : Flag<["-"], "D">, HelpText<"print shared library id">;
def f : Flag<["-"], "f">, HelpText<"print universal headers">;
def G : Flag<["-"], "G">, HelpText<"print data-in-code table">;
def h : Flag<["-"], "h">, HelpText<"print mach header">;
def I : Flag<["-"], "I">, HelpText<"print indirect symbol table">;
def j : Flag<["-"], "j">, HelpText<"print opcode bytes">;
def l : Flag<["-"], "l">, HelpText<"print load commnads">;
def L : Flag<["-"], "L">, HelpText<"print used shared libraries">;
def mcpu_EQ : Joined<["-"], "mcpu=">, HelpText<"select cpu for disassembly">;
def o : Flag<["-"], "o">, HelpText<"print Objective-C segment">;
def p : Separate<["-"], "p">,
  MetaVarName<"<function name>">,
  HelpText<"start disassembly at <function name>">;
def P : Flag<["-"], "P">, HelpText<"print __TEXT,__info_plist section as strings">;
def : Flag<["-"], "q">, Flags<[HelpHidden]>,
  HelpText<"use LLVM's disassembler (default)">;
def r : Flag<["-"], "r">, HelpText<"print relocation entries">;
def s : MultiArg<["-"], "s", 2>,
  MetaVarName<"<segname> <sectname>">,
  HelpText<"print contents of section">;
def t : Flag<["-"], "t">, HelpText<"print text section">;
def version : Flag<["--"], "version">, HelpText<"print version">;
def v : Flag<["-"], "v">,
  HelpText<"verbose output / disassemble when printing text sections">;
def V : Flag<["-"], "V">,
  HelpText<"symbolize disassembled operands (implies -v)">;
def x : Flag<["-"], "x">, HelpText<"print all text sections">;
def X : Flag<["-"], "X">, HelpText<"omit leading addresses or headers">;

def chained_fixups : Flag<["-"], "chained_fixups">,
  HelpText<"print chained fixup information">;
def dyld_info : Flag<["-"], "dyld_info">,
  HelpText<"print bind and rebase information">;

// Not (yet?) implemented:
// def a : Flag<["-"], "a">, HelpText<"print archive header">;
// -c print argument strings of a core file
// -m don't use archive(member) syntax
// -dyld_opcodes
// -addr_slide=arg
// -function_offsets

// Obsolete and unsupported:
def grp_obsolete : OptionGroup<"kind">,
  HelpText<"Obsolete and unsupported flags">;

def : Flag<["-"], "B">, Flags<[HelpHidden]>, Group<grp_obsolete>,
  HelpText<"force Thum disassembly (ARM 32-bit objects only)">;
def : Flag<["-"], "H">, Flags<[HelpHidden]>, Group<grp_obsolete>,
  HelpText<"print two-level hints table">;
def : Flag<["-"], "M">, Flags<[HelpHidden]>, Group<grp_obsolete>,
  HelpText<"print module table of shared library">;
def : Flag<["-"], "R">, Flags<[HelpHidden]>, Group<grp_obsolete>,
  HelpText<"print reference table of shared library">;
def : Flag<["-"], "S">, Flags<[HelpHidden]>, Group<grp_obsolete>,
  HelpText<"print table of contents of library">;
def : Flag<["-"], "T">, Flags<[HelpHidden]>, Group<grp_obsolete>,
  HelpText<"print table of contents of shared library">;
def : Flag<["-"], "Q">, Flags<[HelpHidden]>, Group<grp_obsolete>,
  HelpText<"llvm-otool cannot use otool-classic's disassembler">;
