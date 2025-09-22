//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ReduceOperands.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"

using namespace llvm;
using namespace PatternMatch;

static void
extractOperandsFromModule(Oracle &O, ReducerWorkItem &WorkItem,
                          function_ref<Value *(Use &)> ReduceValue) {
  Module &Program = WorkItem.getModule();

  for (auto &F : Program.functions()) {
    for (auto &I : instructions(&F)) {
      if (PHINode *Phi = dyn_cast<PHINode>(&I)) {
        for (auto &Op : Phi->incoming_values()) {
          if (!O.shouldKeep()) {
            if (Value *Reduced = ReduceValue(Op))
              Phi->setIncomingValueForBlock(Phi->getIncomingBlock(Op), Reduced);
          }
        }

        continue;
      }

      for (auto &Op : I.operands()) {
        if (Value *Reduced = ReduceValue(Op)) {
          if (!O.shouldKeep())
            Op.set(Reduced);
        }
      }
    }
  }
}

static bool isOne(Use &Op) {
  auto *C = dyn_cast<Constant>(Op);
  return C && C->isOneValue();
}

static bool isZero(Use &Op) {
  auto *C = dyn_cast<Constant>(Op);
  return C && C->isNullValue();
}

static bool isZeroOrOneFP(Value *Op) {
  const APFloat *C;
  return match(Op, m_APFloat(C)) &&
         ((C->isZero() && !C->isNegative()) || C->isExactlyValue(1.0));
}

static bool shouldReduceOperand(Use &Op) {
  Type *Ty = Op->getType();
  if (Ty->isLabelTy() || Ty->isMetadataTy())
    return false;
  // TODO: be more precise about which GEP operands we can reduce (e.g. array
  // indexes)
  if (isa<GEPOperator>(Op.getUser()))
    return false;
  if (auto *CB = dyn_cast<CallBase>(Op.getUser())) {
    if (&CB->getCalledOperandUse() == &Op)
      return false;
  }
  return true;
}

static bool switchCaseExists(Use &Op, ConstantInt *CI) {
  SwitchInst *SI = dyn_cast<SwitchInst>(Op.getUser());
  if (!SI)
    return false;
  return SI->findCaseValue(CI) != SI->case_default();
}

void llvm::reduceOperandsOneDeltaPass(TestRunner &Test) {
  auto ReduceValue = [](Use &Op) -> Value * {
    if (!shouldReduceOperand(Op))
      return nullptr;

    Type *Ty = Op->getType();
    if (auto *IntTy = dyn_cast<IntegerType>(Ty)) {
      // Don't duplicate an existing switch case.
      if (switchCaseExists(Op, ConstantInt::get(IntTy, 1)))
        return nullptr;
      // Don't replace existing ones and zeroes.
      return (isOne(Op) || isZero(Op)) ? nullptr : ConstantInt::get(IntTy, 1);
    }

    if (Ty->isFloatingPointTy())
      return isZeroOrOneFP(Op) ? nullptr : ConstantFP::get(Ty, 1.0);

    if (VectorType *VT = dyn_cast<VectorType>(Ty)) {
      if (isOne(Op) || isZero(Op) || isZeroOrOneFP(Op))
        return nullptr;

      Type *ElementType = VT->getElementType();
      Constant *C;
      if (ElementType->isFloatingPointTy()) {
        C = ConstantFP::get(ElementType, 1.0);
      } else if (IntegerType *IntTy = dyn_cast<IntegerType>(ElementType)) {
        C = ConstantInt::get(IntTy, 1);
      } else {
        return nullptr;
      }
      return ConstantVector::getSplat(VT->getElementCount(), C);
    }

    return nullptr;
  };
  runDeltaPass(
      Test,
      [ReduceValue](Oracle &O, ReducerWorkItem &WorkItem) {
        extractOperandsFromModule(O, WorkItem, ReduceValue);
      },
      "Reducing Operands to one");
}

void llvm::reduceOperandsZeroDeltaPass(TestRunner &Test) {
  auto ReduceValue = [](Use &Op) -> Value * {
    if (!shouldReduceOperand(Op))
      return nullptr;
    // Don't duplicate an existing switch case.
    if (auto *IntTy = dyn_cast<IntegerType>(Op->getType()))
      if (switchCaseExists(Op, ConstantInt::get(IntTy, 0)))
        return nullptr;
    // Don't replace existing zeroes.
    return isZero(Op) ? nullptr : Constant::getNullValue(Op->getType());
  };
  runDeltaPass(
      Test,
      [ReduceValue](Oracle &O, ReducerWorkItem &Program) {
        extractOperandsFromModule(O, Program, ReduceValue);
      },
      "Reducing Operands to zero");
}

void llvm::reduceOperandsNaNDeltaPass(TestRunner &Test) {
  auto ReduceValue = [](Use &Op) -> Value * {
    Type *Ty = Op->getType();
    if (!Ty->isFPOrFPVectorTy())
      return nullptr;

    // Prefer 0.0 or 1.0 over NaN.
    //
    // TODO: Preferring NaN may make more sense because FP operations are more
    // universally foldable.
    if (match(Op.get(), m_NaN()) || isZeroOrOneFP(Op.get()))
      return nullptr;

    if (VectorType *VT = dyn_cast<VectorType>(Ty)) {
      return ConstantVector::getSplat(VT->getElementCount(),
                                      ConstantFP::getQNaN(VT->getElementType()));
    }

    return ConstantFP::getQNaN(Ty);
  };
  runDeltaPass(
      Test,
      [ReduceValue](Oracle &O, ReducerWorkItem &Program) {
        extractOperandsFromModule(O, Program, ReduceValue);
      },
      "Reducing Operands to NaN");
}
