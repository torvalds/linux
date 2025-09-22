//===- InstallAPI/DirectoryScanner.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// The DirectoryScanner for collecting library files on the file system.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_INSTALLAPI_DIRECTORYSCANNER_H
#define LLVM_CLANG_INSTALLAPI_DIRECTORYSCANNER_H

#include "clang/Basic/FileManager.h"
#include "clang/InstallAPI/Library.h"

namespace clang::installapi {

enum ScanMode {
  /// Scanning Framework directory.
  ScanFrameworks,
  /// Scanning Dylib directory.
  ScanDylibs,
};

class DirectoryScanner {
public:
  DirectoryScanner(FileManager &FM, ScanMode Mode = ScanMode::ScanFrameworks)
      : FM(FM), Mode(Mode) {}

  /// Scan for all input files throughout directory.
  ///
  /// \param Directory Path of input directory.
  llvm::Error scan(StringRef Directory);

  /// Take over ownership of stored libraries.
  std::vector<Library> takeLibraries() { return std::move(Libraries); };

  /// Get all the header files in libraries.
  ///
  /// \param Libraries Reference of collection of libraries.
  static HeaderSeq getHeaders(ArrayRef<Library> Libraries);

private:
  /// Collect files for dylibs in usr/(local)/lib within directory.
  llvm::Error scanForUnwrappedLibraries(StringRef Directory);

  /// Collect files for any frameworks within directory.
  llvm::Error scanForFrameworks(StringRef Directory);

  /// Get a library from the libraries collection.
  Library &getOrCreateLibrary(StringRef Path, std::vector<Library> &Libs) const;

  /// Collect multiple frameworks from directory.
  llvm::Error scanMultipleFrameworks(StringRef Directory,
                                     std::vector<Library> &Libs) const;
  /// Collect files from nested frameworks.
  llvm::Error scanSubFrameworksDirectory(StringRef Directory,
                                         std::vector<Library> &Libs) const;

  /// Collect files from framework path.
  llvm::Error scanFrameworkDirectory(StringRef Path, Library &Framework) const;

  /// Collect header files from path.
  llvm::Error scanHeaders(StringRef Path, Library &Lib, HeaderType Type,
                          StringRef BasePath,
                          StringRef ParentPath = StringRef()) const;

  /// Collect files from Version directories inside Framework directories.
  llvm::Error scanFrameworkVersionsDirectory(StringRef Path,
                                             Library &Lib) const;
  FileManager &FM;
  ScanMode Mode;
  StringRef RootPath;
  std::vector<Library> Libraries;
};

} // namespace clang::installapi

#endif // LLVM_CLANG_INSTALLAPI_DIRECTORYSCANNER_H
