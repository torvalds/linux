//===--- APINotesManager.cpp - Manage API Notes Files ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/APINotes/APINotesManager.h"
#include "clang/APINotes/APINotesReader.h"
#include "clang/APINotes/APINotesYAMLCompiler.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceMgrAdapter.h"
#include "clang/Basic/Version.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"

using namespace clang;
using namespace api_notes;

#define DEBUG_TYPE "API Notes"
STATISTIC(NumHeaderAPINotes, "non-framework API notes files loaded");
STATISTIC(NumPublicFrameworkAPINotes, "framework public API notes loaded");
STATISTIC(NumPrivateFrameworkAPINotes, "framework private API notes loaded");
STATISTIC(NumFrameworksSearched, "frameworks searched");
STATISTIC(NumDirectoriesSearched, "header directories searched");
STATISTIC(NumDirectoryCacheHits, "directory cache hits");

namespace {
/// Prints two successive strings, which much be kept alive as long as the
/// PrettyStackTrace entry.
class PrettyStackTraceDoubleString : public llvm::PrettyStackTraceEntry {
  StringRef First, Second;

public:
  PrettyStackTraceDoubleString(StringRef First, StringRef Second)
      : First(First), Second(Second) {}
  void print(raw_ostream &OS) const override { OS << First << Second; }
};
} // namespace

APINotesManager::APINotesManager(SourceManager &SM, const LangOptions &LangOpts)
    : SM(SM), ImplicitAPINotes(LangOpts.APINotes) {}

APINotesManager::~APINotesManager() {
  // Free the API notes readers.
  for (const auto &Entry : Readers) {
    if (auto Reader = Entry.second.dyn_cast<APINotesReader *>())
      delete Reader;
  }

  delete CurrentModuleReaders[ReaderKind::Public];
  delete CurrentModuleReaders[ReaderKind::Private];
}

std::unique_ptr<APINotesReader>
APINotesManager::loadAPINotes(FileEntryRef APINotesFile) {
  PrettyStackTraceDoubleString Trace("Loading API notes from ",
                                     APINotesFile.getName());

  // Open the source file.
  auto SourceFileID = SM.getOrCreateFileID(APINotesFile, SrcMgr::C_User);
  auto SourceBuffer = SM.getBufferOrNone(SourceFileID, SourceLocation());
  if (!SourceBuffer)
    return nullptr;

  // Compile the API notes source into a buffer.
  // FIXME: Either propagate OSType through or, better yet, improve the binary
  // APINotes format to maintain complete availability information.
  // FIXME: We don't even really need to go through the binary format at all;
  // we're just going to immediately deserialize it again.
  llvm::SmallVector<char, 1024> APINotesBuffer;
  std::unique_ptr<llvm::MemoryBuffer> CompiledBuffer;
  {
    SourceMgrAdapter SMAdapter(
        SM, SM.getDiagnostics(), diag::err_apinotes_message,
        diag::warn_apinotes_message, diag::note_apinotes_message, APINotesFile);
    llvm::raw_svector_ostream OS(APINotesBuffer);
    if (api_notes::compileAPINotes(
            SourceBuffer->getBuffer(), SM.getFileEntryForID(SourceFileID), OS,
            SMAdapter.getDiagHandler(), SMAdapter.getDiagContext()))
      return nullptr;

    // Make a copy of the compiled form into the buffer.
    CompiledBuffer = llvm::MemoryBuffer::getMemBufferCopy(
        StringRef(APINotesBuffer.data(), APINotesBuffer.size()));
  }

  // Load the binary form we just compiled.
  auto Reader = APINotesReader::Create(std::move(CompiledBuffer), SwiftVersion);
  assert(Reader && "Could not load the API notes we just generated?");
  return Reader;
}

std::unique_ptr<APINotesReader>
APINotesManager::loadAPINotes(StringRef Buffer) {
  llvm::SmallVector<char, 1024> APINotesBuffer;
  std::unique_ptr<llvm::MemoryBuffer> CompiledBuffer;
  SourceMgrAdapter SMAdapter(
      SM, SM.getDiagnostics(), diag::err_apinotes_message,
      diag::warn_apinotes_message, diag::note_apinotes_message, std::nullopt);
  llvm::raw_svector_ostream OS(APINotesBuffer);

  if (api_notes::compileAPINotes(Buffer, nullptr, OS,
                                 SMAdapter.getDiagHandler(),
                                 SMAdapter.getDiagContext()))
    return nullptr;

  CompiledBuffer = llvm::MemoryBuffer::getMemBufferCopy(
      StringRef(APINotesBuffer.data(), APINotesBuffer.size()));
  auto Reader = APINotesReader::Create(std::move(CompiledBuffer), SwiftVersion);
  assert(Reader && "Could not load the API notes we just generated?");
  return Reader;
}

