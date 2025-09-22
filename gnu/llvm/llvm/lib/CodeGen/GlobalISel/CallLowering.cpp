//===-- lib/CodeGen/GlobalISel/CallLowering.cpp - Call lowering -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements some simple delegations needed for call lowering.
///
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "call-lowering"

using namespace llvm;

void CallLowering::anchor() {}

/// Helper function which updates \p Flags when \p AttrFn returns true.
static void
addFlagsUsingAttrFn(ISD::ArgFlagsTy &Flags,
                    const std::function<bool(Attribute::AttrKind)> &AttrFn) {
  // TODO: There are missing flags. Add them here.
  if (AttrFn(Attribute::SExt))
    Flags.setSExt();
  if (AttrFn(Attribute::ZExt))
    Flags.setZExt();
  if (AttrFn(Attribute::InReg))
    Flags.setInReg();
  if (AttrFn(Attribute::StructRet))
    Flags.setSRet();
  if (AttrFn(Attribute::Nest))
    Flags.setNest();
  if (AttrFn(Attribute::ByVal))
    Flags.setByVal();
  if (AttrFn(Attribute::ByRef))
    Flags.setByRef();
  if (AttrFn(Attribute::Preallocated))
    Flags.setPreallocated();
  if (AttrFn(Attribute::InAlloca))
    Flags.setInAlloca();
  if (AttrFn(Attribute::Returned))
    Flags.setReturned();
  if (AttrFn(Attribute::SwiftSelf))
    Flags.setSwiftSelf();
  if (AttrFn(Attribute::SwiftAsync))
    Flags.setSwiftAsync();
  if (AttrFn(Attribute::SwiftError))
    Flags.setSwiftError();
}

ISD::ArgFlagsTy CallLowering::getAttributesForArgIdx(const CallBase &Call,
                                                     unsigned ArgIdx) const {
  ISD::ArgFlagsTy Flags;
  addFlagsUsingAttrFn(Flags, [&Call, &ArgIdx](Attribute::AttrKind Attr) {
    return Call.paramHasAttr(ArgIdx, Attr);
  });
  return Flags;
}

ISD::ArgFlagsTy
CallLowering::getAttributesForReturn(const CallBase &Call) const {
  ISD::ArgFlagsTy Flags;
  addFlagsUsingAttrFn(Flags, [&Call](Attribute::AttrKind Attr) {
    return Call.hasRetAttr(Attr);
  });
  return Flags;
}

void CallLowering::addArgFlagsFromAttributes(ISD::ArgFlagsTy &Flags,
                                             const AttributeList &Attrs,
                                             unsigned OpIdx) const {
  addFlagsUsingAttrFn(Flags, [&Attrs, &OpIdx](Attribute::AttrKind Attr) {
    return Attrs.hasAttributeAtIndex(OpIdx, Attr);
  });
}

bool CallLowering::lowerCall(MachineIRBuilder &MIRBuilder, const CallBase &CB,
                             ArrayRef<Register> ResRegs,
                             ArrayRef<ArrayRef<Register>> ArgRegs,
                             Register SwiftErrorVReg,
                             std::optional<PtrAuthInfo> PAI,
                             Register ConvergenceCtrlToken,
                             std::function<unsigned()> GetCalleeReg) const {
  CallLoweringInfo Info;
  const DataLayout &DL = MIRBuilder.getDataLayout();
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool CanBeTailCalled = CB.isTailCall() &&
                         isInTailCallPosition(CB, MF.getTarget()) &&
                         (MF.getFunction()
                              .getFnAttribute("disable-tail-calls")
                              .getValueAsString() != "true");

  CallingConv::ID CallConv = CB.getCallingConv();
  Type *RetTy = CB.getType();
  bool IsVarArg = CB.getFunctionType()->isVarArg();

  SmallVector<BaseArgInfo, 4> SplitArgs;
  getReturnInfo(CallConv, RetTy, CB.getAttributes(), SplitArgs, DL);
  Info.CanLowerReturn = canLowerReturn(MF, CallConv, SplitArgs, IsVarArg);

  Info.IsConvergent = CB.isConvergent();

  if (!Info.CanLowerReturn) {
    // Callee requires sret demotion.
    insertSRetOutgoingArgument(MIRBuilder, CB, Info);

    // The sret demotion isn't compatible with tail-calls, since the sret
    // argument points into the caller's stack frame.
    CanBeTailCalled = false;
  }

  // First step is to marshall all the function's parameters into the correct
  // physregs and memory locations. Gather the sequence of argument types that
  // we'll pass to the assigner function.
  unsigned i = 0;
  unsigned NumFixedArgs = CB.getFunctionType()->getNumParams();
  for (const auto &Arg : CB.args()) {
    ArgInfo OrigArg{ArgRegs[i], *Arg.get(), i, getAttributesForArgIdx(CB, i),
                    i < NumFixedArgs};
    setArgFlags(OrigArg, i + AttributeList::FirstArgIndex, DL, CB);

    // If we have an explicit sret argument that is an Instruction, (i.e., it
    // might point to function-local memory), we can't meaningfully tail-call.
    if (OrigArg.Flags[0].isSRet() && isa<Instruction>(&Arg))
      CanBeTailCalled = false;

    Info.OrigArgs.push_back(OrigArg);
    ++i;
  }

  // Try looking through a bitcast from one function type to another.
  // Commonly happens with calls to objc_msgSend().
  const Value *CalleeV = CB.getCalledOperand()->stripPointerCasts();

  // If IRTranslator chose to drop the ptrauth info, we can turn this into
  // a direct call.
  if (!PAI && CB.countOperandBundlesOfType(LLVMContext::OB_ptrauth)) {
    CalleeV = cast<ConstantPtrAuth>(CalleeV)->getPointer();
    assert(isa<Function>(CalleeV));
  }

  if (const Function *F = dyn_cast<Function>(CalleeV)) {
    if (F->hasFnAttribute(Attribute::NonLazyBind)) {
      LLT Ty = getLLTForType(*F->getType(), DL);
      Register Reg = MIRBuilder.buildGlobalValue(Ty, F).getReg(0);
      Info.Callee = MachineOperand::CreateReg(Reg, false);
    } else {
      Info.Callee = MachineOperand::CreateGA(F, 0);
    }
  } else if (isa<GlobalIFunc>(CalleeV) || isa<GlobalAlias>(CalleeV)) {
    // IR IFuncs and Aliases can't be forward declared (only defined), so the
    // callee must be in the same TU and therefore we can direct-call it without
    // worrying about it being out of range.
    Info.Callee = MachineOperand::CreateGA(cast<GlobalValue>(CalleeV), 0);
  } else
    Info.Callee = MachineOperand::CreateReg(GetCalleeReg(), false);

  Register ReturnHintAlignReg;
  Align ReturnHintAlign;

  Info.OrigRet = ArgInfo{ResRegs, RetTy, 0, getAttributesForReturn(CB)};

  if (!Info.OrigRet.Ty->isVoidTy()) {
    setArgFlags(Info.OrigRet, AttributeList::ReturnIndex, DL, CB);

    if (MaybeAlign Alignment = CB.getRetAlign()) {
      if (*Alignment > Align(1)) {
        ReturnHintAlignReg = MRI.cloneVirtualRegister(ResRegs[0]);
        Info.OrigRet.Regs[0] = ReturnHintAlignReg;
        ReturnHintAlign = *Alignment;
      }
    }
  }

  auto Bundle = CB.getOperandBundle(LLVMContext::OB_kcfi);
  if (Bundle && CB.isIndirectCall()) {
    Info.CFIType = cast<ConstantInt>(Bundle->Inputs[0]);
    assert(Info.CFIType->getType()->isIntegerTy(32) && "Invalid CFI type");
  }

  Info.CB = &CB;
  Info.KnownCallees = CB.getMetadata(LLVMContext::MD_callees);
  Info.CallConv = CallConv;
  Info.SwiftErrorVReg = SwiftErrorVReg;
  Info.PAI = PAI;
  Info.ConvergenceCtrlToken = ConvergenceCtrlToken;
  Info.IsMustTailCall = CB.isMustTailCall();
  Info.IsTailCall = CanBeTailCalled;
  Info.IsVarArg = IsVarArg;
  if (!lowerCall(MIRBuilder, Info))
    return false;

  if (ReturnHintAlignReg && !Info.LoweredTailCall) {
    MIRBuilder.buildAssertAlign(ResRegs[0], ReturnHintAlignReg,
                                ReturnHintAlign);
  }

  return true;
}

