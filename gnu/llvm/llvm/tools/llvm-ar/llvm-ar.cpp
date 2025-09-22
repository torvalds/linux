//===-- llvm-ar.cpp - LLVM archive librarian utility ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Builds up (relatively) standard unix archive files (.a) containing LLVM
// bitcode or other files.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/ToolDrivers/llvm-dlltool/DlltoolDriver.h"
#include "llvm/ToolDrivers/llvm-lib/LibDriver.h"

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

#ifdef _WIN32
#include "llvm/Support/Windows/WindowsSupport.h"
#endif

using namespace llvm;
using namespace llvm::object;

// The name this program was invoked as.
static StringRef ToolName;

// The basename of this program.
static StringRef Stem;

static void printRanLibHelp(StringRef ToolName) {
  outs() << "OVERVIEW: LLVM ranlib\n\n"
         << "Generate an index for archives\n\n"
         << "USAGE: " + ToolName + " archive...\n\n"
         << "OPTIONS:\n"
         << "  -h --help             - Display available options\n"
         << "  -V --version          - Display the version of this program\n"
         << "  -D                    - Use zero for timestamps and uids/gids "
            "(default)\n"
         << "  -U                    - Use actual timestamps and uids/gids\n"
         << "  -X{32|64|32_64|any}   - Specify which archive symbol tables "
            "should be generated if they do not already exist (AIX OS only)\n";
}

static void printArHelp(StringRef ToolName) {
  const char ArOptions[] =
      R"(OPTIONS:
  --format              - archive format to create
    =default            -   default
    =gnu                -   gnu
    =darwin             -   darwin
    =bsd                -   bsd
    =bigarchive         -   big archive (AIX OS)
    =coff               -   coff
  --plugin=<string>     - ignored for compatibility
  -h --help             - display this help and exit
  --output              - the directory to extract archive members to
  --rsp-quoting         - quoting style for response files
    =posix              -   posix
    =windows            -   windows
  --thin                - create a thin archive
  --version             - print the version and exit
  -X{32|64|32_64|any}   - object mode (only for AIX OS)
  @<file>               - read options from <file>

OPERATIONS:
  d - delete [files] from the archive
  m - move [files] in the archive
  p - print contents of [files] found in the archive
  q - quick append [files] to the archive
  r - replace or insert [files] into the archive
  s - act as ranlib
  t - display list of files in archive
  x - extract [files] from the archive

MODIFIERS:
  [a] - put [files] after [relpos]
  [b] - put [files] before [relpos] (same as [i])
  [c] - do not warn if archive had to be created
  [D] - use zero for timestamps and uids/gids (default)
  [h] - display this help and exit
  [i] - put [files] before [relpos] (same as [b])
  [l] - ignored for compatibility
  [L] - add archive's contents
  [N] - use instance [count] of name
  [o] - preserve original dates
  [O] - display member offsets
  [P] - use full names when matching (implied for thin archives)
  [s] - create an archive index (cf. ranlib)
  [S] - do not build a symbol table
  [T] - deprecated, use --thin instead
  [u] - update only [files] newer than archive contents
  [U] - use actual timestamps and uids/gids
  [v] - be verbose about actions taken
  [V] - display the version and exit
)";

  outs() << "OVERVIEW: LLVM Archiver\n\n"
         << "USAGE: " + ToolName +
                " [options] [-]<operation>[modifiers] [relpos] "
                "[count] <archive> [files]\n"
         << "       " + ToolName + " -M [<mri-script]\n\n";

  outs() << ArOptions;
}

static void printHelpMessage() {
  if (Stem.contains_insensitive("ranlib"))
    printRanLibHelp(Stem);
  else if (Stem.contains_insensitive("ar"))
    printArHelp(Stem);
}

static unsigned MRILineNumber;
static bool ParsingMRIScript;

// Show the error plus the usage message, and exit.
[[noreturn]] static void badUsage(Twine Error) {
  WithColor::error(errs(), ToolName) << Error << "\n";
  printHelpMessage();
  exit(1);
}

// Show the error message and exit.
[[noreturn]] static void fail(Twine Error) {
  if (ParsingMRIScript) {
    WithColor::error(errs(), ToolName)
        << "script line " << MRILineNumber << ": " << Error << "\n";
  } else {
    WithColor::error(errs(), ToolName) << Error << "\n";
  }
  exit(1);
}

static void failIfError(std::error_code EC, Twine Context = "") {
  if (!EC)
    return;

  std::string ContextStr = Context.str();
  if (ContextStr.empty())
    fail(EC.message());
  fail(Context + ": " + EC.message());
}

static void failIfError(Error E, Twine Context = "") {
  if (!E)
    return;

  handleAllErrors(std::move(E), [&](const llvm::ErrorInfoBase &EIB) {
    std::string ContextStr = Context.str();
    if (ContextStr.empty())
      fail(EIB.message());
    fail(Context + ": " + EIB.message());
  });
}

static void warn(Twine Message) {
  WithColor::warning(errs(), ToolName) << Message << "\n";
}

static SmallVector<const char *, 256> PositionalArgs;

static bool MRI;

namespace {
enum Format { Default, GNU, COFF, BSD, DARWIN, BIGARCHIVE, Unknown };
}

static Format FormatType = Default;

static std::string Options;

// This enumeration delineates the kinds of operations on an archive
// that are permitted.
enum ArchiveOperation {
  Print,           ///< Print the contents of the archive
  Delete,          ///< Delete the specified members
  Move,            ///< Move members to end or as given by {a,b,i} modifiers
  QuickAppend,     ///< Quickly append to end of archive
  ReplaceOrInsert, ///< Replace or Insert members
  DisplayTable,    ///< Display the table of contents
  Extract,         ///< Extract files back to file system
  CreateSymTab     ///< Create a symbol table in an existing archive
};

enum class BitModeTy { Bit32, Bit64, Bit32_64, Any, Unknown };

static BitModeTy BitMode = BitModeTy::Bit32;

// Modifiers to follow operation to vary behavior
static bool AddAfter = false;             ///< 'a' modifier
static bool AddBefore = false;            ///< 'b' modifier
static bool Create = false;               ///< 'c' modifier
static bool OriginalDates = false;        ///< 'o' modifier
static bool DisplayMemberOffsets = false; ///< 'O' modifier
static bool CompareFullPath = false;      ///< 'P' modifier
static bool OnlyUpdate = false;           ///< 'u' modifier
static bool Verbose = false;              ///< 'v' modifier
static SymtabWritingMode Symtab =
    SymtabWritingMode::NormalSymtab;      ///< 's' modifier
