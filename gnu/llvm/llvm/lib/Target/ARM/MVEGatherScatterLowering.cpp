//===- MVEGatherScatterLowering.cpp - Gather/Scatter lowering -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// This pass custom lowers llvm.gather and llvm.scatter instructions to
/// arm.mve.gather and arm.mve.scatter intrinsics, optimising the code to
/// produce a better final result as we go.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMSubtarget.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "arm-mve-gather-scatter-lowering"

cl::opt<bool> EnableMaskedGatherScatters(
    "enable-arm-maskedgatscat", cl::Hidden, cl::init(true),
    cl::desc("Enable the generation of masked gathers and scatters"));

namespace {

class MVEGatherScatterLowering : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  explicit MVEGatherScatterLowering() : FunctionPass(ID) {
    initializeMVEGatherScatterLoweringPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override {
    return "MVE gather/scatter lowering";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TargetPassConfig>();
    AU.addRequired<LoopInfoWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }

private:
  LoopInfo *LI = nullptr;
  const DataLayout *DL;

  // Check this is a valid gather with correct alignment
  bool isLegalTypeAndAlignment(unsigned NumElements, unsigned ElemSize,
                               Align Alignment);
  // Check whether Ptr is hidden behind a bitcast and look through it
  void lookThroughBitcast(Value *&Ptr);
  // Decompose a ptr into Base and Offsets, potentially using a GEP to return a
  // scalar base and vector offsets, or else fallback to using a base of 0 and
  // offset of Ptr where possible.
  Value *decomposePtr(Value *Ptr, Value *&Offsets, int &Scale,
                      FixedVectorType *Ty, Type *MemoryTy,
                      IRBuilder<> &Builder);
  // Check for a getelementptr and deduce base and offsets from it, on success
  // returning the base directly and the offsets indirectly using the Offsets
  // argument
  Value *decomposeGEP(Value *&Offsets, FixedVectorType *Ty,
                      GetElementPtrInst *GEP, IRBuilder<> &Builder);
  // Compute the scale of this gather/scatter instruction
  int computeScale(unsigned GEPElemSize, unsigned MemoryElemSize);
  // If the value is a constant, or derived from constants via additions
  // and multilications, return its numeric value
  std::optional<int64_t> getIfConst(const Value *V);
  // If Inst is an add instruction, check whether one summand is a
  // constant. If so, scale this constant and return it together with
  // the other summand.
  std::pair<Value *, int64_t> getVarAndConst(Value *Inst, int TypeScale);

  Instruction *lowerGather(IntrinsicInst *I);
  // Create a gather from a base + vector of offsets
  Instruction *tryCreateMaskedGatherOffset(IntrinsicInst *I, Value *Ptr,
                                           Instruction *&Root,
                                           IRBuilder<> &Builder);
  // Create a gather from a vector of pointers
  Instruction *tryCreateMaskedGatherBase(IntrinsicInst *I, Value *Ptr,
                                         IRBuilder<> &Builder,
                                         int64_t Increment = 0);
  // Create an incrementing gather from a vector of pointers
  Instruction *tryCreateMaskedGatherBaseWB(IntrinsicInst *I, Value *Ptr,
                                           IRBuilder<> &Builder,
                                           int64_t Increment = 0);

  Instruction *lowerScatter(IntrinsicInst *I);
  // Create a scatter to a base + vector of offsets
  Instruction *tryCreateMaskedScatterOffset(IntrinsicInst *I, Value *Offsets,
                                            IRBuilder<> &Builder);
  // Create a scatter to a vector of pointers
  Instruction *tryCreateMaskedScatterBase(IntrinsicInst *I, Value *Ptr,
                                          IRBuilder<> &Builder,
                                          int64_t Increment = 0);
  // Create an incrementing scatter from a vector of pointers
  Instruction *tryCreateMaskedScatterBaseWB(IntrinsicInst *I, Value *Ptr,
                                            IRBuilder<> &Builder,
                                            int64_t Increment = 0);

  // QI gathers and scatters can increment their offsets on their own if
  // the increment is a constant value (digit)
  Instruction *tryCreateIncrementingGatScat(IntrinsicInst *I, Value *Ptr,
                                            IRBuilder<> &Builder);
  // QI gathers/scatters can increment their offsets on their own if the
  // increment is a constant value (digit) - this creates a writeback QI
  // gather/scatter
  Instruction *tryCreateIncrementingWBGatScat(IntrinsicInst *I, Value *BasePtr,
                                              Value *Ptr, unsigned TypeScale,
                                              IRBuilder<> &Builder);

  // Optimise the base and offsets of the given address
  bool optimiseAddress(Value *Address, BasicBlock *BB, LoopInfo *LI);
  // Try to fold consecutive geps together into one
  Value *foldGEP(GetElementPtrInst *GEP, Value *&Offsets, unsigned &Scale,
                 IRBuilder<> &Builder);
  // Check whether these offsets could be moved out of the loop they're in
  bool optimiseOffsets(Value *Offsets, BasicBlock *BB, LoopInfo *LI);
  // Pushes the given add out of the loop
  void pushOutAdd(PHINode *&Phi, Value *OffsSecondOperand, unsigned StartIndex);
  // Pushes the given mul or shl out of the loop
  void pushOutMulShl(unsigned Opc, PHINode *&Phi, Value *IncrementPerRound,
                     Value *OffsSecondOperand, unsigned LoopIncrement,
                     IRBuilder<> &Builder);
};

} // end anonymous namespace

char MVEGatherScatterLowering::ID = 0;

INITIALIZE_PASS(MVEGatherScatterLowering, DEBUG_TYPE,
                "MVE gather/scattering lowering pass", false, false)

Pass *llvm::createMVEGatherScatterLoweringPass() {
  return new MVEGatherScatterLowering();
}

bool MVEGatherScatterLowering::isLegalTypeAndAlignment(unsigned NumElements,
                                                       unsigned ElemSize,
                                                       Align Alignment) {
  if (((NumElements == 4 &&
        (ElemSize == 32 || ElemSize == 16 || ElemSize == 8)) ||
       (NumElements == 8 && (ElemSize == 16 || ElemSize == 8)) ||
       (NumElements == 16 && ElemSize == 8)) &&
      Alignment >= ElemSize / 8)
    return true;
  LLVM_DEBUG(dbgs() << "masked gathers/scatters: instruction does not have "
                    << "valid alignment or vector type \n");
  return false;
}

