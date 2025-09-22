//===-- llvm-libtool-darwin.cpp - a tool for creating libraries -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A utility for creating static and dynamic libraries for Darwin.
//
//===----------------------------------------------------------------------===//

#include "DependencyInfo.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/MachOUniversalWriter.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TextAPI/Architecture.h"
#include <cstdlib>
#include <map>
#include <type_traits>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::opt;

// Command-line option boilerplate.
namespace {
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Opts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class LibtoolDarwinOptTable : public opt::GenericOptTable {
public:
  LibtoolDarwinOptTable() : GenericOptTable(InfoTable) {}
};
} // end anonymous namespace

class NewArchiveMemberList;
typedef std::map<uint64_t, NewArchiveMemberList> MembersPerArchitectureMap;

static std::string OutputFile;
static std::vector<std::string> InputFiles;
static std::optional<std::string> ArchType;

enum class Operation { None, Static };
static Operation LibraryOperation = Operation::None;

static bool DeterministicOption;
static bool NonDeterministicOption;
static std::string FileList;
static std::vector<std::string> Libraries;
static std::vector<std::string> LibrarySearchDirs;
static std::string DependencyInfoPath;
static bool VersionOption;
static bool NoWarningForNoSymbols;
static bool WarningsAsErrors;
static std::string IgnoredSyslibRoot;

static const std::array<std::string, 3> StandardSearchDirs{
    "/lib",
    "/usr/lib",
    "/usr/local/lib",
};

std::unique_ptr<DependencyInfo> GlobalDependencyInfo;

struct Config {
  bool Deterministic = true; // Updated by 'D' and 'U' modifiers.
  uint32_t ArchCPUType;
  uint32_t ArchCPUSubtype;
};

static Expected<std::string> searchForFile(const Twine &FileName) {
  auto FindLib =
      [FileName](
          ArrayRef<std::string> SearchDirs) -> std::optional<std::string> {
    for (StringRef Dir : SearchDirs) {
      SmallString<128> Path;
      sys::path::append(Path, Dir, FileName);

      if (sys::fs::exists(Path))
        return std::string(Path);

      GlobalDependencyInfo->addMissingInput(Path);
    }
    return std::nullopt;
  };

  std::optional<std::string> Found = FindLib(LibrarySearchDirs);
  if (!Found)
    Found = FindLib(StandardSearchDirs);
  if (Found)
    return *Found;

  return createStringError(std::errc::invalid_argument,
                           "cannot locate file '%s'", FileName.str().c_str());
}

static Error processCommandLineLibraries() {
  for (StringRef BaseName : Libraries) {
    Expected<std::string> FullPath = searchForFile(
        BaseName.ends_with(".o") ? BaseName.str() : "lib" + BaseName + ".a");
    if (!FullPath)
      return FullPath.takeError();
    InputFiles.push_back(FullPath.get());
  }

  return Error::success();
}

static Error processFileList() {
  StringRef FileName, DirName;
  std::tie(FileName, DirName) = StringRef(FileList).rsplit(",");

  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(FileName, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);
  if (std::error_code EC = FileOrErr.getError())
    return createFileError(FileName, errorCodeToError(EC));
  const MemoryBuffer &Ref = *FileOrErr.get();

  line_iterator I(Ref, /*SkipBlanks=*/false);
  if (I.is_at_eof())
    return createStringError(std::errc::invalid_argument,
                             "file list file: '%s' is empty",
                             FileName.str().c_str());
  for (; !I.is_at_eof(); ++I) {
    StringRef Line = *I;
    if (Line.empty())
      return createStringError(std::errc::invalid_argument,
                               "file list file: '%s': filename cannot be empty",
                               FileName.str().c_str());

    SmallString<128> Path;
    if (!DirName.empty())
      sys::path::append(Path, DirName, Line);
    else
      sys::path::append(Path, Line);
    InputFiles.push_back(static_cast<std::string>(Path));
  }
  return Error::success();
}

static Error validateArchitectureName(StringRef ArchitectureName) {
  if (!MachOObjectFile::isValidArch(ArchitectureName)) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    for (StringRef Arch : MachOObjectFile::getValidArchs())
      OS << Arch << " ";

    return createStringError(
        std::errc::invalid_argument,
        "invalid architecture '%s': valid architecture names are %s",
        ArchitectureName.str().c_str(), OS.str().c_str());
  }
  return Error::success();
}

