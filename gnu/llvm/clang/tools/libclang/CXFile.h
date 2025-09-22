//===- CXFile.h - Routines for manipulating CXFile --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_CXFILE_H
#define LLVM_CLANG_TOOLS_LIBCLANG_CXFILE_H

#include "clang-c/CXFile.h"
#include "clang/Basic/FileEntry.h"

namespace clang {
namespace cxfile {
inline CXFile makeCXFile(OptionalFileEntryRef FE) {
  return CXFile(FE ? const_cast<FileEntryRef::MapEntry *>(&FE->getMapEntry())
                   : nullptr);
}

inline OptionalFileEntryRef getFileEntryRef(CXFile File) {
  if (!File)
    return std::nullopt;
  return FileEntryRef(*reinterpret_cast<const FileEntryRef::MapEntry *>(File));
}
} // namespace cxfile
} // namespace clang

#endif
