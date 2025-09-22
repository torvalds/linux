//===-- OpDescriptor.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provides the fuzzerop::Descriptor class and related tools for describing
// operations an IR fuzzer can work with.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_OPDESCRIPTOR_H
#define LLVM_FUZZMUTATE_OPDESCRIPTOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include <functional>

namespace llvm {
class Instruction;
namespace fuzzerop {

/// @{
/// Populate a small list of potentially interesting constants of a given type.
void makeConstantsWithType(Type *T, std::vector<Constant *> &Cs);
std::vector<Constant *> makeConstantsWithType(Type *T);
/// @}

/// A matcher/generator for finding suitable values for the next source in an
/// operation's partially completed argument list.
///
/// Given that we're building some operation X and may have already filled some
/// subset of its operands, this predicate determines if some value New is
/// suitable for the next operand or generates a set of values that are
/// suitable.
class SourcePred {
public:
  /// Given a list of already selected operands, returns whether a given new
  /// operand is suitable for the next operand.
  using PredT = std::function<bool(ArrayRef<Value *> Cur, const Value *New)>;
  /// Given a list of already selected operands and a set of valid base types
  /// for a fuzzer, generates a list of constants that could be used for the
  /// next operand.
  using MakeT = std::function<std::vector<Constant *>(
      ArrayRef<Value *> Cur, ArrayRef<Type *> BaseTypes)>;

private:
  PredT Pred;
  MakeT Make;

public:
  /// Create a fully general source predicate.
  SourcePred(PredT Pred, MakeT Make) : Pred(Pred), Make(Make) {}
  SourcePred(PredT Pred, std::nullopt_t) : Pred(Pred) {
    Make = [Pred](ArrayRef<Value *> Cur, ArrayRef<Type *> BaseTypes) {
      // Default filter just calls Pred on each of the base types.
      std::vector<Constant *> Result;
      for (Type *T : BaseTypes) {
        Constant *V = UndefValue::get(T);
        if (Pred(Cur, V))
          makeConstantsWithType(T, Result);
      }
      if (Result.empty())
        report_fatal_error("Predicate does not match for base types");
      return Result;
    };
  }

  /// Returns true if \c New is compatible for the argument after \c Cur
  bool matches(ArrayRef<Value *> Cur, const Value *New) {
    return Pred(Cur, New);
  }