static uint64_t getCPUID(uint32_t CPUType, uint32_t CPUSubtype) {
  switch (CPUType) {
  case MachO::CPU_TYPE_ARM:
  case MachO::CPU_TYPE_ARM64:
  case MachO::CPU_TYPE_ARM64_32:
  case MachO::CPU_TYPE_X86_64:
    // We consider CPUSubtype only for the above 4 CPUTypes to match cctools'
    // libtool behavior.
    return static_cast<uint64_t>(CPUType) << 32 | CPUSubtype;
  default:
    return CPUType;
  }
}

// MembersData is an organized collection of members.
struct MembersData {
  // MembersPerArchitectureMap is a mapping from CPU architecture to a list of
  // members.
  MembersPerArchitectureMap MembersPerArchitecture;
  std::vector<std::unique_ptr<MemoryBuffer>> FileBuffers;
};

// NewArchiveMemberList instances serve as collections of archive members and
// information about those members.
class NewArchiveMemberList {
  std::vector<NewArchiveMember> Members;
  // This vector contains the file that each NewArchiveMember from Members came
  // from. Therefore, it has the same size as Members.
  std::vector<StringRef> Files;

public:
  // Add a NewArchiveMember and the file it came from to the list.
  void push_back(NewArchiveMember &&Member, StringRef File) {
    Members.push_back(std::move(Member));
    Files.push_back(File);
  }

  ArrayRef<NewArchiveMember> getMembers() const { return Members; }

  ArrayRef<StringRef> getFiles() const { return Files; }

  static_assert(
      std::is_same<decltype(MembersData::MembersPerArchitecture)::mapped_type,
                   NewArchiveMemberList>(),
      "This test makes sure NewArchiveMemberList is used by MembersData since "
      "the following asserts test invariants required for MembersData.");
  static_assert(
      !std::is_copy_constructible_v<
          decltype(NewArchiveMemberList::Members)::value_type>,
      "MembersData::MembersPerArchitecture has a dependency on "
      "MembersData::FileBuffers so it should not be able to "
      "be copied on its own without FileBuffers. Unfortunately, "
      "is_copy_constructible does not detect whether the container (ie vector) "
      "of a non-copyable type is itself non-copyable so we have to test the "
      "actual type of the stored data (ie, value_type).");
  static_assert(
      !std::is_copy_assignable_v<
          decltype(NewArchiveMemberList::Members)::value_type>,
      "MembersData::MembersPerArchitecture has a dependency on "
      "MembersData::FileBuffers so it should not be able to "
      "be copied on its own without FileBuffers. Unfortunately, "
      "is_copy_constructible does not detect whether the container (ie vector) "
      "of a non-copyable type is itself non-copyable so we have to test the "
      "actual type of the stored data (ie, value_type).");
};

// MembersBuilder collects and organizes all members from the files provided by
// the user.
class MembersBuilder {
public:
  MembersBuilder(LLVMContext &LLVMCtx, const Config &C)
      : LLVMCtx(LLVMCtx), C(C) {}

  Expected<MembersData> build() {
    for (StringRef FileName : InputFiles)
      if (Error E = AddMember(*this, FileName)())
        return std::move(E);

    std::string Arch = ArchType.value_or("");
    if (!Arch.empty()) {
      uint64_t ArchCPUID = getCPUID(C.ArchCPUType, C.ArchCPUSubtype);
      if (Data.MembersPerArchitecture.find(ArchCPUID) ==
          Data.MembersPerArchitecture.end())
        return createStringError(std::errc::invalid_argument,
                                 "no library created (no object files in input "
                                 "files matching -arch_only %s)",
                                 Arch.c_str());
    }
    return std::move(Data);
  }

private:
  class AddMember {
    MembersBuilder &Builder;
    StringRef FileName;

  public:
    AddMember(MembersBuilder &Builder, StringRef FileName)
        : Builder(Builder), FileName(FileName) {}

