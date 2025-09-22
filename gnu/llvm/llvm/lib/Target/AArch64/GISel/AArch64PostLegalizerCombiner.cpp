//=== AArch64PostLegalizerCombiner.cpp --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Post-legalization combines on generic MachineInstrs.
///
/// The combines here must preserve instruction legality.
///
/// Lowering combines (e.g. pseudo matching) should be handled by
/// AArch64PostLegalizerLowering.
///
/// Combines which don't rely on instruction legality should go in the
/// AArch64PreLegalizerCombiner.
///
//===----------------------------------------------------------------------===//

#include "AArch64TargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/CSEMIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/Debug.h"

#define GET_GICOMBINER_DEPS
#include "AArch64GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

#define DEBUG_TYPE "aarch64-postlegalizer-combiner"

using namespace llvm;
using namespace MIPatternMatch;

namespace {

#define GET_GICOMBINER_TYPES
#include "AArch64GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_TYPES

/// This combine tries do what performExtractVectorEltCombine does in SDAG.
/// Rewrite for pairwise fadd pattern
///   (s32 (g_extract_vector_elt
///           (g_fadd (vXs32 Other)
///                  (g_vector_shuffle (vXs32 Other) undef <1,X,...> )) 0))
/// ->
///   (s32 (g_fadd (g_extract_vector_elt (vXs32 Other) 0)
///              (g_extract_vector_elt (vXs32 Other) 1))
bool matchExtractVecEltPairwiseAdd(
    MachineInstr &MI, MachineRegisterInfo &MRI,
    std::tuple<unsigned, LLT, Register> &MatchInfo) {
  Register Src1 = MI.getOperand(1).getReg();
  Register Src2 = MI.getOperand(2).getReg();
  LLT DstTy = MRI.getType(MI.getOperand(0).getReg());

  auto Cst = getIConstantVRegValWithLookThrough(Src2, MRI);
  if (!Cst || Cst->Value != 0)
    return false;
  // SDAG also checks for FullFP16, but this looks to be beneficial anyway.

  // Now check for an fadd operation. TODO: expand this for integer add?
  auto *FAddMI = getOpcodeDef(TargetOpcode::G_FADD, Src1, MRI);
  if (!FAddMI)
    return false;

  // If we add support for integer add, must restrict these types to just s64.
  unsigned DstSize = DstTy.getSizeInBits();
  if (DstSize != 16 && DstSize != 32 && DstSize != 64)
    return false;

  Register Src1Op1 = FAddMI->getOperand(1).getReg();
  Register Src1Op2 = FAddMI->getOperand(2).getReg();
  MachineInstr *Shuffle =
      getOpcodeDef(TargetOpcode::G_SHUFFLE_VECTOR, Src1Op2, MRI);
  MachineInstr *Other = MRI.getVRegDef(Src1Op1);
  if (!Shuffle) {
    Shuffle = getOpcodeDef(TargetOpcode::G_SHUFFLE_VECTOR, Src1Op1, MRI);
    Other = MRI.getVRegDef(Src1Op2);
  }

  // We're looking for a shuffle that moves the second element to index 0.
  if (Shuffle && Shuffle->getOperand(3).getShuffleMask()[0] == 1 &&
      Other == MRI.getVRegDef(Shuffle->getOperand(1).getReg())) {
    std::get<0>(MatchInfo) = TargetOpcode::G_FADD;
    std::get<1>(MatchInfo) = DstTy;
    std::get<2>(MatchInfo) = Other->getOperand(0).getReg();
    return true;
  }
  return false;
}

void applyExtractVecEltPairwiseAdd(
    MachineInstr &MI, MachineRegisterInfo &MRI, MachineIRBuilder &B,
    std::tuple<unsigned, LLT, Register> &MatchInfo) {
  unsigned Opc = std::get<0>(MatchInfo);
  assert(Opc == TargetOpcode::G_FADD && "Unexpected opcode!");
  // We want to generate two extracts of elements 0 and 1, and add them.
  LLT Ty = std::get<1>(MatchInfo);
  Register Src = std::get<2>(MatchInfo);
  LLT s64 = LLT::scalar(64);
  B.setInstrAndDebugLoc(MI);
  auto Elt0 = B.buildExtractVectorElement(Ty, Src, B.buildConstant(s64, 0));
  auto Elt1 = B.buildExtractVectorElement(Ty, Src, B.buildConstant(s64, 1));
  B.buildInstr(Opc, {MI.getOperand(0).getReg()}, {Elt0, Elt1});
  MI.eraseFromParent();
}

bool isSignExtended(Register R, MachineRegisterInfo &MRI) {
  // TODO: check if extended build vector as well.
  unsigned Opc = MRI.getVRegDef(R)->getOpcode();
  return Opc == TargetOpcode::G_SEXT || Opc == TargetOpcode::G_SEXT_INREG;
}

bool isZeroExtended(Register R, MachineRegisterInfo &MRI) {
  // TODO: check if extended build vector as well.
  return MRI.getVRegDef(R)->getOpcode() == TargetOpcode::G_ZEXT;
}

bool matchAArch64MulConstCombine(
    MachineInstr &MI, MachineRegisterInfo &MRI,
    std::function<void(MachineIRBuilder &B, Register DstReg)> &ApplyFn) {
  assert(MI.getOpcode() == TargetOpcode::G_MUL);
  Register LHS = MI.getOperand(1).getReg();
  Register RHS = MI.getOperand(2).getReg();
  Register Dst = MI.getOperand(0).getReg();
  const LLT Ty = MRI.getType(LHS);

  // The below optimizations require a constant RHS.
  auto Const = getIConstantVRegValWithLookThrough(RHS, MRI);
  if (!Const)
    return false;

  APInt ConstValue = Const->Value.sext(Ty.getSizeInBits());
  // The following code is ported from AArch64ISelLowering.
  // Multiplication of a power of two plus/minus one can be done more
  // cheaply as shift+add/sub. For now, this is true unilaterally. If
  // future CPUs have a cheaper MADD instruction, this may need to be
  // gated on a subtarget feature. For Cyclone, 32-bit MADD is 4 cycles and
  // 64-bit is 5 cycles, so this is always a win.
  // More aggressively, some multiplications N0 * C can be lowered to
  // shift+add+shift if the constant C = A * B where A = 2^N + 1 and B = 2^M,
  // e.g. 6=3*2=(2+1)*2.
  // TODO: consider lowering more cases, e.g. C = 14, -6, -14 or even 45
  // which equals to (1+2)*16-(1+2).
  // TrailingZeroes is used to test if the mul can be lowered to
  // shift+add+shift.
  unsigned TrailingZeroes = ConstValue.countr_zero();
  if (TrailingZeroes) {
    // Conservatively do not lower to shift+add+shift if the mul might be
    // folded into smul or umul.
    if (MRI.hasOneNonDBGUse(LHS) &&
        (isSignExtended(LHS, MRI) || isZeroExtended(LHS, MRI)))
      return false;
    // Conservatively do not lower to shift+add+shift if the mul might be
    // folded into madd or msub.
    if (MRI.hasOneNonDBGUse(Dst)) {
      MachineInstr &UseMI = *MRI.use_instr_begin(Dst);
      unsigned UseOpc = UseMI.getOpcode();
      if (UseOpc == TargetOpcode::G_ADD || UseOpc == TargetOpcode::G_PTR_ADD ||
          UseOpc == TargetOpcode::G_SUB)
        return false;
    }
  }
  // Use ShiftedConstValue instead of ConstValue to support both shift+add/sub
  // and shift+add+shift.
  APInt ShiftedConstValue = ConstValue.ashr(TrailingZeroes);

  unsigned ShiftAmt, AddSubOpc;
  // Is the shifted value the LHS operand of the add/sub?
  bool ShiftValUseIsLHS = true;
  // Do we need to negate the result?
  bool NegateResult = false;

  if (ConstValue.isNonNegative()) {
    // (mul x, 2^N + 1) => (add (shl x, N), x)
    // (mul x, 2^N - 1) => (sub (shl x, N), x)
    // (mul x, (2^N + 1) * 2^M) => (shl (add (shl x, N), x), M)
    APInt SCVMinus1 = ShiftedConstValue - 1;
    APInt CVPlus1 = ConstValue + 1;
    if (SCVMinus1.isPowerOf2()) {
      ShiftAmt = SCVMinus1.logBase2();
      AddSubOpc = TargetOpcode::G_ADD;
    } else if (CVPlus1.isPowerOf2()) {
      ShiftAmt = CVPlus1.logBase2();
      AddSubOpc = TargetOpcode::G_SUB;
    } else
      return false;
  } else {
    // (mul x, -(2^N - 1)) => (sub x, (shl x, N))
    // (mul x, -(2^N + 1)) => - (add (shl x, N), x)
    APInt CVNegPlus1 = -ConstValue + 1;
    APInt CVNegMinus1 = -ConstValue - 1;
    if (CVNegPlus1.isPowerOf2()) {
      ShiftAmt = CVNegPlus1.logBase2();
      AddSubOpc = TargetOpcode::G_SUB;
      ShiftValUseIsLHS = false;
    } else if (CVNegMinus1.isPowerOf2()) {
      ShiftAmt = CVNegMinus1.logBase2();
      AddSubOpc = TargetOpcode::G_ADD;
      NegateResult = true;
    } else
      return false;
  }

  if (NegateResult && TrailingZeroes)
    return false;

  ApplyFn = [=](MachineIRBuilder &B, Register DstReg) {
    auto Shift = B.buildConstant(LLT::scalar(64), ShiftAmt);
    auto ShiftedVal = B.buildShl(Ty, LHS, Shift);

    Register AddSubLHS = ShiftValUseIsLHS ? ShiftedVal.getReg(0) : LHS;
    Register AddSubRHS = ShiftValUseIsLHS ? LHS : ShiftedVal.getReg(0);
    auto Res = B.buildInstr(AddSubOpc, {Ty}, {AddSubLHS, AddSubRHS});
    assert(!(NegateResult && TrailingZeroes) &&
           "NegateResult and TrailingZeroes cannot both be true for now.");
    // Negate the result.
    if (NegateResult) {
      B.buildSub(DstReg, B.buildConstant(Ty, 0), Res);
      return;
    }
    // Shift the result.
    if (TrailingZeroes) {
      B.buildShl(DstReg, Res, B.buildConstant(LLT::scalar(64), TrailingZeroes));
      return;
    }
    B.buildCopy(DstReg, Res.getReg(0));
  };
  return true;
}

void applyAArch64MulConstCombine(
    MachineInstr &MI, MachineRegisterInfo &MRI, MachineIRBuilder &B,
    std::function<void(MachineIRBuilder &B, Register DstReg)> &ApplyFn) {
  B.setInstrAndDebugLoc(MI);
  ApplyFn(B, MI.getOperand(0).getReg());
  MI.eraseFromParent();
}

/// Try to fold a G_MERGE_VALUES of 2 s32 sources, where the second source
/// is a zero, into a G_ZEXT of the first.
bool matchFoldMergeToZext(MachineInstr &MI, MachineRegisterInfo &MRI) {
  auto &Merge = cast<GMerge>(MI);
  LLT SrcTy = MRI.getType(Merge.getSourceReg(0));
  if (SrcTy != LLT::scalar(32) || Merge.getNumSources() != 2)
    return false;
  return mi_match(Merge.getSourceReg(1), MRI, m_SpecificICst(0));
}

void applyFoldMergeToZext(MachineInstr &MI, MachineRegisterInfo &MRI,
                          MachineIRBuilder &B, GISelChangeObserver &Observer) {
  // Mutate %d(s64) = G_MERGE_VALUES %a(s32), 0(s32)
  //  ->
  // %d(s64) = G_ZEXT %a(s32)
  Observer.changingInstr(MI);
  MI.setDesc(B.getTII().get(TargetOpcode::G_ZEXT));
  MI.removeOperand(2);
  Observer.changedInstr(MI);
}

/// \returns True if a G_ANYEXT instruction \p MI should be mutated to a G_ZEXT
/// instruction.
bool matchMutateAnyExtToZExt(MachineInstr &MI, MachineRegisterInfo &MRI) {
  // If this is coming from a scalar compare then we can use a G_ZEXT instead of
  // a G_ANYEXT:
  //
  // %cmp:_(s32) = G_[I|F]CMP ... <-- produces 0/1.
  // %ext:_(s64) = G_ANYEXT %cmp(s32)
  //
  // By doing this, we can leverage more KnownBits combines.
  assert(MI.getOpcode() == TargetOpcode::G_ANYEXT);
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  return MRI.getType(Dst).isScalar() &&
         mi_match(Src, MRI,
                  m_any_of(m_GICmp(m_Pred(), m_Reg(), m_Reg()),
                           m_GFCmp(m_Pred(), m_Reg(), m_Reg())));
}

void applyMutateAnyExtToZExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                             MachineIRBuilder &B,
                             GISelChangeObserver &Observer) {
  Observer.changingInstr(MI);
  MI.setDesc(B.getTII().get(TargetOpcode::G_ZEXT));
  Observer.changedInstr(MI);
}