static bool checkOffsetSize(Value *Offsets, unsigned TargetElemCount) {
  // Offsets that are not of type <N x i32> are sign extended by the
  // getelementptr instruction, and MVE gathers/scatters treat the offset as
  // unsigned. Thus, if the element size is smaller than 32, we can only allow
  // positive offsets - i.e., the offsets are not allowed to be variables we
  // can't look into.
  // Additionally, <N x i32> offsets have to either originate from a zext of a
  // vector with element types smaller or equal the type of the gather we're
  // looking at, or consist of constants that we can check are small enough
  // to fit into the gather type.
  // Thus we check that 0 < value < 2^TargetElemSize.
  unsigned TargetElemSize = 128 / TargetElemCount;
  unsigned OffsetElemSize = cast<FixedVectorType>(Offsets->getType())
                                ->getElementType()
                                ->getScalarSizeInBits();
  if (OffsetElemSize != TargetElemSize || OffsetElemSize != 32) {
    Constant *ConstOff = dyn_cast<Constant>(Offsets);
    if (!ConstOff)
      return false;
    int64_t TargetElemMaxSize = (1ULL << TargetElemSize);
    auto CheckValueSize = [TargetElemMaxSize](Value *OffsetElem) {
      ConstantInt *OConst = dyn_cast<ConstantInt>(OffsetElem);
      if (!OConst)
        return false;
      int SExtValue = OConst->getSExtValue();
      if (SExtValue >= TargetElemMaxSize || SExtValue < 0)
        return false;
      return true;
    };
    if (isa<FixedVectorType>(ConstOff->getType())) {
      for (unsigned i = 0; i < TargetElemCount; i++) {
        if (!CheckValueSize(ConstOff->getAggregateElement(i)))
          return false;
      }
    } else {
      if (!CheckValueSize(ConstOff))
        return false;
    }
  }
  return true;
}

Value *MVEGatherScatterLowering::decomposePtr(Value *Ptr, Value *&Offsets,
                                              int &Scale, FixedVectorType *Ty,
                                              Type *MemoryTy,
                                              IRBuilder<> &Builder) {
  if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
    if (Value *V = decomposeGEP(Offsets, Ty, GEP, Builder)) {
      Scale =
          computeScale(GEP->getSourceElementType()->getPrimitiveSizeInBits(),
                       MemoryTy->getScalarSizeInBits());
      return Scale == -1 ? nullptr : V;
    }
  }

  // If we couldn't use the GEP (or it doesn't exist), attempt to use a
  // BasePtr of 0 with Ptr as the Offsets, so long as there are only 4
  // elements.
  FixedVectorType *PtrTy = cast<FixedVectorType>(Ptr->getType());
  if (PtrTy->getNumElements() != 4 || MemoryTy->getScalarSizeInBits() == 32)
    return nullptr;
  Value *Zero = ConstantInt::get(Builder.getInt32Ty(), 0);
  Value *BasePtr = Builder.CreateIntToPtr(Zero, Builder.getPtrTy());
  Offsets = Builder.CreatePtrToInt(
      Ptr, FixedVectorType::get(Builder.getInt32Ty(), 4));
  Scale = 0;
  return BasePtr;
}

Value *MVEGatherScatterLowering::decomposeGEP(Value *&Offsets,
                                              FixedVectorType *Ty,
                                              GetElementPtrInst *GEP,
                                              IRBuilder<> &Builder) {
  if (!GEP) {
    LLVM_DEBUG(dbgs() << "masked gathers/scatters: no getelementpointer "
                      << "found\n");
    return nullptr;
  }
  LLVM_DEBUG(dbgs() << "masked gathers/scatters: getelementpointer found."
                    << " Looking at intrinsic for base + vector of offsets\n");
  Value *GEPPtr = GEP->getPointerOperand();
  Offsets = GEP->getOperand(1);
  if (GEPPtr->getType()->isVectorTy() ||
      !isa<FixedVectorType>(Offsets->getType()))
    return nullptr;

  if (GEP->getNumOperands() != 2) {
    LLVM_DEBUG(dbgs() << "masked gathers/scatters: getelementptr with too many"
                      << " operands. Expanding.\n");
    return nullptr;
  }
  Offsets = GEP->getOperand(1);
  unsigned OffsetsElemCount =
      cast<FixedVectorType>(Offsets->getType())->getNumElements();
  // Paranoid check whether the number of parallel lanes is the same
  assert(Ty->getNumElements() == OffsetsElemCount);

  ZExtInst *ZextOffs = dyn_cast<ZExtInst>(Offsets);
  if (ZextOffs)
    Offsets = ZextOffs->getOperand(0);
  FixedVectorType *OffsetType = cast<FixedVectorType>(Offsets->getType());

  // If the offsets are already being zext-ed to <N x i32>, that relieves us of
  // having to make sure that they won't overflow.
  if (!ZextOffs || cast<FixedVectorType>(ZextOffs->getDestTy())
                           ->getElementType()
                           ->getScalarSizeInBits() != 32)
    if (!checkOffsetSize(Offsets, OffsetsElemCount))
      return nullptr;

  // The offset sizes have been checked; if any truncating or zext-ing is
  // required to fix them, do that now
  if (Ty != Offsets->getType()) {
    if ((Ty->getElementType()->getScalarSizeInBits() <
         OffsetType->getElementType()->getScalarSizeInBits())) {
      Offsets = Builder.CreateTrunc(Offsets, Ty);
    } else {
      Offsets = Builder.CreateZExt(Offsets, VectorType::getInteger(Ty));
    }
  }
  // If none of the checks failed, return the gep's base pointer
  LLVM_DEBUG(dbgs() << "masked gathers/scatters: found correct offsets\n");
  return GEPPtr;
}

void MVEGatherScatterLowering::lookThroughBitcast(Value *&Ptr) {
  // Look through bitcast instruction if #elements is the same
  if (auto *BitCast = dyn_cast<BitCastInst>(Ptr)) {
    auto *BCTy = cast<FixedVectorType>(BitCast->getType());
    auto *BCSrcTy = cast<FixedVectorType>(BitCast->getOperand(0)->getType());
    if (BCTy->getNumElements() == BCSrcTy->getNumElements()) {
      LLVM_DEBUG(dbgs() << "masked gathers/scatters: looking through "
                        << "bitcast\n");
      Ptr = BitCast->getOperand(0);
    }
  }
}

int MVEGatherScatterLowering::computeScale(unsigned GEPElemSize,
                                           unsigned MemoryElemSize) {
  // This can be a 32bit load/store scaled by 4, a 16bit load/store scaled by 2,
  // or a 8bit, 16bit or 32bit load/store scaled by 1
  if (GEPElemSize == 32 && MemoryElemSize == 32)
    return 2;
  else if (GEPElemSize == 16 && MemoryElemSize == 16)
    return 1;
  else if (GEPElemSize == 8)
    return 0;
  LLVM_DEBUG(dbgs() << "masked gathers/scatters: incorrect scale. Can't "
                    << "create intrinsic\n");
  return -1;
}

std::optional<int64_t> MVEGatherScatterLowering::getIfConst(const Value *V) {
  const Constant *C = dyn_cast<Constant>(V);
  if (C && C->getSplatValue())
    return std::optional<int64_t>{C->getUniqueInteger().getSExtValue()};
  if (!isa<Instruction>(V))
    return std::optional<int64_t>{};

  const Instruction *I = cast<Instruction>(V);
  if (I->getOpcode() == Instruction::Add || I->getOpcode() == Instruction::Or ||
      I->getOpcode() == Instruction::Mul ||
      I->getOpcode() == Instruction::Shl) {
    std::optional<int64_t> Op0 = getIfConst(I->getOperand(0));
    std::optional<int64_t> Op1 = getIfConst(I->getOperand(1));
    if (!Op0 || !Op1)
      return std::optional<int64_t>{};
    if (I->getOpcode() == Instruction::Add)
      return std::optional<int64_t>{*Op0 + *Op1};
    if (I->getOpcode() == Instruction::Mul)
      return std::optional<int64_t>{*Op0 * *Op1};
    if (I->getOpcode() == Instruction::Shl)
      return std::optional<int64_t>{*Op0 << *Op1};
    if (I->getOpcode() == Instruction::Or)
      return std::optional<int64_t>{*Op0 | *Op1};
  }
  return std::optional<int64_t>{};
}

