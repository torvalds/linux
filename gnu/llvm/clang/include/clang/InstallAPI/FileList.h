//===- InstallAPI/FileList.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// The JSON file list parser is used to communicate input to InstallAPI.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INSTALLAPI_FILELIST_H
#define LLVM_CLANG_INSTALLAPI_FILELIST_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/InstallAPI/HeaderFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

namespace clang {
namespace installapi {

class FileListReader {
public:
  /// Decode JSON input and append header input into destination container.
  /// Headers are loaded in the order they appear in the JSON input.
  ///
  /// \param InputBuffer JSON input data.
  /// \param Destination Container to load headers into.
  /// \param FM Optional File Manager to validate input files exist.
  static llvm::Error
  loadHeaders(std::unique_ptr<llvm::MemoryBuffer> InputBuffer,
              HeaderSeq &Destination, clang::FileManager *FM = nullptr);

  FileListReader() = delete;
};

} // namespace installapi
} // namespace clang

#endif // LLVM_CLANG_INSTALLAPI_FILELIST_H