/// Match a 128b store of zero and split it into two 64 bit stores, for
/// size/performance reasons.
bool matchSplitStoreZero128(MachineInstr &MI, MachineRegisterInfo &MRI) {
  GStore &Store = cast<GStore>(MI);
  if (!Store.isSimple())
    return false;
  LLT ValTy = MRI.getType(Store.getValueReg());
  if (ValTy.isScalableVector())
    return false;
  if (!ValTy.isVector() || ValTy.getSizeInBits() != 128)
    return false;
  if (Store.getMemSizeInBits() != ValTy.getSizeInBits())
    return false; // Don't split truncating stores.
  if (!MRI.hasOneNonDBGUse(Store.getValueReg()))
    return false;
  auto MaybeCst = isConstantOrConstantSplatVector(
      *MRI.getVRegDef(Store.getValueReg()), MRI);
  return MaybeCst && MaybeCst->isZero();
}

void applySplitStoreZero128(MachineInstr &MI, MachineRegisterInfo &MRI,
                            MachineIRBuilder &B,
                            GISelChangeObserver &Observer) {
  B.setInstrAndDebugLoc(MI);
  GStore &Store = cast<GStore>(MI);
  assert(MRI.getType(Store.getValueReg()).isVector() &&
         "Expected a vector store value");
  LLT NewTy = LLT::scalar(64);
  Register PtrReg = Store.getPointerReg();
  auto Zero = B.buildConstant(NewTy, 0);
  auto HighPtr = B.buildPtrAdd(MRI.getType(PtrReg), PtrReg,
                               B.buildConstant(LLT::scalar(64), 8));
  auto &MF = *MI.getMF();
  auto *LowMMO = MF.getMachineMemOperand(&Store.getMMO(), 0, NewTy);
  auto *HighMMO = MF.getMachineMemOperand(&Store.getMMO(), 8, NewTy);
  B.buildStore(Zero, PtrReg, *LowMMO);
  B.buildStore(Zero, HighPtr, *HighMMO);
  Store.eraseFromParent();
}