template <typename FuncInfoTy>
void CallLowering::setArgFlags(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                               const DataLayout &DL,
                               const FuncInfoTy &FuncInfo) const {
  auto &Flags = Arg.Flags[0];
  const AttributeList &Attrs = FuncInfo.getAttributes();
  addArgFlagsFromAttributes(Flags, Attrs, OpIdx);

  PointerType *PtrTy = dyn_cast<PointerType>(Arg.Ty->getScalarType());
  if (PtrTy) {
    Flags.setPointer();
    Flags.setPointerAddrSpace(PtrTy->getPointerAddressSpace());
  }

  Align MemAlign = DL.getABITypeAlign(Arg.Ty);
  if (Flags.isByVal() || Flags.isInAlloca() || Flags.isPreallocated() ||
      Flags.isByRef()) {
    assert(OpIdx >= AttributeList::FirstArgIndex);
    unsigned ParamIdx = OpIdx - AttributeList::FirstArgIndex;

    Type *ElementTy = FuncInfo.getParamByValType(ParamIdx);
    if (!ElementTy)
      ElementTy = FuncInfo.getParamByRefType(ParamIdx);
    if (!ElementTy)
      ElementTy = FuncInfo.getParamInAllocaType(ParamIdx);
    if (!ElementTy)
      ElementTy = FuncInfo.getParamPreallocatedType(ParamIdx);

    assert(ElementTy && "Must have byval, inalloca or preallocated type");

    uint64_t MemSize = DL.getTypeAllocSize(ElementTy);
    if (Flags.isByRef())
      Flags.setByRefSize(MemSize);
    else
      Flags.setByValSize(MemSize);

    // For ByVal, alignment should be passed from FE.  BE will guess if
    // this info is not there but there are cases it cannot get right.
    if (auto ParamAlign = FuncInfo.getParamStackAlign(ParamIdx))
      MemAlign = *ParamAlign;
    else if ((ParamAlign = FuncInfo.getParamAlign(ParamIdx)))
      MemAlign = *ParamAlign;
    else
      MemAlign = Align(getTLI()->getByValTypeAlignment(ElementTy, DL));
  } else if (OpIdx >= AttributeList::FirstArgIndex) {
    if (auto ParamAlign =
            FuncInfo.getParamStackAlign(OpIdx - AttributeList::FirstArgIndex))
      MemAlign = *ParamAlign;
  }
  Flags.setMemAlign(MemAlign);
  Flags.setOrigAlign(DL.getABITypeAlign(Arg.Ty));

  // Don't try to use the returned attribute if the argument is marked as
  // swiftself, since it won't be passed in x0.
  if (Flags.isSwiftSelf())
    Flags.setReturned(false);
}

template void
CallLowering::setArgFlags<Function>(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                                    const DataLayout &DL,
                                    const Function &FuncInfo) const;

template void
CallLowering::setArgFlags<CallBase>(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                                    const DataLayout &DL,
                                    const CallBase &FuncInfo) const;

void CallLowering::splitToValueTypes(const ArgInfo &OrigArg,
                                     SmallVectorImpl<ArgInfo> &SplitArgs,
                                     const DataLayout &DL,
                                     CallingConv::ID CallConv,
                                     SmallVectorImpl<uint64_t> *Offsets) const {
  LLVMContext &Ctx = OrigArg.Ty->getContext();

  SmallVector<EVT, 4> SplitVTs;
  ComputeValueVTs(*TLI, DL, OrigArg.Ty, SplitVTs, Offsets, 0);

  if (SplitVTs.size() == 0)
    return;

  if (SplitVTs.size() == 1) {
    // No splitting to do, but we want to replace the original type (e.g. [1 x
    // double] -> double).
    SplitArgs.emplace_back(OrigArg.Regs[0], SplitVTs[0].getTypeForEVT(Ctx),
                           OrigArg.OrigArgIndex, OrigArg.Flags[0],
                           OrigArg.IsFixed, OrigArg.OrigValue);
    return;
  }

  // Create one ArgInfo for each virtual register in the original ArgInfo.
  assert(OrigArg.Regs.size() == SplitVTs.size() && "Regs / types mismatch");

  bool NeedsRegBlock = TLI->functionArgumentNeedsConsecutiveRegisters(
      OrigArg.Ty, CallConv, false, DL);
  for (unsigned i = 0, e = SplitVTs.size(); i < e; ++i) {
    Type *SplitTy = SplitVTs[i].getTypeForEVT(Ctx);
    SplitArgs.emplace_back(OrigArg.Regs[i], SplitTy, OrigArg.OrigArgIndex,
                           OrigArg.Flags[0], OrigArg.IsFixed);
    if (NeedsRegBlock)
      SplitArgs.back().Flags[0].setInConsecutiveRegs();
  }

  SplitArgs.back().Flags[0].setInConsecutiveRegsLast();
}

/// Pack values \p SrcRegs to cover the vector type result \p DstRegs.
static MachineInstrBuilder
mergeVectorRegsToResultRegs(MachineIRBuilder &B, ArrayRef<Register> DstRegs,
                            ArrayRef<Register> SrcRegs) {
  MachineRegisterInfo &MRI = *B.getMRI();
  LLT LLTy = MRI.getType(DstRegs[0]);
  LLT PartLLT = MRI.getType(SrcRegs[0]);

  // Deal with v3s16 split into v2s16
  LLT LCMTy = getCoverTy(LLTy, PartLLT);
  if (LCMTy == LLTy) {
    // Common case where no padding is needed.
    assert(DstRegs.size() == 1);
    return B.buildConcatVectors(DstRegs[0], SrcRegs);
  }

  // We need to create an unmerge to the result registers, which may require
  // widening the original value.
  Register UnmergeSrcReg;
  if (LCMTy != PartLLT) {
    assert(DstRegs.size() == 1);
    return B.buildDeleteTrailingVectorElements(
        DstRegs[0], B.buildMergeLikeInstr(LCMTy, SrcRegs));
  } else {
    // We don't need to widen anything if we're extracting a scalar which was
    // promoted to a vector e.g. s8 -> v4s8 -> s8
    assert(SrcRegs.size() == 1);
    UnmergeSrcReg = SrcRegs[0];
  }

  int NumDst = LCMTy.getSizeInBits() / LLTy.getSizeInBits();

  SmallVector<Register, 8> PadDstRegs(NumDst);
  std::copy(DstRegs.begin(), DstRegs.end(), PadDstRegs.begin());

  // Create the excess dead defs for the unmerge.
  for (int I = DstRegs.size(); I != NumDst; ++I)
    PadDstRegs[I] = MRI.createGenericVirtualRegister(LLTy);

  if (PadDstRegs.size() == 1)
    return B.buildDeleteTrailingVectorElements(DstRegs[0], UnmergeSrcReg);
  return B.buildUnmerge(PadDstRegs, UnmergeSrcReg);
}

