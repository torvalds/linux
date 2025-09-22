include "llvm/Option/OptParser.td"

multiclass B<string name, string help1, string help2> {
  def NAME: Flag<["--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--"], "no-" # name>, HelpText<help2>;
}

multiclass Eq<string name, string help> {
  def NAME : Separate<["--"], name>;
  def NAME #_eq : Joined<["--"], name #"=">,
                  Alias<!cast<Separate>(NAME)>,
                  HelpText<help>;
}

def help : Flag<["--"], "help">,
  HelpText<"Display available options (--help-hidden for more)">;

def help_hidden : Flag<["--"], "help-hidden">,
  Flags<[HelpHidden]>,
  HelpText<"Display all available options">;

def version : Flag<["--"], "version">,
  HelpText<"Display the version of this program">;
def : Flag<["-"], "v">, Alias<version>, HelpText<"Alias for --version">;

def adjust_vma_EQ : Joined<["--"], "adjust-vma=">,
  MetaVarName<"offset">,
  HelpText<"Increase the displayed address by the specified offset">;

def all_headers : Flag<["--"], "all-headers">,
  HelpText<"Display all available header information, "
           "relocation entries and the symbol table">;
def : Flag<["-"], "x">, Alias<all_headers>, HelpText<"Alias for --all-headers">;

def arch_name_EQ : Joined<["--"], "arch-name=">,
  HelpText<"Target arch to disassemble for, "
            "see --version for available targets">;
def archive_headers : Flag<["--"], "archive-headers">,
  HelpText<"Display archive header information">;

defm build_id :
  Eq<"build-id", "Build ID to look up. Once found, added as an input file">,
  MetaVarName<"<hex>">;

def : Flag<["-"], "a">, Alias<archive_headers>,
  HelpText<"Alias for --archive-headers">;

def demangle : Flag<["--"], "demangle">, HelpText<"Demangle symbol names">;
def : Flag<["-"], "C">, Alias<demangle>, HelpText<"Alias for --demangle">;

defm debug_file_directory :
  Eq<"debug-file-directory", "Path to directory where to look for debug files">,
  MetaVarName<"<dir>">;

defm debuginfod : B<"debuginfod", "Use debuginfod to find debug files", "Don't use debuginfod to find debug files">;

def disassemble : Flag<["--"], "disassemble">,
  HelpText<"Disassemble all executable sections found in the input files">;
def : Flag<["-"], "d">, Alias<disassemble>, HelpText<"Alias for --disassemble">;

def disassemble_all : Flag<["--"], "disassemble-all">,
  HelpText<"Disassemble all sections found in the input files">;
def : Flag<["-"], "D">, Alias<disassemble_all>,
  HelpText<"Alias for --disassemble-all">;

def symbol_description : Flag<["--"], "symbol-description">,
  HelpText<"Add symbol description for disassembly. This "
           "option is for XCOFF files only">;

def traceback_table : Flag<["--"], "traceback-table">,
  HelpText<"Decode traceback table in disassembly. Implies --disassemble. "
           "This option is for XCOFF files only">;

def disassemble_symbols_EQ : Joined<["--"], "disassemble-symbols=">,
  HelpText<"List of symbols to disassemble. "
           "Accept demangled names when --demangle is "
           "specified, otherwise accept mangled names">;

def disassemble_zeroes : Flag<["--"], "disassemble-zeroes">,
  HelpText<"Do not skip blocks of zeroes when disassembling">;
def : Flag<["-"], "z">, Alias<disassemble_zeroes>,
  HelpText<"Alias for --disassemble-zeroes">;

def disassembler_options_EQ : Joined<["--"], "disassembler-options=">,
  MetaVarName<"options">,
  HelpText<"Pass target specific disassembler options">;
def : JoinedOrSeparate<["-"], "M">, Alias<disassembler_options_EQ>,
  HelpText<"Alias for --disassembler-options=">;

def disassembler_color_EQ : Joined<["--"], "disassembler-color=">,
  MetaVarName<"mode">,
  HelpText<"Enable or disable disassembler color output. "
           "Valid options are \"on\", \"off\" and \"terminal\" (default)">;

def dynamic_reloc : Flag<["--"], "dynamic-reloc">,
  HelpText<"Display the dynamic relocation entries in the file">;
