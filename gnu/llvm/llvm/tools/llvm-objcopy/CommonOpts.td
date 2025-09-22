include "llvm/Option/OptParser.td"

multiclass Eq<string name, string help> {
  def NAME : Separate<["--"], name>;
  def NAME #_eq : Joined<["--"], name #"=">,
                  Alias<!cast<Separate>(NAME)>,
                  HelpText<help>;
}

def grp_coff : OptionGroup<"kind">, HelpText<"OPTIONS (COFF specific)">;
def grp_macho : OptionGroup<"kind">, HelpText<"OPTIONS (Mach-O specific)">;

def help : Flag<["--"], "help">;
def h : Flag<["-"], "h">, Alias<help>;

def allow_broken_links
    : Flag<["--"], "allow-broken-links">,
      HelpText<"Allow the tool to remove sections even if it would leave "
               "invalid section references. The appropriate sh_link fields "
               "will be set to zero.">;

def enable_deterministic_archives
    : Flag<["--"], "enable-deterministic-archives">,
      HelpText<"Enable deterministic mode when operating on archives (use "
               "zero for UIDs, GIDs, and timestamps).">;
def D : Flag<["-"], "D">,
        Alias<enable_deterministic_archives>,
        HelpText<"Alias for --enable-deterministic-archives">;

def disable_deterministic_archives
    : Flag<["--"], "disable-deterministic-archives">,
      HelpText<"Disable deterministic mode when operating on archives (use "
               "real values for UIDs, GIDs, and timestamps).">;
def U : Flag<["-"], "U">,
        Alias<disable_deterministic_archives>,
        HelpText<"Alias for --disable-deterministic-archives">;

def preserve_dates : Flag<["--"], "preserve-dates">,
                     HelpText<"Preserve access and modification timestamps">;
def p : Flag<["-"], "p">,
        Alias<preserve_dates>,
        HelpText<"Alias for --preserve-dates">;

def strip_all : Flag<["--"], "strip-all">,
    HelpText<"For ELF, remove all symbols and non-alloc sections not within "
             "segments, except for .gnu.warning*, .ARM.attribute, and the section name table. "
             "For COFF and Mach-O, remove all symbols, debug sections, and relocations">;

def strip_all_gnu
    : Flag<["--"], "strip-all-gnu">,
      HelpText<"Remove all symbols, debug sections and relocations. Compatible with GNU's --strip-all">;

def strip_debug : Flag<["--"], "strip-debug">,
                  HelpText<"Remove all debug sections">;
def g : Flag<["-"], "g">,
        Alias<strip_debug>,
        HelpText<"Alias for --strip-debug">;

def strip_unneeded : Flag<["--"], "strip-unneeded">,
                     HelpText<"Remove all symbols not needed by relocations">;

defm remove_section : Eq<"remove-section", "Remove <section>">,
                      MetaVarName<"section">;
def R : JoinedOrSeparate<["-"], "R">,
        Alias<remove_section>,
        HelpText<"Alias for --remove-section">;

def strip_sections
    : Flag<["--"], "strip-sections">,
      HelpText<"Remove all section headers and all section data not within segments">;

defm strip_symbol : Eq<"strip-symbol", "Strip <symbol>">,
                    MetaVarName<"symbol">;
def N : JoinedOrSeparate<["-"], "N">,
        Alias<strip_symbol>,
        HelpText<"Alias for --strip-symbol">;

defm keep_section : Eq<"keep-section", "Keep <section>">,
                    MetaVarName<"section">;

defm keep_symbol : Eq<"keep-symbol", "When removing symbols, do not remove <symbol>">,
                   MetaVarName<"symbol">;
def K : JoinedOrSeparate<["-"], "K">,
        Alias<keep_symbol>,
        HelpText<"Alias for --keep-symbol">;

def keep_file_symbols : Flag<["--"], "keep-file-symbols">,
                        HelpText<"Keep symbols of type STT_FILE, even if they would otherwise be stripped">;

def keep_undefined : Flag<["--"], "keep-undefined">,
                        HelpText<"Do not remove undefined symbols">, Group<grp_macho>;

def only_keep_debug
    : Flag<["--"], "only-keep-debug">,
      HelpText<
          "Produce a debug file as the output that only preserves contents of "
          "sections useful for debugging purposes">;

def discard_locals : Flag<["--"], "discard-locals">,
                     HelpText<"Remove local symbols starting with .L">;
def X : Flag<["-"], "X">,
        Alias<discard_locals>,
        HelpText<"Alias for --discard-locals">;

def discard_all
    : Flag<["--"], "discard-all">,
      HelpText<"Remove most local symbols. Different file formats may limit this to a subset. "
      "For ELF, file and section symbols are not discarded. "
      "Additionally, remove all debug sections">;
def x : Flag<["-"], "x">,
        Alias<discard_all>,
        HelpText<"Alias for --discard-all">;

def regex
    : Flag<["--"], "regex">,
      HelpText<"Permit regular expressions in name comparison">;

def version : Flag<["--"], "version">,
              HelpText<"Print the version and exit.">;
def V : Flag<["-"], "V">,
        Alias<version>,
        HelpText<"Alias for --version">;

def wildcard
    : Flag<["--"], "wildcard">,
      HelpText<"Allow wildcard syntax for symbol-related flags. Incompatible "
               "with --regex. Allows using '*' to match any number of "
               "characters, '?' to match any single character, '\' to escape "
               "special characters, and '[]' to define character classes. "
               "Wildcards beginning with '!' will prevent a match, for example "
               "\"-N '*' -N '!x'\" will strip all symbols except for \"x\".">;
def w : Flag<["-"], "w">, Alias<wildcard>, HelpText<"Alias for --wildcard">;