static bool Deterministic = true;         ///< 'D' and 'U' modifiers
static bool Thin = false;                 ///< 'T' modifier
static bool AddLibrary = false;           ///< 'L' modifier

// Relative Positional Argument (for insert/move). This variable holds
// the name of the archive member to which the 'a', 'b' or 'i' modifier
// refers. Only one of 'a', 'b' or 'i' can be specified so we only need
// one variable.
static std::string RelPos;

// Count parameter for 'N' modifier. This variable specifies which file should
// match for extract/delete operations when there are multiple matches. This is
// 1-indexed. A value of 0 is invalid, and implies 'N' is not used.
static int CountParam = 0;

// This variable holds the name of the archive file as given on the
// command line.
static std::string ArchiveName;

// Output directory specified by --output.
static std::string OutputDir;

static std::vector<std::unique_ptr<MemoryBuffer>> ArchiveBuffers;
static std::vector<std::unique_ptr<object::Archive>> Archives;

// This variable holds the list of member files to proecess, as given
// on the command line.
static std::vector<StringRef> Members;

// Static buffer to hold StringRefs.
static BumpPtrAllocator Alloc;

// Extract the member filename from the command line for the [relpos] argument
// associated with a, b, and i modifiers
static void getRelPos() {
  if (PositionalArgs.empty())
    fail("expected [relpos] for 'a', 'b', or 'i' modifier");
  RelPos = PositionalArgs[0];
  PositionalArgs.erase(PositionalArgs.begin());
}

// Extract the parameter from the command line for the [count] argument
// associated with the N modifier
static void getCountParam() {
  if (PositionalArgs.empty())
    badUsage("expected [count] for 'N' modifier");
  auto CountParamArg = StringRef(PositionalArgs[0]);
  if (CountParamArg.getAsInteger(10, CountParam))
    badUsage("value for [count] must be numeric, got: " + CountParamArg);
  if (CountParam < 1)
    badUsage("value for [count] must be positive, got: " + CountParamArg);
  PositionalArgs.erase(PositionalArgs.begin());
}

// Get the archive file name from the command line
static void getArchive() {
  if (PositionalArgs.empty())
    badUsage("an archive name must be specified");
  ArchiveName = PositionalArgs[0];
  PositionalArgs.erase(PositionalArgs.begin());
}

static object::Archive &readLibrary(const Twine &Library) {
  auto BufOrErr = MemoryBuffer::getFile(Library, /*IsText=*/false,
                                        /*RequiresNullTerminator=*/false);
  failIfError(BufOrErr.getError(), "could not open library " + Library);
  ArchiveBuffers.push_back(std::move(*BufOrErr));
  auto LibOrErr =
      object::Archive::create(ArchiveBuffers.back()->getMemBufferRef());
  failIfError(errorToErrorCode(LibOrErr.takeError()),
              "could not parse library");
  Archives.push_back(std::move(*LibOrErr));
  return *Archives.back();
}

static void runMRIScript();

// Parse the command line options as presented and return the operation
// specified. Process all modifiers and check to make sure that constraints on
// modifier/operation pairs have not been violated.
static ArchiveOperation parseCommandLine() {
  if (MRI) {
    if (!PositionalArgs.empty() || !Options.empty())
      badUsage("cannot mix -M and other options");
    runMRIScript();
  }

  // Keep track of number of operations. We can only specify one
  // per execution.
  unsigned NumOperations = 0;

  // Keep track of the number of positional modifiers (a,b,i). Only
  // one can be specified.
  unsigned NumPositional = 0;

  // Keep track of which operation was requested
  ArchiveOperation Operation;

  bool MaybeJustCreateSymTab = false;

  for (unsigned i = 0; i < Options.size(); ++i) {
    switch (Options[i]) {
    case 'd':
      ++NumOperations;
      Operation = Delete;
      break;
    case 'm':
      ++NumOperations;
      Operation = Move;
      break;
    case 'p':
      ++NumOperations;
      Operation = Print;
      break;
    case 'q':
      ++NumOperations;
      Operation = QuickAppend;
      break;
    case 'r':
      ++NumOperations;
      Operation = ReplaceOrInsert;
      break;
    case 't':
      ++NumOperations;
      Operation = DisplayTable;
      break;
    case 'x':
      ++NumOperations;
      Operation = Extract;
      break;
    case 'c':
      Create = true;
      break;
    case 'l': /* accepted but unused */
      break;
    case 'o':
      OriginalDates = true;
      break;
    case 'O':
      DisplayMemberOffsets = true;
      break;
    case 'P':
      CompareFullPath = true;
      break;
    case 's':
      Symtab = SymtabWritingMode::NormalSymtab;
      MaybeJustCreateSymTab = true;
      break;
    case 'S':
      Symtab = SymtabWritingMode::NoSymtab;
      break;
    case 'u':
      OnlyUpdate = true;
      break;
    case 'v':
      Verbose = true;
      break;
    case 'a':
      getRelPos();
      AddAfter = true;
      NumPositional++;
      break;
    case 'b':
      getRelPos();
      AddBefore = true;
      NumPositional++;
      break;
    case 'i':
      getRelPos();
      AddBefore = true;
      NumPositional++;
      break;
    case 'D':
      Deterministic = true;
      break;
    case 'U':
      Deterministic = false;
      break;
    case 'N':
      getCountParam();
      break;
    case 'T':
      Thin = true;
      break;
    case 'L':
      AddLibrary = true;
      break;
    case 'V':
      cl::PrintVersionMessage();
      exit(0);
    case 'h':
      printHelpMessage();
      exit(0);
    default:
      badUsage(std::string("unknown option ") + Options[i]);
    }
  }

  // Thin archives store path names, so P should be forced.
  if (Thin)
    CompareFullPath = true;

  // At this point, the next thing on the command line must be
  // the archive name.
  getArchive();

  // Everything on the command line at this point is a member.
  Members.assign(PositionalArgs.begin(), PositionalArgs.end());

  if (NumOperations == 0 && MaybeJustCreateSymTab) {
    NumOperations = 1;
    Operation = CreateSymTab;
    if (!Members.empty())
      badUsage("the 's' operation takes only an archive as argument");
  }

  // Perform various checks on the operation/modifier specification
  // to make sure we are dealing with a legal request.
  if (NumOperations == 0)
    badUsage("you must specify at least one of the operations");
  if (NumOperations > 1)
    badUsage("only one operation may be specified");
  if (NumPositional > 1)
    badUsage("you may only specify one of 'a', 'b', and 'i' modifiers");
  if (AddAfter || AddBefore)
    if (Operation != Move && Operation != ReplaceOrInsert)
      badUsage("the 'a', 'b' and 'i' modifiers can only be specified with "
               "the 'm' or 'r' operations");
  if (CountParam)
    if (Operation != Extract && Operation != Delete)
      badUsage("the 'N' modifier can only be specified with the 'x' or 'd' "
               "operations");
  if (OriginalDates && Operation != Extract)
    badUsage("the 'o' modifier is only applicable to the 'x' operation");
  if (OnlyUpdate && Operation != ReplaceOrInsert)
    badUsage("the 'u' modifier is only applicable to the 'r' operation");
  if (AddLibrary && Operation != QuickAppend)
    badUsage("the 'L' modifier is only applicable to the 'q' operation");

  if (!OutputDir.empty()) {
    if (Operation != Extract)
      badUsage("--output is only applicable to the 'x' operation");
    bool IsDir = false;
    // If OutputDir is not a directory, create_directories may still succeed if
    // all components of the path prefix are directories. Test is_directory as
    // well.
    if (!sys::fs::create_directories(OutputDir))
      sys::fs::is_directory(OutputDir, IsDir);
    if (!IsDir)
      fail("'" + OutputDir + "' is not a directory");
  }

  // Return the parsed operation to the caller
  return Operation;
}