// Return true if I is an Or instruction that is equivalent to an add, due to
// the operands having no common bits set.
static bool isAddLikeOr(Instruction *I, const DataLayout &DL) {
  return I->getOpcode() == Instruction::Or &&
         haveNoCommonBitsSet(I->getOperand(0), I->getOperand(1), DL);
}

std::pair<Value *, int64_t>
MVEGatherScatterLowering::getVarAndConst(Value *Inst, int TypeScale) {
  std::pair<Value *, int64_t> ReturnFalse =
      std::pair<Value *, int64_t>(nullptr, 0);
  // At this point, the instruction we're looking at must be an add or an
  // add-like-or.
  Instruction *Add = dyn_cast<Instruction>(Inst);
  if (Add == nullptr ||
      (Add->getOpcode() != Instruction::Add && !isAddLikeOr(Add, *DL)))
    return ReturnFalse;

  Value *Summand;
  std::optional<int64_t> Const;
  // Find out which operand the value that is increased is
  if ((Const = getIfConst(Add->getOperand(0))))
    Summand = Add->getOperand(1);
  else if ((Const = getIfConst(Add->getOperand(1))))
    Summand = Add->getOperand(0);
  else
    return ReturnFalse;

  // Check that the constant is small enough for an incrementing gather
  int64_t Immediate = *Const << TypeScale;
  if (Immediate > 512 || Immediate < -512 || Immediate % 4 != 0)
    return ReturnFalse;

  return std::pair<Value *, int64_t>(Summand, Immediate);
}

Instruction *MVEGatherScatterLowering::lowerGather(IntrinsicInst *I) {
  using namespace PatternMatch;
  LLVM_DEBUG(dbgs() << "masked gathers: checking transform preconditions\n"
                    << *I << "\n");

  // @llvm.masked.gather.*(Ptrs, alignment, Mask, Src0)
  // Attempt to turn the masked gather in I into a MVE intrinsic
  // Potentially optimising the addressing modes as we do so.
  auto *Ty = cast<FixedVectorType>(I->getType());
  Value *Ptr = I->getArgOperand(0);
  Align Alignment = cast<ConstantInt>(I->getArgOperand(1))->getAlignValue();
  Value *Mask = I->getArgOperand(2);
  Value *PassThru = I->getArgOperand(3);

  if (!isLegalTypeAndAlignment(Ty->getNumElements(), Ty->getScalarSizeInBits(),
                               Alignment))
    return nullptr;
  lookThroughBitcast(Ptr);
  assert(Ptr->getType()->isVectorTy() && "Unexpected pointer type");

  IRBuilder<> Builder(I->getContext());
  Builder.SetInsertPoint(I);
  Builder.SetCurrentDebugLocation(I->getDebugLoc());

  Instruction *Root = I;

  Instruction *Load = tryCreateIncrementingGatScat(I, Ptr, Builder);
  if (!Load)
    Load = tryCreateMaskedGatherOffset(I, Ptr, Root, Builder);
  if (!Load)
    Load = tryCreateMaskedGatherBase(I, Ptr, Builder);
  if (!Load)
    return nullptr;

  if (!isa<UndefValue>(PassThru) && !match(PassThru, m_Zero())) {
    LLVM_DEBUG(dbgs() << "masked gathers: found non-trivial passthru - "
                      << "creating select\n");
    Load = SelectInst::Create(Mask, Load, PassThru);
    Builder.Insert(Load);
  }

  Root->replaceAllUsesWith(Load);
  Root->eraseFromParent();
  if (Root != I)
    // If this was an extending gather, we need to get rid of the sext/zext
    // sext/zext as well as of the gather itself
    I->eraseFromParent();

  LLVM_DEBUG(dbgs() << "masked gathers: successfully built masked gather\n"
                    << *Load << "\n");
  return Load;
}

Instruction *MVEGatherScatterLowering::tryCreateMaskedGatherBase(
    IntrinsicInst *I, Value *Ptr, IRBuilder<> &Builder, int64_t Increment) {
  using namespace PatternMatch;
  auto *Ty = cast<FixedVectorType>(I->getType());
  LLVM_DEBUG(dbgs() << "masked gathers: loading from vector of pointers\n");
  if (Ty->getNumElements() != 4 || Ty->getScalarSizeInBits() != 32)
    // Can't build an intrinsic for this
    return nullptr;
  Value *Mask = I->getArgOperand(2);
  if (match(Mask, m_One()))
    return Builder.CreateIntrinsic(Intrinsic::arm_mve_vldr_gather_base,
                                   {Ty, Ptr->getType()},
                                   {Ptr, Builder.getInt32(Increment)});
  else
    return Builder.CreateIntrinsic(
        Intrinsic::arm_mve_vldr_gather_base_predicated,
        {Ty, Ptr->getType(), Mask->getType()},
        {Ptr, Builder.getInt32(Increment), Mask});
}

Instruction *MVEGatherScatterLowering::tryCreateMaskedGatherBaseWB(
    IntrinsicInst *I, Value *Ptr, IRBuilder<> &Builder, int64_t Increment) {
  using namespace PatternMatch;
  auto *Ty = cast<FixedVectorType>(I->getType());
  LLVM_DEBUG(dbgs() << "masked gathers: loading from vector of pointers with "
                    << "writeback\n");
  if (Ty->getNumElements() != 4 || Ty->getScalarSizeInBits() != 32)
    // Can't build an intrinsic for this
    return nullptr;
  Value *Mask = I->getArgOperand(2);
  if (match(Mask, m_One()))
    return Builder.CreateIntrinsic(Intrinsic::arm_mve_vldr_gather_base_wb,
                                   {Ty, Ptr->getType()},
                                   {Ptr, Builder.getInt32(Increment)});
  else
    return Builder.CreateIntrinsic(
        Intrinsic::arm_mve_vldr_gather_base_wb_predicated,
        {Ty, Ptr->getType(), Mask->getType()},
        {Ptr, Builder.getInt32(Increment), Mask});
}

