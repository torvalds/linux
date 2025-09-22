//===- DemandedBits.cpp - Determine demanded bits -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements a demanded bits analysis. A demanded bit is one that
// contributes to a result; bits that are not demanded can be either zero or
// one without affecting control or data flow. For example in this sequence:
//
//   %1 = add i32 %x, %y
//   %2 = trunc i32 %1 to i16
//
// Only the lowest 16 bits of %1 are demanded; the rest are removed by the
// trunc.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DemandedBits.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "demanded-bits"

static bool isAlwaysLive(Instruction *I) {
  return I->isTerminator() || isa<DbgInfoIntrinsic>(I) || I->isEHPad() ||
         I->mayHaveSideEffects();
}

void DemandedBits::determineLiveOperandBits(
    const Instruction *UserI, const Value *Val, unsigned OperandNo,
    const APInt &AOut, APInt &AB, KnownBits &Known, KnownBits &Known2,
    bool &KnownBitsComputed) {
  unsigned BitWidth = AB.getBitWidth();

  // We're called once per operand, but for some instructions, we need to
  // compute known bits of both operands in order to determine the live bits of
  // either (when both operands are instructions themselves). We don't,
  // however, want to do this twice, so we cache the result in APInts that live
  // in the caller. For the two-relevant-operands case, both operand values are
  // provided here.
  auto ComputeKnownBits =
      [&](unsigned BitWidth, const Value *V1, const Value *V2) {
        if (KnownBitsComputed)
          return;
        KnownBitsComputed = true;

        const DataLayout &DL = UserI->getDataLayout();
        Known = KnownBits(BitWidth);
        computeKnownBits(V1, Known, DL, 0, &AC, UserI, &DT);

        if (V2) {
          Known2 = KnownBits(BitWidth);
          computeKnownBits(V2, Known2, DL, 0, &AC, UserI, &DT);
        }
      };

  switch (UserI->getOpcode()) {
  default: break;
  case Instruction::Call:
  case Instruction::Invoke:
    if (const auto *II = dyn_cast<IntrinsicInst>(UserI)) {
      switch (II->getIntrinsicID()) {
      default: break;
      case Intrinsic::bswap:
        // The alive bits of the input are the swapped alive bits of
        // the output.
        AB = AOut.byteSwap();
        break;
      case Intrinsic::bitreverse:
        // The alive bits of the input are the reversed alive bits of
        // the output.
        AB = AOut.reverseBits();
        break;
      case Intrinsic::ctlz:
        if (OperandNo == 0) {
          // We need some output bits, so we need all bits of the
          // input to the left of, and including, the leftmost bit
          // known to be one.
          ComputeKnownBits(BitWidth, Val, nullptr);
          AB = APInt::getHighBitsSet(BitWidth,
                 std::min(BitWidth, Known.countMaxLeadingZeros()+1));
        }
        break;
      case Intrinsic::cttz:
        if (OperandNo == 0) {
          // We need some output bits, so we need all bits of the
          // input to the right of, and including, the rightmost bit
          // known to be one.
          ComputeKnownBits(BitWidth, Val, nullptr);
          AB = APInt::getLowBitsSet(BitWidth,
                 std::min(BitWidth, Known.countMaxTrailingZeros()+1));
        }
        break;
      case Intrinsic::fshl:
      case Intrinsic::fshr: {
        const APInt *SA;
        if (OperandNo == 2) {
          // Shift amount is modulo the bitwidth. For powers of two we have
          // SA % BW == SA & (BW - 1).
          if (isPowerOf2_32(BitWidth))
            AB = BitWidth - 1;
        } else if (match(II->getOperand(2), m_APInt(SA))) {
          // Normalize to funnel shift left. APInt shifts of BitWidth are well-
          // defined, so no need to special-case zero shifts here.
          uint64_t ShiftAmt = SA->urem(BitWidth);
          if (II->getIntrinsicID() == Intrinsic::fshr)
            ShiftAmt = BitWidth - ShiftAmt;

          if (OperandNo == 0)
            AB = AOut.lshr(ShiftAmt);
          else if (OperandNo == 1)
            AB = AOut.shl(BitWidth - ShiftAmt);
        }
        break;
      }
      case Intrinsic::umax:
      case Intrinsic::umin:
      case Intrinsic::smax:
      case Intrinsic::smin:
        // If low bits of result are not demanded, they are also not demanded
        // for the min/max operands.
        AB = APInt::getBitsSetFrom(BitWidth, AOut.countr_zero());
        break;
      }
    }
    break;
  case Instruction::Add:
    if (AOut.isMask()) {
      AB = AOut;
    } else {
      ComputeKnownBits(BitWidth, UserI->getOperand(0), UserI->getOperand(1));
      AB = determineLiveOperandBitsAdd(OperandNo, AOut, Known, Known2);
    }
    break;
  case Instruction::Sub:
    if (AOut.isMask()) {
      AB = AOut;
    } else {
      ComputeKnownBits(BitWidth, UserI->getOperand(0), UserI->getOperand(1));
      AB = determineLiveOperandBitsSub(OperandNo, AOut, Known, Known2);
    }
    break;
  case Instruction::Mul:
    // Find the highest live output bit. We don't need any more input
    // bits than that (adds, and thus subtracts, ripple only to the
    // left).
    AB = APInt::getLowBitsSet(BitWidth, AOut.getActiveBits());
    break;
  case Instruction::Shl:
    if (OperandNo == 0) {
      const APInt *ShiftAmtC;
      if (match(UserI->getOperand(1), m_APInt(ShiftAmtC))) {
        uint64_t ShiftAmt = ShiftAmtC->getLimitedValue(BitWidth - 1);
        AB = AOut.lshr(ShiftAmt);

        // If the shift is nuw/nsw, then the high bits are not dead
        // (because we've promised that they *must* be zero).
        const auto *S = cast<ShlOperator>(UserI);
        if (S->hasNoSignedWrap())
          AB |= APInt::getHighBitsSet(BitWidth, ShiftAmt+1);
        else if (S->hasNoUnsignedWrap())
          AB |= APInt::getHighBitsSet(BitWidth, ShiftAmt);
      }
    }
    break;
  case Instruction::LShr:
    if (OperandNo == 0) {
      const APInt *ShiftAmtC;
      if (match(UserI->getOperand(1), m_APInt(ShiftAmtC))) {
        uint64_t ShiftAmt = ShiftAmtC->getLimitedValue(BitWidth - 1);
        AB = AOut.shl(ShiftAmt);

        // If the shift is exact, then the low bits are not dead
        // (they must be zero).
        if (cast<LShrOperator>(UserI)->isExact())
          AB |= APInt::getLowBitsSet(BitWidth, ShiftAmt);
      }
    }
    break;
  case Instruction::AShr:
    if (OperandNo == 0) {
      const APInt *ShiftAmtC;
      if (match(UserI->getOperand(1), m_APInt(ShiftAmtC))) {
        uint64_t ShiftAmt = ShiftAmtC->getLimitedValue(BitWidth - 1);
        AB = AOut.shl(ShiftAmt);
        // Because the high input bit is replicated into the
        // high-order bits of the result, if we need any of those
        // bits, then we must keep the highest input bit.
        if ((AOut & APInt::getHighBitsSet(BitWidth, ShiftAmt))
            .getBoolValue())
          AB.setSignBit();

        // If the shift is exact, then the low bits are not dead
        // (they must be zero).
        if (cast<AShrOperator>(UserI)->isExact())
          AB |= APInt::getLowBitsSet(BitWidth, ShiftAmt);
      }
    }
    break;
  case Instruction::And:
    AB = AOut;

    // For bits that are known zero, the corresponding bits in the
    // other operand are dead (unless they're both zero, in which
    // case they can't both be dead, so just mark the LHS bits as
    // dead).
    ComputeKnownBits(BitWidth, UserI->getOperand(0), UserI->getOperand(1));
    if (OperandNo == 0)
      AB &= ~Known2.Zero;
    else
      AB &= ~(Known.Zero & ~Known2.Zero);
    break;
  case Instruction::Or:
    AB = AOut;

    // For bits that are known one, the corresponding bits in the
    // other operand are dead (unless they're both one, in which
    // case they can't both be dead, so just mark the LHS bits as
    // dead).
    ComputeKnownBits(BitWidth, UserI->getOperand(0), UserI->getOperand(1));
    if (OperandNo == 0)
      AB &= ~Known2.One;
    else
      AB &= ~(Known.One & ~Known2.One);
    break;
  case Instruction::Xor:
  case Instruction::PHI:
    AB = AOut;
    break;
  case Instruction::Trunc:
    AB = AOut.zext(BitWidth);
    break;
  case Instruction::ZExt:
    AB = AOut.trunc(BitWidth);
    break;
  case Instruction::SExt:
    AB = AOut.trunc(BitWidth);
    // Because the high input bit is replicated into the
    // high-order bits of the result, if we need any of those
    // bits, then we must keep the highest input bit.
    if ((AOut & APInt::getHighBitsSet(AOut.getBitWidth(),
                                      AOut.getBitWidth() - BitWidth))
        .getBoolValue())
      AB.setSignBit();
    break;
  case Instruction::Select:
    if (OperandNo != 0)
      AB = AOut;
    break;
  case Instruction::ExtractElement:
    if (OperandNo == 0)
      AB = AOut;
    break;
  case Instruction::InsertElement:
  case Instruction::ShuffleVector:
    if (OperandNo == 0 || OperandNo == 1)
      AB = AOut;
    break;
  }
}

