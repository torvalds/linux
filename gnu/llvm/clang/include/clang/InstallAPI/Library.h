//===- InstallAPI/Library.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Defines the content of a library, such as public and private
/// header files, and whether it is a framework.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_INSTALLAPI_LIBRARY_H
#define LLVM_CLANG_INSTALLAPI_LIBRARY_H

#include "clang/InstallAPI/HeaderFile.h"
#include "clang/InstallAPI/MachO.h"

namespace clang::installapi {

class Library {
public:
  Library(StringRef Directory) : BaseDirectory(Directory) {}

  /// Capture the name of the framework by the install name.
  ///
  /// \param InstallName The install name of the library encoded in a dynamic
  /// library.
  static StringRef getFrameworkNameFromInstallName(StringRef InstallName);

  /// Get name of library by the discovered file path.
  StringRef getName() const;

  /// Get discovered path of library.
  StringRef getPath() const { return BaseDirectory; }

  /// Add a header file that belongs to the library.
  ///
  /// \param FullPath Path to header file.
  /// \param Type Access level of header.
  /// \param IncludePath The way the header should be included.
  void addHeaderFile(StringRef FullPath, HeaderType Type,
                     StringRef IncludePath = StringRef()) {
    Headers.emplace_back(FullPath, Type, IncludePath);
  }

  /// Determine if library is empty.
  bool empty() {
    return SubFrameworks.empty() && Headers.empty() &&
           FrameworkVersions.empty();
  }

private:
  std::string BaseDirectory;
  HeaderSeq Headers;
  std::vector<Library> SubFrameworks;
  std::vector<Library> FrameworkVersions;
  bool IsUnwrappedDylib{false};

  friend class DirectoryScanner;
};

} // namespace clang::installapi

#endif // LLVM_CLANG_INSTALLAPI_LIBRARY_H