/// Create a sequence of instructions to combine pieces split into register
/// typed values to the original IR value. \p OrigRegs contains the destination
/// value registers of type \p LLTy, and \p Regs contains the legalized pieces
/// with type \p PartLLT. This is used for incoming values (physregs to vregs).
static void buildCopyFromRegs(MachineIRBuilder &B, ArrayRef<Register> OrigRegs,
                              ArrayRef<Register> Regs, LLT LLTy, LLT PartLLT,
                              const ISD::ArgFlagsTy Flags) {
  MachineRegisterInfo &MRI = *B.getMRI();

  if (PartLLT == LLTy) {
    // We should have avoided introducing a new virtual register, and just
    // directly assigned here.
    assert(OrigRegs[0] == Regs[0]);
    return;
  }

  if (PartLLT.getSizeInBits() == LLTy.getSizeInBits() && OrigRegs.size() == 1 &&
      Regs.size() == 1) {
    B.buildBitcast(OrigRegs[0], Regs[0]);
    return;
  }

  // A vector PartLLT needs extending to LLTy's element size.
  // E.g. <2 x s64> = G_SEXT <2 x s32>.
  if (PartLLT.isVector() == LLTy.isVector() &&
      PartLLT.getScalarSizeInBits() > LLTy.getScalarSizeInBits() &&
      (!PartLLT.isVector() ||
       PartLLT.getElementCount() == LLTy.getElementCount()) &&
      OrigRegs.size() == 1 && Regs.size() == 1) {
    Register SrcReg = Regs[0];

    LLT LocTy = MRI.getType(SrcReg);

    if (Flags.isSExt()) {
      SrcReg = B.buildAssertSExt(LocTy, SrcReg, LLTy.getScalarSizeInBits())
                   .getReg(0);
    } else if (Flags.isZExt()) {
      SrcReg = B.buildAssertZExt(LocTy, SrcReg, LLTy.getScalarSizeInBits())
                   .getReg(0);
    }

    // Sometimes pointers are passed zero extended.
    LLT OrigTy = MRI.getType(OrigRegs[0]);
    if (OrigTy.isPointer()) {
      LLT IntPtrTy = LLT::scalar(OrigTy.getSizeInBits());
      B.buildIntToPtr(OrigRegs[0], B.buildTrunc(IntPtrTy, SrcReg));
      return;
    }

    B.buildTrunc(OrigRegs[0], SrcReg);
    return;
  }

  if (!LLTy.isVector() && !PartLLT.isVector()) {
    assert(OrigRegs.size() == 1);
    LLT OrigTy = MRI.getType(OrigRegs[0]);

    unsigned SrcSize = PartLLT.getSizeInBits().getFixedValue() * Regs.size();
    if (SrcSize == OrigTy.getSizeInBits())
      B.buildMergeValues(OrigRegs[0], Regs);
    else {
      auto Widened = B.buildMergeLikeInstr(LLT::scalar(SrcSize), Regs);
      B.buildTrunc(OrigRegs[0], Widened);
    }

    return;
  }

  if (PartLLT.isVector()) {
    assert(OrigRegs.size() == 1);
    SmallVector<Register> CastRegs(Regs.begin(), Regs.end());

    // If PartLLT is a mismatched vector in both number of elements and element
    // size, e.g. PartLLT == v2s64 and LLTy is v3s32, then first coerce it to
    // have the same elt type, i.e. v4s32.
    // TODO: Extend this coersion to element multiples other than just 2.
    if (TypeSize::isKnownGT(PartLLT.getSizeInBits(), LLTy.getSizeInBits()) &&
        PartLLT.getScalarSizeInBits() == LLTy.getScalarSizeInBits() * 2 &&
        Regs.size() == 1) {
      LLT NewTy = PartLLT.changeElementType(LLTy.getElementType())
                      .changeElementCount(PartLLT.getElementCount() * 2);
      CastRegs[0] = B.buildBitcast(NewTy, Regs[0]).getReg(0);
      PartLLT = NewTy;
    }

    if (LLTy.getScalarType() == PartLLT.getElementType()) {
      mergeVectorRegsToResultRegs(B, OrigRegs, CastRegs);
    } else {
      unsigned I = 0;
      LLT GCDTy = getGCDType(LLTy, PartLLT);

      // We are both splitting a vector, and bitcasting its element types. Cast
      // the source pieces into the appropriate number of pieces with the result
      // element type.
      for (Register SrcReg : CastRegs)
        CastRegs[I++] = B.buildBitcast(GCDTy, SrcReg).getReg(0);
      mergeVectorRegsToResultRegs(B, OrigRegs, CastRegs);
    }

    return;
  }

  assert(LLTy.isVector() && !PartLLT.isVector());

  LLT DstEltTy = LLTy.getElementType();

  // Pointer information was discarded. We'll need to coerce some register types
  // to avoid violating type constraints.
  LLT RealDstEltTy = MRI.getType(OrigRegs[0]).getElementType();

  assert(DstEltTy.getSizeInBits() == RealDstEltTy.getSizeInBits());

  if (DstEltTy == PartLLT) {
    // Vector was trivially scalarized.

    if (RealDstEltTy.isPointer()) {
      for (Register Reg : Regs)
        MRI.setType(Reg, RealDstEltTy);
    }

    B.buildBuildVector(OrigRegs[0], Regs);
  } else if (DstEltTy.getSizeInBits() > PartLLT.getSizeInBits()) {
    // Deal with vector with 64-bit elements decomposed to 32-bit
    // registers. Need to create intermediate 64-bit elements.
    SmallVector<Register, 8> EltMerges;
    int PartsPerElt =
        divideCeil(DstEltTy.getSizeInBits(), PartLLT.getSizeInBits());
    LLT ExtendedPartTy = LLT::scalar(PartLLT.getSizeInBits() * PartsPerElt);

    for (int I = 0, NumElts = LLTy.getNumElements(); I != NumElts; ++I) {
      auto Merge =
          B.buildMergeLikeInstr(ExtendedPartTy, Regs.take_front(PartsPerElt));
      if (ExtendedPartTy.getSizeInBits() > RealDstEltTy.getSizeInBits())
        Merge = B.buildTrunc(RealDstEltTy, Merge);
      // Fix the type in case this is really a vector of pointers.
      MRI.setType(Merge.getReg(0), RealDstEltTy);
      EltMerges.push_back(Merge.getReg(0));
      Regs = Regs.drop_front(PartsPerElt);
    }

    B.buildBuildVector(OrigRegs[0], EltMerges);
  } else {
    // Vector was split, and elements promoted to a wider type.
    // FIXME: Should handle floating point promotions.
    unsigned NumElts = LLTy.getNumElements();
    LLT BVType = LLT::fixed_vector(NumElts, PartLLT);

    Register BuildVec;
    if (NumElts == Regs.size())
      BuildVec = B.buildBuildVector(BVType, Regs).getReg(0);
    else {
      // Vector elements are packed in the inputs.
      // e.g. we have a <4 x s16> but 2 x s32 in regs.
      assert(NumElts > Regs.size());
      LLT SrcEltTy = MRI.getType(Regs[0]);

      LLT OriginalEltTy = MRI.getType(OrigRegs[0]).getElementType();

      // Input registers contain packed elements.
      // Determine how many elements per reg.
      assert((SrcEltTy.getSizeInBits() % OriginalEltTy.getSizeInBits()) == 0);
      unsigned EltPerReg =
          (SrcEltTy.getSizeInBits() / OriginalEltTy.getSizeInBits());

      SmallVector<Register, 0> BVRegs;
      BVRegs.reserve(Regs.size() * EltPerReg);
      for (Register R : Regs) {
        auto Unmerge = B.buildUnmerge(OriginalEltTy, R);
        for (unsigned K = 0; K < EltPerReg; ++K)
          BVRegs.push_back(B.buildAnyExt(PartLLT, Unmerge.getReg(K)).getReg(0));
      }

      // We may have some more elements in BVRegs, e.g. if we have 2 s32 pieces
      // for a <3 x s16> vector. We should have less than EltPerReg extra items.
      if (BVRegs.size() > NumElts) {
        assert((BVRegs.size() - NumElts) < EltPerReg);
        BVRegs.truncate(NumElts);
      }
      BuildVec = B.buildBuildVector(BVType, BVRegs).getReg(0);
    }
    B.buildTrunc(OrigRegs[0], BuildVec);
  }
}

