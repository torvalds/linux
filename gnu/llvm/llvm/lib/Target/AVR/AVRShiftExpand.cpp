//===- AVRShift.cpp - Shift Expansion Pass --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Expand non-8-bit and non-16-bit shift instructions (shl, lshr, ashr) to
/// inline loops, just like avr-gcc. This must be done in IR because otherwise
/// the type legalizer will turn 32-bit shifts into (non-existing) library calls
/// such as __ashlsi3.
//
//===----------------------------------------------------------------------===//

#include "AVR.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"

using namespace llvm;

namespace {

class AVRShiftExpand : public FunctionPass {
public:
  static char ID;

  AVRShiftExpand() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override { return "AVR Shift Expansion"; }

private:
  void expand(BinaryOperator *BI);
};

} // end of anonymous namespace

char AVRShiftExpand::ID = 0;

INITIALIZE_PASS(AVRShiftExpand, "avr-shift-expand", "AVR Shift Expansion",
                false, false)

Pass *llvm::createAVRShiftExpandPass() { return new AVRShiftExpand(); }

bool AVRShiftExpand::runOnFunction(Function &F) {
  SmallVector<BinaryOperator *, 1> ShiftInsts;
  auto &Ctx = F.getContext();
  for (Instruction &I : instructions(F)) {
    if (!I.isShift())
      // Only expand shift instructions (shl, lshr, ashr).
      continue;
    if (I.getType() == Type::getInt8Ty(Ctx) || I.getType() == Type::getInt16Ty(Ctx))
      // Only expand non-8-bit and non-16-bit shifts, since those are expanded
      // directly during isel.
      continue;
    if (isa<ConstantInt>(I.getOperand(1)))
      // Only expand when the shift amount is not known.
      // Known shift amounts are (currently) better expanded inline.
      continue;
    ShiftInsts.push_back(cast<BinaryOperator>(&I));
  }

  // The expanding itself needs to be done separately as expand() will remove
  // these instructions. Removing instructions while iterating over a basic
  // block is not a great idea.
  for (auto *I : ShiftInsts) {
    expand(I);
  }

  // Return whether this function expanded any shift instructions.
  return ShiftInsts.size() > 0;
}

void AVRShiftExpand::expand(BinaryOperator *BI) {
  auto &Ctx = BI->getContext();
  IRBuilder<> Builder(BI);
  Type *InputTy = cast<Instruction>(BI)->getType();
  Type *Int8Ty = Type::getInt8Ty(Ctx);
  Value *Int8Zero = ConstantInt::get(Int8Ty, 0);

  // Split the current basic block at the point of the existing shift
  // instruction and insert a new basic block for the loop.
  BasicBlock *BB = BI->getParent();
  Function *F = BB->getParent();
  BasicBlock *EndBB = BB->splitBasicBlock(BI, "shift.done");
  BasicBlock *LoopBB = BasicBlock::Create(Ctx, "shift.loop", F, EndBB);

  // Truncate the shift amount to i8, which is trivially lowered to a single
  // AVR register.
  Builder.SetInsertPoint(&BB->back());
  Value *ShiftAmount = Builder.CreateTrunc(BI->getOperand(1), Int8Ty);

  // Replace the unconditional branch that splitBasicBlock created with a
  // conditional branch.
  Value *Cmp1 = Builder.CreateICmpEQ(ShiftAmount, Int8Zero);
  Builder.CreateCondBr(Cmp1, EndBB, LoopBB);
  BB->back().eraseFromParent();

  // Create the loop body starting with PHI nodes.
  Builder.SetInsertPoint(LoopBB);
  PHINode *ShiftAmountPHI = Builder.CreatePHI(Int8Ty, 2);
  ShiftAmountPHI->addIncoming(ShiftAmount, BB);
  PHINode *ValuePHI = Builder.CreatePHI(InputTy, 2);
  ValuePHI->addIncoming(BI->getOperand(0), BB);

  // Subtract the shift amount by one, as we're shifting one this loop
  // iteration.
  Value *ShiftAmountSub =
      Builder.CreateSub(ShiftAmountPHI, ConstantInt::get(Int8Ty, 1));
  ShiftAmountPHI->addIncoming(ShiftAmountSub, LoopBB);

  // Emit the actual shift instruction. The difference is that this shift
  // instruction has a constant shift amount, which can be emitted inline
  // without a library call.
  Value *ValueShifted;
  switch (BI->getOpcode()) {
  case Instruction::Shl:
    ValueShifted = Builder.CreateShl(ValuePHI, ConstantInt::get(InputTy, 1));
    break;
  case Instruction::LShr:
    ValueShifted = Builder.CreateLShr(ValuePHI, ConstantInt::get(InputTy, 1));
    break;
  case Instruction::AShr:
    ValueShifted = Builder.CreateAShr(ValuePHI, ConstantInt::get(InputTy, 1));
    break;
  default:
    llvm_unreachable("asked to expand an instruction that is not a shift");
  }
  ValuePHI->addIncoming(ValueShifted, LoopBB);

  // Branch to either the loop again (if there is more to shift) or to the
  // basic block after the loop (if all bits are shifted).
  Value *Cmp2 = Builder.CreateICmpEQ(ShiftAmountSub, Int8Zero);
  Builder.CreateCondBr(Cmp2, EndBB, LoopBB);

  // Collect the resulting value. This is necessary in the IR but won't produce
  // any actual instructions.
  Builder.SetInsertPoint(BI);
  PHINode *Result = Builder.CreatePHI(InputTy, 2);
  Result->addIncoming(BI->getOperand(0), BB);
  Result->addIncoming(ValueShifted, LoopBB);

  // Replace the original shift instruction.
  BI->replaceAllUsesWith(Result);
  BI->eraseFromParent();
}
