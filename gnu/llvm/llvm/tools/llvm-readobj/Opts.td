include "llvm/Option/OptParser.td"

class F<string letter, string help> : Flag<["-"], letter>, HelpText<help>;
class FF<string name, string help> : Flag<["--"], name>, HelpText<help>;

multiclass BB<string name, string help1, string help2> {
  def NAME: Flag<["--"], name>, HelpText<help1>;
  def no_ # NAME: Flag<["--"], "no-" # name>, HelpText<help2>;
}

multiclass Eq<string name, string help> {
  def NAME #_EQ : Joined<["--"], name #"=">, HelpText<help>;
  def : Separate<["--"], name>, Alias<!cast<Joined>(NAME #_EQ)>;
}

def addrsig : FF<"addrsig", "Display address-significance table">;
def all : FF<"all", "Equivalent to setting: --file-header, --program-headers, --section-headers, "
             "--symbols, --relocations, --dynamic-table, --notes, --version-info, --unwind, "
             "--section-groups and --histogram">;
def arch_specific : FF<"arch-specific", "Display architecture-specific information">;
def bb_addr_map : FF<"bb-addr-map", "Display the BB address map section">;
def pretty_pgo_analysis_map : FF<"pretty-pgo-analysis-map", "Display PGO analysis values with formatting rather than raw numbers">;
def cg_profile : FF<"cg-profile", "Display call graph profile section">;
def decompress : FF<"decompress", "Dump decompressed section content when used with -x or -p">;
defm demangle : BB<"demangle", "Demangle symbol names", "Do not demangle symbol names (default)">;
def dependent_libraries : FF<"dependent-libraries", "Display the dependent libraries section">;
def dyn_relocations : FF<"dyn-relocations", "Display the dynamic relocation entries in the file">;
def dyn_syms : FF<"dyn-syms", "Display the dynamic symbol table">;
def expand_relocs : FF<"expand-relocs", "Expand each shown relocation to multiple lines">;
def extra_sym_info : FF<"extra-sym-info", "Display extra information when showing symbols">;
def file_header : FF<"file-header", "Display file header">;
def headers : FF<"headers", "Equivalent to setting: --file-header, --program-headers, --section-headers">;
defm hex_dump : Eq<"hex-dump", "Display the specified section(s) as hexadecimal bytes">, MetaVarName<"<name or index>">;
def pretty_print : FF<"pretty-print", "Pretty print JSON output">;
def relocs : FF<"relocs", "Display the relocation entries in the file">;
def section_data : FF<"section-data", "Display section data for each section shown. This option has no effect for GNU style output">;
def section_details : FF<"section-details", "Display the section details">;
def section_headers : FF<"section-headers", "Display section headers">;
def section_mapping : FF<"section-mapping", "Display the section to segment mapping">;
def section_mapping_EQ_false : FF<"section-mapping=false", "Don't display the section to segment mapping">, Flags<[HelpHidden]>;
def section_relocations : FF<"section-relocations", "Display relocations for each section shown. This option has no effect for GNU style output">;
def section_symbols : FF<"section-symbols", "Display symbols for each section shown. This option has no effect for GNU style output">;
defm sort_symbols : Eq<"sort-symbols", "Specify the keys to sort the symbols before displaying symtab">;
def stack_sizes : FF<"stack-sizes", "Display contents of all stack sizes sections. This option has no effect for GNU style output">;
def stackmap : FF<"stackmap", "Display contents of stackmap section">;
defm string_dump : Eq<"string-dump", "Display the specified section(s) as a list of strings">, MetaVarName<"<name or index>">;
def string_table : FF<"string-table", "Display the string table (only for XCOFF now)">;
def symbols : FF<"symbols", "Display the symbol table. Also display the dynamic symbol table when using GNU output style for ELF">;
def unwind : FF<"unwind", "Display unwind information">;

// ELF specific options.
def grp_elf : OptionGroup<"kind">, HelpText<"OPTIONS (ELF specific)">;
def dynamic_table : FF<"dynamic-table", "Display the dynamic section table">, Group<grp_elf>;
def elf_linker_options : FF<"elf-linker-options", "Display the .linker-options section">, Group<grp_elf>;
defm elf_output_style : Eq<"elf-output-style", "Specify ELF dump style: LLVM, GNU, JSON">, Group<grp_elf>;
def histogram : FF<"histogram", "Display bucket list histogram for hash sections">, Group<grp_elf>;
def section_groups : FF<"section-groups", "Display section groups">, Group<grp_elf>;
def gnu_hash_table : FF<"gnu-hash-table", "Display the GNU hash table for dynamic symbols">, Group<grp_elf>;
def hash_symbols : FF<"hash-symbols", "Display the dynamic symbols derived from the hash section">, Group<grp_elf>;
def hash_table : FF<"hash-table", "Display .hash section">, Group<grp_elf>;
def memtag : FF<"memtag", "Display memory tagging metadata (modes, Android notes, global descriptors)">, Group<grp_elf>;
def needed_libs : FF<"needed-libs", "Display the needed libraries">, Group<grp_elf>;
def notes : FF<"notes", "Display notes">, Group<grp_elf>;
def program_headers : FF<"program-headers", "Display program headers">, Group<grp_elf>;
def version_info : FF<"version-info", "Display version sections">, Group<grp_elf>;

// Mach-O specific options.
def grp_mach_o : OptionGroup<"kind">, HelpText<"OPTIONS (Mach-O specific)">;
def macho_data_in_code : FF<"macho-data-in-code", "Display Data in Code command">, Group<grp_mach_o>;
def macho_dysymtab : FF<"macho-dysymtab", "Display Dysymtab command">, Group<grp_mach_o>;
def macho_indirect_symbols : FF<"macho-indirect-symbols", "Display indirect symbols">, Group<grp_mach_o>;
def macho_linker_options : FF<"macho-linker-options", "Display linker options">, Group<grp_mach_o>;
def macho_segment : FF<"macho-segment", "Display Segment command">, Group<grp_mach_o>;
def macho_version_min : FF<"macho-version-min", "Display version min command">, Group<grp_mach_o>;

// PE/COFF specific options.
def grp_coff : OptionGroup<"kind">, HelpText<"OPTIONS (PE/COFF specific)">;
def codeview : FF<"codeview", "Display CodeView debug information">, Group<grp_coff>;
def codeview_ghash : FF<"codeview-ghash", "Enable global hashing for CodeView type stream de-duplication">, Group<grp_coff>;
def codeview_merged_types : FF<"codeview-merged-types", "Display the merged CodeView type stream">, Group<grp_coff>;
def codeview_subsection_bytes : FF<"codeview-subsection-bytes", "Dump raw contents of codeview debug sections and records">, Group<grp_coff>;
def coff_basereloc : FF<"coff-basereloc", "Display .reloc section">, Group<grp_coff>;
def coff_debug_directory : FF<"coff-debug-directory", "Display debug directory">, Group<grp_coff>;
def coff_directives : FF<"coff-directives", "Display .drectve section">, Group<grp_coff>;
def coff_exports : FF<"coff-exports", "Display export table">, Group<grp_coff>;
def coff_imports : FF<"coff-imports", "Display import table">, Group<grp_coff>;
def coff_load_config : FF<"coff-load-config", "Display load config">, Group<grp_coff>;
def coff_resources : FF<"coff-resources", "Display .rsrc section">, Group<grp_coff>;
def coff_tls_directory : FF<"coff-tls-directory", "Display TLS directory">, Group<grp_coff>;

// XCOFF specific options.
def grp_xcoff : OptionGroup<"kind">, HelpText<"OPTIONS (XCOFF specific)">;
def auxiliary_header : FF<"auxiliary-header" , "Display the auxiliary header">, Group<grp_xcoff>;
def exception_section : FF<"exception-section" , "Display the exception section entries">, Group<grp_xcoff>;
def loader_section_header : FF<"loader-section-header" , "Display the loader section header">, Group<grp_xcoff>;
def loader_section_symbols : FF<"loader-section-symbols" , "Display the loader section symbol table">, Group<grp_xcoff>;
def loader_section_relocations : FF<"loader-section-relocations" , "Display the loader section relocation entries">, Group<grp_xcoff>;

def help : FF<"help", "Display this help">;
def version : FF<"version", "Display the version">;

// Ignored for GNU readelf compatibility.
def wide : FF<"wide", "Ignored for GNU readelf compatibility">;
def : F<"W", "Ignored for GNU readelf compatibility">, Alias<wide>;

// Traditional llvm-readobj Aliases.
def : Flag<["--"], "dt">, Alias<dyn_syms>, HelpText<"Alias for --dyn-syms">;
def : Flag<["--"], "sd">, Alias<section_data>, HelpText<"Alias for --section-data">;
def : Flag<["--"], "st">, Alias<section_symbols>, HelpText<"Alias for --section-symbols">;
def : Flag<["--"], "sr">, Alias<section_relocations>, HelpText<"Alias for --section-relocations">;

// Aliases.
def : FF<"dyn-symbols", "Alias for --dyn-syms">, Alias<dyn_syms>;
def : FF<"dynamic", "Alias for --dynamic-table">, Alias<dynamic_table>;
def : FF<"elf-cg-profile", "Alias for --cg-profile">, Alias<cg_profile>, Flags<[HelpHidden]>;
def : FF<"elf-hash-histogram", "Alias for --histogram">, Alias<histogram>, Flags<[HelpHidden]>;
def : FF<"elf-section-groups", "Alias for --section-groups">, Alias<section_groups>, Flags<[HelpHidden]>;
def : FF<"file-headers", "Alias for --file-header">, Alias<file_header>, Flags<[HelpHidden]>;
def : FF<"relocations", "Alias for --relocs">, Alias<relocs>;
def : FF<"sections", "Alias for --section-headers">, Alias<section_headers>;
def : FF<"segments", "Alias for --program-headers">, Alias<program_headers>, Group<grp_elf>;
def : FF<"syms", "Alias for --symbols">, Alias<symbols>;

def : F<"A", "Alias for --arch-specific">, Alias<arch_specific>;
def : F<"a", "Alias for --all">, Alias<all>;
def : F<"C", "Alias for --demangle">, Alias<demangle>;
def : F<"d", "Alias for --dynamic-table">, Alias<dynamic_table>, Group<grp_elf>;
def : F<"e", "Alias for --headers">, Alias<headers>;
def : F<"g", "Alias for --section-groups">, Alias<section_groups>, Group<grp_elf>;
def : F<"h", "Alias for --file-header">, Alias<file_header>;
def : F<"I", "Alias for --histogram">, Alias<histogram>, Group<grp_elf>;
def : F<"l", "Alias for --program-headers">, Alias<program_headers>;
def : F<"n", "Alias for --notes">, Alias<notes>;
def : JoinedOrSeparate<["-"], "p">, Alias<string_dump_EQ>, HelpText<"Alias for --string-dump">, MetaVarName<"<name or index>">;
def : F<"r", "Alias for --relocs">, Alias<relocs>;
def : F<"S", "Alias for --section-headers">, Alias<section_headers>;
def : F<"s", "Alias for --symbols">, Alias<symbols>;
def : F<"t", "Alias for --section-details">, Alias<section_details>;
def : F<"u", "Alias for --unwind">, Alias<unwind>;
def : F<"X", "Alias for --extra-sym-info">, Alias<extra_sym_info>, Group<grp_elf>;
def : F<"V", "Alias for --version-info">, Alias<version_info>, Group<grp_elf>;
def : JoinedOrSeparate<["-"], "x">, Alias<hex_dump_EQ>, HelpText<"Alias for --hex-dump">, MetaVarName<"<name or index>">;
def : F<"z", "Alias for --decompress">, Alias<decompress>;
