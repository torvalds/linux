//===--- FunctionPointer.h - Types for the constexpr VM ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_FUNCTION_POINTER_H
#define LLVM_CLANG_AST_INTERP_FUNCTION_POINTER_H

#include "Function.h"
#include "Primitives.h"
#include "clang/AST/APValue.h"

namespace clang {
class ASTContext;
namespace interp {

class FunctionPointer final {
private:
  const Function *Func;
  bool Valid;

public:
  FunctionPointer(const Function *Func) : Func(Func), Valid(true) {
    assert(Func);
  }

  FunctionPointer(uintptr_t IntVal = 0, const Descriptor *Desc = nullptr)
      : Func(reinterpret_cast<const Function *>(IntVal)), Valid(false) {}

  const Function *getFunction() const { return Func; }
  bool isZero() const { return !Func; }
  bool isValid() const { return Valid; }
  bool isWeak() const {
    if (!Func || !Valid)
      return false;

    return Func->getDecl()->isWeak();
  }

  APValue toAPValue(const ASTContext &) const {
    if (!Func)
      return APValue(static_cast<Expr *>(nullptr), CharUnits::Zero(), {},
                     /*OnePastTheEnd=*/false, /*IsNull=*/true);

    if (!Valid)
      return APValue(static_cast<Expr *>(nullptr),
                     CharUnits::fromQuantity(getIntegerRepresentation()), {},
                     /*OnePastTheEnd=*/false, /*IsNull=*/false);

    return APValue(Func->getDecl(), CharUnits::Zero(), {},
                   /*OnePastTheEnd=*/false, /*IsNull=*/false);
  }

  void print(llvm::raw_ostream &OS) const {
    OS << "FnPtr(";
    if (Func && Valid)
      OS << Func->getName();
    else if (Func)
      OS << reinterpret_cast<uintptr_t>(Func);
    else
      OS << "nullptr";
    OS << ")";
  }

  std::string toDiagnosticString(const ASTContext &Ctx) const {
    if (!Func)
      return "nullptr";

    return toAPValue(Ctx).getAsString(Ctx, Func->getDecl()->getType());
  }

  uint64_t getIntegerRepresentation() const {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Func));
  }

  ComparisonCategoryResult compare(const FunctionPointer &RHS) const {
    if (Func == RHS.Func)
      return ComparisonCategoryResult::Equal;
    return ComparisonCategoryResult::Unordered;
  }
};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                     FunctionPointer FP) {
  FP.print(OS);
  return OS;
}

} // namespace interp
} // namespace clang

#endif
