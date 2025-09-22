//===- InstallAPI/Context.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INSTALLAPI_CONTEXT_H
#define LLVM_CLANG_INSTALLAPI_CONTEXT_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/InstallAPI/DylibVerifier.h"
#include "clang/InstallAPI/HeaderFile.h"
#include "clang/InstallAPI/MachO.h"
#include "llvm/ADT/DenseMap.h"

namespace clang {
namespace installapi {
class FrontendRecordsSlice;

/// Struct used for generating validating InstallAPI.
/// The attributes captured represent all necessary information
/// to generate TextAPI output.
struct InstallAPIContext {

  /// Library attributes that are typically passed as linker inputs.
  BinaryAttrs BA;

  /// Install names of reexported libraries of a library.
  LibAttrs Reexports;

  /// All headers that represent a library.
  HeaderSeq InputHeaders;

  /// Active language mode to parse in.
  Language LangMode = Language::ObjC;

  /// Active header access type.
  HeaderType Type = HeaderType::Unknown;

  /// Active TargetSlice for symbol record collection.
  std::shared_ptr<FrontendRecordsSlice> Slice;

  /// FileManager for all I/O operations.
  FileManager *FM = nullptr;

  /// DiagnosticsEngine for all error reporting.
  DiagnosticsEngine *Diags = nullptr;

  /// Verifier when binary dylib is passed as input.
  std::unique_ptr<DylibVerifier> Verifier = nullptr;

  /// File Path of output location.
  llvm::StringRef OutputLoc{};

  /// What encoding to write output as.
  FileType FT = FileType::TBD_V5;

  /// Populate entries of headers that should be included for TextAPI
  /// generation.
  void addKnownHeader(const HeaderFile &H);

  /// Record visited files during frontend actions to determine whether to
  /// include their declarations for TextAPI generation.
  ///
  /// \param FE Header that is being parsed.
  /// \param PP Preprocesser used for querying how header was imported.
  /// \return Access level of header if it should be included for TextAPI
  /// generation.
  std::optional<HeaderType> findAndRecordFile(const FileEntry *FE,
                                              const Preprocessor &PP);

private:
  using HeaderMap = llvm::DenseMap<const FileEntry *, HeaderType>;

  // Collection of parsed header files and their access level. If set to
  // HeaderType::Unknown, they are not used for TextAPI generation.
  HeaderMap KnownFiles;

  // Collection of expected header includes and the access level for them.
  llvm::DenseMap<StringRef, HeaderType> KnownIncludes;
};

/// Lookup the dylib or TextAPI file location for a system library or framework.
/// The search paths provided are searched in order.
/// @rpath based libraries are not supported.
///
/// \param InstallName The install name for the library.
/// \param FrameworkSearchPaths Search paths to look up frameworks with.
/// \param LibrarySearchPaths Search paths to look up dylibs with.
/// \param SearchPaths Fallback search paths if library was not found in earlier
/// paths.
/// \return The full path of the library.
std::string findLibrary(StringRef InstallName, FileManager &FM,
                        ArrayRef<std::string> FrameworkSearchPaths,
                        ArrayRef<std::string> LibrarySearchPaths,
                        ArrayRef<std::string> SearchPaths);
} // namespace installapi
} // namespace clang

#endif // LLVM_CLANG_INSTALLAPI_CONTEXT_H