def : Flag<["-"], "R">, Alias<dynamic_reloc>,
  HelpText<"Alias for --dynamic-reloc">;

def dwarf_EQ : Joined<["--"], "dwarf=">,
  HelpText<"Dump the specified DWARF debug sections. The "
           "only supported value is 'frames'">,
  Values<"frames">;

def fault_map_section : Flag<["--"], "fault-map-section">,
  HelpText<"Display the content of the fault map section">;

def offloading : Flag<["--"], "offloading">,
  HelpText<"Display the content of the offloading section">;

def file_headers : Flag<["--"], "file-headers">,
  HelpText<"Display the contents of the overall file header">;
def : Flag<["-"], "f">, Alias<file_headers>,
  HelpText<"Alias for --file-headers">;

def full_contents : Flag<["--"], "full-contents">,
  HelpText<"Display the content of each section">;
def : Flag<["-"], "s">, Alias<full_contents>,
  HelpText<"Alias for --full-contents">;

def line_numbers : Flag<["--"], "line-numbers">,
  HelpText<"When disassembling, display source line numbers. "
           "Implies --disassemble">;
def : Flag<["-"], "l">,
  Alias<line_numbers>,
  HelpText<"Alias for --line-numbers">;

def macho : Flag<["--"], "macho">,
  HelpText<"Use MachO specific object file parser">;
def : Flag<["-"], "m">, Alias<macho>, HelpText<"Alias for --macho">;

def mcpu_EQ : Joined<["--"], "mcpu=">,
  MetaVarName<"cpu-name">,
  HelpText<"Target a specific cpu type (--mcpu=help for details)">;

def mattr_EQ : Joined<["--"], "mattr=">,
  MetaVarName<"a1,+a2,-a3,...">,
  HelpText<"Target specific attributes (--mattr=help for details)">;

def mllvm : Separate<["-"], "mllvm">, HelpText<"Specify an argument to forward to LLVM's CommandLine library">, MetaVarName<"<arg>">;
def : Joined<["-"], "mllvm=">, Alias<mllvm>;

def no_show_raw_insn : Flag<["--"], "no-show-raw-insn">,
  HelpText<"When disassembling instructions, "
           "do not print the instruction bytes.">;

def no_leading_addr : Flag<["--"], "no-leading-addr">,
  HelpText<"When disassembling, do not print leading addresses for instructions or inline relocations">;
def : Flag<["--"], "no-addresses">, Alias<no_leading_addr>,
  HelpText<"Alias for --no-leading-addr">;

def raw_clang_ast : Flag<["--"], "raw-clang-ast">,
  HelpText<"Dump the raw binary contents of the clang AST section">;

def reloc : Flag<["--"], "reloc">,
  HelpText<"Display the relocation entries in the file">;
def : Flag<["-"], "r">, Alias<reloc>, HelpText<"Alias for --reloc">;

def print_imm_hex : Flag<["--"], "print-imm-hex">,
  HelpText<"Use hex format for immediate values (default)">;

def no_print_imm_hex : Flag<["--"], "no-print-imm-hex">,
  HelpText<"Do not use hex format for immediate values">;
def : Flag<["--"], "print-imm-hex=false">, Alias<no_print_imm_hex>;

def private_headers : Flag<["--"], "private-headers">,
  HelpText<"Display format specific file headers">;
def : Flag<["-"], "p">, Alias<private_headers>,
  HelpText<"Alias for --private-headers">;

def section_EQ : Joined<["--"], "section=">,
  HelpText<"Operate on the specified sections only. "
           "With --macho dump segment,section">;
def : Separate<["--"], "section">, Alias<section_EQ>;
def : JoinedOrSeparate<["-"], "j">, Alias<section_EQ>,
  HelpText<"Alias for --section">;

def section_headers : Flag<["--"], "section-headers">,
  HelpText<"Display summaries of the headers for each section.">;
def : Flag<["--"], "headers">, Alias<section_headers>,
  HelpText<"Alias for --section-headers">;
def : Flag<["-"], "h">, Alias<section_headers>,
  HelpText<"Alias for --section-headers">;

def show_all_symbols : Flag<["--"], "show-all-symbols">,
  HelpText<"Show all symbols during disassembly, even if multiple "
           "symbols are defined at the same location">;

