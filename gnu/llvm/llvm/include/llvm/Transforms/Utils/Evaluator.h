//===- Evaluator.h - LLVM IR evaluator --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Function evaluator for LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_EVALUATOR_H
#define LLVM_TRANSFORMS_UTILS_EVALUATOR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <deque>
#include <memory>

namespace llvm {

class CallBase;
class DataLayout;
class Function;
class TargetLibraryInfo;

/// This class evaluates LLVM IR, producing the Constant representing each SSA
/// instruction.  Changes to global variables are stored in a mapping that can
/// be iterated over after the evaluation is complete.  Once an evaluation call
/// fails, the evaluation object should not be reused.
class Evaluator {
  struct MutableAggregate;

  /// The evaluator represents values either as a Constant*, or as a
  /// MutableAggregate, which allows changing individual aggregate elements
  /// without creating a new interned Constant.
  class MutableValue {
    PointerUnion<Constant *, MutableAggregate *> Val;
    void clear();
    bool makeMutable();

  public:
    MutableValue(Constant *C) { Val = C; }
    MutableValue(const MutableValue &) = delete;
    MutableValue(MutableValue &&Other) {
      Val = Other.Val;
      Other.Val = nullptr;
    }
    ~MutableValue() { clear(); }

    Type *getType() const {
      if (auto *C = dyn_cast_if_present<Constant *>(Val))
        return C->getType();
      return cast<MutableAggregate *>(Val)->Ty;
    }

    Constant *toConstant() const {
      if (auto *C = dyn_cast_if_present<Constant *>(Val))
        return C;
      return cast<MutableAggregate *>(Val)->toConstant();
    }

    Constant *read(Type *Ty, APInt Offset, const DataLayout &DL) const;
    bool write(Constant *V, APInt Offset, const DataLayout &DL);
  };

  struct MutableAggregate {
    Type *Ty;
    SmallVector<MutableValue> Elements;

    MutableAggregate(Type *Ty) : Ty(Ty) {}
    Constant *toConstant() const;
  };

public:
  Evaluator(const DataLayout &DL, const TargetLibraryInfo *TLI)
      : DL(DL), TLI(TLI) {
    ValueStack.emplace_back();
  }

  ~Evaluator() {
    for (auto &Tmp : AllocaTmps)
      // If there are still users of the alloca, the program is doing something
      // silly, e.g. storing the address of the alloca somewhere and using it
      // later.  Since this is undefined, we'll just make it be null.
      if (!Tmp->use_empty())
        Tmp->replaceAllUsesWith(Constant::getNullValue(Tmp->getType()));
  }

  /// Evaluate a call to function F, returning true if successful, false if we
  /// can't evaluate it.  ActualArgs contains the formal arguments for the
  /// function.
  bool EvaluateFunction(Function *F, Constant *&RetVal,
                        const SmallVectorImpl<Constant*> &ActualArgs);

  DenseMap<GlobalVariable *, Constant *> getMutatedInitializers() const {
    DenseMap<GlobalVariable *, Constant *> Result;
    for (const auto &Pair : MutatedMemory)
      Result[Pair.first] = Pair.second.toConstant();
    return Result;
  }

  const SmallPtrSetImpl<GlobalVariable *> &getInvariants() const {
    return Invariants;
  }

private:
  bool EvaluateBlock(BasicBlock::iterator CurInst, BasicBlock *&NextBB,
                     bool &StrippedPointerCastsForAliasAnalysis);

  Constant *getVal(Value *V) {
    if (Constant *CV = dyn_cast<Constant>(V)) return CV;
    Constant *R = ValueStack.back().lookup(V);
    assert(R && "Reference to an uncomputed value!");
    return R;
  }

  void setVal(Value *V, Constant *C) {
    ValueStack.back()[V] = C;
  }

  /// Casts call result to a type of bitcast call expression
  Constant *castCallResultIfNeeded(Type *ReturnType, Constant *RV);

  /// Given call site return callee and list of its formal arguments
  Function *getCalleeWithFormalArgs(CallBase &CB,
                                    SmallVectorImpl<Constant *> &Formals);

  /// Given call site and callee returns list of callee formal argument
  /// values converting them when necessary
  bool getFormalParams(CallBase &CB, Function *F,
                       SmallVectorImpl<Constant *> &Formals);

  Constant *ComputeLoadResult(Constant *P, Type *Ty);
  Constant *ComputeLoadResult(GlobalVariable *GV, Type *Ty,
                              const APInt &Offset);

  /// As we compute SSA register values, we store their contents here. The back
  /// of the deque contains the current function and the stack contains the
  /// values in the calling frames.
  std::deque<DenseMap<Value*, Constant*>> ValueStack;

  /// This is used to detect recursion.  In pathological situations we could hit
  /// exponential behavior, but at least there is nothing unbounded.
  SmallVector<Function*, 4> CallStack;

  /// For each store we execute, we update this map.  Loads check this to get
  /// the most up-to-date value.  If evaluation is successful, this state is
  /// committed to the process.
  DenseMap<GlobalVariable *, MutableValue> MutatedMemory;

  /// To 'execute' an alloca, we create a temporary global variable to represent
  /// its body.  This vector is needed so we can delete the temporary globals
  /// when we are done.
  SmallVector<std::unique_ptr<GlobalVariable>, 32> AllocaTmps;

  /// These global variables have been marked invariant by the static
  /// constructor.
  SmallPtrSet<GlobalVariable*, 8> Invariants;

  /// These are constants we have checked and know to be simple enough to live
  /// in a static initializer of a global.
  SmallPtrSet<Constant*, 8> SimpleConstants;

  const DataLayout &DL;
  const TargetLibraryInfo *TLI;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_EVALUATOR_H