/// Create a sequence of instructions to expand the value in \p SrcReg (of type
/// \p SrcTy) to the types in \p DstRegs (of type \p PartTy). \p ExtendOp should
/// contain the type of scalar value extension if necessary.
///
/// This is used for outgoing values (vregs to physregs)
static void buildCopyToRegs(MachineIRBuilder &B, ArrayRef<Register> DstRegs,
                            Register SrcReg, LLT SrcTy, LLT PartTy,
                            unsigned ExtendOp = TargetOpcode::G_ANYEXT) {
  // We could just insert a regular copy, but this is unreachable at the moment.
  assert(SrcTy != PartTy && "identical part types shouldn't reach here");

  const TypeSize PartSize = PartTy.getSizeInBits();

  if (PartTy.isVector() == SrcTy.isVector() &&
      PartTy.getScalarSizeInBits() > SrcTy.getScalarSizeInBits()) {
    assert(DstRegs.size() == 1);
    B.buildInstr(ExtendOp, {DstRegs[0]}, {SrcReg});
    return;
  }

  if (SrcTy.isVector() && !PartTy.isVector() &&
      TypeSize::isKnownGT(PartSize, SrcTy.getElementType().getSizeInBits())) {
    // Vector was scalarized, and the elements extended.
    auto UnmergeToEltTy = B.buildUnmerge(SrcTy.getElementType(), SrcReg);
    for (int i = 0, e = DstRegs.size(); i != e; ++i)
      B.buildAnyExt(DstRegs[i], UnmergeToEltTy.getReg(i));
    return;
  }

  if (SrcTy.isVector() && PartTy.isVector() &&
      PartTy.getSizeInBits() == SrcTy.getSizeInBits() &&
      ElementCount::isKnownLT(SrcTy.getElementCount(),
                              PartTy.getElementCount())) {
    // A coercion like: v2f32 -> v4f32 or nxv2f32 -> nxv4f32
    Register DstReg = DstRegs.front();
    B.buildPadVectorWithUndefElements(DstReg, SrcReg);
    return;
  }

  LLT GCDTy = getGCDType(SrcTy, PartTy);
  if (GCDTy == PartTy) {
    // If this already evenly divisible, we can create a simple unmerge.
    B.buildUnmerge(DstRegs, SrcReg);
    return;
  }

  if (SrcTy.isVector() && !PartTy.isVector() &&
      SrcTy.getScalarSizeInBits() > PartTy.getSizeInBits()) {
    LLT ExtTy =
        LLT::vector(SrcTy.getElementCount(),
                    LLT::scalar(PartTy.getScalarSizeInBits() * DstRegs.size() /
                                SrcTy.getNumElements()));
    auto Ext = B.buildAnyExt(ExtTy, SrcReg);
    B.buildUnmerge(DstRegs, Ext);
    return;
  }

  MachineRegisterInfo &MRI = *B.getMRI();
  LLT DstTy = MRI.getType(DstRegs[0]);
  LLT LCMTy = getCoverTy(SrcTy, PartTy);

  if (PartTy.isVector() && LCMTy == PartTy) {
    assert(DstRegs.size() == 1);
    B.buildPadVectorWithUndefElements(DstRegs[0], SrcReg);
    return;
  }

  const unsigned DstSize = DstTy.getSizeInBits();
  const unsigned SrcSize = SrcTy.getSizeInBits();
  unsigned CoveringSize = LCMTy.getSizeInBits();

  Register UnmergeSrc = SrcReg;

  if (!LCMTy.isVector() && CoveringSize != SrcSize) {
    // For scalars, it's common to be able to use a simple extension.
    if (SrcTy.isScalar() && DstTy.isScalar()) {
      CoveringSize = alignTo(SrcSize, DstSize);
      LLT CoverTy = LLT::scalar(CoveringSize);
      UnmergeSrc = B.buildInstr(ExtendOp, {CoverTy}, {SrcReg}).getReg(0);
    } else {
      // Widen to the common type.
      // FIXME: This should respect the extend type
      Register Undef = B.buildUndef(SrcTy).getReg(0);
      SmallVector<Register, 8> MergeParts(1, SrcReg);
      for (unsigned Size = SrcSize; Size != CoveringSize; Size += SrcSize)
        MergeParts.push_back(Undef);
      UnmergeSrc = B.buildMergeLikeInstr(LCMTy, MergeParts).getReg(0);
    }
  }

  if (LCMTy.isVector() && CoveringSize != SrcSize)
    UnmergeSrc = B.buildPadVectorWithUndefElements(LCMTy, SrcReg).getReg(0);

  B.buildUnmerge(DstRegs, UnmergeSrc);
}

bool CallLowering::determineAndHandleAssignments(
    ValueHandler &Handler, ValueAssigner &Assigner,
    SmallVectorImpl<ArgInfo> &Args, MachineIRBuilder &MIRBuilder,
    CallingConv::ID CallConv, bool IsVarArg,
    ArrayRef<Register> ThisReturnRegs) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  SmallVector<CCValAssign, 16> ArgLocs;

  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, F.getContext());
  if (!determineAssignments(Assigner, Args, CCInfo))
    return false;

  return handleAssignments(Handler, Args, CCInfo, ArgLocs, MIRBuilder,
                           ThisReturnRegs);
}

static unsigned extendOpFromFlags(llvm::ISD::ArgFlagsTy Flags) {
  if (Flags.isSExt())
    return TargetOpcode::G_SEXT;
  if (Flags.isZExt())
    return TargetOpcode::G_ZEXT;
  return TargetOpcode::G_ANYEXT;
}