void DemandedBits::performAnalysis() {
  if (Analyzed)
    // Analysis already completed for this function.
    return;
  Analyzed = true;

  Visited.clear();
  AliveBits.clear();
  DeadUses.clear();

  SmallSetVector<Instruction*, 16> Worklist;

  // Collect the set of "root" instructions that are known live.
  for (Instruction &I : instructions(F)) {
    if (!isAlwaysLive(&I))
      continue;

    LLVM_DEBUG(dbgs() << "DemandedBits: Root: " << I << "\n");
    // For integer-valued instructions, set up an initial empty set of alive
    // bits and add the instruction to the work list. For other instructions
    // add their operands to the work list (for integer values operands, mark
    // all bits as live).
    Type *T = I.getType();
    if (T->isIntOrIntVectorTy()) {
      if (AliveBits.try_emplace(&I, T->getScalarSizeInBits(), 0).second)
        Worklist.insert(&I);

      continue;
    }

    // Non-integer-typed instructions...
    for (Use &OI : I.operands()) {
      if (auto *J = dyn_cast<Instruction>(OI)) {
        Type *T = J->getType();
        if (T->isIntOrIntVectorTy())
          AliveBits[J] = APInt::getAllOnes(T->getScalarSizeInBits());
        else
          Visited.insert(J);
        Worklist.insert(J);
      }
    }
    // To save memory, we don't add I to the Visited set here. Instead, we
    // check isAlwaysLive on every instruction when searching for dead
    // instructions later (we need to check isAlwaysLive for the
    // integer-typed instructions anyway).
  }

  // Propagate liveness backwards to operands.
  while (!Worklist.empty()) {
    Instruction *UserI = Worklist.pop_back_val();

    LLVM_DEBUG(dbgs() << "DemandedBits: Visiting: " << *UserI);
    APInt AOut;
    bool InputIsKnownDead = false;
    if (UserI->getType()->isIntOrIntVectorTy()) {
      AOut = AliveBits[UserI];
      LLVM_DEBUG(dbgs() << " Alive Out: 0x"
                        << Twine::utohexstr(AOut.getLimitedValue()));

      // If all bits of the output are dead, then all bits of the input
      // are also dead.
      InputIsKnownDead = !AOut && !isAlwaysLive(UserI);
    }
    LLVM_DEBUG(dbgs() << "\n");

    KnownBits Known, Known2;
    bool KnownBitsComputed = false;
    // Compute the set of alive bits for each operand. These are anded into the
    // existing set, if any, and if that changes the set of alive bits, the
    // operand is added to the work-list.
    for (Use &OI : UserI->operands()) {
      // We also want to detect dead uses of arguments, but will only store
      // demanded bits for instructions.
      auto *I = dyn_cast<Instruction>(OI);
      if (!I && !isa<Argument>(OI))
        continue;

      Type *T = OI->getType();
      if (T->isIntOrIntVectorTy()) {
        unsigned BitWidth = T->getScalarSizeInBits();
        APInt AB = APInt::getAllOnes(BitWidth);
        if (InputIsKnownDead) {
          AB = APInt(BitWidth, 0);
        } else {
          // Bits of each operand that are used to compute alive bits of the
          // output are alive, all others are dead.
          determineLiveOperandBits(UserI, OI, OI.getOperandNo(), AOut, AB,
                                   Known, Known2, KnownBitsComputed);

          // Keep track of uses which have no demanded bits.
          if (AB.isZero())
            DeadUses.insert(&OI);
          else
            DeadUses.erase(&OI);
        }

        if (I) {
          // If we've added to the set of alive bits (or the operand has not
          // been previously visited), then re-queue the operand to be visited
          // again.
          auto Res = AliveBits.try_emplace(I);
          if (Res.second || (AB |= Res.first->second) != Res.first->second) {
            Res.first->second = std::move(AB);
            Worklist.insert(I);
          }
        }
      } else if (I && Visited.insert(I).second) {
        Worklist.insert(I);
      }
    }
  }
}