// Implements the 'p' operation. This function traverses the archive
// looking for members that match the path list.
static void doPrint(StringRef Name, const object::Archive::Child &C) {
  if (Verbose)
    outs() << "Printing " << Name << "\n";

  Expected<StringRef> DataOrErr = C.getBuffer();
  failIfError(DataOrErr.takeError());
  StringRef Data = *DataOrErr;
  outs().write(Data.data(), Data.size());
}

// Utility function for printing out the file mode when the 't' operation is in
// verbose mode.
static void printMode(unsigned mode) {
  outs() << ((mode & 004) ? "r" : "-");
  outs() << ((mode & 002) ? "w" : "-");
  outs() << ((mode & 001) ? "x" : "-");
}

// Implement the 't' operation. This function prints out just
// the file names of each of the members. However, if verbose mode is requested
// ('v' modifier) then the file type, permission mode, user, group, size, and
// modification time are also printed.
static void doDisplayTable(StringRef Name, const object::Archive::Child &C) {
  if (Verbose) {
    Expected<sys::fs::perms> ModeOrErr = C.getAccessMode();
    failIfError(ModeOrErr.takeError());
    sys::fs::perms Mode = ModeOrErr.get();
    printMode((Mode >> 6) & 007);
    printMode((Mode >> 3) & 007);
    printMode(Mode & 007);
    Expected<unsigned> UIDOrErr = C.getUID();
    failIfError(UIDOrErr.takeError());
    outs() << ' ' << UIDOrErr.get();
    Expected<unsigned> GIDOrErr = C.getGID();
    failIfError(GIDOrErr.takeError());
    outs() << '/' << GIDOrErr.get();
    Expected<uint64_t> Size = C.getSize();
    failIfError(Size.takeError());
    outs() << ' ' << format("%6llu", Size.get());
    auto ModTimeOrErr = C.getLastModified();
    failIfError(ModTimeOrErr.takeError());
    // Note: formatv() only handles the default TimePoint<>, which is in
    // nanoseconds.
    // TODO: fix format_provider<TimePoint<>> to allow other units.
    sys::TimePoint<> ModTimeInNs = ModTimeOrErr.get();
    outs() << ' ' << formatv("{0:%b %e %H:%M %Y}", ModTimeInNs);
    outs() << ' ';
  }

  if (C.getParent()->isThin()) {
    if (!sys::path::is_absolute(Name)) {
      StringRef ParentDir = sys::path::parent_path(ArchiveName);
      if (!ParentDir.empty())
        outs() << sys::path::convert_to_slash(ParentDir) << '/';
    }
    outs() << Name;
  } else {
    outs() << Name;
    if (DisplayMemberOffsets)
      outs() << " 0x" << utohexstr(C.getDataOffset(), true);
  }
  outs() << '\n';
}

static std::string normalizePath(StringRef Path) {
  return CompareFullPath ? sys::path::convert_to_slash(Path)
                         : std::string(sys::path::filename(Path));
}

static bool comparePaths(StringRef Path1, StringRef Path2) {
// When on Windows this function calls CompareStringOrdinal
// as Windows file paths are case-insensitive.
// CompareStringOrdinal compares two Unicode strings for
// binary equivalence and allows for case insensitivity.
#ifdef _WIN32
  SmallVector<wchar_t, 128> WPath1, WPath2;
  failIfError(sys::windows::UTF8ToUTF16(normalizePath(Path1), WPath1));
  failIfError(sys::windows::UTF8ToUTF16(normalizePath(Path2), WPath2));

  return CompareStringOrdinal(WPath1.data(), WPath1.size(), WPath2.data(),
                              WPath2.size(), true) == CSTR_EQUAL;
#else
  return normalizePath(Path1) == normalizePath(Path2);
#endif
}

// Implement the 'x' operation. This function extracts files back to the file
// system.
static void doExtract(StringRef Name, const object::Archive::Child &C) {
  // Retain the original mode.
  Expected<sys::fs::perms> ModeOrErr = C.getAccessMode();
  failIfError(ModeOrErr.takeError());
  sys::fs::perms Mode = ModeOrErr.get();

  StringRef outputFilePath;
  SmallString<128> path;
  if (OutputDir.empty()) {
    outputFilePath = sys::path::filename(Name);
  } else {
    sys::path::append(path, OutputDir, sys::path::filename(Name));
    outputFilePath = path.str();
  }

  if (Verbose)
    outs() << "x - " << outputFilePath << '\n';

  int FD;
  failIfError(sys::fs::openFileForWrite(outputFilePath, FD,
                                        sys::fs::CD_CreateAlways,
                                        sys::fs::OF_None, Mode),
              Name);

  {
    raw_fd_ostream file(FD, false);

    // Get the data and its length
    Expected<StringRef> BufOrErr = C.getBuffer();
    failIfError(BufOrErr.takeError());
    StringRef Data = BufOrErr.get();

    // Write the data.
    file.write(Data.data(), Data.size());
  }

  // If we're supposed to retain the original modification times, etc. do so
  // now.
  if (OriginalDates) {
    auto ModTimeOrErr = C.getLastModified();
    failIfError(ModTimeOrErr.takeError());
    failIfError(
        sys::fs::setLastAccessAndModificationTime(FD, ModTimeOrErr.get()));
  }

  if (close(FD))
    fail("Could not close the file");
}