def show_lma : Flag<["--"], "show-lma">,
  HelpText<"Display LMA column when dumping ELF section headers">;

def source : Flag<["--"], "source">,
  HelpText<"When disassembling, display source interleaved with the "
           "disassembly. Implies --disassemble">;
def : Flag<["-"], "S">, Alias<source>, HelpText<"Alias for --source">;

def start_address_EQ : Joined<["--"], "start-address=">,
  MetaVarName<"address">,
  HelpText<"Set the start address for disassembling, "
           "printing relocations and printing symbols">;
def stop_address_EQ : Joined<["--"], "stop-address=">,
  MetaVarName<"address">,
  HelpText<"Set the stop address for disassembling, "
           "printing relocations and printing symbols">;

def syms : Flag<["--"], "syms">,
  HelpText<"Display the symbol table">;
def : Flag<["-"], "t">, Alias<syms>, HelpText<"Alias for --syms">;

def symbolize_operands : Flag<["--"], "symbolize-operands">,
  HelpText<"Symbolize instruction operands when disassembling">;

def pretty_pgo_analysis_map : Flag<["--"], "pretty-pgo-analysis-map">,
                              HelpText<"Display PGO analysis values with "
                                       "formatting rather than raw numbers">;

def dynamic_syms : Flag<["--"], "dynamic-syms">,
  HelpText<"Display the contents of the dynamic symbol table">;
def : Flag<["-"], "T">, Alias<dynamic_syms>,
  HelpText<"Alias for --dynamic-syms">;

def triple_EQ : Joined<["--"], "triple=">,
  HelpText<"Target triple to disassemble for, "
            "see --version for available targets">;
def : Separate<["--"], "triple">,
  Alias<triple_EQ>;

def unwind_info : Flag<["--"], "unwind-info">,
  HelpText<"Display unwind information">;
def : Flag<["-"], "u">, Alias<unwind_info>,
  HelpText<"Alias for --unwind-info">;

def wide : Flag<["--"], "wide">,
  HelpText<"Ignored for compatibility with GNU objdump">;
def : Flag<["-"], "w">, Alias<wide>;

defm prefix : Eq<"prefix", "Add prefix to absolute paths">,
              MetaVarName<"prefix">;
defm prefix_strip
    : Eq<"prefix-strip", "Strip out initial directories from absolute "
                         "paths. No effect without --prefix">,
      MetaVarName<"prefix">;

def debug_vars_EQ : Joined<["--"], "debug-vars=">,
  HelpText<"Print the locations (in registers or memory) of "
           "source-level variables alongside disassembly. "
           "Supported formats: ascii, unicode (default)">,
  Values<"unicode,ascii">;
def : Flag<["--"], "debug-vars">, Alias<debug_vars_EQ>, AliasArgs<["unicode"]>;

def debug_vars_indent_EQ : Joined<["--"], "debug-vars-indent=">,
  HelpText<"Distance to indent the source-level variable display, "
           "relative to the start of the disassembly">;

def x86_asm_syntax_att : Flag<["--"], "x86-asm-syntax=att">,
  HelpText<"Emit AT&T-style disassembly">;

def x86_asm_syntax_intel : Flag<["--"], "x86-asm-syntax=intel">,
  HelpText<"Emit Intel-style disassembly">;


def grp_mach_o : OptionGroup<"kind">, HelpText<"llvm-objdump MachO Specific Options">;

def private_header : Flag<["--"], "private-header">,
  HelpText<"Display only the first format specific file header">,
  Group<grp_mach_o>;

def exports_trie : Flag<["--"], "exports-trie">,
  HelpText<"Display mach-o exported symbols">,
  Group<grp_mach_o>;

def rebase : Flag<["--"], "rebase">,
  HelpText<"Display mach-o rebasing info">,
  Group<grp_mach_o>;

def bind : Flag<["--"], "bind">,
  HelpText<"Display mach-o binding info">,
  Group<grp_mach_o>;

def lazy_bind : Flag<["--"], "lazy-bind">,
  HelpText<"Display mach-o lazy binding info">,
  Group<grp_mach_o>;

def weak_bind : Flag<["--"], "weak-bind">,
  HelpText<"Display mach-o weak binding info">,
  Group<grp_mach_o>;

