include "llvm/Option/OptParser.td"

// For LLVM-specific options, we prefer a two-dash prefix, but accept one for
// compatibility with llvm-mc. For clear separation from ML.EXE compatible
// options, slash is not accepted.
class LLVMFlag<string name> : Flag<["--", "-"], name>;
class LLVMJoined<string name> : Joined<["--", "-"], name>;
class LLVMJoinedOrSeparate<string name> : JoinedOrSeparate<["--", "-"], name>;
class LLVMSeparate<string name> : Separate<["--", "-"], name>;
class LLVMCommaJoined<string name> : CommaJoined<["--", "-"], name>;

def ml_Group : OptionGroup<"<ml options>">,
               HelpText<"ML.EXE COMPATIBILITY OPTIONS">;
class MLFlag<string name> : Flag<["/", "-"], name>, Group<ml_Group>;
class MLJoined<string name> : Joined<["/", "-"], name>, Group<ml_Group>;
class MLJoinedOrSeparate<string name> : JoinedOrSeparate<["/", "-"], name>,
                                        Group<ml_Group>;
class MLSeparate<string name> : Separate<["/", "-"], name>, Group<ml_Group>;

def unsupported_Group : OptionGroup<"unsupported">, Flags<[HelpHidden]>,
                        HelpText<"UNSUPPORTED ML.EXE COMPATIBILITY OPTIONS">;
class UnsupportedFlag<string name> : Flag<["/", "-"], name>,
                                     Group<unsupported_Group>;
class UnsupportedJoined<string name> : Joined<["/", "-"], name>,
                                       Group<unsupported_Group>;
class UnsupportedJoinedOrSeparate<string name> :
        JoinedOrSeparate<["/", "-"], name>, Group<unsupported_Group>;
class UnsupportedSeparate<string name> : Separate<["/", "-"], name>,
                                         Group<unsupported_Group>;

def bitness : LLVMJoined<"m">, Values<"32,64">,
              HelpText<"Target platform (x86 or x86-64)">;
def as_lex : LLVMFlag<"as-lex">,
             HelpText<"Lex tokens from a file without assembling">;
def debug : LLVMFlag<"debug">, Flags<[HelpHidden]>,
            HelpText<"Enable debug output">;
def debug_only : LLVMCommaJoined<"debug-only=">, Flags<[HelpHidden]>,
                 HelpText<"Enable a specific type of debug output (comma "
                          "separated list of types)">;
def fatal_warnings : LLVMFlag<"fatal-warnings">,
                     HelpText<"Treat warnings as errors">;
def filetype : LLVMJoined<"filetype=">, Values<"obj,s,null">,
               HelpText<"Emit a file with the given type">;
def output_att_asm : LLVMFlag<"output-att-asm">,
                     HelpText<"Use ATT syntax for output assembly">;
def show_encoding : LLVMFlag<"show-encoding">,
                    HelpText<"Show instruction encodings in output assembly">;
def show_inst : LLVMFlag<"show-inst">,
                HelpText<"Show internal instruction representation in output "
                         "assembly">;
def show_inst_operands : LLVMFlag<"show-inst-operands">,
                         HelpText<"Show instructions operands as parsed">;
def print_imm_hex : LLVMFlag<"print-imm-hex">,
                    HelpText<"Prefer hex format for immediate values in output "
                             "assembly">;
def preserve_comments : LLVMFlag<"preserve-comments">,
                        HelpText<"Preserve comments in output assembly">;
def save_temp_labels : LLVMFlag<"save-temp-labels">,
                       HelpText<"Don't discard temporary labels">;
def timestamp : LLVMJoined<"timestamp=">,
                HelpText<"Specify the assembly timestamp (used for @Date and "
                         "@Time built-ins)">;
def utc : LLVMFlag<"utc">,
          HelpText<"Render @Date and @Time built-ins in GMT/UTC">;
def gmtime : LLVMFlag<"gmtime">, Alias<utc>;

def help : MLFlag<"?">,
           HelpText<"Display available options">;