    Error operator()() {
      Expected<NewArchiveMember> NewMemberOrErr =
          NewArchiveMember::getFile(FileName, Builder.C.Deterministic);
      if (!NewMemberOrErr)
        return createFileError(FileName, NewMemberOrErr.takeError());
      auto &NewMember = *NewMemberOrErr;

      // For regular archives, use the basename of the object path for the
      // member name.
      NewMember.MemberName = sys::path::filename(NewMember.MemberName);
      file_magic Magic = identify_magic(NewMember.Buf->getBuffer());

      // Flatten archives.
      if (Magic == file_magic::archive)
        return addArchiveMembers(std::move(NewMember));

      // Flatten universal files.
      if (Magic == file_magic::macho_universal_binary)
        return addUniversalMembers(std::move(NewMember));

      // Bitcode files.
      if (Magic == file_magic::bitcode)
        return verifyAndAddIRObject(std::move(NewMember));

      return verifyAndAddMachOObject(std::move(NewMember));
    }

  private:
    // Check that a file's architecture [FileCPUType, FileCPUSubtype]
    // matches the architecture specified under -arch_only flag.
    bool acceptFileArch(uint32_t FileCPUType, uint32_t FileCPUSubtype) {
      if (Builder.C.ArchCPUType != FileCPUType)
        return false;

      switch (Builder.C.ArchCPUType) {
      case MachO::CPU_TYPE_ARM:
      case MachO::CPU_TYPE_ARM64_32:
      case MachO::CPU_TYPE_X86_64:
        return Builder.C.ArchCPUSubtype == FileCPUSubtype;

      case MachO::CPU_TYPE_ARM64:
        if (Builder.C.ArchCPUSubtype == MachO::CPU_SUBTYPE_ARM64_ALL)
          return FileCPUSubtype == MachO::CPU_SUBTYPE_ARM64_ALL ||
                 FileCPUSubtype == MachO::CPU_SUBTYPE_ARM64_V8;
        else
          return Builder.C.ArchCPUSubtype == FileCPUSubtype;

      default:
        return true;
      }
    }

    Error verifyAndAddMachOObject(NewArchiveMember Member) {
      auto MBRef = Member.Buf->getMemBufferRef();
      Expected<std::unique_ptr<object::ObjectFile>> ObjOrErr =
          object::ObjectFile::createObjectFile(MBRef);

      // Throw error if not a valid object file.
      if (!ObjOrErr)
        return createFileError(Member.MemberName, ObjOrErr.takeError());

      // Throw error if not in Mach-O format.
      if (!isa<object::MachOObjectFile>(**ObjOrErr))
        return createStringError(std::errc::invalid_argument,
                                 "'%s': format not supported",
                                 Member.MemberName.data());

      auto *O = cast<MachOObjectFile>(ObjOrErr->get());
      uint32_t FileCPUType, FileCPUSubtype;
      std::tie(FileCPUType, FileCPUSubtype) = MachO::getCPUTypeFromArchitecture(
          MachO::getArchitectureFromName(O->getArchTriple().getArchName()));

      // If -arch_only is specified then skip this file if it doesn't match
      // the architecture specified.
      if (ArchType && !acceptFileArch(FileCPUType, FileCPUSubtype)) {
        return Error::success();
      }

      if (!NoWarningForNoSymbols && O->symbols().empty()) {
        Error E = createFileError(
            Member.MemberName,
            createStringError(std::errc::invalid_argument,
                              "has no symbols for architecture %s",
                              O->getArchTriple().getArchName().str().c_str()));

        if (WarningsAsErrors)
          return E;
        WithColor::defaultWarningHandler(std::move(E));
      }

      uint64_t FileCPUID = getCPUID(FileCPUType, FileCPUSubtype);
      Builder.Data.MembersPerArchitecture[FileCPUID].push_back(
          std::move(Member), FileName);
      return Error::success();
    }

    Error verifyAndAddIRObject(NewArchiveMember Member) {
      auto MBRef = Member.Buf->getMemBufferRef();
      Expected<std::unique_ptr<object::IRObjectFile>> IROrErr =
          object::IRObjectFile::create(MBRef, Builder.LLVMCtx);

      // Throw error if not a valid IR object file.
      if (!IROrErr)
        return createFileError(Member.MemberName, IROrErr.takeError());

      Triple TT = Triple(IROrErr->get()->getTargetTriple());

      Expected<uint32_t> FileCPUTypeOrErr = MachO::getCPUType(TT);
      if (!FileCPUTypeOrErr)
        return FileCPUTypeOrErr.takeError();

      Expected<uint32_t> FileCPUSubTypeOrErr = MachO::getCPUSubType(TT);
      if (!FileCPUSubTypeOrErr)
        return FileCPUSubTypeOrErr.takeError();

      // If -arch_only is specified then skip this file if it doesn't match
      // the architecture specified.
      if (ArchType &&
          !acceptFileArch(*FileCPUTypeOrErr, *FileCPUSubTypeOrErr)) {
        return Error::success();
      }

      uint64_t FileCPUID = getCPUID(*FileCPUTypeOrErr, *FileCPUSubTypeOrErr);
      Builder.Data.MembersPerArchitecture[FileCPUID].push_back(
          std::move(Member), FileName);
      return Error::success();
    }

