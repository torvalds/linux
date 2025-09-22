//===-- llvm-readtapi.cpp - tapi file reader and transformer -----*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the command-line driver for llvm-readtapi.
//
//===----------------------------------------------------------------------===//
#include "DiffEngine.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TextAPI/DylibReader.h"
#include "llvm/TextAPI/TextAPIError.h"
#include "llvm/TextAPI/TextAPIReader.h"
#include "llvm/TextAPI/TextAPIWriter.h"
#include "llvm/TextAPI/Utils.h"
#include <cstdlib>

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#endif

using namespace llvm;
using namespace MachO;
using namespace object;

namespace {
using namespace llvm::opt;
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "TapiOpts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "TapiOpts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "TapiOpts.inc"
#undef OPTION
};

class TAPIOptTable : public opt::GenericOptTable {
public:
  TAPIOptTable() : opt::GenericOptTable(InfoTable) {
    setGroupedShortOptions(true);
  }
};

struct StubOptions {
  bool DeleteInput = false;
  bool DeletePrivate = false;
  bool TraceLibs = false;
};

struct Context {
  std::vector<std::string> Inputs;
  StubOptions StubOpt;
  std::unique_ptr<llvm::raw_fd_stream> OutStream;
  FileType WriteFT = FileType::TBD_V5;
  bool Compact = false;
  Architecture Arch = AK_unknown;
};

// Use unique exit code to differentiate failures not directly caused from
// TextAPI operations. This is used for wrapping `compare` operations in
// automation and scripting.
const int NON_TAPI_EXIT_CODE = 2;
const std::string TOOLNAME = "llvm-readtapi";
ExitOnError ExitOnErr;
} // anonymous namespace

// Handle error reporting in cases where `ExitOnError` is not used.
static void reportError(Twine Message, int ExitCode = EXIT_FAILURE) {
  errs() << TOOLNAME << ": error: " << Message << "\n";
  errs().flush();
  exit(ExitCode);
}

// Handle warnings.
static void reportWarning(Twine Message) {
  errs() << TOOLNAME << ": warning: " << Message << "\n";
}

/// Get what the symlink points to.
/// This is a no-op on windows as it references POSIX level apis.
static void read_link(const Twine &Path, SmallVectorImpl<char> &Output) {
#if !defined(_MSC_VER) && !defined(__MINGW32__)
  Output.clear();
  if (Path.isTriviallyEmpty())
    return;

  SmallString<PATH_MAX> Storage;
  auto P = Path.toNullTerminatedStringRef(Storage);
  SmallString<PATH_MAX> Result;
  ssize_t Len;
  if ((Len = ::readlink(P.data(), Result.data(), PATH_MAX)) == -1)
    reportError("unable to read symlink: " + Path);
  Result.resize_for_overwrite(Len);
  Output.swap(Result);
#else
  reportError("unable to read symlink on windows: " + Path);
#endif
}

static std::unique_ptr<InterfaceFile>
getInterfaceFile(const StringRef Filename, bool ResetBanner = true) {
  ExitOnErr.setBanner(TOOLNAME + ": error: '" + Filename.str() + "' ");
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFile(Filename);
  if (BufferOrErr.getError())
    ExitOnErr(errorCodeToError(BufferOrErr.getError()));
  auto Buffer = std::move(*BufferOrErr);

  std::unique_ptr<InterfaceFile> IF;
  switch (identify_magic(Buffer->getBuffer())) {
  case file_magic::macho_dynamically_linked_shared_lib:
  case file_magic::macho_dynamically_linked_shared_lib_stub:
  case file_magic::macho_universal_binary:
    IF = ExitOnErr(DylibReader::get(Buffer->getMemBufferRef()));
    break;
  case file_magic::tapi_file:
    IF = ExitOnErr(TextAPIReader::get(Buffer->getMemBufferRef()));
    break;
  default:
    reportError(Filename + ": unsupported file type");
  }

  if (ResetBanner)
    ExitOnErr.setBanner(TOOLNAME + ": error: ");
  return IF;
}