bool APINotesManager::loadAPINotes(const DirectoryEntry *HeaderDir,
                                   FileEntryRef APINotesFile) {
  assert(!Readers.contains(HeaderDir));
  if (auto Reader = loadAPINotes(APINotesFile)) {
    Readers[HeaderDir] = Reader.release();
    return false;
  }

  Readers[HeaderDir] = nullptr;
  return true;
}

OptionalFileEntryRef
APINotesManager::findAPINotesFile(DirectoryEntryRef Directory,
                                  StringRef Basename, bool WantPublic) {
  FileManager &FM = SM.getFileManager();

  llvm::SmallString<128> Path(Directory.getName());

  StringRef Suffix = WantPublic ? "" : "_private";

  // Look for the source API notes file.
  llvm::sys::path::append(Path, llvm::Twine(Basename) + Suffix + "." +
                                    SOURCE_APINOTES_EXTENSION);
  return FM.getOptionalFileRef(Path, /*Open*/ true);
}

OptionalDirectoryEntryRef APINotesManager::loadFrameworkAPINotes(
    llvm::StringRef FrameworkPath, llvm::StringRef FrameworkName, bool Public) {
  FileManager &FM = SM.getFileManager();

  llvm::SmallString<128> Path(FrameworkPath);
  unsigned FrameworkNameLength = Path.size();

  StringRef Suffix = Public ? "" : "_private";

  // Form the path to the APINotes file.
  llvm::sys::path::append(Path, "APINotes");
  llvm::sys::path::append(Path, (llvm::Twine(FrameworkName) + Suffix + "." +
                                 SOURCE_APINOTES_EXTENSION));

  // Try to open the APINotes file.
  auto APINotesFile = FM.getOptionalFileRef(Path);
  if (!APINotesFile)
    return std::nullopt;

  // Form the path to the corresponding header directory.
  Path.resize(FrameworkNameLength);
  llvm::sys::path::append(Path, Public ? "Headers" : "PrivateHeaders");

  // Try to access the header directory.
  auto HeaderDir = FM.getOptionalDirectoryRef(Path);
  if (!HeaderDir)
    return std::nullopt;

  // Try to load the API notes.
  if (loadAPINotes(*HeaderDir, *APINotesFile))
    return std::nullopt;

  // Success: return the header directory.
  if (Public)
    ++NumPublicFrameworkAPINotes;
  else
    ++NumPrivateFrameworkAPINotes;
  return *HeaderDir;
}

static void checkPrivateAPINotesName(DiagnosticsEngine &Diags,
                                     const FileEntry *File, const Module *M) {
  if (File->tryGetRealPathName().empty())
    return;

  StringRef RealFileName =
      llvm::sys::path::filename(File->tryGetRealPathName());
  StringRef RealStem = llvm::sys::path::stem(RealFileName);
  if (RealStem.ends_with("_private"))
    return;

  unsigned DiagID = diag::warn_apinotes_private_case;
  if (M->IsSystem)
    DiagID = diag::warn_apinotes_private_case_system;

  Diags.Report(SourceLocation(), DiagID) << M->Name << RealFileName;
}

/// \returns true if any of \p module's immediate submodules are defined in a
/// private module map
static bool hasPrivateSubmodules(const Module *M) {
  return llvm::any_of(M->submodules(), [](const Module *Submodule) {
    return Submodule->ModuleMapIsPrivate;
  });
}

