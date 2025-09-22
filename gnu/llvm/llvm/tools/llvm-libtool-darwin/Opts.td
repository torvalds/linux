include "llvm/Option/OptParser.td"

class F<string letter, string help> : Flag<["-", "--"], letter>, HelpText<help>;
class FF<string name, string help> : Flag<["-", "--"], name>, HelpText<help>;
class S<string letter, string help> : Separate<["-", "--"], letter>, HelpText<help>;
class SS<string name, string help> : Separate<["-", "--"], name>, HelpText<help>;
class JS<string letter, string help> : JoinedOrSeparate<["-", "--"], letter>, HelpText<help>;

def help : FF<"help", "Display this help">;
def : F<"h", "">, Alias<help>;
def deterministicOption : F<"D","Use zero for timestamps and UIDs/GIDs (Default)">;
def nonDeterministicOption : F<"U", "Use actual timestamps and UIDs/GIDs">;
def version : FF<"version", "Display the version of this program">;
def : F<"V", "Print the version number and exit">, Alias<version>;
def noWarningForNoSymbols : FF<"no_warning_for_no_symbols", "Do not warn about files that have no symbols">;
def warningsAsErrors : FF<"warnings_as_errors", "Treat warnings as errors">;
def static : Flag<["-", "--"], "static">, HelpText<"Produce a statically linked library from the input files">;
def outputFile : S<"o", "Specify output filename">, MetaVarName<"<filename>">;
def fileList : SS<"filelist", "Pass in file containing a list of filenames">, MetaVarName<"<listfile[,dirname]>">;
def archType : SS<"arch_only", "Specify architecture type for output library">, MetaVarName<"<arch_type>">;
def libraries : JS<"l", "l<x> searches for the library libx.a in the library search path. If the string 'x'"#
                       " ends with '.o', then the library 'x' is searched for without prepending 'lib' or appending '.a'">, MetaVarName<"<x>">;
def librarySearchDirs : JS<"L", "L<dir> adds <dir> to the list of directories in which to search for libraries">, MetaVarName<"<dir>">;
def ignoredSyslibRoot : SS<"syslibroot", "">, Flags<[HelpHidden]>;
def dependencyInfoPath : SS<"dependency_info", "Write an Xcode dependency info file describing the dependencies of the created library">, MetaVarName<"<string>">;