    Error addChildMember(const object::Archive::Child &M) {
      Expected<NewArchiveMember> NewMemberOrErr =
          NewArchiveMember::getOldMember(M, Builder.C.Deterministic);
      if (!NewMemberOrErr)
        return NewMemberOrErr.takeError();
      auto &NewMember = *NewMemberOrErr;

      file_magic Magic = identify_magic(NewMember.Buf->getBuffer());

      if (Magic == file_magic::bitcode)
        return verifyAndAddIRObject(std::move(NewMember));

      return verifyAndAddMachOObject(std::move(NewMember));
    }

    Error processArchive(object::Archive &Lib) {
      Error Err = Error::success();
      for (const object::Archive::Child &Child : Lib.children(Err))
        if (Error E = addChildMember(Child))
          return createFileError(FileName, std::move(E));
      if (Err)
        return createFileError(FileName, std::move(Err));

      return Error::success();
    }

    Error addArchiveMembers(NewArchiveMember NewMember) {
      Expected<std::unique_ptr<Archive>> LibOrErr =
          object::Archive::create(NewMember.Buf->getMemBufferRef());
      if (!LibOrErr)
        return createFileError(FileName, LibOrErr.takeError());

      if (Error E = processArchive(**LibOrErr))
        return E;

      // Update vector FileBuffers with the MemoryBuffers to transfer
      // ownership.
      Builder.Data.FileBuffers.push_back(std::move(NewMember.Buf));
      return Error::success();
    }

    Error addUniversalMembers(NewArchiveMember NewMember) {
      Expected<std::unique_ptr<MachOUniversalBinary>> BinaryOrErr =
          MachOUniversalBinary::create(NewMember.Buf->getMemBufferRef());
      if (!BinaryOrErr)
        return createFileError(FileName, BinaryOrErr.takeError());

      auto *UO = BinaryOrErr->get();
      for (const MachOUniversalBinary::ObjectForArch &O : UO->objects()) {

        Expected<std::unique_ptr<MachOObjectFile>> MachOObjOrErr =
            O.getAsObjectFile();
        if (MachOObjOrErr) {
          NewArchiveMember NewMember =
              NewArchiveMember(MachOObjOrErr->get()->getMemoryBufferRef());
          NewMember.MemberName = sys::path::filename(NewMember.MemberName);

          if (Error E = verifyAndAddMachOObject(std::move(NewMember)))
            return E;
          continue;
        }

        Expected<std::unique_ptr<IRObjectFile>> IRObjectOrError =
            O.getAsIRObject(Builder.LLVMCtx);
        if (IRObjectOrError) {
          // A universal file member can be a MachOObjectFile, an IRObject or an
          // Archive. In case we can successfully cast the member as an
          // IRObject, it is safe to throw away the error generated due to
          // casting the object as a MachOObjectFile.
          consumeError(MachOObjOrErr.takeError());

          NewArchiveMember NewMember =
              NewArchiveMember(IRObjectOrError->get()->getMemoryBufferRef());
          NewMember.MemberName = sys::path::filename(NewMember.MemberName);

          if (Error E = verifyAndAddIRObject(std::move(NewMember)))
            return E;
          continue;
        }

        Expected<std::unique_ptr<Archive>> ArchiveOrError = O.getAsArchive();
        if (ArchiveOrError) {
          // A universal file member can be a MachOObjectFile, an IRObject or an
          // Archive. In case we can successfully cast the member as an Archive,
          // it is safe to throw away the error generated due to casting the
          // object as a MachOObjectFile.
          consumeError(MachOObjOrErr.takeError());
          consumeError(IRObjectOrError.takeError());

          if (Error E = processArchive(**ArchiveOrError))
            return E;
          continue;
        }

        Error CombinedError = joinErrors(
            ArchiveOrError.takeError(),
            joinErrors(IRObjectOrError.takeError(), MachOObjOrErr.takeError()));
        return createFileError(FileName, std::move(CombinedError));
      }

      // Update vector FileBuffers with the MemoryBuffers to transfer
      // ownership.
      Builder.Data.FileBuffers.push_back(std::move(NewMember.Buf));
      return Error::success();
    }
  };