Instruction *MVEGatherScatterLowering::tryCreateMaskedGatherOffset(
    IntrinsicInst *I, Value *Ptr, Instruction *&Root, IRBuilder<> &Builder) {
  using namespace PatternMatch;

  Type *MemoryTy = I->getType();
  Type *ResultTy = MemoryTy;

  unsigned Unsigned = 1;
  // The size of the gather was already checked in isLegalTypeAndAlignment;
  // if it was not a full vector width an appropriate extend should follow.
  auto *Extend = Root;
  bool TruncResult = false;
  if (MemoryTy->getPrimitiveSizeInBits() < 128) {
    if (I->hasOneUse()) {
      // If the gather has a single extend of the correct type, use an extending
      // gather and replace the ext. In which case the correct root to replace
      // is not the CallInst itself, but the instruction which extends it.
      Instruction* User = cast<Instruction>(*I->users().begin());
      if (isa<SExtInst>(User) &&
          User->getType()->getPrimitiveSizeInBits() == 128) {
        LLVM_DEBUG(dbgs() << "masked gathers: Incorporating extend: "
                          << *User << "\n");
        Extend = User;
        ResultTy = User->getType();
        Unsigned = 0;
      } else if (isa<ZExtInst>(User) &&
                 User->getType()->getPrimitiveSizeInBits() == 128) {
        LLVM_DEBUG(dbgs() << "masked gathers: Incorporating extend: "
                          << *ResultTy << "\n");
        Extend = User;
        ResultTy = User->getType();
      }
    }

    // If an extend hasn't been found and the type is an integer, create an
    // extending gather and truncate back to the original type.
    if (ResultTy->getPrimitiveSizeInBits() < 128 &&
        ResultTy->isIntOrIntVectorTy()) {
      ResultTy = ResultTy->getWithNewBitWidth(
          128 / cast<FixedVectorType>(ResultTy)->getNumElements());
      TruncResult = true;
      LLVM_DEBUG(dbgs() << "masked gathers: Small input type, truncing to: "
                        << *ResultTy << "\n");
    }

    // The final size of the gather must be a full vector width
    if (ResultTy->getPrimitiveSizeInBits() != 128) {
      LLVM_DEBUG(dbgs() << "masked gathers: Extend needed but not provided "
                           "from the correct type. Expanding\n");
      return nullptr;
    }
  }

  Value *Offsets;
  int Scale;
  Value *BasePtr = decomposePtr(
      Ptr, Offsets, Scale, cast<FixedVectorType>(ResultTy), MemoryTy, Builder);
  if (!BasePtr)
    return nullptr;

  Root = Extend;
  Value *Mask = I->getArgOperand(2);
  Instruction *Load = nullptr;
  if (!match(Mask, m_One()))
    Load = Builder.CreateIntrinsic(
        Intrinsic::arm_mve_vldr_gather_offset_predicated,
        {ResultTy, BasePtr->getType(), Offsets->getType(), Mask->getType()},
        {BasePtr, Offsets, Builder.getInt32(MemoryTy->getScalarSizeInBits()),
         Builder.getInt32(Scale), Builder.getInt32(Unsigned), Mask});
  else
    Load = Builder.CreateIntrinsic(
        Intrinsic::arm_mve_vldr_gather_offset,
        {ResultTy, BasePtr->getType(), Offsets->getType()},
        {BasePtr, Offsets, Builder.getInt32(MemoryTy->getScalarSizeInBits()),
         Builder.getInt32(Scale), Builder.getInt32(Unsigned)});

  if (TruncResult) {
    Load = TruncInst::Create(Instruction::Trunc, Load, MemoryTy);
    Builder.Insert(Load);
  }
  return Load;
}

Instruction *MVEGatherScatterLowering::lowerScatter(IntrinsicInst *I) {
  using namespace PatternMatch;
  LLVM_DEBUG(dbgs() << "masked scatters: checking transform preconditions\n"
                    << *I << "\n");

  // @llvm.masked.scatter.*(data, ptrs, alignment, mask)
  // Attempt to turn the masked scatter in I into a MVE intrinsic
  // Potentially optimising the addressing modes as we do so.
  Value *Input = I->getArgOperand(0);
  Value *Ptr = I->getArgOperand(1);
  Align Alignment = cast<ConstantInt>(I->getArgOperand(2))->getAlignValue();
  auto *Ty = cast<FixedVectorType>(Input->getType());

  if (!isLegalTypeAndAlignment(Ty->getNumElements(), Ty->getScalarSizeInBits(),
                               Alignment))
    return nullptr;

  lookThroughBitcast(Ptr);
  assert(Ptr->getType()->isVectorTy() && "Unexpected pointer type");

  IRBuilder<> Builder(I->getContext());
  Builder.SetInsertPoint(I);
  Builder.SetCurrentDebugLocation(I->getDebugLoc());

  Instruction *Store = tryCreateIncrementingGatScat(I, Ptr, Builder);
  if (!Store)
    Store = tryCreateMaskedScatterOffset(I, Ptr, Builder);
  if (!Store)
    Store = tryCreateMaskedScatterBase(I, Ptr, Builder);
  if (!Store)
    return nullptr;

  LLVM_DEBUG(dbgs() << "masked scatters: successfully built masked scatter\n"
                    << *Store << "\n");
  I->eraseFromParent();
  return Store;
}

Instruction *MVEGatherScatterLowering::tryCreateMaskedScatterBase(
    IntrinsicInst *I, Value *Ptr, IRBuilder<> &Builder, int64_t Increment) {
  using namespace PatternMatch;
  Value *Input = I->getArgOperand(0);
  auto *Ty = cast<FixedVectorType>(Input->getType());
  // Only QR variants allow truncating
  if (!(Ty->getNumElements() == 4 && Ty->getScalarSizeInBits() == 32)) {
    // Can't build an intrinsic for this
    return nullptr;
  }
  Value *Mask = I->getArgOperand(3);
  //  int_arm_mve_vstr_scatter_base(_predicated) addr, offset, data(, mask)
  LLVM_DEBUG(dbgs() << "masked scatters: storing to a vector of pointers\n");
  if (match(Mask, m_One()))
    return Builder.CreateIntrinsic(Intrinsic::arm_mve_vstr_scatter_base,
                                   {Ptr->getType(), Input->getType()},
                                   {Ptr, Builder.getInt32(Increment), Input});
  else
    return Builder.CreateIntrinsic(
        Intrinsic::arm_mve_vstr_scatter_base_predicated,
        {Ptr->getType(), Input->getType(), Mask->getType()},
        {Ptr, Builder.getInt32(Increment), Input, Mask});
}

Instruction *MVEGatherScatterLowering::tryCreateMaskedScatterBaseWB(
    IntrinsicInst *I, Value *Ptr, IRBuilder<> &Builder, int64_t Increment) {
  using namespace PatternMatch;
  Value *Input = I->getArgOperand(0);
  auto *Ty = cast<FixedVectorType>(Input->getType());
  LLVM_DEBUG(dbgs() << "masked scatters: storing to a vector of pointers "
                    << "with writeback\n");
  if (Ty->getNumElements() != 4 || Ty->getScalarSizeInBits() != 32)
    // Can't build an intrinsic for this
    return nullptr;
  Value *Mask = I->getArgOperand(3);
  if (match(Mask, m_One()))
    return Builder.CreateIntrinsic(Intrinsic::arm_mve_vstr_scatter_base_wb,
                                   {Ptr->getType(), Input->getType()},
                                   {Ptr, Builder.getInt32(Increment), Input});
  else
    return Builder.CreateIntrinsic(
        Intrinsic::arm_mve_vstr_scatter_base_wb_predicated,
        {Ptr->getType(), Input->getType(), Mask->getType()},
        {Ptr, Builder.getInt32(Increment), Input, Mask});
}

