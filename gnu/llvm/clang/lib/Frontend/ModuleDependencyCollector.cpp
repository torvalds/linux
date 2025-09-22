//===--- ModuleDependencyCollector.cpp - Collect module dependencies ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Collect the dependencies of a set of modules.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/CharInfo.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

namespace {
/// Private implementations for ModuleDependencyCollector
class ModuleDependencyListener : public ASTReaderListener {
  ModuleDependencyCollector &Collector;
  FileManager &FileMgr;
public:
  ModuleDependencyListener(ModuleDependencyCollector &Collector,
                           FileManager &FileMgr)
      : Collector(Collector), FileMgr(FileMgr) {}
  bool needsInputFileVisitation() override { return true; }
  bool needsSystemInputFileVisitation() override { return true; }
  bool visitInputFile(StringRef Filename, bool IsSystem, bool IsOverridden,
                      bool IsExplicitModule) override {
    // Run this through the FileManager in order to respect 'use-external-name'
    // in case we have a VFS overlay.
    if (auto FE = FileMgr.getOptionalFileRef(Filename))
      Filename = FE->getName();
    Collector.addFile(Filename);
    return true;
  }
};

struct ModuleDependencyPPCallbacks : public PPCallbacks {
  ModuleDependencyCollector &Collector;
  SourceManager &SM;
  ModuleDependencyPPCallbacks(ModuleDependencyCollector &Collector,
                              SourceManager &SM)
      : Collector(Collector), SM(SM) {}

  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange,
                          OptionalFileEntryRef File, StringRef SearchPath,
                          StringRef RelativePath, const Module *SuggestedModule,
                          bool ModuleImported,
                          SrcMgr::CharacteristicKind FileType) override {
    if (!File)
      return;
    Collector.addFile(File->getName());
  }
};

struct ModuleDependencyMMCallbacks : public ModuleMapCallbacks {
  ModuleDependencyCollector &Collector;
  ModuleDependencyMMCallbacks(ModuleDependencyCollector &Collector)
      : Collector(Collector) {}

  void moduleMapAddHeader(StringRef HeaderPath) override {
    if (llvm::sys::path::is_absolute(HeaderPath))
      Collector.addFile(HeaderPath);
  }
  void moduleMapAddUmbrellaHeader(FileEntryRef Header) override {
    moduleMapAddHeader(Header.getNameAsRequested());
  }
};

} // namespace

void ModuleDependencyCollector::attachToASTReader(ASTReader &R) {
  R.addListener(
      std::make_unique<ModuleDependencyListener>(*this, R.getFileManager()));
}

void ModuleDependencyCollector::attachToPreprocessor(Preprocessor &PP) {
  PP.addPPCallbacks(std::make_unique<ModuleDependencyPPCallbacks>(
      *this, PP.getSourceManager()));
  PP.getHeaderSearchInfo().getModuleMap().addModuleMapCallbacks(
      std::make_unique<ModuleDependencyMMCallbacks>(*this));
}

static bool isCaseSensitivePath(StringRef Path) {
  SmallString<256> TmpDest = Path, UpperDest, RealDest;
  // Remove component traversals, links, etc.
  if (llvm::sys::fs::real_path(Path, TmpDest))
    return true; // Current default value in vfs.yaml
  Path = TmpDest;

  // Change path to all upper case and ask for its real path, if the latter
  // exists and is equal to Path, it's not case sensitive. Default to case
  // sensitive in the absence of realpath, since this is what the VFSWriter
  // already expects when sensitivity isn't setup.
  for (auto &C : Path)
    UpperDest.push_back(toUppercase(C));
  if (!llvm::sys::fs::real_path(UpperDest, RealDest) && Path == RealDest)
    return false;
  return true;
}

void ModuleDependencyCollector::writeFileMap() {
  if (Seen.empty())
    return;

  StringRef VFSDir = getDest();

  // Default to use relative overlay directories in the VFS yaml file. This
  // allows crash reproducer scripts to work across machines.
  VFSWriter.setOverlayDir(VFSDir);

  // Explicitly set case sensitivity for the YAML writer. For that, find out
  // the sensitivity at the path where the headers all collected to.
  VFSWriter.setCaseSensitivity(isCaseSensitivePath(VFSDir));

  // Do not rely on real path names when executing the crash reproducer scripts
  // since we only want to actually use the files we have on the VFS cache.
  VFSWriter.setUseExternalNames(false);

  std::error_code EC;
  SmallString<256> YAMLPath = VFSDir;
  llvm::sys::path::append(YAMLPath, "vfs.yaml");
  llvm::raw_fd_ostream OS(YAMLPath, EC, llvm::sys::fs::OF_TextWithCRLF);
  if (EC) {
    HasErrors = true;
    return;
  }
  VFSWriter.write(OS);
}

std::error_code ModuleDependencyCollector::copyToRoot(StringRef Src,
                                                      StringRef Dst) {
  using namespace llvm::sys;
  llvm::FileCollector::PathCanonicalizer::PathStorage Paths =
      Canonicalizer.canonicalize(Src);

  SmallString<256> CacheDst = getDest();

  if (Dst.empty()) {
    // The common case is to map the virtual path to the same path inside the
    // cache.
    path::append(CacheDst, path::relative_path(Paths.CopyFrom));
  } else {
    // When collecting entries from input vfsoverlays, copy the external
    // contents into the cache but still map from the source.
    if (!fs::exists(Dst))
      return std::error_code();
    path::append(CacheDst, Dst);
    Paths.CopyFrom = Dst;
  }

  // Copy the file into place.
  if (std::error_code EC = fs::create_directories(path::parent_path(CacheDst),
                                                  /*IgnoreExisting=*/true))
    return EC;
  if (std::error_code EC = fs::copy_file(Paths.CopyFrom, CacheDst))
    return EC;

  // Always map a canonical src path to its real path into the YAML, by doing
  // this we map different virtual src paths to the same entry in the VFS
  // overlay, which is a way to emulate symlink inside the VFS; this is also
  // needed for correctness, not doing that can lead to module redefinition
  // errors.
  addFileMapping(Paths.VirtualPath, CacheDst);
  return std::error_code();
}

void ModuleDependencyCollector::addFile(StringRef Filename, StringRef FileDst) {
  if (insertSeen(Filename))
    if (copyToRoot(Filename, FileDst))
      HasErrors = true;
}