bool matchOrToBSP(MachineInstr &MI, MachineRegisterInfo &MRI,
                  std::tuple<Register, Register, Register> &MatchInfo) {
  const LLT DstTy = MRI.getType(MI.getOperand(0).getReg());
  if (!DstTy.isVector())
    return false;

  Register AO1, AO2, BVO1, BVO2;
  if (!mi_match(MI, MRI,
                m_GOr(m_GAnd(m_Reg(AO1), m_Reg(BVO1)),
                      m_GAnd(m_Reg(AO2), m_Reg(BVO2)))))
    return false;

  auto *BV1 = getOpcodeDef<GBuildVector>(BVO1, MRI);
  auto *BV2 = getOpcodeDef<GBuildVector>(BVO2, MRI);
  if (!BV1 || !BV2)
    return false;

  for (int I = 0, E = DstTy.getNumElements(); I < E; I++) {
    auto ValAndVReg1 =
        getIConstantVRegValWithLookThrough(BV1->getSourceReg(I), MRI);
    auto ValAndVReg2 =
        getIConstantVRegValWithLookThrough(BV2->getSourceReg(I), MRI);
    if (!ValAndVReg1 || !ValAndVReg2 ||
        ValAndVReg1->Value != ~ValAndVReg2->Value)
      return false;
  }

  MatchInfo = {AO1, AO2, BVO1};
  return true;
}