static bool shouldCreateArchive(ArchiveOperation Op) {
  switch (Op) {
  case Print:
  case Delete:
  case Move:
  case DisplayTable:
  case Extract:
  case CreateSymTab:
    return false;

  case QuickAppend:
  case ReplaceOrInsert:
    return true;
  }

  llvm_unreachable("Missing entry in covered switch.");
}

static bool isValidInBitMode(Binary &Bin) {
  if (BitMode == BitModeTy::Bit32_64 || BitMode == BitModeTy::Any)
    return true;

  if (SymbolicFile *SymFile = dyn_cast<SymbolicFile>(&Bin)) {
    bool Is64Bit = SymFile->is64Bit();
    if ((Is64Bit && (BitMode == BitModeTy::Bit32)) ||
        (!Is64Bit && (BitMode == BitModeTy::Bit64)))
      return false;
  }
  // In AIX "ar", non-object files are always considered to have a valid bit
  // mode.
  return true;
}

Expected<std::unique_ptr<Binary>> getAsBinary(const NewArchiveMember &NM,
                                              LLVMContext *Context) {
  auto BinaryOrErr = createBinary(NM.Buf->getMemBufferRef(), Context);
  if (BinaryOrErr)
    return std::move(*BinaryOrErr);
  return BinaryOrErr.takeError();
}

Expected<std::unique_ptr<Binary>> getAsBinary(const Archive::Child &C,
                                              LLVMContext *Context) {
  return C.getAsBinary(Context);
}

template <class A> static bool isValidInBitMode(const A &Member) {
  if (object::Archive::getDefaultKind() != object::Archive::K_AIXBIG)
    return true;
  LLVMContext Context;
  Expected<std::unique_ptr<Binary>> BinOrErr = getAsBinary(Member, &Context);
  // In AIX "ar", if there is a non-object file member, it is never ignored due
  // to the bit mode setting.
  if (!BinOrErr) {
    consumeError(BinOrErr.takeError());
    return true;
  }
  return isValidInBitMode(*BinOrErr.get());
}

static void warnInvalidObjectForFileMode(Twine Name) {
  warn("'" + Name + "' is not valid with the current object file mode");
}

static void performReadOperation(ArchiveOperation Operation,
                                 object::Archive *OldArchive) {
  if (Operation == Extract && OldArchive->isThin())
    fail("extracting from a thin archive is not supported");

  bool Filter = !Members.empty();
  StringMap<int> MemberCount;
  {
    Error Err = Error::success();
    for (auto &C : OldArchive->children(Err)) {
      Expected<StringRef> NameOrErr = C.getName();
      failIfError(NameOrErr.takeError());
      StringRef Name = NameOrErr.get();

      // Check whether to ignore this object due to its bitness.
      if (!isValidInBitMode(C))
        continue;

      if (Filter) {
        auto I = find_if(Members, [Name](StringRef Path) {
          return comparePaths(Name, Path);
        });
        if (I == Members.end())
          continue;
        if (CountParam && ++MemberCount[Name] != CountParam)
          continue;
        Members.erase(I);
      }

      switch (Operation) {
      default:
        llvm_unreachable("Not a read operation");
      case Print:
        doPrint(Name, C);
        break;
      case DisplayTable:
        doDisplayTable(Name, C);
        break;
      case Extract:
        doExtract(Name, C);
        break;
      }
    }
    failIfError(std::move(Err));
  }

  if (Members.empty())
    return;
  for (StringRef Name : Members)
    WithColor::error(errs(), ToolName) << "'" << Name << "' was not found\n";
  exit(1);
}

static void addChildMember(std::vector<NewArchiveMember> &Members,
                           const object::Archive::Child &M,
                           bool FlattenArchive = false) {
  Expected<NewArchiveMember> NMOrErr =
      NewArchiveMember::getOldMember(M, Deterministic);
  failIfError(NMOrErr.takeError());
  // If the child member we're trying to add is thin, use the path relative to
  // the archive it's in, so the file resolves correctly.
  if (Thin && FlattenArchive) {
    StringSaver Saver(Alloc);
    Expected<std::string> FileNameOrErr(M.getName());
    failIfError(FileNameOrErr.takeError());
    if (sys::path::is_absolute(*FileNameOrErr)) {
      NMOrErr->MemberName = Saver.save(sys::path::convert_to_slash(*FileNameOrErr));
    } else {
      FileNameOrErr = M.getFullName();
      failIfError(FileNameOrErr.takeError());
      Expected<std::string> PathOrErr =
          computeArchiveRelativePath(ArchiveName, *FileNameOrErr);
      NMOrErr->MemberName = Saver.save(
          PathOrErr ? *PathOrErr : sys::path::convert_to_slash(*FileNameOrErr));
    }
  }
  if (FlattenArchive &&
      identify_magic(NMOrErr->Buf->getBuffer()) == file_magic::archive) {
    Expected<std::string> FileNameOrErr = M.getFullName();
    failIfError(FileNameOrErr.takeError());
    object::Archive &Lib = readLibrary(*FileNameOrErr);
    // When creating thin archives, only flatten if the member is also thin.
    if (!Thin || Lib.isThin()) {
      Error Err = Error::success();
      // Only Thin archives are recursively flattened.
      for (auto &Child : Lib.children(Err))
        addChildMember(Members, Child, /*FlattenArchive=*/Thin);
      failIfError(std::move(Err));
      return;
    }
  }
  Members.push_back(std::move(*NMOrErr));
}