llvm::SmallVector<FileEntryRef, 2>
APINotesManager::getCurrentModuleAPINotes(Module *M, bool LookInModule,
                                          ArrayRef<std::string> SearchPaths) {
  FileManager &FM = SM.getFileManager();
  auto ModuleName = M->getTopLevelModuleName();
  auto ExportedModuleName = M->getTopLevelModule()->ExportAsModule;
  llvm::SmallVector<FileEntryRef, 2> APINotes;

  // First, look relative to the module itself.
  if (LookInModule && M->Directory) {
    // Local function to try loading an API notes file in the given directory.
    auto tryAPINotes = [&](DirectoryEntryRef Dir, bool WantPublic) {
      if (auto File = findAPINotesFile(Dir, ModuleName, WantPublic)) {
        if (!WantPublic)
          checkPrivateAPINotesName(SM.getDiagnostics(), *File, M);

        APINotes.push_back(*File);
      }
      // If module FooCore is re-exported through module Foo, try Foo.apinotes.
      if (!ExportedModuleName.empty())
        if (auto File = findAPINotesFile(Dir, ExportedModuleName, WantPublic))
          APINotes.push_back(*File);
    };

    if (M->IsFramework) {
      // For frameworks, we search in the "Headers" or "PrivateHeaders"
      // subdirectory.
      //
      // Public modules:
      // - Headers/Foo.apinotes
      // - PrivateHeaders/Foo_private.apinotes (if there are private submodules)
      // Private modules:
      // - PrivateHeaders/Bar.apinotes (except that 'Bar' probably already has
      //   the word "Private" in it in practice)
      llvm::SmallString<128> Path(M->Directory->getName());

      if (!M->ModuleMapIsPrivate) {
        unsigned PathLen = Path.size();

        llvm::sys::path::append(Path, "Headers");
        if (auto APINotesDir = FM.getOptionalDirectoryRef(Path))
          tryAPINotes(*APINotesDir, /*wantPublic=*/true);

        Path.resize(PathLen);
      }

      if (M->ModuleMapIsPrivate || hasPrivateSubmodules(M)) {
        llvm::sys::path::append(Path, "PrivateHeaders");
        if (auto PrivateAPINotesDir = FM.getOptionalDirectoryRef(Path))
          tryAPINotes(*PrivateAPINotesDir,
                      /*wantPublic=*/M->ModuleMapIsPrivate);
      }
    } else {
      // Public modules:
      // - Foo.apinotes
      // - Foo_private.apinotes (if there are private submodules)
      // Private modules:
      // - Bar.apinotes (except that 'Bar' probably already has the word
      //   "Private" in it in practice)
      tryAPINotes(*M->Directory, /*wantPublic=*/true);
      if (!M->ModuleMapIsPrivate && hasPrivateSubmodules(M))
        tryAPINotes(*M->Directory, /*wantPublic=*/false);
    }

    if (!APINotes.empty())
      return APINotes;
  }

  // Second, look for API notes for this module in the module API
  // notes search paths.
  for (const auto &SearchPath : SearchPaths) {
    if (auto SearchDir = FM.getOptionalDirectoryRef(SearchPath)) {
      if (auto File = findAPINotesFile(*SearchDir, ModuleName)) {
        APINotes.push_back(*File);
        return APINotes;
      }
    }
  }

  // Didn't find any API notes.
  return APINotes;
}

bool APINotesManager::loadCurrentModuleAPINotes(
    Module *M, bool LookInModule, ArrayRef<std::string> SearchPaths) {
  assert(!CurrentModuleReaders[ReaderKind::Public] &&
         "Already loaded API notes for the current module?");

  auto APINotes = getCurrentModuleAPINotes(M, LookInModule, SearchPaths);
  unsigned NumReaders = 0;
  for (auto File : APINotes) {
    CurrentModuleReaders[NumReaders++] = loadAPINotes(File).release();
    if (!getCurrentModuleReaders().empty())
      M->APINotesFile = File.getName().str();
  }

  return NumReaders > 0;
}

bool APINotesManager::loadCurrentModuleAPINotesFromBuffer(
    ArrayRef<StringRef> Buffers) {
  unsigned NumReader = 0;
  for (auto Buf : Buffers) {
    auto Reader = loadAPINotes(Buf);
    assert(Reader && "Could not load the API notes we just generated?");

    CurrentModuleReaders[NumReader++] = Reader.release();
  }
  return NumReader;
}