  MembersData Data;
  LLVMContext &LLVMCtx;
  const Config &C;
};

static Expected<SmallVector<Slice, 2>>
buildSlices(LLVMContext &LLVMCtx,
            ArrayRef<OwningBinary<Archive>> OutputBinaries) {
  SmallVector<Slice, 2> Slices;

  for (const auto &OB : OutputBinaries) {
    const Archive &A = *OB.getBinary();
    Expected<Slice> ArchiveSlice = Slice::create(A, &LLVMCtx);
    if (!ArchiveSlice)
      return ArchiveSlice.takeError();
    Slices.push_back(*ArchiveSlice);
  }
  return Slices;
}

static Error
checkForDuplicates(const MembersPerArchitectureMap &MembersPerArch) {
  for (const auto &M : MembersPerArch) {
    ArrayRef<NewArchiveMember> Members = M.second.getMembers();
    ArrayRef<StringRef> Files = M.second.getFiles();
    MapVector<StringRef, SmallVector<StringRef, 1>> MembersToFiles;
    for (auto Iterators = std::make_pair(Members.begin(), Files.begin());
         Iterators.first != Members.end();
         ++Iterators.first, ++Iterators.second) {
      assert(Iterators.second != Files.end() &&
             "Files should be the same size as Members.");
      MembersToFiles[Iterators.first->MemberName].push_back(*Iterators.second);
    }

    std::string ErrorData;
    raw_string_ostream ErrorStream(ErrorData);
    for (const auto &[Key, Value] : MembersToFiles) {
      if (Value.size() > 1) {
        ErrorStream << "file '" << Key << "' was specified multiple times.\n";

        for (StringRef OriginalFile : Value)
          ErrorStream << "in: " << OriginalFile.str() << '\n';

        ErrorStream << '\n';
      }
    }

    ErrorStream.flush();
    if (ErrorData.size() > 0)
      return createStringError(std::errc::invalid_argument, ErrorData.c_str());
  }
  return Error::success();
}

static Error createStaticLibrary(LLVMContext &LLVMCtx, const Config &C) {
  MembersBuilder Builder(LLVMCtx, C);
  auto DataOrError = Builder.build();
  if (auto Error = DataOrError.takeError())
    return Error;

  const auto &NewMembers = DataOrError->MembersPerArchitecture;

  if (Error E = checkForDuplicates(NewMembers)) {
    if (WarningsAsErrors)
      return E;
    WithColor::defaultWarningHandler(std::move(E));
  }

  if (NewMembers.size() == 1)
    return writeArchive(OutputFile, NewMembers.begin()->second.getMembers(),
                        SymtabWritingMode::NormalSymtab,
                        /*Kind=*/object::Archive::K_DARWIN, C.Deterministic,
                        /*Thin=*/false);

  SmallVector<OwningBinary<Archive>, 2> OutputBinaries;
  for (const std::pair<const uint64_t, NewArchiveMemberList> &M : NewMembers) {
    Expected<std::unique_ptr<MemoryBuffer>> OutputBufferOrErr =
        writeArchiveToBuffer(
            M.second.getMembers(), SymtabWritingMode::NormalSymtab,
            /*Kind=*/object::Archive::K_DARWIN, C.Deterministic,
            /*Thin=*/false);
    if (!OutputBufferOrErr)
      return OutputBufferOrErr.takeError();
    std::unique_ptr<MemoryBuffer> &OutputBuffer = OutputBufferOrErr.get();

    Expected<std::unique_ptr<Archive>> ArchiveOrError =
        Archive::create(OutputBuffer->getMemBufferRef());
    if (!ArchiveOrError)
      return ArchiveOrError.takeError();
    std::unique_ptr<Archive> &A = ArchiveOrError.get();

    OutputBinaries.push_back(
        OwningBinary<Archive>(std::move(A), std::move(OutputBuffer)));
  }

  Expected<SmallVector<Slice, 2>> Slices = buildSlices(LLVMCtx, OutputBinaries);
  if (!Slices)
    return Slices.takeError();

  llvm::stable_sort(*Slices);
  return writeUniversalBinary(*Slices, OutputFile);
}