bool CallLowering::determineAssignments(ValueAssigner &Assigner,
                                        SmallVectorImpl<ArgInfo> &Args,
                                        CCState &CCInfo) const {
  LLVMContext &Ctx = CCInfo.getContext();
  const CallingConv::ID CallConv = CCInfo.getCallingConv();

  unsigned NumArgs = Args.size();
  for (unsigned i = 0; i != NumArgs; ++i) {
    EVT CurVT = EVT::getEVT(Args[i].Ty);

    MVT NewVT = TLI->getRegisterTypeForCallingConv(Ctx, CallConv, CurVT);

    // If we need to split the type over multiple regs, check it's a scenario
    // we currently support.
    unsigned NumParts =
        TLI->getNumRegistersForCallingConv(Ctx, CallConv, CurVT);

    if (NumParts == 1) {
      // Try to use the register type if we couldn't assign the VT.
      if (Assigner.assignArg(i, CurVT, NewVT, NewVT, CCValAssign::Full, Args[i],
                             Args[i].Flags[0], CCInfo))
        return false;
      continue;
    }

    // For incoming arguments (physregs to vregs), we could have values in
    // physregs (or memlocs) which we want to extract and copy to vregs.
    // During this, we might have to deal with the LLT being split across
    // multiple regs, so we have to record this information for later.
    //
    // If we have outgoing args, then we have the opposite case. We have a
    // vreg with an LLT which we want to assign to a physical location, and
    // we might have to record that the value has to be split later.

    // We're handling an incoming arg which is split over multiple regs.
    // E.g. passing an s128 on AArch64.
    ISD::ArgFlagsTy OrigFlags = Args[i].Flags[0];
    Args[i].Flags.clear();

    for (unsigned Part = 0; Part < NumParts; ++Part) {
      ISD::ArgFlagsTy Flags = OrigFlags;
      if (Part == 0) {
        Flags.setSplit();
      } else {
        Flags.setOrigAlign(Align(1));
        if (Part == NumParts - 1)
          Flags.setSplitEnd();
      }

      Args[i].Flags.push_back(Flags);
      if (Assigner.assignArg(i, CurVT, NewVT, NewVT, CCValAssign::Full, Args[i],
                             Args[i].Flags[Part], CCInfo)) {
        // Still couldn't assign this smaller part type for some reason.
        return false;
      }
    }
  }

  return true;
}

bool CallLowering::handleAssignments(ValueHandler &Handler,
                                     SmallVectorImpl<ArgInfo> &Args,
                                     CCState &CCInfo,
                                     SmallVectorImpl<CCValAssign> &ArgLocs,
                                     MachineIRBuilder &MIRBuilder,
                                     ArrayRef<Register> ThisReturnRegs) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Function &F = MF.getFunction();
  const DataLayout &DL = F.getDataLayout();

  const unsigned NumArgs = Args.size();

  // Stores thunks for outgoing register assignments. This is used so we delay
  // generating register copies until mem loc assignments are done. We do this
  // so that if the target is using the delayed stack protector feature, we can
  // find the split point of the block accurately. E.g. if we have:
  // G_STORE %val, %memloc
  // $x0 = COPY %foo
  // $x1 = COPY %bar
  // CALL func
  // ... then the split point for the block will correctly be at, and including,
  // the copy to $x0. If instead the G_STORE instruction immediately precedes
  // the CALL, then we'd prematurely choose the CALL as the split point, thus
  // generating a split block with a CALL that uses undefined physregs.
  SmallVector<std::function<void()>> DelayedOutgoingRegAssignments;

  for (unsigned i = 0, j = 0; i != NumArgs; ++i, ++j) {
    assert(j < ArgLocs.size() && "Skipped too many arg locs");
    CCValAssign &VA = ArgLocs[j];
    assert(VA.getValNo() == i && "Location doesn't correspond to current arg");

    if (VA.needsCustom()) {
      std::function<void()> Thunk;
      unsigned NumArgRegs = Handler.assignCustomValue(
          Args[i], ArrayRef(ArgLocs).slice(j), &Thunk);
      if (Thunk)
        DelayedOutgoingRegAssignments.emplace_back(Thunk);
      if (!NumArgRegs)
        return false;
      j += (NumArgRegs - 1);
      continue;
    }

    auto AllocaAddressSpace = MF.getDataLayout().getAllocaAddrSpace();

    const MVT ValVT = VA.getValVT();
    const MVT LocVT = VA.getLocVT();

    const LLT LocTy(LocVT);
    const LLT ValTy(ValVT);
    const LLT NewLLT = Handler.isIncomingArgumentHandler() ? LocTy : ValTy;
    const EVT OrigVT = EVT::getEVT(Args[i].Ty);
    const LLT OrigTy = getLLTForType(*Args[i].Ty, DL);
    const LLT PointerTy = LLT::pointer(
        AllocaAddressSpace, DL.getPointerSizeInBits(AllocaAddressSpace));

    // Expected to be multiple regs for a single incoming arg.
    // There should be Regs.size() ArgLocs per argument.
    // This should be the same as getNumRegistersForCallingConv
    const unsigned NumParts = Args[i].Flags.size();

    // Now split the registers into the assigned types.
    Args[i].OrigRegs.assign(Args[i].Regs.begin(), Args[i].Regs.end());

    if (NumParts != 1 || NewLLT != OrigTy) {
      // If we can't directly assign the register, we need one or more
      // intermediate values.
      Args[i].Regs.resize(NumParts);

      // When we have indirect parameter passing we are receiving a pointer,
      // that points to the actual value, so we need one "temporary" pointer.
      if (VA.getLocInfo() == CCValAssign::Indirect) {
        if (Handler.isIncomingArgumentHandler())
          Args[i].Regs[0] = MRI.createGenericVirtualRegister(PointerTy);
      } else {
        // For each split register, create and assign a vreg that will store
        // the incoming component of the larger value. These will later be
        // merged to form the final vreg.
        for (unsigned Part = 0; Part < NumParts; ++Part)
          Args[i].Regs[Part] = MRI.createGenericVirtualRegister(NewLLT);
      }
    }

    assert((j + (NumParts - 1)) < ArgLocs.size() &&
           "Too many regs for number of args");

    // Coerce into outgoing value types before register assignment.
    if (!Handler.isIncomingArgumentHandler() && OrigTy != ValTy &&
        VA.getLocInfo() != CCValAssign::Indirect) {
      assert(Args[i].OrigRegs.size() == 1);
      buildCopyToRegs(MIRBuilder, Args[i].Regs, Args[i].OrigRegs[0], OrigTy,
                      ValTy, extendOpFromFlags(Args[i].Flags[0]));
    }

    bool IndirectParameterPassingHandled = false;
    bool BigEndianPartOrdering = TLI->hasBigEndianPartOrdering(OrigVT, DL);
    for (unsigned Part = 0; Part < NumParts; ++Part) {
      assert((VA.getLocInfo() != CCValAssign::Indirect || Part == 0) &&
             "Only the first parameter should be processed when "
             "handling indirect passing!");
      Register ArgReg = Args[i].Regs[Part];
      // There should be Regs.size() ArgLocs per argument.
      unsigned Idx = BigEndianPartOrdering ? NumParts - 1 - Part : Part;
      CCValAssign &VA = ArgLocs[j + Idx];
      const ISD::ArgFlagsTy Flags = Args[i].Flags[Part];

      // We found an indirect parameter passing, and we have an
      // OutgoingValueHandler as our handler (so we are at the call site or the
      // return value). In this case, start the construction of the following
      // GMIR, that is responsible for the preparation of indirect parameter
      // passing:
      //
      // %1(indirectly passed type) = The value to pass
      // %3(pointer) = G_FRAME_INDEX %stack.0
      // G_STORE %1, %3 :: (store (s128), align 8)
      //
      // After this GMIR, the remaining part of the loop body will decide how
      // to get the value to the caller and we break out of the loop.
      if (VA.getLocInfo() == CCValAssign::Indirect &&
          !Handler.isIncomingArgumentHandler()) {
        Align AlignmentForStored = DL.getPrefTypeAlign(Args[i].Ty);
        MachineFrameInfo &MFI = MF.getFrameInfo();
        // Get some space on the stack for the value, so later we can pass it
        // as a reference.
        int FrameIdx = MFI.CreateStackObject(OrigTy.getScalarSizeInBits(),
                                             AlignmentForStored, false);
        Register PointerToStackReg =
            MIRBuilder.buildFrameIndex(PointerTy, FrameIdx).getReg(0);
        MachinePointerInfo StackPointerMPO =
            MachinePointerInfo::getFixedStack(MF, FrameIdx);
        // Store the value in the previously created stack space.
        MIRBuilder.buildStore(Args[i].OrigRegs[Part], PointerToStackReg,
                              StackPointerMPO,
                              inferAlignFromPtrInfo(MF, StackPointerMPO));

        ArgReg = PointerToStackReg;
        IndirectParameterPassingHandled = true;
      }

      if (VA.isMemLoc() && !Flags.isByVal()) {
        // Individual pieces may have been spilled to the stack and others
        // passed in registers.

        // TODO: The memory size may be larger than the value we need to
        // store. We may need to adjust the offset for big endian targets.
        LLT MemTy = Handler.getStackValueStoreType(DL, VA, Flags);

        MachinePointerInfo MPO;
        Register StackAddr =
            Handler.getStackAddress(VA.getLocInfo() == CCValAssign::Indirect
                                        ? PointerTy.getSizeInBytes()
                                        : MemTy.getSizeInBytes(),
                                    VA.getLocMemOffset(), MPO, Flags);

        // Finish the handling of indirect passing from the passers
        // (OutgoingParameterHandler) side.
        // This branch is needed, so the pointer to the value is loaded onto the
        // stack.
        if (VA.getLocInfo() == CCValAssign::Indirect)
          Handler.assignValueToAddress(ArgReg, StackAddr, PointerTy, MPO, VA);
        else
          Handler.assignValueToAddress(Args[i], Part, StackAddr, MemTy, MPO,
                                       VA);
      } else if (VA.isMemLoc() && Flags.isByVal()) {
        assert(Args[i].Regs.size() == 1 && "didn't expect split byval pointer");

        if (Handler.isIncomingArgumentHandler()) {
          // We just need to copy the frame index value to the pointer.
          MachinePointerInfo MPO;
          Register StackAddr = Handler.getStackAddress(
              Flags.getByValSize(), VA.getLocMemOffset(), MPO, Flags);
          MIRBuilder.buildCopy(Args[i].Regs[0], StackAddr);
        } else {
          // For outgoing byval arguments, insert the implicit copy byval
          // implies, such that writes in the callee do not modify the caller's
          // value.
          uint64_t MemSize = Flags.getByValSize();
          int64_t Offset = VA.getLocMemOffset();

          MachinePointerInfo DstMPO;
          Register StackAddr =
              Handler.getStackAddress(MemSize, Offset, DstMPO, Flags);

          MachinePointerInfo SrcMPO(Args[i].OrigValue);
          if (!Args[i].OrigValue) {
            // We still need to accurately track the stack address space if we
            // don't know the underlying value.
            const LLT PtrTy = MRI.getType(StackAddr);
            SrcMPO = MachinePointerInfo(PtrTy.getAddressSpace());
          }

          Align DstAlign = std::max(Flags.getNonZeroByValAlign(),
                                    inferAlignFromPtrInfo(MF, DstMPO));

          Align SrcAlign = std::max(Flags.getNonZeroByValAlign(),
                                    inferAlignFromPtrInfo(MF, SrcMPO));

          Handler.copyArgumentMemory(Args[i], StackAddr, Args[i].Regs[0],
                                     DstMPO, DstAlign, SrcMPO, SrcAlign,
                                     MemSize, VA);
        }
      } else if (i == 0 && !ThisReturnRegs.empty() &&
                 Handler.isIncomingArgumentHandler() &&
                 isTypeIsValidForThisReturn(ValVT)) {
        Handler.assignValueToReg(ArgReg, ThisReturnRegs[Part], VA);
      } else if (Handler.isIncomingArgumentHandler()) {
        Handler.assignValueToReg(ArgReg, VA.getLocReg(), VA);
      } else {
        DelayedOutgoingRegAssignments.emplace_back([=, &Handler]() {
          Handler.assignValueToReg(ArgReg, VA.getLocReg(), VA);
        });
      }

      // Finish the handling of indirect parameter passing when receiving
      // the value (we are in the called function or the caller when receiving
      // the return value).
      if (VA.getLocInfo() == CCValAssign::Indirect &&
          Handler.isIncomingArgumentHandler()) {
        Align Alignment = DL.getABITypeAlign(Args[i].Ty);
        MachinePointerInfo MPO = MachinePointerInfo::getUnknownStack(MF);

        // Since we are doing indirect parameter passing, we know that the value
        // in the temporary register is not the value passed to the function,
        // but rather a pointer to that value. Let's load that value into the
        // virtual register where the parameter should go.
        MIRBuilder.buildLoad(Args[i].OrigRegs[0], Args[i].Regs[0], MPO,
                             Alignment);

        IndirectParameterPassingHandled = true;
      }

      if (IndirectParameterPassingHandled)
        break;
    }

    // Now that all pieces have been assigned, re-pack the register typed values
    // into the original value typed registers. This is only necessary, when
    // the value was passed in multiple registers, not indirectly.
    if (Handler.isIncomingArgumentHandler() && OrigVT != LocVT &&
        !IndirectParameterPassingHandled) {
      // Merge the split registers into the expected larger result vregs of
      // the original call.
      buildCopyFromRegs(MIRBuilder, Args[i].OrigRegs, Args[i].Regs, OrigTy,
                        LocTy, Args[i].Flags[0]);
    }

    j += NumParts - 1;
  }
  for (auto &Fn : DelayedOutgoingRegAssignments)
    Fn();

  return true;
}