llvm::SmallVector<APINotesReader *, 2>
APINotesManager::findAPINotes(SourceLocation Loc) {
  llvm::SmallVector<APINotesReader *, 2> Results;

  // If there are readers for the current module, return them.
  if (!getCurrentModuleReaders().empty()) {
    Results.append(getCurrentModuleReaders().begin(),
                   getCurrentModuleReaders().end());
    return Results;
  }

  // If we're not allowed to implicitly load API notes files, we're done.
  if (!ImplicitAPINotes)
    return Results;

  // If we don't have source location information, we're done.
  if (Loc.isInvalid())
    return Results;

  // API notes are associated with the expansion location. Retrieve the
  // file for this location.
  SourceLocation ExpansionLoc = SM.getExpansionLoc(Loc);
  FileID ID = SM.getFileID(ExpansionLoc);
  if (ID.isInvalid())
    return Results;
  OptionalFileEntryRef File = SM.getFileEntryRefForID(ID);
  if (!File)
    return Results;

  // Look for API notes in the directory corresponding to this file, or one of
  // its its parent directories.
  OptionalDirectoryEntryRef Dir = File->getDir();
  FileManager &FileMgr = SM.getFileManager();
  llvm::SetVector<const DirectoryEntry *,
                  SmallVector<const DirectoryEntry *, 4>,
                  llvm::SmallPtrSet<const DirectoryEntry *, 4>>
      DirsVisited;
  do {
    // Look for an API notes reader for this header search directory.
    auto Known = Readers.find(*Dir);

    // If we already know the answer, chase it.
    if (Known != Readers.end()) {
      ++NumDirectoryCacheHits;

      // We've been redirected to another directory for answers. Follow it.
      if (Known->second && Known->second.is<DirectoryEntryRef>()) {
        DirsVisited.insert(*Dir);
        Dir = Known->second.get<DirectoryEntryRef>();
        continue;
      }

      // We have the answer.
      if (auto Reader = Known->second.dyn_cast<APINotesReader *>())
        Results.push_back(Reader);
      break;
    }

    // Look for API notes corresponding to this directory.
    StringRef Path = Dir->getName();
    if (llvm::sys::path::extension(Path) == ".framework") {
      // If this is a framework directory, check whether there are API notes
      // in the APINotes subdirectory.
      auto FrameworkName = llvm::sys::path::stem(Path);
      ++NumFrameworksSearched;

      // Look for API notes for both the public and private headers.
      OptionalDirectoryEntryRef PublicDir =
          loadFrameworkAPINotes(Path, FrameworkName, /*Public=*/true);
      OptionalDirectoryEntryRef PrivateDir =
          loadFrameworkAPINotes(Path, FrameworkName, /*Public=*/false);

      if (PublicDir || PrivateDir) {
        // We found API notes: don't ever look past the framework directory.
        Readers[*Dir] = nullptr;

        // Pretend we found the result in the public or private directory,
        // as appropriate. All headers should be in one of those two places,
        // but be defensive here.
        if (!DirsVisited.empty()) {
          if (PublicDir && DirsVisited.back() == *PublicDir) {
            DirsVisited.pop_back();
            Dir = *PublicDir;
          } else if (PrivateDir && DirsVisited.back() == *PrivateDir) {
            DirsVisited.pop_back();
            Dir = *PrivateDir;
          }
        }

        // Grab the result.
        if (auto Reader = Readers[*Dir].dyn_cast<APINotesReader *>())
          Results.push_back(Reader);
        break;
      }
    } else {
      // Look for an APINotes file in this directory.
      llvm::SmallString<128> APINotesPath(Dir->getName());
      llvm::sys::path::append(
          APINotesPath, (llvm::Twine("APINotes.") + SOURCE_APINOTES_EXTENSION));

      // If there is an API notes file here, try to load it.
      ++NumDirectoriesSearched;
      if (auto APINotesFile = FileMgr.getOptionalFileRef(APINotesPath)) {
        if (!loadAPINotes(*Dir, *APINotesFile)) {
          ++NumHeaderAPINotes;
          if (auto Reader = Readers[*Dir].dyn_cast<APINotesReader *>())
            Results.push_back(Reader);
          break;
        }
      }
    }

    // We didn't find anything. Look at the parent directory.
    if (!DirsVisited.insert(*Dir)) {
      Dir = std::nullopt;
      break;
    }

    StringRef ParentPath = llvm::sys::path::parent_path(Path);
    while (llvm::sys::path::stem(ParentPath) == "..")
      ParentPath = llvm::sys::path::parent_path(ParentPath);

    Dir = ParentPath.empty() ? std::nullopt
                             : FileMgr.getOptionalDirectoryRef(ParentPath);
  } while (Dir);

  // Path compression for all of the directories we visited, redirecting
  // them to the directory we ended on. If no API notes were found, the
  // resulting directory will be NULL, indicating no API notes.
  for (const auto Visited : DirsVisited)
    Readers[Visited] = Dir ? ReaderEntry(*Dir) : ReaderEntry();

  return Results;
}