static bool handleCompareAction(const Context &Ctx) {
  if (Ctx.Inputs.size() != 2)
    reportError("compare only supports two input files",
                /*ExitCode=*/NON_TAPI_EXIT_CODE);

  // Override default exit code.
  ExitOnErr = ExitOnError(TOOLNAME + ": error: ",
                          /*DefaultErrorExitCode=*/NON_TAPI_EXIT_CODE);
  auto LeftIF = getInterfaceFile(Ctx.Inputs.front());
  auto RightIF = getInterfaceFile(Ctx.Inputs.at(1));

  raw_ostream &OS = Ctx.OutStream ? *Ctx.OutStream : outs();
  return DiffEngine(LeftIF.get(), RightIF.get()).compareFiles(OS);
}

static bool handleWriteAction(const Context &Ctx,
                              std::unique_ptr<InterfaceFile> Out = nullptr) {
  if (!Out) {
    if (Ctx.Inputs.size() != 1)
      reportError("write only supports one input file");
    Out = getInterfaceFile(Ctx.Inputs.front());
  }
  raw_ostream &OS = Ctx.OutStream ? *Ctx.OutStream : outs();
  ExitOnErr(TextAPIWriter::writeToStream(OS, *Out, Ctx.WriteFT, Ctx.Compact));
  return EXIT_SUCCESS;
}

static bool handleMergeAction(const Context &Ctx) {
  if (Ctx.Inputs.size() < 2)
    reportError("merge requires at least two input files");

  std::unique_ptr<InterfaceFile> Out;
  for (StringRef FileName : Ctx.Inputs) {
    auto IF = getInterfaceFile(FileName);
    // On the first iteration copy the input file and skip merge.
    if (!Out) {
      Out = std::move(IF);
      continue;
    }
    Out = ExitOnErr(Out->merge(IF.get()));
  }
  return handleWriteAction(Ctx, std::move(Out));
}

static void stubifyImpl(std::unique_ptr<InterfaceFile> IF, Context &Ctx) {
  // TODO: Add inlining and magic merge support.
  if (Ctx.OutStream == nullptr) {
    std::error_code EC;
    assert(!IF->getPath().empty() && "Unknown output location");
    SmallString<PATH_MAX> OutputLoc = IF->getPath();
    replace_extension(OutputLoc, ".tbd");
    Ctx.OutStream = std::make_unique<llvm::raw_fd_stream>(OutputLoc, EC);
    if (EC)
      reportError("opening file '" + OutputLoc + ": " + EC.message());
  }

  handleWriteAction(Ctx, std::move(IF));
  // Clear out output stream after file has been written incase more files are
  // stubifed.
  Ctx.OutStream = nullptr;
}