void CallLowering::insertSRetLoads(MachineIRBuilder &MIRBuilder, Type *RetTy,
                                   ArrayRef<Register> VRegs, Register DemoteReg,
                                   int FI) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = MF.getDataLayout();

  SmallVector<EVT, 4> SplitVTs;
  SmallVector<uint64_t, 4> Offsets;
  ComputeValueVTs(*TLI, DL, RetTy, SplitVTs, &Offsets, 0);

  assert(VRegs.size() == SplitVTs.size());

  unsigned NumValues = SplitVTs.size();
  Align BaseAlign = DL.getPrefTypeAlign(RetTy);
  Type *RetPtrTy =
      PointerType::get(RetTy->getContext(), DL.getAllocaAddrSpace());
  LLT OffsetLLTy = getLLTForType(*DL.getIndexType(RetPtrTy), DL);

  MachinePointerInfo PtrInfo = MachinePointerInfo::getFixedStack(MF, FI);

  for (unsigned I = 0; I < NumValues; ++I) {
    Register Addr;
    MIRBuilder.materializePtrAdd(Addr, DemoteReg, OffsetLLTy, Offsets[I]);
    auto *MMO = MF.getMachineMemOperand(PtrInfo, MachineMemOperand::MOLoad,
                                        MRI.getType(VRegs[I]),
                                        commonAlignment(BaseAlign, Offsets[I]));
    MIRBuilder.buildLoad(VRegs[I], Addr, *MMO);
  }
}

void CallLowering::insertSRetStores(MachineIRBuilder &MIRBuilder, Type *RetTy,
                                    ArrayRef<Register> VRegs,
                                    Register DemoteReg) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = MF.getDataLayout();

  SmallVector<EVT, 4> SplitVTs;
  SmallVector<uint64_t, 4> Offsets;
  ComputeValueVTs(*TLI, DL, RetTy, SplitVTs, &Offsets, 0);

  assert(VRegs.size() == SplitVTs.size());

  unsigned NumValues = SplitVTs.size();
  Align BaseAlign = DL.getPrefTypeAlign(RetTy);
  unsigned AS = DL.getAllocaAddrSpace();
  LLT OffsetLLTy = getLLTForType(*DL.getIndexType(RetTy->getPointerTo(AS)), DL);

  MachinePointerInfo PtrInfo(AS);

  for (unsigned I = 0; I < NumValues; ++I) {
    Register Addr;
    MIRBuilder.materializePtrAdd(Addr, DemoteReg, OffsetLLTy, Offsets[I]);
    auto *MMO = MF.getMachineMemOperand(PtrInfo, MachineMemOperand::MOStore,
                                        MRI.getType(VRegs[I]),
                                        commonAlignment(BaseAlign, Offsets[I]));
    MIRBuilder.buildStore(VRegs[I], Addr, *MMO);
  }
}

