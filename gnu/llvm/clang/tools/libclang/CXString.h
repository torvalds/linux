//===- CXString.h - Routines for manipulating CXStrings -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines routines for manipulating CXStrings.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_CXSTRING_H
#define LLVM_CLANG_TOOLS_LIBCLANG_CXSTRING_H

#include "clang-c/Index.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <string>
#include <vector>

namespace clang {
namespace cxstring {

struct CXStringBuf;

/// Create a CXString object for an empty "" string.
CXString createEmpty();

/// Create a CXString object for an NULL string.
///
/// A NULL string should be used as an "invalid" value in case of errors.
CXString createNull();

/// Create a CXString object from a nul-terminated C string.  New
/// CXString may contain a pointer to \p String.
///
/// \p String should not be changed by the caller afterwards.
CXString createRef(const char *String);

/// Create a CXString object from a nul-terminated C string.  New
/// CXString will contain a copy of \p String.
///
/// \p String can be changed or freed by the caller.
CXString createDup(const char *String);

/// Create a CXString object from a StringRef.  New CXString may
/// contain a pointer to the undrelying data of \p String.
///
/// \p String should not be changed by the caller afterwards.
CXString createRef(StringRef String);

/// Create a CXString object from a StringRef.  New CXString will
/// contain a copy of \p String.
///
/// \p String can be changed or freed by the caller.
CXString createDup(StringRef String);

// Usually std::string is intended to be used as backing storage for CXString.
// In this case, call \c createRef(String.c_str()).
//
// If you need to make a copy, call \c createDup(StringRef(String)).
CXString createRef(std::string String) = delete;

/// Create a CXString object that is backed by a string buffer.
CXString createCXString(CXStringBuf *buf);

CXStringSet *createSet(const std::vector<std::string> &Strings);

/// A string pool used for fast allocation/deallocation of strings.
class CXStringPool {
public:
  ~CXStringPool();

  CXStringBuf *getCXStringBuf(CXTranslationUnit TU);

private:
  std::vector<CXStringBuf *> Pool;

  friend struct CXStringBuf;
};

struct CXStringBuf {
  SmallString<128> Data;
  CXTranslationUnit TU;

  CXStringBuf(CXTranslationUnit TU) : TU(TU) {}

  /// Return this buffer to the pool.
  void dispose();
};

CXStringBuf *getCXStringBuf(CXTranslationUnit TU);

/// Returns true if the CXString data is managed by a pool.
bool isManagedByPool(CXString str);

}

static inline StringRef getContents(const CXUnsavedFile &UF) {
  return StringRef(UF.Contents, UF.Length);
}
}

#endif