APInt DemandedBits::getDemandedBits(Instruction *I) {
  performAnalysis();

  auto Found = AliveBits.find(I);
  if (Found != AliveBits.end())
    return Found->second;

  const DataLayout &DL = I->getDataLayout();
  return APInt::getAllOnes(DL.getTypeSizeInBits(I->getType()->getScalarType()));
}

APInt DemandedBits::getDemandedBits(Use *U) {
  Type *T = (*U)->getType();
  auto *UserI = cast<Instruction>(U->getUser());
  const DataLayout &DL = UserI->getDataLayout();
  unsigned BitWidth = DL.getTypeSizeInBits(T->getScalarType());

  // We only track integer uses, everything else produces a mask with all bits
  // set
  if (!T->isIntOrIntVectorTy())
    return APInt::getAllOnes(BitWidth);

  if (isUseDead(U))
    return APInt(BitWidth, 0);

  performAnalysis();

  APInt AOut = getDemandedBits(UserI);
  APInt AB = APInt::getAllOnes(BitWidth);
  KnownBits Known, Known2;
  bool KnownBitsComputed = false;

  determineLiveOperandBits(UserI, *U, U->getOperandNo(), AOut, AB, Known,
                           Known2, KnownBitsComputed);

  return AB;
}

bool DemandedBits::isInstructionDead(Instruction *I) {
  performAnalysis();

  return !Visited.count(I) && !AliveBits.contains(I) && !isAlwaysLive(I);
}