Instruction *MVEGatherScatterLowering::tryCreateMaskedScatterOffset(
    IntrinsicInst *I, Value *Ptr, IRBuilder<> &Builder) {
  using namespace PatternMatch;
  Value *Input = I->getArgOperand(0);
  Value *Mask = I->getArgOperand(3);
  Type *InputTy = Input->getType();
  Type *MemoryTy = InputTy;

  LLVM_DEBUG(dbgs() << "masked scatters: getelementpointer found. Storing"
                    << " to base + vector of offsets\n");
  // If the input has been truncated, try to integrate that trunc into the
  // scatter instruction (we don't care about alignment here)
  if (TruncInst *Trunc = dyn_cast<TruncInst>(Input)) {
    Value *PreTrunc = Trunc->getOperand(0);
    Type *PreTruncTy = PreTrunc->getType();
    if (PreTruncTy->getPrimitiveSizeInBits() == 128) {
      Input = PreTrunc;
      InputTy = PreTruncTy;
    }
  }
  bool ExtendInput = false;
  if (InputTy->getPrimitiveSizeInBits() < 128 &&
      InputTy->isIntOrIntVectorTy()) {
    // If we can't find a trunc to incorporate into the instruction, create an
    // implicit one with a zext, so that we can still create a scatter. We know
    // that the input type is 4x/8x/16x and of type i8/i16/i32, so any type
    // smaller than 128 bits will divide evenly into a 128bit vector.
    InputTy = InputTy->getWithNewBitWidth(
        128 / cast<FixedVectorType>(InputTy)->getNumElements());
    ExtendInput = true;
    LLVM_DEBUG(dbgs() << "masked scatters: Small input type, will extend:\n"
                      << *Input << "\n");
  }
  if (InputTy->getPrimitiveSizeInBits() != 128) {
    LLVM_DEBUG(dbgs() << "masked scatters: cannot create scatters for "
                         "non-standard input types. Expanding.\n");
    return nullptr;
  }

  Value *Offsets;
  int Scale;
  Value *BasePtr = decomposePtr(
      Ptr, Offsets, Scale, cast<FixedVectorType>(InputTy), MemoryTy, Builder);
  if (!BasePtr)
    return nullptr;

  if (ExtendInput)
    Input = Builder.CreateZExt(Input, InputTy);
  if (!match(Mask, m_One()))
    return Builder.CreateIntrinsic(
        Intrinsic::arm_mve_vstr_scatter_offset_predicated,
        {BasePtr->getType(), Offsets->getType(), Input->getType(),
         Mask->getType()},
        {BasePtr, Offsets, Input,
         Builder.getInt32(MemoryTy->getScalarSizeInBits()),
         Builder.getInt32(Scale), Mask});
  else
    return Builder.CreateIntrinsic(
        Intrinsic::arm_mve_vstr_scatter_offset,
        {BasePtr->getType(), Offsets->getType(), Input->getType()},
        {BasePtr, Offsets, Input,
         Builder.getInt32(MemoryTy->getScalarSizeInBits()),
         Builder.getInt32(Scale)});
}

Instruction *MVEGatherScatterLowering::tryCreateIncrementingGatScat(
    IntrinsicInst *I, Value *Ptr, IRBuilder<> &Builder) {
  FixedVectorType *Ty;
  if (I->getIntrinsicID() == Intrinsic::masked_gather)
    Ty = cast<FixedVectorType>(I->getType());
  else
    Ty = cast<FixedVectorType>(I->getArgOperand(0)->getType());

  // Incrementing gathers only exist for v4i32
  if (Ty->getNumElements() != 4 || Ty->getScalarSizeInBits() != 32)
    return nullptr;
  // Incrementing gathers are not beneficial outside of a loop
  Loop *L = LI->getLoopFor(I->getParent());
  if (L == nullptr)
    return nullptr;

  // Decompose the GEP into Base and Offsets
  GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  Value *Offsets;
  Value *BasePtr = decomposeGEP(Offsets, Ty, GEP, Builder);
  if (!BasePtr)
    return nullptr;

  LLVM_DEBUG(dbgs() << "masked gathers/scatters: trying to build incrementing "
                       "wb gather/scatter\n");

  // The gep was in charge of making sure the offsets are scaled correctly
  // - calculate that factor so it can be applied by hand
  int TypeScale =
      computeScale(DL->getTypeSizeInBits(GEP->getOperand(0)->getType()),
                   DL->getTypeSizeInBits(GEP->getType()) /
                       cast<FixedVectorType>(GEP->getType())->getNumElements());
  if (TypeScale == -1)
    return nullptr;

  if (GEP->hasOneUse()) {
    // Only in this case do we want to build a wb gather, because the wb will
    // change the phi which does affect other users of the gep (which will still
    // be using the phi in the old way)
    if (auto *Load = tryCreateIncrementingWBGatScat(I, BasePtr, Offsets,
                                                    TypeScale, Builder))
      return Load;
  }

  LLVM_DEBUG(dbgs() << "masked gathers/scatters: trying to build incrementing "
                       "non-wb gather/scatter\n");

  std::pair<Value *, int64_t> Add = getVarAndConst(Offsets, TypeScale);
  if (Add.first == nullptr)
    return nullptr;
  Value *OffsetsIncoming = Add.first;
  int64_t Immediate = Add.second;

  // Make sure the offsets are scaled correctly
  Instruction *ScaledOffsets = BinaryOperator::Create(
      Instruction::Shl, OffsetsIncoming,
      Builder.CreateVectorSplat(Ty->getNumElements(),
                                Builder.getInt32(TypeScale)),
      "ScaledIndex", I->getIterator());
  // Add the base to the offsets
  OffsetsIncoming = BinaryOperator::Create(
      Instruction::Add, ScaledOffsets,
      Builder.CreateVectorSplat(
          Ty->getNumElements(),
          Builder.CreatePtrToInt(
              BasePtr,
              cast<VectorType>(ScaledOffsets->getType())->getElementType())),
      "StartIndex", I->getIterator());

  if (I->getIntrinsicID() == Intrinsic::masked_gather)
    return tryCreateMaskedGatherBase(I, OffsetsIncoming, Builder, Immediate);
  else
    return tryCreateMaskedScatterBase(I, OffsetsIncoming, Builder, Immediate);
}

