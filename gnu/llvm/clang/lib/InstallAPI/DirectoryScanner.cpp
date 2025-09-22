//===- DirectoryScanner.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/InstallAPI/DirectoryScanner.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/TextAPI/DylibReader.h"

using namespace llvm;
using namespace llvm::MachO;

namespace clang::installapi {

HeaderSeq DirectoryScanner::getHeaders(ArrayRef<Library> Libraries) {
  HeaderSeq Headers;
  for (const Library &Lib : Libraries)
    llvm::append_range(Headers, Lib.Headers);
  return Headers;
}

llvm::Error DirectoryScanner::scan(StringRef Directory) {
  if (Mode == ScanMode::ScanFrameworks)
    return scanForFrameworks(Directory);

  return scanForUnwrappedLibraries(Directory);
}

llvm::Error DirectoryScanner::scanForUnwrappedLibraries(StringRef Directory) {
  // Check some known sub-directory locations.
  auto GetDirectory = [&](const char *Sub) -> OptionalDirectoryEntryRef {
    SmallString<PATH_MAX> Path(Directory);
    sys::path::append(Path, Sub);
    return FM.getOptionalDirectoryRef(Path);
  };

  auto DirPublic = GetDirectory("usr/include");
  auto DirPrivate = GetDirectory("usr/local/include");
  if (!DirPublic && !DirPrivate) {
    std::error_code ec = std::make_error_code(std::errc::not_a_directory);
    return createStringError(ec,
                             "cannot find any public (usr/include) or private "
                             "(usr/local/include) header directory");
  }

  Library &Lib = getOrCreateLibrary(Directory, Libraries);
  Lib.IsUnwrappedDylib = true;

  if (DirPublic)
    if (Error Err = scanHeaders(DirPublic->getName(), Lib, HeaderType::Public,
                                Directory))
      return Err;

  if (DirPrivate)
    if (Error Err = scanHeaders(DirPrivate->getName(), Lib, HeaderType::Private,
                                Directory))
      return Err;

  return Error::success();
}

static bool isFramework(StringRef Path) {
  while (Path.back() == '/')
    Path = Path.slice(0, Path.size() - 1);

  return llvm::StringSwitch<bool>(llvm::sys::path::extension(Path))
      .Case(".framework", true)
      .Default(false);
}

Library &
DirectoryScanner::getOrCreateLibrary(StringRef Path,
                                     std::vector<Library> &Libs) const {
  if (Path.consume_front(RootPath) && Path.empty())
    Path = "/";

  auto LibIt =
      find_if(Libs, [Path](const Library &L) { return L.getPath() == Path; });
  if (LibIt != Libs.end())
    return *LibIt;

  Libs.emplace_back(Path);
  return Libs.back();
}

Error DirectoryScanner::scanHeaders(StringRef Path, Library &Lib,
                                    HeaderType Type, StringRef BasePath,
                                    StringRef ParentPath) const {
  std::error_code ec;
  auto &FS = FM.getVirtualFileSystem();
  PathSeq SubDirectories;
  for (vfs::directory_iterator i = FS.dir_begin(Path, ec), ie; i != ie;
       i.increment(ec)) {
    StringRef HeaderPath = i->path();
    if (ec)
      return createStringError(ec, "unable to read: " + HeaderPath);

    if (sys::fs::is_symlink_file(HeaderPath))
      continue;

    // Ignore tmp files from unifdef.
    const StringRef Filename = sys::path::filename(HeaderPath);
    if (Filename.starts_with("."))
      continue;

    // If it is a directory, remember the subdirectory.
    if (FM.getOptionalDirectoryRef(HeaderPath))
      SubDirectories.push_back(HeaderPath.str());

    if (!isHeaderFile(HeaderPath))
      continue;

    // Skip files that do not exist. This usually happens for broken symlinks.
    if (FS.status(HeaderPath) == std::errc::no_such_file_or_directory)
      continue;

    auto IncludeName = createIncludeHeaderName(HeaderPath);
    Lib.addHeaderFile(HeaderPath, Type,
                      IncludeName.has_value() ? IncludeName.value() : "");
  }

  // Go through the subdirectories.
  // Sort the sub-directory first since different file systems might have
  // different traverse order.
  llvm::sort(SubDirectories);
  if (ParentPath.empty())
    ParentPath = Path;
  for (const StringRef Dir : SubDirectories)
    return scanHeaders(Dir, Lib, Type, BasePath, ParentPath);

  return Error::success();
}

llvm::Error
DirectoryScanner::scanMultipleFrameworks(StringRef Directory,
                                         std::vector<Library> &Libs) const {
  std::error_code ec;
  auto &FS = FM.getVirtualFileSystem();
  for (vfs::directory_iterator i = FS.dir_begin(Directory, ec), ie; i != ie;
       i.increment(ec)) {
    StringRef Curr = i->path();

    // Skip files that do not exist. This usually happens for broken symlinks.
    if (ec == std::errc::no_such_file_or_directory) {
      ec.clear();
      continue;
    }
    if (ec)
      return createStringError(ec, Curr);

    if (sys::fs::is_symlink_file(Curr))
      continue;

    if (isFramework(Curr)) {
      if (!FM.getOptionalDirectoryRef(Curr))
        continue;
      Library &Framework = getOrCreateLibrary(Curr, Libs);
      if (Error Err = scanFrameworkDirectory(Curr, Framework))
        return Err;
    }
  }

  return Error::success();
}

llvm::Error
DirectoryScanner::scanSubFrameworksDirectory(StringRef Directory,
                                             std::vector<Library> &Libs) const {
  if (FM.getOptionalDirectoryRef(Directory))
    return scanMultipleFrameworks(Directory, Libs);

  std::error_code ec = std::make_error_code(std::errc::not_a_directory);
  return createStringError(ec, Directory);
}

/// FIXME: How to handle versions? For now scan them separately as independent
/// frameworks.
llvm::Error
DirectoryScanner::scanFrameworkVersionsDirectory(StringRef Path,
                                                 Library &Lib) const {
  std::error_code ec;
  auto &FS = FM.getVirtualFileSystem();
  for (vfs::directory_iterator i = FS.dir_begin(Path, ec), ie; i != ie;
       i.increment(ec)) {
    const StringRef Curr = i->path();

    // Skip files that do not exist. This usually happens for broken symlinks.
    if (ec == std::errc::no_such_file_or_directory) {
      ec.clear();
      continue;
    }
    if (ec)
      return createStringError(ec, Curr);

    if (sys::fs::is_symlink_file(Curr))
      continue;

    // Each version should be a framework directory.
    if (!FM.getOptionalDirectoryRef(Curr))
      continue;

    Library &VersionedFramework =
        getOrCreateLibrary(Curr, Lib.FrameworkVersions);
    if (Error Err = scanFrameworkDirectory(Curr, VersionedFramework))
      return Err;
  }

  return Error::success();
}

llvm::Error DirectoryScanner::scanFrameworkDirectory(StringRef Path,
                                                     Library &Framework) const {
  // If the framework is inside Kernel or IOKit, scan headers in the different
  // directories separately.
  Framework.IsUnwrappedDylib =
      Path.contains("Kernel.framework") || Path.contains("IOKit.framework");

  // Unfortunately we cannot identify symlinks in the VFS. We assume that if
  // there is a Versions directory, then we have symlinks and directly proceed
  // to the Versions folder.
  std::error_code ec;
  auto &FS = FM.getVirtualFileSystem();

  for (vfs::directory_iterator i = FS.dir_begin(Path, ec), ie; i != ie;
       i.increment(ec)) {
    StringRef Curr = i->path();
    // Skip files that do not exist. This usually happens for broken symlinks.
    if (ec == std::errc::no_such_file_or_directory) {
      ec.clear();
      continue;
    }

    if (ec)
      return createStringError(ec, Curr);

    if (sys::fs::is_symlink_file(Curr))
      continue;

    StringRef FileName = sys::path::filename(Curr);
    // Scan all "public" headers.
    if (FileName.contains("Headers")) {
      if (Error Err = scanHeaders(Curr, Framework, HeaderType::Public, Curr))
        return Err;
      continue;
    }
    // Scan all "private" headers.
    if (FileName.contains("PrivateHeaders")) {
      if (Error Err = scanHeaders(Curr, Framework, HeaderType::Private, Curr))
        return Err;
      continue;
    }
    // Scan sub frameworks.
    if (FileName.contains("Frameworks")) {
      if (Error Err = scanSubFrameworksDirectory(Curr, Framework.SubFrameworks))
        return Err;
      continue;
    }
    // Check for versioned frameworks.
    if (FileName.contains("Versions")) {
      if (Error Err = scanFrameworkVersionsDirectory(Curr, Framework))
        return Err;
      continue;
    }
  }

  return Error::success();
}

llvm::Error DirectoryScanner::scanForFrameworks(StringRef Directory) {
  RootPath = "";

  // Expect a certain directory structure and naming convention to find
  // frameworks.
  static const char *SubDirectories[] = {"System/Library/Frameworks/",
                                         "System/Library/PrivateFrameworks/"};

  // Check if the directory is already a framework.
  if (isFramework(Directory)) {
    Library &Framework = getOrCreateLibrary(Directory, Libraries);
    if (Error Err = scanFrameworkDirectory(Directory, Framework))
      return Err;
    return Error::success();
  }

  // Check known sub-directory locations.
  for (const auto *SubDir : SubDirectories) {
    SmallString<PATH_MAX> Path(Directory);
    sys::path::append(Path, SubDir);

    if (Error Err = scanMultipleFrameworks(Path, Libraries))
      return Err;
  }

  return Error::success();
}
} // namespace clang::installapi