static void stubifyDirectory(const StringRef InputPath, Context &Ctx) {
  assert(InputPath.back() != '/' && "Unexpected / at end of input path.");
  StringMap<std::vector<SymLink>> SymLinks;
  StringMap<std::unique_ptr<InterfaceFile>> Dylibs;
  StringMap<std::string> OriginalNames;
  std::set<std::pair<std::string, bool>> LibsToDelete;

  std::error_code EC;
  for (sys::fs::recursive_directory_iterator IT(InputPath, EC), IE; IT != IE;
       IT.increment(EC)) {
    if (EC == std::errc::no_such_file_or_directory) {
      reportWarning(IT->path() + ": " + EC.message());
      continue;
    }
    if (EC)
      reportError(IT->path() + ": " + EC.message());

    // Skip header directories (include/Headers/PrivateHeaders) and module
    // files.
    StringRef Path = IT->path();
    if (Path.ends_with("/include") || Path.ends_with("/Headers") ||
        Path.ends_with("/PrivateHeaders") || Path.ends_with("/Modules") ||
        Path.ends_with(".map") || Path.ends_with(".modulemap")) {
      IT.no_push();
      continue;
    }

    // Check if the entry is a symlink. We don't follow symlinks but we record
    // their content.
    bool IsSymLink;
    if (auto EC = sys::fs::is_symlink_file(Path, IsSymLink))
      reportError(Path + ": " + EC.message());

    if (IsSymLink) {
      IT.no_push();

      bool ShouldSkip;
      auto SymLinkEC = shouldSkipSymLink(Path, ShouldSkip);

      // If symlink is broken, for some reason, we should continue
      // trying to repair it before quitting.
      if (!SymLinkEC && ShouldSkip)
        continue;

      if (Ctx.StubOpt.DeletePrivate &&
          isPrivateLibrary(Path.drop_front(InputPath.size()), true)) {
        LibsToDelete.emplace(Path, false);
        continue;
      }

      SmallString<PATH_MAX> SymPath;
      read_link(Path, SymPath);
      // Sometimes there are broken symlinks that are absolute paths, which are
      // invalid during build time, but would be correct during runtime. In the
      // case of an absolute path we should check first if the path exists with
      // the known locations as prefix.
      SmallString<PATH_MAX> LinkSrc = Path;
      SmallString<PATH_MAX> LinkTarget;
      if (sys::path::is_absolute(SymPath)) {
        LinkTarget = InputPath;
        sys::path::append(LinkTarget, SymPath);

        // TODO: Investigate supporting a file manager for file system accesses.
        if (sys::fs::exists(LinkTarget)) {
          // Convert the absolute path to an relative path.
          if (auto ec = MachO::make_relative(LinkSrc, LinkTarget, SymPath))
            reportError(LinkTarget + ": " + EC.message());
        } else if (!sys::fs::exists(SymPath)) {
          reportWarning("ignoring broken symlink: " + Path);
          continue;
        } else {
          LinkTarget = SymPath;
        }
      } else {
        LinkTarget = LinkSrc;
        sys::path::remove_filename(LinkTarget);
        sys::path::append(LinkTarget, SymPath);
      }

      // For Apple SDKs, the symlink src is guaranteed to be a canonical path
      // because we don't follow symlinks when scanning. The symlink target is
      // constructed from the symlink path and needs to be canonicalized.
      if (auto ec = sys::fs::real_path(Twine(LinkTarget), LinkTarget)) {
        reportWarning(LinkTarget + ": " + ec.message());
        continue;
      }

      auto itr = SymLinks.insert({LinkTarget.c_str(), std::vector<SymLink>()});
      itr.first->second.emplace_back(LinkSrc.str(), std::string(SymPath.str()));

      continue;
    }

    bool IsDirectory = false;
    if (auto EC = sys::fs::is_directory(Path, IsDirectory))
      reportError(Path + ": " + EC.message());
    if (IsDirectory)
      continue;

    if (Ctx.StubOpt.DeletePrivate &&
        isPrivateLibrary(Path.drop_front(InputPath.size()))) {
      IT.no_push();
      LibsToDelete.emplace(Path, false);
      continue;
    }
    auto IF = getInterfaceFile(Path);
    if (Ctx.StubOpt.TraceLibs)
      errs() << Path << "\n";

    // Normalize path for map lookup by removing the extension.
    SmallString<PATH_MAX> NormalizedPath(Path);
    replace_extension(NormalizedPath, "");

    if ((IF->getFileType() == FileType::MachO_DynamicLibrary) ||
        (IF->getFileType() == FileType::MachO_DynamicLibrary_Stub)) {
      OriginalNames[NormalizedPath.c_str()] = IF->getPath();

      // Don't add this MachO dynamic library because we already have a
      // text-based stub recorded for this path.
      if (Dylibs.count(NormalizedPath.c_str()))
        continue;
    }

    Dylibs[NormalizedPath.c_str()] = std::move(IF);
  }

  for (auto &Lib : Dylibs) {
    auto &Dylib = Lib.second;
    // Get the original file name.
    SmallString<PATH_MAX> NormalizedPath(Dylib->getPath());
    stubifyImpl(std::move(Dylib), Ctx);

    replace_extension(NormalizedPath, "");
    auto Found = OriginalNames.find(NormalizedPath.c_str());
    if (Found == OriginalNames.end())
      continue;

    if (Ctx.StubOpt.DeleteInput)
      LibsToDelete.emplace(Found->second, true);

    // Don't allow for more than 20 levels of symlinks when searching for
    // libraries to stubify.
    StringRef LibToCheck = Found->second;
    for (int i = 0; i < 20; ++i) {
      auto LinkIt = SymLinks.find(LibToCheck.str());
      if (LinkIt != SymLinks.end()) {
        for (auto &SymInfo : LinkIt->second) {
          SmallString<PATH_MAX> LinkSrc(SymInfo.SrcPath);
          SmallString<PATH_MAX> LinkTarget(SymInfo.LinkContent);
          replace_extension(LinkSrc, "tbd");
          replace_extension(LinkTarget, "tbd");

          if (auto EC = sys::fs::remove(LinkSrc))
            reportError(LinkSrc + " : " + EC.message());

          if (auto EC = sys::fs::create_link(LinkTarget, LinkSrc))
            reportError(LinkTarget + " : " + EC.message());

          if (Ctx.StubOpt.DeleteInput)
            LibsToDelete.emplace(SymInfo.SrcPath, true);

          LibToCheck = SymInfo.SrcPath;
        }
      } else
        break;
    }
  }

  // Recursively delete the directories. This will abort when they are not empty
  // or we reach the root of the SDK.
  for (const auto &[LibPath, IsInput] : LibsToDelete) {
    if (!IsInput && SymLinks.count(LibPath))
      continue;

    if (auto EC = sys::fs::remove(LibPath))
      reportError(LibPath + " : " + EC.message());

    std::error_code EC;
    auto Dir = sys::path::parent_path(LibPath);
    do {
      EC = sys::fs::remove(Dir);
      Dir = sys::path::parent_path(Dir);
      if (!Dir.starts_with(InputPath))
        break;
    } while (!EC);
  }
}