Instruction *MVEGatherScatterLowering::tryCreateIncrementingWBGatScat(
    IntrinsicInst *I, Value *BasePtr, Value *Offsets, unsigned TypeScale,
    IRBuilder<> &Builder) {
  // Check whether this gather's offset is incremented by a constant - if so,
  // and the load is of the right type, we can merge this into a QI gather
  Loop *L = LI->getLoopFor(I->getParent());
  // Offsets that are worth merging into this instruction will be incremented
  // by a constant, thus we're looking for an add of a phi and a constant
  PHINode *Phi = dyn_cast<PHINode>(Offsets);
  if (Phi == nullptr || Phi->getNumIncomingValues() != 2 ||
      Phi->getParent() != L->getHeader() || Phi->getNumUses() != 2)
    // No phi means no IV to write back to; if there is a phi, we expect it
    // to have exactly two incoming values; the only phis we are interested in
    // will be loop IV's and have exactly two uses, one in their increment and
    // one in the gather's gep
    return nullptr;

  unsigned IncrementIndex =
      Phi->getIncomingBlock(0) == L->getLoopLatch() ? 0 : 1;
  // Look through the phi to the phi increment
  Offsets = Phi->getIncomingValue(IncrementIndex);

  std::pair<Value *, int64_t> Add = getVarAndConst(Offsets, TypeScale);
  if (Add.first == nullptr)
    return nullptr;
  Value *OffsetsIncoming = Add.first;
  int64_t Immediate = Add.second;
  if (OffsetsIncoming != Phi)
    // Then the increment we are looking at is not an increment of the
    // induction variable, and we don't want to do a writeback
    return nullptr;

  Builder.SetInsertPoint(&Phi->getIncomingBlock(1 - IncrementIndex)->back());
  unsigned NumElems =
      cast<FixedVectorType>(OffsetsIncoming->getType())->getNumElements();

  // Make sure the offsets are scaled correctly
  Instruction *ScaledOffsets = BinaryOperator::Create(
      Instruction::Shl, Phi->getIncomingValue(1 - IncrementIndex),
      Builder.CreateVectorSplat(NumElems, Builder.getInt32(TypeScale)),
      "ScaledIndex",
      Phi->getIncomingBlock(1 - IncrementIndex)->back().getIterator());
  // Add the base to the offsets
  OffsetsIncoming = BinaryOperator::Create(
      Instruction::Add, ScaledOffsets,
      Builder.CreateVectorSplat(
          NumElems,
          Builder.CreatePtrToInt(
              BasePtr,
              cast<VectorType>(ScaledOffsets->getType())->getElementType())),
      "StartIndex",
      Phi->getIncomingBlock(1 - IncrementIndex)->back().getIterator());
  // The gather is pre-incrementing
  OffsetsIncoming = BinaryOperator::Create(
      Instruction::Sub, OffsetsIncoming,
      Builder.CreateVectorSplat(NumElems, Builder.getInt32(Immediate)),
      "PreIncrementStartIndex",
      Phi->getIncomingBlock(1 - IncrementIndex)->back().getIterator());
  Phi->setIncomingValue(1 - IncrementIndex, OffsetsIncoming);

  Builder.SetInsertPoint(I);

  Instruction *EndResult;
  Instruction *NewInduction;
  if (I->getIntrinsicID() == Intrinsic::masked_gather) {
    // Build the incrementing gather
    Value *Load = tryCreateMaskedGatherBaseWB(I, Phi, Builder, Immediate);
    // One value to be handed to whoever uses the gather, one is the loop
    // increment
    EndResult = ExtractValueInst::Create(Load, 0, "Gather");
    NewInduction = ExtractValueInst::Create(Load, 1, "GatherIncrement");
    Builder.Insert(EndResult);
    Builder.Insert(NewInduction);
  } else {
    // Build the incrementing scatter
    EndResult = NewInduction =
        tryCreateMaskedScatterBaseWB(I, Phi, Builder, Immediate);
  }
  Instruction *AddInst = cast<Instruction>(Offsets);
  AddInst->replaceAllUsesWith(NewInduction);
  AddInst->eraseFromParent();
  Phi->setIncomingValue(IncrementIndex, NewInduction);

  return EndResult;
}

void MVEGatherScatterLowering::pushOutAdd(PHINode *&Phi,
                                          Value *OffsSecondOperand,
                                          unsigned StartIndex) {
  LLVM_DEBUG(dbgs() << "masked gathers/scatters: optimising add instruction\n");
  BasicBlock::iterator InsertionPoint =
      Phi->getIncomingBlock(StartIndex)->back().getIterator();
  // Initialize the phi with a vector that contains a sum of the constants
  Instruction *NewIndex = BinaryOperator::Create(
      Instruction::Add, Phi->getIncomingValue(StartIndex), OffsSecondOperand,
      "PushedOutAdd", InsertionPoint);
  unsigned IncrementIndex = StartIndex == 0 ? 1 : 0;

  // Order such that start index comes first (this reduces mov's)
  Phi->addIncoming(NewIndex, Phi->getIncomingBlock(StartIndex));
  Phi->addIncoming(Phi->getIncomingValue(IncrementIndex),
                   Phi->getIncomingBlock(IncrementIndex));
  Phi->removeIncomingValue(1);
  Phi->removeIncomingValue((unsigned)0);
}

void MVEGatherScatterLowering::pushOutMulShl(unsigned Opcode, PHINode *&Phi,
                                             Value *IncrementPerRound,
                                             Value *OffsSecondOperand,
                                             unsigned LoopIncrement,
                                             IRBuilder<> &Builder) {
  LLVM_DEBUG(dbgs() << "masked gathers/scatters: optimising mul instruction\n");

  // Create a new scalar add outside of the loop and transform it to a splat
  // by which loop variable can be incremented
  BasicBlock::iterator InsertionPoint =
      Phi->getIncomingBlock(LoopIncrement == 1 ? 0 : 1)->back().getIterator();

  // Create a new index
  Value *StartIndex =
      BinaryOperator::Create((Instruction::BinaryOps)Opcode,
                             Phi->getIncomingValue(LoopIncrement == 1 ? 0 : 1),
                             OffsSecondOperand, "PushedOutMul", InsertionPoint);

  Instruction *Product =
      BinaryOperator::Create((Instruction::BinaryOps)Opcode, IncrementPerRound,
                             OffsSecondOperand, "Product", InsertionPoint);

  BasicBlock::iterator NewIncrInsertPt =
      Phi->getIncomingBlock(LoopIncrement)->back().getIterator();
  NewIncrInsertPt = std::prev(NewIncrInsertPt);

  // Increment NewIndex by Product instead of the multiplication
  Instruction *NewIncrement = BinaryOperator::Create(
      Instruction::Add, Phi, Product, "IncrementPushedOutMul", NewIncrInsertPt);

  Phi->addIncoming(StartIndex,
                   Phi->getIncomingBlock(LoopIncrement == 1 ? 0 : 1));
  Phi->addIncoming(NewIncrement, Phi->getIncomingBlock(LoopIncrement));
  Phi->removeIncomingValue((unsigned)0);
  Phi->removeIncomingValue((unsigned)0);
}

// Check whether all usages of this instruction are as offsets of
// gathers/scatters or simple arithmetics only used by gathers/scatters
static bool hasAllGatScatUsers(Instruction *I, const DataLayout &DL) {
  if (I->hasNUses(0)) {
    return false;
  }
  bool Gatscat = true;
  for (User *U : I->users()) {
    if (!isa<Instruction>(U))
      return false;
    if (isa<GetElementPtrInst>(U) ||
        isGatherScatter(dyn_cast<IntrinsicInst>(U))) {
      return Gatscat;
    } else {
      unsigned OpCode = cast<Instruction>(U)->getOpcode();
      if ((OpCode == Instruction::Add || OpCode == Instruction::Mul ||
           OpCode == Instruction::Shl ||
           isAddLikeOr(cast<Instruction>(U), DL)) &&
          hasAllGatScatUsers(cast<Instruction>(U), DL)) {
        continue;
      }
      return false;
    }
  }
  return Gatscat;
}