void applyOrToBSP(MachineInstr &MI, MachineRegisterInfo &MRI,
                  MachineIRBuilder &B,
                  std::tuple<Register, Register, Register> &MatchInfo) {
  B.setInstrAndDebugLoc(MI);
  B.buildInstr(
      AArch64::G_BSP, {MI.getOperand(0).getReg()},
      {std::get<2>(MatchInfo), std::get<0>(MatchInfo), std::get<1>(MatchInfo)});
  MI.eraseFromParent();
}

// Combines Mul(And(Srl(X, 15), 0x10001), 0xffff) into CMLTz
bool matchCombineMulCMLT(MachineInstr &MI, MachineRegisterInfo &MRI,
                         Register &SrcReg) {
  LLT DstTy = MRI.getType(MI.getOperand(0).getReg());

  if (DstTy != LLT::fixed_vector(2, 64) && DstTy != LLT::fixed_vector(2, 32) &&
      DstTy != LLT::fixed_vector(4, 32) && DstTy != LLT::fixed_vector(4, 16) &&
      DstTy != LLT::fixed_vector(8, 16))
    return false;

  auto AndMI = getDefIgnoringCopies(MI.getOperand(1).getReg(), MRI);
  if (AndMI->getOpcode() != TargetOpcode::G_AND)
    return false;
  auto LShrMI = getDefIgnoringCopies(AndMI->getOperand(1).getReg(), MRI);
  if (LShrMI->getOpcode() != TargetOpcode::G_LSHR)
    return false;

  // Check the constant splat values
  auto V1 = isConstantOrConstantSplatVector(
      *MRI.getVRegDef(MI.getOperand(2).getReg()), MRI);
  auto V2 = isConstantOrConstantSplatVector(
      *MRI.getVRegDef(AndMI->getOperand(2).getReg()), MRI);
  auto V3 = isConstantOrConstantSplatVector(
      *MRI.getVRegDef(LShrMI->getOperand(2).getReg()), MRI);
  if (!V1.has_value() || !V2.has_value() || !V3.has_value())
    return false;
  unsigned HalfSize = DstTy.getScalarSizeInBits() / 2;
  if (!V1.value().isMask(HalfSize) || V2.value() != (1ULL | 1ULL << HalfSize) ||
      V3 != (HalfSize - 1))
    return false;

  SrcReg = LShrMI->getOperand(1).getReg();

  return true;
}