static NewArchiveMember getArchiveMember(StringRef FileName) {
  Expected<NewArchiveMember> NMOrErr =
      NewArchiveMember::getFile(FileName, Deterministic);
  failIfError(NMOrErr.takeError(), FileName);
  StringSaver Saver(Alloc);
  // For regular archives, use the basename of the object path for the member
  // name. For thin archives, use the full relative paths so the file resolves
  // correctly.
  if (!Thin) {
    NMOrErr->MemberName = sys::path::filename(NMOrErr->MemberName);
  } else {
    if (sys::path::is_absolute(FileName))
      NMOrErr->MemberName = Saver.save(sys::path::convert_to_slash(FileName));
    else {
      Expected<std::string> PathOrErr =
          computeArchiveRelativePath(ArchiveName, FileName);
      NMOrErr->MemberName = Saver.save(
          PathOrErr ? *PathOrErr : sys::path::convert_to_slash(FileName));
    }
  }
  return std::move(*NMOrErr);
}

static void addMember(std::vector<NewArchiveMember> &Members,
                      NewArchiveMember &NM) {
  Members.push_back(std::move(NM));
}

static void addMember(std::vector<NewArchiveMember> &Members,
                      StringRef FileName, bool FlattenArchive = false) {
  NewArchiveMember NM = getArchiveMember(FileName);
  if (!isValidInBitMode(NM)) {
    warnInvalidObjectForFileMode(FileName);
    return;
  }

  if (FlattenArchive &&
      identify_magic(NM.Buf->getBuffer()) == file_magic::archive) {
    object::Archive &Lib = readLibrary(FileName);
    // When creating thin archives, only flatten if the member is also thin.
    if (!Thin || Lib.isThin()) {
      Error Err = Error::success();
      // Only Thin archives are recursively flattened.
      for (auto &Child : Lib.children(Err))
        addChildMember(Members, Child, /*FlattenArchive=*/Thin);
      failIfError(std::move(Err));
      return;
    }
  }
  Members.push_back(std::move(NM));
}

enum InsertAction {
  IA_AddOldMember,
  IA_AddNewMember,
  IA_Delete,
  IA_MoveOldMember,
  IA_MoveNewMember
};

static InsertAction computeInsertAction(ArchiveOperation Operation,
                                        const object::Archive::Child &Member,
                                        StringRef Name,
                                        std::vector<StringRef>::iterator &Pos,
                                        StringMap<int> &MemberCount) {
  if (!isValidInBitMode(Member))
    return IA_AddOldMember;

  if (Operation == QuickAppend || Members.empty())
    return IA_AddOldMember;

  auto MI = find_if(Members, [Name](StringRef Path) {
    if (Thin && !sys::path::is_absolute(Path)) {
      Expected<std::string> PathOrErr =
          computeArchiveRelativePath(ArchiveName, Path);
      return comparePaths(Name, PathOrErr ? *PathOrErr : Path);
    } else {
      return comparePaths(Name, Path);
    }
  });

  if (MI == Members.end())
    return IA_AddOldMember;

  Pos = MI;

  if (Operation == Delete) {
    if (CountParam && ++MemberCount[Name] != CountParam)
      return IA_AddOldMember;
    return IA_Delete;
  }

  if (Operation == Move)
    return IA_MoveOldMember;

  if (Operation == ReplaceOrInsert) {
    if (!OnlyUpdate) {
      if (RelPos.empty())
        return IA_AddNewMember;
      return IA_MoveNewMember;
    }

    // We could try to optimize this to a fstat, but it is not a common
    // operation.
    sys::fs::file_status Status;
    failIfError(sys::fs::status(*MI, Status), *MI);
    auto ModTimeOrErr = Member.getLastModified();
    failIfError(ModTimeOrErr.takeError());
    if (Status.getLastModificationTime() < ModTimeOrErr.get()) {
      if (RelPos.empty())
        return IA_AddOldMember;
      return IA_MoveOldMember;
    }

    if (RelPos.empty())
      return IA_AddNewMember;
    return IA_MoveNewMember;
  }
  llvm_unreachable("No such operation");
}

// We have to walk this twice and computing it is not trivial, so creating an
// explicit std::vector is actually fairly efficient.
static std::vector<NewArchiveMember>
computeNewArchiveMembers(ArchiveOperation Operation,
                         object::Archive *OldArchive) {
  std::vector<NewArchiveMember> Ret;
  std::vector<NewArchiveMember> Moved;
  int InsertPos = -1;
  if (OldArchive) {
    Error Err = Error::success();
    StringMap<int> MemberCount;
    for (auto &Child : OldArchive->children(Err)) {
      int Pos = Ret.size();
      Expected<StringRef> NameOrErr = Child.getName();
      failIfError(NameOrErr.takeError());
      std::string Name = std::string(NameOrErr.get());
      if (comparePaths(Name, RelPos) && isValidInBitMode(Child)) {
        assert(AddAfter || AddBefore);
        if (AddBefore)
          InsertPos = Pos;
        else
          InsertPos = Pos + 1;
      }

      std::vector<StringRef>::iterator MemberI = Members.end();
      InsertAction Action =
          computeInsertAction(Operation, Child, Name, MemberI, MemberCount);

      auto HandleNewMember = [](auto Member, auto &Members, auto &Child) {
        NewArchiveMember NM = getArchiveMember(*Member);
        if (isValidInBitMode(NM))
          addMember(Members, NM);
        else {
          // If a new member is not a valid object for the bit mode, add
          // the old member back.
          warnInvalidObjectForFileMode(*Member);
          addChildMember(Members, Child, /*FlattenArchive=*/Thin);
        }
      };

      switch (Action) {
      case IA_AddOldMember:
        addChildMember(Ret, Child, /*FlattenArchive=*/Thin);
        break;
      case IA_AddNewMember:
        HandleNewMember(MemberI, Ret, Child);
        break;
      case IA_Delete:
        break;
      case IA_MoveOldMember:
        addChildMember(Moved, Child, /*FlattenArchive=*/Thin);
        break;
      case IA_MoveNewMember:
        HandleNewMember(MemberI, Moved, Child);
        break;
      }
      // When processing elements with the count param, we need to preserve the
      // full members list when iterating over all archive members. For
      // instance, "llvm-ar dN 2 archive.a member.o" should delete the second
      // file named member.o it sees; we are not done with member.o the first
      // time we see it in the archive.
      if (MemberI != Members.end() && !CountParam)
        Members.erase(MemberI);
    }
    failIfError(std::move(Err));
  }

  if (Operation == Delete)
    return Ret;

  if (!RelPos.empty() && InsertPos == -1)
    fail("insertion point not found");

  if (RelPos.empty())
    InsertPos = Ret.size();

  assert(unsigned(InsertPos) <= Ret.size());
  int Pos = InsertPos;
  for (auto &M : Moved) {
    Ret.insert(Ret.begin() + Pos, std::move(M));
    ++Pos;
  }

  if (AddLibrary) {
    assert(Operation == QuickAppend);
    for (auto &Member : Members)
      addMember(Ret, Member, /*FlattenArchive=*/true);
    return Ret;
  }

  std::vector<NewArchiveMember> NewMembers;
  for (auto &Member : Members)
    addMember(NewMembers, Member, /*FlattenArchive=*/Thin);
  Ret.reserve(Ret.size() + NewMembers.size());
  std::move(NewMembers.begin(), NewMembers.end(),
            std::inserter(Ret, std::next(Ret.begin(), InsertPos)));

  return Ret;
}