bool MVEGatherScatterLowering::optimiseOffsets(Value *Offsets, BasicBlock *BB,
                                               LoopInfo *LI) {
  LLVM_DEBUG(dbgs() << "masked gathers/scatters: trying to optimize: "
                    << *Offsets << "\n");
  // Optimise the addresses of gathers/scatters by moving invariant
  // calculations out of the loop
  if (!isa<Instruction>(Offsets))
    return false;
  Instruction *Offs = cast<Instruction>(Offsets);
  if (Offs->getOpcode() != Instruction::Add && !isAddLikeOr(Offs, *DL) &&
      Offs->getOpcode() != Instruction::Mul &&
      Offs->getOpcode() != Instruction::Shl)
    return false;
  Loop *L = LI->getLoopFor(BB);
  if (L == nullptr)
    return false;
  if (!Offs->hasOneUse()) {
    if (!hasAllGatScatUsers(Offs, *DL))
      return false;
  }

  // Find out which, if any, operand of the instruction
  // is a phi node
  PHINode *Phi;
  int OffsSecondOp;
  if (isa<PHINode>(Offs->getOperand(0))) {
    Phi = cast<PHINode>(Offs->getOperand(0));
    OffsSecondOp = 1;
  } else if (isa<PHINode>(Offs->getOperand(1))) {
    Phi = cast<PHINode>(Offs->getOperand(1));
    OffsSecondOp = 0;
  } else {
    bool Changed = false;
    if (isa<Instruction>(Offs->getOperand(0)) &&
        L->contains(cast<Instruction>(Offs->getOperand(0))))
      Changed |= optimiseOffsets(Offs->getOperand(0), BB, LI);
    if (isa<Instruction>(Offs->getOperand(1)) &&
        L->contains(cast<Instruction>(Offs->getOperand(1))))
      Changed |= optimiseOffsets(Offs->getOperand(1), BB, LI);
    if (!Changed)
      return false;
    if (isa<PHINode>(Offs->getOperand(0))) {
      Phi = cast<PHINode>(Offs->getOperand(0));
      OffsSecondOp = 1;
    } else if (isa<PHINode>(Offs->getOperand(1))) {
      Phi = cast<PHINode>(Offs->getOperand(1));
      OffsSecondOp = 0;
    } else {
      return false;
    }
  }
  // A phi node we want to perform this function on should be from the
  // loop header.
  if (Phi->getParent() != L->getHeader())
    return false;

  // We're looking for a simple add recurrence.
  BinaryOperator *IncInstruction;
  Value *Start, *IncrementPerRound;
  if (!matchSimpleRecurrence(Phi, IncInstruction, Start, IncrementPerRound) ||
      IncInstruction->getOpcode() != Instruction::Add)
    return false;

  int IncrementingBlock = Phi->getIncomingValue(0) == IncInstruction ? 0 : 1;

  // Get the value that is added to/multiplied with the phi
  Value *OffsSecondOperand = Offs->getOperand(OffsSecondOp);

  if (IncrementPerRound->getType() != OffsSecondOperand->getType() ||
      !L->isLoopInvariant(OffsSecondOperand))
    // Something has gone wrong, abort
    return false;

  // Only proceed if the increment per round is a constant or an instruction
  // which does not originate from within the loop
  if (!isa<Constant>(IncrementPerRound) &&
      !(isa<Instruction>(IncrementPerRound) &&
        !L->contains(cast<Instruction>(IncrementPerRound))))
    return false;

  // If the phi is not used by anything else, we can just adapt it when
  // replacing the instruction; if it is, we'll have to duplicate it
  PHINode *NewPhi;
  if (Phi->getNumUses() == 2) {
    // No other users -> reuse existing phi (One user is the instruction
    // we're looking at, the other is the phi increment)
    if (IncInstruction->getNumUses() != 1) {
      // If the incrementing instruction does have more users than
      // our phi, we need to copy it
      IncInstruction = BinaryOperator::Create(
          Instruction::BinaryOps(IncInstruction->getOpcode()), Phi,
          IncrementPerRound, "LoopIncrement", IncInstruction->getIterator());
      Phi->setIncomingValue(IncrementingBlock, IncInstruction);
    }
    NewPhi = Phi;
  } else {
    // There are other users -> create a new phi
    NewPhi = PHINode::Create(Phi->getType(), 2, "NewPhi", Phi->getIterator());
    // Copy the incoming values of the old phi
    NewPhi->addIncoming(Phi->getIncomingValue(IncrementingBlock == 1 ? 0 : 1),
                        Phi->getIncomingBlock(IncrementingBlock == 1 ? 0 : 1));
    IncInstruction = BinaryOperator::Create(
        Instruction::BinaryOps(IncInstruction->getOpcode()), NewPhi,
        IncrementPerRound, "LoopIncrement", IncInstruction->getIterator());
    NewPhi->addIncoming(IncInstruction,
                        Phi->getIncomingBlock(IncrementingBlock));
    IncrementingBlock = 1;
  }

  IRBuilder<> Builder(BB->getContext());
  Builder.SetInsertPoint(Phi);
  Builder.SetCurrentDebugLocation(Offs->getDebugLoc());

  switch (Offs->getOpcode()) {
  case Instruction::Add:
  case Instruction::Or:
    pushOutAdd(NewPhi, OffsSecondOperand, IncrementingBlock == 1 ? 0 : 1);
    break;
  case Instruction::Mul:
  case Instruction::Shl:
    pushOutMulShl(Offs->getOpcode(), NewPhi, IncrementPerRound,
                  OffsSecondOperand, IncrementingBlock, Builder);
    break;
  default:
    return false;
  }
  LLVM_DEBUG(dbgs() << "masked gathers/scatters: simplified loop variable "
                    << "add/mul\n");

  // The instruction has now been "absorbed" into the phi value
  Offs->replaceAllUsesWith(NewPhi);
  if (Offs->hasNUses(0))
    Offs->eraseFromParent();
  // Clean up the old increment in case it's unused because we built a new
  // one
  if (IncInstruction->hasNUses(0))
    IncInstruction->eraseFromParent();

  return true;
}

