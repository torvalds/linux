//===------ EvaluationResult.h - Result class  for the VM -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_EVALUATION_RESULT_H
#define LLVM_CLANG_AST_INTERP_EVALUATION_RESULT_H

#include "FunctionPointer.h"
#include "Pointer.h"
#include "clang/AST/APValue.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include <optional>
#include <variant>

namespace clang {
namespace interp {
class EvalEmitter;
class Context;

/// Defines the result of an evaluation.
///
/// The result might be in different forms--one of the pointer types,
/// an APValue, or nothing.
///
/// We use this class to inspect and diagnose the result, as well as
/// convert it to the requested form.
class EvaluationResult final {
public:
  enum ResultKind {
    Empty,   // Initial state.
    LValue,  // Result is an lvalue/pointer.
    RValue,  // Result is an rvalue.
    Invalid, // Result is invalid.
    Valid,   // Result is valid and empty.
  };

  using DeclTy = llvm::PointerUnion<const Decl *, const Expr *>;

private:
  const Context *Ctx = nullptr;
  std::variant<std::monostate, Pointer, FunctionPointer, APValue> Value;
  ResultKind Kind = Empty;
  DeclTy Source = nullptr; // Currently only needed for dump().

  EvaluationResult(ResultKind Kind) : Kind(Kind) {
    // Leave everything empty. Can be used as an
    // error marker or for void return values.
    assert(Kind == Valid || Kind == Invalid);
  }

  void setSource(DeclTy D) { Source = D; }

  void setValue(const APValue &V) {
    // V could still be an LValue.
    assert(empty());
    Value = std::move(V);
    Kind = RValue;
  }
  void setPointer(const Pointer P) {
    assert(empty());
    Value = P;
    Kind = LValue;
  }
  void setFunctionPointer(const FunctionPointer &P) {
    assert(empty());
    Value = P;
    Kind = LValue;
  }
  void setInvalid() {
    // We are NOT asserting empty() here, since setting it to invalid
    // is allowed even if there is already a result.
    Kind = Invalid;
  }
  void setValid() {
    assert(empty());
    Kind = Valid;
  }

public:
  EvaluationResult(const Context *Ctx) : Ctx(Ctx) {}

  bool empty() const { return Kind == Empty; }
  bool isInvalid() const { return Kind == Invalid; }
  bool isLValue() const { return Kind == LValue; }
  bool isRValue() const { return Kind == RValue; }

  /// Returns an APValue for the evaluation result. The returned
  /// APValue might be an LValue or RValue.
  APValue toAPValue() const;

  /// If the result is an LValue, convert that to an RValue
  /// and return it. This may fail, e.g. if the result is an
  /// LValue and we can't read from it.
  std::optional<APValue> toRValue() const;

  /// Check that all subobjects of the given pointer have been initialized.
  bool checkFullyInitialized(InterpState &S, const Pointer &Ptr) const;
  /// Check that none of the blocks the given pointer (transitively) points
  /// to are dynamically allocated.
  bool checkReturnValue(InterpState &S, const Context &Ctx, const Pointer &Ptr,
                        const SourceInfo &Info);

  QualType getSourceType() const {
    if (const auto *D =
            dyn_cast_if_present<ValueDecl>(Source.dyn_cast<const Decl *>()))
      return D->getType();
    else if (const auto *E = Source.dyn_cast<const Expr *>())
      return E->getType();
    return QualType();
  }

  /// Dump to stderr.
  void dump() const;

  friend class EvalEmitter;
  friend class InterpState;
};

} // namespace interp
} // namespace clang

#endif