static void performWriteOperation(ArchiveOperation Operation,
                                  object::Archive *OldArchive,
                                  std::unique_ptr<MemoryBuffer> OldArchiveBuf,
                                  std::vector<NewArchiveMember> *NewMembersP) {
  if (OldArchive) {
    if (Thin && !OldArchive->isThin())
      fail("cannot convert a regular archive to a thin one");

    if (OldArchive->isThin())
      Thin = true;
  }

  std::vector<NewArchiveMember> NewMembers;
  if (!NewMembersP)
    NewMembers = computeNewArchiveMembers(Operation, OldArchive);

  object::Archive::Kind Kind;
  switch (FormatType) {
  case Default:
    if (Thin)
      Kind = object::Archive::K_GNU;
    else if (OldArchive) {
      Kind = OldArchive->kind();
      std::optional<object::Archive::Kind> AltKind;
      if (Kind == object::Archive::K_BSD)
        AltKind = object::Archive::K_DARWIN;
      else if (Kind == object::Archive::K_GNU && !OldArchive->hasSymbolTable())
        // If there is no symbol table, we can't tell GNU from COFF format
        // from the old archive type.
        AltKind = object::Archive::K_COFF;
      if (AltKind) {
        auto InferredKind = Kind;
        if (NewMembersP && !NewMembersP->empty())
          InferredKind = NewMembersP->front().detectKindFromObject();
        else if (!NewMembers.empty())
          InferredKind = NewMembers.front().detectKindFromObject();
        if (InferredKind == AltKind)
          Kind = *AltKind;
      }
    } else if (NewMembersP)
      Kind = !NewMembersP->empty() ? NewMembersP->front().detectKindFromObject()
                                   : object::Archive::getDefaultKind();
    else
      Kind = !NewMembers.empty() ? NewMembers.front().detectKindFromObject()
                                 : object::Archive::getDefaultKind();
    break;
  case GNU:
    Kind = object::Archive::K_GNU;
    break;
  case COFF:
    Kind = object::Archive::K_COFF;
    break;
  case BSD:
    if (Thin)
      fail("only the gnu format has a thin mode");
    Kind = object::Archive::K_BSD;
    break;
  case DARWIN:
    if (Thin)
      fail("only the gnu format has a thin mode");
    Kind = object::Archive::K_DARWIN;
    break;
  case BIGARCHIVE:
    if (Thin)
      fail("only the gnu format has a thin mode");
    Kind = object::Archive::K_AIXBIG;
    break;
  case Unknown:
    llvm_unreachable("");
  }

  Error E =
      writeArchive(ArchiveName, NewMembersP ? *NewMembersP : NewMembers, Symtab,
                   Kind, Deterministic, Thin, std::move(OldArchiveBuf));
  failIfError(std::move(E), ArchiveName);
}

static void createSymbolTable(object::Archive *OldArchive) {
  // When an archive is created or modified, if the s option is given, the
  // resulting archive will have a current symbol table. If the S option
  // is given, it will have no symbol table.
  // In summary, we only need to update the symbol table if we have none.
  // This is actually very common because of broken build systems that think
  // they have to run ranlib.
  if (OldArchive->hasSymbolTable()) {
    if (OldArchive->kind() != object::Archive::K_AIXBIG)
      return;

    // For archives in the Big Archive format, the bit mode option specifies
    // which symbol table to generate. The presence of a symbol table that does
    // not match the specified bit mode does not prevent creation of the symbol
    // table that has been requested.
    if (OldArchive->kind() == object::Archive::K_AIXBIG) {
      BigArchive *BigArc = dyn_cast<BigArchive>(OldArchive);
      if (BigArc->has32BitGlobalSymtab() &&
          Symtab == SymtabWritingMode::BigArchive32)
        return;

      if (BigArc->has64BitGlobalSymtab() &&
          Symtab == SymtabWritingMode::BigArchive64)
        return;

      if (BigArc->has32BitGlobalSymtab() && BigArc->has64BitGlobalSymtab() &&
          Symtab == SymtabWritingMode::NormalSymtab)
        return;

      Symtab = SymtabWritingMode::NormalSymtab;
    }
  }
  if (OldArchive->isThin())
    Thin = true;
  performWriteOperation(CreateSymTab, OldArchive, nullptr, nullptr);
}

static void performOperation(ArchiveOperation Operation,
                             object::Archive *OldArchive,
                             std::unique_ptr<MemoryBuffer> OldArchiveBuf,
                             std::vector<NewArchiveMember> *NewMembers) {
  switch (Operation) {
  case Print:
  case DisplayTable:
  case Extract:
    performReadOperation(Operation, OldArchive);
    return;

  case Delete:
  case Move:
  case QuickAppend:
  case ReplaceOrInsert:
    performWriteOperation(Operation, OldArchive, std::move(OldArchiveBuf),
                          NewMembers);
    return;
  case CreateSymTab:
    createSymbolTable(OldArchive);
    return;
  }
  llvm_unreachable("Unknown operation.");
}

static int performOperation(ArchiveOperation Operation) {
  // Create or open the archive object.
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buf = MemoryBuffer::getFile(
      ArchiveName, /*IsText=*/false, /*RequiresNullTerminator=*/false);
  std::error_code EC = Buf.getError();
  if (EC && EC != errc::no_such_file_or_directory)
    fail("unable to open '" + ArchiveName + "': " + EC.message());

  if (!EC) {
    Expected<std::unique_ptr<object::Archive>> ArchiveOrError =
        object::Archive::create(Buf.get()->getMemBufferRef());
    if (!ArchiveOrError)
      failIfError(ArchiveOrError.takeError(),
                  "unable to load '" + ArchiveName + "'");

    std::unique_ptr<object::Archive> Archive = std::move(ArchiveOrError.get());
    if (Archive->isThin())
      CompareFullPath = true;
    performOperation(Operation, Archive.get(), std::move(Buf.get()),
                     /*NewMembers=*/nullptr);
    return 0;
  }

  assert(EC == errc::no_such_file_or_directory);

  if (!shouldCreateArchive(Operation)) {
    failIfError(EC, Twine("unable to load '") + ArchiveName + "'");
  } else {
    if (!Create) {
      // Produce a warning if we should and we're creating the archive
      warn("creating " + ArchiveName);
    }
  }

  performOperation(Operation, nullptr, nullptr, /*NewMembers=*/nullptr);
  return 0;
}