void CallLowering::insertSRetIncomingArgument(
    const Function &F, SmallVectorImpl<ArgInfo> &SplitArgs, Register &DemoteReg,
    MachineRegisterInfo &MRI, const DataLayout &DL) const {
  unsigned AS = DL.getAllocaAddrSpace();
  DemoteReg = MRI.createGenericVirtualRegister(
      LLT::pointer(AS, DL.getPointerSizeInBits(AS)));

  Type *PtrTy = PointerType::get(F.getReturnType(), AS);

  SmallVector<EVT, 1> ValueVTs;
  ComputeValueVTs(*TLI, DL, PtrTy, ValueVTs);

  // NOTE: Assume that a pointer won't get split into more than one VT.
  assert(ValueVTs.size() == 1);

  ArgInfo DemoteArg(DemoteReg, ValueVTs[0].getTypeForEVT(PtrTy->getContext()),
                    ArgInfo::NoArgIndex);
  setArgFlags(DemoteArg, AttributeList::ReturnIndex, DL, F);
  DemoteArg.Flags[0].setSRet();
  SplitArgs.insert(SplitArgs.begin(), DemoteArg);
}

void CallLowering::insertSRetOutgoingArgument(MachineIRBuilder &MIRBuilder,
                                              const CallBase &CB,
                                              CallLoweringInfo &Info) const {
  const DataLayout &DL = MIRBuilder.getDataLayout();
  Type *RetTy = CB.getType();
  unsigned AS = DL.getAllocaAddrSpace();
  LLT FramePtrTy = LLT::pointer(AS, DL.getPointerSizeInBits(AS));

  int FI = MIRBuilder.getMF().getFrameInfo().CreateStackObject(
      DL.getTypeAllocSize(RetTy), DL.getPrefTypeAlign(RetTy), false);

  Register DemoteReg = MIRBuilder.buildFrameIndex(FramePtrTy, FI).getReg(0);
  ArgInfo DemoteArg(DemoteReg, PointerType::get(RetTy, AS),
                    ArgInfo::NoArgIndex);
  setArgFlags(DemoteArg, AttributeList::ReturnIndex, DL, CB);
  DemoteArg.Flags[0].setSRet();

  Info.OrigArgs.insert(Info.OrigArgs.begin(), DemoteArg);
  Info.DemoteStackIndex = FI;
  Info.DemoteRegister = DemoteReg;
}

bool CallLowering::checkReturn(CCState &CCInfo,
                               SmallVectorImpl<BaseArgInfo> &Outs,
                               CCAssignFn *Fn) const {
  for (unsigned I = 0, E = Outs.size(); I < E; ++I) {
    MVT VT = MVT::getVT(Outs[I].Ty);
    if (Fn(I, VT, VT, CCValAssign::Full, Outs[I].Flags[0], CCInfo))
      return false;
  }
  return true;
}

void CallLowering::getReturnInfo(CallingConv::ID CallConv, Type *RetTy,
                                 AttributeList Attrs,
                                 SmallVectorImpl<BaseArgInfo> &Outs,
                                 const DataLayout &DL) const {
  LLVMContext &Context = RetTy->getContext();
  ISD::ArgFlagsTy Flags = ISD::ArgFlagsTy();

  SmallVector<EVT, 4> SplitVTs;
  ComputeValueVTs(*TLI, DL, RetTy, SplitVTs);
  addArgFlagsFromAttributes(Flags, Attrs, AttributeList::ReturnIndex);

  for (EVT VT : SplitVTs) {
    unsigned NumParts =
        TLI->getNumRegistersForCallingConv(Context, CallConv, VT);
    MVT RegVT = TLI->getRegisterTypeForCallingConv(Context, CallConv, VT);
    Type *PartTy = EVT(RegVT).getTypeForEVT(Context);

    for (unsigned I = 0; I < NumParts; ++I) {
      Outs.emplace_back(PartTy, Flags);
    }
  }
}

bool CallLowering::checkReturnTypeForCallConv(MachineFunction &MF) const {
  const auto &F = MF.getFunction();
  Type *ReturnType = F.getReturnType();
  CallingConv::ID CallConv = F.getCallingConv();

  SmallVector<BaseArgInfo, 4> SplitArgs;
  getReturnInfo(CallConv, ReturnType, F.getAttributes(), SplitArgs,
                MF.getDataLayout());
  return canLowerReturn(MF, CallConv, SplitArgs, F.isVarArg());
}

bool CallLowering::parametersInCSRMatch(
    const MachineRegisterInfo &MRI, const uint32_t *CallerPreservedMask,
    const SmallVectorImpl<CCValAssign> &OutLocs,
    const SmallVectorImpl<ArgInfo> &OutArgs) const {
  for (unsigned i = 0; i < OutLocs.size(); ++i) {
    const auto &ArgLoc = OutLocs[i];
    // If it's not a register, it's fine.
    if (!ArgLoc.isRegLoc())
      continue;

    MCRegister PhysReg = ArgLoc.getLocReg();

    // Only look at callee-saved registers.
    if (MachineOperand::clobbersPhysReg(CallerPreservedMask, PhysReg))
      continue;

    LLVM_DEBUG(
        dbgs()
        << "... Call has an argument passed in a callee-saved register.\n");

    // Check if it was copied from.
    const ArgInfo &OutInfo = OutArgs[i];

    if (OutInfo.Regs.size() > 1) {
      LLVM_DEBUG(
          dbgs() << "... Cannot handle arguments in multiple registers.\n");
      return false;
    }

    // Check if we copy the register, walking through copies from virtual
    // registers. Note that getDefIgnoringCopies does not ignore copies from
    // physical registers.
    MachineInstr *RegDef = getDefIgnoringCopies(OutInfo.Regs[0], MRI);
    if (!RegDef || RegDef->getOpcode() != TargetOpcode::COPY) {
      LLVM_DEBUG(
          dbgs()
          << "... Parameter was not copied into a VReg, cannot tail call.\n");
      return false;
    }

    // Got a copy. Verify that it's the same as the register we want.
    Register CopyRHS = RegDef->getOperand(1).getReg();
    if (CopyRHS != PhysReg) {
      LLVM_DEBUG(dbgs() << "... Callee-saved register was not copied into "
                           "VReg, cannot tail call.\n");
      return false;
    }
  }

  return true;
}