static void parseRawArgs(int Argc, char **Argv) {
  LibtoolDarwinOptTable Tbl;
  llvm::BumpPtrAllocator A;
  llvm::StringSaver Saver{A};
  opt::InputArgList Args =
      Tbl.parseArgs(Argc, Argv, OPT_UNKNOWN, Saver, [&](StringRef Msg) {
        llvm::errs() << Msg << '\n';
        std::exit(1);
      });

  if (Args.hasArg(OPT_help)) {
    Tbl.printHelp(llvm::outs(), "llvm-libtool-darwin [options] <input files>",
                  "llvm-libtool-darwin");
    std::exit(0);
  }

  InputFiles = Args.getAllArgValues(OPT_INPUT);
  Libraries = Args.getAllArgValues(OPT_libraries);
  LibrarySearchDirs = Args.getAllArgValues(OPT_librarySearchDirs);

  if (const opt::Arg *A = Args.getLastArg(OPT_outputFile))
    OutputFile = A->getValue();

  if (const opt::Arg *A = Args.getLastArg(OPT_archType))
    ArchType = std::make_optional(A->getValue());

  if (const opt::Arg *A = Args.getLastArg(OPT_fileList))
    FileList = A->getValue();

  if (const opt::Arg *A = Args.getLastArg(OPT_dependencyInfoPath))
    DependencyInfoPath = A->getValue();

  if (const opt::Arg *A = Args.getLastArg(OPT_ignoredSyslibRoot))
    IgnoredSyslibRoot = A->getValue();

  LibraryOperation =
      Args.hasArg(OPT_static) ? Operation::Static : Operation::None;
  DeterministicOption = Args.hasArg(OPT_deterministicOption);
  NonDeterministicOption = Args.hasArg(OPT_nonDeterministicOption);
  VersionOption = Args.hasArg(OPT_version);
  NoWarningForNoSymbols = Args.hasArg(OPT_noWarningForNoSymbols);
  WarningsAsErrors = Args.hasArg(OPT_warningsAsErrors);
}

static Expected<Config> parseCommandLine(int Argc, char **Argv) {
  Config C;
  parseRawArgs(Argc, Argv);

  if (LibraryOperation == Operation::None) {
    if (!VersionOption) {
      return createStringError(std::errc::invalid_argument,
                               "-static option: must be specified");
    }
    return C;
  }

  GlobalDependencyInfo =
      DependencyInfoPath.empty()
          ? std::make_unique<DummyDependencyInfo>()
          : std::make_unique<DependencyInfo>(DependencyInfoPath);

  if (OutputFile.empty()) {
    return createStringError(std::errc::invalid_argument,
                             "-o option: must be specified");
  }

  if (DeterministicOption && NonDeterministicOption)
    return createStringError(std::errc::invalid_argument,
                             "cannot specify both -D and -U flags");
  else if (NonDeterministicOption)
    C.Deterministic = false;

  if (!Libraries.empty())
    if (Error E = processCommandLineLibraries())
      return std::move(E);

  if (!FileList.empty())
    if (Error E = processFileList())
      return std::move(E);

  if (InputFiles.empty())
    return createStringError(std::errc::invalid_argument,
                             "no input files specified");

  if (ArchType) {
    if (Error E = validateArchitectureName(ArchType.value()))
      return std::move(E);

    std::tie(C.ArchCPUType, C.ArchCPUSubtype) =
        MachO::getCPUTypeFromArchitecture(
            MachO::getArchitectureFromName(ArchType.value()));
  }

  GlobalDependencyInfo->write("llvm-libtool-darwin " LLVM_VERSION_STRING,
                              InputFiles, OutputFile);

  return C;
}

int llvm_libtool_darwin_main(int Argc, char **Argv, const llvm::ToolContext &) {
  Expected<Config> ConfigOrErr = parseCommandLine(Argc, Argv);
  if (!ConfigOrErr) {
    WithColor::defaultErrorHandler(ConfigOrErr.takeError());
    return EXIT_FAILURE;
  }

  if (VersionOption)
    cl::PrintVersionMessage();

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();

  LLVMContext LLVMCtx;
  Config C = *ConfigOrErr;
  switch (LibraryOperation) {
  case Operation::None:
    break;
  case Operation::Static:
    if (Error E = createStaticLibrary(LLVMCtx, C)) {
      WithColor::defaultErrorHandler(std::move(E));
      return EXIT_FAILURE;
    }
    break;
  }
  return EXIT_SUCCESS;
}
