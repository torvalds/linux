//===-- llvm/CodeGen/GlobalISel/LegalizerHelper.cpp -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file implements the LegalizerHelper class to legalize
/// individual instructions and the LegalizeMachineIR wrapper pass for the
/// primary legalization.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/LostDebugLocObserver.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <numeric>
#include <optional>

#define DEBUG_TYPE "legalizer"

using namespace llvm;
using namespace LegalizeActions;
using namespace MIPatternMatch;

/// Try to break down \p OrigTy into \p NarrowTy sized pieces.
///
/// Returns the number of \p NarrowTy elements needed to reconstruct \p OrigTy,
/// with any leftover piece as type \p LeftoverTy
///
/// Returns -1 in the first element of the pair if the breakdown is not
/// satisfiable.
static std::pair<int, int>
getNarrowTypeBreakDown(LLT OrigTy, LLT NarrowTy, LLT &LeftoverTy) {
  assert(!LeftoverTy.isValid() && "this is an out argument");

  unsigned Size = OrigTy.getSizeInBits();
  unsigned NarrowSize = NarrowTy.getSizeInBits();
  unsigned NumParts = Size / NarrowSize;
  unsigned LeftoverSize = Size - NumParts * NarrowSize;
  assert(Size > NarrowSize);

  if (LeftoverSize == 0)
    return {NumParts, 0};

  if (NarrowTy.isVector()) {
    unsigned EltSize = OrigTy.getScalarSizeInBits();
    if (LeftoverSize % EltSize != 0)
      return {-1, -1};
    LeftoverTy =
        LLT::scalarOrVector(ElementCount::getFixed(LeftoverSize / EltSize),
                            OrigTy.getElementType());
  } else {
    LeftoverTy = LLT::scalar(LeftoverSize);
  }

  int NumLeftover = LeftoverSize / LeftoverTy.getSizeInBits();
  return std::make_pair(NumParts, NumLeftover);
}

static Type *getFloatTypeForLLT(LLVMContext &Ctx, LLT Ty) {

  if (!Ty.isScalar())
    return nullptr;

  switch (Ty.getSizeInBits()) {
  case 16:
    return Type::getHalfTy(Ctx);
  case 32:
    return Type::getFloatTy(Ctx);
  case 64:
    return Type::getDoubleTy(Ctx);
  case 80:
    return Type::getX86_FP80Ty(Ctx);
  case 128:
    return Type::getFP128Ty(Ctx);
  default:
    return nullptr;
  }
}

LegalizerHelper::LegalizerHelper(MachineFunction &MF,
                                 GISelChangeObserver &Observer,
                                 MachineIRBuilder &Builder)
    : MIRBuilder(Builder), Observer(Observer), MRI(MF.getRegInfo()),
      LI(*MF.getSubtarget().getLegalizerInfo()),
      TLI(*MF.getSubtarget().getTargetLowering()), KB(nullptr) {}

LegalizerHelper::LegalizerHelper(MachineFunction &MF, const LegalizerInfo &LI,
                                 GISelChangeObserver &Observer,
                                 MachineIRBuilder &B, GISelKnownBits *KB)
    : MIRBuilder(B), Observer(Observer), MRI(MF.getRegInfo()), LI(LI),
      TLI(*MF.getSubtarget().getTargetLowering()), KB(KB) {}

LegalizerHelper::LegalizeResult
LegalizerHelper::legalizeInstrStep(MachineInstr &MI,
                                   LostDebugLocObserver &LocObserver) {
  LLVM_DEBUG(dbgs() << "Legalizing: " << MI);

  MIRBuilder.setInstrAndDebugLoc(MI);

  if (isa<GIntrinsic>(MI))
    return LI.legalizeIntrinsic(*this, MI) ? Legalized : UnableToLegalize;
  auto Step = LI.getAction(MI, MRI);
  switch (Step.Action) {
  case Legal:
    LLVM_DEBUG(dbgs() << ".. Already legal\n");
    return AlreadyLegal;
  case Libcall:
    LLVM_DEBUG(dbgs() << ".. Convert to libcall\n");
    return libcall(MI, LocObserver);
  case NarrowScalar:
    LLVM_DEBUG(dbgs() << ".. Narrow scalar\n");
    return narrowScalar(MI, Step.TypeIdx, Step.NewType);
  case WidenScalar:
    LLVM_DEBUG(dbgs() << ".. Widen scalar\n");
    return widenScalar(MI, Step.TypeIdx, Step.NewType);
  case Bitcast:
    LLVM_DEBUG(dbgs() << ".. Bitcast type\n");
    return bitcast(MI, Step.TypeIdx, Step.NewType);
  case Lower:
    LLVM_DEBUG(dbgs() << ".. Lower\n");
    return lower(MI, Step.TypeIdx, Step.NewType);
  case FewerElements:
    LLVM_DEBUG(dbgs() << ".. Reduce number of elements\n");
    return fewerElementsVector(MI, Step.TypeIdx, Step.NewType);
  case MoreElements:
    LLVM_DEBUG(dbgs() << ".. Increase number of elements\n");
    return moreElementsVector(MI, Step.TypeIdx, Step.NewType);
  case Custom:
    LLVM_DEBUG(dbgs() << ".. Custom legalization\n");
    return LI.legalizeCustom(*this, MI, LocObserver) ? Legalized
                                                     : UnableToLegalize;
  default:
    LLVM_DEBUG(dbgs() << ".. Unable to legalize\n");
    return UnableToLegalize;
  }
}

void LegalizerHelper::insertParts(Register DstReg,
                                  LLT ResultTy, LLT PartTy,
                                  ArrayRef<Register> PartRegs,
                                  LLT LeftoverTy,
                                  ArrayRef<Register> LeftoverRegs) {
  if (!LeftoverTy.isValid()) {
    assert(LeftoverRegs.empty());

    if (!ResultTy.isVector()) {
      MIRBuilder.buildMergeLikeInstr(DstReg, PartRegs);
      return;
    }

    if (PartTy.isVector())
      MIRBuilder.buildConcatVectors(DstReg, PartRegs);
    else
      MIRBuilder.buildBuildVector(DstReg, PartRegs);
    return;
  }

  // Merge sub-vectors with different number of elements and insert into DstReg.
  if (ResultTy.isVector()) {
    assert(LeftoverRegs.size() == 1 && "Expected one leftover register");
    SmallVector<Register, 8> AllRegs;
    for (auto Reg : concat<const Register>(PartRegs, LeftoverRegs))
      AllRegs.push_back(Reg);
    return mergeMixedSubvectors(DstReg, AllRegs);
  }

  SmallVector<Register> GCDRegs;
  LLT GCDTy = getGCDType(getGCDType(ResultTy, LeftoverTy), PartTy);
  for (auto PartReg : concat<const Register>(PartRegs, LeftoverRegs))
    extractGCDType(GCDRegs, GCDTy, PartReg);
  LLT ResultLCMTy = buildLCMMergePieces(ResultTy, LeftoverTy, GCDTy, GCDRegs);
  buildWidenedRemergeToDst(DstReg, ResultLCMTy, GCDRegs);
}

void LegalizerHelper::appendVectorElts(SmallVectorImpl<Register> &Elts,
                                       Register Reg) {
  LLT Ty = MRI.getType(Reg);
  SmallVector<Register, 8> RegElts;
  extractParts(Reg, Ty.getScalarType(), Ty.getNumElements(), RegElts,
               MIRBuilder, MRI);
  Elts.append(RegElts);
}

/// Merge \p PartRegs with different types into \p DstReg.
void LegalizerHelper::mergeMixedSubvectors(Register DstReg,
                                           ArrayRef<Register> PartRegs) {
  SmallVector<Register, 8> AllElts;
  for (unsigned i = 0; i < PartRegs.size() - 1; ++i)
    appendVectorElts(AllElts, PartRegs[i]);

  Register Leftover = PartRegs[PartRegs.size() - 1];
  if (!MRI.getType(Leftover).isVector())
    AllElts.push_back(Leftover);
  else
    appendVectorElts(AllElts, Leftover);

  MIRBuilder.buildMergeLikeInstr(DstReg, AllElts);
}

/// Append the result registers of G_UNMERGE_VALUES \p MI to \p Regs.
static void getUnmergeResults(SmallVectorImpl<Register> &Regs,
                              const MachineInstr &MI) {
  assert(MI.getOpcode() == TargetOpcode::G_UNMERGE_VALUES);

  const int StartIdx = Regs.size();
  const int NumResults = MI.getNumOperands() - 1;
  Regs.resize(Regs.size() + NumResults);
  for (int I = 0; I != NumResults; ++I)
    Regs[StartIdx + I] = MI.getOperand(I).getReg();
}

void LegalizerHelper::extractGCDType(SmallVectorImpl<Register> &Parts,
                                     LLT GCDTy, Register SrcReg) {
  LLT SrcTy = MRI.getType(SrcReg);
  if (SrcTy == GCDTy) {
    // If the source already evenly divides the result type, we don't need to do
    // anything.
    Parts.push_back(SrcReg);
  } else {
    // Need to split into common type sized pieces.
    auto Unmerge = MIRBuilder.buildUnmerge(GCDTy, SrcReg);
    getUnmergeResults(Parts, *Unmerge);
  }
}

LLT LegalizerHelper::extractGCDType(SmallVectorImpl<Register> &Parts, LLT DstTy,
                                    LLT NarrowTy, Register SrcReg) {
  LLT SrcTy = MRI.getType(SrcReg);
  LLT GCDTy = getGCDType(getGCDType(SrcTy, NarrowTy), DstTy);
  extractGCDType(Parts, GCDTy, SrcReg);
  return GCDTy;
}

LLT LegalizerHelper::buildLCMMergePieces(LLT DstTy, LLT NarrowTy, LLT GCDTy,
                                         SmallVectorImpl<Register> &VRegs,
                                         unsigned PadStrategy) {
  LLT LCMTy = getLCMType(DstTy, NarrowTy);

  int NumParts = LCMTy.getSizeInBits() / NarrowTy.getSizeInBits();
  int NumSubParts = NarrowTy.getSizeInBits() / GCDTy.getSizeInBits();
  int NumOrigSrc = VRegs.size();

  Register PadReg;

  // Get a value we can use to pad the source value if the sources won't evenly
  // cover the result type.
  if (NumOrigSrc < NumParts * NumSubParts) {
    if (PadStrategy == TargetOpcode::G_ZEXT)
      PadReg = MIRBuilder.buildConstant(GCDTy, 0).getReg(0);
    else if (PadStrategy == TargetOpcode::G_ANYEXT)
      PadReg = MIRBuilder.buildUndef(GCDTy).getReg(0);
    else {
      assert(PadStrategy == TargetOpcode::G_SEXT);

      // Shift the sign bit of the low register through the high register.
      auto ShiftAmt =
        MIRBuilder.buildConstant(LLT::scalar(64), GCDTy.getSizeInBits() - 1);
      PadReg = MIRBuilder.buildAShr(GCDTy, VRegs.back(), ShiftAmt).getReg(0);
    }
  }

  // Registers for the final merge to be produced.
  SmallVector<Register, 4> Remerge(NumParts);

  // Registers needed for intermediate merges, which will be merged into a
  // source for Remerge.
  SmallVector<Register, 4> SubMerge(NumSubParts);

  // Once we've fully read off the end of the original source bits, we can reuse
  // the same high bits for remaining padding elements.
  Register AllPadReg;

  // Build merges to the LCM type to cover the original result type.
  for (int I = 0; I != NumParts; ++I) {
    bool AllMergePartsArePadding = true;

    // Build the requested merges to the requested type.
    for (int J = 0; J != NumSubParts; ++J) {
      int Idx = I * NumSubParts + J;
      if (Idx >= NumOrigSrc) {
        SubMerge[J] = PadReg;
        continue;
      }

      SubMerge[J] = VRegs[Idx];

      // There are meaningful bits here we can't reuse later.
      AllMergePartsArePadding = false;
    }

    // If we've filled up a complete piece with padding bits, we can directly
    // emit the natural sized constant if applicable, rather than a merge of
    // smaller constants.
    if (AllMergePartsArePadding && !AllPadReg) {
      if (PadStrategy == TargetOpcode::G_ANYEXT)
        AllPadReg = MIRBuilder.buildUndef(NarrowTy).getReg(0);
      else if (PadStrategy == TargetOpcode::G_ZEXT)
        AllPadReg = MIRBuilder.buildConstant(NarrowTy, 0).getReg(0);

      // If this is a sign extension, we can't materialize a trivial constant
      // with the right type and have to produce a merge.
    }

    if (AllPadReg) {
      // Avoid creating additional instructions if we're just adding additional
      // copies of padding bits.
      Remerge[I] = AllPadReg;
      continue;
    }

    if (NumSubParts == 1)
      Remerge[I] = SubMerge[0];
    else
      Remerge[I] = MIRBuilder.buildMergeLikeInstr(NarrowTy, SubMerge).getReg(0);

    // In the sign extend padding case, re-use the first all-signbit merge.
    if (AllMergePartsArePadding && !AllPadReg)
      AllPadReg = Remerge[I];
  }

  VRegs = std::move(Remerge);
  return LCMTy;
}

void LegalizerHelper::buildWidenedRemergeToDst(Register DstReg, LLT LCMTy,
                                               ArrayRef<Register> RemergeRegs) {
  LLT DstTy = MRI.getType(DstReg);

  // Create the merge to the widened source, and extract the relevant bits into
  // the result.

  if (DstTy == LCMTy) {
    MIRBuilder.buildMergeLikeInstr(DstReg, RemergeRegs);
    return;
  }

  auto Remerge = MIRBuilder.buildMergeLikeInstr(LCMTy, RemergeRegs);
  if (DstTy.isScalar() && LCMTy.isScalar()) {
    MIRBuilder.buildTrunc(DstReg, Remerge);
    return;
  }

  if (LCMTy.isVector()) {
    unsigned NumDefs = LCMTy.getSizeInBits() / DstTy.getSizeInBits();
    SmallVector<Register, 8> UnmergeDefs(NumDefs);
    UnmergeDefs[0] = DstReg;
    for (unsigned I = 1; I != NumDefs; ++I)
      UnmergeDefs[I] = MRI.createGenericVirtualRegister(DstTy);

    MIRBuilder.buildUnmerge(UnmergeDefs,
                            MIRBuilder.buildMergeLikeInstr(LCMTy, RemergeRegs));
    return;
  }

  llvm_unreachable("unhandled case");
}

static RTLIB::Libcall getRTLibDesc(unsigned Opcode, unsigned Size) {
#define RTLIBCASE_INT(LibcallPrefix)                                           \
  do {                                                                         \
    switch (Size) {                                                            \
    case 32:                                                                   \
      return RTLIB::LibcallPrefix##32;                                         \
    case 64:                                                                   \
      return RTLIB::LibcallPrefix##64;                                         \
    case 128:                                                                  \
      return RTLIB::LibcallPrefix##128;                                        \
    default:                                                                   \
      llvm_unreachable("unexpected size");                                     \
    }                                                                          \
  } while (0)

#define RTLIBCASE(LibcallPrefix)                                               \
  do {                                                                         \
    switch (Size) {                                                            \
    case 32:                                                                   \
      return RTLIB::LibcallPrefix##32;                                         \
    case 64:                                                                   \
      return RTLIB::LibcallPrefix##64;                                         \
    case 80:                                                                   \
      return RTLIB::LibcallPrefix##80;                                         \
    case 128:                                                                  \
      return RTLIB::LibcallPrefix##128;                                        \
    default:                                                                   \
      llvm_unreachable("unexpected size");                                     \
    }                                                                          \
  } while (0)

  switch (Opcode) {
  case TargetOpcode::G_MUL:
    RTLIBCASE_INT(MUL_I);
  case TargetOpcode::G_SDIV:
    RTLIBCASE_INT(SDIV_I);
  case TargetOpcode::G_UDIV:
    RTLIBCASE_INT(UDIV_I);
  case TargetOpcode::G_SREM:
    RTLIBCASE_INT(SREM_I);
  case TargetOpcode::G_UREM:
    RTLIBCASE_INT(UREM_I);
  case TargetOpcode::G_CTLZ_ZERO_UNDEF:
    RTLIBCASE_INT(CTLZ_I);
  case TargetOpcode::G_FADD:
    RTLIBCASE(ADD_F);
  case TargetOpcode::G_FSUB:
    RTLIBCASE(SUB_F);
  case TargetOpcode::G_FMUL:
    RTLIBCASE(MUL_F);
  case TargetOpcode::G_FDIV:
    RTLIBCASE(DIV_F);
  case TargetOpcode::G_FEXP:
    RTLIBCASE(EXP_F);
  case TargetOpcode::G_FEXP2:
    RTLIBCASE(EXP2_F);
  case TargetOpcode::G_FEXP10:
    RTLIBCASE(EXP10_F);
  case TargetOpcode::G_FREM:
    RTLIBCASE(REM_F);
  case TargetOpcode::G_FPOW:
    RTLIBCASE(POW_F);
  case TargetOpcode::G_FPOWI:
    RTLIBCASE(POWI_F);
  case TargetOpcode::G_FMA:
    RTLIBCASE(FMA_F);
  case TargetOpcode::G_FSIN:
    RTLIBCASE(SIN_F);
  case TargetOpcode::G_FCOS:
    RTLIBCASE(COS_F);
  case TargetOpcode::G_FTAN:
    RTLIBCASE(TAN_F);
  case TargetOpcode::G_FASIN:
    RTLIBCASE(ASIN_F);
  case TargetOpcode::G_FACOS:
    RTLIBCASE(ACOS_F);
  case TargetOpcode::G_FATAN:
    RTLIBCASE(ATAN_F);
  case TargetOpcode::G_FSINH:
    RTLIBCASE(SINH_F);
  case TargetOpcode::G_FCOSH:
    RTLIBCASE(COSH_F);
  case TargetOpcode::G_FTANH:
    RTLIBCASE(TANH_F);
  case TargetOpcode::G_FLOG10:
    RTLIBCASE(LOG10_F);
  case TargetOpcode::G_FLOG:
    RTLIBCASE(LOG_F);
  case TargetOpcode::G_FLOG2:
    RTLIBCASE(LOG2_F);
  case TargetOpcode::G_FLDEXP:
    RTLIBCASE(LDEXP_F);
  case TargetOpcode::G_FCEIL:
    RTLIBCASE(CEIL_F);
  case TargetOpcode::G_FFLOOR:
    RTLIBCASE(FLOOR_F);
  case TargetOpcode::G_FMINNUM:
    RTLIBCASE(FMIN_F);
  case TargetOpcode::G_FMAXNUM:
    RTLIBCASE(FMAX_F);
  case TargetOpcode::G_FSQRT:
    RTLIBCASE(SQRT_F);
  case TargetOpcode::G_FRINT:
    RTLIBCASE(RINT_F);
  case TargetOpcode::G_FNEARBYINT:
    RTLIBCASE(NEARBYINT_F);
  case TargetOpcode::G_INTRINSIC_ROUNDEVEN:
    RTLIBCASE(ROUNDEVEN_F);
  case TargetOpcode::G_INTRINSIC_LRINT:
    RTLIBCASE(LRINT_F);
  case TargetOpcode::G_INTRINSIC_LLRINT:
    RTLIBCASE(LLRINT_F);
  }
  llvm_unreachable("Unknown libcall function");
}

/// True if an instruction is in tail position in its caller. Intended for
/// legalizing libcalls as tail calls when possible.
static bool isLibCallInTailPosition(const CallLowering::ArgInfo &Result,
                                    MachineInstr &MI,
                                    const TargetInstrInfo &TII,
                                    MachineRegisterInfo &MRI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const Function &F = MBB.getParent()->getFunction();

  // Conservatively require the attributes of the call to match those of
  // the return. Ignore NoAlias and NonNull because they don't affect the
  // call sequence.
  AttributeList CallerAttrs = F.getAttributes();
  if (AttrBuilder(F.getContext(), CallerAttrs.getRetAttrs())
          .removeAttribute(Attribute::NoAlias)
          .removeAttribute(Attribute::NonNull)
          .hasAttributes())
    return false;

  // It's not safe to eliminate the sign / zero extension of the return value.
  if (CallerAttrs.hasRetAttr(Attribute::ZExt) ||
      CallerAttrs.hasRetAttr(Attribute::SExt))
    return false;

  // Only tail call if the following instruction is a standard return or if we
  // have a `thisreturn` callee, and a sequence like:
  //
  //   G_MEMCPY %0, %1, %2
  //   $x0 = COPY %0
  //   RET_ReallyLR implicit $x0
  auto Next = next_nodbg(MI.getIterator(), MBB.instr_end());
  if (Next != MBB.instr_end() && Next->isCopy()) {
    if (MI.getOpcode() == TargetOpcode::G_BZERO)
      return false;

    // For MEMCPY/MOMMOVE/MEMSET these will be the first use (the dst), as the
    // mempy/etc routines return the same parameter. For other it will be the
    // returned value.
    Register VReg = MI.getOperand(0).getReg();
    if (!VReg.isVirtual() || VReg != Next->getOperand(1).getReg())
      return false;

    Register PReg = Next->getOperand(0).getReg();
    if (!PReg.isPhysical())
      return false;

    auto Ret = next_nodbg(Next, MBB.instr_end());
    if (Ret == MBB.instr_end() || !Ret->isReturn())
      return false;

    if (Ret->getNumImplicitOperands() != 1)
      return false;

    if (!Ret->getOperand(0).isReg() || PReg != Ret->getOperand(0).getReg())
      return false;

    // Skip over the COPY that we just validated.
    Next = Ret;
  }

  if (Next == MBB.instr_end() || TII.isTailCall(*Next) || !Next->isReturn())
    return false;

  return true;
}

LegalizerHelper::LegalizeResult
llvm::createLibcall(MachineIRBuilder &MIRBuilder, const char *Name,
                    const CallLowering::ArgInfo &Result,
                    ArrayRef<CallLowering::ArgInfo> Args,
                    const CallingConv::ID CC, LostDebugLocObserver &LocObserver,
                    MachineInstr *MI) {
  auto &CLI = *MIRBuilder.getMF().getSubtarget().getCallLowering();

  CallLowering::CallLoweringInfo Info;
  Info.CallConv = CC;
  Info.Callee = MachineOperand::CreateES(Name);
  Info.OrigRet = Result;
  if (MI)
    Info.IsTailCall =
        (Result.Ty->isVoidTy() ||
         Result.Ty == MIRBuilder.getMF().getFunction().getReturnType()) &&
        isLibCallInTailPosition(Result, *MI, MIRBuilder.getTII(),
                                *MIRBuilder.getMRI());

  std::copy(Args.begin(), Args.end(), std::back_inserter(Info.OrigArgs));
  if (!CLI.lowerCall(MIRBuilder, Info))
    return LegalizerHelper::UnableToLegalize;

  if (MI && Info.LoweredTailCall) {
    assert(Info.IsTailCall && "Lowered tail call when it wasn't a tail call?");

    // Check debug locations before removing the return.
    LocObserver.checkpoint(true);

    // We must have a return following the call (or debug insts) to get past
    // isLibCallInTailPosition.
    do {
      MachineInstr *Next = MI->getNextNode();
      assert(Next &&
             (Next->isCopy() || Next->isReturn() || Next->isDebugInstr()) &&
             "Expected instr following MI to be return or debug inst?");
      // We lowered a tail call, so the call is now the return from the block.
      // Delete the old return.
      Next->eraseFromParent();
    } while (MI->getNextNode());

    // We expect to lose the debug location from the return.
    LocObserver.checkpoint(false);
  }
  return LegalizerHelper::Legalized;
}

LegalizerHelper::LegalizeResult
llvm::createLibcall(MachineIRBuilder &MIRBuilder, RTLIB::Libcall Libcall,
                    const CallLowering::ArgInfo &Result,
                    ArrayRef<CallLowering::ArgInfo> Args,
                    LostDebugLocObserver &LocObserver, MachineInstr *MI) {
  auto &TLI = *MIRBuilder.getMF().getSubtarget().getTargetLowering();
  const char *Name = TLI.getLibcallName(Libcall);
  if (!Name)
    return LegalizerHelper::UnableToLegalize;
  const CallingConv::ID CC = TLI.getLibcallCallingConv(Libcall);
  return createLibcall(MIRBuilder, Name, Result, Args, CC, LocObserver, MI);
}

// Useful for libcalls where all operands have the same type.
static LegalizerHelper::LegalizeResult
simpleLibcall(MachineInstr &MI, MachineIRBuilder &MIRBuilder, unsigned Size,
              Type *OpType, LostDebugLocObserver &LocObserver) {
  auto Libcall = getRTLibDesc(MI.getOpcode(), Size);

  // FIXME: What does the original arg index mean here?
  SmallVector<CallLowering::ArgInfo, 3> Args;
  for (const MachineOperand &MO : llvm::drop_begin(MI.operands()))
    Args.push_back({MO.getReg(), OpType, 0});
  return createLibcall(MIRBuilder, Libcall,
                       {MI.getOperand(0).getReg(), OpType, 0}, Args,
                       LocObserver, &MI);
}

LegalizerHelper::LegalizeResult
llvm::createMemLibcall(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                       MachineInstr &MI, LostDebugLocObserver &LocObserver) {
  auto &Ctx = MIRBuilder.getMF().getFunction().getContext();

  SmallVector<CallLowering::ArgInfo, 3> Args;
  // Add all the args, except for the last which is an imm denoting 'tail'.
  for (unsigned i = 0; i < MI.getNumOperands() - 1; ++i) {
    Register Reg = MI.getOperand(i).getReg();

    // Need derive an IR type for call lowering.
    LLT OpLLT = MRI.getType(Reg);
    Type *OpTy = nullptr;
    if (OpLLT.isPointer())
      OpTy = PointerType::get(Ctx, OpLLT.getAddressSpace());
    else
      OpTy = IntegerType::get(Ctx, OpLLT.getSizeInBits());
    Args.push_back({Reg, OpTy, 0});
  }

  auto &CLI = *MIRBuilder.getMF().getSubtarget().getCallLowering();
  auto &TLI = *MIRBuilder.getMF().getSubtarget().getTargetLowering();
  RTLIB::Libcall RTLibcall;
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  case TargetOpcode::G_BZERO:
    RTLibcall = RTLIB::BZERO;
    break;
  case TargetOpcode::G_MEMCPY:
    RTLibcall = RTLIB::MEMCPY;
    Args[0].Flags[0].setReturned();
    break;
  case TargetOpcode::G_MEMMOVE:
    RTLibcall = RTLIB::MEMMOVE;
    Args[0].Flags[0].setReturned();
    break;
  case TargetOpcode::G_MEMSET:
    RTLibcall = RTLIB::MEMSET;
    Args[0].Flags[0].setReturned();
    break;
  default:
    llvm_unreachable("unsupported opcode");
  }
  const char *Name = TLI.getLibcallName(RTLibcall);

  // Unsupported libcall on the target.
  if (!Name) {
    LLVM_DEBUG(dbgs() << ".. .. Could not find libcall name for "
                      << MIRBuilder.getTII().getName(Opc) << "\n");
    return LegalizerHelper::UnableToLegalize;
  }

  CallLowering::CallLoweringInfo Info;
  Info.CallConv = TLI.getLibcallCallingConv(RTLibcall);
  Info.Callee = MachineOperand::CreateES(Name);
  Info.OrigRet = CallLowering::ArgInfo({0}, Type::getVoidTy(Ctx), 0);
  Info.IsTailCall =
      MI.getOperand(MI.getNumOperands() - 1).getImm() &&
      isLibCallInTailPosition(Info.OrigRet, MI, MIRBuilder.getTII(), MRI);

  std::copy(Args.begin(), Args.end(), std::back_inserter(Info.OrigArgs));
  if (!CLI.lowerCall(MIRBuilder, Info))
    return LegalizerHelper::UnableToLegalize;

  if (Info.LoweredTailCall) {
    assert(Info.IsTailCall && "Lowered tail call when it wasn't a tail call?");

    // Check debug locations before removing the return.
    LocObserver.checkpoint(true);

    // We must have a return following the call (or debug insts) to get past
    // isLibCallInTailPosition.
    do {
      MachineInstr *Next = MI.getNextNode();
      assert(Next &&
             (Next->isCopy() || Next->isReturn() || Next->isDebugInstr()) &&
             "Expected instr following MI to be return or debug inst?");
      // We lowered a tail call, so the call is now the return from the block.
      // Delete the old return.
      Next->eraseFromParent();
    } while (MI.getNextNode());

    // We expect to lose the debug location from the return.
    LocObserver.checkpoint(false);
  }

  return LegalizerHelper::Legalized;
}

static RTLIB::Libcall getOutlineAtomicLibcall(MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  auto &AtomicMI = cast<GMemOperation>(MI);
  auto &MMO = AtomicMI.getMMO();
  auto Ordering = MMO.getMergedOrdering();
  LLT MemType = MMO.getMemoryType();
  uint64_t MemSize = MemType.getSizeInBytes();
  if (MemType.isVector())
    return RTLIB::UNKNOWN_LIBCALL;

#define LCALLS(A, B)                                                           \
  { A##B##_RELAX, A##B##_ACQ, A##B##_REL, A##B##_ACQ_REL }
#define LCALL5(A)                                                              \
  LCALLS(A, 1), LCALLS(A, 2), LCALLS(A, 4), LCALLS(A, 8), LCALLS(A, 16)
  switch (Opc) {
  case TargetOpcode::G_ATOMIC_CMPXCHG:
  case TargetOpcode::G_ATOMIC_CMPXCHG_WITH_SUCCESS: {
    const RTLIB::Libcall LC[5][4] = {LCALL5(RTLIB::OUTLINE_ATOMIC_CAS)};
    return getOutlineAtomicHelper(LC, Ordering, MemSize);
  }
  case TargetOpcode::G_ATOMICRMW_XCHG: {
    const RTLIB::Libcall LC[5][4] = {LCALL5(RTLIB::OUTLINE_ATOMIC_SWP)};
    return getOutlineAtomicHelper(LC, Ordering, MemSize);
  }
  case TargetOpcode::G_ATOMICRMW_ADD:
  case TargetOpcode::G_ATOMICRMW_SUB: {
    const RTLIB::Libcall LC[5][4] = {LCALL5(RTLIB::OUTLINE_ATOMIC_LDADD)};
    return getOutlineAtomicHelper(LC, Ordering, MemSize);
  }
  case TargetOpcode::G_ATOMICRMW_AND: {
    const RTLIB::Libcall LC[5][4] = {LCALL5(RTLIB::OUTLINE_ATOMIC_LDCLR)};
    return getOutlineAtomicHelper(LC, Ordering, MemSize);
  }
  case TargetOpcode::G_ATOMICRMW_OR: {
    const RTLIB::Libcall LC[5][4] = {LCALL5(RTLIB::OUTLINE_ATOMIC_LDSET)};
    return getOutlineAtomicHelper(LC, Ordering, MemSize);
  }
  case TargetOpcode::G_ATOMICRMW_XOR: {
    const RTLIB::Libcall LC[5][4] = {LCALL5(RTLIB::OUTLINE_ATOMIC_LDEOR)};
    return getOutlineAtomicHelper(LC, Ordering, MemSize);
  }
  default:
    return RTLIB::UNKNOWN_LIBCALL;
  }
#undef LCALLS
#undef LCALL5
}

static LegalizerHelper::LegalizeResult
createAtomicLibcall(MachineIRBuilder &MIRBuilder, MachineInstr &MI) {
  auto &Ctx = MIRBuilder.getMF().getFunction().getContext();

  Type *RetTy;
  SmallVector<Register> RetRegs;
  SmallVector<CallLowering::ArgInfo, 3> Args;
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  case TargetOpcode::G_ATOMIC_CMPXCHG:
  case TargetOpcode::G_ATOMIC_CMPXCHG_WITH_SUCCESS: {
    Register Success;
    LLT SuccessLLT;
    auto [Ret, RetLLT, Mem, MemLLT, Cmp, CmpLLT, New, NewLLT] =
        MI.getFirst4RegLLTs();
    RetRegs.push_back(Ret);
    RetTy = IntegerType::get(Ctx, RetLLT.getSizeInBits());
    if (Opc == TargetOpcode::G_ATOMIC_CMPXCHG_WITH_SUCCESS) {
      std::tie(Ret, RetLLT, Success, SuccessLLT, Mem, MemLLT, Cmp, CmpLLT, New,
               NewLLT) = MI.getFirst5RegLLTs();
      RetRegs.push_back(Success);
      RetTy = StructType::get(
          Ctx, {RetTy, IntegerType::get(Ctx, SuccessLLT.getSizeInBits())});
    }
    Args.push_back({Cmp, IntegerType::get(Ctx, CmpLLT.getSizeInBits()), 0});
    Args.push_back({New, IntegerType::get(Ctx, NewLLT.getSizeInBits()), 0});
    Args.push_back({Mem, PointerType::get(Ctx, MemLLT.getAddressSpace()), 0});
    break;
  }
  case TargetOpcode::G_ATOMICRMW_XCHG:
  case TargetOpcode::G_ATOMICRMW_ADD:
  case TargetOpcode::G_ATOMICRMW_SUB:
  case TargetOpcode::G_ATOMICRMW_AND:
  case TargetOpcode::G_ATOMICRMW_OR:
  case TargetOpcode::G_ATOMICRMW_XOR: {
    auto [Ret, RetLLT, Mem, MemLLT, Val, ValLLT] = MI.getFirst3RegLLTs();
    RetRegs.push_back(Ret);
    RetTy = IntegerType::get(Ctx, RetLLT.getSizeInBits());
    if (Opc == TargetOpcode::G_ATOMICRMW_AND)
      Val =
          MIRBuilder.buildXor(ValLLT, MIRBuilder.buildConstant(ValLLT, -1), Val)
              .getReg(0);
    else if (Opc == TargetOpcode::G_ATOMICRMW_SUB)
      Val =
          MIRBuilder.buildSub(ValLLT, MIRBuilder.buildConstant(ValLLT, 0), Val)
              .getReg(0);
    Args.push_back({Val, IntegerType::get(Ctx, ValLLT.getSizeInBits()), 0});
    Args.push_back({Mem, PointerType::get(Ctx, MemLLT.getAddressSpace()), 0});
    break;
  }
  default:
    llvm_unreachable("unsupported opcode");
  }

  auto &CLI = *MIRBuilder.getMF().getSubtarget().getCallLowering();
  auto &TLI = *MIRBuilder.getMF().getSubtarget().getTargetLowering();
  RTLIB::Libcall RTLibcall = getOutlineAtomicLibcall(MI);
  const char *Name = TLI.getLibcallName(RTLibcall);

  // Unsupported libcall on the target.
  if (!Name) {
    LLVM_DEBUG(dbgs() << ".. .. Could not find libcall name for "
                      << MIRBuilder.getTII().getName(Opc) << "\n");
    return LegalizerHelper::UnableToLegalize;
  }

  CallLowering::CallLoweringInfo Info;
  Info.CallConv = TLI.getLibcallCallingConv(RTLibcall);
  Info.Callee = MachineOperand::CreateES(Name);
  Info.OrigRet = CallLowering::ArgInfo(RetRegs, RetTy, 0);

  std::copy(Args.begin(), Args.end(), std::back_inserter(Info.OrigArgs));
  if (!CLI.lowerCall(MIRBuilder, Info))
    return LegalizerHelper::UnableToLegalize;

  return LegalizerHelper::Legalized;
}

static RTLIB::Libcall getConvRTLibDesc(unsigned Opcode, Type *ToType,
                                       Type *FromType) {
  auto ToMVT = MVT::getVT(ToType);
  auto FromMVT = MVT::getVT(FromType);

  switch (Opcode) {
  case TargetOpcode::G_FPEXT:
    return RTLIB::getFPEXT(FromMVT, ToMVT);
  case TargetOpcode::G_FPTRUNC:
    return RTLIB::getFPROUND(FromMVT, ToMVT);
  case TargetOpcode::G_FPTOSI:
    return RTLIB::getFPTOSINT(FromMVT, ToMVT);
  case TargetOpcode::G_FPTOUI:
    return RTLIB::getFPTOUINT(FromMVT, ToMVT);
  case TargetOpcode::G_SITOFP:
    return RTLIB::getSINTTOFP(FromMVT, ToMVT);
  case TargetOpcode::G_UITOFP:
    return RTLIB::getUINTTOFP(FromMVT, ToMVT);
  }
  llvm_unreachable("Unsupported libcall function");
}

static LegalizerHelper::LegalizeResult
conversionLibcall(MachineInstr &MI, MachineIRBuilder &MIRBuilder, Type *ToType,
                  Type *FromType, LostDebugLocObserver &LocObserver) {
  RTLIB::Libcall Libcall = getConvRTLibDesc(MI.getOpcode(), ToType, FromType);
  return createLibcall(
      MIRBuilder, Libcall, {MI.getOperand(0).getReg(), ToType, 0},
      {{MI.getOperand(1).getReg(), FromType, 0}}, LocObserver, &MI);
}

static RTLIB::Libcall
getStateLibraryFunctionFor(MachineInstr &MI, const TargetLowering &TLI) {
  RTLIB::Libcall RTLibcall;
  switch (MI.getOpcode()) {
  case TargetOpcode::G_GET_FPENV:
    RTLibcall = RTLIB::FEGETENV;
    break;
  case TargetOpcode::G_SET_FPENV:
  case TargetOpcode::G_RESET_FPENV:
    RTLibcall = RTLIB::FESETENV;
    break;
  case TargetOpcode::G_GET_FPMODE:
    RTLibcall = RTLIB::FEGETMODE;
    break;
  case TargetOpcode::G_SET_FPMODE:
  case TargetOpcode::G_RESET_FPMODE:
    RTLibcall = RTLIB::FESETMODE;
    break;
  default:
    llvm_unreachable("Unexpected opcode");
  }
  return RTLibcall;
}

// Some library functions that read FP state (fegetmode, fegetenv) write the
// state into a region in memory. IR intrinsics that do the same operations
// (get_fpmode, get_fpenv) return the state as integer value. To implement these
// intrinsics via the library functions, we need to use temporary variable,
// for example:
//
//     %0:_(s32) = G_GET_FPMODE
//
// is transformed to:
//
//     %1:_(p0) = G_FRAME_INDEX %stack.0
//     BL &fegetmode
//     %0:_(s32) = G_LOAD % 1
//
LegalizerHelper::LegalizeResult
LegalizerHelper::createGetStateLibcall(MachineIRBuilder &MIRBuilder,
                                       MachineInstr &MI,
                                       LostDebugLocObserver &LocObserver) {
  const DataLayout &DL = MIRBuilder.getDataLayout();
  auto &MF = MIRBuilder.getMF();
  auto &MRI = *MIRBuilder.getMRI();
  auto &Ctx = MF.getFunction().getContext();

  // Create temporary, where library function will put the read state.
  Register Dst = MI.getOperand(0).getReg();
  LLT StateTy = MRI.getType(Dst);
  TypeSize StateSize = StateTy.getSizeInBytes();
  Align TempAlign = getStackTemporaryAlignment(StateTy);
  MachinePointerInfo TempPtrInfo;
  auto Temp = createStackTemporary(StateSize, TempAlign, TempPtrInfo);

  // Create a call to library function, with the temporary as an argument.
  unsigned TempAddrSpace = DL.getAllocaAddrSpace();
  Type *StatePtrTy = PointerType::get(Ctx, TempAddrSpace);
  RTLIB::Libcall RTLibcall = getStateLibraryFunctionFor(MI, TLI);
  auto Res =
      createLibcall(MIRBuilder, RTLibcall,
                    CallLowering::ArgInfo({0}, Type::getVoidTy(Ctx), 0),
                    CallLowering::ArgInfo({Temp.getReg(0), StatePtrTy, 0}),
                    LocObserver, nullptr);
  if (Res != LegalizerHelper::Legalized)
    return Res;

  // Create a load from the temporary.
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      TempPtrInfo, MachineMemOperand::MOLoad, StateTy, TempAlign);
  MIRBuilder.buildLoadInstr(TargetOpcode::G_LOAD, Dst, Temp, *MMO);

  return LegalizerHelper::Legalized;
}

// Similar to `createGetStateLibcall` the function calls a library function
// using transient space in stack. In this case the library function reads
// content of memory region.
LegalizerHelper::LegalizeResult
LegalizerHelper::createSetStateLibcall(MachineIRBuilder &MIRBuilder,
                                       MachineInstr &MI,
                                       LostDebugLocObserver &LocObserver) {
  const DataLayout &DL = MIRBuilder.getDataLayout();
  auto &MF = MIRBuilder.getMF();
  auto &MRI = *MIRBuilder.getMRI();
  auto &Ctx = MF.getFunction().getContext();

  // Create temporary, where library function will get the new state.
  Register Src = MI.getOperand(0).getReg();
  LLT StateTy = MRI.getType(Src);
  TypeSize StateSize = StateTy.getSizeInBytes();
  Align TempAlign = getStackTemporaryAlignment(StateTy);
  MachinePointerInfo TempPtrInfo;
  auto Temp = createStackTemporary(StateSize, TempAlign, TempPtrInfo);

  // Put the new state into the temporary.
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      TempPtrInfo, MachineMemOperand::MOStore, StateTy, TempAlign);
  MIRBuilder.buildStore(Src, Temp, *MMO);

  // Create a call to library function, with the temporary as an argument.
  unsigned TempAddrSpace = DL.getAllocaAddrSpace();
  Type *StatePtrTy = PointerType::get(Ctx, TempAddrSpace);
  RTLIB::Libcall RTLibcall = getStateLibraryFunctionFor(MI, TLI);
  return createLibcall(MIRBuilder, RTLibcall,
                       CallLowering::ArgInfo({0}, Type::getVoidTy(Ctx), 0),
                       CallLowering::ArgInfo({Temp.getReg(0), StatePtrTy, 0}),
                       LocObserver, nullptr);
}

// The function is used to legalize operations that set default environment
// state. In C library a call like `fesetmode(FE_DFL_MODE)` is used for that.
// On most targets supported in glibc FE_DFL_MODE is defined as
// `((const femode_t *) -1)`. Such assumption is used here. If for some target
// it is not true, the target must provide custom lowering.
LegalizerHelper::LegalizeResult
LegalizerHelper::createResetStateLibcall(MachineIRBuilder &MIRBuilder,
                                         MachineInstr &MI,
                                         LostDebugLocObserver &LocObserver) {
  const DataLayout &DL = MIRBuilder.getDataLayout();
  auto &MF = MIRBuilder.getMF();
  auto &Ctx = MF.getFunction().getContext();

  // Create an argument for the library function.
  unsigned AddrSpace = DL.getDefaultGlobalsAddressSpace();
  Type *StatePtrTy = PointerType::get(Ctx, AddrSpace);
  unsigned PtrSize = DL.getPointerSizeInBits(AddrSpace);
  LLT MemTy = LLT::pointer(AddrSpace, PtrSize);
  auto DefValue = MIRBuilder.buildConstant(LLT::scalar(PtrSize), -1LL);
  DstOp Dest(MRI.createGenericVirtualRegister(MemTy));
  MIRBuilder.buildIntToPtr(Dest, DefValue);

  RTLIB::Libcall RTLibcall = getStateLibraryFunctionFor(MI, TLI);
  return createLibcall(MIRBuilder, RTLibcall,
                       CallLowering::ArgInfo({0}, Type::getVoidTy(Ctx), 0),
                       CallLowering::ArgInfo({Dest.getReg(), StatePtrTy, 0}),
                       LocObserver, &MI);
}

LegalizerHelper::LegalizeResult
LegalizerHelper::libcall(MachineInstr &MI, LostDebugLocObserver &LocObserver) {
  auto &Ctx = MIRBuilder.getMF().getFunction().getContext();

  switch (MI.getOpcode()) {
  default:
    return UnableToLegalize;
  case TargetOpcode::G_MUL:
  case TargetOpcode::G_SDIV:
  case TargetOpcode::G_UDIV:
  case TargetOpcode::G_SREM:
  case TargetOpcode::G_UREM:
  case TargetOpcode::G_CTLZ_ZERO_UNDEF: {
    LLT LLTy = MRI.getType(MI.getOperand(0).getReg());
    unsigned Size = LLTy.getSizeInBits();
    Type *HLTy = IntegerType::get(Ctx, Size);
    auto Status = simpleLibcall(MI, MIRBuilder, Size, HLTy, LocObserver);
    if (Status != Legalized)
      return Status;
    break;
  }
  case TargetOpcode::G_FADD:
  case TargetOpcode::G_FSUB:
  case TargetOpcode::G_FMUL:
  case TargetOpcode::G_FDIV:
  case TargetOpcode::G_FMA:
  case TargetOpcode::G_FPOW:
  case TargetOpcode::G_FREM:
  case TargetOpcode::G_FCOS:
  case TargetOpcode::G_FSIN:
  case TargetOpcode::G_FTAN:
  case TargetOpcode::G_FACOS:
  case TargetOpcode::G_FASIN:
  case TargetOpcode::G_FATAN:
  case TargetOpcode::G_FCOSH:
  case TargetOpcode::G_FSINH:
  case TargetOpcode::G_FTANH:
  case TargetOpcode::G_FLOG10:
  case TargetOpcode::G_FLOG:
  case TargetOpcode::G_FLOG2:
  case TargetOpcode::G_FLDEXP:
  case TargetOpcode::G_FEXP:
  case TargetOpcode::G_FEXP2:
  case TargetOpcode::G_FEXP10:
  case TargetOpcode::G_FCEIL:
  case TargetOpcode::G_FFLOOR:
  case TargetOpcode::G_FMINNUM:
  case TargetOpcode::G_FMAXNUM:
  case TargetOpcode::G_FSQRT:
  case TargetOpcode::G_FRINT:
  case TargetOpcode::G_FNEARBYINT:
  case TargetOpcode::G_INTRINSIC_ROUNDEVEN: {
    LLT LLTy = MRI.getType(MI.getOperand(0).getReg());
    unsigned Size = LLTy.getSizeInBits();
    Type *HLTy = getFloatTypeForLLT(Ctx, LLTy);
    if (!HLTy || (Size != 32 && Size != 64 && Size != 80 && Size != 128)) {
      LLVM_DEBUG(dbgs() << "No libcall available for type " << LLTy << ".\n");
      return UnableToLegalize;
    }
    auto Status = simpleLibcall(MI, MIRBuilder, Size, HLTy, LocObserver);
    if (Status != Legalized)
      return Status;
    break;
  }
  case TargetOpcode::G_INTRINSIC_LRINT:
  case TargetOpcode::G_INTRINSIC_LLRINT: {
    LLT LLTy = MRI.getType(MI.getOperand(1).getReg());
    unsigned Size = LLTy.getSizeInBits();
    Type *HLTy = getFloatTypeForLLT(Ctx, LLTy);
    Type *ITy = IntegerType::get(
        Ctx, MRI.getType(MI.getOperand(0).getReg()).getSizeInBits());
    if (!HLTy || (Size != 32 && Size != 64 && Size != 80 && Size != 128)) {
      LLVM_DEBUG(dbgs() << "No libcall available for type " << LLTy << ".\n");
      return UnableToLegalize;
    }
    auto Libcall = getRTLibDesc(MI.getOpcode(), Size);
    LegalizeResult Status =
        createLibcall(MIRBuilder, Libcall, {MI.getOperand(0).getReg(), ITy, 0},
                      {{MI.getOperand(1).getReg(), HLTy, 0}}, LocObserver, &MI);
    if (Status != Legalized)
      return Status;
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_FPOWI: {
    LLT LLTy = MRI.getType(MI.getOperand(0).getReg());
    unsigned Size = LLTy.getSizeInBits();
    Type *HLTy = getFloatTypeForLLT(Ctx, LLTy);
    Type *ITy = IntegerType::get(
        Ctx, MRI.getType(MI.getOperand(2).getReg()).getSizeInBits());
    if (!HLTy || (Size != 32 && Size != 64 && Size != 80 && Size != 128)) {
      LLVM_DEBUG(dbgs() << "No libcall available for type " << LLTy << ".\n");
      return UnableToLegalize;
    }
    auto Libcall = getRTLibDesc(MI.getOpcode(), Size);
    std::initializer_list<CallLowering::ArgInfo> Args = {
        {MI.getOperand(1).getReg(), HLTy, 0},
        {MI.getOperand(2).getReg(), ITy, 1}};
    LegalizeResult Status =
        createLibcall(MIRBuilder, Libcall, {MI.getOperand(0).getReg(), HLTy, 0},
                      Args, LocObserver, &MI);
    if (Status != Legalized)
      return Status;
    break;
  }
  case TargetOpcode::G_FPEXT:
  case TargetOpcode::G_FPTRUNC: {
    Type *FromTy = getFloatTypeForLLT(Ctx,  MRI.getType(MI.getOperand(1).getReg()));
    Type *ToTy = getFloatTypeForLLT(Ctx, MRI.getType(MI.getOperand(0).getReg()));
    if (!FromTy || !ToTy)
      return UnableToLegalize;
    LegalizeResult Status =
        conversionLibcall(MI, MIRBuilder, ToTy, FromTy, LocObserver);
    if (Status != Legalized)
      return Status;
    break;
  }
  case TargetOpcode::G_FPTOSI:
  case TargetOpcode::G_FPTOUI: {
    // FIXME: Support other types
    Type *FromTy =
        getFloatTypeForLLT(Ctx, MRI.getType(MI.getOperand(1).getReg()));
    unsigned ToSize = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
    if ((ToSize != 32 && ToSize != 64 && ToSize != 128) || !FromTy)
      return UnableToLegalize;
    LegalizeResult Status = conversionLibcall(
        MI, MIRBuilder, Type::getIntNTy(Ctx, ToSize), FromTy, LocObserver);
    if (Status != Legalized)
      return Status;
    break;
  }
  case TargetOpcode::G_SITOFP:
  case TargetOpcode::G_UITOFP: {
    unsigned FromSize = MRI.getType(MI.getOperand(1).getReg()).getSizeInBits();
    Type *ToTy =
        getFloatTypeForLLT(Ctx, MRI.getType(MI.getOperand(0).getReg()));
    if ((FromSize != 32 && FromSize != 64 && FromSize != 128) || !ToTy)
      return UnableToLegalize;
    LegalizeResult Status = conversionLibcall(
        MI, MIRBuilder, ToTy, Type::getIntNTy(Ctx, FromSize), LocObserver);
    if (Status != Legalized)
      return Status;
    break;
  }
  case TargetOpcode::G_ATOMICRMW_XCHG:
  case TargetOpcode::G_ATOMICRMW_ADD:
  case TargetOpcode::G_ATOMICRMW_SUB:
  case TargetOpcode::G_ATOMICRMW_AND:
  case TargetOpcode::G_ATOMICRMW_OR:
  case TargetOpcode::G_ATOMICRMW_XOR:
  case TargetOpcode::G_ATOMIC_CMPXCHG:
  case TargetOpcode::G_ATOMIC_CMPXCHG_WITH_SUCCESS: {
    auto Status = createAtomicLibcall(MIRBuilder, MI);
    if (Status != Legalized)
      return Status;
    break;
  }
  case TargetOpcode::G_BZERO:
  case TargetOpcode::G_MEMCPY:
  case TargetOpcode::G_MEMMOVE:
  case TargetOpcode::G_MEMSET: {
    LegalizeResult Result =
        createMemLibcall(MIRBuilder, *MIRBuilder.getMRI(), MI, LocObserver);
    if (Result != Legalized)
      return Result;
    MI.eraseFromParent();
    return Result;
  }
  case TargetOpcode::G_GET_FPENV:
  case TargetOpcode::G_GET_FPMODE: {
    LegalizeResult Result = createGetStateLibcall(MIRBuilder, MI, LocObserver);
    if (Result != Legalized)
      return Result;
    break;
  }
  case TargetOpcode::G_SET_FPENV:
  case TargetOpcode::G_SET_FPMODE: {
    LegalizeResult Result = createSetStateLibcall(MIRBuilder, MI, LocObserver);
    if (Result != Legalized)
      return Result;
    break;
  }
  case TargetOpcode::G_RESET_FPENV:
  case TargetOpcode::G_RESET_FPMODE: {
    LegalizeResult Result =
        createResetStateLibcall(MIRBuilder, MI, LocObserver);
    if (Result != Legalized)
      return Result;
    break;
  }
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::narrowScalar(MachineInstr &MI,
                                                              unsigned TypeIdx,
                                                              LLT NarrowTy) {
  uint64_t SizeOp0 = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
  uint64_t NarrowSize = NarrowTy.getSizeInBits();

  switch (MI.getOpcode()) {
  default:
    return UnableToLegalize;
  case TargetOpcode::G_IMPLICIT_DEF: {
    Register DstReg = MI.getOperand(0).getReg();
    LLT DstTy = MRI.getType(DstReg);

    // If SizeOp0 is not an exact multiple of NarrowSize, emit
    // G_ANYEXT(G_IMPLICIT_DEF). Cast result to vector if needed.
    // FIXME: Although this would also be legal for the general case, it causes
    //  a lot of regressions in the emitted code (superfluous COPYs, artifact
    //  combines not being hit). This seems to be a problem related to the
    //  artifact combiner.
    if (SizeOp0 % NarrowSize != 0) {
      LLT ImplicitTy = NarrowTy;
      if (DstTy.isVector())
        ImplicitTy = LLT::vector(DstTy.getElementCount(), ImplicitTy);

      Register ImplicitReg = MIRBuilder.buildUndef(ImplicitTy).getReg(0);
      MIRBuilder.buildAnyExt(DstReg, ImplicitReg);

      MI.eraseFromParent();
      return Legalized;
    }

    int NumParts = SizeOp0 / NarrowSize;

    SmallVector<Register, 2> DstRegs;
    for (int i = 0; i < NumParts; ++i)
      DstRegs.push_back(MIRBuilder.buildUndef(NarrowTy).getReg(0));

    if (DstTy.isVector())
      MIRBuilder.buildBuildVector(DstReg, DstRegs);
    else
      MIRBuilder.buildMergeLikeInstr(DstReg, DstRegs);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_CONSTANT: {
    LLT Ty = MRI.getType(MI.getOperand(0).getReg());
    const APInt &Val = MI.getOperand(1).getCImm()->getValue();
    unsigned TotalSize = Ty.getSizeInBits();
    unsigned NarrowSize = NarrowTy.getSizeInBits();
    int NumParts = TotalSize / NarrowSize;

    SmallVector<Register, 4> PartRegs;
    for (int I = 0; I != NumParts; ++I) {
      unsigned Offset = I * NarrowSize;
      auto K = MIRBuilder.buildConstant(NarrowTy,
                                        Val.lshr(Offset).trunc(NarrowSize));
      PartRegs.push_back(K.getReg(0));
    }

    LLT LeftoverTy;
    unsigned LeftoverBits = TotalSize - NumParts * NarrowSize;
    SmallVector<Register, 1> LeftoverRegs;
    if (LeftoverBits != 0) {
      LeftoverTy = LLT::scalar(LeftoverBits);
      auto K = MIRBuilder.buildConstant(
        LeftoverTy,
        Val.lshr(NumParts * NarrowSize).trunc(LeftoverBits));
      LeftoverRegs.push_back(K.getReg(0));
    }

    insertParts(MI.getOperand(0).getReg(),
                Ty, NarrowTy, PartRegs, LeftoverTy, LeftoverRegs);

    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_SEXT:
  case TargetOpcode::G_ZEXT:
  case TargetOpcode::G_ANYEXT:
    return narrowScalarExt(MI, TypeIdx, NarrowTy);
  case TargetOpcode::G_TRUNC: {
    if (TypeIdx != 1)
      return UnableToLegalize;

    uint64_t SizeOp1 = MRI.getType(MI.getOperand(1).getReg()).getSizeInBits();
    if (NarrowTy.getSizeInBits() * 2 != SizeOp1) {
      LLVM_DEBUG(dbgs() << "Can't narrow trunc to type " << NarrowTy << "\n");
      return UnableToLegalize;
    }

    auto Unmerge = MIRBuilder.buildUnmerge(NarrowTy, MI.getOperand(1));
    MIRBuilder.buildCopy(MI.getOperand(0), Unmerge.getReg(0));
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_CONSTANT_FOLD_BARRIER:
  case TargetOpcode::G_FREEZE: {
    if (TypeIdx != 0)
      return UnableToLegalize;

    LLT Ty = MRI.getType(MI.getOperand(0).getReg());
    // Should widen scalar first
    if (Ty.getSizeInBits() % NarrowTy.getSizeInBits() != 0)
      return UnableToLegalize;

    auto Unmerge = MIRBuilder.buildUnmerge(NarrowTy, MI.getOperand(1).getReg());
    SmallVector<Register, 8> Parts;
    for (unsigned i = 0; i < Unmerge->getNumDefs(); ++i) {
      Parts.push_back(
          MIRBuilder.buildInstr(MI.getOpcode(), {NarrowTy}, {Unmerge.getReg(i)})
              .getReg(0));
    }

    MIRBuilder.buildMergeLikeInstr(MI.getOperand(0).getReg(), Parts);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_ADD:
  case TargetOpcode::G_SUB:
  case TargetOpcode::G_SADDO:
  case TargetOpcode::G_SSUBO:
  case TargetOpcode::G_SADDE:
  case TargetOpcode::G_SSUBE:
  case TargetOpcode::G_UADDO:
  case TargetOpcode::G_USUBO:
  case TargetOpcode::G_UADDE:
  case TargetOpcode::G_USUBE:
    return narrowScalarAddSub(MI, TypeIdx, NarrowTy);
  case TargetOpcode::G_MUL:
  case TargetOpcode::G_UMULH:
    return narrowScalarMul(MI, NarrowTy);
  case TargetOpcode::G_EXTRACT:
    return narrowScalarExtract(MI, TypeIdx, NarrowTy);
  case TargetOpcode::G_INSERT:
    return narrowScalarInsert(MI, TypeIdx, NarrowTy);
  case TargetOpcode::G_LOAD: {
    auto &LoadMI = cast<GLoad>(MI);
    Register DstReg = LoadMI.getDstReg();
    LLT DstTy = MRI.getType(DstReg);
    if (DstTy.isVector())
      return UnableToLegalize;

    if (8 * LoadMI.getMemSize().getValue() != DstTy.getSizeInBits()) {
      Register TmpReg = MRI.createGenericVirtualRegister(NarrowTy);
      MIRBuilder.buildLoad(TmpReg, LoadMI.getPointerReg(), LoadMI.getMMO());
      MIRBuilder.buildAnyExt(DstReg, TmpReg);
      LoadMI.eraseFromParent();
      return Legalized;
    }

    return reduceLoadStoreWidth(LoadMI, TypeIdx, NarrowTy);
  }
  case TargetOpcode::G_ZEXTLOAD:
  case TargetOpcode::G_SEXTLOAD: {
    auto &LoadMI = cast<GExtLoad>(MI);
    Register DstReg = LoadMI.getDstReg();
    Register PtrReg = LoadMI.getPointerReg();

    Register TmpReg = MRI.createGenericVirtualRegister(NarrowTy);
    auto &MMO = LoadMI.getMMO();
    unsigned MemSize = MMO.getSizeInBits().getValue();

    if (MemSize == NarrowSize) {
      MIRBuilder.buildLoad(TmpReg, PtrReg, MMO);
    } else if (MemSize < NarrowSize) {
      MIRBuilder.buildLoadInstr(LoadMI.getOpcode(), TmpReg, PtrReg, MMO);
    } else if (MemSize > NarrowSize) {
      // FIXME: Need to split the load.
      return UnableToLegalize;
    }

    if (isa<GZExtLoad>(LoadMI))
      MIRBuilder.buildZExt(DstReg, TmpReg);
    else
      MIRBuilder.buildSExt(DstReg, TmpReg);

    LoadMI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_STORE: {
    auto &StoreMI = cast<GStore>(MI);

    Register SrcReg = StoreMI.getValueReg();
    LLT SrcTy = MRI.getType(SrcReg);
    if (SrcTy.isVector())
      return UnableToLegalize;

    int NumParts = SizeOp0 / NarrowSize;
    unsigned HandledSize = NumParts * NarrowTy.getSizeInBits();
    unsigned LeftoverBits = SrcTy.getSizeInBits() - HandledSize;
    if (SrcTy.isVector() && LeftoverBits != 0)
      return UnableToLegalize;

    if (8 * StoreMI.getMemSize().getValue() != SrcTy.getSizeInBits()) {
      Register TmpReg = MRI.createGenericVirtualRegister(NarrowTy);
      MIRBuilder.buildTrunc(TmpReg, SrcReg);
      MIRBuilder.buildStore(TmpReg, StoreMI.getPointerReg(), StoreMI.getMMO());
      StoreMI.eraseFromParent();
      return Legalized;
    }

    return reduceLoadStoreWidth(StoreMI, 0, NarrowTy);
  }
  case TargetOpcode::G_SELECT:
    return narrowScalarSelect(MI, TypeIdx, NarrowTy);
  case TargetOpcode::G_AND:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_XOR: {
    // Legalize bitwise operation:
    // A = BinOp<Ty> B, C
    // into:
    // B1, ..., BN = G_UNMERGE_VALUES B
    // C1, ..., CN = G_UNMERGE_VALUES C
    // A1 = BinOp<Ty/N> B1, C2
    // ...
    // AN = BinOp<Ty/N> BN, CN
    // A = G_MERGE_VALUES A1, ..., AN
    return narrowScalarBasic(MI, TypeIdx, NarrowTy);
  }
  case TargetOpcode::G_SHL:
  case TargetOpcode::G_LSHR:
  case TargetOpcode::G_ASHR:
    return narrowScalarShift(MI, TypeIdx, NarrowTy);
  case TargetOpcode::G_CTLZ:
  case TargetOpcode::G_CTLZ_ZERO_UNDEF:
  case TargetOpcode::G_CTTZ:
  case TargetOpcode::G_CTTZ_ZERO_UNDEF:
  case TargetOpcode::G_CTPOP:
    if (TypeIdx == 1)
      switch (MI.getOpcode()) {
      case TargetOpcode::G_CTLZ:
      case TargetOpcode::G_CTLZ_ZERO_UNDEF:
        return narrowScalarCTLZ(MI, TypeIdx, NarrowTy);
      case TargetOpcode::G_CTTZ:
      case TargetOpcode::G_CTTZ_ZERO_UNDEF:
        return narrowScalarCTTZ(MI, TypeIdx, NarrowTy);
      case TargetOpcode::G_CTPOP:
        return narrowScalarCTPOP(MI, TypeIdx, NarrowTy);
      default:
        return UnableToLegalize;
      }

    Observer.changingInstr(MI);
    narrowScalarDst(MI, NarrowTy, 0, TargetOpcode::G_ZEXT);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_INTTOPTR:
    if (TypeIdx != 1)
      return UnableToLegalize;

    Observer.changingInstr(MI);
    narrowScalarSrc(MI, NarrowTy, 1);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_PTRTOINT:
    if (TypeIdx != 0)
      return UnableToLegalize;

    Observer.changingInstr(MI);
    narrowScalarDst(MI, NarrowTy, 0, TargetOpcode::G_ZEXT);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_PHI: {
    // FIXME: add support for when SizeOp0 isn't an exact multiple of
    // NarrowSize.
    if (SizeOp0 % NarrowSize != 0)
      return UnableToLegalize;

    unsigned NumParts = SizeOp0 / NarrowSize;
    SmallVector<Register, 2> DstRegs(NumParts);
    SmallVector<SmallVector<Register, 2>, 2> SrcRegs(MI.getNumOperands() / 2);
    Observer.changingInstr(MI);
    for (unsigned i = 1; i < MI.getNumOperands(); i += 2) {
      MachineBasicBlock &OpMBB = *MI.getOperand(i + 1).getMBB();
      MIRBuilder.setInsertPt(OpMBB, OpMBB.getFirstTerminatorForward());
      extractParts(MI.getOperand(i).getReg(), NarrowTy, NumParts,
                   SrcRegs[i / 2], MIRBuilder, MRI);
    }
    MachineBasicBlock &MBB = *MI.getParent();
    MIRBuilder.setInsertPt(MBB, MI);
    for (unsigned i = 0; i < NumParts; ++i) {
      DstRegs[i] = MRI.createGenericVirtualRegister(NarrowTy);
      MachineInstrBuilder MIB =
          MIRBuilder.buildInstr(TargetOpcode::G_PHI).addDef(DstRegs[i]);
      for (unsigned j = 1; j < MI.getNumOperands(); j += 2)
        MIB.addUse(SrcRegs[j / 2][i]).add(MI.getOperand(j + 1));
    }
    MIRBuilder.setInsertPt(MBB, MBB.getFirstNonPHI());
    MIRBuilder.buildMergeLikeInstr(MI.getOperand(0), DstRegs);
    Observer.changedInstr(MI);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_EXTRACT_VECTOR_ELT:
  case TargetOpcode::G_INSERT_VECTOR_ELT: {
    if (TypeIdx != 2)
      return UnableToLegalize;

    int OpIdx = MI.getOpcode() == TargetOpcode::G_EXTRACT_VECTOR_ELT ? 2 : 3;
    Observer.changingInstr(MI);
    narrowScalarSrc(MI, NarrowTy, OpIdx);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_ICMP: {
    Register LHS = MI.getOperand(2).getReg();
    LLT SrcTy = MRI.getType(LHS);
    uint64_t SrcSize = SrcTy.getSizeInBits();
    CmpInst::Predicate Pred =
        static_cast<CmpInst::Predicate>(MI.getOperand(1).getPredicate());

    // TODO: Handle the non-equality case for weird sizes.
    if (NarrowSize * 2 != SrcSize && !ICmpInst::isEquality(Pred))
      return UnableToLegalize;

    LLT LeftoverTy; // Example: s88 -> s64 (NarrowTy) + s24 (leftover)
    SmallVector<Register, 4> LHSPartRegs, LHSLeftoverRegs;
    if (!extractParts(LHS, SrcTy, NarrowTy, LeftoverTy, LHSPartRegs,
                      LHSLeftoverRegs, MIRBuilder, MRI))
      return UnableToLegalize;

    LLT Unused; // Matches LeftoverTy; G_ICMP LHS and RHS are the same type.
    SmallVector<Register, 4> RHSPartRegs, RHSLeftoverRegs;
    if (!extractParts(MI.getOperand(3).getReg(), SrcTy, NarrowTy, Unused,
                      RHSPartRegs, RHSLeftoverRegs, MIRBuilder, MRI))
      return UnableToLegalize;

    // We now have the LHS and RHS of the compare split into narrow-type
    // registers, plus potentially some leftover type.
    Register Dst = MI.getOperand(0).getReg();
    LLT ResTy = MRI.getType(Dst);
    if (ICmpInst::isEquality(Pred)) {
      // For each part on the LHS and RHS, keep track of the result of XOR-ing
      // them together. For each equal part, the result should be all 0s. For
      // each non-equal part, we'll get at least one 1.
      auto Zero = MIRBuilder.buildConstant(NarrowTy, 0);
      SmallVector<Register, 4> Xors;
      for (auto LHSAndRHS : zip(LHSPartRegs, RHSPartRegs)) {
        auto LHS = std::get<0>(LHSAndRHS);
        auto RHS = std::get<1>(LHSAndRHS);
        auto Xor = MIRBuilder.buildXor(NarrowTy, LHS, RHS).getReg(0);
        Xors.push_back(Xor);
      }

      // Build a G_XOR for each leftover register. Each G_XOR must be widened
      // to the desired narrow type so that we can OR them together later.
      SmallVector<Register, 4> WidenedXors;
      for (auto LHSAndRHS : zip(LHSLeftoverRegs, RHSLeftoverRegs)) {
        auto LHS = std::get<0>(LHSAndRHS);
        auto RHS = std::get<1>(LHSAndRHS);
        auto Xor = MIRBuilder.buildXor(LeftoverTy, LHS, RHS).getReg(0);
        LLT GCDTy = extractGCDType(WidenedXors, NarrowTy, LeftoverTy, Xor);
        buildLCMMergePieces(LeftoverTy, NarrowTy, GCDTy, WidenedXors,
                            /* PadStrategy = */ TargetOpcode::G_ZEXT);
        Xors.insert(Xors.end(), WidenedXors.begin(), WidenedXors.end());
      }

      // Now, for each part we broke up, we know if they are equal/not equal
      // based off the G_XOR. We can OR these all together and compare against
      // 0 to get the result.
      assert(Xors.size() >= 2 && "Should have gotten at least two Xors?");
      auto Or = MIRBuilder.buildOr(NarrowTy, Xors[0], Xors[1]);
      for (unsigned I = 2, E = Xors.size(); I < E; ++I)
        Or = MIRBuilder.buildOr(NarrowTy, Or, Xors[I]);
      MIRBuilder.buildICmp(Pred, Dst, Or, Zero);
    } else {
      // TODO: Handle non-power-of-two types.
      assert(LHSPartRegs.size() == 2 && "Expected exactly 2 LHS part regs?");
      assert(RHSPartRegs.size() == 2 && "Expected exactly 2 RHS part regs?");
      Register LHSL = LHSPartRegs[0];
      Register LHSH = LHSPartRegs[1];
      Register RHSL = RHSPartRegs[0];
      Register RHSH = RHSPartRegs[1];
      MachineInstrBuilder CmpH = MIRBuilder.buildICmp(Pred, ResTy, LHSH, RHSH);
      MachineInstrBuilder CmpHEQ =
          MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_EQ, ResTy, LHSH, RHSH);
      MachineInstrBuilder CmpLU = MIRBuilder.buildICmp(
          ICmpInst::getUnsignedPredicate(Pred), ResTy, LHSL, RHSL);
      MIRBuilder.buildSelect(Dst, CmpHEQ, CmpLU, CmpH);
    }
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_FCMP:
    if (TypeIdx != 0)
      return UnableToLegalize;

    Observer.changingInstr(MI);
    narrowScalarDst(MI, NarrowTy, 0, TargetOpcode::G_ZEXT);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_SEXT_INREG: {
    if (TypeIdx != 0)
      return UnableToLegalize;

    int64_t SizeInBits = MI.getOperand(2).getImm();

    // So long as the new type has more bits than the bits we're extending we
    // don't need to break it apart.
    if (NarrowTy.getScalarSizeInBits() > SizeInBits) {
      Observer.changingInstr(MI);
      // We don't lose any non-extension bits by truncating the src and
      // sign-extending the dst.
      MachineOperand &MO1 = MI.getOperand(1);
      auto TruncMIB = MIRBuilder.buildTrunc(NarrowTy, MO1);
      MO1.setReg(TruncMIB.getReg(0));

      MachineOperand &MO2 = MI.getOperand(0);
      Register DstExt = MRI.createGenericVirtualRegister(NarrowTy);
      MIRBuilder.setInsertPt(MIRBuilder.getMBB(), ++MIRBuilder.getInsertPt());
      MIRBuilder.buildSExt(MO2, DstExt);
      MO2.setReg(DstExt);
      Observer.changedInstr(MI);
      return Legalized;
    }

    // Break it apart. Components below the extension point are unmodified. The
    // component containing the extension point becomes a narrower SEXT_INREG.
    // Components above it are ashr'd from the component containing the
    // extension point.
    if (SizeOp0 % NarrowSize != 0)
      return UnableToLegalize;
    int NumParts = SizeOp0 / NarrowSize;

    // List the registers where the destination will be scattered.
    SmallVector<Register, 2> DstRegs;
    // List the registers where the source will be split.
    SmallVector<Register, 2> SrcRegs;

    // Create all the temporary registers.
    for (int i = 0; i < NumParts; ++i) {
      Register SrcReg = MRI.createGenericVirtualRegister(NarrowTy);

      SrcRegs.push_back(SrcReg);
    }

    // Explode the big arguments into smaller chunks.
    MIRBuilder.buildUnmerge(SrcRegs, MI.getOperand(1));

    Register AshrCstReg =
        MIRBuilder.buildConstant(NarrowTy, NarrowTy.getScalarSizeInBits() - 1)
            .getReg(0);
    Register FullExtensionReg;
    Register PartialExtensionReg;

    // Do the operation on each small part.
    for (int i = 0; i < NumParts; ++i) {
      if ((i + 1) * NarrowTy.getScalarSizeInBits() <= SizeInBits) {
        DstRegs.push_back(SrcRegs[i]);
        PartialExtensionReg = DstRegs.back();
      } else if (i * NarrowTy.getScalarSizeInBits() >= SizeInBits) {
        assert(PartialExtensionReg &&
               "Expected to visit partial extension before full");
        if (FullExtensionReg) {
          DstRegs.push_back(FullExtensionReg);
          continue;
        }
        DstRegs.push_back(
            MIRBuilder.buildAShr(NarrowTy, PartialExtensionReg, AshrCstReg)
                .getReg(0));
        FullExtensionReg = DstRegs.back();
      } else {
        DstRegs.push_back(
            MIRBuilder
                .buildInstr(
                    TargetOpcode::G_SEXT_INREG, {NarrowTy},
                    {SrcRegs[i], SizeInBits % NarrowTy.getScalarSizeInBits()})
                .getReg(0));
        PartialExtensionReg = DstRegs.back();
      }
    }

    // Gather the destination registers into the final destination.
    Register DstReg = MI.getOperand(0).getReg();
    MIRBuilder.buildMergeLikeInstr(DstReg, DstRegs);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_BSWAP:
  case TargetOpcode::G_BITREVERSE: {
    if (SizeOp0 % NarrowSize != 0)
      return UnableToLegalize;

    Observer.changingInstr(MI);
    SmallVector<Register, 2> SrcRegs, DstRegs;
    unsigned NumParts = SizeOp0 / NarrowSize;
    extractParts(MI.getOperand(1).getReg(), NarrowTy, NumParts, SrcRegs,
                 MIRBuilder, MRI);

    for (unsigned i = 0; i < NumParts; ++i) {
      auto DstPart = MIRBuilder.buildInstr(MI.getOpcode(), {NarrowTy},
                                           {SrcRegs[NumParts - 1 - i]});
      DstRegs.push_back(DstPart.getReg(0));
    }

    MIRBuilder.buildMergeLikeInstr(MI.getOperand(0), DstRegs);

    Observer.changedInstr(MI);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_PTR_ADD:
  case TargetOpcode::G_PTRMASK: {
    if (TypeIdx != 1)
      return UnableToLegalize;
    Observer.changingInstr(MI);
    narrowScalarSrc(MI, NarrowTy, 2);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_FPTOUI:
  case TargetOpcode::G_FPTOSI:
    return narrowScalarFPTOI(MI, TypeIdx, NarrowTy);
  case TargetOpcode::G_FPEXT:
    if (TypeIdx != 0)
      return UnableToLegalize;
    Observer.changingInstr(MI);
    narrowScalarDst(MI, NarrowTy, 0, TargetOpcode::G_FPEXT);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_FLDEXP:
  case TargetOpcode::G_STRICT_FLDEXP:
    return narrowScalarFLDEXP(MI, TypeIdx, NarrowTy);
  case TargetOpcode::G_VSCALE: {
    Register Dst = MI.getOperand(0).getReg();
    LLT Ty = MRI.getType(Dst);

    // Assume VSCALE(1) fits into a legal integer
    const APInt One(NarrowTy.getSizeInBits(), 1);
    auto VScaleBase = MIRBuilder.buildVScale(NarrowTy, One);
    auto ZExt = MIRBuilder.buildZExt(Ty, VScaleBase);
    auto C = MIRBuilder.buildConstant(Ty, *MI.getOperand(1).getCImm());
    MIRBuilder.buildMul(Dst, ZExt, C);

    MI.eraseFromParent();
    return Legalized;
  }
  }
}

Register LegalizerHelper::coerceToScalar(Register Val) {
  LLT Ty = MRI.getType(Val);
  if (Ty.isScalar())
    return Val;

  const DataLayout &DL = MIRBuilder.getDataLayout();
  LLT NewTy = LLT::scalar(Ty.getSizeInBits());
  if (Ty.isPointer()) {
    if (DL.isNonIntegralAddressSpace(Ty.getAddressSpace()))
      return Register();
    return MIRBuilder.buildPtrToInt(NewTy, Val).getReg(0);
  }

  Register NewVal = Val;

  assert(Ty.isVector());
  if (Ty.isPointerVector())
    NewVal = MIRBuilder.buildPtrToInt(NewTy, NewVal).getReg(0);
  return MIRBuilder.buildBitcast(NewTy, NewVal).getReg(0);
}

void LegalizerHelper::widenScalarSrc(MachineInstr &MI, LLT WideTy,
                                     unsigned OpIdx, unsigned ExtOpcode) {
  MachineOperand &MO = MI.getOperand(OpIdx);
  auto ExtB = MIRBuilder.buildInstr(ExtOpcode, {WideTy}, {MO});
  MO.setReg(ExtB.getReg(0));
}

void LegalizerHelper::narrowScalarSrc(MachineInstr &MI, LLT NarrowTy,
                                      unsigned OpIdx) {
  MachineOperand &MO = MI.getOperand(OpIdx);
  auto ExtB = MIRBuilder.buildTrunc(NarrowTy, MO);
  MO.setReg(ExtB.getReg(0));
}

void LegalizerHelper::widenScalarDst(MachineInstr &MI, LLT WideTy,
                                     unsigned OpIdx, unsigned TruncOpcode) {
  MachineOperand &MO = MI.getOperand(OpIdx);
  Register DstExt = MRI.createGenericVirtualRegister(WideTy);
  MIRBuilder.setInsertPt(MIRBuilder.getMBB(), ++MIRBuilder.getInsertPt());
  MIRBuilder.buildInstr(TruncOpcode, {MO}, {DstExt});
  MO.setReg(DstExt);
}

void LegalizerHelper::narrowScalarDst(MachineInstr &MI, LLT NarrowTy,
                                      unsigned OpIdx, unsigned ExtOpcode) {
  MachineOperand &MO = MI.getOperand(OpIdx);
  Register DstTrunc = MRI.createGenericVirtualRegister(NarrowTy);
  MIRBuilder.setInsertPt(MIRBuilder.getMBB(), ++MIRBuilder.getInsertPt());
  MIRBuilder.buildInstr(ExtOpcode, {MO}, {DstTrunc});
  MO.setReg(DstTrunc);
}

void LegalizerHelper::moreElementsVectorDst(MachineInstr &MI, LLT WideTy,
                                            unsigned OpIdx) {
  MachineOperand &MO = MI.getOperand(OpIdx);
  MIRBuilder.setInsertPt(MIRBuilder.getMBB(), ++MIRBuilder.getInsertPt());
  Register Dst = MO.getReg();
  Register DstExt = MRI.createGenericVirtualRegister(WideTy);
  MO.setReg(DstExt);
  MIRBuilder.buildDeleteTrailingVectorElements(Dst, DstExt);
}

void LegalizerHelper::moreElementsVectorSrc(MachineInstr &MI, LLT MoreTy,
                                            unsigned OpIdx) {
  MachineOperand &MO = MI.getOperand(OpIdx);
  SmallVector<Register, 8> Regs;
  MO.setReg(MIRBuilder.buildPadVectorWithUndefElements(MoreTy, MO).getReg(0));
}

void LegalizerHelper::bitcastSrc(MachineInstr &MI, LLT CastTy, unsigned OpIdx) {
  MachineOperand &Op = MI.getOperand(OpIdx);
  Op.setReg(MIRBuilder.buildBitcast(CastTy, Op).getReg(0));
}

void LegalizerHelper::bitcastDst(MachineInstr &MI, LLT CastTy, unsigned OpIdx) {
  MachineOperand &MO = MI.getOperand(OpIdx);
  Register CastDst = MRI.createGenericVirtualRegister(CastTy);
  MIRBuilder.setInsertPt(MIRBuilder.getMBB(), ++MIRBuilder.getInsertPt());
  MIRBuilder.buildBitcast(MO, CastDst);
  MO.setReg(CastDst);
}

LegalizerHelper::LegalizeResult
LegalizerHelper::widenScalarMergeValues(MachineInstr &MI, unsigned TypeIdx,
                                        LLT WideTy) {
  if (TypeIdx != 1)
    return UnableToLegalize;

  auto [DstReg, DstTy, Src1Reg, Src1Ty] = MI.getFirst2RegLLTs();
  if (DstTy.isVector())
    return UnableToLegalize;

  LLT SrcTy = MRI.getType(Src1Reg);
  const int DstSize = DstTy.getSizeInBits();
  const int SrcSize = SrcTy.getSizeInBits();
  const int WideSize = WideTy.getSizeInBits();
  const int NumMerge = (DstSize + WideSize - 1) / WideSize;

  unsigned NumOps = MI.getNumOperands();
  unsigned NumSrc = MI.getNumOperands() - 1;
  unsigned PartSize = DstTy.getSizeInBits() / NumSrc;

  if (WideSize >= DstSize) {
    // Directly pack the bits in the target type.
    Register ResultReg = MIRBuilder.buildZExt(WideTy, Src1Reg).getReg(0);

    for (unsigned I = 2; I != NumOps; ++I) {
      const unsigned Offset = (I - 1) * PartSize;

      Register SrcReg = MI.getOperand(I).getReg();
      assert(MRI.getType(SrcReg) == LLT::scalar(PartSize));

      auto ZextInput = MIRBuilder.buildZExt(WideTy, SrcReg);

      Register NextResult = I + 1 == NumOps && WideTy == DstTy ? DstReg :
        MRI.createGenericVirtualRegister(WideTy);

      auto ShiftAmt = MIRBuilder.buildConstant(WideTy, Offset);
      auto Shl = MIRBuilder.buildShl(WideTy, ZextInput, ShiftAmt);
      MIRBuilder.buildOr(NextResult, ResultReg, Shl);
      ResultReg = NextResult;
    }

    if (WideSize > DstSize)
      MIRBuilder.buildTrunc(DstReg, ResultReg);
    else if (DstTy.isPointer())
      MIRBuilder.buildIntToPtr(DstReg, ResultReg);

    MI.eraseFromParent();
    return Legalized;
  }

  // Unmerge the original values to the GCD type, and recombine to the next
  // multiple greater than the original type.
  //
  // %3:_(s12) = G_MERGE_VALUES %0:_(s4), %1:_(s4), %2:_(s4) -> s6
  // %4:_(s2), %5:_(s2) = G_UNMERGE_VALUES %0
  // %6:_(s2), %7:_(s2) = G_UNMERGE_VALUES %1
  // %8:_(s2), %9:_(s2) = G_UNMERGE_VALUES %2
  // %10:_(s6) = G_MERGE_VALUES %4, %5, %6
  // %11:_(s6) = G_MERGE_VALUES %7, %8, %9
  // %12:_(s12) = G_MERGE_VALUES %10, %11
  //
  // Padding with undef if necessary:
  //
  // %2:_(s8) = G_MERGE_VALUES %0:_(s4), %1:_(s4) -> s6
  // %3:_(s2), %4:_(s2) = G_UNMERGE_VALUES %0
  // %5:_(s2), %6:_(s2) = G_UNMERGE_VALUES %1
  // %7:_(s2) = G_IMPLICIT_DEF
  // %8:_(s6) = G_MERGE_VALUES %3, %4, %5
  // %9:_(s6) = G_MERGE_VALUES %6, %7, %7
  // %10:_(s12) = G_MERGE_VALUES %8, %9

  const int GCD = std::gcd(SrcSize, WideSize);
  LLT GCDTy = LLT::scalar(GCD);

  SmallVector<Register, 8> Parts;
  SmallVector<Register, 8> NewMergeRegs;
  SmallVector<Register, 8> Unmerges;
  LLT WideDstTy = LLT::scalar(NumMerge * WideSize);

  // Decompose the original operands if they don't evenly divide.
  for (const MachineOperand &MO : llvm::drop_begin(MI.operands())) {
    Register SrcReg = MO.getReg();
    if (GCD == SrcSize) {
      Unmerges.push_back(SrcReg);
    } else {
      auto Unmerge = MIRBuilder.buildUnmerge(GCDTy, SrcReg);
      for (int J = 0, JE = Unmerge->getNumOperands() - 1; J != JE; ++J)
        Unmerges.push_back(Unmerge.getReg(J));
    }
  }

  // Pad with undef to the next size that is a multiple of the requested size.
  if (static_cast<int>(Unmerges.size()) != NumMerge * WideSize) {
    Register UndefReg = MIRBuilder.buildUndef(GCDTy).getReg(0);
    for (int I = Unmerges.size(); I != NumMerge * WideSize; ++I)
      Unmerges.push_back(UndefReg);
  }

  const int PartsPerGCD = WideSize / GCD;

  // Build merges of each piece.
  ArrayRef<Register> Slicer(Unmerges);
  for (int I = 0; I != NumMerge; ++I, Slicer = Slicer.drop_front(PartsPerGCD)) {
    auto Merge =
        MIRBuilder.buildMergeLikeInstr(WideTy, Slicer.take_front(PartsPerGCD));
    NewMergeRegs.push_back(Merge.getReg(0));
  }

  // A truncate may be necessary if the requested type doesn't evenly divide the
  // original result type.
  if (DstTy.getSizeInBits() == WideDstTy.getSizeInBits()) {
    MIRBuilder.buildMergeLikeInstr(DstReg, NewMergeRegs);
  } else {
    auto FinalMerge = MIRBuilder.buildMergeLikeInstr(WideDstTy, NewMergeRegs);
    MIRBuilder.buildTrunc(DstReg, FinalMerge.getReg(0));
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::widenScalarUnmergeValues(MachineInstr &MI, unsigned TypeIdx,
                                          LLT WideTy) {
  if (TypeIdx != 0)
    return UnableToLegalize;

  int NumDst = MI.getNumOperands() - 1;
  Register SrcReg = MI.getOperand(NumDst).getReg();
  LLT SrcTy = MRI.getType(SrcReg);
  if (SrcTy.isVector())
    return UnableToLegalize;

  Register Dst0Reg = MI.getOperand(0).getReg();
  LLT DstTy = MRI.getType(Dst0Reg);
  if (!DstTy.isScalar())
    return UnableToLegalize;

  if (WideTy.getSizeInBits() >= SrcTy.getSizeInBits()) {
    if (SrcTy.isPointer()) {
      const DataLayout &DL = MIRBuilder.getDataLayout();
      if (DL.isNonIntegralAddressSpace(SrcTy.getAddressSpace())) {
        LLVM_DEBUG(
            dbgs() << "Not casting non-integral address space integer\n");
        return UnableToLegalize;
      }

      SrcTy = LLT::scalar(SrcTy.getSizeInBits());
      SrcReg = MIRBuilder.buildPtrToInt(SrcTy, SrcReg).getReg(0);
    }

    // Widen SrcTy to WideTy. This does not affect the result, but since the
    // user requested this size, it is probably better handled than SrcTy and
    // should reduce the total number of legalization artifacts.
    if (WideTy.getSizeInBits() > SrcTy.getSizeInBits()) {
      SrcTy = WideTy;
      SrcReg = MIRBuilder.buildAnyExt(WideTy, SrcReg).getReg(0);
    }

    // Theres no unmerge type to target. Directly extract the bits from the
    // source type
    unsigned DstSize = DstTy.getSizeInBits();

    MIRBuilder.buildTrunc(Dst0Reg, SrcReg);
    for (int I = 1; I != NumDst; ++I) {
      auto ShiftAmt = MIRBuilder.buildConstant(SrcTy, DstSize * I);
      auto Shr = MIRBuilder.buildLShr(SrcTy, SrcReg, ShiftAmt);
      MIRBuilder.buildTrunc(MI.getOperand(I), Shr);
    }

    MI.eraseFromParent();
    return Legalized;
  }

  // Extend the source to a wider type.
  LLT LCMTy = getLCMType(SrcTy, WideTy);

  Register WideSrc = SrcReg;
  if (LCMTy.getSizeInBits() != SrcTy.getSizeInBits()) {
    // TODO: If this is an integral address space, cast to integer and anyext.
    if (SrcTy.isPointer()) {
      LLVM_DEBUG(dbgs() << "Widening pointer source types not implemented\n");
      return UnableToLegalize;
    }

    WideSrc = MIRBuilder.buildAnyExt(LCMTy, WideSrc).getReg(0);
  }

  auto Unmerge = MIRBuilder.buildUnmerge(WideTy, WideSrc);

  // Create a sequence of unmerges and merges to the original results. Since we
  // may have widened the source, we will need to pad the results with dead defs
  // to cover the source register.
  // e.g. widen s48 to s64:
  // %1:_(s48), %2:_(s48) = G_UNMERGE_VALUES %0:_(s96)
  //
  // =>
  //  %4:_(s192) = G_ANYEXT %0:_(s96)
  //  %5:_(s64), %6, %7 = G_UNMERGE_VALUES %4 ; Requested unmerge
  //  ; unpack to GCD type, with extra dead defs
  //  %8:_(s16), %9, %10, %11 = G_UNMERGE_VALUES %5:_(s64)
  //  %12:_(s16), %13, dead %14, dead %15 = G_UNMERGE_VALUES %6:_(s64)
  //  dead %16:_(s16), dead %17, dead %18, dead %18 = G_UNMERGE_VALUES %7:_(s64)
  //  %1:_(s48) = G_MERGE_VALUES %8:_(s16), %9, %10   ; Remerge to destination
  //  %2:_(s48) = G_MERGE_VALUES %11:_(s16), %12, %13 ; Remerge to destination
  const LLT GCDTy = getGCDType(WideTy, DstTy);
  const int NumUnmerge = Unmerge->getNumOperands() - 1;
  const int PartsPerRemerge = DstTy.getSizeInBits() / GCDTy.getSizeInBits();

  // Directly unmerge to the destination without going through a GCD type
  // if possible
  if (PartsPerRemerge == 1) {
    const int PartsPerUnmerge = WideTy.getSizeInBits() / DstTy.getSizeInBits();

    for (int I = 0; I != NumUnmerge; ++I) {
      auto MIB = MIRBuilder.buildInstr(TargetOpcode::G_UNMERGE_VALUES);

      for (int J = 0; J != PartsPerUnmerge; ++J) {
        int Idx = I * PartsPerUnmerge + J;
        if (Idx < NumDst)
          MIB.addDef(MI.getOperand(Idx).getReg());
        else {
          // Create dead def for excess components.
          MIB.addDef(MRI.createGenericVirtualRegister(DstTy));
        }
      }

      MIB.addUse(Unmerge.getReg(I));
    }
  } else {
    SmallVector<Register, 16> Parts;
    for (int J = 0; J != NumUnmerge; ++J)
      extractGCDType(Parts, GCDTy, Unmerge.getReg(J));

    SmallVector<Register, 8> RemergeParts;
    for (int I = 0; I != NumDst; ++I) {
      for (int J = 0; J < PartsPerRemerge; ++J) {
        const int Idx = I * PartsPerRemerge + J;
        RemergeParts.emplace_back(Parts[Idx]);
      }

      MIRBuilder.buildMergeLikeInstr(MI.getOperand(I).getReg(), RemergeParts);
      RemergeParts.clear();
    }
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::widenScalarExtract(MachineInstr &MI, unsigned TypeIdx,
                                    LLT WideTy) {
  auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();
  unsigned Offset = MI.getOperand(2).getImm();

  if (TypeIdx == 0) {
    if (SrcTy.isVector() || DstTy.isVector())
      return UnableToLegalize;

    SrcOp Src(SrcReg);
    if (SrcTy.isPointer()) {
      // Extracts from pointers can be handled only if they are really just
      // simple integers.
      const DataLayout &DL = MIRBuilder.getDataLayout();
      if (DL.isNonIntegralAddressSpace(SrcTy.getAddressSpace()))
        return UnableToLegalize;

      LLT SrcAsIntTy = LLT::scalar(SrcTy.getSizeInBits());
      Src = MIRBuilder.buildPtrToInt(SrcAsIntTy, Src);
      SrcTy = SrcAsIntTy;
    }

    if (DstTy.isPointer())
      return UnableToLegalize;

    if (Offset == 0) {
      // Avoid a shift in the degenerate case.
      MIRBuilder.buildTrunc(DstReg,
                            MIRBuilder.buildAnyExtOrTrunc(WideTy, Src));
      MI.eraseFromParent();
      return Legalized;
    }

    // Do a shift in the source type.
    LLT ShiftTy = SrcTy;
    if (WideTy.getSizeInBits() > SrcTy.getSizeInBits()) {
      Src = MIRBuilder.buildAnyExt(WideTy, Src);
      ShiftTy = WideTy;
    }

    auto LShr = MIRBuilder.buildLShr(
      ShiftTy, Src, MIRBuilder.buildConstant(ShiftTy, Offset));
    MIRBuilder.buildTrunc(DstReg, LShr);
    MI.eraseFromParent();
    return Legalized;
  }

  if (SrcTy.isScalar()) {
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);
    Observer.changedInstr(MI);
    return Legalized;
  }

  if (!SrcTy.isVector())
    return UnableToLegalize;

  if (DstTy != SrcTy.getElementType())
    return UnableToLegalize;

  if (Offset % SrcTy.getScalarSizeInBits() != 0)
    return UnableToLegalize;

  Observer.changingInstr(MI);
  widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);

  MI.getOperand(2).setImm((WideTy.getSizeInBits() / SrcTy.getSizeInBits()) *
                          Offset);
  widenScalarDst(MI, WideTy.getScalarType(), 0);
  Observer.changedInstr(MI);
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::widenScalarInsert(MachineInstr &MI, unsigned TypeIdx,
                                   LLT WideTy) {
  if (TypeIdx != 0 || WideTy.isVector())
    return UnableToLegalize;
  Observer.changingInstr(MI);
  widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);
  widenScalarDst(MI, WideTy);
  Observer.changedInstr(MI);
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::widenScalarAddSubOverflow(MachineInstr &MI, unsigned TypeIdx,
                                           LLT WideTy) {
  unsigned Opcode;
  unsigned ExtOpcode;
  std::optional<Register> CarryIn;
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unexpected opcode!");
  case TargetOpcode::G_SADDO:
    Opcode = TargetOpcode::G_ADD;
    ExtOpcode = TargetOpcode::G_SEXT;
    break;
  case TargetOpcode::G_SSUBO:
    Opcode = TargetOpcode::G_SUB;
    ExtOpcode = TargetOpcode::G_SEXT;
    break;
  case TargetOpcode::G_UADDO:
    Opcode = TargetOpcode::G_ADD;
    ExtOpcode = TargetOpcode::G_ZEXT;
    break;
  case TargetOpcode::G_USUBO:
    Opcode = TargetOpcode::G_SUB;
    ExtOpcode = TargetOpcode::G_ZEXT;
    break;
  case TargetOpcode::G_SADDE:
    Opcode = TargetOpcode::G_UADDE;
    ExtOpcode = TargetOpcode::G_SEXT;
    CarryIn = MI.getOperand(4).getReg();
    break;
  case TargetOpcode::G_SSUBE:
    Opcode = TargetOpcode::G_USUBE;
    ExtOpcode = TargetOpcode::G_SEXT;
    CarryIn = MI.getOperand(4).getReg();
    break;
  case TargetOpcode::G_UADDE:
    Opcode = TargetOpcode::G_UADDE;
    ExtOpcode = TargetOpcode::G_ZEXT;
    CarryIn = MI.getOperand(4).getReg();
    break;
  case TargetOpcode::G_USUBE:
    Opcode = TargetOpcode::G_USUBE;
    ExtOpcode = TargetOpcode::G_ZEXT;
    CarryIn = MI.getOperand(4).getReg();
    break;
  }

  if (TypeIdx == 1) {
    unsigned BoolExtOp = MIRBuilder.getBoolExtOp(WideTy.isVector(), false);

    Observer.changingInstr(MI);
    if (CarryIn)
      widenScalarSrc(MI, WideTy, 4, BoolExtOp);
    widenScalarDst(MI, WideTy, 1);

    Observer.changedInstr(MI);
    return Legalized;
  }

  auto LHSExt = MIRBuilder.buildInstr(ExtOpcode, {WideTy}, {MI.getOperand(2)});
  auto RHSExt = MIRBuilder.buildInstr(ExtOpcode, {WideTy}, {MI.getOperand(3)});
  // Do the arithmetic in the larger type.
  Register NewOp;
  if (CarryIn) {
    LLT CarryOutTy = MRI.getType(MI.getOperand(1).getReg());
    NewOp = MIRBuilder
                .buildInstr(Opcode, {WideTy, CarryOutTy},
                            {LHSExt, RHSExt, *CarryIn})
                .getReg(0);
  } else {
    NewOp = MIRBuilder.buildInstr(Opcode, {WideTy}, {LHSExt, RHSExt}).getReg(0);
  }
  LLT OrigTy = MRI.getType(MI.getOperand(0).getReg());
  auto TruncOp = MIRBuilder.buildTrunc(OrigTy, NewOp);
  auto ExtOp = MIRBuilder.buildInstr(ExtOpcode, {WideTy}, {TruncOp});
  // There is no overflow if the ExtOp is the same as NewOp.
  MIRBuilder.buildICmp(CmpInst::ICMP_NE, MI.getOperand(1), NewOp, ExtOp);
  // Now trunc the NewOp to the original result.
  MIRBuilder.buildTrunc(MI.getOperand(0), NewOp);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::widenScalarAddSubShlSat(MachineInstr &MI, unsigned TypeIdx,
                                         LLT WideTy) {
  bool IsSigned = MI.getOpcode() == TargetOpcode::G_SADDSAT ||
                  MI.getOpcode() == TargetOpcode::G_SSUBSAT ||
                  MI.getOpcode() == TargetOpcode::G_SSHLSAT;
  bool IsShift = MI.getOpcode() == TargetOpcode::G_SSHLSAT ||
                 MI.getOpcode() == TargetOpcode::G_USHLSAT;
  // We can convert this to:
  //   1. Any extend iN to iM
  //   2. SHL by M-N
  //   3. [US][ADD|SUB|SHL]SAT
  //   4. L/ASHR by M-N
  //
  // It may be more efficient to lower this to a min and a max operation in
  // the higher precision arithmetic if the promoted operation isn't legal,
  // but this decision is up to the target's lowering request.
  Register DstReg = MI.getOperand(0).getReg();

  unsigned NewBits = WideTy.getScalarSizeInBits();
  unsigned SHLAmount = NewBits - MRI.getType(DstReg).getScalarSizeInBits();

  // Shifts must zero-extend the RHS to preserve the unsigned quantity, and
  // must not left shift the RHS to preserve the shift amount.
  auto LHS = MIRBuilder.buildAnyExt(WideTy, MI.getOperand(1));
  auto RHS = IsShift ? MIRBuilder.buildZExt(WideTy, MI.getOperand(2))
                     : MIRBuilder.buildAnyExt(WideTy, MI.getOperand(2));
  auto ShiftK = MIRBuilder.buildConstant(WideTy, SHLAmount);
  auto ShiftL = MIRBuilder.buildShl(WideTy, LHS, ShiftK);
  auto ShiftR = IsShift ? RHS : MIRBuilder.buildShl(WideTy, RHS, ShiftK);

  auto WideInst = MIRBuilder.buildInstr(MI.getOpcode(), {WideTy},
                                        {ShiftL, ShiftR}, MI.getFlags());

  // Use a shift that will preserve the number of sign bits when the trunc is
  // folded away.
  auto Result = IsSigned ? MIRBuilder.buildAShr(WideTy, WideInst, ShiftK)
                         : MIRBuilder.buildLShr(WideTy, WideInst, ShiftK);

  MIRBuilder.buildTrunc(DstReg, Result);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::widenScalarMulo(MachineInstr &MI, unsigned TypeIdx,
                                 LLT WideTy) {
  if (TypeIdx == 1) {
    Observer.changingInstr(MI);
    widenScalarDst(MI, WideTy, 1);
    Observer.changedInstr(MI);
    return Legalized;
  }

  bool IsSigned = MI.getOpcode() == TargetOpcode::G_SMULO;
  auto [Result, OriginalOverflow, LHS, RHS] = MI.getFirst4Regs();
  LLT SrcTy = MRI.getType(LHS);
  LLT OverflowTy = MRI.getType(OriginalOverflow);
  unsigned SrcBitWidth = SrcTy.getScalarSizeInBits();

  // To determine if the result overflowed in the larger type, we extend the
  // input to the larger type, do the multiply (checking if it overflows),
  // then also check the high bits of the result to see if overflow happened
  // there.
  unsigned ExtOp = IsSigned ? TargetOpcode::G_SEXT : TargetOpcode::G_ZEXT;
  auto LeftOperand = MIRBuilder.buildInstr(ExtOp, {WideTy}, {LHS});
  auto RightOperand = MIRBuilder.buildInstr(ExtOp, {WideTy}, {RHS});

  // Multiplication cannot overflow if the WideTy is >= 2 * original width,
  // so we don't need to check the overflow result of larger type Mulo.
  bool WideMulCanOverflow = WideTy.getScalarSizeInBits() < 2 * SrcBitWidth;

  unsigned MulOpc =
      WideMulCanOverflow ? MI.getOpcode() : (unsigned)TargetOpcode::G_MUL;

  MachineInstrBuilder Mulo;
  if (WideMulCanOverflow)
    Mulo = MIRBuilder.buildInstr(MulOpc, {WideTy, OverflowTy},
                                 {LeftOperand, RightOperand});
  else
    Mulo = MIRBuilder.buildInstr(MulOpc, {WideTy}, {LeftOperand, RightOperand});

  auto Mul = Mulo->getOperand(0);
  MIRBuilder.buildTrunc(Result, Mul);

  MachineInstrBuilder ExtResult;
  // Overflow occurred if it occurred in the larger type, or if the high part
  // of the result does not zero/sign-extend the low part.  Check this second
  // possibility first.
  if (IsSigned) {
    // For signed, overflow occurred when the high part does not sign-extend
    // the low part.
    ExtResult = MIRBuilder.buildSExtInReg(WideTy, Mul, SrcBitWidth);
  } else {
    // Unsigned overflow occurred when the high part does not zero-extend the
    // low part.
    ExtResult = MIRBuilder.buildZExtInReg(WideTy, Mul, SrcBitWidth);
  }

  if (WideMulCanOverflow) {
    auto Overflow =
        MIRBuilder.buildICmp(CmpInst::ICMP_NE, OverflowTy, Mul, ExtResult);
    // Finally check if the multiplication in the larger type itself overflowed.
    MIRBuilder.buildOr(OriginalOverflow, Mulo->getOperand(1), Overflow);
  } else {
    MIRBuilder.buildICmp(CmpInst::ICMP_NE, OriginalOverflow, Mul, ExtResult);
  }
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::widenScalar(MachineInstr &MI, unsigned TypeIdx, LLT WideTy) {
  switch (MI.getOpcode()) {
  default:
    return UnableToLegalize;
  case TargetOpcode::G_ATOMICRMW_XCHG:
  case TargetOpcode::G_ATOMICRMW_ADD:
  case TargetOpcode::G_ATOMICRMW_SUB:
  case TargetOpcode::G_ATOMICRMW_AND:
  case TargetOpcode::G_ATOMICRMW_OR:
  case TargetOpcode::G_ATOMICRMW_XOR:
  case TargetOpcode::G_ATOMICRMW_MIN:
  case TargetOpcode::G_ATOMICRMW_MAX:
  case TargetOpcode::G_ATOMICRMW_UMIN:
  case TargetOpcode::G_ATOMICRMW_UMAX:
    assert(TypeIdx == 0 && "atomicrmw with second scalar type");
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ANYEXT);
    widenScalarDst(MI, WideTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_ATOMIC_CMPXCHG:
    assert(TypeIdx == 0 && "G_ATOMIC_CMPXCHG with second scalar type");
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ANYEXT);
    widenScalarSrc(MI, WideTy, 3, TargetOpcode::G_ANYEXT);
    widenScalarDst(MI, WideTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_ATOMIC_CMPXCHG_WITH_SUCCESS:
    if (TypeIdx == 0) {
      Observer.changingInstr(MI);
      widenScalarSrc(MI, WideTy, 3, TargetOpcode::G_ANYEXT);
      widenScalarSrc(MI, WideTy, 4, TargetOpcode::G_ANYEXT);
      widenScalarDst(MI, WideTy, 0);
      Observer.changedInstr(MI);
      return Legalized;
    }
    assert(TypeIdx == 1 &&
           "G_ATOMIC_CMPXCHG_WITH_SUCCESS with third scalar type");
    Observer.changingInstr(MI);
    widenScalarDst(MI, WideTy, 1);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_EXTRACT:
    return widenScalarExtract(MI, TypeIdx, WideTy);
  case TargetOpcode::G_INSERT:
    return widenScalarInsert(MI, TypeIdx, WideTy);
  case TargetOpcode::G_MERGE_VALUES:
    return widenScalarMergeValues(MI, TypeIdx, WideTy);
  case TargetOpcode::G_UNMERGE_VALUES:
    return widenScalarUnmergeValues(MI, TypeIdx, WideTy);
  case TargetOpcode::G_SADDO:
  case TargetOpcode::G_SSUBO:
  case TargetOpcode::G_UADDO:
  case TargetOpcode::G_USUBO:
  case TargetOpcode::G_SADDE:
  case TargetOpcode::G_SSUBE:
  case TargetOpcode::G_UADDE:
  case TargetOpcode::G_USUBE:
    return widenScalarAddSubOverflow(MI, TypeIdx, WideTy);
  case TargetOpcode::G_UMULO:
  case TargetOpcode::G_SMULO:
    return widenScalarMulo(MI, TypeIdx, WideTy);
  case TargetOpcode::G_SADDSAT:
  case TargetOpcode::G_SSUBSAT:
  case TargetOpcode::G_SSHLSAT:
  case TargetOpcode::G_UADDSAT:
  case TargetOpcode::G_USUBSAT:
  case TargetOpcode::G_USHLSAT:
    return widenScalarAddSubShlSat(MI, TypeIdx, WideTy);
  case TargetOpcode::G_CTTZ:
  case TargetOpcode::G_CTTZ_ZERO_UNDEF:
  case TargetOpcode::G_CTLZ:
  case TargetOpcode::G_CTLZ_ZERO_UNDEF:
  case TargetOpcode::G_CTPOP: {
    if (TypeIdx == 0) {
      Observer.changingInstr(MI);
      widenScalarDst(MI, WideTy, 0);
      Observer.changedInstr(MI);
      return Legalized;
    }

    Register SrcReg = MI.getOperand(1).getReg();

    // First extend the input.
    unsigned ExtOpc = MI.getOpcode() == TargetOpcode::G_CTTZ ||
                              MI.getOpcode() == TargetOpcode::G_CTTZ_ZERO_UNDEF
                          ? TargetOpcode::G_ANYEXT
                          : TargetOpcode::G_ZEXT;
    auto MIBSrc = MIRBuilder.buildInstr(ExtOpc, {WideTy}, {SrcReg});
    LLT CurTy = MRI.getType(SrcReg);
    unsigned NewOpc = MI.getOpcode();
    if (NewOpc == TargetOpcode::G_CTTZ) {
      // The count is the same in the larger type except if the original
      // value was zero.  This can be handled by setting the bit just off
      // the top of the original type.
      auto TopBit =
          APInt::getOneBitSet(WideTy.getSizeInBits(), CurTy.getSizeInBits());
      MIBSrc = MIRBuilder.buildOr(
        WideTy, MIBSrc, MIRBuilder.buildConstant(WideTy, TopBit));
      // Now we know the operand is non-zero, use the more relaxed opcode.
      NewOpc = TargetOpcode::G_CTTZ_ZERO_UNDEF;
    }

    unsigned SizeDiff = WideTy.getSizeInBits() - CurTy.getSizeInBits();

    if (MI.getOpcode() == TargetOpcode::G_CTLZ_ZERO_UNDEF) {
      // An optimization where the result is the CTLZ after the left shift by
      // (Difference in widety and current ty), that is,
      // MIBSrc = MIBSrc << (sizeinbits(WideTy) - sizeinbits(CurTy))
      // Result = ctlz MIBSrc
      MIBSrc = MIRBuilder.buildShl(WideTy, MIBSrc,
                                   MIRBuilder.buildConstant(WideTy, SizeDiff));
    }

    // Perform the operation at the larger size.
    auto MIBNewOp = MIRBuilder.buildInstr(NewOpc, {WideTy}, {MIBSrc});
    // This is already the correct result for CTPOP and CTTZs
    if (MI.getOpcode() == TargetOpcode::G_CTLZ) {
      // The correct result is NewOp - (Difference in widety and current ty).
      MIBNewOp = MIRBuilder.buildSub(
          WideTy, MIBNewOp, MIRBuilder.buildConstant(WideTy, SizeDiff));
    }

    MIRBuilder.buildZExtOrTrunc(MI.getOperand(0), MIBNewOp);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_BSWAP: {
    Observer.changingInstr(MI);
    Register DstReg = MI.getOperand(0).getReg();

    Register ShrReg = MRI.createGenericVirtualRegister(WideTy);
    Register DstExt = MRI.createGenericVirtualRegister(WideTy);
    Register ShiftAmtReg = MRI.createGenericVirtualRegister(WideTy);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);

    MI.getOperand(0).setReg(DstExt);

    MIRBuilder.setInsertPt(MIRBuilder.getMBB(), ++MIRBuilder.getInsertPt());

    LLT Ty = MRI.getType(DstReg);
    unsigned DiffBits = WideTy.getScalarSizeInBits() - Ty.getScalarSizeInBits();
    MIRBuilder.buildConstant(ShiftAmtReg, DiffBits);
    MIRBuilder.buildLShr(ShrReg, DstExt, ShiftAmtReg);

    MIRBuilder.buildTrunc(DstReg, ShrReg);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_BITREVERSE: {
    Observer.changingInstr(MI);

    Register DstReg = MI.getOperand(0).getReg();
    LLT Ty = MRI.getType(DstReg);
    unsigned DiffBits = WideTy.getScalarSizeInBits() - Ty.getScalarSizeInBits();

    Register DstExt = MRI.createGenericVirtualRegister(WideTy);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);
    MI.getOperand(0).setReg(DstExt);
    MIRBuilder.setInsertPt(MIRBuilder.getMBB(), ++MIRBuilder.getInsertPt());

    auto ShiftAmt = MIRBuilder.buildConstant(WideTy, DiffBits);
    auto Shift = MIRBuilder.buildLShr(WideTy, DstExt, ShiftAmt);
    MIRBuilder.buildTrunc(DstReg, Shift);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_FREEZE:
  case TargetOpcode::G_CONSTANT_FOLD_BARRIER:
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);
    widenScalarDst(MI, WideTy);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_ABS:
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_SEXT);
    widenScalarDst(MI, WideTy);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_ADD:
  case TargetOpcode::G_AND:
  case TargetOpcode::G_MUL:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_XOR:
  case TargetOpcode::G_SUB:
  case TargetOpcode::G_SHUFFLE_VECTOR:
    // Perform operation at larger width (any extension is fines here, high bits
    // don't affect the result) and then truncate the result back to the
    // original type.
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ANYEXT);
    widenScalarDst(MI, WideTy);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_SBFX:
  case TargetOpcode::G_UBFX:
    Observer.changingInstr(MI);

    if (TypeIdx == 0) {
      widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);
      widenScalarDst(MI, WideTy);
    } else {
      widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ZEXT);
      widenScalarSrc(MI, WideTy, 3, TargetOpcode::G_ZEXT);
    }

    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_SHL:
    Observer.changingInstr(MI);

    if (TypeIdx == 0) {
      widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);
      widenScalarDst(MI, WideTy);
    } else {
      assert(TypeIdx == 1);
      // The "number of bits to shift" operand must preserve its value as an
      // unsigned integer:
      widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ZEXT);
    }

    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_ROTR:
  case TargetOpcode::G_ROTL:
    if (TypeIdx != 1)
      return UnableToLegalize;

    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ZEXT);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_SDIV:
  case TargetOpcode::G_SREM:
  case TargetOpcode::G_SMIN:
  case TargetOpcode::G_SMAX:
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_SEXT);
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_SEXT);
    widenScalarDst(MI, WideTy);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_SDIVREM:
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_SEXT);
    widenScalarSrc(MI, WideTy, 3, TargetOpcode::G_SEXT);
    widenScalarDst(MI, WideTy);
    widenScalarDst(MI, WideTy, 1);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_ASHR:
  case TargetOpcode::G_LSHR:
    Observer.changingInstr(MI);

    if (TypeIdx == 0) {
      unsigned CvtOp = MI.getOpcode() == TargetOpcode::G_ASHR ?
        TargetOpcode::G_SEXT : TargetOpcode::G_ZEXT;

      widenScalarSrc(MI, WideTy, 1, CvtOp);
      widenScalarDst(MI, WideTy);
    } else {
      assert(TypeIdx == 1);
      // The "number of bits to shift" operand must preserve its value as an
      // unsigned integer:
      widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ZEXT);
    }

    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_UDIV:
  case TargetOpcode::G_UREM:
  case TargetOpcode::G_UMIN:
  case TargetOpcode::G_UMAX:
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ZEXT);
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ZEXT);
    widenScalarDst(MI, WideTy);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_UDIVREM:
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ZEXT);
    widenScalarSrc(MI, WideTy, 3, TargetOpcode::G_ZEXT);
    widenScalarDst(MI, WideTy);
    widenScalarDst(MI, WideTy, 1);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_SELECT:
    Observer.changingInstr(MI);
    if (TypeIdx == 0) {
      // Perform operation at larger width (any extension is fine here, high
      // bits don't affect the result) and then truncate the result back to the
      // original type.
      widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ANYEXT);
      widenScalarSrc(MI, WideTy, 3, TargetOpcode::G_ANYEXT);
      widenScalarDst(MI, WideTy);
    } else {
      bool IsVec = MRI.getType(MI.getOperand(1).getReg()).isVector();
      // Explicit extension is required here since high bits affect the result.
      widenScalarSrc(MI, WideTy, 1, MIRBuilder.getBoolExtOp(IsVec, false));
    }
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_FPTOSI:
  case TargetOpcode::G_FPTOUI:
  case TargetOpcode::G_INTRINSIC_LRINT:
  case TargetOpcode::G_INTRINSIC_LLRINT:
  case TargetOpcode::G_IS_FPCLASS:
    Observer.changingInstr(MI);

    if (TypeIdx == 0)
      widenScalarDst(MI, WideTy);
    else
      widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_FPEXT);

    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_SITOFP:
    Observer.changingInstr(MI);

    if (TypeIdx == 0)
      widenScalarDst(MI, WideTy, 0, TargetOpcode::G_FPTRUNC);
    else
      widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_SEXT);

    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_UITOFP:
    Observer.changingInstr(MI);

    if (TypeIdx == 0)
      widenScalarDst(MI, WideTy, 0, TargetOpcode::G_FPTRUNC);
    else
      widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ZEXT);

    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_LOAD:
  case TargetOpcode::G_SEXTLOAD:
  case TargetOpcode::G_ZEXTLOAD:
    Observer.changingInstr(MI);
    widenScalarDst(MI, WideTy);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_STORE: {
    if (TypeIdx != 0)
      return UnableToLegalize;

    LLT Ty = MRI.getType(MI.getOperand(0).getReg());
    if (!Ty.isScalar())
      return UnableToLegalize;

    Observer.changingInstr(MI);

    unsigned ExtType = Ty.getScalarSizeInBits() == 1 ?
      TargetOpcode::G_ZEXT : TargetOpcode::G_ANYEXT;
    widenScalarSrc(MI, WideTy, 0, ExtType);

    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_CONSTANT: {
    MachineOperand &SrcMO = MI.getOperand(1);
    LLVMContext &Ctx = MIRBuilder.getMF().getFunction().getContext();
    unsigned ExtOpc = LI.getExtOpcodeForWideningConstant(
        MRI.getType(MI.getOperand(0).getReg()));
    assert((ExtOpc == TargetOpcode::G_ZEXT || ExtOpc == TargetOpcode::G_SEXT ||
            ExtOpc == TargetOpcode::G_ANYEXT) &&
           "Illegal Extend");
    const APInt &SrcVal = SrcMO.getCImm()->getValue();
    const APInt &Val = (ExtOpc == TargetOpcode::G_SEXT)
                           ? SrcVal.sext(WideTy.getSizeInBits())
                           : SrcVal.zext(WideTy.getSizeInBits());
    Observer.changingInstr(MI);
    SrcMO.setCImm(ConstantInt::get(Ctx, Val));

    widenScalarDst(MI, WideTy);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_FCONSTANT: {
    // To avoid changing the bits of the constant due to extension to a larger
    // type and then using G_FPTRUNC, we simply convert to a G_CONSTANT.
    MachineOperand &SrcMO = MI.getOperand(1);
    APInt Val = SrcMO.getFPImm()->getValueAPF().bitcastToAPInt();
    MIRBuilder.setInstrAndDebugLoc(MI);
    auto IntCst = MIRBuilder.buildConstant(MI.getOperand(0).getReg(), Val);
    widenScalarDst(*IntCst, WideTy, 0, TargetOpcode::G_TRUNC);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_IMPLICIT_DEF: {
    Observer.changingInstr(MI);
    widenScalarDst(MI, WideTy);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_BRCOND:
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 0, MIRBuilder.getBoolExtOp(false, false));
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_FCMP:
    Observer.changingInstr(MI);
    if (TypeIdx == 0)
      widenScalarDst(MI, WideTy);
    else {
      widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_FPEXT);
      widenScalarSrc(MI, WideTy, 3, TargetOpcode::G_FPEXT);
    }
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_ICMP:
    Observer.changingInstr(MI);
    if (TypeIdx == 0)
      widenScalarDst(MI, WideTy);
    else {
      unsigned ExtOpcode = CmpInst::isSigned(static_cast<CmpInst::Predicate>(
                               MI.getOperand(1).getPredicate()))
                               ? TargetOpcode::G_SEXT
                               : TargetOpcode::G_ZEXT;
      widenScalarSrc(MI, WideTy, 2, ExtOpcode);
      widenScalarSrc(MI, WideTy, 3, ExtOpcode);
    }
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_PTR_ADD:
    assert(TypeIdx == 1 && "unable to legalize pointer of G_PTR_ADD");
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_SEXT);
    Observer.changedInstr(MI);
    return Legalized;

  case TargetOpcode::G_PHI: {
    assert(TypeIdx == 0 && "Expecting only Idx 0");

    Observer.changingInstr(MI);
    for (unsigned I = 1; I < MI.getNumOperands(); I += 2) {
      MachineBasicBlock &OpMBB = *MI.getOperand(I + 1).getMBB();
      MIRBuilder.setInsertPt(OpMBB, OpMBB.getFirstTerminatorForward());
      widenScalarSrc(MI, WideTy, I, TargetOpcode::G_ANYEXT);
    }

    MachineBasicBlock &MBB = *MI.getParent();
    MIRBuilder.setInsertPt(MBB, --MBB.getFirstNonPHI());
    widenScalarDst(MI, WideTy);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_EXTRACT_VECTOR_ELT: {
    if (TypeIdx == 0) {
      Register VecReg = MI.getOperand(1).getReg();
      LLT VecTy = MRI.getType(VecReg);
      Observer.changingInstr(MI);

      widenScalarSrc(
          MI, LLT::vector(VecTy.getElementCount(), WideTy.getSizeInBits()), 1,
          TargetOpcode::G_ANYEXT);

      widenScalarDst(MI, WideTy, 0);
      Observer.changedInstr(MI);
      return Legalized;
    }

    if (TypeIdx != 2)
      return UnableToLegalize;
    Observer.changingInstr(MI);
    // TODO: Probably should be zext
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_SEXT);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_INSERT_VECTOR_ELT: {
    if (TypeIdx == 0) {
      Observer.changingInstr(MI);
      const LLT WideEltTy = WideTy.getElementType();

      widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);
      widenScalarSrc(MI, WideEltTy, 2, TargetOpcode::G_ANYEXT);
      widenScalarDst(MI, WideTy, 0);
      Observer.changedInstr(MI);
      return Legalized;
    }

    if (TypeIdx == 1) {
      Observer.changingInstr(MI);

      Register VecReg = MI.getOperand(1).getReg();
      LLT VecTy = MRI.getType(VecReg);
      LLT WideVecTy = LLT::vector(VecTy.getElementCount(), WideTy);

      widenScalarSrc(MI, WideVecTy, 1, TargetOpcode::G_ANYEXT);
      widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ANYEXT);
      widenScalarDst(MI, WideVecTy, 0);
      Observer.changedInstr(MI);
      return Legalized;
    }

    if (TypeIdx == 2) {
      Observer.changingInstr(MI);
      // TODO: Probably should be zext
      widenScalarSrc(MI, WideTy, 3, TargetOpcode::G_SEXT);
      Observer.changedInstr(MI);
      return Legalized;
    }

    return UnableToLegalize;
  }
  case TargetOpcode::G_FADD:
  case TargetOpcode::G_FMUL:
  case TargetOpcode::G_FSUB:
  case TargetOpcode::G_FMA:
  case TargetOpcode::G_FMAD:
  case TargetOpcode::G_FNEG:
  case TargetOpcode::G_FABS:
  case TargetOpcode::G_FCANONICALIZE:
  case TargetOpcode::G_FMINNUM:
  case TargetOpcode::G_FMAXNUM:
  case TargetOpcode::G_FMINNUM_IEEE:
  case TargetOpcode::G_FMAXNUM_IEEE:
  case TargetOpcode::G_FMINIMUM:
  case TargetOpcode::G_FMAXIMUM:
  case TargetOpcode::G_FDIV:
  case TargetOpcode::G_FREM:
  case TargetOpcode::G_FCEIL:
  case TargetOpcode::G_FFLOOR:
  case TargetOpcode::G_FCOS:
  case TargetOpcode::G_FSIN:
  case TargetOpcode::G_FTAN:
  case TargetOpcode::G_FACOS:
  case TargetOpcode::G_FASIN:
  case TargetOpcode::G_FATAN:
  case TargetOpcode::G_FCOSH:
  case TargetOpcode::G_FSINH:
  case TargetOpcode::G_FTANH:
  case TargetOpcode::G_FLOG10:
  case TargetOpcode::G_FLOG:
  case TargetOpcode::G_FLOG2:
  case TargetOpcode::G_FRINT:
  case TargetOpcode::G_FNEARBYINT:
  case TargetOpcode::G_FSQRT:
  case TargetOpcode::G_FEXP:
  case TargetOpcode::G_FEXP2:
  case TargetOpcode::G_FEXP10:
  case TargetOpcode::G_FPOW:
  case TargetOpcode::G_INTRINSIC_TRUNC:
  case TargetOpcode::G_INTRINSIC_ROUND:
  case TargetOpcode::G_INTRINSIC_ROUNDEVEN:
    assert(TypeIdx == 0);
    Observer.changingInstr(MI);

    for (unsigned I = 1, E = MI.getNumOperands(); I != E; ++I)
      widenScalarSrc(MI, WideTy, I, TargetOpcode::G_FPEXT);

    widenScalarDst(MI, WideTy, 0, TargetOpcode::G_FPTRUNC);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_FPOWI:
  case TargetOpcode::G_FLDEXP:
  case TargetOpcode::G_STRICT_FLDEXP: {
    if (TypeIdx == 0) {
      if (MI.getOpcode() == TargetOpcode::G_STRICT_FLDEXP)
        return UnableToLegalize;

      Observer.changingInstr(MI);
      widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_FPEXT);
      widenScalarDst(MI, WideTy, 0, TargetOpcode::G_FPTRUNC);
      Observer.changedInstr(MI);
      return Legalized;
    }

    if (TypeIdx == 1) {
      // For some reason SelectionDAG tries to promote to a libcall without
      // actually changing the integer type for promotion.
      Observer.changingInstr(MI);
      widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_SEXT);
      Observer.changedInstr(MI);
      return Legalized;
    }

    return UnableToLegalize;
  }
  case TargetOpcode::G_FFREXP: {
    Observer.changingInstr(MI);

    if (TypeIdx == 0) {
      widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_FPEXT);
      widenScalarDst(MI, WideTy, 0, TargetOpcode::G_FPTRUNC);
    } else {
      widenScalarDst(MI, WideTy, 1);
    }

    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_INTTOPTR:
    if (TypeIdx != 1)
      return UnableToLegalize;

    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ZEXT);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_PTRTOINT:
    if (TypeIdx != 0)
      return UnableToLegalize;

    Observer.changingInstr(MI);
    widenScalarDst(MI, WideTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_BUILD_VECTOR: {
    Observer.changingInstr(MI);

    const LLT WideEltTy = TypeIdx == 1 ? WideTy : WideTy.getElementType();
    for (int I = 1, E = MI.getNumOperands(); I != E; ++I)
      widenScalarSrc(MI, WideEltTy, I, TargetOpcode::G_ANYEXT);

    // Avoid changing the result vector type if the source element type was
    // requested.
    if (TypeIdx == 1) {
      MI.setDesc(MIRBuilder.getTII().get(TargetOpcode::G_BUILD_VECTOR_TRUNC));
    } else {
      widenScalarDst(MI, WideTy, 0);
    }

    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_SEXT_INREG:
    if (TypeIdx != 0)
      return UnableToLegalize;

    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);
    widenScalarDst(MI, WideTy, 0, TargetOpcode::G_TRUNC);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_PTRMASK: {
    if (TypeIdx != 1)
      return UnableToLegalize;
    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 2, TargetOpcode::G_ZEXT);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_VECREDUCE_FADD:
  case TargetOpcode::G_VECREDUCE_FMUL:
  case TargetOpcode::G_VECREDUCE_FMIN:
  case TargetOpcode::G_VECREDUCE_FMAX:
  case TargetOpcode::G_VECREDUCE_FMINIMUM:
  case TargetOpcode::G_VECREDUCE_FMAXIMUM: {
    if (TypeIdx != 0)
      return UnableToLegalize;
    Observer.changingInstr(MI);
    Register VecReg = MI.getOperand(1).getReg();
    LLT VecTy = MRI.getType(VecReg);
    LLT WideVecTy = VecTy.isVector()
                        ? LLT::vector(VecTy.getElementCount(), WideTy)
                        : WideTy;
    widenScalarSrc(MI, WideVecTy, 1, TargetOpcode::G_FPEXT);
    widenScalarDst(MI, WideTy, 0, TargetOpcode::G_FPTRUNC);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_VSCALE: {
    MachineOperand &SrcMO = MI.getOperand(1);
    LLVMContext &Ctx = MIRBuilder.getMF().getFunction().getContext();
    const APInt &SrcVal = SrcMO.getCImm()->getValue();
    // The CImm is always a signed value
    const APInt Val = SrcVal.sext(WideTy.getSizeInBits());
    Observer.changingInstr(MI);
    SrcMO.setCImm(ConstantInt::get(Ctx, Val));
    widenScalarDst(MI, WideTy);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_SPLAT_VECTOR: {
    if (TypeIdx != 1)
      return UnableToLegalize;

    Observer.changingInstr(MI);
    widenScalarSrc(MI, WideTy, 1, TargetOpcode::G_ANYEXT);
    Observer.changedInstr(MI);
    return Legalized;
  }
  }
}

static void getUnmergePieces(SmallVectorImpl<Register> &Pieces,
                             MachineIRBuilder &B, Register Src, LLT Ty) {
  auto Unmerge = B.buildUnmerge(Ty, Src);
  for (int I = 0, E = Unmerge->getNumOperands() - 1; I != E; ++I)
    Pieces.push_back(Unmerge.getReg(I));
}

static void emitLoadFromConstantPool(Register DstReg, const Constant *ConstVal,
                                     MachineIRBuilder &MIRBuilder) {
  MachineRegisterInfo &MRI = *MIRBuilder.getMRI();
  MachineFunction &MF = MIRBuilder.getMF();
  const DataLayout &DL = MIRBuilder.getDataLayout();
  unsigned AddrSpace = DL.getDefaultGlobalsAddressSpace();
  LLT AddrPtrTy = LLT::pointer(AddrSpace, DL.getPointerSizeInBits(AddrSpace));
  LLT DstLLT = MRI.getType(DstReg);

  Align Alignment(DL.getABITypeAlign(ConstVal->getType()));

  auto Addr = MIRBuilder.buildConstantPool(
      AddrPtrTy,
      MF.getConstantPool()->getConstantPoolIndex(ConstVal, Alignment));

  MachineMemOperand *MMO =
      MF.getMachineMemOperand(MachinePointerInfo::getConstantPool(MF),
                              MachineMemOperand::MOLoad, DstLLT, Alignment);

  MIRBuilder.buildLoadInstr(TargetOpcode::G_LOAD, DstReg, Addr, *MMO);
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerConstant(MachineInstr &MI) {
  const MachineOperand &ConstOperand = MI.getOperand(1);
  const Constant *ConstantVal = ConstOperand.getCImm();

  emitLoadFromConstantPool(MI.getOperand(0).getReg(), ConstantVal, MIRBuilder);
  MI.eraseFromParent();

  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerFConstant(MachineInstr &MI) {
  const MachineOperand &ConstOperand = MI.getOperand(1);
  const Constant *ConstantVal = ConstOperand.getFPImm();

  emitLoadFromConstantPool(MI.getOperand(0).getReg(), ConstantVal, MIRBuilder);
  MI.eraseFromParent();

  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerBitcast(MachineInstr &MI) {
  auto [Dst, DstTy, Src, SrcTy] = MI.getFirst2RegLLTs();
  if (SrcTy.isVector()) {
    LLT SrcEltTy = SrcTy.getElementType();
    SmallVector<Register, 8> SrcRegs;

    if (DstTy.isVector()) {
      int NumDstElt = DstTy.getNumElements();
      int NumSrcElt = SrcTy.getNumElements();

      LLT DstEltTy = DstTy.getElementType();
      LLT DstCastTy = DstEltTy; // Intermediate bitcast result type
      LLT SrcPartTy = SrcEltTy; // Original unmerge result type.

      // If there's an element size mismatch, insert intermediate casts to match
      // the result element type.
      if (NumSrcElt < NumDstElt) { // Source element type is larger.
        // %1:_(<4 x s8>) = G_BITCAST %0:_(<2 x s16>)
        //
        // =>
        //
        // %2:_(s16), %3:_(s16) = G_UNMERGE_VALUES %0
        // %3:_(<2 x s8>) = G_BITCAST %2
        // %4:_(<2 x s8>) = G_BITCAST %3
        // %1:_(<4 x s16>) = G_CONCAT_VECTORS %3, %4
        DstCastTy = LLT::fixed_vector(NumDstElt / NumSrcElt, DstEltTy);
        SrcPartTy = SrcEltTy;
      } else if (NumSrcElt > NumDstElt) { // Source element type is smaller.
        //
        // %1:_(<2 x s16>) = G_BITCAST %0:_(<4 x s8>)
        //
        // =>
        //
        // %2:_(<2 x s8>), %3:_(<2 x s8>) = G_UNMERGE_VALUES %0
        // %3:_(s16) = G_BITCAST %2
        // %4:_(s16) = G_BITCAST %3
        // %1:_(<2 x s16>) = G_BUILD_VECTOR %3, %4
        SrcPartTy = LLT::fixed_vector(NumSrcElt / NumDstElt, SrcEltTy);
        DstCastTy = DstEltTy;
      }

      getUnmergePieces(SrcRegs, MIRBuilder, Src, SrcPartTy);
      for (Register &SrcReg : SrcRegs)
        SrcReg = MIRBuilder.buildBitcast(DstCastTy, SrcReg).getReg(0);
    } else
      getUnmergePieces(SrcRegs, MIRBuilder, Src, SrcEltTy);

    MIRBuilder.buildMergeLikeInstr(Dst, SrcRegs);
    MI.eraseFromParent();
    return Legalized;
  }

  if (DstTy.isVector()) {
    SmallVector<Register, 8> SrcRegs;
    getUnmergePieces(SrcRegs, MIRBuilder, Src, DstTy.getElementType());
    MIRBuilder.buildMergeLikeInstr(Dst, SrcRegs);
    MI.eraseFromParent();
    return Legalized;
  }

  return UnableToLegalize;
}

/// Figure out the bit offset into a register when coercing a vector index for
/// the wide element type. This is only for the case when promoting vector to
/// one with larger elements.
//
///
/// %offset_idx = G_AND %idx, ~(-1 << Log2(DstEltSize / SrcEltSize))
/// %offset_bits = G_SHL %offset_idx, Log2(SrcEltSize)
static Register getBitcastWiderVectorElementOffset(MachineIRBuilder &B,
                                                   Register Idx,
                                                   unsigned NewEltSize,
                                                   unsigned OldEltSize) {
  const unsigned Log2EltRatio = Log2_32(NewEltSize / OldEltSize);
  LLT IdxTy = B.getMRI()->getType(Idx);

  // Now figure out the amount we need to shift to get the target bits.
  auto OffsetMask = B.buildConstant(
      IdxTy, ~(APInt::getAllOnes(IdxTy.getSizeInBits()) << Log2EltRatio));
  auto OffsetIdx = B.buildAnd(IdxTy, Idx, OffsetMask);
  return B.buildShl(IdxTy, OffsetIdx,
                    B.buildConstant(IdxTy, Log2_32(OldEltSize))).getReg(0);
}

/// Perform a G_EXTRACT_VECTOR_ELT in a different sized vector element. If this
/// is casting to a vector with a smaller element size, perform multiple element
/// extracts and merge the results. If this is coercing to a vector with larger
/// elements, index the bitcasted vector and extract the target element with bit
/// operations. This is intended to force the indexing in the native register
/// size for architectures that can dynamically index the register file.
LegalizerHelper::LegalizeResult
LegalizerHelper::bitcastExtractVectorElt(MachineInstr &MI, unsigned TypeIdx,
                                         LLT CastTy) {
  if (TypeIdx != 1)
    return UnableToLegalize;

  auto [Dst, DstTy, SrcVec, SrcVecTy, Idx, IdxTy] = MI.getFirst3RegLLTs();

  LLT SrcEltTy = SrcVecTy.getElementType();
  unsigned NewNumElts = CastTy.isVector() ? CastTy.getNumElements() : 1;
  unsigned OldNumElts = SrcVecTy.getNumElements();

  LLT NewEltTy = CastTy.isVector() ? CastTy.getElementType() : CastTy;
  Register CastVec = MIRBuilder.buildBitcast(CastTy, SrcVec).getReg(0);

  const unsigned NewEltSize = NewEltTy.getSizeInBits();
  const unsigned OldEltSize = SrcEltTy.getSizeInBits();
  if (NewNumElts > OldNumElts) {
    // Decreasing the vector element size
    //
    // e.g. i64 = extract_vector_elt x:v2i64, y:i32
    //  =>
    //  v4i32:castx = bitcast x:v2i64
    //
    // i64 = bitcast
    //   (v2i32 build_vector (i32 (extract_vector_elt castx, (2 * y))),
    //                       (i32 (extract_vector_elt castx, (2 * y + 1)))
    //
    if (NewNumElts % OldNumElts != 0)
      return UnableToLegalize;

    // Type of the intermediate result vector.
    const unsigned NewEltsPerOldElt = NewNumElts / OldNumElts;
    LLT MidTy =
        LLT::scalarOrVector(ElementCount::getFixed(NewEltsPerOldElt), NewEltTy);

    auto NewEltsPerOldEltK = MIRBuilder.buildConstant(IdxTy, NewEltsPerOldElt);

    SmallVector<Register, 8> NewOps(NewEltsPerOldElt);
    auto NewBaseIdx = MIRBuilder.buildMul(IdxTy, Idx, NewEltsPerOldEltK);

    for (unsigned I = 0; I < NewEltsPerOldElt; ++I) {
      auto IdxOffset = MIRBuilder.buildConstant(IdxTy, I);
      auto TmpIdx = MIRBuilder.buildAdd(IdxTy, NewBaseIdx, IdxOffset);
      auto Elt = MIRBuilder.buildExtractVectorElement(NewEltTy, CastVec, TmpIdx);
      NewOps[I] = Elt.getReg(0);
    }

    auto NewVec = MIRBuilder.buildBuildVector(MidTy, NewOps);
    MIRBuilder.buildBitcast(Dst, NewVec);
    MI.eraseFromParent();
    return Legalized;
  }

  if (NewNumElts < OldNumElts) {
    if (NewEltSize % OldEltSize != 0)
      return UnableToLegalize;

    // This only depends on powers of 2 because we use bit tricks to figure out
    // the bit offset we need to shift to get the target element. A general
    // expansion could emit division/multiply.
    if (!isPowerOf2_32(NewEltSize / OldEltSize))
      return UnableToLegalize;

    // Increasing the vector element size.
    // %elt:_(small_elt) = G_EXTRACT_VECTOR_ELT %vec:_(<N x small_elt>), %idx
    //
    //   =>
    //
    // %cast = G_BITCAST %vec
    // %scaled_idx = G_LSHR %idx, Log2(DstEltSize / SrcEltSize)
    // %wide_elt  = G_EXTRACT_VECTOR_ELT %cast, %scaled_idx
    // %offset_idx = G_AND %idx, ~(-1 << Log2(DstEltSize / SrcEltSize))
    // %offset_bits = G_SHL %offset_idx, Log2(SrcEltSize)
    // %elt_bits = G_LSHR %wide_elt, %offset_bits
    // %elt = G_TRUNC %elt_bits

    const unsigned Log2EltRatio = Log2_32(NewEltSize / OldEltSize);
    auto Log2Ratio = MIRBuilder.buildConstant(IdxTy, Log2EltRatio);

    // Divide to get the index in the wider element type.
    auto ScaledIdx = MIRBuilder.buildLShr(IdxTy, Idx, Log2Ratio);

    Register WideElt = CastVec;
    if (CastTy.isVector()) {
      WideElt = MIRBuilder.buildExtractVectorElement(NewEltTy, CastVec,
                                                     ScaledIdx).getReg(0);
    }

    // Compute the bit offset into the register of the target element.
    Register OffsetBits = getBitcastWiderVectorElementOffset(
      MIRBuilder, Idx, NewEltSize, OldEltSize);

    // Shift the wide element to get the target element.
    auto ExtractedBits = MIRBuilder.buildLShr(NewEltTy, WideElt, OffsetBits);
    MIRBuilder.buildTrunc(Dst, ExtractedBits);
    MI.eraseFromParent();
    return Legalized;
  }

  return UnableToLegalize;
}

/// Emit code to insert \p InsertReg into \p TargetRet at \p OffsetBits in \p
/// TargetReg, while preserving other bits in \p TargetReg.
///
/// (InsertReg << Offset) | (TargetReg & ~(-1 >> InsertReg.size()) << Offset)
static Register buildBitFieldInsert(MachineIRBuilder &B,
                                    Register TargetReg, Register InsertReg,
                                    Register OffsetBits) {
  LLT TargetTy = B.getMRI()->getType(TargetReg);
  LLT InsertTy = B.getMRI()->getType(InsertReg);
  auto ZextVal = B.buildZExt(TargetTy, InsertReg);
  auto ShiftedInsertVal = B.buildShl(TargetTy, ZextVal, OffsetBits);

  // Produce a bitmask of the value to insert
  auto EltMask = B.buildConstant(
    TargetTy, APInt::getLowBitsSet(TargetTy.getSizeInBits(),
                                   InsertTy.getSizeInBits()));
  // Shift it into position
  auto ShiftedMask = B.buildShl(TargetTy, EltMask, OffsetBits);
  auto InvShiftedMask = B.buildNot(TargetTy, ShiftedMask);

  // Clear out the bits in the wide element
  auto MaskedOldElt = B.buildAnd(TargetTy, TargetReg, InvShiftedMask);

  // The value to insert has all zeros already, so stick it into the masked
  // wide element.
  return B.buildOr(TargetTy, MaskedOldElt, ShiftedInsertVal).getReg(0);
}

/// Perform a G_INSERT_VECTOR_ELT in a different sized vector element. If this
/// is increasing the element size, perform the indexing in the target element
/// type, and use bit operations to insert at the element position. This is
/// intended for architectures that can dynamically index the register file and
/// want to force indexing in the native register size.
LegalizerHelper::LegalizeResult
LegalizerHelper::bitcastInsertVectorElt(MachineInstr &MI, unsigned TypeIdx,
                                        LLT CastTy) {
  if (TypeIdx != 0)
    return UnableToLegalize;

  auto [Dst, DstTy, SrcVec, SrcVecTy, Val, ValTy, Idx, IdxTy] =
      MI.getFirst4RegLLTs();
  LLT VecTy = DstTy;

  LLT VecEltTy = VecTy.getElementType();
  LLT NewEltTy = CastTy.isVector() ? CastTy.getElementType() : CastTy;
  const unsigned NewEltSize = NewEltTy.getSizeInBits();
  const unsigned OldEltSize = VecEltTy.getSizeInBits();

  unsigned NewNumElts = CastTy.isVector() ? CastTy.getNumElements() : 1;
  unsigned OldNumElts = VecTy.getNumElements();

  Register CastVec = MIRBuilder.buildBitcast(CastTy, SrcVec).getReg(0);
  if (NewNumElts < OldNumElts) {
    if (NewEltSize % OldEltSize != 0)
      return UnableToLegalize;

    // This only depends on powers of 2 because we use bit tricks to figure out
    // the bit offset we need to shift to get the target element. A general
    // expansion could emit division/multiply.
    if (!isPowerOf2_32(NewEltSize / OldEltSize))
      return UnableToLegalize;

    const unsigned Log2EltRatio = Log2_32(NewEltSize / OldEltSize);
    auto Log2Ratio = MIRBuilder.buildConstant(IdxTy, Log2EltRatio);

    // Divide to get the index in the wider element type.
    auto ScaledIdx = MIRBuilder.buildLShr(IdxTy, Idx, Log2Ratio);

    Register ExtractedElt = CastVec;
    if (CastTy.isVector()) {
      ExtractedElt = MIRBuilder.buildExtractVectorElement(NewEltTy, CastVec,
                                                          ScaledIdx).getReg(0);
    }

    // Compute the bit offset into the register of the target element.
    Register OffsetBits = getBitcastWiderVectorElementOffset(
      MIRBuilder, Idx, NewEltSize, OldEltSize);

    Register InsertedElt = buildBitFieldInsert(MIRBuilder, ExtractedElt,
                                               Val, OffsetBits);
    if (CastTy.isVector()) {
      InsertedElt = MIRBuilder.buildInsertVectorElement(
        CastTy, CastVec, InsertedElt, ScaledIdx).getReg(0);
    }

    MIRBuilder.buildBitcast(Dst, InsertedElt);
    MI.eraseFromParent();
    return Legalized;
  }

  return UnableToLegalize;
}

// This attempts to handle G_CONCAT_VECTORS with illegal operands, particularly
// those that have smaller than legal operands.
//
// <16 x s8> = G_CONCAT_VECTORS <4 x s8>, <4 x s8>, <4 x s8>, <4 x s8>
//
// ===>
//
// s32 = G_BITCAST <4 x s8>
// s32 = G_BITCAST <4 x s8>
// s32 = G_BITCAST <4 x s8>
// s32 = G_BITCAST <4 x s8>
// <4 x s32> = G_BUILD_VECTOR s32, s32, s32, s32
// <16 x s8> = G_BITCAST <4 x s32>
LegalizerHelper::LegalizeResult
LegalizerHelper::bitcastConcatVector(MachineInstr &MI, unsigned TypeIdx,
                                     LLT CastTy) {
  // Convert it to CONCAT instruction
  auto ConcatMI = dyn_cast<GConcatVectors>(&MI);
  if (!ConcatMI) {
    return UnableToLegalize;
  }

  // Check if bitcast is Legal
  auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();
  LLT SrcScalTy = LLT::scalar(SrcTy.getSizeInBits());

  // Check if the build vector is Legal
  if (!LI.isLegal({TargetOpcode::G_BUILD_VECTOR, {CastTy, SrcScalTy}})) {
    return UnableToLegalize;
  }

  // Bitcast the sources
  SmallVector<Register> BitcastRegs;
  for (unsigned i = 0; i < ConcatMI->getNumSources(); i++) {
    BitcastRegs.push_back(
        MIRBuilder.buildBitcast(SrcScalTy, ConcatMI->getSourceReg(i))
            .getReg(0));
  }

  // Build the scalar values into a vector
  Register BuildReg =
      MIRBuilder.buildBuildVector(CastTy, BitcastRegs).getReg(0);
  MIRBuilder.buildBitcast(DstReg, BuildReg);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerLoad(GAnyLoad &LoadMI) {
  // Lower to a memory-width G_LOAD and a G_SEXT/G_ZEXT/G_ANYEXT
  Register DstReg = LoadMI.getDstReg();
  Register PtrReg = LoadMI.getPointerReg();
  LLT DstTy = MRI.getType(DstReg);
  MachineMemOperand &MMO = LoadMI.getMMO();
  LLT MemTy = MMO.getMemoryType();
  MachineFunction &MF = MIRBuilder.getMF();

  unsigned MemSizeInBits = MemTy.getSizeInBits();
  unsigned MemStoreSizeInBits = 8 * MemTy.getSizeInBytes();

  if (MemSizeInBits != MemStoreSizeInBits) {
    if (MemTy.isVector())
      return UnableToLegalize;

    // Promote to a byte-sized load if not loading an integral number of
    // bytes.  For example, promote EXTLOAD:i20 -> EXTLOAD:i24.
    LLT WideMemTy = LLT::scalar(MemStoreSizeInBits);
    MachineMemOperand *NewMMO =
        MF.getMachineMemOperand(&MMO, MMO.getPointerInfo(), WideMemTy);

    Register LoadReg = DstReg;
    LLT LoadTy = DstTy;

    // If this wasn't already an extending load, we need to widen the result
    // register to avoid creating a load with a narrower result than the source.
    if (MemStoreSizeInBits > DstTy.getSizeInBits()) {
      LoadTy = WideMemTy;
      LoadReg = MRI.createGenericVirtualRegister(WideMemTy);
    }

    if (isa<GSExtLoad>(LoadMI)) {
      auto NewLoad = MIRBuilder.buildLoad(LoadTy, PtrReg, *NewMMO);
      MIRBuilder.buildSExtInReg(LoadReg, NewLoad, MemSizeInBits);
    } else if (isa<GZExtLoad>(LoadMI) || WideMemTy == LoadTy) {
      auto NewLoad = MIRBuilder.buildLoad(LoadTy, PtrReg, *NewMMO);
      // The extra bits are guaranteed to be zero, since we stored them that
      // way.  A zext load from Wide thus automatically gives zext from MemVT.
      MIRBuilder.buildAssertZExt(LoadReg, NewLoad, MemSizeInBits);
    } else {
      MIRBuilder.buildLoad(LoadReg, PtrReg, *NewMMO);
    }

    if (DstTy != LoadTy)
      MIRBuilder.buildTrunc(DstReg, LoadReg);

    LoadMI.eraseFromParent();
    return Legalized;
  }

  // Big endian lowering not implemented.
  if (MIRBuilder.getDataLayout().isBigEndian())
    return UnableToLegalize;

  // This load needs splitting into power of 2 sized loads.
  //
  // Our strategy here is to generate anyextending loads for the smaller
  // types up to next power-2 result type, and then combine the two larger
  // result values together, before truncating back down to the non-pow-2
  // type.
  // E.g. v1 = i24 load =>
  // v2 = i32 zextload (2 byte)
  // v3 = i32 load (1 byte)
  // v4 = i32 shl v3, 16
  // v5 = i32 or v4, v2
  // v1 = i24 trunc v5
  // By doing this we generate the correct truncate which should get
  // combined away as an artifact with a matching extend.

  uint64_t LargeSplitSize, SmallSplitSize;

  if (!isPowerOf2_32(MemSizeInBits)) {
    // This load needs splitting into power of 2 sized loads.
    LargeSplitSize = llvm::bit_floor(MemSizeInBits);
    SmallSplitSize = MemSizeInBits - LargeSplitSize;
  } else {
    // This is already a power of 2, but we still need to split this in half.
    //
    // Assume we're being asked to decompose an unaligned load.
    // TODO: If this requires multiple splits, handle them all at once.
    auto &Ctx = MF.getFunction().getContext();
    if (TLI.allowsMemoryAccess(Ctx, MIRBuilder.getDataLayout(), MemTy, MMO))
      return UnableToLegalize;

    SmallSplitSize = LargeSplitSize = MemSizeInBits / 2;
  }

  if (MemTy.isVector()) {
    // TODO: Handle vector extloads
    if (MemTy != DstTy)
      return UnableToLegalize;

    // TODO: We can do better than scalarizing the vector and at least split it
    // in half.
    return reduceLoadStoreWidth(LoadMI, 0, DstTy.getElementType());
  }

  MachineMemOperand *LargeMMO =
      MF.getMachineMemOperand(&MMO, 0, LargeSplitSize / 8);
  MachineMemOperand *SmallMMO =
      MF.getMachineMemOperand(&MMO, LargeSplitSize / 8, SmallSplitSize / 8);

  LLT PtrTy = MRI.getType(PtrReg);
  unsigned AnyExtSize = PowerOf2Ceil(DstTy.getSizeInBits());
  LLT AnyExtTy = LLT::scalar(AnyExtSize);
  auto LargeLoad = MIRBuilder.buildLoadInstr(TargetOpcode::G_ZEXTLOAD, AnyExtTy,
                                             PtrReg, *LargeMMO);

  auto OffsetCst = MIRBuilder.buildConstant(LLT::scalar(PtrTy.getSizeInBits()),
                                            LargeSplitSize / 8);
  Register PtrAddReg = MRI.createGenericVirtualRegister(PtrTy);
  auto SmallPtr = MIRBuilder.buildPtrAdd(PtrAddReg, PtrReg, OffsetCst);
  auto SmallLoad = MIRBuilder.buildLoadInstr(LoadMI.getOpcode(), AnyExtTy,
                                             SmallPtr, *SmallMMO);

  auto ShiftAmt = MIRBuilder.buildConstant(AnyExtTy, LargeSplitSize);
  auto Shift = MIRBuilder.buildShl(AnyExtTy, SmallLoad, ShiftAmt);

  if (AnyExtTy == DstTy)
    MIRBuilder.buildOr(DstReg, Shift, LargeLoad);
  else if (AnyExtTy.getSizeInBits() != DstTy.getSizeInBits()) {
    auto Or = MIRBuilder.buildOr(AnyExtTy, Shift, LargeLoad);
    MIRBuilder.buildTrunc(DstReg, {Or});
  } else {
    assert(DstTy.isPointer() && "expected pointer");
    auto Or = MIRBuilder.buildOr(AnyExtTy, Shift, LargeLoad);

    // FIXME: We currently consider this to be illegal for non-integral address
    // spaces, but we need still need a way to reinterpret the bits.
    MIRBuilder.buildIntToPtr(DstReg, Or);
  }

  LoadMI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerStore(GStore &StoreMI) {
  // Lower a non-power of 2 store into multiple pow-2 stores.
  // E.g. split an i24 store into an i16 store + i8 store.
  // We do this by first extending the stored value to the next largest power
  // of 2 type, and then using truncating stores to store the components.
  // By doing this, likewise with G_LOAD, generate an extend that can be
  // artifact-combined away instead of leaving behind extracts.
  Register SrcReg = StoreMI.getValueReg();
  Register PtrReg = StoreMI.getPointerReg();
  LLT SrcTy = MRI.getType(SrcReg);
  MachineFunction &MF = MIRBuilder.getMF();
  MachineMemOperand &MMO = **StoreMI.memoperands_begin();
  LLT MemTy = MMO.getMemoryType();

  unsigned StoreWidth = MemTy.getSizeInBits();
  unsigned StoreSizeInBits = 8 * MemTy.getSizeInBytes();

  if (StoreWidth != StoreSizeInBits) {
    if (SrcTy.isVector())
      return UnableToLegalize;

    // Promote to a byte-sized store with upper bits zero if not
    // storing an integral number of bytes.  For example, promote
    // TRUNCSTORE:i1 X -> TRUNCSTORE:i8 (and X, 1)
    LLT WideTy = LLT::scalar(StoreSizeInBits);

    if (StoreSizeInBits > SrcTy.getSizeInBits()) {
      // Avoid creating a store with a narrower source than result.
      SrcReg = MIRBuilder.buildAnyExt(WideTy, SrcReg).getReg(0);
      SrcTy = WideTy;
    }

    auto ZextInReg = MIRBuilder.buildZExtInReg(SrcTy, SrcReg, StoreWidth);

    MachineMemOperand *NewMMO =
        MF.getMachineMemOperand(&MMO, MMO.getPointerInfo(), WideTy);
    MIRBuilder.buildStore(ZextInReg, PtrReg, *NewMMO);
    StoreMI.eraseFromParent();
    return Legalized;
  }

  if (MemTy.isVector()) {
    // TODO: Handle vector trunc stores
    if (MemTy != SrcTy)
      return UnableToLegalize;

    // TODO: We can do better than scalarizing the vector and at least split it
    // in half.
    return reduceLoadStoreWidth(StoreMI, 0, SrcTy.getElementType());
  }

  unsigned MemSizeInBits = MemTy.getSizeInBits();
  uint64_t LargeSplitSize, SmallSplitSize;

  if (!isPowerOf2_32(MemSizeInBits)) {
    LargeSplitSize = llvm::bit_floor<uint64_t>(MemTy.getSizeInBits());
    SmallSplitSize = MemTy.getSizeInBits() - LargeSplitSize;
  } else {
    auto &Ctx = MF.getFunction().getContext();
    if (TLI.allowsMemoryAccess(Ctx, MIRBuilder.getDataLayout(), MemTy, MMO))
      return UnableToLegalize; // Don't know what we're being asked to do.

    SmallSplitSize = LargeSplitSize = MemSizeInBits / 2;
  }

  // Extend to the next pow-2. If this store was itself the result of lowering,
  // e.g. an s56 store being broken into s32 + s24, we might have a stored type
  // that's wider than the stored size.
  unsigned AnyExtSize = PowerOf2Ceil(MemTy.getSizeInBits());
  const LLT NewSrcTy = LLT::scalar(AnyExtSize);

  if (SrcTy.isPointer()) {
    const LLT IntPtrTy = LLT::scalar(SrcTy.getSizeInBits());
    SrcReg = MIRBuilder.buildPtrToInt(IntPtrTy, SrcReg).getReg(0);
  }

  auto ExtVal = MIRBuilder.buildAnyExtOrTrunc(NewSrcTy, SrcReg);

  // Obtain the smaller value by shifting away the larger value.
  auto ShiftAmt = MIRBuilder.buildConstant(NewSrcTy, LargeSplitSize);
  auto SmallVal = MIRBuilder.buildLShr(NewSrcTy, ExtVal, ShiftAmt);

  // Generate the PtrAdd and truncating stores.
  LLT PtrTy = MRI.getType(PtrReg);
  auto OffsetCst = MIRBuilder.buildConstant(
    LLT::scalar(PtrTy.getSizeInBits()), LargeSplitSize / 8);
  auto SmallPtr =
    MIRBuilder.buildPtrAdd(PtrTy, PtrReg, OffsetCst);

  MachineMemOperand *LargeMMO =
    MF.getMachineMemOperand(&MMO, 0, LargeSplitSize / 8);
  MachineMemOperand *SmallMMO =
    MF.getMachineMemOperand(&MMO, LargeSplitSize / 8, SmallSplitSize / 8);
  MIRBuilder.buildStore(ExtVal, PtrReg, *LargeMMO);
  MIRBuilder.buildStore(SmallVal, SmallPtr, *SmallMMO);
  StoreMI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::bitcast(MachineInstr &MI, unsigned TypeIdx, LLT CastTy) {
  switch (MI.getOpcode()) {
  case TargetOpcode::G_LOAD: {
    if (TypeIdx != 0)
      return UnableToLegalize;
    MachineMemOperand &MMO = **MI.memoperands_begin();

    // Not sure how to interpret a bitcast of an extending load.
    if (MMO.getMemoryType().getSizeInBits() != CastTy.getSizeInBits())
      return UnableToLegalize;

    Observer.changingInstr(MI);
    bitcastDst(MI, CastTy, 0);
    MMO.setType(CastTy);
    // The range metadata is no longer valid when reinterpreted as a different
    // type.
    MMO.clearRanges();
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_STORE: {
    if (TypeIdx != 0)
      return UnableToLegalize;

    MachineMemOperand &MMO = **MI.memoperands_begin();

    // Not sure how to interpret a bitcast of a truncating store.
    if (MMO.getMemoryType().getSizeInBits() != CastTy.getSizeInBits())
      return UnableToLegalize;

    Observer.changingInstr(MI);
    bitcastSrc(MI, CastTy, 0);
    MMO.setType(CastTy);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_SELECT: {
    if (TypeIdx != 0)
      return UnableToLegalize;

    if (MRI.getType(MI.getOperand(1).getReg()).isVector()) {
      LLVM_DEBUG(
          dbgs() << "bitcast action not implemented for vector select\n");
      return UnableToLegalize;
    }

    Observer.changingInstr(MI);
    bitcastSrc(MI, CastTy, 2);
    bitcastSrc(MI, CastTy, 3);
    bitcastDst(MI, CastTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_AND:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_XOR: {
    Observer.changingInstr(MI);
    bitcastSrc(MI, CastTy, 1);
    bitcastSrc(MI, CastTy, 2);
    bitcastDst(MI, CastTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_EXTRACT_VECTOR_ELT:
    return bitcastExtractVectorElt(MI, TypeIdx, CastTy);
  case TargetOpcode::G_INSERT_VECTOR_ELT:
    return bitcastInsertVectorElt(MI, TypeIdx, CastTy);
  case TargetOpcode::G_CONCAT_VECTORS:
    return bitcastConcatVector(MI, TypeIdx, CastTy);
  default:
    return UnableToLegalize;
  }
}

// Legalize an instruction by changing the opcode in place.
void LegalizerHelper::changeOpcode(MachineInstr &MI, unsigned NewOpcode) {
    Observer.changingInstr(MI);
    MI.setDesc(MIRBuilder.getTII().get(NewOpcode));
    Observer.changedInstr(MI);
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lower(MachineInstr &MI, unsigned TypeIdx, LLT LowerHintTy) {
  using namespace TargetOpcode;

  switch(MI.getOpcode()) {
  default:
    return UnableToLegalize;
  case TargetOpcode::G_FCONSTANT:
    return lowerFConstant(MI);
  case TargetOpcode::G_BITCAST:
    return lowerBitcast(MI);
  case TargetOpcode::G_SREM:
  case TargetOpcode::G_UREM: {
    LLT Ty = MRI.getType(MI.getOperand(0).getReg());
    auto Quot =
        MIRBuilder.buildInstr(MI.getOpcode() == G_SREM ? G_SDIV : G_UDIV, {Ty},
                              {MI.getOperand(1), MI.getOperand(2)});

    auto Prod = MIRBuilder.buildMul(Ty, Quot, MI.getOperand(2));
    MIRBuilder.buildSub(MI.getOperand(0), MI.getOperand(1), Prod);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_SADDO:
  case TargetOpcode::G_SSUBO:
    return lowerSADDO_SSUBO(MI);
  case TargetOpcode::G_UMULH:
  case TargetOpcode::G_SMULH:
    return lowerSMULH_UMULH(MI);
  case TargetOpcode::G_SMULO:
  case TargetOpcode::G_UMULO: {
    // Generate G_UMULH/G_SMULH to check for overflow and a normal G_MUL for the
    // result.
    auto [Res, Overflow, LHS, RHS] = MI.getFirst4Regs();
    LLT Ty = MRI.getType(Res);

    unsigned Opcode = MI.getOpcode() == TargetOpcode::G_SMULO
                          ? TargetOpcode::G_SMULH
                          : TargetOpcode::G_UMULH;

    Observer.changingInstr(MI);
    const auto &TII = MIRBuilder.getTII();
    MI.setDesc(TII.get(TargetOpcode::G_MUL));
    MI.removeOperand(1);
    Observer.changedInstr(MI);

    auto HiPart = MIRBuilder.buildInstr(Opcode, {Ty}, {LHS, RHS});
    auto Zero = MIRBuilder.buildConstant(Ty, 0);

    // Move insert point forward so we can use the Res register if needed.
    MIRBuilder.setInsertPt(MIRBuilder.getMBB(), ++MIRBuilder.getInsertPt());

    // For *signed* multiply, overflow is detected by checking:
    // (hi != (lo >> bitwidth-1))
    if (Opcode == TargetOpcode::G_SMULH) {
      auto ShiftAmt = MIRBuilder.buildConstant(Ty, Ty.getSizeInBits() - 1);
      auto Shifted = MIRBuilder.buildAShr(Ty, Res, ShiftAmt);
      MIRBuilder.buildICmp(CmpInst::ICMP_NE, Overflow, HiPart, Shifted);
    } else {
      MIRBuilder.buildICmp(CmpInst::ICMP_NE, Overflow, HiPart, Zero);
    }
    return Legalized;
  }
  case TargetOpcode::G_FNEG: {
    auto [Res, SubByReg] = MI.getFirst2Regs();
    LLT Ty = MRI.getType(Res);

    // TODO: Handle vector types once we are able to
    // represent them.
    if (Ty.isVector())
      return UnableToLegalize;
    auto SignMask =
        MIRBuilder.buildConstant(Ty, APInt::getSignMask(Ty.getSizeInBits()));
    MIRBuilder.buildXor(Res, SubByReg, SignMask);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_FSUB:
  case TargetOpcode::G_STRICT_FSUB: {
    auto [Res, LHS, RHS] = MI.getFirst3Regs();
    LLT Ty = MRI.getType(Res);

    // Lower (G_FSUB LHS, RHS) to (G_FADD LHS, (G_FNEG RHS)).
    auto Neg = MIRBuilder.buildFNeg(Ty, RHS);

    if (MI.getOpcode() == TargetOpcode::G_STRICT_FSUB)
      MIRBuilder.buildStrictFAdd(Res, LHS, Neg, MI.getFlags());
    else
      MIRBuilder.buildFAdd(Res, LHS, Neg, MI.getFlags());

    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_FMAD:
    return lowerFMad(MI);
  case TargetOpcode::G_FFLOOR:
    return lowerFFloor(MI);
  case TargetOpcode::G_INTRINSIC_ROUND:
    return lowerIntrinsicRound(MI);
  case TargetOpcode::G_FRINT: {
    // Since round even is the assumed rounding mode for unconstrained FP
    // operations, rint and roundeven are the same operation.
    changeOpcode(MI, TargetOpcode::G_INTRINSIC_ROUNDEVEN);
    return Legalized;
  }
  case TargetOpcode::G_ATOMIC_CMPXCHG_WITH_SUCCESS: {
    auto [OldValRes, SuccessRes, Addr, CmpVal, NewVal] = MI.getFirst5Regs();
    Register NewOldValRes = MRI.cloneVirtualRegister(OldValRes);
    MIRBuilder.buildAtomicCmpXchg(NewOldValRes, Addr, CmpVal, NewVal,
                                  **MI.memoperands_begin());
    MIRBuilder.buildICmp(CmpInst::ICMP_EQ, SuccessRes, NewOldValRes, CmpVal);
    MIRBuilder.buildCopy(OldValRes, NewOldValRes);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_LOAD:
  case TargetOpcode::G_SEXTLOAD:
  case TargetOpcode::G_ZEXTLOAD:
    return lowerLoad(cast<GAnyLoad>(MI));
  case TargetOpcode::G_STORE:
    return lowerStore(cast<GStore>(MI));
  case TargetOpcode::G_CTLZ_ZERO_UNDEF:
  case TargetOpcode::G_CTTZ_ZERO_UNDEF:
  case TargetOpcode::G_CTLZ:
  case TargetOpcode::G_CTTZ:
  case TargetOpcode::G_CTPOP:
    return lowerBitCount(MI);
  case G_UADDO: {
    auto [Res, CarryOut, LHS, RHS] = MI.getFirst4Regs();

    Register NewRes = MRI.cloneVirtualRegister(Res);

    MIRBuilder.buildAdd(NewRes, LHS, RHS);
    MIRBuilder.buildICmp(CmpInst::ICMP_ULT, CarryOut, NewRes, RHS);

    MIRBuilder.buildCopy(Res, NewRes);

    MI.eraseFromParent();
    return Legalized;
  }
  case G_UADDE: {
    auto [Res, CarryOut, LHS, RHS, CarryIn] = MI.getFirst5Regs();
    const LLT CondTy = MRI.getType(CarryOut);
    const LLT Ty = MRI.getType(Res);

    Register NewRes = MRI.cloneVirtualRegister(Res);

    // Initial add of the two operands.
    auto TmpRes = MIRBuilder.buildAdd(Ty, LHS, RHS);

    // Initial check for carry.
    auto Carry = MIRBuilder.buildICmp(CmpInst::ICMP_ULT, CondTy, TmpRes, LHS);

    // Add the sum and the carry.
    auto ZExtCarryIn = MIRBuilder.buildZExt(Ty, CarryIn);
    MIRBuilder.buildAdd(NewRes, TmpRes, ZExtCarryIn);

    // Second check for carry. We can only carry if the initial sum is all 1s
    // and the carry is set, resulting in a new sum of 0.
    auto Zero = MIRBuilder.buildConstant(Ty, 0);
    auto ResEqZero =
        MIRBuilder.buildICmp(CmpInst::ICMP_EQ, CondTy, NewRes, Zero);
    auto Carry2 = MIRBuilder.buildAnd(CondTy, ResEqZero, CarryIn);
    MIRBuilder.buildOr(CarryOut, Carry, Carry2);

    MIRBuilder.buildCopy(Res, NewRes);

    MI.eraseFromParent();
    return Legalized;
  }
  case G_USUBO: {
    auto [Res, BorrowOut, LHS, RHS] = MI.getFirst4Regs();

    MIRBuilder.buildSub(Res, LHS, RHS);
    MIRBuilder.buildICmp(CmpInst::ICMP_ULT, BorrowOut, LHS, RHS);

    MI.eraseFromParent();
    return Legalized;
  }
  case G_USUBE: {
    auto [Res, BorrowOut, LHS, RHS, BorrowIn] = MI.getFirst5Regs();
    const LLT CondTy = MRI.getType(BorrowOut);
    const LLT Ty = MRI.getType(Res);

    // Initial subtract of the two operands.
    auto TmpRes = MIRBuilder.buildSub(Ty, LHS, RHS);

    // Initial check for borrow.
    auto Borrow = MIRBuilder.buildICmp(CmpInst::ICMP_UGT, CondTy, TmpRes, LHS);

    // Subtract the borrow from the first subtract.
    auto ZExtBorrowIn = MIRBuilder.buildZExt(Ty, BorrowIn);
    MIRBuilder.buildSub(Res, TmpRes, ZExtBorrowIn);

    // Second check for borrow. We can only borrow if the initial difference is
    // 0 and the borrow is set, resulting in a new difference of all 1s.
    auto Zero = MIRBuilder.buildConstant(Ty, 0);
    auto TmpResEqZero =
        MIRBuilder.buildICmp(CmpInst::ICMP_EQ, CondTy, TmpRes, Zero);
    auto Borrow2 = MIRBuilder.buildAnd(CondTy, TmpResEqZero, BorrowIn);
    MIRBuilder.buildOr(BorrowOut, Borrow, Borrow2);

    MI.eraseFromParent();
    return Legalized;
  }
  case G_UITOFP:
    return lowerUITOFP(MI);
  case G_SITOFP:
    return lowerSITOFP(MI);
  case G_FPTOUI:
    return lowerFPTOUI(MI);
  case G_FPTOSI:
    return lowerFPTOSI(MI);
  case G_FPTRUNC:
    return lowerFPTRUNC(MI);
  case G_FPOWI:
    return lowerFPOWI(MI);
  case G_SMIN:
  case G_SMAX:
  case G_UMIN:
  case G_UMAX:
    return lowerMinMax(MI);
  case G_SCMP:
  case G_UCMP:
    return lowerThreewayCompare(MI);
  case G_FCOPYSIGN:
    return lowerFCopySign(MI);
  case G_FMINNUM:
  case G_FMAXNUM:
    return lowerFMinNumMaxNum(MI);
  case G_MERGE_VALUES:
    return lowerMergeValues(MI);
  case G_UNMERGE_VALUES:
    return lowerUnmergeValues(MI);
  case TargetOpcode::G_SEXT_INREG: {
    assert(MI.getOperand(2).isImm() && "Expected immediate");
    int64_t SizeInBits = MI.getOperand(2).getImm();

    auto [DstReg, SrcReg] = MI.getFirst2Regs();
    LLT DstTy = MRI.getType(DstReg);
    Register TmpRes = MRI.createGenericVirtualRegister(DstTy);

    auto MIBSz = MIRBuilder.buildConstant(DstTy, DstTy.getScalarSizeInBits() - SizeInBits);
    MIRBuilder.buildShl(TmpRes, SrcReg, MIBSz->getOperand(0));
    MIRBuilder.buildAShr(DstReg, TmpRes, MIBSz->getOperand(0));
    MI.eraseFromParent();
    return Legalized;
  }
  case G_EXTRACT_VECTOR_ELT:
  case G_INSERT_VECTOR_ELT:
    return lowerExtractInsertVectorElt(MI);
  case G_SHUFFLE_VECTOR:
    return lowerShuffleVector(MI);
  case G_VECTOR_COMPRESS:
    return lowerVECTOR_COMPRESS(MI);
  case G_DYN_STACKALLOC:
    return lowerDynStackAlloc(MI);
  case G_STACKSAVE:
    return lowerStackSave(MI);
  case G_STACKRESTORE:
    return lowerStackRestore(MI);
  case G_EXTRACT:
    return lowerExtract(MI);
  case G_INSERT:
    return lowerInsert(MI);
  case G_BSWAP:
    return lowerBswap(MI);
  case G_BITREVERSE:
    return lowerBitreverse(MI);
  case G_READ_REGISTER:
  case G_WRITE_REGISTER:
    return lowerReadWriteRegister(MI);
  case G_UADDSAT:
  case G_USUBSAT: {
    // Try to make a reasonable guess about which lowering strategy to use. The
    // target can override this with custom lowering and calling the
    // implementation functions.
    LLT Ty = MRI.getType(MI.getOperand(0).getReg());
    if (LI.isLegalOrCustom({G_UMIN, Ty}))
      return lowerAddSubSatToMinMax(MI);
    return lowerAddSubSatToAddoSubo(MI);
  }
  case G_SADDSAT:
  case G_SSUBSAT: {
    LLT Ty = MRI.getType(MI.getOperand(0).getReg());

    // FIXME: It would probably make more sense to see if G_SADDO is preferred,
    // since it's a shorter expansion. However, we would need to figure out the
    // preferred boolean type for the carry out for the query.
    if (LI.isLegalOrCustom({G_SMIN, Ty}) && LI.isLegalOrCustom({G_SMAX, Ty}))
      return lowerAddSubSatToMinMax(MI);
    return lowerAddSubSatToAddoSubo(MI);
  }
  case G_SSHLSAT:
  case G_USHLSAT:
    return lowerShlSat(MI);
  case G_ABS:
    return lowerAbsToAddXor(MI);
  case G_SELECT:
    return lowerSelect(MI);
  case G_IS_FPCLASS:
    return lowerISFPCLASS(MI);
  case G_SDIVREM:
  case G_UDIVREM:
    return lowerDIVREM(MI);
  case G_FSHL:
  case G_FSHR:
    return lowerFunnelShift(MI);
  case G_ROTL:
  case G_ROTR:
    return lowerRotate(MI);
  case G_MEMSET:
  case G_MEMCPY:
  case G_MEMMOVE:
    return lowerMemCpyFamily(MI);
  case G_MEMCPY_INLINE:
    return lowerMemcpyInline(MI);
  case G_ZEXT:
  case G_SEXT:
  case G_ANYEXT:
    return lowerEXT(MI);
  case G_TRUNC:
    return lowerTRUNC(MI);
  GISEL_VECREDUCE_CASES_NONSEQ
    return lowerVectorReduction(MI);
  case G_VAARG:
    return lowerVAArg(MI);
  }
}

Align LegalizerHelper::getStackTemporaryAlignment(LLT Ty,
                                                  Align MinAlign) const {
  // FIXME: We're missing a way to go back from LLT to llvm::Type to query the
  // datalayout for the preferred alignment. Also there should be a target hook
  // for this to allow targets to reduce the alignment and ignore the
  // datalayout. e.g. AMDGPU should always use a 4-byte alignment, regardless of
  // the type.
  return std::max(Align(PowerOf2Ceil(Ty.getSizeInBytes())), MinAlign);
}

MachineInstrBuilder
LegalizerHelper::createStackTemporary(TypeSize Bytes, Align Alignment,
                                      MachinePointerInfo &PtrInfo) {
  MachineFunction &MF = MIRBuilder.getMF();
  const DataLayout &DL = MIRBuilder.getDataLayout();
  int FrameIdx = MF.getFrameInfo().CreateStackObject(Bytes, Alignment, false);

  unsigned AddrSpace = DL.getAllocaAddrSpace();
  LLT FramePtrTy = LLT::pointer(AddrSpace, DL.getPointerSizeInBits(AddrSpace));

  PtrInfo = MachinePointerInfo::getFixedStack(MF, FrameIdx);
  return MIRBuilder.buildFrameIndex(FramePtrTy, FrameIdx);
}

static Register clampVectorIndex(MachineIRBuilder &B, Register IdxReg,
                                 LLT VecTy) {
  LLT IdxTy = B.getMRI()->getType(IdxReg);
  unsigned NElts = VecTy.getNumElements();

  int64_t IdxVal;
  if (mi_match(IdxReg, *B.getMRI(), m_ICst(IdxVal))) {
    if (IdxVal < VecTy.getNumElements())
      return IdxReg;
    // If a constant index would be out of bounds, clamp it as well.
  }

  if (isPowerOf2_32(NElts)) {
    APInt Imm = APInt::getLowBitsSet(IdxTy.getSizeInBits(), Log2_32(NElts));
    return B.buildAnd(IdxTy, IdxReg, B.buildConstant(IdxTy, Imm)).getReg(0);
  }

  return B.buildUMin(IdxTy, IdxReg, B.buildConstant(IdxTy, NElts - 1))
      .getReg(0);
}

Register LegalizerHelper::getVectorElementPointer(Register VecPtr, LLT VecTy,
                                                  Register Index) {
  LLT EltTy = VecTy.getElementType();

  // Calculate the element offset and add it to the pointer.
  unsigned EltSize = EltTy.getSizeInBits() / 8; // FIXME: should be ABI size.
  assert(EltSize * 8 == EltTy.getSizeInBits() &&
         "Converting bits to bytes lost precision");

  Index = clampVectorIndex(MIRBuilder, Index, VecTy);

  // Convert index to the correct size for the address space.
  const DataLayout &DL = MIRBuilder.getDataLayout();
  unsigned AS = MRI.getType(VecPtr).getAddressSpace();
  unsigned IndexSizeInBits = DL.getIndexSize(AS) * 8;
  LLT IdxTy = MRI.getType(Index).changeElementSize(IndexSizeInBits);
  if (IdxTy != MRI.getType(Index))
    Index = MIRBuilder.buildSExtOrTrunc(IdxTy, Index).getReg(0);

  auto Mul = MIRBuilder.buildMul(IdxTy, Index,
                                 MIRBuilder.buildConstant(IdxTy, EltSize));

  LLT PtrTy = MRI.getType(VecPtr);
  return MIRBuilder.buildPtrAdd(PtrTy, VecPtr, Mul).getReg(0);
}

#ifndef NDEBUG
/// Check that all vector operands have same number of elements. Other operands
/// should be listed in NonVecOp.
static bool hasSameNumEltsOnAllVectorOperands(
    GenericMachineInstr &MI, MachineRegisterInfo &MRI,
    std::initializer_list<unsigned> NonVecOpIndices) {
  if (MI.getNumMemOperands() != 0)
    return false;

  LLT VecTy = MRI.getType(MI.getReg(0));
  if (!VecTy.isVector())
    return false;
  unsigned NumElts = VecTy.getNumElements();

  for (unsigned OpIdx = 1; OpIdx < MI.getNumOperands(); ++OpIdx) {
    MachineOperand &Op = MI.getOperand(OpIdx);
    if (!Op.isReg()) {
      if (!is_contained(NonVecOpIndices, OpIdx))
        return false;
      continue;
    }

    LLT Ty = MRI.getType(Op.getReg());
    if (!Ty.isVector()) {
      if (!is_contained(NonVecOpIndices, OpIdx))
        return false;
      continue;
    }

    if (Ty.getNumElements() != NumElts)
      return false;
  }

  return true;
}
#endif

/// Fill \p DstOps with DstOps that have same number of elements combined as
/// the Ty. These DstOps have either scalar type when \p NumElts = 1 or are
/// vectors with \p NumElts elements. When Ty.getNumElements() is not multiple
/// of \p NumElts last DstOp (leftover) has fewer then \p NumElts elements.
static void makeDstOps(SmallVectorImpl<DstOp> &DstOps, LLT Ty,
                       unsigned NumElts) {
  LLT LeftoverTy;
  assert(Ty.isVector() && "Expected vector type");
  LLT EltTy = Ty.getElementType();
  LLT NarrowTy = (NumElts == 1) ? EltTy : LLT::fixed_vector(NumElts, EltTy);
  int NumParts, NumLeftover;
  std::tie(NumParts, NumLeftover) =
      getNarrowTypeBreakDown(Ty, NarrowTy, LeftoverTy);

  assert(NumParts > 0 && "Error in getNarrowTypeBreakDown");
  for (int i = 0; i < NumParts; ++i) {
    DstOps.push_back(NarrowTy);
  }

  if (LeftoverTy.isValid()) {
    assert(NumLeftover == 1 && "expected exactly one leftover");
    DstOps.push_back(LeftoverTy);
  }
}

/// Operand \p Op is used on \p N sub-instructions. Fill \p Ops with \p N SrcOps
/// made from \p Op depending on operand type.
static void broadcastSrcOp(SmallVectorImpl<SrcOp> &Ops, unsigned N,
                           MachineOperand &Op) {
  for (unsigned i = 0; i < N; ++i) {
    if (Op.isReg())
      Ops.push_back(Op.getReg());
    else if (Op.isImm())
      Ops.push_back(Op.getImm());
    else if (Op.isPredicate())
      Ops.push_back(static_cast<CmpInst::Predicate>(Op.getPredicate()));
    else
      llvm_unreachable("Unsupported type");
  }
}

// Handle splitting vector operations which need to have the same number of
// elements in each type index, but each type index may have a different element
// type.
//
// e.g.  <4 x s64> = G_SHL <4 x s64>, <4 x s32> ->
//       <2 x s64> = G_SHL <2 x s64>, <2 x s32>
//       <2 x s64> = G_SHL <2 x s64>, <2 x s32>
//
// Also handles some irregular breakdown cases, e.g.
// e.g.  <3 x s64> = G_SHL <3 x s64>, <3 x s32> ->
//       <2 x s64> = G_SHL <2 x s64>, <2 x s32>
//             s64 = G_SHL s64, s32
LegalizerHelper::LegalizeResult
LegalizerHelper::fewerElementsVectorMultiEltType(
    GenericMachineInstr &MI, unsigned NumElts,
    std::initializer_list<unsigned> NonVecOpIndices) {
  assert(hasSameNumEltsOnAllVectorOperands(MI, MRI, NonVecOpIndices) &&
         "Non-compatible opcode or not specified non-vector operands");
  unsigned OrigNumElts = MRI.getType(MI.getReg(0)).getNumElements();

  unsigned NumInputs = MI.getNumOperands() - MI.getNumDefs();
  unsigned NumDefs = MI.getNumDefs();

  // Create DstOps (sub-vectors with NumElts elts + Leftover) for each output.
  // Build instructions with DstOps to use instruction found by CSE directly.
  // CSE copies found instruction into given vreg when building with vreg dest.
  SmallVector<SmallVector<DstOp, 8>, 2> OutputOpsPieces(NumDefs);
  // Output registers will be taken from created instructions.
  SmallVector<SmallVector<Register, 8>, 2> OutputRegs(NumDefs);
  for (unsigned i = 0; i < NumDefs; ++i) {
    makeDstOps(OutputOpsPieces[i], MRI.getType(MI.getReg(i)), NumElts);
  }

  // Split vector input operands into sub-vectors with NumElts elts + Leftover.
  // Operands listed in NonVecOpIndices will be used as is without splitting;
  // examples: compare predicate in icmp and fcmp (op 1), vector select with i1
  // scalar condition (op 1), immediate in sext_inreg (op 2).
  SmallVector<SmallVector<SrcOp, 8>, 3> InputOpsPieces(NumInputs);
  for (unsigned UseIdx = NumDefs, UseNo = 0; UseIdx < MI.getNumOperands();
       ++UseIdx, ++UseNo) {
    if (is_contained(NonVecOpIndices, UseIdx)) {
      broadcastSrcOp(InputOpsPieces[UseNo], OutputOpsPieces[0].size(),
                     MI.getOperand(UseIdx));
    } else {
      SmallVector<Register, 8> SplitPieces;
      extractVectorParts(MI.getReg(UseIdx), NumElts, SplitPieces, MIRBuilder,
                         MRI);
      for (auto Reg : SplitPieces)
        InputOpsPieces[UseNo].push_back(Reg);
    }
  }

  unsigned NumLeftovers = OrigNumElts % NumElts ? 1 : 0;

  // Take i-th piece of each input operand split and build sub-vector/scalar
  // instruction. Set i-th DstOp(s) from OutputOpsPieces as destination(s).
  for (unsigned i = 0; i < OrigNumElts / NumElts + NumLeftovers; ++i) {
    SmallVector<DstOp, 2> Defs;
    for (unsigned DstNo = 0; DstNo < NumDefs; ++DstNo)
      Defs.push_back(OutputOpsPieces[DstNo][i]);

    SmallVector<SrcOp, 3> Uses;
    for (unsigned InputNo = 0; InputNo < NumInputs; ++InputNo)
      Uses.push_back(InputOpsPieces[InputNo][i]);

    auto I = MIRBuilder.buildInstr(MI.getOpcode(), Defs, Uses, MI.getFlags());
    for (unsigned DstNo = 0; DstNo < NumDefs; ++DstNo)
      OutputRegs[DstNo].push_back(I.getReg(DstNo));
  }

  // Merge small outputs into MI's output for each def operand.
  if (NumLeftovers) {
    for (unsigned i = 0; i < NumDefs; ++i)
      mergeMixedSubvectors(MI.getReg(i), OutputRegs[i]);
  } else {
    for (unsigned i = 0; i < NumDefs; ++i)
      MIRBuilder.buildMergeLikeInstr(MI.getReg(i), OutputRegs[i]);
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::fewerElementsVectorPhi(GenericMachineInstr &MI,
                                        unsigned NumElts) {
  unsigned OrigNumElts = MRI.getType(MI.getReg(0)).getNumElements();

  unsigned NumInputs = MI.getNumOperands() - MI.getNumDefs();
  unsigned NumDefs = MI.getNumDefs();

  SmallVector<DstOp, 8> OutputOpsPieces;
  SmallVector<Register, 8> OutputRegs;
  makeDstOps(OutputOpsPieces, MRI.getType(MI.getReg(0)), NumElts);

  // Instructions that perform register split will be inserted in basic block
  // where register is defined (basic block is in the next operand).
  SmallVector<SmallVector<Register, 8>, 3> InputOpsPieces(NumInputs / 2);
  for (unsigned UseIdx = NumDefs, UseNo = 0; UseIdx < MI.getNumOperands();
       UseIdx += 2, ++UseNo) {
    MachineBasicBlock &OpMBB = *MI.getOperand(UseIdx + 1).getMBB();
    MIRBuilder.setInsertPt(OpMBB, OpMBB.getFirstTerminatorForward());
    extractVectorParts(MI.getReg(UseIdx), NumElts, InputOpsPieces[UseNo],
                       MIRBuilder, MRI);
  }

  // Build PHIs with fewer elements.
  unsigned NumLeftovers = OrigNumElts % NumElts ? 1 : 0;
  MIRBuilder.setInsertPt(*MI.getParent(), MI);
  for (unsigned i = 0; i < OrigNumElts / NumElts + NumLeftovers; ++i) {
    auto Phi = MIRBuilder.buildInstr(TargetOpcode::G_PHI);
    Phi.addDef(
        MRI.createGenericVirtualRegister(OutputOpsPieces[i].getLLTTy(MRI)));
    OutputRegs.push_back(Phi.getReg(0));

    for (unsigned j = 0; j < NumInputs / 2; ++j) {
      Phi.addUse(InputOpsPieces[j][i]);
      Phi.add(MI.getOperand(1 + j * 2 + 1));
    }
  }

  // Set the insert point after the existing PHIs
  MachineBasicBlock &MBB = *MI.getParent();
  MIRBuilder.setInsertPt(MBB, MBB.getFirstNonPHI());

  // Merge small outputs into MI's def.
  if (NumLeftovers) {
    mergeMixedSubvectors(MI.getReg(0), OutputRegs);
  } else {
    MIRBuilder.buildMergeLikeInstr(MI.getReg(0), OutputRegs);
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::fewerElementsVectorUnmergeValues(MachineInstr &MI,
                                                  unsigned TypeIdx,
                                                  LLT NarrowTy) {
  const int NumDst = MI.getNumOperands() - 1;
  const Register SrcReg = MI.getOperand(NumDst).getReg();
  LLT DstTy = MRI.getType(MI.getOperand(0).getReg());
  LLT SrcTy = MRI.getType(SrcReg);

  if (TypeIdx != 1 || NarrowTy == DstTy)
    return UnableToLegalize;

  // Requires compatible types. Otherwise SrcReg should have been defined by
  // merge-like instruction that would get artifact combined. Most likely
  // instruction that defines SrcReg has to perform more/fewer elements
  // legalization compatible with NarrowTy.
  assert(SrcTy.isVector() && NarrowTy.isVector() && "Expected vector types");
  assert((SrcTy.getScalarType() == NarrowTy.getScalarType()) && "bad type");

  if ((SrcTy.getSizeInBits() % NarrowTy.getSizeInBits() != 0) ||
      (NarrowTy.getSizeInBits() % DstTy.getSizeInBits() != 0))
    return UnableToLegalize;

  // This is most likely DstTy (smaller then register size) packed in SrcTy
  // (larger then register size) and since unmerge was not combined it will be
  // lowered to bit sequence extracts from register. Unpack SrcTy to NarrowTy
  // (register size) pieces first. Then unpack each of NarrowTy pieces to DstTy.

  // %1:_(DstTy), %2, %3, %4 = G_UNMERGE_VALUES %0:_(SrcTy)
  //
  // %5:_(NarrowTy), %6 = G_UNMERGE_VALUES %0:_(SrcTy) - reg sequence
  // %1:_(DstTy), %2 = G_UNMERGE_VALUES %5:_(NarrowTy) - sequence of bits in reg
  // %3:_(DstTy), %4 = G_UNMERGE_VALUES %6:_(NarrowTy)
  auto Unmerge = MIRBuilder.buildUnmerge(NarrowTy, SrcReg);
  const int NumUnmerge = Unmerge->getNumOperands() - 1;
  const int PartsPerUnmerge = NumDst / NumUnmerge;

  for (int I = 0; I != NumUnmerge; ++I) {
    auto MIB = MIRBuilder.buildInstr(TargetOpcode::G_UNMERGE_VALUES);

    for (int J = 0; J != PartsPerUnmerge; ++J)
      MIB.addDef(MI.getOperand(I * PartsPerUnmerge + J).getReg());
    MIB.addUse(Unmerge.getReg(I));
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::fewerElementsVectorMerge(MachineInstr &MI, unsigned TypeIdx,
                                          LLT NarrowTy) {
  auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();
  // Requires compatible types. Otherwise user of DstReg did not perform unmerge
  // that should have been artifact combined. Most likely instruction that uses
  // DstReg has to do more/fewer elements legalization compatible with NarrowTy.
  assert(DstTy.isVector() && NarrowTy.isVector() && "Expected vector types");
  assert((DstTy.getScalarType() == NarrowTy.getScalarType()) && "bad type");
  if (NarrowTy == SrcTy)
    return UnableToLegalize;

  // This attempts to lower part of LCMTy merge/unmerge sequence. Intended use
  // is for old mir tests. Since the changes to more/fewer elements it should no
  // longer be possible to generate MIR like this when starting from llvm-ir
  // because LCMTy approach was replaced with merge/unmerge to vector elements.
  if (TypeIdx == 1) {
    assert(SrcTy.isVector() && "Expected vector types");
    assert((SrcTy.getScalarType() == NarrowTy.getScalarType()) && "bad type");
    if ((DstTy.getSizeInBits() % NarrowTy.getSizeInBits() != 0) ||
        (NarrowTy.getNumElements() >= SrcTy.getNumElements()))
      return UnableToLegalize;
    // %2:_(DstTy) = G_CONCAT_VECTORS %0:_(SrcTy), %1:_(SrcTy)
    //
    // %3:_(EltTy), %4, %5 = G_UNMERGE_VALUES %0:_(SrcTy)
    // %6:_(EltTy), %7, %8 = G_UNMERGE_VALUES %1:_(SrcTy)
    // %9:_(NarrowTy) = G_BUILD_VECTOR %3:_(EltTy), %4
    // %10:_(NarrowTy) = G_BUILD_VECTOR %5:_(EltTy), %6
    // %11:_(NarrowTy) = G_BUILD_VECTOR %7:_(EltTy), %8
    // %2:_(DstTy) = G_CONCAT_VECTORS %9:_(NarrowTy), %10, %11

    SmallVector<Register, 8> Elts;
    LLT EltTy = MRI.getType(MI.getOperand(1).getReg()).getScalarType();
    for (unsigned i = 1; i < MI.getNumOperands(); ++i) {
      auto Unmerge = MIRBuilder.buildUnmerge(EltTy, MI.getOperand(i).getReg());
      for (unsigned j = 0; j < Unmerge->getNumDefs(); ++j)
        Elts.push_back(Unmerge.getReg(j));
    }

    SmallVector<Register, 8> NarrowTyElts;
    unsigned NumNarrowTyElts = NarrowTy.getNumElements();
    unsigned NumNarrowTyPieces = DstTy.getNumElements() / NumNarrowTyElts;
    for (unsigned i = 0, Offset = 0; i < NumNarrowTyPieces;
         ++i, Offset += NumNarrowTyElts) {
      ArrayRef<Register> Pieces(&Elts[Offset], NumNarrowTyElts);
      NarrowTyElts.push_back(
          MIRBuilder.buildMergeLikeInstr(NarrowTy, Pieces).getReg(0));
    }

    MIRBuilder.buildMergeLikeInstr(DstReg, NarrowTyElts);
    MI.eraseFromParent();
    return Legalized;
  }

  assert(TypeIdx == 0 && "Bad type index");
  if ((NarrowTy.getSizeInBits() % SrcTy.getSizeInBits() != 0) ||
      (DstTy.getSizeInBits() % NarrowTy.getSizeInBits() != 0))
    return UnableToLegalize;

  // This is most likely SrcTy (smaller then register size) packed in DstTy
  // (larger then register size) and since merge was not combined it will be
  // lowered to bit sequence packing into register. Merge SrcTy to NarrowTy
  // (register size) pieces first. Then merge each of NarrowTy pieces to DstTy.

  // %0:_(DstTy) = G_MERGE_VALUES %1:_(SrcTy), %2, %3, %4
  //
  // %5:_(NarrowTy) = G_MERGE_VALUES %1:_(SrcTy), %2 - sequence of bits in reg
  // %6:_(NarrowTy) = G_MERGE_VALUES %3:_(SrcTy), %4
  // %0:_(DstTy)  = G_MERGE_VALUES %5:_(NarrowTy), %6 - reg sequence
  SmallVector<Register, 8> NarrowTyElts;
  unsigned NumParts = DstTy.getNumElements() / NarrowTy.getNumElements();
  unsigned NumSrcElts = SrcTy.isVector() ? SrcTy.getNumElements() : 1;
  unsigned NumElts = NarrowTy.getNumElements() / NumSrcElts;
  for (unsigned i = 0; i < NumParts; ++i) {
    SmallVector<Register, 8> Sources;
    for (unsigned j = 0; j < NumElts; ++j)
      Sources.push_back(MI.getOperand(1 + i * NumElts + j).getReg());
    NarrowTyElts.push_back(
        MIRBuilder.buildMergeLikeInstr(NarrowTy, Sources).getReg(0));
  }

  MIRBuilder.buildMergeLikeInstr(DstReg, NarrowTyElts);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::fewerElementsVectorExtractInsertVectorElt(MachineInstr &MI,
                                                           unsigned TypeIdx,
                                                           LLT NarrowVecTy) {
  auto [DstReg, SrcVec] = MI.getFirst2Regs();
  Register InsertVal;
  bool IsInsert = MI.getOpcode() == TargetOpcode::G_INSERT_VECTOR_ELT;

  assert((IsInsert ? TypeIdx == 0 : TypeIdx == 1) && "not a vector type index");
  if (IsInsert)
    InsertVal = MI.getOperand(2).getReg();

  Register Idx = MI.getOperand(MI.getNumOperands() - 1).getReg();

  // TODO: Handle total scalarization case.
  if (!NarrowVecTy.isVector())
    return UnableToLegalize;

  LLT VecTy = MRI.getType(SrcVec);

  // If the index is a constant, we can really break this down as you would
  // expect, and index into the target size pieces.
  int64_t IdxVal;
  auto MaybeCst = getIConstantVRegValWithLookThrough(Idx, MRI);
  if (MaybeCst) {
    IdxVal = MaybeCst->Value.getSExtValue();
    // Avoid out of bounds indexing the pieces.
    if (IdxVal >= VecTy.getNumElements()) {
      MIRBuilder.buildUndef(DstReg);
      MI.eraseFromParent();
      return Legalized;
    }

    SmallVector<Register, 8> VecParts;
    LLT GCDTy = extractGCDType(VecParts, VecTy, NarrowVecTy, SrcVec);

    // Build a sequence of NarrowTy pieces in VecParts for this operand.
    LLT LCMTy = buildLCMMergePieces(VecTy, NarrowVecTy, GCDTy, VecParts,
                                    TargetOpcode::G_ANYEXT);

    unsigned NewNumElts = NarrowVecTy.getNumElements();

    LLT IdxTy = MRI.getType(Idx);
    int64_t PartIdx = IdxVal / NewNumElts;
    auto NewIdx =
        MIRBuilder.buildConstant(IdxTy, IdxVal - NewNumElts * PartIdx);

    if (IsInsert) {
      LLT PartTy = MRI.getType(VecParts[PartIdx]);

      // Use the adjusted index to insert into one of the subvectors.
      auto InsertPart = MIRBuilder.buildInsertVectorElement(
          PartTy, VecParts[PartIdx], InsertVal, NewIdx);
      VecParts[PartIdx] = InsertPart.getReg(0);

      // Recombine the inserted subvector with the others to reform the result
      // vector.
      buildWidenedRemergeToDst(DstReg, LCMTy, VecParts);
    } else {
      MIRBuilder.buildExtractVectorElement(DstReg, VecParts[PartIdx], NewIdx);
    }

    MI.eraseFromParent();
    return Legalized;
  }

  // With a variable index, we can't perform the operation in a smaller type, so
  // we're forced to expand this.
  //
  // TODO: We could emit a chain of compare/select to figure out which piece to
  // index.
  return lowerExtractInsertVectorElt(MI);
}

LegalizerHelper::LegalizeResult
LegalizerHelper::reduceLoadStoreWidth(GLoadStore &LdStMI, unsigned TypeIdx,
                                      LLT NarrowTy) {
  // FIXME: Don't know how to handle secondary types yet.
  if (TypeIdx != 0)
    return UnableToLegalize;

  // This implementation doesn't work for atomics. Give up instead of doing
  // something invalid.
  if (LdStMI.isAtomic())
    return UnableToLegalize;

  bool IsLoad = isa<GLoad>(LdStMI);
  Register ValReg = LdStMI.getReg(0);
  Register AddrReg = LdStMI.getPointerReg();
  LLT ValTy = MRI.getType(ValReg);

  // FIXME: Do we need a distinct NarrowMemory legalize action?
  if (ValTy.getSizeInBits() != 8 * LdStMI.getMemSize().getValue()) {
    LLVM_DEBUG(dbgs() << "Can't narrow extload/truncstore\n");
    return UnableToLegalize;
  }

  int NumParts = -1;
  int NumLeftover = -1;
  LLT LeftoverTy;
  SmallVector<Register, 8> NarrowRegs, NarrowLeftoverRegs;
  if (IsLoad) {
    std::tie(NumParts, NumLeftover) = getNarrowTypeBreakDown(ValTy, NarrowTy, LeftoverTy);
  } else {
    if (extractParts(ValReg, ValTy, NarrowTy, LeftoverTy, NarrowRegs,
                     NarrowLeftoverRegs, MIRBuilder, MRI)) {
      NumParts = NarrowRegs.size();
      NumLeftover = NarrowLeftoverRegs.size();
    }
  }

  if (NumParts == -1)
    return UnableToLegalize;

  LLT PtrTy = MRI.getType(AddrReg);
  const LLT OffsetTy = LLT::scalar(PtrTy.getSizeInBits());

  unsigned TotalSize = ValTy.getSizeInBits();

  // Split the load/store into PartTy sized pieces starting at Offset. If this
  // is a load, return the new registers in ValRegs. For a store, each elements
  // of ValRegs should be PartTy. Returns the next offset that needs to be
  // handled.
  bool isBigEndian = MIRBuilder.getDataLayout().isBigEndian();
  auto MMO = LdStMI.getMMO();
  auto splitTypePieces = [=](LLT PartTy, SmallVectorImpl<Register> &ValRegs,
                             unsigned NumParts, unsigned Offset) -> unsigned {
    MachineFunction &MF = MIRBuilder.getMF();
    unsigned PartSize = PartTy.getSizeInBits();
    for (unsigned Idx = 0, E = NumParts; Idx != E && Offset < TotalSize;
         ++Idx) {
      unsigned ByteOffset = Offset / 8;
      Register NewAddrReg;

      MIRBuilder.materializePtrAdd(NewAddrReg, AddrReg, OffsetTy, ByteOffset);

      MachineMemOperand *NewMMO =
          MF.getMachineMemOperand(&MMO, ByteOffset, PartTy);

      if (IsLoad) {
        Register Dst = MRI.createGenericVirtualRegister(PartTy);
        ValRegs.push_back(Dst);
        MIRBuilder.buildLoad(Dst, NewAddrReg, *NewMMO);
      } else {
        MIRBuilder.buildStore(ValRegs[Idx], NewAddrReg, *NewMMO);
      }
      Offset = isBigEndian ? Offset - PartSize : Offset + PartSize;
    }

    return Offset;
  };

  unsigned Offset = isBigEndian ? TotalSize - NarrowTy.getSizeInBits() : 0;
  unsigned HandledOffset =
      splitTypePieces(NarrowTy, NarrowRegs, NumParts, Offset);

  // Handle the rest of the register if this isn't an even type breakdown.
  if (LeftoverTy.isValid())
    splitTypePieces(LeftoverTy, NarrowLeftoverRegs, NumLeftover, HandledOffset);

  if (IsLoad) {
    insertParts(ValReg, ValTy, NarrowTy, NarrowRegs,
                LeftoverTy, NarrowLeftoverRegs);
  }

  LdStMI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::fewerElementsVector(MachineInstr &MI, unsigned TypeIdx,
                                     LLT NarrowTy) {
  using namespace TargetOpcode;
  GenericMachineInstr &GMI = cast<GenericMachineInstr>(MI);
  unsigned NumElts = NarrowTy.isVector() ? NarrowTy.getNumElements() : 1;

  switch (MI.getOpcode()) {
  case G_IMPLICIT_DEF:
  case G_TRUNC:
  case G_AND:
  case G_OR:
  case G_XOR:
  case G_ADD:
  case G_SUB:
  case G_MUL:
  case G_PTR_ADD:
  case G_SMULH:
  case G_UMULH:
  case G_FADD:
  case G_FMUL:
  case G_FSUB:
  case G_FNEG:
  case G_FABS:
  case G_FCANONICALIZE:
  case G_FDIV:
  case G_FREM:
  case G_FMA:
  case G_FMAD:
  case G_FPOW:
  case G_FEXP:
  case G_FEXP2:
  case G_FEXP10:
  case G_FLOG:
  case G_FLOG2:
  case G_FLOG10:
  case G_FLDEXP:
  case G_FNEARBYINT:
  case G_FCEIL:
  case G_FFLOOR:
  case G_FRINT:
  case G_INTRINSIC_ROUND:
  case G_INTRINSIC_ROUNDEVEN:
  case G_INTRINSIC_TRUNC:
  case G_FCOS:
  case G_FSIN:
  case G_FTAN:
  case G_FACOS:
  case G_FASIN:
  case G_FATAN:
  case G_FCOSH:
  case G_FSINH:
  case G_FTANH:
  case G_FSQRT:
  case G_BSWAP:
  case G_BITREVERSE:
  case G_SDIV:
  case G_UDIV:
  case G_SREM:
  case G_UREM:
  case G_SDIVREM:
  case G_UDIVREM:
  case G_SMIN:
  case G_SMAX:
  case G_UMIN:
  case G_UMAX:
  case G_ABS:
  case G_FMINNUM:
  case G_FMAXNUM:
  case G_FMINNUM_IEEE:
  case G_FMAXNUM_IEEE:
  case G_FMINIMUM:
  case G_FMAXIMUM:
  case G_FSHL:
  case G_FSHR:
  case G_ROTL:
  case G_ROTR:
  case G_FREEZE:
  case G_SADDSAT:
  case G_SSUBSAT:
  case G_UADDSAT:
  case G_USUBSAT:
  case G_UMULO:
  case G_SMULO:
  case G_SHL:
  case G_LSHR:
  case G_ASHR:
  case G_SSHLSAT:
  case G_USHLSAT:
  case G_CTLZ:
  case G_CTLZ_ZERO_UNDEF:
  case G_CTTZ:
  case G_CTTZ_ZERO_UNDEF:
  case G_CTPOP:
  case G_FCOPYSIGN:
  case G_ZEXT:
  case G_SEXT:
  case G_ANYEXT:
  case G_FPEXT:
  case G_FPTRUNC:
  case G_SITOFP:
  case G_UITOFP:
  case G_FPTOSI:
  case G_FPTOUI:
  case G_INTTOPTR:
  case G_PTRTOINT:
  case G_ADDRSPACE_CAST:
  case G_UADDO:
  case G_USUBO:
  case G_UADDE:
  case G_USUBE:
  case G_SADDO:
  case G_SSUBO:
  case G_SADDE:
  case G_SSUBE:
  case G_STRICT_FADD:
  case G_STRICT_FSUB:
  case G_STRICT_FMUL:
  case G_STRICT_FMA:
  case G_STRICT_FLDEXP:
  case G_FFREXP:
    return fewerElementsVectorMultiEltType(GMI, NumElts);
  case G_ICMP:
  case G_FCMP:
    return fewerElementsVectorMultiEltType(GMI, NumElts, {1 /*cpm predicate*/});
  case G_IS_FPCLASS:
    return fewerElementsVectorMultiEltType(GMI, NumElts, {2, 3 /*mask,fpsem*/});
  case G_SELECT:
    if (MRI.getType(MI.getOperand(1).getReg()).isVector())
      return fewerElementsVectorMultiEltType(GMI, NumElts);
    return fewerElementsVectorMultiEltType(GMI, NumElts, {1 /*scalar cond*/});
  case G_PHI:
    return fewerElementsVectorPhi(GMI, NumElts);
  case G_UNMERGE_VALUES:
    return fewerElementsVectorUnmergeValues(MI, TypeIdx, NarrowTy);
  case G_BUILD_VECTOR:
    assert(TypeIdx == 0 && "not a vector type index");
    return fewerElementsVectorMerge(MI, TypeIdx, NarrowTy);
  case G_CONCAT_VECTORS:
    if (TypeIdx != 1) // TODO: This probably does work as expected already.
      return UnableToLegalize;
    return fewerElementsVectorMerge(MI, TypeIdx, NarrowTy);
  case G_EXTRACT_VECTOR_ELT:
  case G_INSERT_VECTOR_ELT:
    return fewerElementsVectorExtractInsertVectorElt(MI, TypeIdx, NarrowTy);
  case G_LOAD:
  case G_STORE:
    return reduceLoadStoreWidth(cast<GLoadStore>(MI), TypeIdx, NarrowTy);
  case G_SEXT_INREG:
    return fewerElementsVectorMultiEltType(GMI, NumElts, {2 /*imm*/});
  GISEL_VECREDUCE_CASES_NONSEQ
    return fewerElementsVectorReductions(MI, TypeIdx, NarrowTy);
  case TargetOpcode::G_VECREDUCE_SEQ_FADD:
  case TargetOpcode::G_VECREDUCE_SEQ_FMUL:
    return fewerElementsVectorSeqReductions(MI, TypeIdx, NarrowTy);
  case G_SHUFFLE_VECTOR:
    return fewerElementsVectorShuffle(MI, TypeIdx, NarrowTy);
  case G_FPOWI:
    return fewerElementsVectorMultiEltType(GMI, NumElts, {2 /*pow*/});
  case G_BITCAST:
    return fewerElementsBitcast(MI, TypeIdx, NarrowTy);
  case G_INTRINSIC_FPTRUNC_ROUND:
    return fewerElementsVectorMultiEltType(GMI, NumElts, {2});
  default:
    return UnableToLegalize;
  }
}

LegalizerHelper::LegalizeResult
LegalizerHelper::fewerElementsBitcast(MachineInstr &MI, unsigned int TypeIdx,
                                      LLT NarrowTy) {
  assert(MI.getOpcode() == TargetOpcode::G_BITCAST &&
         "Not a bitcast operation");

  if (TypeIdx != 0)
    return UnableToLegalize;

  auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();

  unsigned SrcScalSize = SrcTy.getScalarSizeInBits();
  LLT SrcNarrowTy =
      LLT::fixed_vector(NarrowTy.getSizeInBits() / SrcScalSize, SrcScalSize);

  // Split the Src and Dst Reg into smaller registers
  SmallVector<Register> SrcVRegs, BitcastVRegs;
  if (extractGCDType(SrcVRegs, DstTy, SrcNarrowTy, SrcReg) != SrcNarrowTy)
    return UnableToLegalize;

  // Build new smaller bitcast instructions
  // Not supporting Leftover types for now but will have to
  for (unsigned i = 0; i < SrcVRegs.size(); i++)
    BitcastVRegs.push_back(
        MIRBuilder.buildBitcast(NarrowTy, SrcVRegs[i]).getReg(0));

  MIRBuilder.buildMergeLikeInstr(DstReg, BitcastVRegs);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::fewerElementsVectorShuffle(
    MachineInstr &MI, unsigned int TypeIdx, LLT NarrowTy) {
  assert(MI.getOpcode() == TargetOpcode::G_SHUFFLE_VECTOR);
  if (TypeIdx != 0)
    return UnableToLegalize;

  auto [DstReg, DstTy, Src1Reg, Src1Ty, Src2Reg, Src2Ty] =
      MI.getFirst3RegLLTs();
  ArrayRef<int> Mask = MI.getOperand(3).getShuffleMask();
  // The shuffle should be canonicalized by now.
  if (DstTy != Src1Ty)
    return UnableToLegalize;
  if (DstTy != Src2Ty)
    return UnableToLegalize;

  if (!isPowerOf2_32(DstTy.getNumElements()))
    return UnableToLegalize;

  // We only support splitting a shuffle into 2, so adjust NarrowTy accordingly.
  // Further legalization attempts will be needed to do split further.
  NarrowTy =
      DstTy.changeElementCount(DstTy.getElementCount().divideCoefficientBy(2));
  unsigned NewElts = NarrowTy.getNumElements();

  SmallVector<Register> SplitSrc1Regs, SplitSrc2Regs;
  extractParts(Src1Reg, NarrowTy, 2, SplitSrc1Regs, MIRBuilder, MRI);
  extractParts(Src2Reg, NarrowTy, 2, SplitSrc2Regs, MIRBuilder, MRI);
  Register Inputs[4] = {SplitSrc1Regs[0], SplitSrc1Regs[1], SplitSrc2Regs[0],
                        SplitSrc2Regs[1]};

  Register Hi, Lo;

  // If Lo or Hi uses elements from at most two of the four input vectors, then
  // express it as a vector shuffle of those two inputs.  Otherwise extract the
  // input elements by hand and construct the Lo/Hi output using a BUILD_VECTOR.
  SmallVector<int, 16> Ops;
  for (unsigned High = 0; High < 2; ++High) {
    Register &Output = High ? Hi : Lo;

    // Build a shuffle mask for the output, discovering on the fly which
    // input vectors to use as shuffle operands (recorded in InputUsed).
    // If building a suitable shuffle vector proves too hard, then bail
    // out with useBuildVector set.
    unsigned InputUsed[2] = {-1U, -1U}; // Not yet discovered.
    unsigned FirstMaskIdx = High * NewElts;
    bool UseBuildVector = false;
    for (unsigned MaskOffset = 0; MaskOffset < NewElts; ++MaskOffset) {
      // The mask element.  This indexes into the input.
      int Idx = Mask[FirstMaskIdx + MaskOffset];

      // The input vector this mask element indexes into.
      unsigned Input = (unsigned)Idx / NewElts;

      if (Input >= std::size(Inputs)) {
        // The mask element does not index into any input vector.
        Ops.push_back(-1);
        continue;
      }

      // Turn the index into an offset from the start of the input vector.
      Idx -= Input * NewElts;

      // Find or create a shuffle vector operand to hold this input.
      unsigned OpNo;
      for (OpNo = 0; OpNo < std::size(InputUsed); ++OpNo) {
        if (InputUsed[OpNo] == Input) {
          // This input vector is already an operand.
          break;
        } else if (InputUsed[OpNo] == -1U) {
          // Create a new operand for this input vector.
          InputUsed[OpNo] = Input;
          break;
        }
      }

      if (OpNo >= std::size(InputUsed)) {
        // More than two input vectors used!  Give up on trying to create a
        // shuffle vector.  Insert all elements into a BUILD_VECTOR instead.
        UseBuildVector = true;
        break;
      }

      // Add the mask index for the new shuffle vector.
      Ops.push_back(Idx + OpNo * NewElts);
    }

    if (UseBuildVector) {
      LLT EltTy = NarrowTy.getElementType();
      SmallVector<Register, 16> SVOps;

      // Extract the input elements by hand.
      for (unsigned MaskOffset = 0; MaskOffset < NewElts; ++MaskOffset) {
        // The mask element.  This indexes into the input.
        int Idx = Mask[FirstMaskIdx + MaskOffset];

        // The input vector this mask element indexes into.
        unsigned Input = (unsigned)Idx / NewElts;

        if (Input >= std::size(Inputs)) {
          // The mask element is "undef" or indexes off the end of the input.
          SVOps.push_back(MIRBuilder.buildUndef(EltTy).getReg(0));
          continue;
        }

        // Turn the index into an offset from the start of the input vector.
        Idx -= Input * NewElts;

        // Extract the vector element by hand.
        SVOps.push_back(MIRBuilder
                            .buildExtractVectorElement(
                                EltTy, Inputs[Input],
                                MIRBuilder.buildConstant(LLT::scalar(32), Idx))
                            .getReg(0));
      }

      // Construct the Lo/Hi output using a G_BUILD_VECTOR.
      Output = MIRBuilder.buildBuildVector(NarrowTy, SVOps).getReg(0);
    } else if (InputUsed[0] == -1U) {
      // No input vectors were used! The result is undefined.
      Output = MIRBuilder.buildUndef(NarrowTy).getReg(0);
    } else {
      Register Op0 = Inputs[InputUsed[0]];
      // If only one input was used, use an undefined vector for the other.
      Register Op1 = InputUsed[1] == -1U
                         ? MIRBuilder.buildUndef(NarrowTy).getReg(0)
                         : Inputs[InputUsed[1]];
      // At least one input vector was used. Create a new shuffle vector.
      Output = MIRBuilder.buildShuffleVector(NarrowTy, Op0, Op1, Ops).getReg(0);
    }

    Ops.clear();
  }

  MIRBuilder.buildConcatVectors(DstReg, {Lo, Hi});
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::fewerElementsVectorReductions(
    MachineInstr &MI, unsigned int TypeIdx, LLT NarrowTy) {
  auto &RdxMI = cast<GVecReduce>(MI);

  if (TypeIdx != 1)
    return UnableToLegalize;

  // The semantics of the normal non-sequential reductions allow us to freely
  // re-associate the operation.
  auto [DstReg, DstTy, SrcReg, SrcTy] = RdxMI.getFirst2RegLLTs();

  if (NarrowTy.isVector() &&
      (SrcTy.getNumElements() % NarrowTy.getNumElements() != 0))
    return UnableToLegalize;

  unsigned ScalarOpc = RdxMI.getScalarOpcForReduction();
  SmallVector<Register> SplitSrcs;
  // If NarrowTy is a scalar then we're being asked to scalarize.
  const unsigned NumParts =
      NarrowTy.isVector() ? SrcTy.getNumElements() / NarrowTy.getNumElements()
                          : SrcTy.getNumElements();

  extractParts(SrcReg, NarrowTy, NumParts, SplitSrcs, MIRBuilder, MRI);
  if (NarrowTy.isScalar()) {
    if (DstTy != NarrowTy)
      return UnableToLegalize; // FIXME: handle implicit extensions.

    if (isPowerOf2_32(NumParts)) {
      // Generate a tree of scalar operations to reduce the critical path.
      SmallVector<Register> PartialResults;
      unsigned NumPartsLeft = NumParts;
      while (NumPartsLeft > 1) {
        for (unsigned Idx = 0; Idx < NumPartsLeft - 1; Idx += 2) {
          PartialResults.emplace_back(
              MIRBuilder
                  .buildInstr(ScalarOpc, {NarrowTy},
                              {SplitSrcs[Idx], SplitSrcs[Idx + 1]})
                  .getReg(0));
        }
        SplitSrcs = PartialResults;
        PartialResults.clear();
        NumPartsLeft = SplitSrcs.size();
      }
      assert(SplitSrcs.size() == 1);
      MIRBuilder.buildCopy(DstReg, SplitSrcs[0]);
      MI.eraseFromParent();
      return Legalized;
    }
    // If we can't generate a tree, then just do sequential operations.
    Register Acc = SplitSrcs[0];
    for (unsigned Idx = 1; Idx < NumParts; ++Idx)
      Acc = MIRBuilder.buildInstr(ScalarOpc, {NarrowTy}, {Acc, SplitSrcs[Idx]})
                .getReg(0);
    MIRBuilder.buildCopy(DstReg, Acc);
    MI.eraseFromParent();
    return Legalized;
  }
  SmallVector<Register> PartialReductions;
  for (unsigned Part = 0; Part < NumParts; ++Part) {
    PartialReductions.push_back(
        MIRBuilder.buildInstr(RdxMI.getOpcode(), {DstTy}, {SplitSrcs[Part]})
            .getReg(0));
  }

  // If the types involved are powers of 2, we can generate intermediate vector
  // ops, before generating a final reduction operation.
  if (isPowerOf2_32(SrcTy.getNumElements()) &&
      isPowerOf2_32(NarrowTy.getNumElements())) {
    return tryNarrowPow2Reduction(MI, SrcReg, SrcTy, NarrowTy, ScalarOpc);
  }

  Register Acc = PartialReductions[0];
  for (unsigned Part = 1; Part < NumParts; ++Part) {
    if (Part == NumParts - 1) {
      MIRBuilder.buildInstr(ScalarOpc, {DstReg},
                            {Acc, PartialReductions[Part]});
    } else {
      Acc = MIRBuilder
                .buildInstr(ScalarOpc, {DstTy}, {Acc, PartialReductions[Part]})
                .getReg(0);
    }
  }
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::fewerElementsVectorSeqReductions(MachineInstr &MI,
                                                  unsigned int TypeIdx,
                                                  LLT NarrowTy) {
  auto [DstReg, DstTy, ScalarReg, ScalarTy, SrcReg, SrcTy] =
      MI.getFirst3RegLLTs();
  if (!NarrowTy.isScalar() || TypeIdx != 2 || DstTy != ScalarTy ||
      DstTy != NarrowTy)
    return UnableToLegalize;

  assert((MI.getOpcode() == TargetOpcode::G_VECREDUCE_SEQ_FADD ||
          MI.getOpcode() == TargetOpcode::G_VECREDUCE_SEQ_FMUL) &&
         "Unexpected vecreduce opcode");
  unsigned ScalarOpc = MI.getOpcode() == TargetOpcode::G_VECREDUCE_SEQ_FADD
                           ? TargetOpcode::G_FADD
                           : TargetOpcode::G_FMUL;

  SmallVector<Register> SplitSrcs;
  unsigned NumParts = SrcTy.getNumElements();
  extractParts(SrcReg, NarrowTy, NumParts, SplitSrcs, MIRBuilder, MRI);
  Register Acc = ScalarReg;
  for (unsigned i = 0; i < NumParts; i++)
    Acc = MIRBuilder.buildInstr(ScalarOpc, {NarrowTy}, {Acc, SplitSrcs[i]})
              .getReg(0);

  MIRBuilder.buildCopy(DstReg, Acc);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::tryNarrowPow2Reduction(MachineInstr &MI, Register SrcReg,
                                        LLT SrcTy, LLT NarrowTy,
                                        unsigned ScalarOpc) {
  SmallVector<Register> SplitSrcs;
  // Split the sources into NarrowTy size pieces.
  extractParts(SrcReg, NarrowTy,
               SrcTy.getNumElements() / NarrowTy.getNumElements(), SplitSrcs,
               MIRBuilder, MRI);
  // We're going to do a tree reduction using vector operations until we have
  // one NarrowTy size value left.
  while (SplitSrcs.size() > 1) {
    SmallVector<Register> PartialRdxs;
    for (unsigned Idx = 0; Idx < SplitSrcs.size()-1; Idx += 2) {
      Register LHS = SplitSrcs[Idx];
      Register RHS = SplitSrcs[Idx + 1];
      // Create the intermediate vector op.
      Register Res =
          MIRBuilder.buildInstr(ScalarOpc, {NarrowTy}, {LHS, RHS}).getReg(0);
      PartialRdxs.push_back(Res);
    }
    SplitSrcs = std::move(PartialRdxs);
  }
  // Finally generate the requested NarrowTy based reduction.
  Observer.changingInstr(MI);
  MI.getOperand(1).setReg(SplitSrcs[0]);
  Observer.changedInstr(MI);
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarShiftByConstant(MachineInstr &MI, const APInt &Amt,
                                             const LLT HalfTy, const LLT AmtTy) {

  Register InL = MRI.createGenericVirtualRegister(HalfTy);
  Register InH = MRI.createGenericVirtualRegister(HalfTy);
  MIRBuilder.buildUnmerge({InL, InH}, MI.getOperand(1));

  if (Amt.isZero()) {
    MIRBuilder.buildMergeLikeInstr(MI.getOperand(0), {InL, InH});
    MI.eraseFromParent();
    return Legalized;
  }

  LLT NVT = HalfTy;
  unsigned NVTBits = HalfTy.getSizeInBits();
  unsigned VTBits = 2 * NVTBits;

  SrcOp Lo(Register(0)), Hi(Register(0));
  if (MI.getOpcode() == TargetOpcode::G_SHL) {
    if (Amt.ugt(VTBits)) {
      Lo = Hi = MIRBuilder.buildConstant(NVT, 0);
    } else if (Amt.ugt(NVTBits)) {
      Lo = MIRBuilder.buildConstant(NVT, 0);
      Hi = MIRBuilder.buildShl(NVT, InL,
                               MIRBuilder.buildConstant(AmtTy, Amt - NVTBits));
    } else if (Amt == NVTBits) {
      Lo = MIRBuilder.buildConstant(NVT, 0);
      Hi = InL;
    } else {
      Lo = MIRBuilder.buildShl(NVT, InL, MIRBuilder.buildConstant(AmtTy, Amt));
      auto OrLHS =
          MIRBuilder.buildShl(NVT, InH, MIRBuilder.buildConstant(AmtTy, Amt));
      auto OrRHS = MIRBuilder.buildLShr(
          NVT, InL, MIRBuilder.buildConstant(AmtTy, -Amt + NVTBits));
      Hi = MIRBuilder.buildOr(NVT, OrLHS, OrRHS);
    }
  } else if (MI.getOpcode() == TargetOpcode::G_LSHR) {
    if (Amt.ugt(VTBits)) {
      Lo = Hi = MIRBuilder.buildConstant(NVT, 0);
    } else if (Amt.ugt(NVTBits)) {
      Lo = MIRBuilder.buildLShr(NVT, InH,
                                MIRBuilder.buildConstant(AmtTy, Amt - NVTBits));
      Hi = MIRBuilder.buildConstant(NVT, 0);
    } else if (Amt == NVTBits) {
      Lo = InH;
      Hi = MIRBuilder.buildConstant(NVT, 0);
    } else {
      auto ShiftAmtConst = MIRBuilder.buildConstant(AmtTy, Amt);

      auto OrLHS = MIRBuilder.buildLShr(NVT, InL, ShiftAmtConst);
      auto OrRHS = MIRBuilder.buildShl(
          NVT, InH, MIRBuilder.buildConstant(AmtTy, -Amt + NVTBits));

      Lo = MIRBuilder.buildOr(NVT, OrLHS, OrRHS);
      Hi = MIRBuilder.buildLShr(NVT, InH, ShiftAmtConst);
    }
  } else {
    if (Amt.ugt(VTBits)) {
      Hi = Lo = MIRBuilder.buildAShr(
          NVT, InH, MIRBuilder.buildConstant(AmtTy, NVTBits - 1));
    } else if (Amt.ugt(NVTBits)) {
      Lo = MIRBuilder.buildAShr(NVT, InH,
                                MIRBuilder.buildConstant(AmtTy, Amt - NVTBits));
      Hi = MIRBuilder.buildAShr(NVT, InH,
                                MIRBuilder.buildConstant(AmtTy, NVTBits - 1));
    } else if (Amt == NVTBits) {
      Lo = InH;
      Hi = MIRBuilder.buildAShr(NVT, InH,
                                MIRBuilder.buildConstant(AmtTy, NVTBits - 1));
    } else {
      auto ShiftAmtConst = MIRBuilder.buildConstant(AmtTy, Amt);

      auto OrLHS = MIRBuilder.buildLShr(NVT, InL, ShiftAmtConst);
      auto OrRHS = MIRBuilder.buildShl(
          NVT, InH, MIRBuilder.buildConstant(AmtTy, -Amt + NVTBits));

      Lo = MIRBuilder.buildOr(NVT, OrLHS, OrRHS);
      Hi = MIRBuilder.buildAShr(NVT, InH, ShiftAmtConst);
    }
  }

  MIRBuilder.buildMergeLikeInstr(MI.getOperand(0), {Lo, Hi});
  MI.eraseFromParent();

  return Legalized;
}

// TODO: Optimize if constant shift amount.
LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarShift(MachineInstr &MI, unsigned TypeIdx,
                                   LLT RequestedTy) {
  if (TypeIdx == 1) {
    Observer.changingInstr(MI);
    narrowScalarSrc(MI, RequestedTy, 2);
    Observer.changedInstr(MI);
    return Legalized;
  }

  Register DstReg = MI.getOperand(0).getReg();
  LLT DstTy = MRI.getType(DstReg);
  if (DstTy.isVector())
    return UnableToLegalize;

  Register Amt = MI.getOperand(2).getReg();
  LLT ShiftAmtTy = MRI.getType(Amt);
  const unsigned DstEltSize = DstTy.getScalarSizeInBits();
  if (DstEltSize % 2 != 0)
    return UnableToLegalize;

  // Ignore the input type. We can only go to exactly half the size of the
  // input. If that isn't small enough, the resulting pieces will be further
  // legalized.
  const unsigned NewBitSize = DstEltSize / 2;
  const LLT HalfTy = LLT::scalar(NewBitSize);
  const LLT CondTy = LLT::scalar(1);

  if (auto VRegAndVal = getIConstantVRegValWithLookThrough(Amt, MRI)) {
    return narrowScalarShiftByConstant(MI, VRegAndVal->Value, HalfTy,
                                       ShiftAmtTy);
  }

  // TODO: Expand with known bits.

  // Handle the fully general expansion by an unknown amount.
  auto NewBits = MIRBuilder.buildConstant(ShiftAmtTy, NewBitSize);

  Register InL = MRI.createGenericVirtualRegister(HalfTy);
  Register InH = MRI.createGenericVirtualRegister(HalfTy);
  MIRBuilder.buildUnmerge({InL, InH}, MI.getOperand(1));

  auto AmtExcess = MIRBuilder.buildSub(ShiftAmtTy, Amt, NewBits);
  auto AmtLack = MIRBuilder.buildSub(ShiftAmtTy, NewBits, Amt);

  auto Zero = MIRBuilder.buildConstant(ShiftAmtTy, 0);
  auto IsShort = MIRBuilder.buildICmp(ICmpInst::ICMP_ULT, CondTy, Amt, NewBits);
  auto IsZero = MIRBuilder.buildICmp(ICmpInst::ICMP_EQ, CondTy, Amt, Zero);

  Register ResultRegs[2];
  switch (MI.getOpcode()) {
  case TargetOpcode::G_SHL: {
    // Short: ShAmt < NewBitSize
    auto LoS = MIRBuilder.buildShl(HalfTy, InL, Amt);

    auto LoOr = MIRBuilder.buildLShr(HalfTy, InL, AmtLack);
    auto HiOr = MIRBuilder.buildShl(HalfTy, InH, Amt);
    auto HiS = MIRBuilder.buildOr(HalfTy, LoOr, HiOr);

    // Long: ShAmt >= NewBitSize
    auto LoL = MIRBuilder.buildConstant(HalfTy, 0);         // Lo part is zero.
    auto HiL = MIRBuilder.buildShl(HalfTy, InL, AmtExcess); // Hi from Lo part.

    auto Lo = MIRBuilder.buildSelect(HalfTy, IsShort, LoS, LoL);
    auto Hi = MIRBuilder.buildSelect(
        HalfTy, IsZero, InH, MIRBuilder.buildSelect(HalfTy, IsShort, HiS, HiL));

    ResultRegs[0] = Lo.getReg(0);
    ResultRegs[1] = Hi.getReg(0);
    break;
  }
  case TargetOpcode::G_LSHR:
  case TargetOpcode::G_ASHR: {
    // Short: ShAmt < NewBitSize
    auto HiS = MIRBuilder.buildInstr(MI.getOpcode(), {HalfTy}, {InH, Amt});

    auto LoOr = MIRBuilder.buildLShr(HalfTy, InL, Amt);
    auto HiOr = MIRBuilder.buildShl(HalfTy, InH, AmtLack);
    auto LoS = MIRBuilder.buildOr(HalfTy, LoOr, HiOr);

    // Long: ShAmt >= NewBitSize
    MachineInstrBuilder HiL;
    if (MI.getOpcode() == TargetOpcode::G_LSHR) {
      HiL = MIRBuilder.buildConstant(HalfTy, 0);            // Hi part is zero.
    } else {
      auto ShiftAmt = MIRBuilder.buildConstant(ShiftAmtTy, NewBitSize - 1);
      HiL = MIRBuilder.buildAShr(HalfTy, InH, ShiftAmt);    // Sign of Hi part.
    }
    auto LoL = MIRBuilder.buildInstr(MI.getOpcode(), {HalfTy},
                                     {InH, AmtExcess});     // Lo from Hi part.

    auto Lo = MIRBuilder.buildSelect(
        HalfTy, IsZero, InL, MIRBuilder.buildSelect(HalfTy, IsShort, LoS, LoL));

    auto Hi = MIRBuilder.buildSelect(HalfTy, IsShort, HiS, HiL);

    ResultRegs[0] = Lo.getReg(0);
    ResultRegs[1] = Hi.getReg(0);
    break;
  }
  default:
    llvm_unreachable("not a shift");
  }

  MIRBuilder.buildMergeLikeInstr(DstReg, ResultRegs);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::moreElementsVectorPhi(MachineInstr &MI, unsigned TypeIdx,
                                       LLT MoreTy) {
  assert(TypeIdx == 0 && "Expecting only Idx 0");

  Observer.changingInstr(MI);
  for (unsigned I = 1, E = MI.getNumOperands(); I != E; I += 2) {
    MachineBasicBlock &OpMBB = *MI.getOperand(I + 1).getMBB();
    MIRBuilder.setInsertPt(OpMBB, OpMBB.getFirstTerminator());
    moreElementsVectorSrc(MI, MoreTy, I);
  }

  MachineBasicBlock &MBB = *MI.getParent();
  MIRBuilder.setInsertPt(MBB, --MBB.getFirstNonPHI());
  moreElementsVectorDst(MI, MoreTy, 0);
  Observer.changedInstr(MI);
  return Legalized;
}

MachineInstrBuilder LegalizerHelper::getNeutralElementForVecReduce(
    unsigned Opcode, MachineIRBuilder &MIRBuilder, LLT Ty) {
  assert(Ty.isScalar() && "Expected scalar type to make neutral element for");

  switch (Opcode) {
  default:
    llvm_unreachable(
        "getNeutralElementForVecReduce called with invalid opcode!");
  case TargetOpcode::G_VECREDUCE_ADD:
  case TargetOpcode::G_VECREDUCE_OR:
  case TargetOpcode::G_VECREDUCE_XOR:
  case TargetOpcode::G_VECREDUCE_UMAX:
    return MIRBuilder.buildConstant(Ty, 0);
  case TargetOpcode::G_VECREDUCE_MUL:
    return MIRBuilder.buildConstant(Ty, 1);
  case TargetOpcode::G_VECREDUCE_AND:
  case TargetOpcode::G_VECREDUCE_UMIN:
    return MIRBuilder.buildConstant(
        Ty, APInt::getAllOnes(Ty.getScalarSizeInBits()));
  case TargetOpcode::G_VECREDUCE_SMAX:
    return MIRBuilder.buildConstant(
        Ty, APInt::getSignedMinValue(Ty.getSizeInBits()));
  case TargetOpcode::G_VECREDUCE_SMIN:
    return MIRBuilder.buildConstant(
        Ty, APInt::getSignedMaxValue(Ty.getSizeInBits()));
  case TargetOpcode::G_VECREDUCE_FADD:
    return MIRBuilder.buildFConstant(Ty, -0.0);
  case TargetOpcode::G_VECREDUCE_FMUL:
    return MIRBuilder.buildFConstant(Ty, 1.0);
  case TargetOpcode::G_VECREDUCE_FMINIMUM:
  case TargetOpcode::G_VECREDUCE_FMAXIMUM:
    assert(false && "getNeutralElementForVecReduce unimplemented for "
                    "G_VECREDUCE_FMINIMUM and G_VECREDUCE_FMAXIMUM!");
  }
  llvm_unreachable("switch expected to return!");
}

LegalizerHelper::LegalizeResult
LegalizerHelper::moreElementsVector(MachineInstr &MI, unsigned TypeIdx,
                                    LLT MoreTy) {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  case TargetOpcode::G_IMPLICIT_DEF:
  case TargetOpcode::G_LOAD: {
    if (TypeIdx != 0)
      return UnableToLegalize;
    Observer.changingInstr(MI);
    moreElementsVectorDst(MI, MoreTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_STORE:
    if (TypeIdx != 0)
      return UnableToLegalize;
    Observer.changingInstr(MI);
    moreElementsVectorSrc(MI, MoreTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_AND:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_XOR:
  case TargetOpcode::G_ADD:
  case TargetOpcode::G_SUB:
  case TargetOpcode::G_MUL:
  case TargetOpcode::G_FADD:
  case TargetOpcode::G_FSUB:
  case TargetOpcode::G_FMUL:
  case TargetOpcode::G_FDIV:
  case TargetOpcode::G_FCOPYSIGN:
  case TargetOpcode::G_UADDSAT:
  case TargetOpcode::G_USUBSAT:
  case TargetOpcode::G_SADDSAT:
  case TargetOpcode::G_SSUBSAT:
  case TargetOpcode::G_SMIN:
  case TargetOpcode::G_SMAX:
  case TargetOpcode::G_UMIN:
  case TargetOpcode::G_UMAX:
  case TargetOpcode::G_FMINNUM:
  case TargetOpcode::G_FMAXNUM:
  case TargetOpcode::G_FMINNUM_IEEE:
  case TargetOpcode::G_FMAXNUM_IEEE:
  case TargetOpcode::G_FMINIMUM:
  case TargetOpcode::G_FMAXIMUM:
  case TargetOpcode::G_STRICT_FADD:
  case TargetOpcode::G_STRICT_FSUB:
  case TargetOpcode::G_STRICT_FMUL:
  case TargetOpcode::G_SHL:
  case TargetOpcode::G_ASHR:
  case TargetOpcode::G_LSHR: {
    Observer.changingInstr(MI);
    moreElementsVectorSrc(MI, MoreTy, 1);
    moreElementsVectorSrc(MI, MoreTy, 2);
    moreElementsVectorDst(MI, MoreTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_FMA:
  case TargetOpcode::G_STRICT_FMA:
  case TargetOpcode::G_FSHR:
  case TargetOpcode::G_FSHL: {
    Observer.changingInstr(MI);
    moreElementsVectorSrc(MI, MoreTy, 1);
    moreElementsVectorSrc(MI, MoreTy, 2);
    moreElementsVectorSrc(MI, MoreTy, 3);
    moreElementsVectorDst(MI, MoreTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_EXTRACT_VECTOR_ELT:
  case TargetOpcode::G_EXTRACT:
    if (TypeIdx != 1)
      return UnableToLegalize;
    Observer.changingInstr(MI);
    moreElementsVectorSrc(MI, MoreTy, 1);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_INSERT:
  case TargetOpcode::G_INSERT_VECTOR_ELT:
  case TargetOpcode::G_FREEZE:
  case TargetOpcode::G_FNEG:
  case TargetOpcode::G_FABS:
  case TargetOpcode::G_FSQRT:
  case TargetOpcode::G_FCEIL:
  case TargetOpcode::G_FFLOOR:
  case TargetOpcode::G_FNEARBYINT:
  case TargetOpcode::G_FRINT:
  case TargetOpcode::G_INTRINSIC_ROUND:
  case TargetOpcode::G_INTRINSIC_ROUNDEVEN:
  case TargetOpcode::G_INTRINSIC_TRUNC:
  case TargetOpcode::G_BSWAP:
  case TargetOpcode::G_FCANONICALIZE:
  case TargetOpcode::G_SEXT_INREG:
  case TargetOpcode::G_ABS:
    if (TypeIdx != 0)
      return UnableToLegalize;
    Observer.changingInstr(MI);
    moreElementsVectorSrc(MI, MoreTy, 1);
    moreElementsVectorDst(MI, MoreTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  case TargetOpcode::G_SELECT: {
    auto [DstReg, DstTy, CondReg, CondTy] = MI.getFirst2RegLLTs();
    if (TypeIdx == 1) {
      if (!CondTy.isScalar() ||
          DstTy.getElementCount() != MoreTy.getElementCount())
        return UnableToLegalize;

      // This is turning a scalar select of vectors into a vector
      // select. Broadcast the select condition.
      auto ShufSplat = MIRBuilder.buildShuffleSplat(MoreTy, CondReg);
      Observer.changingInstr(MI);
      MI.getOperand(1).setReg(ShufSplat.getReg(0));
      Observer.changedInstr(MI);
      return Legalized;
    }

    if (CondTy.isVector())
      return UnableToLegalize;

    Observer.changingInstr(MI);
    moreElementsVectorSrc(MI, MoreTy, 2);
    moreElementsVectorSrc(MI, MoreTy, 3);
    moreElementsVectorDst(MI, MoreTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_UNMERGE_VALUES:
    return UnableToLegalize;
  case TargetOpcode::G_PHI:
    return moreElementsVectorPhi(MI, TypeIdx, MoreTy);
  case TargetOpcode::G_SHUFFLE_VECTOR:
    return moreElementsVectorShuffle(MI, TypeIdx, MoreTy);
  case TargetOpcode::G_BUILD_VECTOR: {
    SmallVector<SrcOp, 8> Elts;
    for (auto Op : MI.uses()) {
      Elts.push_back(Op.getReg());
    }

    for (unsigned i = Elts.size(); i < MoreTy.getNumElements(); ++i) {
      Elts.push_back(MIRBuilder.buildUndef(MoreTy.getScalarType()));
    }

    MIRBuilder.buildDeleteTrailingVectorElements(
        MI.getOperand(0).getReg(), MIRBuilder.buildInstr(Opc, {MoreTy}, Elts));
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_SEXT:
  case TargetOpcode::G_ZEXT:
  case TargetOpcode::G_ANYEXT:
  case TargetOpcode::G_TRUNC:
  case TargetOpcode::G_FPTRUNC:
  case TargetOpcode::G_FPEXT:
  case TargetOpcode::G_FPTOSI:
  case TargetOpcode::G_FPTOUI:
  case TargetOpcode::G_SITOFP:
  case TargetOpcode::G_UITOFP: {
    Observer.changingInstr(MI);
    LLT SrcExtTy;
    LLT DstExtTy;
    if (TypeIdx == 0) {
      DstExtTy = MoreTy;
      SrcExtTy = LLT::fixed_vector(
          MoreTy.getNumElements(),
          MRI.getType(MI.getOperand(1).getReg()).getElementType());
    } else {
      DstExtTy = LLT::fixed_vector(
          MoreTy.getNumElements(),
          MRI.getType(MI.getOperand(0).getReg()).getElementType());
      SrcExtTy = MoreTy;
    }
    moreElementsVectorSrc(MI, SrcExtTy, 1);
    moreElementsVectorDst(MI, DstExtTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_ICMP:
  case TargetOpcode::G_FCMP: {
    if (TypeIdx != 1)
      return UnableToLegalize;

    Observer.changingInstr(MI);
    moreElementsVectorSrc(MI, MoreTy, 2);
    moreElementsVectorSrc(MI, MoreTy, 3);
    LLT CondTy = LLT::fixed_vector(
        MoreTy.getNumElements(),
        MRI.getType(MI.getOperand(0).getReg()).getElementType());
    moreElementsVectorDst(MI, CondTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_BITCAST: {
    if (TypeIdx != 0)
      return UnableToLegalize;

    LLT SrcTy = MRI.getType(MI.getOperand(1).getReg());
    LLT DstTy = MRI.getType(MI.getOperand(0).getReg());

    unsigned coefficient = SrcTy.getNumElements() * MoreTy.getNumElements();
    if (coefficient % DstTy.getNumElements() != 0)
      return UnableToLegalize;

    coefficient = coefficient / DstTy.getNumElements();

    LLT NewTy = SrcTy.changeElementCount(
        ElementCount::get(coefficient, MoreTy.isScalable()));
    Observer.changingInstr(MI);
    moreElementsVectorSrc(MI, NewTy, 1);
    moreElementsVectorDst(MI, MoreTy, 0);
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_VECREDUCE_FADD:
  case TargetOpcode::G_VECREDUCE_FMUL:
  case TargetOpcode::G_VECREDUCE_ADD:
  case TargetOpcode::G_VECREDUCE_MUL:
  case TargetOpcode::G_VECREDUCE_AND:
  case TargetOpcode::G_VECREDUCE_OR:
  case TargetOpcode::G_VECREDUCE_XOR:
  case TargetOpcode::G_VECREDUCE_SMAX:
  case TargetOpcode::G_VECREDUCE_SMIN:
  case TargetOpcode::G_VECREDUCE_UMAX:
  case TargetOpcode::G_VECREDUCE_UMIN: {
    LLT OrigTy = MRI.getType(MI.getOperand(1).getReg());
    MachineOperand &MO = MI.getOperand(1);
    auto NewVec = MIRBuilder.buildPadVectorWithUndefElements(MoreTy, MO);
    auto NeutralElement = getNeutralElementForVecReduce(
        MI.getOpcode(), MIRBuilder, MoreTy.getElementType());

    LLT IdxTy(TLI.getVectorIdxTy(MIRBuilder.getDataLayout()));
    for (size_t i = OrigTy.getNumElements(), e = MoreTy.getNumElements();
         i != e; i++) {
      auto Idx = MIRBuilder.buildConstant(IdxTy, i);
      NewVec = MIRBuilder.buildInsertVectorElement(MoreTy, NewVec,
                                                   NeutralElement, Idx);
    }

    Observer.changingInstr(MI);
    MO.setReg(NewVec.getReg(0));
    Observer.changedInstr(MI);
    return Legalized;
  }

  default:
    return UnableToLegalize;
  }
}

LegalizerHelper::LegalizeResult
LegalizerHelper::equalizeVectorShuffleLengths(MachineInstr &MI) {
  auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();
  ArrayRef<int> Mask = MI.getOperand(3).getShuffleMask();
  unsigned MaskNumElts = Mask.size();
  unsigned SrcNumElts = SrcTy.getNumElements();
  LLT DestEltTy = DstTy.getElementType();

  if (MaskNumElts == SrcNumElts)
    return Legalized;

  if (MaskNumElts < SrcNumElts) {
    // Extend mask to match new destination vector size with
    // undef values.
    SmallVector<int, 16> NewMask(Mask);
    for (unsigned I = MaskNumElts; I < SrcNumElts; ++I)
      NewMask.push_back(-1);

    moreElementsVectorDst(MI, SrcTy, 0);
    MIRBuilder.setInstrAndDebugLoc(MI);
    MIRBuilder.buildShuffleVector(MI.getOperand(0).getReg(),
                                  MI.getOperand(1).getReg(),
                                  MI.getOperand(2).getReg(), NewMask);
    MI.eraseFromParent();

    return Legalized;
  }

  unsigned PaddedMaskNumElts = alignTo(MaskNumElts, SrcNumElts);
  unsigned NumConcat = PaddedMaskNumElts / SrcNumElts;
  LLT PaddedTy = LLT::fixed_vector(PaddedMaskNumElts, DestEltTy);

  // Create new source vectors by concatenating the initial
  // source vectors with undefined vectors of the same size.
  auto Undef = MIRBuilder.buildUndef(SrcTy);
  SmallVector<Register, 8> MOps1(NumConcat, Undef.getReg(0));
  SmallVector<Register, 8> MOps2(NumConcat, Undef.getReg(0));
  MOps1[0] = MI.getOperand(1).getReg();
  MOps2[0] = MI.getOperand(2).getReg();

  auto Src1 = MIRBuilder.buildConcatVectors(PaddedTy, MOps1);
  auto Src2 = MIRBuilder.buildConcatVectors(PaddedTy, MOps2);

  // Readjust mask for new input vector length.
  SmallVector<int, 8> MappedOps(PaddedMaskNumElts, -1);
  for (unsigned I = 0; I != MaskNumElts; ++I) {
    int Idx = Mask[I];
    if (Idx >= static_cast<int>(SrcNumElts))
      Idx += PaddedMaskNumElts - SrcNumElts;
    MappedOps[I] = Idx;
  }

  // If we got more elements than required, extract subvector.
  if (MaskNumElts != PaddedMaskNumElts) {
    auto Shuffle =
        MIRBuilder.buildShuffleVector(PaddedTy, Src1, Src2, MappedOps);

    SmallVector<Register, 16> Elts(MaskNumElts);
    for (unsigned I = 0; I < MaskNumElts; ++I) {
      Elts[I] =
          MIRBuilder.buildExtractVectorElementConstant(DestEltTy, Shuffle, I)
              .getReg(0);
    }
    MIRBuilder.buildBuildVector(DstReg, Elts);
  } else {
    MIRBuilder.buildShuffleVector(DstReg, Src1, Src2, MappedOps);
  }

  MI.eraseFromParent();
  return LegalizerHelper::LegalizeResult::Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::moreElementsVectorShuffle(MachineInstr &MI,
                                           unsigned int TypeIdx, LLT MoreTy) {
  auto [DstTy, Src1Ty, Src2Ty] = MI.getFirst3LLTs();
  ArrayRef<int> Mask = MI.getOperand(3).getShuffleMask();
  unsigned NumElts = DstTy.getNumElements();
  unsigned WidenNumElts = MoreTy.getNumElements();

  if (DstTy.isVector() && Src1Ty.isVector() &&
      DstTy.getNumElements() != Src1Ty.getNumElements()) {
    return equalizeVectorShuffleLengths(MI);
  }

  if (TypeIdx != 0)
    return UnableToLegalize;

  // Expect a canonicalized shuffle.
  if (DstTy != Src1Ty || DstTy != Src2Ty)
    return UnableToLegalize;

  moreElementsVectorSrc(MI, MoreTy, 1);
  moreElementsVectorSrc(MI, MoreTy, 2);

  // Adjust mask based on new input vector length.
  SmallVector<int, 16> NewMask;
  for (unsigned I = 0; I != NumElts; ++I) {
    int Idx = Mask[I];
    if (Idx < static_cast<int>(NumElts))
      NewMask.push_back(Idx);
    else
      NewMask.push_back(Idx - NumElts + WidenNumElts);
  }
  for (unsigned I = NumElts; I != WidenNumElts; ++I)
    NewMask.push_back(-1);
  moreElementsVectorDst(MI, MoreTy, 0);
  MIRBuilder.setInstrAndDebugLoc(MI);
  MIRBuilder.buildShuffleVector(MI.getOperand(0).getReg(),
                                MI.getOperand(1).getReg(),
                                MI.getOperand(2).getReg(), NewMask);
  MI.eraseFromParent();
  return Legalized;
}

void LegalizerHelper::multiplyRegisters(SmallVectorImpl<Register> &DstRegs,
                                        ArrayRef<Register> Src1Regs,
                                        ArrayRef<Register> Src2Regs,
                                        LLT NarrowTy) {
  MachineIRBuilder &B = MIRBuilder;
  unsigned SrcParts = Src1Regs.size();
  unsigned DstParts = DstRegs.size();

  unsigned DstIdx = 0; // Low bits of the result.
  Register FactorSum =
      B.buildMul(NarrowTy, Src1Regs[DstIdx], Src2Regs[DstIdx]).getReg(0);
  DstRegs[DstIdx] = FactorSum;

  unsigned CarrySumPrevDstIdx;
  SmallVector<Register, 4> Factors;

  for (DstIdx = 1; DstIdx < DstParts; DstIdx++) {
    // Collect low parts of muls for DstIdx.
    for (unsigned i = DstIdx + 1 < SrcParts ? 0 : DstIdx - SrcParts + 1;
         i <= std::min(DstIdx, SrcParts - 1); ++i) {
      MachineInstrBuilder Mul =
          B.buildMul(NarrowTy, Src1Regs[DstIdx - i], Src2Regs[i]);
      Factors.push_back(Mul.getReg(0));
    }
    // Collect high parts of muls from previous DstIdx.
    for (unsigned i = DstIdx < SrcParts ? 0 : DstIdx - SrcParts;
         i <= std::min(DstIdx - 1, SrcParts - 1); ++i) {
      MachineInstrBuilder Umulh =
          B.buildUMulH(NarrowTy, Src1Regs[DstIdx - 1 - i], Src2Regs[i]);
      Factors.push_back(Umulh.getReg(0));
    }
    // Add CarrySum from additions calculated for previous DstIdx.
    if (DstIdx != 1) {
      Factors.push_back(CarrySumPrevDstIdx);
    }

    Register CarrySum;
    // Add all factors and accumulate all carries into CarrySum.
    if (DstIdx != DstParts - 1) {
      MachineInstrBuilder Uaddo =
          B.buildUAddo(NarrowTy, LLT::scalar(1), Factors[0], Factors[1]);
      FactorSum = Uaddo.getReg(0);
      CarrySum = B.buildZExt(NarrowTy, Uaddo.getReg(1)).getReg(0);
      for (unsigned i = 2; i < Factors.size(); ++i) {
        MachineInstrBuilder Uaddo =
            B.buildUAddo(NarrowTy, LLT::scalar(1), FactorSum, Factors[i]);
        FactorSum = Uaddo.getReg(0);
        MachineInstrBuilder Carry = B.buildZExt(NarrowTy, Uaddo.getReg(1));
        CarrySum = B.buildAdd(NarrowTy, CarrySum, Carry).getReg(0);
      }
    } else {
      // Since value for the next index is not calculated, neither is CarrySum.
      FactorSum = B.buildAdd(NarrowTy, Factors[0], Factors[1]).getReg(0);
      for (unsigned i = 2; i < Factors.size(); ++i)
        FactorSum = B.buildAdd(NarrowTy, FactorSum, Factors[i]).getReg(0);
    }

    CarrySumPrevDstIdx = CarrySum;
    DstRegs[DstIdx] = FactorSum;
    Factors.clear();
  }
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarAddSub(MachineInstr &MI, unsigned TypeIdx,
                                    LLT NarrowTy) {
  if (TypeIdx != 0)
    return UnableToLegalize;

  Register DstReg = MI.getOperand(0).getReg();
  LLT DstType = MRI.getType(DstReg);
  // FIXME: add support for vector types
  if (DstType.isVector())
    return UnableToLegalize;

  unsigned Opcode = MI.getOpcode();
  unsigned OpO, OpE, OpF;
  switch (Opcode) {
  case TargetOpcode::G_SADDO:
  case TargetOpcode::G_SADDE:
  case TargetOpcode::G_UADDO:
  case TargetOpcode::G_UADDE:
  case TargetOpcode::G_ADD:
    OpO = TargetOpcode::G_UADDO;
    OpE = TargetOpcode::G_UADDE;
    OpF = TargetOpcode::G_UADDE;
    if (Opcode == TargetOpcode::G_SADDO || Opcode == TargetOpcode::G_SADDE)
      OpF = TargetOpcode::G_SADDE;
    break;
  case TargetOpcode::G_SSUBO:
  case TargetOpcode::G_SSUBE:
  case TargetOpcode::G_USUBO:
  case TargetOpcode::G_USUBE:
  case TargetOpcode::G_SUB:
    OpO = TargetOpcode::G_USUBO;
    OpE = TargetOpcode::G_USUBE;
    OpF = TargetOpcode::G_USUBE;
    if (Opcode == TargetOpcode::G_SSUBO || Opcode == TargetOpcode::G_SSUBE)
      OpF = TargetOpcode::G_SSUBE;
    break;
  default:
    llvm_unreachable("Unexpected add/sub opcode!");
  }

  // 1 for a plain add/sub, 2 if this is an operation with a carry-out.
  unsigned NumDefs = MI.getNumExplicitDefs();
  Register Src1 = MI.getOperand(NumDefs).getReg();
  Register Src2 = MI.getOperand(NumDefs + 1).getReg();
  Register CarryDst, CarryIn;
  if (NumDefs == 2)
    CarryDst = MI.getOperand(1).getReg();
  if (MI.getNumOperands() == NumDefs + 3)
    CarryIn = MI.getOperand(NumDefs + 2).getReg();

  LLT RegTy = MRI.getType(MI.getOperand(0).getReg());
  LLT LeftoverTy, DummyTy;
  SmallVector<Register, 2> Src1Regs, Src2Regs, Src1Left, Src2Left, DstRegs;
  extractParts(Src1, RegTy, NarrowTy, LeftoverTy, Src1Regs, Src1Left,
               MIRBuilder, MRI);
  extractParts(Src2, RegTy, NarrowTy, DummyTy, Src2Regs, Src2Left, MIRBuilder,
               MRI);

  int NarrowParts = Src1Regs.size();
  for (int I = 0, E = Src1Left.size(); I != E; ++I) {
    Src1Regs.push_back(Src1Left[I]);
    Src2Regs.push_back(Src2Left[I]);
  }
  DstRegs.reserve(Src1Regs.size());

  for (int i = 0, e = Src1Regs.size(); i != e; ++i) {
    Register DstReg =
        MRI.createGenericVirtualRegister(MRI.getType(Src1Regs[i]));
    Register CarryOut = MRI.createGenericVirtualRegister(LLT::scalar(1));
    // Forward the final carry-out to the destination register
    if (i == e - 1 && CarryDst)
      CarryOut = CarryDst;

    if (!CarryIn) {
      MIRBuilder.buildInstr(OpO, {DstReg, CarryOut},
                            {Src1Regs[i], Src2Regs[i]});
    } else if (i == e - 1) {
      MIRBuilder.buildInstr(OpF, {DstReg, CarryOut},
                            {Src1Regs[i], Src2Regs[i], CarryIn});
    } else {
      MIRBuilder.buildInstr(OpE, {DstReg, CarryOut},
                            {Src1Regs[i], Src2Regs[i], CarryIn});
    }

    DstRegs.push_back(DstReg);
    CarryIn = CarryOut;
  }
  insertParts(MI.getOperand(0).getReg(), RegTy, NarrowTy,
              ArrayRef(DstRegs).take_front(NarrowParts), LeftoverTy,
              ArrayRef(DstRegs).drop_front(NarrowParts));

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarMul(MachineInstr &MI, LLT NarrowTy) {
  auto [DstReg, Src1, Src2] = MI.getFirst3Regs();

  LLT Ty = MRI.getType(DstReg);
  if (Ty.isVector())
    return UnableToLegalize;

  unsigned Size = Ty.getSizeInBits();
  unsigned NarrowSize = NarrowTy.getSizeInBits();
  if (Size % NarrowSize != 0)
    return UnableToLegalize;

  unsigned NumParts = Size / NarrowSize;
  bool IsMulHigh = MI.getOpcode() == TargetOpcode::G_UMULH;
  unsigned DstTmpParts = NumParts * (IsMulHigh ? 2 : 1);

  SmallVector<Register, 2> Src1Parts, Src2Parts;
  SmallVector<Register, 2> DstTmpRegs(DstTmpParts);
  extractParts(Src1, NarrowTy, NumParts, Src1Parts, MIRBuilder, MRI);
  extractParts(Src2, NarrowTy, NumParts, Src2Parts, MIRBuilder, MRI);
  multiplyRegisters(DstTmpRegs, Src1Parts, Src2Parts, NarrowTy);

  // Take only high half of registers if this is high mul.
  ArrayRef<Register> DstRegs(&DstTmpRegs[DstTmpParts - NumParts], NumParts);
  MIRBuilder.buildMergeLikeInstr(DstReg, DstRegs);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarFPTOI(MachineInstr &MI, unsigned TypeIdx,
                                   LLT NarrowTy) {
  if (TypeIdx != 0)
    return UnableToLegalize;

  bool IsSigned = MI.getOpcode() == TargetOpcode::G_FPTOSI;

  Register Src = MI.getOperand(1).getReg();
  LLT SrcTy = MRI.getType(Src);

  // If all finite floats fit into the narrowed integer type, we can just swap
  // out the result type. This is practically only useful for conversions from
  // half to at least 16-bits, so just handle the one case.
  if (SrcTy.getScalarType() != LLT::scalar(16) ||
      NarrowTy.getScalarSizeInBits() < (IsSigned ? 17u : 16u))
    return UnableToLegalize;

  Observer.changingInstr(MI);
  narrowScalarDst(MI, NarrowTy, 0,
                  IsSigned ? TargetOpcode::G_SEXT : TargetOpcode::G_ZEXT);
  Observer.changedInstr(MI);
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarExtract(MachineInstr &MI, unsigned TypeIdx,
                                     LLT NarrowTy) {
  if (TypeIdx != 1)
    return UnableToLegalize;

  uint64_t NarrowSize = NarrowTy.getSizeInBits();

  int64_t SizeOp1 = MRI.getType(MI.getOperand(1).getReg()).getSizeInBits();
  // FIXME: add support for when SizeOp1 isn't an exact multiple of
  // NarrowSize.
  if (SizeOp1 % NarrowSize != 0)
    return UnableToLegalize;
  int NumParts = SizeOp1 / NarrowSize;

  SmallVector<Register, 2> SrcRegs, DstRegs;
  SmallVector<uint64_t, 2> Indexes;
  extractParts(MI.getOperand(1).getReg(), NarrowTy, NumParts, SrcRegs,
               MIRBuilder, MRI);

  Register OpReg = MI.getOperand(0).getReg();
  uint64_t OpStart = MI.getOperand(2).getImm();
  uint64_t OpSize = MRI.getType(OpReg).getSizeInBits();
  for (int i = 0; i < NumParts; ++i) {
    unsigned SrcStart = i * NarrowSize;

    if (SrcStart + NarrowSize <= OpStart || SrcStart >= OpStart + OpSize) {
      // No part of the extract uses this subregister, ignore it.
      continue;
    } else if (SrcStart == OpStart && NarrowTy == MRI.getType(OpReg)) {
      // The entire subregister is extracted, forward the value.
      DstRegs.push_back(SrcRegs[i]);
      continue;
    }

    // OpSegStart is where this destination segment would start in OpReg if it
    // extended infinitely in both directions.
    int64_t ExtractOffset;
    uint64_t SegSize;
    if (OpStart < SrcStart) {
      ExtractOffset = 0;
      SegSize = std::min(NarrowSize, OpStart + OpSize - SrcStart);
    } else {
      ExtractOffset = OpStart - SrcStart;
      SegSize = std::min(SrcStart + NarrowSize - OpStart, OpSize);
    }

    Register SegReg = SrcRegs[i];
    if (ExtractOffset != 0 || SegSize != NarrowSize) {
      // A genuine extract is needed.
      SegReg = MRI.createGenericVirtualRegister(LLT::scalar(SegSize));
      MIRBuilder.buildExtract(SegReg, SrcRegs[i], ExtractOffset);
    }

    DstRegs.push_back(SegReg);
  }

  Register DstReg = MI.getOperand(0).getReg();
  if (MRI.getType(DstReg).isVector())
    MIRBuilder.buildBuildVector(DstReg, DstRegs);
  else if (DstRegs.size() > 1)
    MIRBuilder.buildMergeLikeInstr(DstReg, DstRegs);
  else
    MIRBuilder.buildCopy(DstReg, DstRegs[0]);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarInsert(MachineInstr &MI, unsigned TypeIdx,
                                    LLT NarrowTy) {
  // FIXME: Don't know how to handle secondary types yet.
  if (TypeIdx != 0)
    return UnableToLegalize;

  SmallVector<Register, 2> SrcRegs, LeftoverRegs, DstRegs;
  SmallVector<uint64_t, 2> Indexes;
  LLT RegTy = MRI.getType(MI.getOperand(0).getReg());
  LLT LeftoverTy;
  extractParts(MI.getOperand(1).getReg(), RegTy, NarrowTy, LeftoverTy, SrcRegs,
               LeftoverRegs, MIRBuilder, MRI);

  for (Register Reg : LeftoverRegs)
    SrcRegs.push_back(Reg);

  uint64_t NarrowSize = NarrowTy.getSizeInBits();
  Register OpReg = MI.getOperand(2).getReg();
  uint64_t OpStart = MI.getOperand(3).getImm();
  uint64_t OpSize = MRI.getType(OpReg).getSizeInBits();
  for (int I = 0, E = SrcRegs.size(); I != E; ++I) {
    unsigned DstStart = I * NarrowSize;

    if (DstStart == OpStart && NarrowTy == MRI.getType(OpReg)) {
      // The entire subregister is defined by this insert, forward the new
      // value.
      DstRegs.push_back(OpReg);
      continue;
    }

    Register SrcReg = SrcRegs[I];
    if (MRI.getType(SrcRegs[I]) == LeftoverTy) {
      // The leftover reg is smaller than NarrowTy, so we need to extend it.
      SrcReg = MRI.createGenericVirtualRegister(NarrowTy);
      MIRBuilder.buildAnyExt(SrcReg, SrcRegs[I]);
    }

    if (DstStart + NarrowSize <= OpStart || DstStart >= OpStart + OpSize) {
      // No part of the insert affects this subregister, forward the original.
      DstRegs.push_back(SrcReg);
      continue;
    }

    // OpSegStart is where this destination segment would start in OpReg if it
    // extended infinitely in both directions.
    int64_t ExtractOffset, InsertOffset;
    uint64_t SegSize;
    if (OpStart < DstStart) {
      InsertOffset = 0;
      ExtractOffset = DstStart - OpStart;
      SegSize = std::min(NarrowSize, OpStart + OpSize - DstStart);
    } else {
      InsertOffset = OpStart - DstStart;
      ExtractOffset = 0;
      SegSize =
        std::min(NarrowSize - InsertOffset, OpStart + OpSize - DstStart);
    }

    Register SegReg = OpReg;
    if (ExtractOffset != 0 || SegSize != OpSize) {
      // A genuine extract is needed.
      SegReg = MRI.createGenericVirtualRegister(LLT::scalar(SegSize));
      MIRBuilder.buildExtract(SegReg, OpReg, ExtractOffset);
    }

    Register DstReg = MRI.createGenericVirtualRegister(NarrowTy);
    MIRBuilder.buildInsert(DstReg, SrcReg, SegReg, InsertOffset);
    DstRegs.push_back(DstReg);
  }

  uint64_t WideSize = DstRegs.size() * NarrowSize;
  Register DstReg = MI.getOperand(0).getReg();
  if (WideSize > RegTy.getSizeInBits()) {
    Register MergeReg = MRI.createGenericVirtualRegister(LLT::scalar(WideSize));
    MIRBuilder.buildMergeLikeInstr(MergeReg, DstRegs);
    MIRBuilder.buildTrunc(DstReg, MergeReg);
  } else
    MIRBuilder.buildMergeLikeInstr(DstReg, DstRegs);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarBasic(MachineInstr &MI, unsigned TypeIdx,
                                   LLT NarrowTy) {
  Register DstReg = MI.getOperand(0).getReg();
  LLT DstTy = MRI.getType(DstReg);

  assert(MI.getNumOperands() == 3 && TypeIdx == 0);

  SmallVector<Register, 4> DstRegs, DstLeftoverRegs;
  SmallVector<Register, 4> Src0Regs, Src0LeftoverRegs;
  SmallVector<Register, 4> Src1Regs, Src1LeftoverRegs;
  LLT LeftoverTy;
  if (!extractParts(MI.getOperand(1).getReg(), DstTy, NarrowTy, LeftoverTy,
                    Src0Regs, Src0LeftoverRegs, MIRBuilder, MRI))
    return UnableToLegalize;

  LLT Unused;
  if (!extractParts(MI.getOperand(2).getReg(), DstTy, NarrowTy, Unused,
                    Src1Regs, Src1LeftoverRegs, MIRBuilder, MRI))
    llvm_unreachable("inconsistent extractParts result");

  for (unsigned I = 0, E = Src1Regs.size(); I != E; ++I) {
    auto Inst = MIRBuilder.buildInstr(MI.getOpcode(), {NarrowTy},
                                        {Src0Regs[I], Src1Regs[I]});
    DstRegs.push_back(Inst.getReg(0));
  }

  for (unsigned I = 0, E = Src1LeftoverRegs.size(); I != E; ++I) {
    auto Inst = MIRBuilder.buildInstr(
      MI.getOpcode(),
      {LeftoverTy}, {Src0LeftoverRegs[I], Src1LeftoverRegs[I]});
    DstLeftoverRegs.push_back(Inst.getReg(0));
  }

  insertParts(DstReg, DstTy, NarrowTy, DstRegs,
              LeftoverTy, DstLeftoverRegs);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarExt(MachineInstr &MI, unsigned TypeIdx,
                                 LLT NarrowTy) {
  if (TypeIdx != 0)
    return UnableToLegalize;

  auto [DstReg, SrcReg] = MI.getFirst2Regs();

  LLT DstTy = MRI.getType(DstReg);
  if (DstTy.isVector())
    return UnableToLegalize;

  SmallVector<Register, 8> Parts;
  LLT GCDTy = extractGCDType(Parts, DstTy, NarrowTy, SrcReg);
  LLT LCMTy = buildLCMMergePieces(DstTy, NarrowTy, GCDTy, Parts, MI.getOpcode());
  buildWidenedRemergeToDst(DstReg, LCMTy, Parts);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarSelect(MachineInstr &MI, unsigned TypeIdx,
                                    LLT NarrowTy) {
  if (TypeIdx != 0)
    return UnableToLegalize;

  Register CondReg = MI.getOperand(1).getReg();
  LLT CondTy = MRI.getType(CondReg);
  if (CondTy.isVector()) // TODO: Handle vselect
    return UnableToLegalize;

  Register DstReg = MI.getOperand(0).getReg();
  LLT DstTy = MRI.getType(DstReg);

  SmallVector<Register, 4> DstRegs, DstLeftoverRegs;
  SmallVector<Register, 4> Src1Regs, Src1LeftoverRegs;
  SmallVector<Register, 4> Src2Regs, Src2LeftoverRegs;
  LLT LeftoverTy;
  if (!extractParts(MI.getOperand(2).getReg(), DstTy, NarrowTy, LeftoverTy,
                    Src1Regs, Src1LeftoverRegs, MIRBuilder, MRI))
    return UnableToLegalize;

  LLT Unused;
  if (!extractParts(MI.getOperand(3).getReg(), DstTy, NarrowTy, Unused,
                    Src2Regs, Src2LeftoverRegs, MIRBuilder, MRI))
    llvm_unreachable("inconsistent extractParts result");

  for (unsigned I = 0, E = Src1Regs.size(); I != E; ++I) {
    auto Select = MIRBuilder.buildSelect(NarrowTy,
                                         CondReg, Src1Regs[I], Src2Regs[I]);
    DstRegs.push_back(Select.getReg(0));
  }

  for (unsigned I = 0, E = Src1LeftoverRegs.size(); I != E; ++I) {
    auto Select = MIRBuilder.buildSelect(
      LeftoverTy, CondReg, Src1LeftoverRegs[I], Src2LeftoverRegs[I]);
    DstLeftoverRegs.push_back(Select.getReg(0));
  }

  insertParts(DstReg, DstTy, NarrowTy, DstRegs,
              LeftoverTy, DstLeftoverRegs);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarCTLZ(MachineInstr &MI, unsigned TypeIdx,
                                  LLT NarrowTy) {
  if (TypeIdx != 1)
    return UnableToLegalize;

  auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();
  unsigned NarrowSize = NarrowTy.getSizeInBits();

  if (SrcTy.isScalar() && SrcTy.getSizeInBits() == 2 * NarrowSize) {
    const bool IsUndef = MI.getOpcode() == TargetOpcode::G_CTLZ_ZERO_UNDEF;

    MachineIRBuilder &B = MIRBuilder;
    auto UnmergeSrc = B.buildUnmerge(NarrowTy, SrcReg);
    // ctlz(Hi:Lo) -> Hi == 0 ? (NarrowSize + ctlz(Lo)) : ctlz(Hi)
    auto C_0 = B.buildConstant(NarrowTy, 0);
    auto HiIsZero = B.buildICmp(CmpInst::ICMP_EQ, LLT::scalar(1),
                                UnmergeSrc.getReg(1), C_0);
    auto LoCTLZ = IsUndef ?
      B.buildCTLZ_ZERO_UNDEF(DstTy, UnmergeSrc.getReg(0)) :
      B.buildCTLZ(DstTy, UnmergeSrc.getReg(0));
    auto C_NarrowSize = B.buildConstant(DstTy, NarrowSize);
    auto HiIsZeroCTLZ = B.buildAdd(DstTy, LoCTLZ, C_NarrowSize);
    auto HiCTLZ = B.buildCTLZ_ZERO_UNDEF(DstTy, UnmergeSrc.getReg(1));
    B.buildSelect(DstReg, HiIsZero, HiIsZeroCTLZ, HiCTLZ);

    MI.eraseFromParent();
    return Legalized;
  }

  return UnableToLegalize;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarCTTZ(MachineInstr &MI, unsigned TypeIdx,
                                  LLT NarrowTy) {
  if (TypeIdx != 1)
    return UnableToLegalize;

  auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();
  unsigned NarrowSize = NarrowTy.getSizeInBits();

  if (SrcTy.isScalar() && SrcTy.getSizeInBits() == 2 * NarrowSize) {
    const bool IsUndef = MI.getOpcode() == TargetOpcode::G_CTTZ_ZERO_UNDEF;

    MachineIRBuilder &B = MIRBuilder;
    auto UnmergeSrc = B.buildUnmerge(NarrowTy, SrcReg);
    // cttz(Hi:Lo) -> Lo == 0 ? (cttz(Hi) + NarrowSize) : cttz(Lo)
    auto C_0 = B.buildConstant(NarrowTy, 0);
    auto LoIsZero = B.buildICmp(CmpInst::ICMP_EQ, LLT::scalar(1),
                                UnmergeSrc.getReg(0), C_0);
    auto HiCTTZ = IsUndef ?
      B.buildCTTZ_ZERO_UNDEF(DstTy, UnmergeSrc.getReg(1)) :
      B.buildCTTZ(DstTy, UnmergeSrc.getReg(1));
    auto C_NarrowSize = B.buildConstant(DstTy, NarrowSize);
    auto LoIsZeroCTTZ = B.buildAdd(DstTy, HiCTTZ, C_NarrowSize);
    auto LoCTTZ = B.buildCTTZ_ZERO_UNDEF(DstTy, UnmergeSrc.getReg(0));
    B.buildSelect(DstReg, LoIsZero, LoIsZeroCTTZ, LoCTTZ);

    MI.eraseFromParent();
    return Legalized;
  }

  return UnableToLegalize;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarCTPOP(MachineInstr &MI, unsigned TypeIdx,
                                   LLT NarrowTy) {
  if (TypeIdx != 1)
    return UnableToLegalize;

  auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();
  unsigned NarrowSize = NarrowTy.getSizeInBits();

  if (SrcTy.isScalar() && SrcTy.getSizeInBits() == 2 * NarrowSize) {
    auto UnmergeSrc = MIRBuilder.buildUnmerge(NarrowTy, MI.getOperand(1));

    auto LoCTPOP = MIRBuilder.buildCTPOP(DstTy, UnmergeSrc.getReg(0));
    auto HiCTPOP = MIRBuilder.buildCTPOP(DstTy, UnmergeSrc.getReg(1));
    MIRBuilder.buildAdd(DstReg, HiCTPOP, LoCTPOP);

    MI.eraseFromParent();
    return Legalized;
  }

  return UnableToLegalize;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::narrowScalarFLDEXP(MachineInstr &MI, unsigned TypeIdx,
                                    LLT NarrowTy) {
  if (TypeIdx != 1)
    return UnableToLegalize;

  MachineIRBuilder &B = MIRBuilder;
  Register ExpReg = MI.getOperand(2).getReg();
  LLT ExpTy = MRI.getType(ExpReg);

  unsigned ClampSize = NarrowTy.getScalarSizeInBits();

  // Clamp the exponent to the range of the target type.
  auto MinExp = B.buildConstant(ExpTy, minIntN(ClampSize));
  auto ClampMin = B.buildSMax(ExpTy, ExpReg, MinExp);
  auto MaxExp = B.buildConstant(ExpTy, maxIntN(ClampSize));
  auto Clamp = B.buildSMin(ExpTy, ClampMin, MaxExp);

  auto Trunc = B.buildTrunc(NarrowTy, Clamp);
  Observer.changingInstr(MI);
  MI.getOperand(2).setReg(Trunc.getReg(0));
  Observer.changedInstr(MI);
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerBitCount(MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  const auto &TII = MIRBuilder.getTII();
  auto isSupported = [this](const LegalityQuery &Q) {
    auto QAction = LI.getAction(Q).Action;
    return QAction == Legal || QAction == Libcall || QAction == Custom;
  };
  switch (Opc) {
  default:
    return UnableToLegalize;
  case TargetOpcode::G_CTLZ_ZERO_UNDEF: {
    // This trivially expands to CTLZ.
    Observer.changingInstr(MI);
    MI.setDesc(TII.get(TargetOpcode::G_CTLZ));
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_CTLZ: {
    auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();
    unsigned Len = SrcTy.getSizeInBits();

    if (isSupported({TargetOpcode::G_CTLZ_ZERO_UNDEF, {DstTy, SrcTy}})) {
      // If CTLZ_ZERO_UNDEF is supported, emit that and a select for zero.
      auto CtlzZU = MIRBuilder.buildCTLZ_ZERO_UNDEF(DstTy, SrcReg);
      auto ZeroSrc = MIRBuilder.buildConstant(SrcTy, 0);
      auto ICmp = MIRBuilder.buildICmp(
          CmpInst::ICMP_EQ, SrcTy.changeElementSize(1), SrcReg, ZeroSrc);
      auto LenConst = MIRBuilder.buildConstant(DstTy, Len);
      MIRBuilder.buildSelect(DstReg, ICmp, LenConst, CtlzZU);
      MI.eraseFromParent();
      return Legalized;
    }
    // for now, we do this:
    // NewLen = NextPowerOf2(Len);
    // x = x | (x >> 1);
    // x = x | (x >> 2);
    // ...
    // x = x | (x >>16);
    // x = x | (x >>32); // for 64-bit input
    // Upto NewLen/2
    // return Len - popcount(x);
    //
    // Ref: "Hacker's Delight" by Henry Warren
    Register Op = SrcReg;
    unsigned NewLen = PowerOf2Ceil(Len);
    for (unsigned i = 0; (1U << i) <= (NewLen / 2); ++i) {
      auto MIBShiftAmt = MIRBuilder.buildConstant(SrcTy, 1ULL << i);
      auto MIBOp = MIRBuilder.buildOr(
          SrcTy, Op, MIRBuilder.buildLShr(SrcTy, Op, MIBShiftAmt));
      Op = MIBOp.getReg(0);
    }
    auto MIBPop = MIRBuilder.buildCTPOP(DstTy, Op);
    MIRBuilder.buildSub(MI.getOperand(0), MIRBuilder.buildConstant(DstTy, Len),
                        MIBPop);
    MI.eraseFromParent();
    return Legalized;
  }
  case TargetOpcode::G_CTTZ_ZERO_UNDEF: {
    // This trivially expands to CTTZ.
    Observer.changingInstr(MI);
    MI.setDesc(TII.get(TargetOpcode::G_CTTZ));
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_CTTZ: {
    auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();

    unsigned Len = SrcTy.getSizeInBits();
    if (isSupported({TargetOpcode::G_CTTZ_ZERO_UNDEF, {DstTy, SrcTy}})) {
      // If CTTZ_ZERO_UNDEF is legal or custom, emit that and a select with
      // zero.
      auto CttzZU = MIRBuilder.buildCTTZ_ZERO_UNDEF(DstTy, SrcReg);
      auto Zero = MIRBuilder.buildConstant(SrcTy, 0);
      auto ICmp = MIRBuilder.buildICmp(
          CmpInst::ICMP_EQ, DstTy.changeElementSize(1), SrcReg, Zero);
      auto LenConst = MIRBuilder.buildConstant(DstTy, Len);
      MIRBuilder.buildSelect(DstReg, ICmp, LenConst, CttzZU);
      MI.eraseFromParent();
      return Legalized;
    }
    // for now, we use: { return popcount(~x & (x - 1)); }
    // unless the target has ctlz but not ctpop, in which case we use:
    // { return 32 - nlz(~x & (x-1)); }
    // Ref: "Hacker's Delight" by Henry Warren
    auto MIBCstNeg1 = MIRBuilder.buildConstant(SrcTy, -1);
    auto MIBNot = MIRBuilder.buildXor(SrcTy, SrcReg, MIBCstNeg1);
    auto MIBTmp = MIRBuilder.buildAnd(
        SrcTy, MIBNot, MIRBuilder.buildAdd(SrcTy, SrcReg, MIBCstNeg1));
    if (!isSupported({TargetOpcode::G_CTPOP, {SrcTy, SrcTy}}) &&
        isSupported({TargetOpcode::G_CTLZ, {SrcTy, SrcTy}})) {
      auto MIBCstLen = MIRBuilder.buildConstant(SrcTy, Len);
      MIRBuilder.buildSub(MI.getOperand(0), MIBCstLen,
                          MIRBuilder.buildCTLZ(SrcTy, MIBTmp));
      MI.eraseFromParent();
      return Legalized;
    }
    Observer.changingInstr(MI);
    MI.setDesc(TII.get(TargetOpcode::G_CTPOP));
    MI.getOperand(1).setReg(MIBTmp.getReg(0));
    Observer.changedInstr(MI);
    return Legalized;
  }
  case TargetOpcode::G_CTPOP: {
    Register SrcReg = MI.getOperand(1).getReg();
    LLT Ty = MRI.getType(SrcReg);
    unsigned Size = Ty.getSizeInBits();
    MachineIRBuilder &B = MIRBuilder;

    // Count set bits in blocks of 2 bits. Default approach would be
    // B2Count = { val & 0x55555555 } + { (val >> 1) & 0x55555555 }
    // We use following formula instead:
    // B2Count = val - { (val >> 1) & 0x55555555 }
    // since it gives same result in blocks of 2 with one instruction less.
    auto C_1 = B.buildConstant(Ty, 1);
    auto B2Set1LoTo1Hi = B.buildLShr(Ty, SrcReg, C_1);
    APInt B2Mask1HiTo0 = APInt::getSplat(Size, APInt(8, 0x55));
    auto C_B2Mask1HiTo0 = B.buildConstant(Ty, B2Mask1HiTo0);
    auto B2Count1Hi = B.buildAnd(Ty, B2Set1LoTo1Hi, C_B2Mask1HiTo0);
    auto B2Count = B.buildSub(Ty, SrcReg, B2Count1Hi);

    // In order to get count in blocks of 4 add values from adjacent block of 2.
    // B4Count = { B2Count & 0x33333333 } + { (B2Count >> 2) & 0x33333333 }
    auto C_2 = B.buildConstant(Ty, 2);
    auto B4Set2LoTo2Hi = B.buildLShr(Ty, B2Count, C_2);
    APInt B4Mask2HiTo0 = APInt::getSplat(Size, APInt(8, 0x33));
    auto C_B4Mask2HiTo0 = B.buildConstant(Ty, B4Mask2HiTo0);
    auto B4HiB2Count = B.buildAnd(Ty, B4Set2LoTo2Hi, C_B4Mask2HiTo0);
    auto B4LoB2Count = B.buildAnd(Ty, B2Count, C_B4Mask2HiTo0);
    auto B4Count = B.buildAdd(Ty, B4HiB2Count, B4LoB2Count);

    // For count in blocks of 8 bits we don't have to mask high 4 bits before
    // addition since count value sits in range {0,...,8} and 4 bits are enough
    // to hold such binary values. After addition high 4 bits still hold count
    // of set bits in high 4 bit block, set them to zero and get 8 bit result.
    // B8Count = { B4Count + (B4Count >> 4) } & 0x0F0F0F0F
    auto C_4 = B.buildConstant(Ty, 4);
    auto B8HiB4Count = B.buildLShr(Ty, B4Count, C_4);
    auto B8CountDirty4Hi = B.buildAdd(Ty, B8HiB4Count, B4Count);
    APInt B8Mask4HiTo0 = APInt::getSplat(Size, APInt(8, 0x0F));
    auto C_B8Mask4HiTo0 = B.buildConstant(Ty, B8Mask4HiTo0);
    auto B8Count = B.buildAnd(Ty, B8CountDirty4Hi, C_B8Mask4HiTo0);

    assert(Size<=128 && "Scalar size is too large for CTPOP lower algorithm");
    // 8 bits can hold CTPOP result of 128 bit int or smaller. Mul with this
    // bitmask will set 8 msb in ResTmp to sum of all B8Counts in 8 bit blocks.
    auto MulMask = B.buildConstant(Ty, APInt::getSplat(Size, APInt(8, 0x01)));

    // Shift count result from 8 high bits to low bits.
    auto C_SizeM8 = B.buildConstant(Ty, Size - 8);

    auto IsMulSupported = [this](const LLT Ty) {
      auto Action = LI.getAction({TargetOpcode::G_MUL, {Ty}}).Action;
      return Action == Legal || Action == WidenScalar || Action == Custom;
    };
    if (IsMulSupported(Ty)) {
      auto ResTmp = B.buildMul(Ty, B8Count, MulMask);
      B.buildLShr(MI.getOperand(0).getReg(), ResTmp, C_SizeM8);
    } else {
      auto ResTmp = B8Count;
      for (unsigned Shift = 8; Shift < Size; Shift *= 2) {
        auto ShiftC = B.buildConstant(Ty, Shift);
        auto Shl = B.buildShl(Ty, ResTmp, ShiftC);
        ResTmp = B.buildAdd(Ty, ResTmp, Shl);
      }
      B.buildLShr(MI.getOperand(0).getReg(), ResTmp, C_SizeM8);
    }
    MI.eraseFromParent();
    return Legalized;
  }
  }
}

// Check that (every element of) Reg is undef or not an exact multiple of BW.
static bool isNonZeroModBitWidthOrUndef(const MachineRegisterInfo &MRI,
                                        Register Reg, unsigned BW) {
  return matchUnaryPredicate(
      MRI, Reg,
      [=](const Constant *C) {
        // Null constant here means an undef.
        const ConstantInt *CI = dyn_cast_or_null<ConstantInt>(C);
        return !CI || CI->getValue().urem(BW) != 0;
      },
      /*AllowUndefs*/ true);
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerFunnelShiftWithInverse(MachineInstr &MI) {
  auto [Dst, X, Y, Z] = MI.getFirst4Regs();
  LLT Ty = MRI.getType(Dst);
  LLT ShTy = MRI.getType(Z);

  unsigned BW = Ty.getScalarSizeInBits();

  if (!isPowerOf2_32(BW))
    return UnableToLegalize;

  const bool IsFSHL = MI.getOpcode() == TargetOpcode::G_FSHL;
  unsigned RevOpcode = IsFSHL ? TargetOpcode::G_FSHR : TargetOpcode::G_FSHL;

  if (isNonZeroModBitWidthOrUndef(MRI, Z, BW)) {
    // fshl X, Y, Z -> fshr X, Y, -Z
    // fshr X, Y, Z -> fshl X, Y, -Z
    auto Zero = MIRBuilder.buildConstant(ShTy, 0);
    Z = MIRBuilder.buildSub(Ty, Zero, Z).getReg(0);
  } else {
    // fshl X, Y, Z -> fshr (srl X, 1), (fshr X, Y, 1), ~Z
    // fshr X, Y, Z -> fshl (fshl X, Y, 1), (shl Y, 1), ~Z
    auto One = MIRBuilder.buildConstant(ShTy, 1);
    if (IsFSHL) {
      Y = MIRBuilder.buildInstr(RevOpcode, {Ty}, {X, Y, One}).getReg(0);
      X = MIRBuilder.buildLShr(Ty, X, One).getReg(0);
    } else {
      X = MIRBuilder.buildInstr(RevOpcode, {Ty}, {X, Y, One}).getReg(0);
      Y = MIRBuilder.buildShl(Ty, Y, One).getReg(0);
    }

    Z = MIRBuilder.buildNot(ShTy, Z).getReg(0);
  }

  MIRBuilder.buildInstr(RevOpcode, {Dst}, {X, Y, Z});
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerFunnelShiftAsShifts(MachineInstr &MI) {
  auto [Dst, X, Y, Z] = MI.getFirst4Regs();
  LLT Ty = MRI.getType(Dst);
  LLT ShTy = MRI.getType(Z);

  const unsigned BW = Ty.getScalarSizeInBits();
  const bool IsFSHL = MI.getOpcode() == TargetOpcode::G_FSHL;

  Register ShX, ShY;
  Register ShAmt, InvShAmt;

  // FIXME: Emit optimized urem by constant instead of letting it expand later.
  if (isNonZeroModBitWidthOrUndef(MRI, Z, BW)) {
    // fshl: X << C | Y >> (BW - C)
    // fshr: X << (BW - C) | Y >> C
    // where C = Z % BW is not zero
    auto BitWidthC = MIRBuilder.buildConstant(ShTy, BW);
    ShAmt = MIRBuilder.buildURem(ShTy, Z, BitWidthC).getReg(0);
    InvShAmt = MIRBuilder.buildSub(ShTy, BitWidthC, ShAmt).getReg(0);
    ShX = MIRBuilder.buildShl(Ty, X, IsFSHL ? ShAmt : InvShAmt).getReg(0);
    ShY = MIRBuilder.buildLShr(Ty, Y, IsFSHL ? InvShAmt : ShAmt).getReg(0);
  } else {
    // fshl: X << (Z % BW) | Y >> 1 >> (BW - 1 - (Z % BW))
    // fshr: X << 1 << (BW - 1 - (Z % BW)) | Y >> (Z % BW)
    auto Mask = MIRBuilder.buildConstant(ShTy, BW - 1);
    if (isPowerOf2_32(BW)) {
      // Z % BW -> Z & (BW - 1)
      ShAmt = MIRBuilder.buildAnd(ShTy, Z, Mask).getReg(0);
      // (BW - 1) - (Z % BW) -> ~Z & (BW - 1)
      auto NotZ = MIRBuilder.buildNot(ShTy, Z);
      InvShAmt = MIRBuilder.buildAnd(ShTy, NotZ, Mask).getReg(0);
    } else {
      auto BitWidthC = MIRBuilder.buildConstant(ShTy, BW);
      ShAmt = MIRBuilder.buildURem(ShTy, Z, BitWidthC).getReg(0);
      InvShAmt = MIRBuilder.buildSub(ShTy, Mask, ShAmt).getReg(0);
    }

    auto One = MIRBuilder.buildConstant(ShTy, 1);
    if (IsFSHL) {
      ShX = MIRBuilder.buildShl(Ty, X, ShAmt).getReg(0);
      auto ShY1 = MIRBuilder.buildLShr(Ty, Y, One);
      ShY = MIRBuilder.buildLShr(Ty, ShY1, InvShAmt).getReg(0);
    } else {
      auto ShX1 = MIRBuilder.buildShl(Ty, X, One);
      ShX = MIRBuilder.buildShl(Ty, ShX1, InvShAmt).getReg(0);
      ShY = MIRBuilder.buildLShr(Ty, Y, ShAmt).getReg(0);
    }
  }

  MIRBuilder.buildOr(Dst, ShX, ShY);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerFunnelShift(MachineInstr &MI) {
  // These operations approximately do the following (while avoiding undefined
  // shifts by BW):
  // G_FSHL: (X << (Z % BW)) | (Y >> (BW - (Z % BW)))
  // G_FSHR: (X << (BW - (Z % BW))) | (Y >> (Z % BW))
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);
  LLT ShTy = MRI.getType(MI.getOperand(3).getReg());

  bool IsFSHL = MI.getOpcode() == TargetOpcode::G_FSHL;
  unsigned RevOpcode = IsFSHL ? TargetOpcode::G_FSHR : TargetOpcode::G_FSHL;

  // TODO: Use smarter heuristic that accounts for vector legalization.
  if (LI.getAction({RevOpcode, {Ty, ShTy}}).Action == Lower)
    return lowerFunnelShiftAsShifts(MI);

  // This only works for powers of 2, fallback to shifts if it fails.
  LegalizerHelper::LegalizeResult Result = lowerFunnelShiftWithInverse(MI);
  if (Result == UnableToLegalize)
    return lowerFunnelShiftAsShifts(MI);
  return Result;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerEXT(MachineInstr &MI) {
  auto [Dst, Src] = MI.getFirst2Regs();
  LLT DstTy = MRI.getType(Dst);
  LLT SrcTy = MRI.getType(Src);

  uint32_t DstTySize = DstTy.getSizeInBits();
  uint32_t DstTyScalarSize = DstTy.getScalarSizeInBits();
  uint32_t SrcTyScalarSize = SrcTy.getScalarSizeInBits();

  if (!isPowerOf2_32(DstTySize) || !isPowerOf2_32(DstTyScalarSize) ||
      !isPowerOf2_32(SrcTyScalarSize))
    return UnableToLegalize;

  // The step between extend is too large, split it by creating an intermediate
  // extend instruction
  if (SrcTyScalarSize * 2 < DstTyScalarSize) {
    LLT MidTy = SrcTy.changeElementSize(SrcTyScalarSize * 2);
    // If the destination type is illegal, split it into multiple statements
    // zext x -> zext(merge(zext(unmerge), zext(unmerge)))
    auto NewExt = MIRBuilder.buildInstr(MI.getOpcode(), {MidTy}, {Src});
    // Unmerge the vector
    LLT EltTy = MidTy.changeElementCount(
        MidTy.getElementCount().divideCoefficientBy(2));
    auto UnmergeSrc = MIRBuilder.buildUnmerge(EltTy, NewExt);

    // ZExt the vectors
    LLT ZExtResTy = DstTy.changeElementCount(
        DstTy.getElementCount().divideCoefficientBy(2));
    auto ZExtRes1 = MIRBuilder.buildInstr(MI.getOpcode(), {ZExtResTy},
                                          {UnmergeSrc.getReg(0)});
    auto ZExtRes2 = MIRBuilder.buildInstr(MI.getOpcode(), {ZExtResTy},
                                          {UnmergeSrc.getReg(1)});

    // Merge the ending vectors
    MIRBuilder.buildMergeLikeInstr(Dst, {ZExtRes1, ZExtRes2});

    MI.eraseFromParent();
    return Legalized;
  }
  return UnableToLegalize;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerTRUNC(MachineInstr &MI) {
  // MachineIRBuilder &MIRBuilder = Helper.MIRBuilder;
  MachineRegisterInfo &MRI = *MIRBuilder.getMRI();
  // Similar to how operand splitting is done in SelectiondDAG, we can handle
  // %res(v8s8) = G_TRUNC %in(v8s32) by generating:
  //   %inlo(<4x s32>), %inhi(<4 x s32>) = G_UNMERGE %in(<8 x s32>)
  //   %lo16(<4 x s16>) = G_TRUNC %inlo
  //   %hi16(<4 x s16>) = G_TRUNC %inhi
  //   %in16(<8 x s16>) = G_CONCAT_VECTORS %lo16, %hi16
  //   %res(<8 x s8>) = G_TRUNC %in16

  assert(MI.getOpcode() == TargetOpcode::G_TRUNC);

  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  LLT DstTy = MRI.getType(DstReg);
  LLT SrcTy = MRI.getType(SrcReg);

  if (DstTy.isVector() && isPowerOf2_32(DstTy.getNumElements()) &&
      isPowerOf2_32(DstTy.getScalarSizeInBits()) &&
      isPowerOf2_32(SrcTy.getNumElements()) &&
      isPowerOf2_32(SrcTy.getScalarSizeInBits())) {
    // Split input type.
    LLT SplitSrcTy = SrcTy.changeElementCount(
        SrcTy.getElementCount().divideCoefficientBy(2));

    // First, split the source into two smaller vectors.
    SmallVector<Register, 2> SplitSrcs;
    extractParts(SrcReg, SplitSrcTy, 2, SplitSrcs, MIRBuilder, MRI);

    // Truncate the splits into intermediate narrower elements.
    LLT InterTy;
    if (DstTy.getScalarSizeInBits() * 2 < SrcTy.getScalarSizeInBits())
      InterTy = SplitSrcTy.changeElementSize(DstTy.getScalarSizeInBits() * 2);
    else
      InterTy = SplitSrcTy.changeElementSize(DstTy.getScalarSizeInBits());
    for (unsigned I = 0; I < SplitSrcs.size(); ++I) {
      SplitSrcs[I] = MIRBuilder.buildTrunc(InterTy, SplitSrcs[I]).getReg(0);
    }

    // Combine the new truncates into one vector
    auto Merge = MIRBuilder.buildMergeLikeInstr(
        DstTy.changeElementSize(InterTy.getScalarSizeInBits()), SplitSrcs);

    // Truncate the new vector to the final result type
    if (DstTy.getScalarSizeInBits() * 2 < SrcTy.getScalarSizeInBits())
      MIRBuilder.buildTrunc(MI.getOperand(0).getReg(), Merge.getReg(0));
    else
      MIRBuilder.buildCopy(MI.getOperand(0).getReg(), Merge.getReg(0));

    MI.eraseFromParent();

    return Legalized;
  }
  return UnableToLegalize;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerRotateWithReverseRotate(MachineInstr &MI) {
  auto [Dst, DstTy, Src, SrcTy, Amt, AmtTy] = MI.getFirst3RegLLTs();
  auto Zero = MIRBuilder.buildConstant(AmtTy, 0);
  bool IsLeft = MI.getOpcode() == TargetOpcode::G_ROTL;
  unsigned RevRot = IsLeft ? TargetOpcode::G_ROTR : TargetOpcode::G_ROTL;
  auto Neg = MIRBuilder.buildSub(AmtTy, Zero, Amt);
  MIRBuilder.buildInstr(RevRot, {Dst}, {Src, Neg});
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerRotate(MachineInstr &MI) {
  auto [Dst, DstTy, Src, SrcTy, Amt, AmtTy] = MI.getFirst3RegLLTs();

  unsigned EltSizeInBits = DstTy.getScalarSizeInBits();
  bool IsLeft = MI.getOpcode() == TargetOpcode::G_ROTL;

  MIRBuilder.setInstrAndDebugLoc(MI);

  // If a rotate in the other direction is supported, use it.
  unsigned RevRot = IsLeft ? TargetOpcode::G_ROTR : TargetOpcode::G_ROTL;
  if (LI.isLegalOrCustom({RevRot, {DstTy, SrcTy}}) &&
      isPowerOf2_32(EltSizeInBits))
    return lowerRotateWithReverseRotate(MI);

  // If a funnel shift is supported, use it.
  unsigned FShOpc = IsLeft ? TargetOpcode::G_FSHL : TargetOpcode::G_FSHR;
  unsigned RevFsh = !IsLeft ? TargetOpcode::G_FSHL : TargetOpcode::G_FSHR;
  bool IsFShLegal = false;
  if ((IsFShLegal = LI.isLegalOrCustom({FShOpc, {DstTy, AmtTy}})) ||
      LI.isLegalOrCustom({RevFsh, {DstTy, AmtTy}})) {
    auto buildFunnelShift = [&](unsigned Opc, Register R1, Register R2,
                                Register R3) {
      MIRBuilder.buildInstr(Opc, {R1}, {R2, R2, R3});
      MI.eraseFromParent();
      return Legalized;
    };
    // If a funnel shift in the other direction is supported, use it.
    if (IsFShLegal) {
      return buildFunnelShift(FShOpc, Dst, Src, Amt);
    } else if (isPowerOf2_32(EltSizeInBits)) {
      Amt = MIRBuilder.buildNeg(DstTy, Amt).getReg(0);
      return buildFunnelShift(RevFsh, Dst, Src, Amt);
    }
  }

  auto Zero = MIRBuilder.buildConstant(AmtTy, 0);
  unsigned ShOpc = IsLeft ? TargetOpcode::G_SHL : TargetOpcode::G_LSHR;
  unsigned RevShiftOpc = IsLeft ? TargetOpcode::G_LSHR : TargetOpcode::G_SHL;
  auto BitWidthMinusOneC = MIRBuilder.buildConstant(AmtTy, EltSizeInBits - 1);
  Register ShVal;
  Register RevShiftVal;
  if (isPowerOf2_32(EltSizeInBits)) {
    // (rotl x, c) -> x << (c & (w - 1)) | x >> (-c & (w - 1))
    // (rotr x, c) -> x >> (c & (w - 1)) | x << (-c & (w - 1))
    auto NegAmt = MIRBuilder.buildSub(AmtTy, Zero, Amt);
    auto ShAmt = MIRBuilder.buildAnd(AmtTy, Amt, BitWidthMinusOneC);
    ShVal = MIRBuilder.buildInstr(ShOpc, {DstTy}, {Src, ShAmt}).getReg(0);
    auto RevAmt = MIRBuilder.buildAnd(AmtTy, NegAmt, BitWidthMinusOneC);
    RevShiftVal =
        MIRBuilder.buildInstr(RevShiftOpc, {DstTy}, {Src, RevAmt}).getReg(0);
  } else {
    // (rotl x, c) -> x << (c % w) | x >> 1 >> (w - 1 - (c % w))
    // (rotr x, c) -> x >> (c % w) | x << 1 << (w - 1 - (c % w))
    auto BitWidthC = MIRBuilder.buildConstant(AmtTy, EltSizeInBits);
    auto ShAmt = MIRBuilder.buildURem(AmtTy, Amt, BitWidthC);
    ShVal = MIRBuilder.buildInstr(ShOpc, {DstTy}, {Src, ShAmt}).getReg(0);
    auto RevAmt = MIRBuilder.buildSub(AmtTy, BitWidthMinusOneC, ShAmt);
    auto One = MIRBuilder.buildConstant(AmtTy, 1);
    auto Inner = MIRBuilder.buildInstr(RevShiftOpc, {DstTy}, {Src, One});
    RevShiftVal =
        MIRBuilder.buildInstr(RevShiftOpc, {DstTy}, {Inner, RevAmt}).getReg(0);
  }
  MIRBuilder.buildOr(Dst, ShVal, RevShiftVal);
  MI.eraseFromParent();
  return Legalized;
}

// Expand s32 = G_UITOFP s64 using bit operations to an IEEE float
// representation.
LegalizerHelper::LegalizeResult
LegalizerHelper::lowerU64ToF32BitOps(MachineInstr &MI) {
  auto [Dst, Src] = MI.getFirst2Regs();
  const LLT S64 = LLT::scalar(64);
  const LLT S32 = LLT::scalar(32);
  const LLT S1 = LLT::scalar(1);

  assert(MRI.getType(Src) == S64 && MRI.getType(Dst) == S32);

  // unsigned cul2f(ulong u) {
  //   uint lz = clz(u);
  //   uint e = (u != 0) ? 127U + 63U - lz : 0;
  //   u = (u << lz) & 0x7fffffffffffffffUL;
  //   ulong t = u & 0xffffffffffUL;
  //   uint v = (e << 23) | (uint)(u >> 40);
  //   uint r = t > 0x8000000000UL ? 1U : (t == 0x8000000000UL ? v & 1U : 0U);
  //   return as_float(v + r);
  // }

  auto Zero32 = MIRBuilder.buildConstant(S32, 0);
  auto Zero64 = MIRBuilder.buildConstant(S64, 0);

  auto LZ = MIRBuilder.buildCTLZ_ZERO_UNDEF(S32, Src);

  auto K = MIRBuilder.buildConstant(S32, 127U + 63U);
  auto Sub = MIRBuilder.buildSub(S32, K, LZ);

  auto NotZero = MIRBuilder.buildICmp(CmpInst::ICMP_NE, S1, Src, Zero64);
  auto E = MIRBuilder.buildSelect(S32, NotZero, Sub, Zero32);

  auto Mask0 = MIRBuilder.buildConstant(S64, (-1ULL) >> 1);
  auto ShlLZ = MIRBuilder.buildShl(S64, Src, LZ);

  auto U = MIRBuilder.buildAnd(S64, ShlLZ, Mask0);

  auto Mask1 = MIRBuilder.buildConstant(S64, 0xffffffffffULL);
  auto T = MIRBuilder.buildAnd(S64, U, Mask1);

  auto UShl = MIRBuilder.buildLShr(S64, U, MIRBuilder.buildConstant(S64, 40));
  auto ShlE = MIRBuilder.buildShl(S32, E, MIRBuilder.buildConstant(S32, 23));
  auto V = MIRBuilder.buildOr(S32, ShlE, MIRBuilder.buildTrunc(S32, UShl));

  auto C = MIRBuilder.buildConstant(S64, 0x8000000000ULL);
  auto RCmp = MIRBuilder.buildICmp(CmpInst::ICMP_UGT, S1, T, C);
  auto TCmp = MIRBuilder.buildICmp(CmpInst::ICMP_EQ, S1, T, C);
  auto One = MIRBuilder.buildConstant(S32, 1);

  auto VTrunc1 = MIRBuilder.buildAnd(S32, V, One);
  auto Select0 = MIRBuilder.buildSelect(S32, TCmp, VTrunc1, Zero32);
  auto R = MIRBuilder.buildSelect(S32, RCmp, One, Select0);
  MIRBuilder.buildAdd(Dst, V, R);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerUITOFP(MachineInstr &MI) {
  auto [Dst, DstTy, Src, SrcTy] = MI.getFirst2RegLLTs();

  if (SrcTy == LLT::scalar(1)) {
    auto True = MIRBuilder.buildFConstant(DstTy, 1.0);
    auto False = MIRBuilder.buildFConstant(DstTy, 0.0);
    MIRBuilder.buildSelect(Dst, Src, True, False);
    MI.eraseFromParent();
    return Legalized;
  }

  if (SrcTy != LLT::scalar(64))
    return UnableToLegalize;

  if (DstTy == LLT::scalar(32)) {
    // TODO: SelectionDAG has several alternative expansions to port which may
    // be more reasonble depending on the available instructions. If a target
    // has sitofp, does not have CTLZ, or can efficiently use f64 as an
    // intermediate type, this is probably worse.
    return lowerU64ToF32BitOps(MI);
  }

  return UnableToLegalize;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerSITOFP(MachineInstr &MI) {
  auto [Dst, DstTy, Src, SrcTy] = MI.getFirst2RegLLTs();

  const LLT S64 = LLT::scalar(64);
  const LLT S32 = LLT::scalar(32);
  const LLT S1 = LLT::scalar(1);

  if (SrcTy == S1) {
    auto True = MIRBuilder.buildFConstant(DstTy, -1.0);
    auto False = MIRBuilder.buildFConstant(DstTy, 0.0);
    MIRBuilder.buildSelect(Dst, Src, True, False);
    MI.eraseFromParent();
    return Legalized;
  }

  if (SrcTy != S64)
    return UnableToLegalize;

  if (DstTy == S32) {
    // signed cl2f(long l) {
    //   long s = l >> 63;
    //   float r = cul2f((l + s) ^ s);
    //   return s ? -r : r;
    // }
    Register L = Src;
    auto SignBit = MIRBuilder.buildConstant(S64, 63);
    auto S = MIRBuilder.buildAShr(S64, L, SignBit);

    auto LPlusS = MIRBuilder.buildAdd(S64, L, S);
    auto Xor = MIRBuilder.buildXor(S64, LPlusS, S);
    auto R = MIRBuilder.buildUITOFP(S32, Xor);

    auto RNeg = MIRBuilder.buildFNeg(S32, R);
    auto SignNotZero = MIRBuilder.buildICmp(CmpInst::ICMP_NE, S1, S,
                                            MIRBuilder.buildConstant(S64, 0));
    MIRBuilder.buildSelect(Dst, SignNotZero, RNeg, R);
    MI.eraseFromParent();
    return Legalized;
  }

  return UnableToLegalize;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerFPTOUI(MachineInstr &MI) {
  auto [Dst, DstTy, Src, SrcTy] = MI.getFirst2RegLLTs();
  const LLT S64 = LLT::scalar(64);
  const LLT S32 = LLT::scalar(32);

  if (SrcTy != S64 && SrcTy != S32)
    return UnableToLegalize;
  if (DstTy != S32 && DstTy != S64)
    return UnableToLegalize;

  // FPTOSI gives same result as FPTOUI for positive signed integers.
  // FPTOUI needs to deal with fp values that convert to unsigned integers
  // greater or equal to 2^31 for float or 2^63 for double. For brevity 2^Exp.

  APInt TwoPExpInt = APInt::getSignMask(DstTy.getSizeInBits());
  APFloat TwoPExpFP(SrcTy.getSizeInBits() == 32 ? APFloat::IEEEsingle()
                                                : APFloat::IEEEdouble(),
                    APInt::getZero(SrcTy.getSizeInBits()));
  TwoPExpFP.convertFromAPInt(TwoPExpInt, false, APFloat::rmNearestTiesToEven);

  MachineInstrBuilder FPTOSI = MIRBuilder.buildFPTOSI(DstTy, Src);

  MachineInstrBuilder Threshold = MIRBuilder.buildFConstant(SrcTy, TwoPExpFP);
  // For fp Value greater or equal to Threshold(2^Exp), we use FPTOSI on
  // (Value - 2^Exp) and add 2^Exp by setting highest bit in result to 1.
  MachineInstrBuilder FSub = MIRBuilder.buildFSub(SrcTy, Src, Threshold);
  MachineInstrBuilder ResLowBits = MIRBuilder.buildFPTOSI(DstTy, FSub);
  MachineInstrBuilder ResHighBit = MIRBuilder.buildConstant(DstTy, TwoPExpInt);
  MachineInstrBuilder Res = MIRBuilder.buildXor(DstTy, ResLowBits, ResHighBit);

  const LLT S1 = LLT::scalar(1);

  MachineInstrBuilder FCMP =
      MIRBuilder.buildFCmp(CmpInst::FCMP_ULT, S1, Src, Threshold);
  MIRBuilder.buildSelect(Dst, FCMP, FPTOSI, Res);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerFPTOSI(MachineInstr &MI) {
  auto [Dst, DstTy, Src, SrcTy] = MI.getFirst2RegLLTs();
  const LLT S64 = LLT::scalar(64);
  const LLT S32 = LLT::scalar(32);

  // FIXME: Only f32 to i64 conversions are supported.
  if (SrcTy.getScalarType() != S32 || DstTy.getScalarType() != S64)
    return UnableToLegalize;

  // Expand f32 -> i64 conversion
  // This algorithm comes from compiler-rt's implementation of fixsfdi:
  // https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/builtins/fixsfdi.c

  unsigned SrcEltBits = SrcTy.getScalarSizeInBits();

  auto ExponentMask = MIRBuilder.buildConstant(SrcTy, 0x7F800000);
  auto ExponentLoBit = MIRBuilder.buildConstant(SrcTy, 23);

  auto AndExpMask = MIRBuilder.buildAnd(SrcTy, Src, ExponentMask);
  auto ExponentBits = MIRBuilder.buildLShr(SrcTy, AndExpMask, ExponentLoBit);

  auto SignMask = MIRBuilder.buildConstant(SrcTy,
                                           APInt::getSignMask(SrcEltBits));
  auto AndSignMask = MIRBuilder.buildAnd(SrcTy, Src, SignMask);
  auto SignLowBit = MIRBuilder.buildConstant(SrcTy, SrcEltBits - 1);
  auto Sign = MIRBuilder.buildAShr(SrcTy, AndSignMask, SignLowBit);
  Sign = MIRBuilder.buildSExt(DstTy, Sign);

  auto MantissaMask = MIRBuilder.buildConstant(SrcTy, 0x007FFFFF);
  auto AndMantissaMask = MIRBuilder.buildAnd(SrcTy, Src, MantissaMask);
  auto K = MIRBuilder.buildConstant(SrcTy, 0x00800000);

  auto R = MIRBuilder.buildOr(SrcTy, AndMantissaMask, K);
  R = MIRBuilder.buildZExt(DstTy, R);

  auto Bias = MIRBuilder.buildConstant(SrcTy, 127);
  auto Exponent = MIRBuilder.buildSub(SrcTy, ExponentBits, Bias);
  auto SubExponent = MIRBuilder.buildSub(SrcTy, Exponent, ExponentLoBit);
  auto ExponentSub = MIRBuilder.buildSub(SrcTy, ExponentLoBit, Exponent);

  auto Shl = MIRBuilder.buildShl(DstTy, R, SubExponent);
  auto Srl = MIRBuilder.buildLShr(DstTy, R, ExponentSub);

  const LLT S1 = LLT::scalar(1);
  auto CmpGt = MIRBuilder.buildICmp(CmpInst::ICMP_SGT,
                                    S1, Exponent, ExponentLoBit);

  R = MIRBuilder.buildSelect(DstTy, CmpGt, Shl, Srl);

  auto XorSign = MIRBuilder.buildXor(DstTy, R, Sign);
  auto Ret = MIRBuilder.buildSub(DstTy, XorSign, Sign);

  auto ZeroSrcTy = MIRBuilder.buildConstant(SrcTy, 0);

  auto ExponentLt0 = MIRBuilder.buildICmp(CmpInst::ICMP_SLT,
                                          S1, Exponent, ZeroSrcTy);

  auto ZeroDstTy = MIRBuilder.buildConstant(DstTy, 0);
  MIRBuilder.buildSelect(Dst, ExponentLt0, ZeroDstTy, Ret);

  MI.eraseFromParent();
  return Legalized;
}

// f64 -> f16 conversion using round-to-nearest-even rounding mode.
LegalizerHelper::LegalizeResult
LegalizerHelper::lowerFPTRUNC_F64_TO_F16(MachineInstr &MI) {
  const LLT S1 = LLT::scalar(1);
  const LLT S32 = LLT::scalar(32);

  auto [Dst, Src] = MI.getFirst2Regs();
  assert(MRI.getType(Dst).getScalarType() == LLT::scalar(16) &&
         MRI.getType(Src).getScalarType() == LLT::scalar(64));

  if (MRI.getType(Src).isVector()) // TODO: Handle vectors directly.
    return UnableToLegalize;

  if (MIRBuilder.getMF().getTarget().Options.UnsafeFPMath) {
    unsigned Flags = MI.getFlags();
    auto Src32 = MIRBuilder.buildFPTrunc(S32, Src, Flags);
    MIRBuilder.buildFPTrunc(Dst, Src32, Flags);
    MI.eraseFromParent();
    return Legalized;
  }

  const unsigned ExpMask = 0x7ff;
  const unsigned ExpBiasf64 = 1023;
  const unsigned ExpBiasf16 = 15;

  auto Unmerge = MIRBuilder.buildUnmerge(S32, Src);
  Register U = Unmerge.getReg(0);
  Register UH = Unmerge.getReg(1);

  auto E = MIRBuilder.buildLShr(S32, UH, MIRBuilder.buildConstant(S32, 20));
  E = MIRBuilder.buildAnd(S32, E, MIRBuilder.buildConstant(S32, ExpMask));

  // Subtract the fp64 exponent bias (1023) to get the real exponent and
  // add the f16 bias (15) to get the biased exponent for the f16 format.
  E = MIRBuilder.buildAdd(
    S32, E, MIRBuilder.buildConstant(S32, -ExpBiasf64 + ExpBiasf16));

  auto M = MIRBuilder.buildLShr(S32, UH, MIRBuilder.buildConstant(S32, 8));
  M = MIRBuilder.buildAnd(S32, M, MIRBuilder.buildConstant(S32, 0xffe));

  auto MaskedSig = MIRBuilder.buildAnd(S32, UH,
                                       MIRBuilder.buildConstant(S32, 0x1ff));
  MaskedSig = MIRBuilder.buildOr(S32, MaskedSig, U);

  auto Zero = MIRBuilder.buildConstant(S32, 0);
  auto SigCmpNE0 = MIRBuilder.buildICmp(CmpInst::ICMP_NE, S1, MaskedSig, Zero);
  auto Lo40Set = MIRBuilder.buildZExt(S32, SigCmpNE0);
  M = MIRBuilder.buildOr(S32, M, Lo40Set);

  // (M != 0 ? 0x0200 : 0) | 0x7c00;
  auto Bits0x200 = MIRBuilder.buildConstant(S32, 0x0200);
  auto CmpM_NE0 = MIRBuilder.buildICmp(CmpInst::ICMP_NE, S1, M, Zero);
  auto SelectCC = MIRBuilder.buildSelect(S32, CmpM_NE0, Bits0x200, Zero);

  auto Bits0x7c00 = MIRBuilder.buildConstant(S32, 0x7c00);
  auto I = MIRBuilder.buildOr(S32, SelectCC, Bits0x7c00);

  // N = M | (E << 12);
  auto EShl12 = MIRBuilder.buildShl(S32, E, MIRBuilder.buildConstant(S32, 12));
  auto N = MIRBuilder.buildOr(S32, M, EShl12);

  // B = clamp(1-E, 0, 13);
  auto One = MIRBuilder.buildConstant(S32, 1);
  auto OneSubExp = MIRBuilder.buildSub(S32, One, E);
  auto B = MIRBuilder.buildSMax(S32, OneSubExp, Zero);
  B = MIRBuilder.buildSMin(S32, B, MIRBuilder.buildConstant(S32, 13));

  auto SigSetHigh = MIRBuilder.buildOr(S32, M,
                                       MIRBuilder.buildConstant(S32, 0x1000));

  auto D = MIRBuilder.buildLShr(S32, SigSetHigh, B);
  auto D0 = MIRBuilder.buildShl(S32, D, B);

  auto D0_NE_SigSetHigh = MIRBuilder.buildICmp(CmpInst::ICMP_NE, S1,
                                             D0, SigSetHigh);
  auto D1 = MIRBuilder.buildZExt(S32, D0_NE_SigSetHigh);
  D = MIRBuilder.buildOr(S32, D, D1);

  auto CmpELtOne = MIRBuilder.buildICmp(CmpInst::ICMP_SLT, S1, E, One);
  auto V = MIRBuilder.buildSelect(S32, CmpELtOne, D, N);

  auto VLow3 = MIRBuilder.buildAnd(S32, V, MIRBuilder.buildConstant(S32, 7));
  V = MIRBuilder.buildLShr(S32, V, MIRBuilder.buildConstant(S32, 2));

  auto VLow3Eq3 = MIRBuilder.buildICmp(CmpInst::ICMP_EQ, S1, VLow3,
                                       MIRBuilder.buildConstant(S32, 3));
  auto V0 = MIRBuilder.buildZExt(S32, VLow3Eq3);

  auto VLow3Gt5 = MIRBuilder.buildICmp(CmpInst::ICMP_SGT, S1, VLow3,
                                       MIRBuilder.buildConstant(S32, 5));
  auto V1 = MIRBuilder.buildZExt(S32, VLow3Gt5);

  V1 = MIRBuilder.buildOr(S32, V0, V1);
  V = MIRBuilder.buildAdd(S32, V, V1);

  auto CmpEGt30 = MIRBuilder.buildICmp(CmpInst::ICMP_SGT,  S1,
                                       E, MIRBuilder.buildConstant(S32, 30));
  V = MIRBuilder.buildSelect(S32, CmpEGt30,
                             MIRBuilder.buildConstant(S32, 0x7c00), V);

  auto CmpEGt1039 = MIRBuilder.buildICmp(CmpInst::ICMP_EQ, S1,
                                         E, MIRBuilder.buildConstant(S32, 1039));
  V = MIRBuilder.buildSelect(S32, CmpEGt1039, I, V);

  // Extract the sign bit.
  auto Sign = MIRBuilder.buildLShr(S32, UH, MIRBuilder.buildConstant(S32, 16));
  Sign = MIRBuilder.buildAnd(S32, Sign, MIRBuilder.buildConstant(S32, 0x8000));

  // Insert the sign bit
  V = MIRBuilder.buildOr(S32, Sign, V);

  MIRBuilder.buildTrunc(Dst, V);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerFPTRUNC(MachineInstr &MI) {
  auto [DstTy, SrcTy] = MI.getFirst2LLTs();
  const LLT S64 = LLT::scalar(64);
  const LLT S16 = LLT::scalar(16);

  if (DstTy.getScalarType() == S16 && SrcTy.getScalarType() == S64)
    return lowerFPTRUNC_F64_TO_F16(MI);

  return UnableToLegalize;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerFPOWI(MachineInstr &MI) {
  auto [Dst, Src0, Src1] = MI.getFirst3Regs();
  LLT Ty = MRI.getType(Dst);

  auto CvtSrc1 = MIRBuilder.buildSITOFP(Ty, Src1);
  MIRBuilder.buildFPow(Dst, Src0, CvtSrc1, MI.getFlags());
  MI.eraseFromParent();
  return Legalized;
}

static CmpInst::Predicate minMaxToCompare(unsigned Opc) {
  switch (Opc) {
  case TargetOpcode::G_SMIN:
    return CmpInst::ICMP_SLT;
  case TargetOpcode::G_SMAX:
    return CmpInst::ICMP_SGT;
  case TargetOpcode::G_UMIN:
    return CmpInst::ICMP_ULT;
  case TargetOpcode::G_UMAX:
    return CmpInst::ICMP_UGT;
  default:
    llvm_unreachable("not in integer min/max");
  }
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerMinMax(MachineInstr &MI) {
  auto [Dst, Src0, Src1] = MI.getFirst3Regs();

  const CmpInst::Predicate Pred = minMaxToCompare(MI.getOpcode());
  LLT CmpType = MRI.getType(Dst).changeElementSize(1);

  auto Cmp = MIRBuilder.buildICmp(Pred, CmpType, Src0, Src1);
  MIRBuilder.buildSelect(Dst, Cmp, Src0, Src1);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerThreewayCompare(MachineInstr &MI) {
  GSUCmp *Cmp = cast<GSUCmp>(&MI);

  Register Dst = Cmp->getReg(0);
  LLT DstTy = MRI.getType(Dst);
  LLT CmpTy = DstTy.changeElementSize(1);

  CmpInst::Predicate LTPredicate = Cmp->isSigned()
                                       ? CmpInst::Predicate::ICMP_SLT
                                       : CmpInst::Predicate::ICMP_ULT;
  CmpInst::Predicate GTPredicate = Cmp->isSigned()
                                       ? CmpInst::Predicate::ICMP_SGT
                                       : CmpInst::Predicate::ICMP_UGT;

  auto One = MIRBuilder.buildConstant(DstTy, 1);
  auto Zero = MIRBuilder.buildConstant(DstTy, 0);
  auto IsGT = MIRBuilder.buildICmp(GTPredicate, CmpTy, Cmp->getLHSReg(),
                                   Cmp->getRHSReg());
  auto SelectZeroOrOne = MIRBuilder.buildSelect(DstTy, IsGT, One, Zero);

  auto MinusOne = MIRBuilder.buildConstant(DstTy, -1);
  auto IsLT = MIRBuilder.buildICmp(LTPredicate, CmpTy, Cmp->getLHSReg(),
                                   Cmp->getRHSReg());
  MIRBuilder.buildSelect(Dst, IsLT, MinusOne, SelectZeroOrOne);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerFCopySign(MachineInstr &MI) {
  auto [Dst, DstTy, Src0, Src0Ty, Src1, Src1Ty] = MI.getFirst3RegLLTs();
  const int Src0Size = Src0Ty.getScalarSizeInBits();
  const int Src1Size = Src1Ty.getScalarSizeInBits();

  auto SignBitMask = MIRBuilder.buildConstant(
    Src0Ty, APInt::getSignMask(Src0Size));

  auto NotSignBitMask = MIRBuilder.buildConstant(
    Src0Ty, APInt::getLowBitsSet(Src0Size, Src0Size - 1));

  Register And0 = MIRBuilder.buildAnd(Src0Ty, Src0, NotSignBitMask).getReg(0);
  Register And1;
  if (Src0Ty == Src1Ty) {
    And1 = MIRBuilder.buildAnd(Src1Ty, Src1, SignBitMask).getReg(0);
  } else if (Src0Size > Src1Size) {
    auto ShiftAmt = MIRBuilder.buildConstant(Src0Ty, Src0Size - Src1Size);
    auto Zext = MIRBuilder.buildZExt(Src0Ty, Src1);
    auto Shift = MIRBuilder.buildShl(Src0Ty, Zext, ShiftAmt);
    And1 = MIRBuilder.buildAnd(Src0Ty, Shift, SignBitMask).getReg(0);
  } else {
    auto ShiftAmt = MIRBuilder.buildConstant(Src1Ty, Src1Size - Src0Size);
    auto Shift = MIRBuilder.buildLShr(Src1Ty, Src1, ShiftAmt);
    auto Trunc = MIRBuilder.buildTrunc(Src0Ty, Shift);
    And1 = MIRBuilder.buildAnd(Src0Ty, Trunc, SignBitMask).getReg(0);
  }

  // Be careful about setting nsz/nnan/ninf on every instruction, since the
  // constants are a nan and -0.0, but the final result should preserve
  // everything.
  unsigned Flags = MI.getFlags();

  // We masked the sign bit and the not-sign bit, so these are disjoint.
  Flags |= MachineInstr::Disjoint;

  MIRBuilder.buildOr(Dst, And0, And1, Flags);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerFMinNumMaxNum(MachineInstr &MI) {
  unsigned NewOp = MI.getOpcode() == TargetOpcode::G_FMINNUM ?
    TargetOpcode::G_FMINNUM_IEEE : TargetOpcode::G_FMAXNUM_IEEE;

  auto [Dst, Src0, Src1] = MI.getFirst3Regs();
  LLT Ty = MRI.getType(Dst);

  if (!MI.getFlag(MachineInstr::FmNoNans)) {
    // Insert canonicalizes if it's possible we need to quiet to get correct
    // sNaN behavior.

    // Note this must be done here, and not as an optimization combine in the
    // absence of a dedicate quiet-snan instruction as we're using an
    // omni-purpose G_FCANONICALIZE.
    if (!isKnownNeverSNaN(Src0, MRI))
      Src0 = MIRBuilder.buildFCanonicalize(Ty, Src0, MI.getFlags()).getReg(0);

    if (!isKnownNeverSNaN(Src1, MRI))
      Src1 = MIRBuilder.buildFCanonicalize(Ty, Src1, MI.getFlags()).getReg(0);
  }

  // If there are no nans, it's safe to simply replace this with the non-IEEE
  // version.
  MIRBuilder.buildInstr(NewOp, {Dst}, {Src0, Src1}, MI.getFlags());
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerFMad(MachineInstr &MI) {
  // Expand G_FMAD a, b, c -> G_FADD (G_FMUL a, b), c
  Register DstReg = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(DstReg);
  unsigned Flags = MI.getFlags();

  auto Mul = MIRBuilder.buildFMul(Ty, MI.getOperand(1), MI.getOperand(2),
                                  Flags);
  MIRBuilder.buildFAdd(DstReg, Mul, MI.getOperand(3), Flags);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerIntrinsicRound(MachineInstr &MI) {
  auto [DstReg, X] = MI.getFirst2Regs();
  const unsigned Flags = MI.getFlags();
  const LLT Ty = MRI.getType(DstReg);
  const LLT CondTy = Ty.changeElementSize(1);

  // round(x) =>
  //  t = trunc(x);
  //  d = fabs(x - t);
  //  o = copysign(d >= 0.5 ? 1.0 : 0.0, x);
  //  return t + o;

  auto T = MIRBuilder.buildIntrinsicTrunc(Ty, X, Flags);

  auto Diff = MIRBuilder.buildFSub(Ty, X, T, Flags);
  auto AbsDiff = MIRBuilder.buildFAbs(Ty, Diff, Flags);

  auto Half = MIRBuilder.buildFConstant(Ty, 0.5);
  auto Cmp =
      MIRBuilder.buildFCmp(CmpInst::FCMP_OGE, CondTy, AbsDiff, Half, Flags);

  // Could emit G_UITOFP instead
  auto One = MIRBuilder.buildFConstant(Ty, 1.0);
  auto Zero = MIRBuilder.buildFConstant(Ty, 0.0);
  auto BoolFP = MIRBuilder.buildSelect(Ty, Cmp, One, Zero);
  auto SignedOffset = MIRBuilder.buildFCopysign(Ty, BoolFP, X);

  MIRBuilder.buildFAdd(DstReg, T, SignedOffset, Flags);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerFFloor(MachineInstr &MI) {
  auto [DstReg, SrcReg] = MI.getFirst2Regs();
  unsigned Flags = MI.getFlags();
  LLT Ty = MRI.getType(DstReg);
  const LLT CondTy = Ty.changeElementSize(1);

  // result = trunc(src);
  // if (src < 0.0 && src != result)
  //   result += -1.0.

  auto Trunc = MIRBuilder.buildIntrinsicTrunc(Ty, SrcReg, Flags);
  auto Zero = MIRBuilder.buildFConstant(Ty, 0.0);

  auto Lt0 = MIRBuilder.buildFCmp(CmpInst::FCMP_OLT, CondTy,
                                  SrcReg, Zero, Flags);
  auto NeTrunc = MIRBuilder.buildFCmp(CmpInst::FCMP_ONE, CondTy,
                                      SrcReg, Trunc, Flags);
  auto And = MIRBuilder.buildAnd(CondTy, Lt0, NeTrunc);
  auto AddVal = MIRBuilder.buildSITOFP(Ty, And);

  MIRBuilder.buildFAdd(DstReg, Trunc, AddVal, Flags);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerMergeValues(MachineInstr &MI) {
  const unsigned NumOps = MI.getNumOperands();
  auto [DstReg, DstTy, Src0Reg, Src0Ty] = MI.getFirst2RegLLTs();
  unsigned PartSize = Src0Ty.getSizeInBits();

  LLT WideTy = LLT::scalar(DstTy.getSizeInBits());
  Register ResultReg = MIRBuilder.buildZExt(WideTy, Src0Reg).getReg(0);

  for (unsigned I = 2; I != NumOps; ++I) {
    const unsigned Offset = (I - 1) * PartSize;

    Register SrcReg = MI.getOperand(I).getReg();
    auto ZextInput = MIRBuilder.buildZExt(WideTy, SrcReg);

    Register NextResult = I + 1 == NumOps && WideTy == DstTy ? DstReg :
      MRI.createGenericVirtualRegister(WideTy);

    auto ShiftAmt = MIRBuilder.buildConstant(WideTy, Offset);
    auto Shl = MIRBuilder.buildShl(WideTy, ZextInput, ShiftAmt);
    MIRBuilder.buildOr(NextResult, ResultReg, Shl);
    ResultReg = NextResult;
  }

  if (DstTy.isPointer()) {
    if (MIRBuilder.getDataLayout().isNonIntegralAddressSpace(
          DstTy.getAddressSpace())) {
      LLVM_DEBUG(dbgs() << "Not casting nonintegral address space\n");
      return UnableToLegalize;
    }

    MIRBuilder.buildIntToPtr(DstReg, ResultReg);
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerUnmergeValues(MachineInstr &MI) {
  const unsigned NumDst = MI.getNumOperands() - 1;
  Register SrcReg = MI.getOperand(NumDst).getReg();
  Register Dst0Reg = MI.getOperand(0).getReg();
  LLT DstTy = MRI.getType(Dst0Reg);
  if (DstTy.isPointer())
    return UnableToLegalize; // TODO

  SrcReg = coerceToScalar(SrcReg);
  if (!SrcReg)
    return UnableToLegalize;

  // Expand scalarizing unmerge as bitcast to integer and shift.
  LLT IntTy = MRI.getType(SrcReg);

  MIRBuilder.buildTrunc(Dst0Reg, SrcReg);

  const unsigned DstSize = DstTy.getSizeInBits();
  unsigned Offset = DstSize;
  for (unsigned I = 1; I != NumDst; ++I, Offset += DstSize) {
    auto ShiftAmt = MIRBuilder.buildConstant(IntTy, Offset);
    auto Shift = MIRBuilder.buildLShr(IntTy, SrcReg, ShiftAmt);
    MIRBuilder.buildTrunc(MI.getOperand(I), Shift);
  }

  MI.eraseFromParent();
  return Legalized;
}

/// Lower a vector extract or insert by writing the vector to a stack temporary
/// and reloading the element or vector.
///
/// %dst = G_EXTRACT_VECTOR_ELT %vec, %idx
///  =>
///  %stack_temp = G_FRAME_INDEX
///  G_STORE %vec, %stack_temp
///  %idx = clamp(%idx, %vec.getNumElements())
///  %element_ptr = G_PTR_ADD %stack_temp, %idx
///  %dst = G_LOAD %element_ptr
LegalizerHelper::LegalizeResult
LegalizerHelper::lowerExtractInsertVectorElt(MachineInstr &MI) {
  Register DstReg = MI.getOperand(0).getReg();
  Register SrcVec = MI.getOperand(1).getReg();
  Register InsertVal;
  if (MI.getOpcode() == TargetOpcode::G_INSERT_VECTOR_ELT)
    InsertVal = MI.getOperand(2).getReg();

  Register Idx = MI.getOperand(MI.getNumOperands() - 1).getReg();

  LLT VecTy = MRI.getType(SrcVec);
  LLT EltTy = VecTy.getElementType();
  unsigned NumElts = VecTy.getNumElements();

  int64_t IdxVal;
  if (mi_match(Idx, MRI, m_ICst(IdxVal)) && IdxVal <= NumElts) {
    SmallVector<Register, 8> SrcRegs;
    extractParts(SrcVec, EltTy, NumElts, SrcRegs, MIRBuilder, MRI);

    if (InsertVal) {
      SrcRegs[IdxVal] = MI.getOperand(2).getReg();
      MIRBuilder.buildMergeLikeInstr(DstReg, SrcRegs);
    } else {
      MIRBuilder.buildCopy(DstReg, SrcRegs[IdxVal]);
    }

    MI.eraseFromParent();
    return Legalized;
  }

  if (!EltTy.isByteSized()) { // Not implemented.
    LLVM_DEBUG(dbgs() << "Can't handle non-byte element vectors yet\n");
    return UnableToLegalize;
  }

  unsigned EltBytes = EltTy.getSizeInBytes();
  Align VecAlign = getStackTemporaryAlignment(VecTy);
  Align EltAlign;

  MachinePointerInfo PtrInfo;
  auto StackTemp = createStackTemporary(
      TypeSize::getFixed(VecTy.getSizeInBytes()), VecAlign, PtrInfo);
  MIRBuilder.buildStore(SrcVec, StackTemp, PtrInfo, VecAlign);

  // Get the pointer to the element, and be sure not to hit undefined behavior
  // if the index is out of bounds.
  Register EltPtr = getVectorElementPointer(StackTemp.getReg(0), VecTy, Idx);

  if (mi_match(Idx, MRI, m_ICst(IdxVal))) {
    int64_t Offset = IdxVal * EltBytes;
    PtrInfo = PtrInfo.getWithOffset(Offset);
    EltAlign = commonAlignment(VecAlign, Offset);
  } else {
    // We lose information with a variable offset.
    EltAlign = getStackTemporaryAlignment(EltTy);
    PtrInfo = MachinePointerInfo(MRI.getType(EltPtr).getAddressSpace());
  }

  if (InsertVal) {
    // Write the inserted element
    MIRBuilder.buildStore(InsertVal, EltPtr, PtrInfo, EltAlign);

    // Reload the whole vector.
    MIRBuilder.buildLoad(DstReg, StackTemp, PtrInfo, VecAlign);
  } else {
    MIRBuilder.buildLoad(DstReg, EltPtr, PtrInfo, EltAlign);
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerShuffleVector(MachineInstr &MI) {
  auto [DstReg, DstTy, Src0Reg, Src0Ty, Src1Reg, Src1Ty] =
      MI.getFirst3RegLLTs();
  LLT IdxTy = LLT::scalar(32);

  ArrayRef<int> Mask = MI.getOperand(3).getShuffleMask();
  Register Undef;
  SmallVector<Register, 32> BuildVec;
  LLT EltTy = DstTy.getScalarType();

  for (int Idx : Mask) {
    if (Idx < 0) {
      if (!Undef.isValid())
        Undef = MIRBuilder.buildUndef(EltTy).getReg(0);
      BuildVec.push_back(Undef);
      continue;
    }

    if (Src0Ty.isScalar()) {
      BuildVec.push_back(Idx == 0 ? Src0Reg : Src1Reg);
    } else {
      int NumElts = Src0Ty.getNumElements();
      Register SrcVec = Idx < NumElts ? Src0Reg : Src1Reg;
      int ExtractIdx = Idx < NumElts ? Idx : Idx - NumElts;
      auto IdxK = MIRBuilder.buildConstant(IdxTy, ExtractIdx);
      auto Extract = MIRBuilder.buildExtractVectorElement(EltTy, SrcVec, IdxK);
      BuildVec.push_back(Extract.getReg(0));
    }
  }

  if (DstTy.isScalar())
    MIRBuilder.buildCopy(DstReg, BuildVec[0]);
  else
    MIRBuilder.buildBuildVector(DstReg, BuildVec);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerVECTOR_COMPRESS(llvm::MachineInstr &MI) {
  auto [Dst, DstTy, Vec, VecTy, Mask, MaskTy, Passthru, PassthruTy] =
      MI.getFirst4RegLLTs();

  if (VecTy.isScalableVector())
    report_fatal_error("Cannot expand masked_compress for scalable vectors.");

  Align VecAlign = getStackTemporaryAlignment(VecTy);
  MachinePointerInfo PtrInfo;
  Register StackPtr =
      createStackTemporary(TypeSize::getFixed(VecTy.getSizeInBytes()), VecAlign,
                           PtrInfo)
          .getReg(0);
  MachinePointerInfo ValPtrInfo =
      MachinePointerInfo::getUnknownStack(*MI.getMF());

  LLT IdxTy = LLT::scalar(32);
  LLT ValTy = VecTy.getElementType();
  Align ValAlign = getStackTemporaryAlignment(ValTy);

  auto OutPos = MIRBuilder.buildConstant(IdxTy, 0);

  bool HasPassthru =
      MRI.getVRegDef(Passthru)->getOpcode() != TargetOpcode::G_IMPLICIT_DEF;

  if (HasPassthru)
    MIRBuilder.buildStore(Passthru, StackPtr, PtrInfo, VecAlign);

  Register LastWriteVal;
  std::optional<APInt> PassthruSplatVal =
      isConstantOrConstantSplatVector(*MRI.getVRegDef(Passthru), MRI);

  if (PassthruSplatVal.has_value()) {
    LastWriteVal =
        MIRBuilder.buildConstant(ValTy, PassthruSplatVal.value()).getReg(0);
  } else if (HasPassthru) {
    auto Popcount = MIRBuilder.buildZExt(MaskTy.changeElementSize(32), Mask);
    Popcount = MIRBuilder.buildInstr(TargetOpcode::G_VECREDUCE_ADD,
                                     {LLT::scalar(32)}, {Popcount});

    Register LastElmtPtr =
        getVectorElementPointer(StackPtr, VecTy, Popcount.getReg(0));
    LastWriteVal =
        MIRBuilder.buildLoad(ValTy, LastElmtPtr, ValPtrInfo, ValAlign)
            .getReg(0);
  }

  unsigned NumElmts = VecTy.getNumElements();
  for (unsigned I = 0; I < NumElmts; ++I) {
    auto Idx = MIRBuilder.buildConstant(IdxTy, I);
    auto Val = MIRBuilder.buildExtractVectorElement(ValTy, Vec, Idx);
    Register ElmtPtr =
        getVectorElementPointer(StackPtr, VecTy, OutPos.getReg(0));
    MIRBuilder.buildStore(Val, ElmtPtr, ValPtrInfo, ValAlign);

    LLT MaskITy = MaskTy.getElementType();
    auto MaskI = MIRBuilder.buildExtractVectorElement(MaskITy, Mask, Idx);
    if (MaskITy.getSizeInBits() > 1)
      MaskI = MIRBuilder.buildTrunc(LLT::scalar(1), MaskI);

    MaskI = MIRBuilder.buildZExt(IdxTy, MaskI);
    OutPos = MIRBuilder.buildAdd(IdxTy, OutPos, MaskI);

    if (HasPassthru && I == NumElmts - 1) {
      auto EndOfVector =
          MIRBuilder.buildConstant(IdxTy, VecTy.getNumElements() - 1);
      auto AllLanesSelected = MIRBuilder.buildICmp(
          CmpInst::ICMP_UGT, LLT::scalar(1), OutPos, EndOfVector);
      OutPos = MIRBuilder.buildInstr(TargetOpcode::G_UMIN, {IdxTy},
                                     {OutPos, EndOfVector});
      ElmtPtr = getVectorElementPointer(StackPtr, VecTy, OutPos.getReg(0));

      LastWriteVal =
          MIRBuilder.buildSelect(ValTy, AllLanesSelected, Val, LastWriteVal)
              .getReg(0);
      MIRBuilder.buildStore(LastWriteVal, ElmtPtr, ValPtrInfo, ValAlign);
    }
  }

  // TODO: Use StackPtr's FrameIndex alignment.
  MIRBuilder.buildLoad(Dst, StackPtr, PtrInfo, VecAlign);

  MI.eraseFromParent();
  return Legalized;
}

Register LegalizerHelper::getDynStackAllocTargetPtr(Register SPReg,
                                                    Register AllocSize,
                                                    Align Alignment,
                                                    LLT PtrTy) {
  LLT IntPtrTy = LLT::scalar(PtrTy.getSizeInBits());

  auto SPTmp = MIRBuilder.buildCopy(PtrTy, SPReg);
  SPTmp = MIRBuilder.buildCast(IntPtrTy, SPTmp);

  // Subtract the final alloc from the SP. We use G_PTRTOINT here so we don't
  // have to generate an extra instruction to negate the alloc and then use
  // G_PTR_ADD to add the negative offset.
  auto Alloc = MIRBuilder.buildSub(IntPtrTy, SPTmp, AllocSize);
  if (Alignment > Align(1)) {
    APInt AlignMask(IntPtrTy.getSizeInBits(), Alignment.value(), true);
    AlignMask.negate();
    auto AlignCst = MIRBuilder.buildConstant(IntPtrTy, AlignMask);
    Alloc = MIRBuilder.buildAnd(IntPtrTy, Alloc, AlignCst);
  }

  return MIRBuilder.buildCast(PtrTy, Alloc).getReg(0);
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerDynStackAlloc(MachineInstr &MI) {
  const auto &MF = *MI.getMF();
  const auto &TFI = *MF.getSubtarget().getFrameLowering();
  if (TFI.getStackGrowthDirection() == TargetFrameLowering::StackGrowsUp)
    return UnableToLegalize;

  Register Dst = MI.getOperand(0).getReg();
  Register AllocSize = MI.getOperand(1).getReg();
  Align Alignment = assumeAligned(MI.getOperand(2).getImm());

  LLT PtrTy = MRI.getType(Dst);
  Register SPReg = TLI.getStackPointerRegisterToSaveRestore();
  Register SPTmp =
      getDynStackAllocTargetPtr(SPReg, AllocSize, Alignment, PtrTy);

  MIRBuilder.buildCopy(SPReg, SPTmp);
  MIRBuilder.buildCopy(Dst, SPTmp);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerStackSave(MachineInstr &MI) {
  Register StackPtr = TLI.getStackPointerRegisterToSaveRestore();
  if (!StackPtr)
    return UnableToLegalize;

  MIRBuilder.buildCopy(MI.getOperand(0), StackPtr);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerStackRestore(MachineInstr &MI) {
  Register StackPtr = TLI.getStackPointerRegisterToSaveRestore();
  if (!StackPtr)
    return UnableToLegalize;

  MIRBuilder.buildCopy(StackPtr, MI.getOperand(0));
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerExtract(MachineInstr &MI) {
  auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();
  unsigned Offset = MI.getOperand(2).getImm();

  // Extract sub-vector or one element
  if (SrcTy.isVector()) {
    unsigned SrcEltSize = SrcTy.getElementType().getSizeInBits();
    unsigned DstSize = DstTy.getSizeInBits();

    if ((Offset % SrcEltSize == 0) && (DstSize % SrcEltSize == 0) &&
        (Offset + DstSize <= SrcTy.getSizeInBits())) {
      // Unmerge and allow access to each Src element for the artifact combiner.
      auto Unmerge = MIRBuilder.buildUnmerge(SrcTy.getElementType(), SrcReg);

      // Take element(s) we need to extract and copy it (merge them).
      SmallVector<Register, 8> SubVectorElts;
      for (unsigned Idx = Offset / SrcEltSize;
           Idx < (Offset + DstSize) / SrcEltSize; ++Idx) {
        SubVectorElts.push_back(Unmerge.getReg(Idx));
      }
      if (SubVectorElts.size() == 1)
        MIRBuilder.buildCopy(DstReg, SubVectorElts[0]);
      else
        MIRBuilder.buildMergeLikeInstr(DstReg, SubVectorElts);

      MI.eraseFromParent();
      return Legalized;
    }
  }

  if (DstTy.isScalar() &&
      (SrcTy.isScalar() ||
       (SrcTy.isVector() && DstTy == SrcTy.getElementType()))) {
    LLT SrcIntTy = SrcTy;
    if (!SrcTy.isScalar()) {
      SrcIntTy = LLT::scalar(SrcTy.getSizeInBits());
      SrcReg = MIRBuilder.buildBitcast(SrcIntTy, SrcReg).getReg(0);
    }

    if (Offset == 0)
      MIRBuilder.buildTrunc(DstReg, SrcReg);
    else {
      auto ShiftAmt = MIRBuilder.buildConstant(SrcIntTy, Offset);
      auto Shr = MIRBuilder.buildLShr(SrcIntTy, SrcReg, ShiftAmt);
      MIRBuilder.buildTrunc(DstReg, Shr);
    }

    MI.eraseFromParent();
    return Legalized;
  }

  return UnableToLegalize;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerInsert(MachineInstr &MI) {
  auto [Dst, Src, InsertSrc] = MI.getFirst3Regs();
  uint64_t Offset = MI.getOperand(3).getImm();

  LLT DstTy = MRI.getType(Src);
  LLT InsertTy = MRI.getType(InsertSrc);

  // Insert sub-vector or one element
  if (DstTy.isVector() && !InsertTy.isPointer()) {
    LLT EltTy = DstTy.getElementType();
    unsigned EltSize = EltTy.getSizeInBits();
    unsigned InsertSize = InsertTy.getSizeInBits();

    if ((Offset % EltSize == 0) && (InsertSize % EltSize == 0) &&
        (Offset + InsertSize <= DstTy.getSizeInBits())) {
      auto UnmergeSrc = MIRBuilder.buildUnmerge(EltTy, Src);
      SmallVector<Register, 8> DstElts;
      unsigned Idx = 0;
      // Elements from Src before insert start Offset
      for (; Idx < Offset / EltSize; ++Idx) {
        DstElts.push_back(UnmergeSrc.getReg(Idx));
      }

      // Replace elements in Src with elements from InsertSrc
      if (InsertTy.getSizeInBits() > EltSize) {
        auto UnmergeInsertSrc = MIRBuilder.buildUnmerge(EltTy, InsertSrc);
        for (unsigned i = 0; Idx < (Offset + InsertSize) / EltSize;
             ++Idx, ++i) {
          DstElts.push_back(UnmergeInsertSrc.getReg(i));
        }
      } else {
        DstElts.push_back(InsertSrc);
        ++Idx;
      }

      // Remaining elements from Src after insert
      for (; Idx < DstTy.getNumElements(); ++Idx) {
        DstElts.push_back(UnmergeSrc.getReg(Idx));
      }

      MIRBuilder.buildMergeLikeInstr(Dst, DstElts);
      MI.eraseFromParent();
      return Legalized;
    }
  }

  if (InsertTy.isVector() ||
      (DstTy.isVector() && DstTy.getElementType() != InsertTy))
    return UnableToLegalize;

  const DataLayout &DL = MIRBuilder.getDataLayout();
  if ((DstTy.isPointer() &&
       DL.isNonIntegralAddressSpace(DstTy.getAddressSpace())) ||
      (InsertTy.isPointer() &&
       DL.isNonIntegralAddressSpace(InsertTy.getAddressSpace()))) {
    LLVM_DEBUG(dbgs() << "Not casting non-integral address space integer\n");
    return UnableToLegalize;
  }

  LLT IntDstTy = DstTy;

  if (!DstTy.isScalar()) {
    IntDstTy = LLT::scalar(DstTy.getSizeInBits());
    Src = MIRBuilder.buildCast(IntDstTy, Src).getReg(0);
  }

  if (!InsertTy.isScalar()) {
    const LLT IntInsertTy = LLT::scalar(InsertTy.getSizeInBits());
    InsertSrc = MIRBuilder.buildPtrToInt(IntInsertTy, InsertSrc).getReg(0);
  }

  Register ExtInsSrc = MIRBuilder.buildZExt(IntDstTy, InsertSrc).getReg(0);
  if (Offset != 0) {
    auto ShiftAmt = MIRBuilder.buildConstant(IntDstTy, Offset);
    ExtInsSrc = MIRBuilder.buildShl(IntDstTy, ExtInsSrc, ShiftAmt).getReg(0);
  }

  APInt MaskVal = APInt::getBitsSetWithWrap(
      DstTy.getSizeInBits(), Offset + InsertTy.getSizeInBits(), Offset);

  auto Mask = MIRBuilder.buildConstant(IntDstTy, MaskVal);
  auto MaskedSrc = MIRBuilder.buildAnd(IntDstTy, Src, Mask);
  auto Or = MIRBuilder.buildOr(IntDstTy, MaskedSrc, ExtInsSrc);

  MIRBuilder.buildCast(Dst, Or);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerSADDO_SSUBO(MachineInstr &MI) {
  auto [Dst0, Dst0Ty, Dst1, Dst1Ty, LHS, LHSTy, RHS, RHSTy] =
      MI.getFirst4RegLLTs();
  const bool IsAdd = MI.getOpcode() == TargetOpcode::G_SADDO;

  LLT Ty = Dst0Ty;
  LLT BoolTy = Dst1Ty;

  Register NewDst0 = MRI.cloneVirtualRegister(Dst0);

  if (IsAdd)
    MIRBuilder.buildAdd(NewDst0, LHS, RHS);
  else
    MIRBuilder.buildSub(NewDst0, LHS, RHS);

  // TODO: If SADDSAT/SSUBSAT is legal, compare results to detect overflow.

  auto Zero = MIRBuilder.buildConstant(Ty, 0);

  // For an addition, the result should be less than one of the operands (LHS)
  // if and only if the other operand (RHS) is negative, otherwise there will
  // be overflow.
  // For a subtraction, the result should be less than one of the operands
  // (LHS) if and only if the other operand (RHS) is (non-zero) positive,
  // otherwise there will be overflow.
  auto ResultLowerThanLHS =
      MIRBuilder.buildICmp(CmpInst::ICMP_SLT, BoolTy, NewDst0, LHS);
  auto ConditionRHS = MIRBuilder.buildICmp(
      IsAdd ? CmpInst::ICMP_SLT : CmpInst::ICMP_SGT, BoolTy, RHS, Zero);

  MIRBuilder.buildXor(Dst1, ConditionRHS, ResultLowerThanLHS);

  MIRBuilder.buildCopy(Dst0, NewDst0);
  MI.eraseFromParent();

  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerAddSubSatToMinMax(MachineInstr &MI) {
  auto [Res, LHS, RHS] = MI.getFirst3Regs();
  LLT Ty = MRI.getType(Res);
  bool IsSigned;
  bool IsAdd;
  unsigned BaseOp;
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("unexpected addsat/subsat opcode");
  case TargetOpcode::G_UADDSAT:
    IsSigned = false;
    IsAdd = true;
    BaseOp = TargetOpcode::G_ADD;
    break;
  case TargetOpcode::G_SADDSAT:
    IsSigned = true;
    IsAdd = true;
    BaseOp = TargetOpcode::G_ADD;
    break;
  case TargetOpcode::G_USUBSAT:
    IsSigned = false;
    IsAdd = false;
    BaseOp = TargetOpcode::G_SUB;
    break;
  case TargetOpcode::G_SSUBSAT:
    IsSigned = true;
    IsAdd = false;
    BaseOp = TargetOpcode::G_SUB;
    break;
  }

  if (IsSigned) {
    // sadd.sat(a, b) ->
    //   hi = 0x7fffffff - smax(a, 0)
    //   lo = 0x80000000 - smin(a, 0)
    //   a + smin(smax(lo, b), hi)
    // ssub.sat(a, b) ->
    //   lo = smax(a, -1) - 0x7fffffff
    //   hi = smin(a, -1) - 0x80000000
    //   a - smin(smax(lo, b), hi)
    // TODO: AMDGPU can use a "median of 3" instruction here:
    //   a +/- med3(lo, b, hi)
    uint64_t NumBits = Ty.getScalarSizeInBits();
    auto MaxVal =
        MIRBuilder.buildConstant(Ty, APInt::getSignedMaxValue(NumBits));
    auto MinVal =
        MIRBuilder.buildConstant(Ty, APInt::getSignedMinValue(NumBits));
    MachineInstrBuilder Hi, Lo;
    if (IsAdd) {
      auto Zero = MIRBuilder.buildConstant(Ty, 0);
      Hi = MIRBuilder.buildSub(Ty, MaxVal, MIRBuilder.buildSMax(Ty, LHS, Zero));
      Lo = MIRBuilder.buildSub(Ty, MinVal, MIRBuilder.buildSMin(Ty, LHS, Zero));
    } else {
      auto NegOne = MIRBuilder.buildConstant(Ty, -1);
      Lo = MIRBuilder.buildSub(Ty, MIRBuilder.buildSMax(Ty, LHS, NegOne),
                               MaxVal);
      Hi = MIRBuilder.buildSub(Ty, MIRBuilder.buildSMin(Ty, LHS, NegOne),
                               MinVal);
    }
    auto RHSClamped =
        MIRBuilder.buildSMin(Ty, MIRBuilder.buildSMax(Ty, Lo, RHS), Hi);
    MIRBuilder.buildInstr(BaseOp, {Res}, {LHS, RHSClamped});
  } else {
    // uadd.sat(a, b) -> a + umin(~a, b)
    // usub.sat(a, b) -> a - umin(a, b)
    Register Not = IsAdd ? MIRBuilder.buildNot(Ty, LHS).getReg(0) : LHS;
    auto Min = MIRBuilder.buildUMin(Ty, Not, RHS);
    MIRBuilder.buildInstr(BaseOp, {Res}, {LHS, Min});
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerAddSubSatToAddoSubo(MachineInstr &MI) {
  auto [Res, LHS, RHS] = MI.getFirst3Regs();
  LLT Ty = MRI.getType(Res);
  LLT BoolTy = Ty.changeElementSize(1);
  bool IsSigned;
  bool IsAdd;
  unsigned OverflowOp;
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("unexpected addsat/subsat opcode");
  case TargetOpcode::G_UADDSAT:
    IsSigned = false;
    IsAdd = true;
    OverflowOp = TargetOpcode::G_UADDO;
    break;
  case TargetOpcode::G_SADDSAT:
    IsSigned = true;
    IsAdd = true;
    OverflowOp = TargetOpcode::G_SADDO;
    break;
  case TargetOpcode::G_USUBSAT:
    IsSigned = false;
    IsAdd = false;
    OverflowOp = TargetOpcode::G_USUBO;
    break;
  case TargetOpcode::G_SSUBSAT:
    IsSigned = true;
    IsAdd = false;
    OverflowOp = TargetOpcode::G_SSUBO;
    break;
  }

  auto OverflowRes =
      MIRBuilder.buildInstr(OverflowOp, {Ty, BoolTy}, {LHS, RHS});
  Register Tmp = OverflowRes.getReg(0);
  Register Ov = OverflowRes.getReg(1);
  MachineInstrBuilder Clamp;
  if (IsSigned) {
    // sadd.sat(a, b) ->
    //   {tmp, ov} = saddo(a, b)
    //   ov ? (tmp >>s 31) + 0x80000000 : r
    // ssub.sat(a, b) ->
    //   {tmp, ov} = ssubo(a, b)
    //   ov ? (tmp >>s 31) + 0x80000000 : r
    uint64_t NumBits = Ty.getScalarSizeInBits();
    auto ShiftAmount = MIRBuilder.buildConstant(Ty, NumBits - 1);
    auto Sign = MIRBuilder.buildAShr(Ty, Tmp, ShiftAmount);
    auto MinVal =
        MIRBuilder.buildConstant(Ty, APInt::getSignedMinValue(NumBits));
    Clamp = MIRBuilder.buildAdd(Ty, Sign, MinVal);
  } else {
    // uadd.sat(a, b) ->
    //   {tmp, ov} = uaddo(a, b)
    //   ov ? 0xffffffff : tmp
    // usub.sat(a, b) ->
    //   {tmp, ov} = usubo(a, b)
    //   ov ? 0 : tmp
    Clamp = MIRBuilder.buildConstant(Ty, IsAdd ? -1 : 0);
  }
  MIRBuilder.buildSelect(Res, Ov, Clamp, Tmp);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerShlSat(MachineInstr &MI) {
  assert((MI.getOpcode() == TargetOpcode::G_SSHLSAT ||
          MI.getOpcode() == TargetOpcode::G_USHLSAT) &&
         "Expected shlsat opcode!");
  bool IsSigned = MI.getOpcode() == TargetOpcode::G_SSHLSAT;
  auto [Res, LHS, RHS] = MI.getFirst3Regs();
  LLT Ty = MRI.getType(Res);
  LLT BoolTy = Ty.changeElementSize(1);

  unsigned BW = Ty.getScalarSizeInBits();
  auto Result = MIRBuilder.buildShl(Ty, LHS, RHS);
  auto Orig = IsSigned ? MIRBuilder.buildAShr(Ty, Result, RHS)
                       : MIRBuilder.buildLShr(Ty, Result, RHS);

  MachineInstrBuilder SatVal;
  if (IsSigned) {
    auto SatMin = MIRBuilder.buildConstant(Ty, APInt::getSignedMinValue(BW));
    auto SatMax = MIRBuilder.buildConstant(Ty, APInt::getSignedMaxValue(BW));
    auto Cmp = MIRBuilder.buildICmp(CmpInst::ICMP_SLT, BoolTy, LHS,
                                    MIRBuilder.buildConstant(Ty, 0));
    SatVal = MIRBuilder.buildSelect(Ty, Cmp, SatMin, SatMax);
  } else {
    SatVal = MIRBuilder.buildConstant(Ty, APInt::getMaxValue(BW));
  }
  auto Ov = MIRBuilder.buildICmp(CmpInst::ICMP_NE, BoolTy, LHS, Orig);
  MIRBuilder.buildSelect(Res, Ov, SatVal, Result);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerBswap(MachineInstr &MI) {
  auto [Dst, Src] = MI.getFirst2Regs();
  const LLT Ty = MRI.getType(Src);
  unsigned SizeInBytes = (Ty.getScalarSizeInBits() + 7) / 8;
  unsigned BaseShiftAmt = (SizeInBytes - 1) * 8;

  // Swap most and least significant byte, set remaining bytes in Res to zero.
  auto ShiftAmt = MIRBuilder.buildConstant(Ty, BaseShiftAmt);
  auto LSByteShiftedLeft = MIRBuilder.buildShl(Ty, Src, ShiftAmt);
  auto MSByteShiftedRight = MIRBuilder.buildLShr(Ty, Src, ShiftAmt);
  auto Res = MIRBuilder.buildOr(Ty, MSByteShiftedRight, LSByteShiftedLeft);

  // Set i-th high/low byte in Res to i-th low/high byte from Src.
  for (unsigned i = 1; i < SizeInBytes / 2; ++i) {
    // AND with Mask leaves byte i unchanged and sets remaining bytes to 0.
    APInt APMask(SizeInBytes * 8, 0xFF << (i * 8));
    auto Mask = MIRBuilder.buildConstant(Ty, APMask);
    auto ShiftAmt = MIRBuilder.buildConstant(Ty, BaseShiftAmt - 16 * i);
    // Low byte shifted left to place of high byte: (Src & Mask) << ShiftAmt.
    auto LoByte = MIRBuilder.buildAnd(Ty, Src, Mask);
    auto LoShiftedLeft = MIRBuilder.buildShl(Ty, LoByte, ShiftAmt);
    Res = MIRBuilder.buildOr(Ty, Res, LoShiftedLeft);
    // High byte shifted right to place of low byte: (Src >> ShiftAmt) & Mask.
    auto SrcShiftedRight = MIRBuilder.buildLShr(Ty, Src, ShiftAmt);
    auto HiShiftedRight = MIRBuilder.buildAnd(Ty, SrcShiftedRight, Mask);
    Res = MIRBuilder.buildOr(Ty, Res, HiShiftedRight);
  }
  Res.getInstr()->getOperand(0).setReg(Dst);

  MI.eraseFromParent();
  return Legalized;
}

//{ (Src & Mask) >> N } | { (Src << N) & Mask }
static MachineInstrBuilder SwapN(unsigned N, DstOp Dst, MachineIRBuilder &B,
                                 MachineInstrBuilder Src, const APInt &Mask) {
  const LLT Ty = Dst.getLLTTy(*B.getMRI());
  MachineInstrBuilder C_N = B.buildConstant(Ty, N);
  MachineInstrBuilder MaskLoNTo0 = B.buildConstant(Ty, Mask);
  auto LHS = B.buildLShr(Ty, B.buildAnd(Ty, Src, MaskLoNTo0), C_N);
  auto RHS = B.buildAnd(Ty, B.buildShl(Ty, Src, C_N), MaskLoNTo0);
  return B.buildOr(Dst, LHS, RHS);
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerBitreverse(MachineInstr &MI) {
  auto [Dst, Src] = MI.getFirst2Regs();
  const LLT Ty = MRI.getType(Src);
  unsigned Size = Ty.getScalarSizeInBits();

  if (Size >= 8) {
    MachineInstrBuilder BSWAP =
        MIRBuilder.buildInstr(TargetOpcode::G_BSWAP, {Ty}, {Src});

    // swap high and low 4 bits in 8 bit blocks 7654|3210 -> 3210|7654
    //    [(val & 0xF0F0F0F0) >> 4] | [(val & 0x0F0F0F0F) << 4]
    // -> [(val & 0xF0F0F0F0) >> 4] | [(val << 4) & 0xF0F0F0F0]
    MachineInstrBuilder Swap4 =
        SwapN(4, Ty, MIRBuilder, BSWAP, APInt::getSplat(Size, APInt(8, 0xF0)));

    // swap high and low 2 bits in 4 bit blocks 32|10 76|54 -> 10|32 54|76
    //    [(val & 0xCCCCCCCC) >> 2] & [(val & 0x33333333) << 2]
    // -> [(val & 0xCCCCCCCC) >> 2] & [(val << 2) & 0xCCCCCCCC]
    MachineInstrBuilder Swap2 =
        SwapN(2, Ty, MIRBuilder, Swap4, APInt::getSplat(Size, APInt(8, 0xCC)));

    // swap high and low 1 bit in 2 bit blocks 1|0 3|2 5|4 7|6 -> 0|1 2|3 4|5
    // 6|7
    //    [(val & 0xAAAAAAAA) >> 1] & [(val & 0x55555555) << 1]
    // -> [(val & 0xAAAAAAAA) >> 1] & [(val << 1) & 0xAAAAAAAA]
    SwapN(1, Dst, MIRBuilder, Swap2, APInt::getSplat(Size, APInt(8, 0xAA)));
  } else {
    // Expand bitreverse for types smaller than 8 bits.
    MachineInstrBuilder Tmp;
    for (unsigned I = 0, J = Size - 1; I < Size; ++I, --J) {
      MachineInstrBuilder Tmp2;
      if (I < J) {
        auto ShAmt = MIRBuilder.buildConstant(Ty, J - I);
        Tmp2 = MIRBuilder.buildShl(Ty, Src, ShAmt);
      } else {
        auto ShAmt = MIRBuilder.buildConstant(Ty, I - J);
        Tmp2 = MIRBuilder.buildLShr(Ty, Src, ShAmt);
      }

      auto Mask = MIRBuilder.buildConstant(Ty, 1ULL << J);
      Tmp2 = MIRBuilder.buildAnd(Ty, Tmp2, Mask);
      if (I == 0)
        Tmp = Tmp2;
      else
        Tmp = MIRBuilder.buildOr(Ty, Tmp, Tmp2);
    }
    MIRBuilder.buildCopy(Dst, Tmp);
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerReadWriteRegister(MachineInstr &MI) {
  MachineFunction &MF = MIRBuilder.getMF();

  bool IsRead = MI.getOpcode() == TargetOpcode::G_READ_REGISTER;
  int NameOpIdx = IsRead ? 1 : 0;
  int ValRegIndex = IsRead ? 0 : 1;

  Register ValReg = MI.getOperand(ValRegIndex).getReg();
  const LLT Ty = MRI.getType(ValReg);
  const MDString *RegStr = cast<MDString>(
    cast<MDNode>(MI.getOperand(NameOpIdx).getMetadata())->getOperand(0));

  Register PhysReg = TLI.getRegisterByName(RegStr->getString().data(), Ty, MF);
  if (!PhysReg.isValid())
    return UnableToLegalize;

  if (IsRead)
    MIRBuilder.buildCopy(ValReg, PhysReg);
  else
    MIRBuilder.buildCopy(PhysReg, ValReg);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerSMULH_UMULH(MachineInstr &MI) {
  bool IsSigned = MI.getOpcode() == TargetOpcode::G_SMULH;
  unsigned ExtOp = IsSigned ? TargetOpcode::G_SEXT : TargetOpcode::G_ZEXT;
  Register Result = MI.getOperand(0).getReg();
  LLT OrigTy = MRI.getType(Result);
  auto SizeInBits = OrigTy.getScalarSizeInBits();
  LLT WideTy = OrigTy.changeElementSize(SizeInBits * 2);

  auto LHS = MIRBuilder.buildInstr(ExtOp, {WideTy}, {MI.getOperand(1)});
  auto RHS = MIRBuilder.buildInstr(ExtOp, {WideTy}, {MI.getOperand(2)});
  auto Mul = MIRBuilder.buildMul(WideTy, LHS, RHS);
  unsigned ShiftOp = IsSigned ? TargetOpcode::G_ASHR : TargetOpcode::G_LSHR;

  auto ShiftAmt = MIRBuilder.buildConstant(WideTy, SizeInBits);
  auto Shifted = MIRBuilder.buildInstr(ShiftOp, {WideTy}, {Mul, ShiftAmt});
  MIRBuilder.buildTrunc(Result, Shifted);

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerISFPCLASS(MachineInstr &MI) {
  auto [DstReg, DstTy, SrcReg, SrcTy] = MI.getFirst2RegLLTs();
  FPClassTest Mask = static_cast<FPClassTest>(MI.getOperand(2).getImm());

  if (Mask == fcNone) {
    MIRBuilder.buildConstant(DstReg, 0);
    MI.eraseFromParent();
    return Legalized;
  }
  if (Mask == fcAllFlags) {
    MIRBuilder.buildConstant(DstReg, 1);
    MI.eraseFromParent();
    return Legalized;
  }

  // TODO: Try inverting the test with getInvertedFPClassTest like the DAG
  // version

  unsigned BitSize = SrcTy.getScalarSizeInBits();
  const fltSemantics &Semantics = getFltSemanticForLLT(SrcTy.getScalarType());

  LLT IntTy = LLT::scalar(BitSize);
  if (SrcTy.isVector())
    IntTy = LLT::vector(SrcTy.getElementCount(), IntTy);
  auto AsInt = MIRBuilder.buildCopy(IntTy, SrcReg);

  // Various masks.
  APInt SignBit = APInt::getSignMask(BitSize);
  APInt ValueMask = APInt::getSignedMaxValue(BitSize);     // All bits but sign.
  APInt Inf = APFloat::getInf(Semantics).bitcastToAPInt(); // Exp and int bit.
  APInt ExpMask = Inf;
  APInt AllOneMantissa = APFloat::getLargest(Semantics).bitcastToAPInt() & ~Inf;
  APInt QNaNBitMask =
      APInt::getOneBitSet(BitSize, AllOneMantissa.getActiveBits() - 1);
  APInt InvertionMask = APInt::getAllOnes(DstTy.getScalarSizeInBits());

  auto SignBitC = MIRBuilder.buildConstant(IntTy, SignBit);
  auto ValueMaskC = MIRBuilder.buildConstant(IntTy, ValueMask);
  auto InfC = MIRBuilder.buildConstant(IntTy, Inf);
  auto ExpMaskC = MIRBuilder.buildConstant(IntTy, ExpMask);
  auto ZeroC = MIRBuilder.buildConstant(IntTy, 0);

  auto Abs = MIRBuilder.buildAnd(IntTy, AsInt, ValueMaskC);
  auto Sign =
      MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_NE, DstTy, AsInt, Abs);

  auto Res = MIRBuilder.buildConstant(DstTy, 0);
  // Clang doesn't support capture of structured bindings:
  LLT DstTyCopy = DstTy;
  const auto appendToRes = [&](MachineInstrBuilder ToAppend) {
    Res = MIRBuilder.buildOr(DstTyCopy, Res, ToAppend);
  };

  // Tests that involve more than one class should be processed first.
  if ((Mask & fcFinite) == fcFinite) {
    // finite(V) ==> abs(V) u< exp_mask
    appendToRes(MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_ULT, DstTy, Abs,
                                     ExpMaskC));
    Mask &= ~fcFinite;
  } else if ((Mask & fcFinite) == fcPosFinite) {
    // finite(V) && V > 0 ==> V u< exp_mask
    appendToRes(MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_ULT, DstTy, AsInt,
                                     ExpMaskC));
    Mask &= ~fcPosFinite;
  } else if ((Mask & fcFinite) == fcNegFinite) {
    // finite(V) && V < 0 ==> abs(V) u< exp_mask && signbit == 1
    auto Cmp = MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_ULT, DstTy, Abs,
                                    ExpMaskC);
    auto And = MIRBuilder.buildAnd(DstTy, Cmp, Sign);
    appendToRes(And);
    Mask &= ~fcNegFinite;
  }

  if (FPClassTest PartialCheck = Mask & (fcZero | fcSubnormal)) {
    // fcZero | fcSubnormal => test all exponent bits are 0
    // TODO: Handle sign bit specific cases
    // TODO: Handle inverted case
    if (PartialCheck == (fcZero | fcSubnormal)) {
      auto ExpBits = MIRBuilder.buildAnd(IntTy, AsInt, ExpMaskC);
      appendToRes(MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_EQ, DstTy,
                                       ExpBits, ZeroC));
      Mask &= ~PartialCheck;
    }
  }

  // Check for individual classes.
  if (FPClassTest PartialCheck = Mask & fcZero) {
    if (PartialCheck == fcPosZero)
      appendToRes(MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_EQ, DstTy,
                                       AsInt, ZeroC));
    else if (PartialCheck == fcZero)
      appendToRes(
          MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_EQ, DstTy, Abs, ZeroC));
    else // fcNegZero
      appendToRes(MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_EQ, DstTy,
                                       AsInt, SignBitC));
  }

  if (FPClassTest PartialCheck = Mask & fcSubnormal) {
    // issubnormal(V) ==> unsigned(abs(V) - 1) u< (all mantissa bits set)
    // issubnormal(V) && V>0 ==> unsigned(V - 1) u< (all mantissa bits set)
    auto V = (PartialCheck == fcPosSubnormal) ? AsInt : Abs;
    auto OneC = MIRBuilder.buildConstant(IntTy, 1);
    auto VMinusOne = MIRBuilder.buildSub(IntTy, V, OneC);
    auto SubnormalRes =
        MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_ULT, DstTy, VMinusOne,
                             MIRBuilder.buildConstant(IntTy, AllOneMantissa));
    if (PartialCheck == fcNegSubnormal)
      SubnormalRes = MIRBuilder.buildAnd(DstTy, SubnormalRes, Sign);
    appendToRes(SubnormalRes);
  }

  if (FPClassTest PartialCheck = Mask & fcInf) {
    if (PartialCheck == fcPosInf)
      appendToRes(MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_EQ, DstTy,
                                       AsInt, InfC));
    else if (PartialCheck == fcInf)
      appendToRes(
          MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_EQ, DstTy, Abs, InfC));
    else { // fcNegInf
      APInt NegInf = APFloat::getInf(Semantics, true).bitcastToAPInt();
      auto NegInfC = MIRBuilder.buildConstant(IntTy, NegInf);
      appendToRes(MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_EQ, DstTy,
                                       AsInt, NegInfC));
    }
  }

  if (FPClassTest PartialCheck = Mask & fcNan) {
    auto InfWithQnanBitC = MIRBuilder.buildConstant(IntTy, Inf | QNaNBitMask);
    if (PartialCheck == fcNan) {
      // isnan(V) ==> abs(V) u> int(inf)
      appendToRes(
          MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_UGT, DstTy, Abs, InfC));
    } else if (PartialCheck == fcQNan) {
      // isquiet(V) ==> abs(V) u>= (unsigned(Inf) | quiet_bit)
      appendToRes(MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_UGE, DstTy, Abs,
                                       InfWithQnanBitC));
    } else { // fcSNan
      // issignaling(V) ==> abs(V) u> unsigned(Inf) &&
      //                    abs(V) u< (unsigned(Inf) | quiet_bit)
      auto IsNan =
          MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_UGT, DstTy, Abs, InfC);
      auto IsNotQnan = MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_ULT, DstTy,
                                            Abs, InfWithQnanBitC);
      appendToRes(MIRBuilder.buildAnd(DstTy, IsNan, IsNotQnan));
    }
  }

  if (FPClassTest PartialCheck = Mask & fcNormal) {
    // isnormal(V) ==> (0 u< exp u< max_exp) ==> (unsigned(exp-1) u<
    // (max_exp-1))
    APInt ExpLSB = ExpMask & ~(ExpMask.shl(1));
    auto ExpMinusOne = MIRBuilder.buildSub(
        IntTy, Abs, MIRBuilder.buildConstant(IntTy, ExpLSB));
    APInt MaxExpMinusOne = ExpMask - ExpLSB;
    auto NormalRes =
        MIRBuilder.buildICmp(CmpInst::Predicate::ICMP_ULT, DstTy, ExpMinusOne,
                             MIRBuilder.buildConstant(IntTy, MaxExpMinusOne));
    if (PartialCheck == fcNegNormal)
      NormalRes = MIRBuilder.buildAnd(DstTy, NormalRes, Sign);
    else if (PartialCheck == fcPosNormal) {
      auto PosSign = MIRBuilder.buildXor(
          DstTy, Sign, MIRBuilder.buildConstant(DstTy, InvertionMask));
      NormalRes = MIRBuilder.buildAnd(DstTy, NormalRes, PosSign);
    }
    appendToRes(NormalRes);
  }

  MIRBuilder.buildCopy(DstReg, Res);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerSelect(MachineInstr &MI) {
  // Implement G_SELECT in terms of XOR, AND, OR.
  auto [DstReg, DstTy, MaskReg, MaskTy, Op1Reg, Op1Ty, Op2Reg, Op2Ty] =
      MI.getFirst4RegLLTs();

  bool IsEltPtr = DstTy.isPointerOrPointerVector();
  if (IsEltPtr) {
    LLT ScalarPtrTy = LLT::scalar(DstTy.getScalarSizeInBits());
    LLT NewTy = DstTy.changeElementType(ScalarPtrTy);
    Op1Reg = MIRBuilder.buildPtrToInt(NewTy, Op1Reg).getReg(0);
    Op2Reg = MIRBuilder.buildPtrToInt(NewTy, Op2Reg).getReg(0);
    DstTy = NewTy;
  }

  if (MaskTy.isScalar()) {
    // Turn the scalar condition into a vector condition mask if needed.

    Register MaskElt = MaskReg;

    // The condition was potentially zero extended before, but we want a sign
    // extended boolean.
    if (MaskTy != LLT::scalar(1))
      MaskElt = MIRBuilder.buildSExtInReg(MaskTy, MaskElt, 1).getReg(0);

    // Continue the sign extension (or truncate) to match the data type.
    MaskElt =
        MIRBuilder.buildSExtOrTrunc(DstTy.getScalarType(), MaskElt).getReg(0);

    if (DstTy.isVector()) {
      // Generate a vector splat idiom.
      auto ShufSplat = MIRBuilder.buildShuffleSplat(DstTy, MaskElt);
      MaskReg = ShufSplat.getReg(0);
    } else {
      MaskReg = MaskElt;
    }
    MaskTy = DstTy;
  } else if (!DstTy.isVector()) {
    // Cannot handle the case that mask is a vector and dst is a scalar.
    return UnableToLegalize;
  }

  if (MaskTy.getSizeInBits() != DstTy.getSizeInBits()) {
    return UnableToLegalize;
  }

  auto NotMask = MIRBuilder.buildNot(MaskTy, MaskReg);
  auto NewOp1 = MIRBuilder.buildAnd(MaskTy, Op1Reg, MaskReg);
  auto NewOp2 = MIRBuilder.buildAnd(MaskTy, Op2Reg, NotMask);
  if (IsEltPtr) {
    auto Or = MIRBuilder.buildOr(DstTy, NewOp1, NewOp2);
    MIRBuilder.buildIntToPtr(DstReg, Or);
  } else {
    MIRBuilder.buildOr(DstReg, NewOp1, NewOp2);
  }
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerDIVREM(MachineInstr &MI) {
  // Split DIVREM into individual instructions.
  unsigned Opcode = MI.getOpcode();

  MIRBuilder.buildInstr(
      Opcode == TargetOpcode::G_SDIVREM ? TargetOpcode::G_SDIV
                                        : TargetOpcode::G_UDIV,
      {MI.getOperand(0).getReg()}, {MI.getOperand(2), MI.getOperand(3)});
  MIRBuilder.buildInstr(
      Opcode == TargetOpcode::G_SDIVREM ? TargetOpcode::G_SREM
                                        : TargetOpcode::G_UREM,
      {MI.getOperand(1).getReg()}, {MI.getOperand(2), MI.getOperand(3)});
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerAbsToAddXor(MachineInstr &MI) {
  // Expand %res = G_ABS %a into:
  // %v1 = G_ASHR %a, scalar_size-1
  // %v2 = G_ADD %a, %v1
  // %res = G_XOR %v2, %v1
  LLT DstTy = MRI.getType(MI.getOperand(0).getReg());
  Register OpReg = MI.getOperand(1).getReg();
  auto ShiftAmt =
      MIRBuilder.buildConstant(DstTy, DstTy.getScalarSizeInBits() - 1);
  auto Shift = MIRBuilder.buildAShr(DstTy, OpReg, ShiftAmt);
  auto Add = MIRBuilder.buildAdd(DstTy, OpReg, Shift);
  MIRBuilder.buildXor(MI.getOperand(0).getReg(), Add, Shift);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerAbsToMaxNeg(MachineInstr &MI) {
  // Expand %res = G_ABS %a into:
  // %v1 = G_CONSTANT 0
  // %v2 = G_SUB %v1, %a
  // %res = G_SMAX %a, %v2
  Register SrcReg = MI.getOperand(1).getReg();
  LLT Ty = MRI.getType(SrcReg);
  auto Zero = MIRBuilder.buildConstant(Ty, 0);
  auto Sub = MIRBuilder.buildSub(Ty, Zero, SrcReg);
  MIRBuilder.buildSMax(MI.getOperand(0), SrcReg, Sub);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerAbsToCNeg(MachineInstr &MI) {
  Register SrcReg = MI.getOperand(1).getReg();
  Register DestReg = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(SrcReg), IType = LLT::scalar(1);
  auto Zero = MIRBuilder.buildConstant(Ty, 0).getReg(0);
  auto Sub = MIRBuilder.buildSub(Ty, Zero, SrcReg).getReg(0);
  auto ICmp = MIRBuilder.buildICmp(CmpInst::ICMP_SGT, IType, SrcReg, Zero);
  MIRBuilder.buildSelect(DestReg, ICmp, SrcReg, Sub);
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerVectorReduction(MachineInstr &MI) {
  Register SrcReg = MI.getOperand(1).getReg();
  LLT SrcTy = MRI.getType(SrcReg);
  LLT DstTy = MRI.getType(SrcReg);

  // The source could be a scalar if the IR type was <1 x sN>.
  if (SrcTy.isScalar()) {
    if (DstTy.getSizeInBits() > SrcTy.getSizeInBits())
      return UnableToLegalize; // FIXME: handle extension.
    // This can be just a plain copy.
    Observer.changingInstr(MI);
    MI.setDesc(MIRBuilder.getTII().get(TargetOpcode::COPY));
    Observer.changedInstr(MI);
    return Legalized;
  }
  return UnableToLegalize;
}

LegalizerHelper::LegalizeResult LegalizerHelper::lowerVAArg(MachineInstr &MI) {
  MachineFunction &MF = *MI.getMF();
  const DataLayout &DL = MIRBuilder.getDataLayout();
  LLVMContext &Ctx = MF.getFunction().getContext();
  Register ListPtr = MI.getOperand(1).getReg();
  LLT PtrTy = MRI.getType(ListPtr);

  // LstPtr is a pointer to the head of the list. Get the address
  // of the head of the list.
  Align PtrAlignment = DL.getABITypeAlign(getTypeForLLT(PtrTy, Ctx));
  MachineMemOperand *PtrLoadMMO = MF.getMachineMemOperand(
      MachinePointerInfo(), MachineMemOperand::MOLoad, PtrTy, PtrAlignment);
  auto VAList = MIRBuilder.buildLoad(PtrTy, ListPtr, *PtrLoadMMO).getReg(0);

  const Align A(MI.getOperand(2).getImm());
  LLT PtrTyAsScalarTy = LLT::scalar(PtrTy.getSizeInBits());
  if (A > TLI.getMinStackArgumentAlignment()) {
    Register AlignAmt =
        MIRBuilder.buildConstant(PtrTyAsScalarTy, A.value() - 1).getReg(0);
    auto AddDst = MIRBuilder.buildPtrAdd(PtrTy, VAList, AlignAmt);
    auto AndDst = MIRBuilder.buildMaskLowPtrBits(PtrTy, AddDst, Log2(A));
    VAList = AndDst.getReg(0);
  }

  // Increment the pointer, VAList, to the next vaarg
  // The list should be bumped by the size of element in the current head of
  // list.
  Register Dst = MI.getOperand(0).getReg();
  LLT LLTTy = MRI.getType(Dst);
  Type *Ty = getTypeForLLT(LLTTy, Ctx);
  auto IncAmt =
      MIRBuilder.buildConstant(PtrTyAsScalarTy, DL.getTypeAllocSize(Ty));
  auto Succ = MIRBuilder.buildPtrAdd(PtrTy, VAList, IncAmt);

  // Store the increment VAList to the legalized pointer
  MachineMemOperand *StoreMMO = MF.getMachineMemOperand(
      MachinePointerInfo(), MachineMemOperand::MOStore, PtrTy, PtrAlignment);
  MIRBuilder.buildStore(Succ, ListPtr, *StoreMMO);
  // Load the actual argument out of the pointer VAList
  Align EltAlignment = DL.getABITypeAlign(Ty);
  MachineMemOperand *EltLoadMMO = MF.getMachineMemOperand(
      MachinePointerInfo(), MachineMemOperand::MOLoad, LLTTy, EltAlignment);
  MIRBuilder.buildLoad(Dst, VAList, *EltLoadMMO);

  MI.eraseFromParent();
  return Legalized;
}

static bool shouldLowerMemFuncForSize(const MachineFunction &MF) {
  // On Darwin, -Os means optimize for size without hurting performance, so
  // only really optimize for size when -Oz (MinSize) is used.
  if (MF.getTarget().getTargetTriple().isOSDarwin())
    return MF.getFunction().hasMinSize();
  return MF.getFunction().hasOptSize();
}

// Returns a list of types to use for memory op lowering in MemOps. A partial
// port of findOptimalMemOpLowering in TargetLowering.
static bool findGISelOptimalMemOpLowering(std::vector<LLT> &MemOps,
                                          unsigned Limit, const MemOp &Op,
                                          unsigned DstAS, unsigned SrcAS,
                                          const AttributeList &FuncAttributes,
                                          const TargetLowering &TLI) {
  if (Op.isMemcpyWithFixedDstAlign() && Op.getSrcAlign() < Op.getDstAlign())
    return false;

  LLT Ty = TLI.getOptimalMemOpLLT(Op, FuncAttributes);

  if (Ty == LLT()) {
    // Use the largest scalar type whose alignment constraints are satisfied.
    // We only need to check DstAlign here as SrcAlign is always greater or
    // equal to DstAlign (or zero).
    Ty = LLT::scalar(64);
    if (Op.isFixedDstAlign())
      while (Op.getDstAlign() < Ty.getSizeInBytes() &&
             !TLI.allowsMisalignedMemoryAccesses(Ty, DstAS, Op.getDstAlign()))
        Ty = LLT::scalar(Ty.getSizeInBytes());
    assert(Ty.getSizeInBits() > 0 && "Could not find valid type");
    // FIXME: check for the largest legal type we can load/store to.
  }

  unsigned NumMemOps = 0;
  uint64_t Size = Op.size();
  while (Size) {
    unsigned TySize = Ty.getSizeInBytes();
    while (TySize > Size) {
      // For now, only use non-vector load / store's for the left-over pieces.
      LLT NewTy = Ty;
      // FIXME: check for mem op safety and legality of the types. Not all of
      // SDAGisms map cleanly to GISel concepts.
      if (NewTy.isVector())
        NewTy = NewTy.getSizeInBits() > 64 ? LLT::scalar(64) : LLT::scalar(32);
      NewTy = LLT::scalar(llvm::bit_floor(NewTy.getSizeInBits() - 1));
      unsigned NewTySize = NewTy.getSizeInBytes();
      assert(NewTySize > 0 && "Could not find appropriate type");

      // If the new LLT cannot cover all of the remaining bits, then consider
      // issuing a (or a pair of) unaligned and overlapping load / store.
      unsigned Fast;
      // Need to get a VT equivalent for allowMisalignedMemoryAccesses().
      MVT VT = getMVTForLLT(Ty);
      if (NumMemOps && Op.allowOverlap() && NewTySize < Size &&
          TLI.allowsMisalignedMemoryAccesses(
              VT, DstAS, Op.isFixedDstAlign() ? Op.getDstAlign() : Align(1),
              MachineMemOperand::MONone, &Fast) &&
          Fast)
        TySize = Size;
      else {
        Ty = NewTy;
        TySize = NewTySize;
      }
    }

    if (++NumMemOps > Limit)
      return false;

    MemOps.push_back(Ty);
    Size -= TySize;
  }

  return true;
}

// Get a vectorized representation of the memset value operand, GISel edition.
static Register getMemsetValue(Register Val, LLT Ty, MachineIRBuilder &MIB) {
  MachineRegisterInfo &MRI = *MIB.getMRI();
  unsigned NumBits = Ty.getScalarSizeInBits();
  auto ValVRegAndVal = getIConstantVRegValWithLookThrough(Val, MRI);
  if (!Ty.isVector() && ValVRegAndVal) {
    APInt Scalar = ValVRegAndVal->Value.trunc(8);
    APInt SplatVal = APInt::getSplat(NumBits, Scalar);
    return MIB.buildConstant(Ty, SplatVal).getReg(0);
  }

  // Extend the byte value to the larger type, and then multiply by a magic
  // value 0x010101... in order to replicate it across every byte.
  // Unless it's zero, in which case just emit a larger G_CONSTANT 0.
  if (ValVRegAndVal && ValVRegAndVal->Value == 0) {
    return MIB.buildConstant(Ty, 0).getReg(0);
  }

  LLT ExtType = Ty.getScalarType();
  auto ZExt = MIB.buildZExtOrTrunc(ExtType, Val);
  if (NumBits > 8) {
    APInt Magic = APInt::getSplat(NumBits, APInt(8, 0x01));
    auto MagicMI = MIB.buildConstant(ExtType, Magic);
    Val = MIB.buildMul(ExtType, ZExt, MagicMI).getReg(0);
  }

  // For vector types create a G_BUILD_VECTOR.
  if (Ty.isVector())
    Val = MIB.buildSplatBuildVector(Ty, Val).getReg(0);

  return Val;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerMemset(MachineInstr &MI, Register Dst, Register Val,
                             uint64_t KnownLen, Align Alignment,
                             bool IsVolatile) {
  auto &MF = *MI.getParent()->getParent();
  const auto &TLI = *MF.getSubtarget().getTargetLowering();
  auto &DL = MF.getDataLayout();
  LLVMContext &C = MF.getFunction().getContext();

  assert(KnownLen != 0 && "Have a zero length memset length!");

  bool DstAlignCanChange = false;
  MachineFrameInfo &MFI = MF.getFrameInfo();
  bool OptSize = shouldLowerMemFuncForSize(MF);

  MachineInstr *FIDef = getOpcodeDef(TargetOpcode::G_FRAME_INDEX, Dst, MRI);
  if (FIDef && !MFI.isFixedObjectIndex(FIDef->getOperand(1).getIndex()))
    DstAlignCanChange = true;

  unsigned Limit = TLI.getMaxStoresPerMemset(OptSize);
  std::vector<LLT> MemOps;

  const auto &DstMMO = **MI.memoperands_begin();
  MachinePointerInfo DstPtrInfo = DstMMO.getPointerInfo();

  auto ValVRegAndVal = getIConstantVRegValWithLookThrough(Val, MRI);
  bool IsZeroVal = ValVRegAndVal && ValVRegAndVal->Value == 0;

  if (!findGISelOptimalMemOpLowering(MemOps, Limit,
                                     MemOp::Set(KnownLen, DstAlignCanChange,
                                                Alignment,
                                                /*IsZeroMemset=*/IsZeroVal,
                                                /*IsVolatile=*/IsVolatile),
                                     DstPtrInfo.getAddrSpace(), ~0u,
                                     MF.getFunction().getAttributes(), TLI))
    return UnableToLegalize;

  if (DstAlignCanChange) {
    // Get an estimate of the type from the LLT.
    Type *IRTy = getTypeForLLT(MemOps[0], C);
    Align NewAlign = DL.getABITypeAlign(IRTy);
    if (NewAlign > Alignment) {
      Alignment = NewAlign;
      unsigned FI = FIDef->getOperand(1).getIndex();
      // Give the stack frame object a larger alignment if needed.
      if (MFI.getObjectAlign(FI) < Alignment)
        MFI.setObjectAlignment(FI, Alignment);
    }
  }

  MachineIRBuilder MIB(MI);
  // Find the largest store and generate the bit pattern for it.
  LLT LargestTy = MemOps[0];
  for (unsigned i = 1; i < MemOps.size(); i++)
    if (MemOps[i].getSizeInBits() > LargestTy.getSizeInBits())
      LargestTy = MemOps[i];

  // The memset stored value is always defined as an s8, so in order to make it
  // work with larger store types we need to repeat the bit pattern across the
  // wider type.
  Register MemSetValue = getMemsetValue(Val, LargestTy, MIB);

  if (!MemSetValue)
    return UnableToLegalize;

  // Generate the stores. For each store type in the list, we generate the
  // matching store of that type to the destination address.
  LLT PtrTy = MRI.getType(Dst);
  unsigned DstOff = 0;
  unsigned Size = KnownLen;
  for (unsigned I = 0; I < MemOps.size(); I++) {
    LLT Ty = MemOps[I];
    unsigned TySize = Ty.getSizeInBytes();
    if (TySize > Size) {
      // Issuing an unaligned load / store pair that overlaps with the previous
      // pair. Adjust the offset accordingly.
      assert(I == MemOps.size() - 1 && I != 0);
      DstOff -= TySize - Size;
    }

    // If this store is smaller than the largest store see whether we can get
    // the smaller value for free with a truncate.
    Register Value = MemSetValue;
    if (Ty.getSizeInBits() < LargestTy.getSizeInBits()) {
      MVT VT = getMVTForLLT(Ty);
      MVT LargestVT = getMVTForLLT(LargestTy);
      if (!LargestTy.isVector() && !Ty.isVector() &&
          TLI.isTruncateFree(LargestVT, VT))
        Value = MIB.buildTrunc(Ty, MemSetValue).getReg(0);
      else
        Value = getMemsetValue(Val, Ty, MIB);
      if (!Value)
        return UnableToLegalize;
    }

    auto *StoreMMO = MF.getMachineMemOperand(&DstMMO, DstOff, Ty);

    Register Ptr = Dst;
    if (DstOff != 0) {
      auto Offset =
          MIB.buildConstant(LLT::scalar(PtrTy.getSizeInBits()), DstOff);
      Ptr = MIB.buildPtrAdd(PtrTy, Dst, Offset).getReg(0);
    }

    MIB.buildStore(Value, Ptr, *StoreMMO);
    DstOff += Ty.getSizeInBytes();
    Size -= TySize;
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerMemcpyInline(MachineInstr &MI) {
  assert(MI.getOpcode() == TargetOpcode::G_MEMCPY_INLINE);

  auto [Dst, Src, Len] = MI.getFirst3Regs();

  const auto *MMOIt = MI.memoperands_begin();
  const MachineMemOperand *MemOp = *MMOIt;
  bool IsVolatile = MemOp->isVolatile();

  // See if this is a constant length copy
  auto LenVRegAndVal = getIConstantVRegValWithLookThrough(Len, MRI);
  // FIXME: support dynamically sized G_MEMCPY_INLINE
  assert(LenVRegAndVal &&
         "inline memcpy with dynamic size is not yet supported");
  uint64_t KnownLen = LenVRegAndVal->Value.getZExtValue();
  if (KnownLen == 0) {
    MI.eraseFromParent();
    return Legalized;
  }

  const auto &DstMMO = **MI.memoperands_begin();
  const auto &SrcMMO = **std::next(MI.memoperands_begin());
  Align DstAlign = DstMMO.getBaseAlign();
  Align SrcAlign = SrcMMO.getBaseAlign();

  return lowerMemcpyInline(MI, Dst, Src, KnownLen, DstAlign, SrcAlign,
                           IsVolatile);
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerMemcpyInline(MachineInstr &MI, Register Dst, Register Src,
                                   uint64_t KnownLen, Align DstAlign,
                                   Align SrcAlign, bool IsVolatile) {
  assert(MI.getOpcode() == TargetOpcode::G_MEMCPY_INLINE);
  return lowerMemcpy(MI, Dst, Src, KnownLen,
                     std::numeric_limits<uint64_t>::max(), DstAlign, SrcAlign,
                     IsVolatile);
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerMemcpy(MachineInstr &MI, Register Dst, Register Src,
                             uint64_t KnownLen, uint64_t Limit, Align DstAlign,
                             Align SrcAlign, bool IsVolatile) {
  auto &MF = *MI.getParent()->getParent();
  const auto &TLI = *MF.getSubtarget().getTargetLowering();
  auto &DL = MF.getDataLayout();
  LLVMContext &C = MF.getFunction().getContext();

  assert(KnownLen != 0 && "Have a zero length memcpy length!");

  bool DstAlignCanChange = false;
  MachineFrameInfo &MFI = MF.getFrameInfo();
  Align Alignment = std::min(DstAlign, SrcAlign);

  MachineInstr *FIDef = getOpcodeDef(TargetOpcode::G_FRAME_INDEX, Dst, MRI);
  if (FIDef && !MFI.isFixedObjectIndex(FIDef->getOperand(1).getIndex()))
    DstAlignCanChange = true;

  // FIXME: infer better src pointer alignment like SelectionDAG does here.
  // FIXME: also use the equivalent of isMemSrcFromConstant and alwaysinlining
  // if the memcpy is in a tail call position.

  std::vector<LLT> MemOps;

  const auto &DstMMO = **MI.memoperands_begin();
  const auto &SrcMMO = **std::next(MI.memoperands_begin());
  MachinePointerInfo DstPtrInfo = DstMMO.getPointerInfo();
  MachinePointerInfo SrcPtrInfo = SrcMMO.getPointerInfo();

  if (!findGISelOptimalMemOpLowering(
          MemOps, Limit,
          MemOp::Copy(KnownLen, DstAlignCanChange, Alignment, SrcAlign,
                      IsVolatile),
          DstPtrInfo.getAddrSpace(), SrcPtrInfo.getAddrSpace(),
          MF.getFunction().getAttributes(), TLI))
    return UnableToLegalize;

  if (DstAlignCanChange) {
    // Get an estimate of the type from the LLT.
    Type *IRTy = getTypeForLLT(MemOps[0], C);
    Align NewAlign = DL.getABITypeAlign(IRTy);

    // Don't promote to an alignment that would require dynamic stack
    // realignment.
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
    if (!TRI->hasStackRealignment(MF))
      while (NewAlign > Alignment && DL.exceedsNaturalStackAlignment(NewAlign))
        NewAlign = NewAlign.previous();

    if (NewAlign > Alignment) {
      Alignment = NewAlign;
      unsigned FI = FIDef->getOperand(1).getIndex();
      // Give the stack frame object a larger alignment if needed.
      if (MFI.getObjectAlign(FI) < Alignment)
        MFI.setObjectAlignment(FI, Alignment);
    }
  }

  LLVM_DEBUG(dbgs() << "Inlining memcpy: " << MI << " into loads & stores\n");

  MachineIRBuilder MIB(MI);
  // Now we need to emit a pair of load and stores for each of the types we've
  // collected. I.e. for each type, generate a load from the source pointer of
  // that type width, and then generate a corresponding store to the dest buffer
  // of that value loaded. This can result in a sequence of loads and stores
  // mixed types, depending on what the target specifies as good types to use.
  unsigned CurrOffset = 0;
  unsigned Size = KnownLen;
  for (auto CopyTy : MemOps) {
    // Issuing an unaligned load / store pair  that overlaps with the previous
    // pair. Adjust the offset accordingly.
    if (CopyTy.getSizeInBytes() > Size)
      CurrOffset -= CopyTy.getSizeInBytes() - Size;

    // Construct MMOs for the accesses.
    auto *LoadMMO =
        MF.getMachineMemOperand(&SrcMMO, CurrOffset, CopyTy.getSizeInBytes());
    auto *StoreMMO =
        MF.getMachineMemOperand(&DstMMO, CurrOffset, CopyTy.getSizeInBytes());

    // Create the load.
    Register LoadPtr = Src;
    Register Offset;
    if (CurrOffset != 0) {
      LLT SrcTy = MRI.getType(Src);
      Offset = MIB.buildConstant(LLT::scalar(SrcTy.getSizeInBits()), CurrOffset)
                   .getReg(0);
      LoadPtr = MIB.buildPtrAdd(SrcTy, Src, Offset).getReg(0);
    }
    auto LdVal = MIB.buildLoad(CopyTy, LoadPtr, *LoadMMO);

    // Create the store.
    Register StorePtr = Dst;
    if (CurrOffset != 0) {
      LLT DstTy = MRI.getType(Dst);
      StorePtr = MIB.buildPtrAdd(DstTy, Dst, Offset).getReg(0);
    }
    MIB.buildStore(LdVal, StorePtr, *StoreMMO);
    CurrOffset += CopyTy.getSizeInBytes();
    Size -= CopyTy.getSizeInBytes();
  }

  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerMemmove(MachineInstr &MI, Register Dst, Register Src,
                              uint64_t KnownLen, Align DstAlign, Align SrcAlign,
                              bool IsVolatile) {
  auto &MF = *MI.getParent()->getParent();
  const auto &TLI = *MF.getSubtarget().getTargetLowering();
  auto &DL = MF.getDataLayout();
  LLVMContext &C = MF.getFunction().getContext();

  assert(KnownLen != 0 && "Have a zero length memmove length!");

  bool DstAlignCanChange = false;
  MachineFrameInfo &MFI = MF.getFrameInfo();
  bool OptSize = shouldLowerMemFuncForSize(MF);
  Align Alignment = std::min(DstAlign, SrcAlign);

  MachineInstr *FIDef = getOpcodeDef(TargetOpcode::G_FRAME_INDEX, Dst, MRI);
  if (FIDef && !MFI.isFixedObjectIndex(FIDef->getOperand(1).getIndex()))
    DstAlignCanChange = true;

  unsigned Limit = TLI.getMaxStoresPerMemmove(OptSize);
  std::vector<LLT> MemOps;

  const auto &DstMMO = **MI.memoperands_begin();
  const auto &SrcMMO = **std::next(MI.memoperands_begin());
  MachinePointerInfo DstPtrInfo = DstMMO.getPointerInfo();
  MachinePointerInfo SrcPtrInfo = SrcMMO.getPointerInfo();

  // FIXME: SelectionDAG always passes false for 'AllowOverlap', apparently due
  // to a bug in it's findOptimalMemOpLowering implementation. For now do the
  // same thing here.
  if (!findGISelOptimalMemOpLowering(
          MemOps, Limit,
          MemOp::Copy(KnownLen, DstAlignCanChange, Alignment, SrcAlign,
                      /*IsVolatile*/ true),
          DstPtrInfo.getAddrSpace(), SrcPtrInfo.getAddrSpace(),
          MF.getFunction().getAttributes(), TLI))
    return UnableToLegalize;

  if (DstAlignCanChange) {
    // Get an estimate of the type from the LLT.
    Type *IRTy = getTypeForLLT(MemOps[0], C);
    Align NewAlign = DL.getABITypeAlign(IRTy);

    // Don't promote to an alignment that would require dynamic stack
    // realignment.
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
    if (!TRI->hasStackRealignment(MF))
      while (NewAlign > Alignment && DL.exceedsNaturalStackAlignment(NewAlign))
        NewAlign = NewAlign.previous();

    if (NewAlign > Alignment) {
      Alignment = NewAlign;
      unsigned FI = FIDef->getOperand(1).getIndex();
      // Give the stack frame object a larger alignment if needed.
      if (MFI.getObjectAlign(FI) < Alignment)
        MFI.setObjectAlignment(FI, Alignment);
    }
  }

  LLVM_DEBUG(dbgs() << "Inlining memmove: " << MI << " into loads & stores\n");

  MachineIRBuilder MIB(MI);
  // Memmove requires that we perform the loads first before issuing the stores.
  // Apart from that, this loop is pretty much doing the same thing as the
  // memcpy codegen function.
  unsigned CurrOffset = 0;
  SmallVector<Register, 16> LoadVals;
  for (auto CopyTy : MemOps) {
    // Construct MMO for the load.
    auto *LoadMMO =
        MF.getMachineMemOperand(&SrcMMO, CurrOffset, CopyTy.getSizeInBytes());

    // Create the load.
    Register LoadPtr = Src;
    if (CurrOffset != 0) {
      LLT SrcTy = MRI.getType(Src);
      auto Offset =
          MIB.buildConstant(LLT::scalar(SrcTy.getSizeInBits()), CurrOffset);
      LoadPtr = MIB.buildPtrAdd(SrcTy, Src, Offset).getReg(0);
    }
    LoadVals.push_back(MIB.buildLoad(CopyTy, LoadPtr, *LoadMMO).getReg(0));
    CurrOffset += CopyTy.getSizeInBytes();
  }

  CurrOffset = 0;
  for (unsigned I = 0; I < MemOps.size(); ++I) {
    LLT CopyTy = MemOps[I];
    // Now store the values loaded.
    auto *StoreMMO =
        MF.getMachineMemOperand(&DstMMO, CurrOffset, CopyTy.getSizeInBytes());

    Register StorePtr = Dst;
    if (CurrOffset != 0) {
      LLT DstTy = MRI.getType(Dst);
      auto Offset =
          MIB.buildConstant(LLT::scalar(DstTy.getSizeInBits()), CurrOffset);
      StorePtr = MIB.buildPtrAdd(DstTy, Dst, Offset).getReg(0);
    }
    MIB.buildStore(LoadVals[I], StorePtr, *StoreMMO);
    CurrOffset += CopyTy.getSizeInBytes();
  }
  MI.eraseFromParent();
  return Legalized;
}

LegalizerHelper::LegalizeResult
LegalizerHelper::lowerMemCpyFamily(MachineInstr &MI, unsigned MaxLen) {
  const unsigned Opc = MI.getOpcode();
  // This combine is fairly complex so it's not written with a separate
  // matcher function.
  assert((Opc == TargetOpcode::G_MEMCPY || Opc == TargetOpcode::G_MEMMOVE ||
          Opc == TargetOpcode::G_MEMSET) &&
         "Expected memcpy like instruction");

  auto MMOIt = MI.memoperands_begin();
  const MachineMemOperand *MemOp = *MMOIt;

  Align DstAlign = MemOp->getBaseAlign();
  Align SrcAlign;
  auto [Dst, Src, Len] = MI.getFirst3Regs();

  if (Opc != TargetOpcode::G_MEMSET) {
    assert(MMOIt != MI.memoperands_end() && "Expected a second MMO on MI");
    MemOp = *(++MMOIt);
    SrcAlign = MemOp->getBaseAlign();
  }

  // See if this is a constant length copy
  auto LenVRegAndVal = getIConstantVRegValWithLookThrough(Len, MRI);
  if (!LenVRegAndVal)
    return UnableToLegalize;
  uint64_t KnownLen = LenVRegAndVal->Value.getZExtValue();

  if (KnownLen == 0) {
    MI.eraseFromParent();
    return Legalized;
  }

  bool IsVolatile = MemOp->isVolatile();
  if (Opc == TargetOpcode::G_MEMCPY_INLINE)
    return lowerMemcpyInline(MI, Dst, Src, KnownLen, DstAlign, SrcAlign,
                             IsVolatile);

  // Don't try to optimize volatile.
  if (IsVolatile)
    return UnableToLegalize;

  if (MaxLen && KnownLen > MaxLen)
    return UnableToLegalize;

  if (Opc == TargetOpcode::G_MEMCPY) {
    auto &MF = *MI.getParent()->getParent();
    const auto &TLI = *MF.getSubtarget().getTargetLowering();
    bool OptSize = shouldLowerMemFuncForSize(MF);
    uint64_t Limit = TLI.getMaxStoresPerMemcpy(OptSize);
    return lowerMemcpy(MI, Dst, Src, KnownLen, Limit, DstAlign, SrcAlign,
                       IsVolatile);
  }
  if (Opc == TargetOpcode::G_MEMMOVE)
    return lowerMemmove(MI, Dst, Src, KnownLen, DstAlign, SrcAlign, IsVolatile);
  if (Opc == TargetOpcode::G_MEMSET)
    return lowerMemset(MI, Dst, Src, KnownLen, DstAlign, IsVolatile);
  return UnableToLegalize;
}