static bool handleStubifyAction(Context &Ctx) {
  if (Ctx.Inputs.empty())
    reportError("stubify requires at least one input file");

  if ((Ctx.Inputs.size() > 1) && (Ctx.OutStream != nullptr))
    reportError("cannot write multiple inputs into single output file");

  for (StringRef PathName : Ctx.Inputs) {
    bool IsDirectory = false;
    if (auto EC = sys::fs::is_directory(PathName, IsDirectory))
      reportError(PathName + ": " + EC.message());

    if (IsDirectory) {
      if (Ctx.OutStream != nullptr)
        reportError("cannot stubify directory'" + PathName +
                    "' into single output file");
      stubifyDirectory(PathName, Ctx);
      continue;
    }

    stubifyImpl(getInterfaceFile(PathName), Ctx);
    if (Ctx.StubOpt.DeleteInput)
      if (auto ec = sys::fs::remove(PathName))
        reportError("deleting file '" + PathName + ": " + ec.message());
  }
  return EXIT_SUCCESS;
}

using IFOperation =
    std::function<llvm::Expected<std::unique_ptr<InterfaceFile>>(
        const llvm::MachO::InterfaceFile &, Architecture)>;
static bool handleSingleFileAction(const Context &Ctx, const StringRef Action,
                                   IFOperation act) {
  if (Ctx.Inputs.size() != 1)
    reportError(Action + " only supports one input file");
  if (Ctx.Arch == AK_unknown)
    reportError(Action + " requires -arch <arch>");

  auto IF = getInterfaceFile(Ctx.Inputs.front(), /*ResetBanner=*/false);
  auto OutIF = act(*IF, Ctx.Arch);
  if (!OutIF)
    ExitOnErr(OutIF.takeError());

  return handleWriteAction(Ctx, std::move(*OutIF));
}