def help_long : MLFlag<"help">, Alias<help>;
def assemble_only : MLFlag<"c">, HelpText<"Assemble only; do not link">;
def define : MLJoinedOrSeparate<"D">, MetaVarName<"<macro>=<value>">,
             HelpText<"Define <macro> to <value> (or blank if <value> "
                      "omitted)">;
def no_logo : MLFlag<"nologo">, HelpText<"">;
def output_file : MLJoinedOrSeparate<"Fo">, HelpText<"Names the output file">;
def include_path : MLJoinedOrSeparate<"I">,
                   HelpText<"Sets path for include files">;
def safeseh : MLFlag<"safeseh">,
              HelpText<"Mark resulting object files as either containing no "
                       "exception handlers or containing exception handlers "
                       "that are all declared with .SAFESEH. Only available in "
                       "32-bit.">;
def assembly_file : MLJoinedOrSeparate<"Ta">,
                    HelpText<"Assemble source file with the given name. Used "
                             "if the filename begins with a forward slash.">;
def error_on_warning : MLFlag<"WX">, Alias<fatal_warnings>;
def parse_only : MLFlag<"Zs">, HelpText<"Run a syntax-check only">,
                 Alias<filetype>, AliasArgs<["null"]>;
def ignore_include_envvar : MLFlag<"X">,
                            HelpText<"Ignore the INCLUDE environment variable">;

def tiny_model_support : UnsupportedFlag<"AT">, HelpText<"">;
def alternate_linker : UnsupportedJoined<"Bl">, HelpText<"">;
def coff_object_file : UnsupportedFlag<"coff">, HelpText<"">;
def preserve_identifier_case : UnsupportedFlag<"Cp">, HelpText<"">;
def uppercase_identifiers : UnsupportedFlag<"Cu">, HelpText<"">;
def preserve_extern_case : UnsupportedFlag<"Cx">, HelpText<"">;
def output_preprocessed : UnsupportedFlag<"EP">, HelpText<"">;
def errorreport : UnsupportedJoined<"ERRORREPORT">, HelpText<"">;
def stacksize : UnsupportedSeparate<"F">, HelpText<"">;
def executable_file : UnsupportedSeparate<"Fe">, HelpText<"">;
def code_listing_file : UnsupportedJoined<"FI">, HelpText<"">;
def linker_map_file : UnsupportedJoined<"Fm">, HelpText<"">;
def fp_emulator_fixups : UnsupportedFlag<"FPi">, HelpText<"">;
def source_browser_file : UnsupportedJoined<"Fr">, HelpText<"">;
def extended_source_browser_file : UnsupportedJoined<"FR">, HelpText<"">;
def pascal_conventions : UnsupportedFlag<"Gc">, HelpText<"">;
def c_conventions : UnsupportedFlag<"Gd">, HelpText<"">;
def stdcall_conventions : UnsupportedFlag<"GZ">, HelpText<"">;
def extern_name_limit : UnsupportedSeparate<"H">, HelpText<"">;
def omf_object_file : UnsupportedFlag<"omf">, HelpText<"">;
def full_listing : UnsupportedFlag<"Sa">, HelpText<"">;
def first_pass_listing : UnsupportedFlag<"Sf">, HelpText<"">;
def listing_width : UnsupportedSeparate<"SI">, HelpText<"">;
def listing_without_symbols : UnsupportedFlag<"Sn">, HelpText<"">;
def listing_page_length : UnsupportedSeparate<"Sp">, HelpText<"">;
def listing_subtitle : UnsupportedSeparate<"Ss">, HelpText<"">;
def listing_title : UnsupportedSeparate<"St">, HelpText<"">;
def listing_false_conditionals : UnsupportedFlag<"Sx">, HelpText<"">;
def extra_warnings : UnsupportedFlag<"w">, HelpText<"">;
def warning_level : UnsupportedJoined<"W">, HelpText<"">;
def line_number_info : UnsupportedFlag<"Zd">, HelpText<"">;
def export_all_symbols : UnsupportedFlag<"Zf">, HelpText<"">;
def codeview_info : UnsupportedFlag<"Zi">, HelpText<"">;
def enable_m510_option : UnsupportedFlag<"Zm">, HelpText<"">;
def structure_packing : UnsupportedJoined<"Zp">, HelpText<"">;