void applyCombineMulCMLT(MachineInstr &MI, MachineRegisterInfo &MRI,
                         MachineIRBuilder &B, Register &SrcReg) {
  Register DstReg = MI.getOperand(0).getReg();
  LLT DstTy = MRI.getType(DstReg);
  LLT HalfTy =
      DstTy.changeElementCount(DstTy.getElementCount().multiplyCoefficientBy(2))
          .changeElementSize(DstTy.getScalarSizeInBits() / 2);

  Register ZeroVec = B.buildConstant(HalfTy, 0).getReg(0);
  Register CastReg =
      B.buildInstr(TargetOpcode::G_BITCAST, {HalfTy}, {SrcReg}).getReg(0);
  Register CMLTReg =
      B.buildICmp(CmpInst::Predicate::ICMP_SLT, HalfTy, CastReg, ZeroVec)
          .getReg(0);

  B.buildInstr(TargetOpcode::G_BITCAST, {DstReg}, {CMLTReg}).getReg(0);
  MI.eraseFromParent();
}

class AArch64PostLegalizerCombinerImpl : public Combiner {
protected:
  // TODO: Make CombinerHelper methods const.
  mutable CombinerHelper Helper;
  const AArch64PostLegalizerCombinerImplRuleConfig &RuleConfig;
  const AArch64Subtarget &STI;

public:
  AArch64PostLegalizerCombinerImpl(
      MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
      GISelKnownBits &KB, GISelCSEInfo *CSEInfo,
      const AArch64PostLegalizerCombinerImplRuleConfig &RuleConfig,
      const AArch64Subtarget &STI, MachineDominatorTree *MDT,
      const LegalizerInfo *LI);

  static const char *getName() { return "AArch64PostLegalizerCombiner"; }

  bool tryCombineAll(MachineInstr &I) const override;

private:
#define GET_GICOMBINER_CLASS_MEMBERS
#include "AArch64GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CLASS_MEMBERS
};

#define GET_GICOMBINER_IMPL
#include "AArch64GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_IMPL

