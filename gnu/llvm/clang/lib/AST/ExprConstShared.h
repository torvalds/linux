//===--- ExprConstShared.h - Shared consetxpr functionality ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared functionality between the new constant expression
// interpreter (AST/Interp/) and the current one (ExprConstant.cpp).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_AST_EXPRCONSTSHARED_H
#define LLVM_CLANG_LIB_AST_EXPRCONSTSHARED_H

namespace llvm {
class APFloat;
}
namespace clang {
class QualType;
class LangOptions;
} // namespace clang
using namespace clang;
/// Values returned by __builtin_classify_type, chosen to match the values
/// produced by GCC's builtin.
enum class GCCTypeClass {
  None = -1,
  Void = 0,
  Integer = 1,
  // GCC reserves 2 for character types, but instead classifies them as
  // integers.
  Enum = 3,
  Bool = 4,
  Pointer = 5,
  // GCC reserves 6 for references, but appears to never use it (because
  // expressions never have reference type, presumably).
  PointerToDataMember = 7,
  RealFloat = 8,
  Complex = 9,
  // GCC reserves 10 for functions, but does not use it since GCC version 6 due
  // to decay to pointer. (Prior to version 6 it was only used in C++ mode).
  // GCC claims to reserve 11 for pointers to member functions, but *actually*
  // uses 12 for that purpose, same as for a class or struct. Maybe it
  // internally implements a pointer to member as a struct?  Who knows.
  PointerToMemberFunction = 12, // Not a bug, see above.
  ClassOrStruct = 12,
  Union = 13,
  // GCC reserves 14 for arrays, but does not use it since GCC version 6 due to
  // decay to pointer. (Prior to version 6 it was only used in C++ mode).
  // GCC reserves 15 for strings, but actually uses 5 (pointer) for string
  // literals.
  // Lang = 16,
  // OpaqueType = 17,
  BitInt = 18,
  Vector = 19
};

GCCTypeClass EvaluateBuiltinClassifyType(QualType T,
                                         const LangOptions &LangOpts);

void HandleComplexComplexMul(llvm::APFloat A, llvm::APFloat B, llvm::APFloat C,
                             llvm::APFloat D, llvm::APFloat &ResR,
                             llvm::APFloat &ResI);
void HandleComplexComplexDiv(llvm::APFloat A, llvm::APFloat B, llvm::APFloat C,
                             llvm::APFloat D, llvm::APFloat &ResR,
                             llvm::APFloat &ResI);

#endif