def g : Flag<["-"], "g">,
  HelpText<"Print line information from debug info if available">,
  Group<grp_mach_o>;

def dsym_EQ : Joined<["--"], "dsym=">,
  HelpText<"Use .dSYM file for debug info">,
  Group<grp_mach_o>;
def : Separate<["--"], "dsym">,
  Alias<dsym_EQ>,
  Group<grp_mach_o>;

def full_leading_addr : Flag<["--"], "full-leading-addr">,
  HelpText<"Print full leading address">,
  Group<grp_mach_o>;

def no_leading_headers : Flag<["--"], "no-leading-headers">,
  HelpText<"Print no leading headers">,
  Group<grp_mach_o>;

def universal_headers : Flag<["--"], "universal-headers">,
  HelpText<"Print Mach-O universal headers (requires --macho)">,
  Group<grp_mach_o>;

def archive_member_offsets : Flag<["--"], "archive-member-offsets">,
  HelpText<"Print the offset to each archive member for Mach-O archives "
           "(requires --macho and --archive-headers)">,
  Group<grp_mach_o>;

def indirect_symbols : Flag<["--"], "indirect-symbols">,
  HelpText<"Print indirect symbol table for Mach-O objects (requires --macho)">,
  Group<grp_mach_o>;

def data_in_code : Flag<["--"], "data-in-code">,
  HelpText<"Print the data in code table for Mach-O objects (requires --macho)">,
  Group<grp_mach_o>;

def function_starts_EQ : Joined<["--"], "function-starts=">,
  HelpText<"Print the function starts table for Mach-O objects. "
           "Options: addrs (default), names, both (requires --macho)">,
  Values<"addrs,names,both">,
  Group<grp_mach_o>;

def : Flag<["--"], "function-starts">, Alias<function_starts_EQ>,
  AliasArgs<["addrs"]>, Group<grp_mach_o>;

def link_opt_hints : Flag<["--"], "link-opt-hints">,
  HelpText<"Print the linker optimization hints for "
           "Mach-O objects (requires --macho)">,
  Group<grp_mach_o>;

def info_plist : Flag<["--"], "info-plist">,
  HelpText<"Print the info plist section as strings for "
           "Mach-O objects (requires --macho)">,
  Group<grp_mach_o>;

def chained_fixups : Flag<["--"], "chained-fixups">,
  HelpText<"Print chained fixup information (requires --macho)">,
  Group<grp_mach_o>;

def dyld_info : Flag<["--"], "dyld-info">,
  HelpText<"Print bind and rebase information used by dyld to resolve "
           "external references in a final linked binary "
           "(requires --macho)">,
  Group<grp_mach_o>;

def dylibs_used : Flag<["--"], "dylibs-used">,
  HelpText<"Print the shared libraries used for linked "
           "Mach-O files (requires --macho)">,
  Group<grp_mach_o>;

def dylib_id : Flag<["--"], "dylib-id">,
  HelpText<"Print the shared library's id for the "
           "dylib Mach-O file (requires --macho)">,
  Group<grp_mach_o>;

def rpaths : Flag<["--"], "rpaths">,
  HelpText<"Print the runtime search paths for the "
           "Mach-O file (requires --macho)">,
  Group<grp_mach_o>;

def non_verbose : Flag<["--"], "non-verbose">,
  HelpText<"Print the info for Mach-O objects in non-verbose or "
           "numeric form (requires --macho)">,
  Group<grp_mach_o>;

def objc_meta_data : Flag<["--"], "objc-meta-data">,
  HelpText<"Print the Objective-C runtime meta data "
           "for Mach-O files (requires --macho)">,
  Group<grp_mach_o>;

def dis_symname : Separate<["--"], "dis-symname">,
  HelpText<"disassemble just this symbol's instructions (requires --macho)">,
  Group<grp_mach_o>;

def no_symbolic_operands : Flag<["--"], "no-symbolic-operands">,
  HelpText<"do not symbolic operands when disassembling (requires --macho)">,
  Group<grp_mach_o>;

def arch_EQ : Joined<["--"], "arch=">,
  HelpText<"architecture(s) from a Mach-O file to dump">,
  Group<grp_mach_o>;
def : Separate<["--"], "arch">,
  Alias<arch_EQ>,
  Group<grp_mach_o>;