AArch64PostLegalizerCombinerImpl::AArch64PostLegalizerCombinerImpl(
    MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
    GISelKnownBits &KB, GISelCSEInfo *CSEInfo,
    const AArch64PostLegalizerCombinerImplRuleConfig &RuleConfig,
    const AArch64Subtarget &STI, MachineDominatorTree *MDT,
    const LegalizerInfo *LI)
    : Combiner(MF, CInfo, TPC, &KB, CSEInfo),
      Helper(Observer, B, /*IsPreLegalize*/ false, &KB, MDT, LI),
      RuleConfig(RuleConfig), STI(STI),
#define GET_GICOMBINER_CONSTRUCTOR_INITS
#include "AArch64GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CONSTRUCTOR_INITS
{
}

class AArch64PostLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  AArch64PostLegalizerCombiner(bool IsOptNone = false);

  StringRef getPassName() const override {
    return "AArch64PostLegalizerCombiner";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  bool IsOptNone;
  AArch64PostLegalizerCombinerImplRuleConfig RuleConfig;


  struct StoreInfo {
    GStore *St = nullptr;
    // The G_PTR_ADD that's used by the store. We keep this to cache the
    // MachineInstr def.
    GPtrAdd *Ptr = nullptr;
    // The signed offset to the Ptr instruction.
    int64_t Offset = 0;
    LLT StoredType;
  };
  bool tryOptimizeConsecStores(SmallVectorImpl<StoreInfo> &Stores,
                               CSEMIRBuilder &MIB);

  bool optimizeConsecutiveMemOpAddressing(MachineFunction &MF,
                                          CSEMIRBuilder &MIB);
};
} // end anonymous namespace

void AArch64PostLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  AU.addRequired<GISelKnownBitsAnalysis>();
  AU.addPreserved<GISelKnownBitsAnalysis>();
  if (!IsOptNone) {
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    AU.addRequired<GISelCSEAnalysisWrapperPass>();
    AU.addPreserved<GISelCSEAnalysisWrapperPass>();
  }
  MachineFunctionPass::getAnalysisUsage(AU);
}

AArch64PostLegalizerCombiner::AArch64PostLegalizerCombiner(bool IsOptNone)
    : MachineFunctionPass(ID), IsOptNone(IsOptNone) {
  initializeAArch64PostLegalizerCombinerPass(*PassRegistry::getPassRegistry());

  if (!RuleConfig.parseCommandLineOption())
    report_fatal_error("Invalid rule identifier");
}

bool AArch64PostLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;
  assert(MF.getProperties().hasProperty(
             MachineFunctionProperties::Property::Legalized) &&
         "Expected a legalized function?");
  auto *TPC = &getAnalysis<TargetPassConfig>();
  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None && !skipFunction(F);

  const AArch64Subtarget &ST = MF.getSubtarget<AArch64Subtarget>();
  const auto *LI = ST.getLegalizerInfo();

  GISelKnownBits *KB = &getAnalysis<GISelKnownBitsAnalysis>().get(MF);
  MachineDominatorTree *MDT =
      IsOptNone ? nullptr
                : &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  GISelCSEAnalysisWrapper &Wrapper =
      getAnalysis<GISelCSEAnalysisWrapperPass>().getCSEWrapper();
  auto *CSEInfo = &Wrapper.get(TPC->getCSEConfig());

  CombinerInfo CInfo(/*AllowIllegalOps*/ true, /*ShouldLegalizeIllegal*/ false,
                     /*LegalizerInfo*/ nullptr, EnableOpt, F.hasOptSize(),
                     F.hasMinSize());
  AArch64PostLegalizerCombinerImpl Impl(MF, CInfo, TPC, *KB, CSEInfo,
                                        RuleConfig, ST, MDT, LI);
  bool Changed = Impl.combineMachineInstrs();

  auto MIB = CSEMIRBuilder(MF);
  MIB.setCSEInfo(CSEInfo);
  Changed |= optimizeConsecutiveMemOpAddressing(MF, MIB);
  return Changed;
}