  /// Generates a list of potential values for the argument after \c Cur.
  std::vector<Constant *> generate(ArrayRef<Value *> Cur,
                                   ArrayRef<Type *> BaseTypes) {
    return Make(Cur, BaseTypes);
  }
};

/// A description of some operation we can build while fuzzing IR.
struct OpDescriptor {
  unsigned Weight;
  SmallVector<SourcePred, 2> SourcePreds;
  std::function<Value *(ArrayRef<Value *>, Instruction *)> BuilderFunc;
};

static inline SourcePred onlyType(Type *Only) {
  auto Pred = [Only](ArrayRef<Value *>, const Value *V) {
    return V->getType() == Only;
  };
  auto Make = [Only](ArrayRef<Value *>, ArrayRef<Type *>) {
    return makeConstantsWithType(Only);
  };
  return {Pred, Make};
}

static inline SourcePred anyType() {
  auto Pred = [](ArrayRef<Value *>, const Value *V) {
    return !V->getType()->isVoidTy();
  };
  auto Make = std::nullopt;
  return {Pred, Make};
}

static inline SourcePred anyIntType() {
  auto Pred = [](ArrayRef<Value *>, const Value *V) {
    return V->getType()->isIntegerTy();
  };
  auto Make = std::nullopt;
  return {Pred, Make};
}

static inline SourcePred anyIntOrVecIntType() {
  auto Pred = [](ArrayRef<Value *>, const Value *V) {
    return V->getType()->isIntOrIntVectorTy();
  };
  return {Pred, std::nullopt};
}

static inline SourcePred boolOrVecBoolType() {
  auto Pred = [](ArrayRef<Value *>, const Value *V) {
    return V->getType()->isIntOrIntVectorTy(1);
  };
  return {Pred, std::nullopt};
}

static inline SourcePred anyFloatType() {
  auto Pred = [](ArrayRef<Value *>, const Value *V) {
    return V->getType()->isFloatingPointTy();
  };
  auto Make = std::nullopt;
  return {Pred, Make};
}

static inline SourcePred anyFloatOrVecFloatType() {
  auto Pred = [](ArrayRef<Value *>, const Value *V) {
    return V->getType()->isFPOrFPVectorTy();
  };
  return {Pred, std::nullopt};
}

static inline SourcePred anyPtrType() {
  auto Pred = [](ArrayRef<Value *>, const Value *V) {
    return V->getType()->isPointerTy() && !V->isSwiftError();
  };
  auto Make = [](ArrayRef<Value *>, ArrayRef<Type *> Ts) {
    std::vector<Constant *> Result;
    // TODO: Should these point at something?
    for (Type *T : Ts)
      Result.push_back(UndefValue::get(PointerType::getUnqual(T)));
    return Result;
  };
  return {Pred, Make};
}

static inline SourcePred sizedPtrType() {
  auto Pred = [](ArrayRef<Value *>, const Value *V) {
    if (V->isSwiftError())
      return false;

    return V->getType()->isPointerTy();
  };
  auto Make = [](ArrayRef<Value *>, ArrayRef<Type *> Ts) {
    std::vector<Constant *> Result;

    // TODO: This doesn't really make sense with opaque pointers,
    // as the pointer type will always be the same.
    for (Type *T : Ts)
      if (T->isSized())
        Result.push_back(UndefValue::get(PointerType::getUnqual(T)));

    return Result;
  };
  return {Pred, Make};
}

static inline SourcePred matchFirstLengthWAnyType() {
  auto Pred = [](ArrayRef<Value *> Cur, const Value *V) {
    assert(!Cur.empty() && "No first source yet");
    Type *This = V->getType(), *First = Cur[0]->getType();
    VectorType *ThisVec = dyn_cast<VectorType>(This);
    VectorType *FirstVec = dyn_cast<VectorType>(First);
    if (ThisVec && FirstVec) {
      return ThisVec->getElementCount() == FirstVec->getElementCount();
    }
    return (ThisVec == nullptr) && (FirstVec == nullptr) && (!This->isVoidTy());
  };
  auto Make = [](ArrayRef<Value *> Cur, ArrayRef<Type *> BaseTypes) {
    assert(!Cur.empty() && "No first source yet");
    std::vector<Constant *> Result;
    ElementCount EC;
    bool isVec = false;
    if (VectorType *VecTy = dyn_cast<VectorType>(Cur[0]->getType())) {
      EC = VecTy->getElementCount();
      isVec = true;
    }
    for (Type *T : BaseTypes) {
      if (VectorType::isValidElementType(T)) {
        if (isVec)
          // If the first pred is <i1 x N>, make the result <T x N>
          makeConstantsWithType(VectorType::get(T, EC), Result);
        else
          makeConstantsWithType(T, Result);
      }
    }
    assert(!Result.empty() && "No potential constants.");
    return Result;
  };
  return {Pred, Make};
}

/// Match values that have the same type as the first source.
static inline SourcePred matchSecondType() {
  auto Pred = [](ArrayRef<Value *> Cur, const Value *V) {
    assert((Cur.size() > 1) && "No second source yet");
    return V->getType() == Cur[1]->getType();
  };
  auto Make = [](ArrayRef<Value *> Cur, ArrayRef<Type *>) {
    assert((Cur.size() > 1) && "No second source yet");
    return makeConstantsWithType(Cur[1]->getType());
  };
  return {Pred, Make};
}

static inline SourcePred anyAggregateType() {
  auto Pred = [](ArrayRef<Value *>, const Value *V) {
    // We can't index zero sized arrays.
    if (isa<ArrayType>(V->getType()))
      return V->getType()->getArrayNumElements() > 0;

    // Structs can also be zero sized. I.e opaque types.
    if (isa<StructType>(V->getType()))
      return V->getType()->getStructNumElements() > 0;

    return V->getType()->isAggregateType();
  };
  // TODO: For now we only find aggregates in BaseTypes. It might be better to
  // manufacture them out of the base types in some cases.
  auto Find = std::nullopt;
  return {Pred, Find};
}

static inline SourcePred anyVectorType() {
  auto Pred = [](ArrayRef<Value *>, const Value *V) {
    return V->getType()->isVectorTy();
  };
  // TODO: For now we only find vectors in BaseTypes. It might be better to
  // manufacture vectors out of the base types, but it's tricky to be sure
  // that's actually a reasonable type.
  auto Make = std::nullopt;
  return {Pred, Make};
}

/// Match values that have the same type as the first source.
static inline SourcePred matchFirstType() {
  auto Pred = [](ArrayRef<Value *> Cur, const Value *V) {
    assert(!Cur.empty() && "No first source yet");
    return V->getType() == Cur[0]->getType();
  };
  auto Make = [](ArrayRef<Value *> Cur, ArrayRef<Type *>) {
    assert(!Cur.empty() && "No first source yet");
    return makeConstantsWithType(Cur[0]->getType());
  };
  return {Pred, Make};
}

/// Match values that have the first source's scalar type.
static inline SourcePred matchScalarOfFirstType() {
  auto Pred = [](ArrayRef<Value *> Cur, const Value *V) {
    assert(!Cur.empty() && "No first source yet");
    return V->getType() == Cur[0]->getType()->getScalarType();
  };
  auto Make = [](ArrayRef<Value *> Cur, ArrayRef<Type *>) {
    assert(!Cur.empty() && "No first source yet");
    return makeConstantsWithType(Cur[0]->getType()->getScalarType());
  };
  return {Pred, Make};
}

} // namespace fuzzerop
} // namespace llvm

#endif // LLVM_FUZZMUTATE_OPDESCRIPTOR_H
