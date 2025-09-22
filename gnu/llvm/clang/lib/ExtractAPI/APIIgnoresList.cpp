//===- ExtractAPI/APIIgnoresList.cpp -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements APIIgnoresList that allows users to specifiy a file
/// containing symbols to ignore during API extraction.
///
//===----------------------------------------------------------------------===//

#include "clang/ExtractAPI/APIIgnoresList.h"
#include "clang/Basic/FileManager.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"

using namespace clang;
using namespace clang::extractapi;
using namespace llvm;

char IgnoresFileNotFound::ID;

void IgnoresFileNotFound::log(llvm::raw_ostream &os) const {
  os << "Could not find API ignores file " << Path;
}

std::error_code IgnoresFileNotFound::convertToErrorCode() const {
  return llvm::inconvertibleErrorCode();
}

Expected<APIIgnoresList>
APIIgnoresList::create(const FilePathList &IgnoresFilePathList,
                       FileManager &FM) {
  SmallVector<StringRef, 32> Lines;
  BufferList symbolBufferList;

  for (const auto &CurrentIgnoresFilePath : IgnoresFilePathList) {
    auto BufferOrErr = FM.getBufferForFile(CurrentIgnoresFilePath);

    if (!BufferOrErr)
      return make_error<IgnoresFileNotFound>(CurrentIgnoresFilePath);

    auto Buffer = std::move(BufferOrErr.get());
    Buffer->getBuffer().split(Lines, '\n', /*MaxSplit*/ -1,
                              /*KeepEmpty*/ false);
    symbolBufferList.push_back(std::move(Buffer));
  }

  // Symbol names don't have spaces in them, let's just remove these in case
  // the input is slighlty malformed.
  transform(Lines, Lines.begin(), [](StringRef Line) { return Line.trim(); });
  sort(Lines);
  return APIIgnoresList(std::move(Lines), std::move(symbolBufferList));
}

bool APIIgnoresList::shouldIgnore(StringRef SymbolName) const {
  auto It = lower_bound(SymbolsToIgnore, SymbolName);
  return (It != SymbolsToIgnore.end()) && (*It == SymbolName);
}