bool AArch64PostLegalizerCombiner::tryOptimizeConsecStores(
    SmallVectorImpl<StoreInfo> &Stores, CSEMIRBuilder &MIB) {
  if (Stores.size() <= 2)
    return false;

  // Profitabity checks:
  int64_t BaseOffset = Stores[0].Offset;
  unsigned NumPairsExpected = Stores.size() / 2;
  unsigned TotalInstsExpected = NumPairsExpected + (Stores.size() % 2);
  // Size savings will depend on whether we can fold the offset, as an
  // immediate of an ADD.
  auto &TLI = *MIB.getMF().getSubtarget().getTargetLowering();
  if (!TLI.isLegalAddImmediate(BaseOffset))
    TotalInstsExpected++;
  int SavingsExpected = Stores.size() - TotalInstsExpected;
  if (SavingsExpected <= 0)
    return false;

  auto &MRI = MIB.getMF().getRegInfo();

  // We have a series of consecutive stores. Factor out the common base
  // pointer and rewrite the offsets.
  Register NewBase = Stores[0].Ptr->getReg(0);
  for (auto &SInfo : Stores) {
    // Compute a new pointer with the new base ptr and adjusted offset.
    MIB.setInstrAndDebugLoc(*SInfo.St);
    auto NewOff = MIB.buildConstant(LLT::scalar(64), SInfo.Offset - BaseOffset);
    auto NewPtr = MIB.buildPtrAdd(MRI.getType(SInfo.St->getPointerReg()),
                                  NewBase, NewOff);
    if (MIB.getObserver())
      MIB.getObserver()->changingInstr(*SInfo.St);
    SInfo.St->getOperand(1).setReg(NewPtr.getReg(0));
    if (MIB.getObserver())
      MIB.getObserver()->changedInstr(*SInfo.St);
  }
  LLVM_DEBUG(dbgs() << "Split a series of " << Stores.size()
                    << " stores into a base pointer and offsets.\n");
  return true;
}

static cl::opt<bool>
    EnableConsecutiveMemOpOpt("aarch64-postlegalizer-consecutive-memops",
                              cl::init(true), cl::Hidden,
                              cl::desc("Enable consecutive memop optimization "
                                       "in AArch64PostLegalizerCombiner"));