bool DemandedBits::isUseDead(Use *U) {
  // We only track integer uses, everything else is assumed live.
  if (!(*U)->getType()->isIntOrIntVectorTy())
    return false;

  // Uses by always-live instructions are never dead.
  auto *UserI = cast<Instruction>(U->getUser());
  if (isAlwaysLive(UserI))
    return false;

  performAnalysis();
  if (DeadUses.count(U))
    return true;

  // If no output bits are demanded, no input bits are demanded and the use
  // is dead. These uses might not be explicitly present in the DeadUses map.
  if (UserI->getType()->isIntOrIntVectorTy()) {
    auto Found = AliveBits.find(UserI);
    if (Found != AliveBits.end() && Found->second.isZero())
      return true;
  }

  return false;
}

void DemandedBits::print(raw_ostream &OS) {
  auto PrintDB = [&](const Instruction *I, const APInt &A, Value *V = nullptr) {
    OS << "DemandedBits: 0x" << Twine::utohexstr(A.getLimitedValue())
       << " for ";
    if (V) {
      V->printAsOperand(OS, false);
      OS << " in ";
    }
    OS << *I << '\n';
  };

  OS << "Printing analysis 'Demanded Bits Analysis' for function '" << F.getName() << "':\n";
  performAnalysis();
  for (auto &KV : AliveBits) {
    Instruction *I = KV.first;
    PrintDB(I, KV.second);

    for (Use &OI : I->operands()) {
      PrintDB(I, getDemandedBits(&OI), OI);
    }
  }
}