static Value *CheckAndCreateOffsetAdd(Value *X, unsigned ScaleX, Value *Y,
                                      unsigned ScaleY, IRBuilder<> &Builder) {
  // Splat the non-vector value to a vector of the given type - if the value is
  // a constant (and its value isn't too big), we can even use this opportunity
  // to scale it to the size of the vector elements
  auto FixSummands = [&Builder](FixedVectorType *&VT, Value *&NonVectorVal) {
    ConstantInt *Const;
    if ((Const = dyn_cast<ConstantInt>(NonVectorVal)) &&
        VT->getElementType() != NonVectorVal->getType()) {
      unsigned TargetElemSize = VT->getElementType()->getPrimitiveSizeInBits();
      uint64_t N = Const->getZExtValue();
      if (N < (unsigned)(1 << (TargetElemSize - 1))) {
        NonVectorVal = Builder.CreateVectorSplat(
            VT->getNumElements(), Builder.getIntN(TargetElemSize, N));
        return;
      }
    }
    NonVectorVal =
        Builder.CreateVectorSplat(VT->getNumElements(), NonVectorVal);
  };

  FixedVectorType *XElType = dyn_cast<FixedVectorType>(X->getType());
  FixedVectorType *YElType = dyn_cast<FixedVectorType>(Y->getType());
  // If one of X, Y is not a vector, we have to splat it in order
  // to add the two of them.
  if (XElType && !YElType) {
    FixSummands(XElType, Y);
    YElType = cast<FixedVectorType>(Y->getType());
  } else if (YElType && !XElType) {
    FixSummands(YElType, X);
    XElType = cast<FixedVectorType>(X->getType());
  }
  assert(XElType && YElType && "Unknown vector types");
  // Check that the summands are of compatible types
  if (XElType != YElType) {
    LLVM_DEBUG(dbgs() << "masked gathers/scatters: incompatible gep offsets\n");
    return nullptr;
  }

  if (XElType->getElementType()->getScalarSizeInBits() != 32) {
    // Check that by adding the vectors we do not accidentally
    // create an overflow
    Constant *ConstX = dyn_cast<Constant>(X);
    Constant *ConstY = dyn_cast<Constant>(Y);
    if (!ConstX || !ConstY)
      return nullptr;
    unsigned TargetElemSize = 128 / XElType->getNumElements();
    for (unsigned i = 0; i < XElType->getNumElements(); i++) {
      ConstantInt *ConstXEl =
          dyn_cast<ConstantInt>(ConstX->getAggregateElement(i));
      ConstantInt *ConstYEl =
          dyn_cast<ConstantInt>(ConstY->getAggregateElement(i));
      if (!ConstXEl || !ConstYEl ||
          ConstXEl->getZExtValue() * ScaleX +
                  ConstYEl->getZExtValue() * ScaleY >=
              (unsigned)(1 << (TargetElemSize - 1)))
        return nullptr;
    }
  }

  Value *XScale = Builder.CreateVectorSplat(
      XElType->getNumElements(),
      Builder.getIntN(XElType->getScalarSizeInBits(), ScaleX));
  Value *YScale = Builder.CreateVectorSplat(
      YElType->getNumElements(),
      Builder.getIntN(YElType->getScalarSizeInBits(), ScaleY));
  Value *Add = Builder.CreateAdd(Builder.CreateMul(X, XScale),
                                 Builder.CreateMul(Y, YScale));

  if (checkOffsetSize(Add, XElType->getNumElements()))
    return Add;
  else
    return nullptr;
}

Value *MVEGatherScatterLowering::foldGEP(GetElementPtrInst *GEP,
                                         Value *&Offsets, unsigned &Scale,
                                         IRBuilder<> &Builder) {
  Value *GEPPtr = GEP->getPointerOperand();
  Offsets = GEP->getOperand(1);
  Scale = DL->getTypeAllocSize(GEP->getSourceElementType());
  // We only merge geps with constant offsets, because only for those
  // we can make sure that we do not cause an overflow
  if (GEP->getNumIndices() != 1 || !isa<Constant>(Offsets))
    return nullptr;
  if (GetElementPtrInst *BaseGEP = dyn_cast<GetElementPtrInst>(GEPPtr)) {
    // Merge the two geps into one
    Value *BaseBasePtr = foldGEP(BaseGEP, Offsets, Scale, Builder);
    if (!BaseBasePtr)
      return nullptr;
    Offsets = CheckAndCreateOffsetAdd(
        Offsets, Scale, GEP->getOperand(1),
        DL->getTypeAllocSize(GEP->getSourceElementType()), Builder);
    if (Offsets == nullptr)
      return nullptr;
    Scale = 1; // Scale is always an i8 at this point.
    return BaseBasePtr;
  }
  return GEPPtr;
}

bool MVEGatherScatterLowering::optimiseAddress(Value *Address, BasicBlock *BB,
                                               LoopInfo *LI) {
  GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Address);
  if (!GEP)
    return false;
  bool Changed = false;
  if (GEP->hasOneUse() && isa<GetElementPtrInst>(GEP->getPointerOperand())) {
    IRBuilder<> Builder(GEP->getContext());
    Builder.SetInsertPoint(GEP);
    Builder.SetCurrentDebugLocation(GEP->getDebugLoc());
    Value *Offsets;
    unsigned Scale;
    Value *Base = foldGEP(GEP, Offsets, Scale, Builder);
    // We only want to merge the geps if there is a real chance that they can be
    // used by an MVE gather; thus the offset has to have the correct size
    // (always i32 if it is not of vector type) and the base has to be a
    // pointer.
    if (Offsets && Base && Base != GEP) {
      assert(Scale == 1 && "Expected to fold GEP to a scale of 1");
      Type *BaseTy = Builder.getPtrTy();
      if (auto *VecTy = dyn_cast<FixedVectorType>(Base->getType()))
        BaseTy = FixedVectorType::get(BaseTy, VecTy);
      GetElementPtrInst *NewAddress = GetElementPtrInst::Create(
          Builder.getInt8Ty(), Builder.CreateBitCast(Base, BaseTy), Offsets,
          "gep.merged", GEP->getIterator());
      LLVM_DEBUG(dbgs() << "Folded GEP: " << *GEP
                        << "\n      new :  " << *NewAddress << "\n");
      GEP->replaceAllUsesWith(
          Builder.CreateBitCast(NewAddress, GEP->getType()));
      GEP = NewAddress;
      Changed = true;
    }
  }
  Changed |= optimiseOffsets(GEP->getOperand(1), GEP->getParent(), LI);
  return Changed;
}

bool MVEGatherScatterLowering::runOnFunction(Function &F) {
  if (!EnableMaskedGatherScatters)
    return false;
  auto &TPC = getAnalysis<TargetPassConfig>();
  auto &TM = TPC.getTM<TargetMachine>();
  auto *ST = &TM.getSubtarget<ARMSubtarget>(F);
  if (!ST->hasMVEIntegerOps())
    return false;
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  DL = &F.getDataLayout();
  SmallVector<IntrinsicInst *, 4> Gathers;
  SmallVector<IntrinsicInst *, 4> Scatters;

  bool Changed = false;

  for (BasicBlock &BB : F) {
    Changed |= SimplifyInstructionsInBlock(&BB);

    for (Instruction &I : BB) {
      IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I);
      if (II && II->getIntrinsicID() == Intrinsic::masked_gather &&
          isa<FixedVectorType>(II->getType())) {
        Gathers.push_back(II);
        Changed |= optimiseAddress(II->getArgOperand(0), II->getParent(), LI);
      } else if (II && II->getIntrinsicID() == Intrinsic::masked_scatter &&
                 isa<FixedVectorType>(II->getArgOperand(0)->getType())) {
        Scatters.push_back(II);
        Changed |= optimiseAddress(II->getArgOperand(1), II->getParent(), LI);
      }
    }
  }
  for (IntrinsicInst *I : Gathers) {
    Instruction *L = lowerGather(I);
    if (L == nullptr)
      continue;

    // Get rid of any now dead instructions
    SimplifyInstructionsInBlock(L->getParent());
    Changed = true;
  }

  for (IntrinsicInst *I : Scatters) {
    Instruction *S = lowerScatter(I);
    if (S == nullptr)
      continue;

    // Get rid of any now dead instructions
    SimplifyInstructionsInBlock(S->getParent());
    Changed = true;
  }
  return Changed;
}