bool AArch64PostLegalizerCombiner::optimizeConsecutiveMemOpAddressing(
    MachineFunction &MF, CSEMIRBuilder &MIB) {
  // This combine needs to run after all reassociations/folds on pointer
  // addressing have been done, specifically those that combine two G_PTR_ADDs
  // with constant offsets into a single G_PTR_ADD with a combined offset.
  // The goal of this optimization is to undo that combine in the case where
  // doing so has prevented the formation of pair stores due to illegal
  // addressing modes of STP. The reason that we do it here is because
  // it's much easier to undo the transformation of a series consecutive
  // mem ops, than it is to detect when doing it would be a bad idea looking
  // at a single G_PTR_ADD in the reassociation/ptradd_immed_chain combine.
  //
  // An example:
  //   G_STORE %11:_(<2 x s64>), %base:_(p0) :: (store (<2 x s64>), align 1)
  //   %off1:_(s64) = G_CONSTANT i64 4128
  //   %p1:_(p0) = G_PTR_ADD %0:_, %off1:_(s64)
  //   G_STORE %11:_(<2 x s64>), %p1:_(p0) :: (store (<2 x s64>), align 1)
  //   %off2:_(s64) = G_CONSTANT i64 4144
  //   %p2:_(p0) = G_PTR_ADD %0:_, %off2:_(s64)
  //   G_STORE %11:_(<2 x s64>), %p2:_(p0) :: (store (<2 x s64>), align 1)
  //   %off3:_(s64) = G_CONSTANT i64 4160
  //   %p3:_(p0) = G_PTR_ADD %0:_, %off3:_(s64)
  //   G_STORE %11:_(<2 x s64>), %17:_(p0) :: (store (<2 x s64>), align 1)
  bool Changed = false;
  auto &MRI = MF.getRegInfo();

  if (!EnableConsecutiveMemOpOpt)
    return Changed;

  SmallVector<StoreInfo, 8> Stores;
  // If we see a load, then we keep track of any values defined by it.
  // In the following example, STP formation will fail anyway because
  // the latter store is using a load result that appears after the
  // the prior store. In this situation if we factor out the offset then
  // we increase code size for no benefit.
  //   G_STORE %v1:_(s64), %base:_(p0) :: (store (s64))
  //   %v2:_(s64) = G_LOAD %ldptr:_(p0) :: (load (s64))
  //   G_STORE %v2:_(s64), %base:_(p0) :: (store (s64))
  SmallVector<Register> LoadValsSinceLastStore;

  auto storeIsValid = [&](StoreInfo &Last, StoreInfo New) {
    // Check if this store is consecutive to the last one.
    if (Last.Ptr->getBaseReg() != New.Ptr->getBaseReg() ||
        (Last.Offset + static_cast<int64_t>(Last.StoredType.getSizeInBytes()) !=
         New.Offset) ||
        Last.StoredType != New.StoredType)
      return false;

    // Check if this store is using a load result that appears after the
    // last store. If so, bail out.
    if (any_of(LoadValsSinceLastStore, [&](Register LoadVal) {
          return New.St->getValueReg() == LoadVal;
        }))
      return false;

    // Check if the current offset would be too large for STP.
    // If not, then STP formation should be able to handle it, so we don't
    // need to do anything.
    int64_t MaxLegalOffset;
    switch (New.StoredType.getSizeInBits()) {
    case 32:
      MaxLegalOffset = 252;
      break;
    case 64:
      MaxLegalOffset = 504;
      break;
    case 128:
      MaxLegalOffset = 1008;
      break;
    default:
      llvm_unreachable("Unexpected stored type size");
    }
    if (New.Offset < MaxLegalOffset)
      return false;

    // If factoring it out still wouldn't help then don't bother.
    return New.Offset - Stores[0].Offset <= MaxLegalOffset;
  };

  auto resetState = [&]() {
    Stores.clear();
    LoadValsSinceLastStore.clear();
  };

  for (auto &MBB : MF) {
    // We're looking inside a single BB at a time since the memset pattern
    // should only be in a single block.
    resetState();
    for (auto &MI : MBB) {
      // Skip for scalable vectors
      if (auto *LdSt = dyn_cast<GLoadStore>(&MI);
          LdSt && MRI.getType(LdSt->getOperand(0).getReg()).isScalableVector())
        continue;

      if (auto *St = dyn_cast<GStore>(&MI)) {
        Register PtrBaseReg;
        APInt Offset;
        LLT StoredValTy = MRI.getType(St->getValueReg());
        unsigned ValSize = StoredValTy.getSizeInBits();
        if (ValSize < 32 || St->getMMO().getSizeInBits() != ValSize)
          continue;

        Register PtrReg = St->getPointerReg();
        if (mi_match(
                PtrReg, MRI,
                m_OneNonDBGUse(m_GPtrAdd(m_Reg(PtrBaseReg), m_ICst(Offset))))) {
          GPtrAdd *PtrAdd = cast<GPtrAdd>(MRI.getVRegDef(PtrReg));
          StoreInfo New = {St, PtrAdd, Offset.getSExtValue(), StoredValTy};

          if (Stores.empty()) {
            Stores.push_back(New);
            continue;
          }

          // Check if this store is a valid continuation of the sequence.
          auto &Last = Stores.back();
          if (storeIsValid(Last, New)) {
            Stores.push_back(New);
            LoadValsSinceLastStore.clear(); // Reset the load value tracking.
          } else {
            // The store isn't a valid to consider for the prior sequence,
            // so try to optimize what we have so far and start a new sequence.
            Changed |= tryOptimizeConsecStores(Stores, MIB);
            resetState();
            Stores.push_back(New);
          }
        }
      } else if (auto *Ld = dyn_cast<GLoad>(&MI)) {
        LoadValsSinceLastStore.push_back(Ld->getDstReg());
      }
    }
    Changed |= tryOptimizeConsecStores(Stores, MIB);
    resetState();
  }

  return Changed;
}

char AArch64PostLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(AArch64PostLegalizerCombiner, DEBUG_TYPE,
                      "Combine AArch64 MachineInstrs after legalization", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelKnownBitsAnalysis)
INITIALIZE_PASS_END(AArch64PostLegalizerCombiner, DEBUG_TYPE,
                    "Combine AArch64 MachineInstrs after legalization", false,
                    false)

namespace llvm {
FunctionPass *createAArch64PostLegalizerCombiner(bool IsOptNone) {
  return new AArch64PostLegalizerCombiner(IsOptNone);
}
} // end namespace llvm
