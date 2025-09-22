//===--- APNumericStorage.h - Store APInt/APFloat in ASTContext -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_APNUMERICSTORAGE_H
#define LLVM_CLANG_AST_APNUMERICSTORAGE_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"

namespace clang {
class ASTContext;

/// Used by IntegerLiteral/FloatingLiteral/EnumConstantDecl to store the
/// numeric without leaking memory.
///
/// For large floats/integers, APFloat/APInt will allocate memory from the heap
/// to represent these numbers.  Unfortunately, when we use a BumpPtrAllocator
/// to allocate IntegerLiteral/FloatingLiteral nodes the memory associated with
/// the APFloat/APInt values will never get freed. APNumericStorage uses
/// ASTContext's allocator for memory allocation.
class APNumericStorage {
  union {
    uint64_t VAL;   ///< Used to store the <= 64 bits integer value.
    uint64_t *pVal; ///< Used to store the >64 bits integer value.
  };
  unsigned BitWidth;

  bool hasAllocation() const { return llvm::APInt::getNumWords(BitWidth) > 1; }

  APNumericStorage(const APNumericStorage &) = delete;
  void operator=(const APNumericStorage &) = delete;

protected:
  APNumericStorage() : VAL(0), BitWidth(0) {}

  llvm::APInt getIntValue() const {
    unsigned NumWords = llvm::APInt::getNumWords(BitWidth);
    if (NumWords > 1)
      return llvm::APInt(BitWidth, NumWords, pVal);
    else
      return llvm::APInt(BitWidth, VAL);
  }
  void setIntValue(const ASTContext &C, const llvm::APInt &Val);
};

class APIntStorage : private APNumericStorage {
public:
  llvm::APInt getValue() const { return getIntValue(); }
  void setValue(const ASTContext &C, const llvm::APInt &Val) {
    setIntValue(C, Val);
  }
};

class APFloatStorage : private APNumericStorage {
public:
  llvm::APFloat getValue(const llvm::fltSemantics &Semantics) const {
    return llvm::APFloat(Semantics, getIntValue());
  }
  void setValue(const ASTContext &C, const llvm::APFloat &Val) {
    setIntValue(C, Val.bitcastToAPInt());
  }
};

} // end namespace clang

#endif // LLVM_CLANG_AST_APNUMERICSTORAGE_H