static void runMRIScript() {
  enum class MRICommand { AddLib, AddMod, Create, CreateThin, Delete, Save, End, Invalid };

  ErrorOr<std::unique_ptr<MemoryBuffer>> Buf = MemoryBuffer::getSTDIN();
  failIfError(Buf.getError());
  const MemoryBuffer &Ref = *Buf.get();
  bool Saved = false;
  std::vector<NewArchiveMember> NewMembers;
  ParsingMRIScript = true;

  for (line_iterator I(Ref, /*SkipBlanks*/ false), E; I != E; ++I) {
    ++MRILineNumber;
    StringRef Line = *I;
    Line = Line.split(';').first;
    Line = Line.split('*').first;
    Line = Line.trim();
    if (Line.empty())
      continue;
    StringRef CommandStr, Rest;
    std::tie(CommandStr, Rest) = Line.split(' ');
    Rest = Rest.trim();
    if (!Rest.empty() && Rest.front() == '"' && Rest.back() == '"')
      Rest = Rest.drop_front().drop_back();
    auto Command = StringSwitch<MRICommand>(CommandStr.lower())
                       .Case("addlib", MRICommand::AddLib)
                       .Case("addmod", MRICommand::AddMod)
                       .Case("create", MRICommand::Create)
                       .Case("createthin", MRICommand::CreateThin)
                       .Case("delete", MRICommand::Delete)
                       .Case("save", MRICommand::Save)
                       .Case("end", MRICommand::End)
                       .Default(MRICommand::Invalid);

    switch (Command) {
    case MRICommand::AddLib: {
      if (!Create)
        fail("no output archive has been opened");
      object::Archive &Lib = readLibrary(Rest);
      {
        if (Thin && !Lib.isThin())
          fail("cannot add a regular archive's contents to a thin archive");
        Error Err = Error::success();
        for (auto &Member : Lib.children(Err))
          addChildMember(NewMembers, Member, /*FlattenArchive=*/Thin);
        failIfError(std::move(Err));
      }
      break;
    }
    case MRICommand::AddMod:
      if (!Create)
        fail("no output archive has been opened");
      addMember(NewMembers, Rest);
      break;
    case MRICommand::CreateThin:
      Thin = true;
      [[fallthrough]];
    case MRICommand::Create:
      Create = true;
      if (!ArchiveName.empty())
        fail("editing multiple archives not supported");
      if (Saved)
        fail("file already saved");
      ArchiveName = std::string(Rest);
      if (ArchiveName.empty())
        fail("missing archive name");
      break;
    case MRICommand::Delete: {
      llvm::erase_if(NewMembers, [=](NewArchiveMember &M) {
        return comparePaths(M.MemberName, Rest);
      });
      break;
    }
    case MRICommand::Save:
      Saved = true;
      break;
    case MRICommand::End:
      break;
    case MRICommand::Invalid:
      fail("unknown command: " + CommandStr);
    }
  }

  ParsingMRIScript = false;

  // Nothing to do if not saved.
  if (Saved)
    performOperation(ReplaceOrInsert, /*OldArchive=*/nullptr,
                     /*OldArchiveBuf=*/nullptr, &NewMembers);
  exit(0);
}

static bool handleGenericOption(StringRef arg) {
  if (arg == "--help" || arg == "-h") {
    printHelpMessage();
    return true;
  }
  if (arg == "--version") {
    cl::PrintVersionMessage();
    return true;
  }
  return false;
}

static BitModeTy getBitMode(const char *RawBitMode) {
  return StringSwitch<BitModeTy>(RawBitMode)
      .Case("32", BitModeTy::Bit32)
      .Case("64", BitModeTy::Bit64)
      .Case("32_64", BitModeTy::Bit32_64)
      .Case("any", BitModeTy::Any)
      .Default(BitModeTy::Unknown);
}

static const char *matchFlagWithArg(StringRef Expected,
                                    ArrayRef<const char *>::iterator &ArgIt,
                                    ArrayRef<const char *> Args) {
  StringRef Arg = *ArgIt;

  Arg.consume_front("--");

  size_t len = Expected.size();
  if (Arg == Expected) {
    if (++ArgIt == Args.end())
      fail(std::string(Expected) + " requires an argument");

    return *ArgIt;
  }
  if (Arg.starts_with(Expected) && Arg.size() > len && Arg[len] == '=')
    return Arg.data() + len + 1;

  return nullptr;
}

static cl::TokenizerCallback getRspQuoting(ArrayRef<const char *> ArgsArr) {
  cl::TokenizerCallback Ret =
      Triple(sys::getProcessTriple()).getOS() == Triple::Win32
          ? cl::TokenizeWindowsCommandLine
          : cl::TokenizeGNUCommandLine;

  for (ArrayRef<const char *>::iterator ArgIt = ArgsArr.begin();
       ArgIt != ArgsArr.end(); ++ArgIt) {
    if (const char *Match = matchFlagWithArg("rsp-quoting", ArgIt, ArgsArr)) {
      StringRef MatchRef = Match;
      if (MatchRef == "posix")
        Ret = cl::TokenizeGNUCommandLine;
      else if (MatchRef == "windows")
        Ret = cl::TokenizeWindowsCommandLine;
      else
        fail(std::string("Invalid response file quoting style ") + Match);
    }
  }

  return Ret;
}

