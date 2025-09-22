//===- ExtractAPI/APIIgnoresList.h ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file defines APIIgnoresList which is a type that allows querying
/// files containing symbols to ignore when extracting API information.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_API_IGNORES_LIST_H
#define LLVM_CLANG_API_IGNORES_LIST_H

#include "clang/Basic/FileManager.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <system_error>

namespace llvm {
class MemoryBuffer;
} // namespace llvm

namespace clang {
namespace extractapi {

struct IgnoresFileNotFound : public llvm::ErrorInfo<IgnoresFileNotFound> {
  std::string Path;
  static char ID;

  explicit IgnoresFileNotFound(StringRef Path) : Path(Path) {}

  virtual void log(llvm::raw_ostream &os) const override;

  virtual std::error_code convertToErrorCode() const override;
};

/// A type that provides access to a new line separated list of symbol names to
/// ignore when extracting API information.
struct APIIgnoresList {
  using FilePathList = std::vector<std::string>;

  /// The API to use for generating from the files at \p IgnoresFilePathList.
  ///
  /// \returns an initialized APIIgnoresList or an Error.
  static llvm::Expected<APIIgnoresList>
  create(const FilePathList &IgnoresFilePathList, FileManager &FM);

  APIIgnoresList() = default;

  /// Check if \p SymbolName is specified in the APIIgnoresList and if it should
  /// therefore be ignored.
  bool shouldIgnore(llvm::StringRef SymbolName) const;

private:
  using SymbolNameList = llvm::SmallVector<llvm::StringRef, 32>;
  using BufferList = llvm::SmallVector<std::unique_ptr<llvm::MemoryBuffer>>;

  APIIgnoresList(SymbolNameList SymbolsToIgnore, BufferList Buffers)
      : SymbolsToIgnore(std::move(SymbolsToIgnore)),
        Buffers(std::move(Buffers)) {}

  SymbolNameList SymbolsToIgnore;
  BufferList Buffers;
};

} // namespace extractapi
} // namespace clang

#endif // LLVM_CLANG_API_IGNORES_LIST_H
