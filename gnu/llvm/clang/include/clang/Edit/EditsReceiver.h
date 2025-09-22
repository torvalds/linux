//===- EditedSource.h - Collection of source edits --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EDIT_EDITSRECEIVER_H
#define LLVM_CLANG_EDIT_EDITSRECEIVER_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace edit {

class EditsReceiver {
public:
  virtual ~EditsReceiver() = default;

  virtual void insert(SourceLocation loc, StringRef text) = 0;
  virtual void replace(CharSourceRange range, StringRef text) = 0;

  /// By default it calls replace with an empty string.
  virtual void remove(CharSourceRange range);
};

} // namespace edit
} // namespace clang

#endif // LLVM_CLANG_EDIT_EDITSRECEIVER_H