static int ar_main(int argc, char **argv) {
  SmallVector<const char *, 0> Argv(argv + 1, argv + argc);
  StringSaver Saver(Alloc);

  cl::ExpandResponseFiles(Saver, getRspQuoting(ArrayRef(argv, argc)), Argv);

  // Get BitMode from enviorment variable "OBJECT_MODE" for AIX OS, if
  // specified.
  if (object::Archive::getDefaultKind() == object::Archive::K_AIXBIG) {
    BitMode = getBitMode(getenv("OBJECT_MODE"));
    if (BitMode == BitModeTy::Unknown)
      BitMode = BitModeTy::Bit32;
  }

  for (ArrayRef<const char *>::iterator ArgIt = Argv.begin();
       ArgIt != Argv.end(); ++ArgIt) {
    const char *Match = nullptr;

    if (handleGenericOption(*ArgIt))
      return 0;
    if (strcmp(*ArgIt, "--") == 0) {
      ++ArgIt;
      for (; ArgIt != Argv.end(); ++ArgIt)
        PositionalArgs.push_back(*ArgIt);
      break;
    }

    if (*ArgIt[0] != '-') {
      if (Options.empty())
        Options += *ArgIt;
      else
        PositionalArgs.push_back(*ArgIt);
      continue;
    }

    if (strcmp(*ArgIt, "-M") == 0) {
      MRI = true;
      continue;
    }

    if (strcmp(*ArgIt, "--thin") == 0) {
      Thin = true;
      continue;
    }

    Match = matchFlagWithArg("format", ArgIt, Argv);
    if (Match) {
      FormatType = StringSwitch<Format>(Match)
                       .Case("default", Default)
                       .Case("gnu", GNU)
                       .Case("darwin", DARWIN)
                       .Case("bsd", BSD)
                       .Case("bigarchive", BIGARCHIVE)
                       .Case("coff", COFF)
                       .Default(Unknown);
      if (FormatType == Unknown)
        fail(std::string("Invalid format ") + Match);
      continue;
    }

    if ((Match = matchFlagWithArg("output", ArgIt, Argv))) {
      OutputDir = Match;
      continue;
    }

    if (matchFlagWithArg("plugin", ArgIt, Argv) ||
        matchFlagWithArg("rsp-quoting", ArgIt, Argv))
      continue;

    if (strncmp(*ArgIt, "-X", 2) == 0) {
      if (object::Archive::getDefaultKind() == object::Archive::K_AIXBIG) {
        Match = *(*ArgIt + 2) != '\0' ? *ArgIt + 2 : *(++ArgIt);
        BitMode = getBitMode(Match);
        if (BitMode == BitModeTy::Unknown)
          fail(Twine("invalid bit mode: ") + Match);
        continue;
      } else {
        fail(Twine(*ArgIt) + " option not supported on non AIX OS");
      }
    }

    Options += *ArgIt + 1;
  }

  return performOperation(parseCommandLine());
}

static int ranlib_main(int argc, char **argv) {
  std::vector<StringRef> Archives;
  bool HasAIXXOption = false;

  for (int i = 1; i < argc; ++i) {
    StringRef arg(argv[i]);
    if (handleGenericOption(arg)) {
      return 0;
    } else if (arg.consume_front("-")) {
      // Handle the -D/-U flag
      while (!arg.empty()) {
        if (arg.front() == 'D') {
          Deterministic = true;
        } else if (arg.front() == 'U') {
          Deterministic = false;
        } else if (arg.front() == 'h') {
          printHelpMessage();
          return 0;
        } else if (arg.front() == 'V') {
          cl::PrintVersionMessage();
          return 0;
        } else if (arg.front() == 'X') {
          if (object::Archive::getDefaultKind() == object::Archive::K_AIXBIG) {
            HasAIXXOption = true;
            arg.consume_front("X");
            const char *Xarg = arg.data();
            if (Xarg[0] == '\0') {
              if (argv[i + 1][0] != '-')
                BitMode = getBitMode(argv[++i]);
              else
                BitMode = BitModeTy::Unknown;
            } else
              BitMode = getBitMode(arg.data());

            if (BitMode == BitModeTy::Unknown)
              fail("the specified object mode is not valid. Specify -X32, "
                   "-X64, -X32_64, or -Xany");
          } else {
            fail(Twine("-") + Twine(arg) +
                 " option not supported on non AIX OS");
          }
          break;
        } else if (arg.front() == 't') {
          // GNU ranlib also supports a -t flag, but does nothing
          // because it just returns true without touching the
          // timestamp, so simulate the same behaviour.
          return 0;
        } else {
          fail("Invalid option: '-" + arg + "'");
        }
        arg = arg.drop_front(1);
      }
    } else {
      Archives.push_back(arg);
    }
  }

  if (object::Archive::getDefaultKind() == object::Archive::K_AIXBIG) {
    // If not specify -X option, get BitMode from enviorment variable
    // "OBJECT_MODE" for AIX OS if specify.
    if (!HasAIXXOption) {
      if (char *EnvObjectMode = getenv("OBJECT_MODE")) {
        BitMode = getBitMode(EnvObjectMode);
        if (BitMode == BitModeTy::Unknown)
          fail("the OBJECT_MODE environment variable has an invalid value. "
               "OBJECT_MODE must be 32, 64, 32_64, or any");
      }
    }

    switch (BitMode) {
    case BitModeTy::Bit32:
      Symtab = SymtabWritingMode::BigArchive32;
      break;
    case BitModeTy::Bit64:
      Symtab = SymtabWritingMode::BigArchive64;
      break;
    default:
      Symtab = SymtabWritingMode::NormalSymtab;
      break;
    }
  }

  for (StringRef Archive : Archives) {
    ArchiveName = Archive.str();
    performOperation(CreateSymTab);
  }
  if (Archives.empty())
    badUsage("an archive name must be specified");
  return 0;
}

int llvm_ar_main(int argc, char **argv, const llvm::ToolContext &) {
  ToolName = argv[0];

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();

  Stem = sys::path::stem(ToolName);
  auto Is = [](StringRef Tool) {
    // We need to recognize the following filenames.
    //
    // Lib.exe -> lib (see D44808, MSBuild runs Lib.exe)
    // dlltool.exe -> dlltool
    // arm-pokymllib32-linux-gnueabi-llvm-ar-10 -> ar
    auto I = Stem.rfind_insensitive(Tool);
    return I != StringRef::npos &&
           (I + Tool.size() == Stem.size() || !isAlnum(Stem[I + Tool.size()]));
  };

  if (Is("dlltool"))
    return dlltoolDriverMain(ArrayRef(argv, argc));
  if (Is("ranlib"))
    return ranlib_main(argc, argv);
  if (Is("lib"))
    return libDriverMain(ArrayRef(argv, argc));
  if (Is("ar"))
    return ar_main(argc, argv);

  fail("not ranlib, ar, lib or dlltool");
}