static APInt determineLiveOperandBitsAddCarry(unsigned OperandNo,
                                              const APInt &AOut,
                                              const KnownBits &LHS,
                                              const KnownBits &RHS,
                                              bool CarryZero, bool CarryOne) {
  assert(!(CarryZero && CarryOne) &&
         "Carry can't be zero and one at the same time");

  // The following check should be done by the caller, as it also indicates
  // that LHS and RHS don't need to be computed.
  //
  // if (AOut.isMask())
  //   return AOut;

  // Boundary bits' carry out is unaffected by their carry in.
  APInt Bound = (LHS.Zero & RHS.Zero) | (LHS.One & RHS.One);

  // First, the alive carry bits are determined from the alive output bits:
  // Let demand ripple to the right but only up to any set bit in Bound.
  //   AOut         = -1----
  //   Bound        = ----1-
  //   ACarry&~AOut = --111-
  APInt RBound = Bound.reverseBits();
  APInt RAOut = AOut.reverseBits();
  APInt RProp = RAOut + (RAOut | ~RBound);
  APInt RACarry = RProp ^ ~RBound;
  APInt ACarry = RACarry.reverseBits();

  // Then, the alive input bits are determined from the alive carry bits:
  APInt NeededToMaintainCarryZero;
  APInt NeededToMaintainCarryOne;
  if (OperandNo == 0) {
    NeededToMaintainCarryZero = LHS.Zero | ~RHS.Zero;
    NeededToMaintainCarryOne = LHS.One | ~RHS.One;
  } else {
    NeededToMaintainCarryZero = RHS.Zero | ~LHS.Zero;
    NeededToMaintainCarryOne = RHS.One | ~LHS.One;
  }

  // As in computeForAddCarry
  APInt PossibleSumZero = ~LHS.Zero + ~RHS.Zero + !CarryZero;
  APInt PossibleSumOne = LHS.One + RHS.One + CarryOne;

  // The below is simplified from
  //
  // APInt CarryKnownZero = ~(PossibleSumZero ^ LHS.Zero ^ RHS.Zero);
  // APInt CarryKnownOne = PossibleSumOne ^ LHS.One ^ RHS.One;
  // APInt CarryUnknown = ~(CarryKnownZero | CarryKnownOne);
  //
  // APInt NeededToMaintainCarry =
  //   (CarryKnownZero & NeededToMaintainCarryZero) |
  //   (CarryKnownOne  & NeededToMaintainCarryOne) |
  //   CarryUnknown;

  APInt NeededToMaintainCarry = (~PossibleSumZero | NeededToMaintainCarryZero) &
                                (PossibleSumOne | NeededToMaintainCarryOne);

  APInt AB = AOut | (ACarry & NeededToMaintainCarry);
  return AB;
}

APInt DemandedBits::determineLiveOperandBitsAdd(unsigned OperandNo,
                                                const APInt &AOut,
                                                const KnownBits &LHS,
                                                const KnownBits &RHS) {
  return determineLiveOperandBitsAddCarry(OperandNo, AOut, LHS, RHS, true,
                                          false);
}

APInt DemandedBits::determineLiveOperandBitsSub(unsigned OperandNo,
                                                const APInt &AOut,
                                                const KnownBits &LHS,
                                                const KnownBits &RHS) {
  KnownBits NRHS;
  NRHS.Zero = RHS.One;
  NRHS.One = RHS.Zero;
  return determineLiveOperandBitsAddCarry(OperandNo, AOut, LHS, NRHS, false,
                                          true);
}

AnalysisKey DemandedBitsAnalysis::Key;

DemandedBits DemandedBitsAnalysis::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  return DemandedBits(F, AC, DT);
}

PreservedAnalyses DemandedBitsPrinterPass::run(Function &F,
                                               FunctionAnalysisManager &AM) {
  AM.getResult<DemandedBitsAnalysis>(F).print(OS);
  return PreservedAnalyses::all();
}