static void setStubOptions(opt::InputArgList &Args, StubOptions &Opt) {
  Opt.DeleteInput = Args.hasArg(OPT_delete_input);
  Opt.DeletePrivate = Args.hasArg(OPT_delete_private_libraries);
  Opt.TraceLibs = Args.hasArg(OPT_t);
}

int main(int Argc, char **Argv) {
  InitLLVM X(Argc, Argv);
  BumpPtrAllocator A;
  StringSaver Saver(A);
  TAPIOptTable Tbl;
  Context Ctx;
  ExitOnErr.setBanner(TOOLNAME + ": error:");
  opt::InputArgList Args = Tbl.parseArgs(
      Argc, Argv, OPT_UNKNOWN, Saver, [&](StringRef Msg) { reportError(Msg); });
  if (Args.hasArg(OPT_help)) {
    Tbl.printHelp(outs(),
                  "USAGE: llvm-readtapi <command> [-arch <architecture> "
                  "<options>]* <inputs> [-o "
                  "<output>]*",
                  "LLVM TAPI file reader and transformer");
    return EXIT_SUCCESS;
  }

  if (Args.hasArg(OPT_version)) {
    cl::PrintVersionMessage();
    return EXIT_SUCCESS;
  }

  for (opt::Arg *A : Args.filtered(OPT_INPUT))
    Ctx.Inputs.push_back(A->getValue());

  if (opt::Arg *A = Args.getLastArg(OPT_output_EQ)) {
    std::string OutputLoc = std::move(A->getValue());
    std::error_code EC;
    Ctx.OutStream = std::make_unique<llvm::raw_fd_stream>(OutputLoc, EC);
    if (EC)
      reportError("error opening the file '" + OutputLoc + EC.message(),
                  NON_TAPI_EXIT_CODE);
  }

  Ctx.Compact = Args.hasArg(OPT_compact);

  if (opt::Arg *A = Args.getLastArg(OPT_filetype_EQ)) {
    StringRef FT = A->getValue();
    Ctx.WriteFT = TextAPIWriter::parseFileType(FT);
    if (Ctx.WriteFT < FileType::TBD_V3)
      reportError("deprecated filetype '" + FT + "' is not supported to write");
    if (Ctx.WriteFT == FileType::Invalid)
      reportError("unsupported filetype '" + FT + "'");
  }

  if (opt::Arg *A = Args.getLastArg(OPT_arch_EQ)) {
    StringRef Arch = A->getValue();
    Ctx.Arch = getArchitectureFromName(Arch);
    if (Ctx.Arch == AK_unknown)
      reportError("unsupported architecture '" + Arch);
  }
  // Handle top level and exclusive operation.
  SmallVector<opt::Arg *, 1> ActionArgs(Args.filtered(OPT_action_group));

  if (ActionArgs.empty())
    // If no action specified, write out tapi file in requested format.
    return handleWriteAction(Ctx);

  if (ActionArgs.size() > 1) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    OS << "only one of the following actions can be specified:";
    for (auto *Arg : ActionArgs)
      OS << " " << Arg->getSpelling();
    reportError(OS.str());
  }

  switch (ActionArgs.front()->getOption().getID()) {
  case OPT_compare:
    return handleCompareAction(Ctx);
  case OPT_merge:
    return handleMergeAction(Ctx);
  case OPT_extract:
    return handleSingleFileAction(Ctx, "extract", &InterfaceFile::extract);
  case OPT_remove:
    return handleSingleFileAction(Ctx, "remove", &InterfaceFile::remove);
  case OPT_stubify:
    setStubOptions(Args, Ctx.StubOpt);
    return handleStubifyAction(Ctx);
  }

  return EXIT_SUCCESS;
}