bool CallLowering::resultsCompatible(CallLoweringInfo &Info,
                                     MachineFunction &MF,
                                     SmallVectorImpl<ArgInfo> &InArgs,
                                     ValueAssigner &CalleeAssigner,
                                     ValueAssigner &CallerAssigner) const {
  const Function &F = MF.getFunction();
  CallingConv::ID CalleeCC = Info.CallConv;
  CallingConv::ID CallerCC = F.getCallingConv();

  if (CallerCC == CalleeCC)
    return true;

  SmallVector<CCValAssign, 16> ArgLocs1;
  CCState CCInfo1(CalleeCC, Info.IsVarArg, MF, ArgLocs1, F.getContext());
  if (!determineAssignments(CalleeAssigner, InArgs, CCInfo1))
    return false;

  SmallVector<CCValAssign, 16> ArgLocs2;
  CCState CCInfo2(CallerCC, F.isVarArg(), MF, ArgLocs2, F.getContext());
  if (!determineAssignments(CallerAssigner, InArgs, CCInfo2))
    return false;

  // We need the argument locations to match up exactly. If there's more in
  // one than the other, then we are done.
  if (ArgLocs1.size() != ArgLocs2.size())
    return false;

  // Make sure that each location is passed in exactly the same way.
  for (unsigned i = 0, e = ArgLocs1.size(); i < e; ++i) {
    const CCValAssign &Loc1 = ArgLocs1[i];
    const CCValAssign &Loc2 = ArgLocs2[i];

    // We need both of them to be the same. So if one is a register and one
    // isn't, we're done.
    if (Loc1.isRegLoc() != Loc2.isRegLoc())
      return false;

    if (Loc1.isRegLoc()) {
      // If they don't have the same register location, we're done.
      if (Loc1.getLocReg() != Loc2.getLocReg())
        return false;

      // They matched, so we can move to the next ArgLoc.
      continue;
    }

    // Loc1 wasn't a RegLoc, so they both must be MemLocs. Check if they match.
    if (Loc1.getLocMemOffset() != Loc2.getLocMemOffset())
      return false;
  }

  return true;
}

LLT CallLowering::ValueHandler::getStackValueStoreType(
    const DataLayout &DL, const CCValAssign &VA, ISD::ArgFlagsTy Flags) const {
  const MVT ValVT = VA.getValVT();
  if (ValVT != MVT::iPTR) {
    LLT ValTy(ValVT);

    // We lost the pointeriness going through CCValAssign, so try to restore it
    // based on the flags.
    if (Flags.isPointer()) {
      LLT PtrTy = LLT::pointer(Flags.getPointerAddrSpace(),
                               ValTy.getScalarSizeInBits());
      if (ValVT.isVector())
        return LLT::vector(ValTy.getElementCount(), PtrTy);
      return PtrTy;
    }

    return ValTy;
  }

  unsigned AddrSpace = Flags.getPointerAddrSpace();
  return LLT::pointer(AddrSpace, DL.getPointerSize(AddrSpace));
}

void CallLowering::ValueHandler::copyArgumentMemory(
    const ArgInfo &Arg, Register DstPtr, Register SrcPtr,
    const MachinePointerInfo &DstPtrInfo, Align DstAlign,
    const MachinePointerInfo &SrcPtrInfo, Align SrcAlign, uint64_t MemSize,
    CCValAssign &VA) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineMemOperand *SrcMMO = MF.getMachineMemOperand(
      SrcPtrInfo,
      MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable, MemSize,
      SrcAlign);

  MachineMemOperand *DstMMO = MF.getMachineMemOperand(
      DstPtrInfo,
      MachineMemOperand::MOStore | MachineMemOperand::MODereferenceable,
      MemSize, DstAlign);

  const LLT PtrTy = MRI.getType(DstPtr);
  const LLT SizeTy = LLT::scalar(PtrTy.getSizeInBits());

  auto SizeConst = MIRBuilder.buildConstant(SizeTy, MemSize);
  MIRBuilder.buildMemCpy(DstPtr, SrcPtr, SizeConst, *DstMMO, *SrcMMO);
}

Register CallLowering::ValueHandler::extendRegister(Register ValReg,
                                                    const CCValAssign &VA,
                                                    unsigned MaxSizeBits) {
  LLT LocTy{VA.getLocVT()};
  LLT ValTy{VA.getValVT()};

  if (LocTy.getSizeInBits() == ValTy.getSizeInBits())
    return ValReg;

  if (LocTy.isScalar() && MaxSizeBits && MaxSizeBits < LocTy.getSizeInBits()) {
    if (MaxSizeBits <= ValTy.getSizeInBits())
      return ValReg;
    LocTy = LLT::scalar(MaxSizeBits);
  }

  const LLT ValRegTy = MRI.getType(ValReg);
  if (ValRegTy.isPointer()) {
    // The x32 ABI wants to zero extend 32-bit pointers to 64-bit registers, so
    // we have to cast to do the extension.
    LLT IntPtrTy = LLT::scalar(ValRegTy.getSizeInBits());
    ValReg = MIRBuilder.buildPtrToInt(IntPtrTy, ValReg).getReg(0);
  }

  switch (VA.getLocInfo()) {
  default:
    break;
  case CCValAssign::Full:
  case CCValAssign::BCvt:
    // FIXME: bitconverting between vector types may or may not be a
    // nop in big-endian situations.
    return ValReg;
  case CCValAssign::AExt: {
    auto MIB = MIRBuilder.buildAnyExt(LocTy, ValReg);
    return MIB.getReg(0);
  }
  case CCValAssign::SExt: {
    Register NewReg = MRI.createGenericVirtualRegister(LocTy);
    MIRBuilder.buildSExt(NewReg, ValReg);
    return NewReg;
  }
  case CCValAssign::ZExt: {
    Register NewReg = MRI.createGenericVirtualRegister(LocTy);
    MIRBuilder.buildZExt(NewReg, ValReg);
    return NewReg;
  }
  }
  llvm_unreachable("unable to extend register");
}

void CallLowering::ValueAssigner::anchor() {}

Register CallLowering::IncomingValueHandler::buildExtensionHint(
    const CCValAssign &VA, Register SrcReg, LLT NarrowTy) {
  switch (VA.getLocInfo()) {
  case CCValAssign::LocInfo::ZExt: {
    return MIRBuilder
        .buildAssertZExt(MRI.cloneVirtualRegister(SrcReg), SrcReg,
                         NarrowTy.getScalarSizeInBits())
        .getReg(0);
  }
  case CCValAssign::LocInfo::SExt: {
    return MIRBuilder
        .buildAssertSExt(MRI.cloneVirtualRegister(SrcReg), SrcReg,
                         NarrowTy.getScalarSizeInBits())
        .getReg(0);
    break;
  }
  default:
    return SrcReg;
  }
}

/// Check if we can use a basic COPY instruction between the two types.
///
/// We're currently building on top of the infrastructure using MVT, which loses
/// pointer information in the CCValAssign. We accept copies from physical
/// registers that have been reported as integers if it's to an equivalent sized
/// pointer LLT.
static bool isCopyCompatibleType(LLT SrcTy, LLT DstTy) {
  if (SrcTy == DstTy)
    return true;

  if (SrcTy.getSizeInBits() != DstTy.getSizeInBits())
    return false;

  SrcTy = SrcTy.getScalarType();
  DstTy = DstTy.getScalarType();

  return (SrcTy.isPointer() && DstTy.isScalar()) ||
         (DstTy.isPointer() && SrcTy.isScalar());
}

void CallLowering::IncomingValueHandler::assignValueToReg(
    Register ValVReg, Register PhysReg, const CCValAssign &VA) {
  const MVT LocVT = VA.getLocVT();
  const LLT LocTy(LocVT);
  const LLT RegTy = MRI.getType(ValVReg);

  if (isCopyCompatibleType(RegTy, LocTy)) {
    MIRBuilder.buildCopy(ValVReg, PhysReg);
    return;
  }

  auto Copy = MIRBuilder.buildCopy(LocTy, PhysReg);
  auto Hint = buildExtensionHint(VA, Copy.getReg(0), RegTy);
  MIRBuilder.buildTrunc(ValVReg, Hint);
}
