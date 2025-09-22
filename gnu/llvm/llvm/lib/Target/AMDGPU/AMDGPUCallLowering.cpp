//===-- llvm/lib/Target/AMDGPU/AMDGPUCallLowering.cpp - Call lowering -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the lowering of LLVM calls to machine code calls for
/// GlobalISel.
///
//===----------------------------------------------------------------------===//

#include "AMDGPUCallLowering.h"
#include "AMDGPU.h"
#include "AMDGPULegalizerInfo.h"
#include "AMDGPUTargetMachine.h"
#include "SIMachineFunctionInfo.h"
#include "SIRegisterInfo.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"

#define DEBUG_TYPE "amdgpu-call-lowering"

using namespace llvm;

namespace {

/// Wrapper around extendRegister to ensure we extend to a full 32-bit register.
static Register extendRegisterMin32(CallLowering::ValueHandler &Handler,
                                    Register ValVReg, const CCValAssign &VA) {
  if (VA.getLocVT().getSizeInBits() < 32) {
    // 16-bit types are reported as legal for 32-bit registers. We need to
    // extend and do a 32-bit copy to avoid the verifier complaining about it.
    return Handler.MIRBuilder.buildAnyExt(LLT::scalar(32), ValVReg).getReg(0);
  }

  return Handler.extendRegister(ValVReg, VA);
}

struct AMDGPUOutgoingValueHandler : public CallLowering::OutgoingValueHandler {
  AMDGPUOutgoingValueHandler(MachineIRBuilder &B, MachineRegisterInfo &MRI,
                             MachineInstrBuilder MIB)
      : OutgoingValueHandler(B, MRI), MIB(MIB) {}

  MachineInstrBuilder MIB;

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    llvm_unreachable("not implemented");
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    llvm_unreachable("not implemented");
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    Register ExtReg = extendRegisterMin32(*this, ValVReg, VA);

    // If this is a scalar return, insert a readfirstlane just in case the value
    // ends up in a VGPR.
    // FIXME: Assert this is a shader return.
    const SIRegisterInfo *TRI
      = static_cast<const SIRegisterInfo *>(MRI.getTargetRegisterInfo());
    if (TRI->isSGPRReg(MRI, PhysReg)) {
      LLT Ty = MRI.getType(ExtReg);
      LLT S32 = LLT::scalar(32);
      if (Ty != S32) {
        // FIXME: We should probably support readfirstlane intrinsics with all
        // legal 32-bit types.
        assert(Ty.getSizeInBits() == 32);
        if (Ty.isPointer())
          ExtReg = MIRBuilder.buildPtrToInt(S32, ExtReg).getReg(0);
        else
          ExtReg = MIRBuilder.buildBitcast(S32, ExtReg).getReg(0);
      }

      auto ToSGPR = MIRBuilder
                        .buildIntrinsic(Intrinsic::amdgcn_readfirstlane,
                                        {MRI.getType(ExtReg)})
                        .addReg(ExtReg);
      ExtReg = ToSGPR.getReg(0);
    }

    MIRBuilder.buildCopy(PhysReg, ExtReg);
    MIB.addUse(PhysReg, RegState::Implicit);
  }
};

struct AMDGPUIncomingArgHandler : public CallLowering::IncomingValueHandler {
  uint64_t StackUsed = 0;

  AMDGPUIncomingArgHandler(MachineIRBuilder &B, MachineRegisterInfo &MRI)
      : IncomingValueHandler(B, MRI) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    auto &MFI = MIRBuilder.getMF().getFrameInfo();

    // Byval is assumed to be writable memory, but other stack passed arguments
    // are not.
    const bool IsImmutable = !Flags.isByVal();
    int FI = MFI.CreateFixedObject(Size, Offset, IsImmutable);
    MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);
    auto AddrReg = MIRBuilder.buildFrameIndex(
        LLT::pointer(AMDGPUAS::PRIVATE_ADDRESS, 32), FI);
    StackUsed = std::max(StackUsed, Size + Offset);
    return AddrReg.getReg(0);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    markPhysRegUsed(PhysReg);

    if (VA.getLocVT().getSizeInBits() < 32) {
      // 16-bit types are reported as legal for 32-bit registers. We need to do
      // a 32-bit copy, and truncate to avoid the verifier complaining about it.
      auto Copy = MIRBuilder.buildCopy(LLT::scalar(32), PhysReg);

      // If we have signext/zeroext, it applies to the whole 32-bit register
      // before truncation.
      auto Extended =
          buildExtensionHint(VA, Copy.getReg(0), LLT(VA.getLocVT()));
      MIRBuilder.buildTrunc(ValVReg, Extended);
      return;
    }

    IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();

    auto MMO = MF.getMachineMemOperand(
        MPO, MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant, MemTy,
        inferAlignFromPtrInfo(MF, MPO));
    MIRBuilder.buildLoad(ValVReg, Addr, *MMO);
  }

  /// How the physical register gets marked varies between formal
  /// parameters (it's a basic-block live-in), and a call instruction
  /// (it's an implicit-def of the BL).
  virtual void markPhysRegUsed(unsigned PhysReg) = 0;
};

struct FormalArgHandler : public AMDGPUIncomingArgHandler {
  FormalArgHandler(MachineIRBuilder &B, MachineRegisterInfo &MRI)
      : AMDGPUIncomingArgHandler(B, MRI) {}

  void markPhysRegUsed(unsigned PhysReg) override {
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }
};

struct CallReturnHandler : public AMDGPUIncomingArgHandler {
  CallReturnHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                    MachineInstrBuilder MIB)
      : AMDGPUIncomingArgHandler(MIRBuilder, MRI), MIB(MIB) {}

  void markPhysRegUsed(unsigned PhysReg) override {
    MIB.addDef(PhysReg, RegState::Implicit);
  }

  MachineInstrBuilder MIB;
};

struct AMDGPUOutgoingArgHandler : public AMDGPUOutgoingValueHandler {
  /// For tail calls, the byte offset of the call's argument area from the
  /// callee's. Unused elsewhere.
  int FPDiff;

  // Cache the SP register vreg if we need it more than once in this call site.
  Register SPReg;

  bool IsTailCall;

  AMDGPUOutgoingArgHandler(MachineIRBuilder &MIRBuilder,
                           MachineRegisterInfo &MRI, MachineInstrBuilder MIB,
                           bool IsTailCall = false, int FPDiff = 0)
      : AMDGPUOutgoingValueHandler(MIRBuilder, MRI, MIB), FPDiff(FPDiff),
        IsTailCall(IsTailCall) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    MachineFunction &MF = MIRBuilder.getMF();
    const LLT PtrTy = LLT::pointer(AMDGPUAS::PRIVATE_ADDRESS, 32);
    const LLT S32 = LLT::scalar(32);

    if (IsTailCall) {
      Offset += FPDiff;
      int FI = MF.getFrameInfo().CreateFixedObject(Size, Offset, true);
      auto FIReg = MIRBuilder.buildFrameIndex(PtrTy, FI);
      MPO = MachinePointerInfo::getFixedStack(MF, FI);
      return FIReg.getReg(0);
    }

    const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

    if (!SPReg) {
      const GCNSubtarget &ST = MIRBuilder.getMF().getSubtarget<GCNSubtarget>();
      if (ST.enableFlatScratch()) {
        // The stack is accessed unswizzled, so we can use a regular copy.
        SPReg = MIRBuilder.buildCopy(PtrTy,
                                     MFI->getStackPtrOffsetReg()).getReg(0);
      } else {
        // The address we produce here, without knowing the use context, is going
        // to be interpreted as a vector address, so we need to convert to a
        // swizzled address.
        SPReg = MIRBuilder.buildInstr(AMDGPU::G_AMDGPU_WAVE_ADDRESS, {PtrTy},
                                      {MFI->getStackPtrOffsetReg()}).getReg(0);
      }
    }

    auto OffsetReg = MIRBuilder.buildConstant(S32, Offset);

    auto AddrReg = MIRBuilder.buildPtrAdd(PtrTy, SPReg, OffsetReg);
    MPO = MachinePointerInfo::getStack(MF, Offset);
    return AddrReg.getReg(0);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    MIB.addUse(PhysReg, RegState::Implicit);
    Register ExtReg = extendRegisterMin32(*this, ValVReg, VA);
    MIRBuilder.buildCopy(PhysReg, ExtReg);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();
    uint64_t LocMemOffset = VA.getLocMemOffset();
    const auto &ST = MF.getSubtarget<GCNSubtarget>();

    auto MMO = MF.getMachineMemOperand(
        MPO, MachineMemOperand::MOStore, MemTy,
        commonAlignment(ST.getStackAlignment(), LocMemOffset));
    MIRBuilder.buildStore(ValVReg, Addr, *MMO);
  }

  void assignValueToAddress(const CallLowering::ArgInfo &Arg,
                            unsigned ValRegIndex, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    Register ValVReg = VA.getLocInfo() != CCValAssign::LocInfo::FPExt
                           ? extendRegister(Arg.Regs[ValRegIndex], VA)
                           : Arg.Regs[ValRegIndex];
    assignValueToAddress(ValVReg, Addr, MemTy, MPO, VA);
  }
};
} // anonymous namespace

AMDGPUCallLowering::AMDGPUCallLowering(const AMDGPUTargetLowering &TLI)
  : CallLowering(&TLI) {
}

// FIXME: Compatibility shim
static ISD::NodeType extOpcodeToISDExtOpcode(unsigned MIOpc) {
  switch (MIOpc) {
  case TargetOpcode::G_SEXT:
    return ISD::SIGN_EXTEND;
  case TargetOpcode::G_ZEXT:
    return ISD::ZERO_EXTEND;
  case TargetOpcode::G_ANYEXT:
    return ISD::ANY_EXTEND;
  default:
    llvm_unreachable("not an extend opcode");
  }
}

bool AMDGPUCallLowering::canLowerReturn(MachineFunction &MF,
                                        CallingConv::ID CallConv,
                                        SmallVectorImpl<BaseArgInfo> &Outs,
                                        bool IsVarArg) const {
  // For shaders. Vector types should be explicitly handled by CC.
  if (AMDGPU::isEntryFunctionCC(CallConv))
    return true;

  SmallVector<CCValAssign, 16> ArgLocs;
  const SITargetLowering &TLI = *getTLI<SITargetLowering>();
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs,
                 MF.getFunction().getContext());

  return checkReturn(CCInfo, Outs, TLI.CCAssignFnForReturn(CallConv, IsVarArg));
}

/// Lower the return value for the already existing \p Ret. This assumes that
/// \p B's insertion point is correct.
bool AMDGPUCallLowering::lowerReturnVal(MachineIRBuilder &B,
                                        const Value *Val, ArrayRef<Register> VRegs,
                                        MachineInstrBuilder &Ret) const {
  if (!Val)
    return true;

  auto &MF = B.getMF();
  const auto &F = MF.getFunction();
  const DataLayout &DL = MF.getDataLayout();
  MachineRegisterInfo *MRI = B.getMRI();
  LLVMContext &Ctx = F.getContext();

  CallingConv::ID CC = F.getCallingConv();
  const SITargetLowering &TLI = *getTLI<SITargetLowering>();

  SmallVector<EVT, 8> SplitEVTs;
  ComputeValueVTs(TLI, DL, Val->getType(), SplitEVTs);
  assert(VRegs.size() == SplitEVTs.size() &&
         "For each split Type there should be exactly one VReg.");

  SmallVector<ArgInfo, 8> SplitRetInfos;

  for (unsigned i = 0; i < SplitEVTs.size(); ++i) {
    EVT VT = SplitEVTs[i];
    Register Reg = VRegs[i];
    ArgInfo RetInfo(Reg, VT.getTypeForEVT(Ctx), 0);
    setArgFlags(RetInfo, AttributeList::ReturnIndex, DL, F);

    if (VT.isScalarInteger()) {
      unsigned ExtendOp = TargetOpcode::G_ANYEXT;
      if (RetInfo.Flags[0].isSExt()) {
        assert(RetInfo.Regs.size() == 1 && "expect only simple return values");
        ExtendOp = TargetOpcode::G_SEXT;
      } else if (RetInfo.Flags[0].isZExt()) {
        assert(RetInfo.Regs.size() == 1 && "expect only simple return values");
        ExtendOp = TargetOpcode::G_ZEXT;
      }

      EVT ExtVT = TLI.getTypeForExtReturn(Ctx, VT,
                                          extOpcodeToISDExtOpcode(ExtendOp));
      if (ExtVT != VT) {
        RetInfo.Ty = ExtVT.getTypeForEVT(Ctx);
        LLT ExtTy = getLLTForType(*RetInfo.Ty, DL);
        Reg = B.buildInstr(ExtendOp, {ExtTy}, {Reg}).getReg(0);
      }
    }

    if (Reg != RetInfo.Regs[0]) {
      RetInfo.Regs[0] = Reg;
      // Reset the arg flags after modifying Reg.
      setArgFlags(RetInfo, AttributeList::ReturnIndex, DL, F);
    }

    splitToValueTypes(RetInfo, SplitRetInfos, DL, CC);
  }

  CCAssignFn *AssignFn = TLI.CCAssignFnForReturn(CC, F.isVarArg());

  OutgoingValueAssigner Assigner(AssignFn);
  AMDGPUOutgoingValueHandler RetHandler(B, *MRI, Ret);
  return determineAndHandleAssignments(RetHandler, Assigner, SplitRetInfos, B,
                                       CC, F.isVarArg());
}

bool AMDGPUCallLowering::lowerReturn(MachineIRBuilder &B, const Value *Val,
                                     ArrayRef<Register> VRegs,
                                     FunctionLoweringInfo &FLI) const {

  MachineFunction &MF = B.getMF();
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  MFI->setIfReturnsVoid(!Val);

  assert(!Val == VRegs.empty() && "Return value without a vreg");

  CallingConv::ID CC = B.getMF().getFunction().getCallingConv();
  const bool IsShader = AMDGPU::isShader(CC);
  const bool IsWaveEnd =
      (IsShader && MFI->returnsVoid()) || AMDGPU::isKernel(CC);
  if (IsWaveEnd) {
    B.buildInstr(AMDGPU::S_ENDPGM)
      .addImm(0);
    return true;
  }

  unsigned ReturnOpc =
      IsShader ? AMDGPU::SI_RETURN_TO_EPILOG : AMDGPU::SI_RETURN;
  auto Ret = B.buildInstrNoInsert(ReturnOpc);

  if (!FLI.CanLowerReturn)
    insertSRetStores(B, Val->getType(), VRegs, FLI.DemoteRegister);
  else if (!lowerReturnVal(B, Val, VRegs, Ret))
    return false;

  // TODO: Handle CalleeSavedRegsViaCopy.

  B.insertInstr(Ret);
  return true;
}

void AMDGPUCallLowering::lowerParameterPtr(Register DstReg, MachineIRBuilder &B,
                                           uint64_t Offset) const {
  MachineFunction &MF = B.getMF();
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  Register KernArgSegmentPtr =
    MFI->getPreloadedReg(AMDGPUFunctionArgInfo::KERNARG_SEGMENT_PTR);
  Register KernArgSegmentVReg = MRI.getLiveInVirtReg(KernArgSegmentPtr);

  auto OffsetReg = B.buildConstant(LLT::scalar(64), Offset);

  B.buildPtrAdd(DstReg, KernArgSegmentVReg, OffsetReg);
}

void AMDGPUCallLowering::lowerParameter(MachineIRBuilder &B, ArgInfo &OrigArg,
                                        uint64_t Offset,
                                        Align Alignment) const {
  MachineFunction &MF = B.getMF();
  const Function &F = MF.getFunction();
  const DataLayout &DL = F.getDataLayout();
  MachinePointerInfo PtrInfo(AMDGPUAS::CONSTANT_ADDRESS);

  LLT PtrTy = LLT::pointer(AMDGPUAS::CONSTANT_ADDRESS, 64);

  SmallVector<ArgInfo, 32> SplitArgs;
  SmallVector<uint64_t> FieldOffsets;
  splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv(), &FieldOffsets);

  unsigned Idx = 0;
  for (ArgInfo &SplitArg : SplitArgs) {
    Register PtrReg = B.getMRI()->createGenericVirtualRegister(PtrTy);
    lowerParameterPtr(PtrReg, B, Offset + FieldOffsets[Idx]);

    LLT ArgTy = getLLTForType(*SplitArg.Ty, DL);
    if (SplitArg.Flags[0].isPointer()) {
      // Compensate for losing pointeriness in splitValueTypes.
      LLT PtrTy = LLT::pointer(SplitArg.Flags[0].getPointerAddrSpace(),
                               ArgTy.getScalarSizeInBits());
      ArgTy = ArgTy.isVector() ? LLT::vector(ArgTy.getElementCount(), PtrTy)
                               : PtrTy;
    }

    MachineMemOperand *MMO = MF.getMachineMemOperand(
        PtrInfo,
        MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable |
            MachineMemOperand::MOInvariant,
        ArgTy, commonAlignment(Alignment, FieldOffsets[Idx]));

    assert(SplitArg.Regs.size() == 1);

    B.buildLoad(SplitArg.Regs[0], PtrReg, *MMO);
    ++Idx;
  }
}

// Allocate special inputs passed in user SGPRs.
static void allocateHSAUserSGPRs(CCState &CCInfo,
                                 MachineIRBuilder &B,
                                 MachineFunction &MF,
                                 const SIRegisterInfo &TRI,
                                 SIMachineFunctionInfo &Info) {
  // FIXME: How should these inputs interact with inreg / custom SGPR inputs?
  const GCNUserSGPRUsageInfo &UserSGPRInfo = Info.getUserSGPRInfo();
  if (UserSGPRInfo.hasPrivateSegmentBuffer()) {
    Register PrivateSegmentBufferReg = Info.addPrivateSegmentBuffer(TRI);
    MF.addLiveIn(PrivateSegmentBufferReg, &AMDGPU::SGPR_128RegClass);
    CCInfo.AllocateReg(PrivateSegmentBufferReg);
  }

  if (UserSGPRInfo.hasDispatchPtr()) {
    Register DispatchPtrReg = Info.addDispatchPtr(TRI);
    MF.addLiveIn(DispatchPtrReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(DispatchPtrReg);
  }

  const Module *M = MF.getFunction().getParent();
  if (UserSGPRInfo.hasQueuePtr() &&
      AMDGPU::getAMDHSACodeObjectVersion(*M) < AMDGPU::AMDHSA_COV5) {
    Register QueuePtrReg = Info.addQueuePtr(TRI);
    MF.addLiveIn(QueuePtrReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(QueuePtrReg);
  }

  if (UserSGPRInfo.hasKernargSegmentPtr()) {
    MachineRegisterInfo &MRI = MF.getRegInfo();
    Register InputPtrReg = Info.addKernargSegmentPtr(TRI);
    const LLT P4 = LLT::pointer(AMDGPUAS::CONSTANT_ADDRESS, 64);
    Register VReg = MRI.createGenericVirtualRegister(P4);
    MRI.addLiveIn(InputPtrReg, VReg);
    B.getMBB().addLiveIn(InputPtrReg);
    B.buildCopy(VReg, InputPtrReg);
    CCInfo.AllocateReg(InputPtrReg);
  }

  if (UserSGPRInfo.hasDispatchID()) {
    Register DispatchIDReg = Info.addDispatchID(TRI);
    MF.addLiveIn(DispatchIDReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(DispatchIDReg);
  }

  if (UserSGPRInfo.hasFlatScratchInit()) {
    Register FlatScratchInitReg = Info.addFlatScratchInit(TRI);
    MF.addLiveIn(FlatScratchInitReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(FlatScratchInitReg);
  }

  // TODO: Add GridWorkGroupCount user SGPRs when used. For now with HSA we read
  // these from the dispatch pointer.
}

bool AMDGPUCallLowering::lowerFormalArgumentsKernel(
    MachineIRBuilder &B, const Function &F,
    ArrayRef<ArrayRef<Register>> VRegs) const {
  MachineFunction &MF = B.getMF();
  const GCNSubtarget *Subtarget = &MF.getSubtarget<GCNSubtarget>();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  const SIRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const SITargetLowering &TLI = *getTLI<SITargetLowering>();
  const DataLayout &DL = F.getDataLayout();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs, F.getContext());

  allocateHSAUserSGPRs(CCInfo, B, MF, *TRI, *Info);

  unsigned i = 0;
  const Align KernArgBaseAlign(16);
  const unsigned BaseOffset = Subtarget->getExplicitKernelArgOffset();
  uint64_t ExplicitArgOffset = 0;

  // TODO: Align down to dword alignment and extract bits for extending loads.
  for (auto &Arg : F.args()) {
    const bool IsByRef = Arg.hasByRefAttr();
    Type *ArgTy = IsByRef ? Arg.getParamByRefType() : Arg.getType();
    unsigned AllocSize = DL.getTypeAllocSize(ArgTy);
    if (AllocSize == 0)
      continue;

    MaybeAlign ParamAlign = IsByRef ? Arg.getParamAlign() : std::nullopt;
    Align ABIAlign = DL.getValueOrABITypeAlignment(ParamAlign, ArgTy);

    uint64_t ArgOffset = alignTo(ExplicitArgOffset, ABIAlign) + BaseOffset;
    ExplicitArgOffset = alignTo(ExplicitArgOffset, ABIAlign) + AllocSize;

    if (Arg.use_empty()) {
      ++i;
      continue;
    }

    Align Alignment = commonAlignment(KernArgBaseAlign, ArgOffset);

    if (IsByRef) {
      unsigned ByRefAS = cast<PointerType>(Arg.getType())->getAddressSpace();

      assert(VRegs[i].size() == 1 &&
             "expected only one register for byval pointers");
      if (ByRefAS == AMDGPUAS::CONSTANT_ADDRESS) {
        lowerParameterPtr(VRegs[i][0], B, ArgOffset);
      } else {
        const LLT ConstPtrTy = LLT::pointer(AMDGPUAS::CONSTANT_ADDRESS, 64);
        Register PtrReg = MRI.createGenericVirtualRegister(ConstPtrTy);
        lowerParameterPtr(PtrReg, B, ArgOffset);

        B.buildAddrSpaceCast(VRegs[i][0], PtrReg);
      }
    } else {
      ArgInfo OrigArg(VRegs[i], Arg, i);
      const unsigned OrigArgIdx = i + AttributeList::FirstArgIndex;
      setArgFlags(OrigArg, OrigArgIdx, DL, F);
      lowerParameter(B, OrigArg, ArgOffset, Alignment);
    }

    ++i;
  }

  TLI.allocateSpecialEntryInputVGPRs(CCInfo, MF, *TRI, *Info);
  TLI.allocateSystemSGPRs(CCInfo, MF, *Info, F.getCallingConv(), false);
  return true;
}

bool AMDGPUCallLowering::lowerFormalArguments(
    MachineIRBuilder &B, const Function &F, ArrayRef<ArrayRef<Register>> VRegs,
    FunctionLoweringInfo &FLI) const {
  CallingConv::ID CC = F.getCallingConv();

  // The infrastructure for normal calling convention lowering is essentially
  // useless for kernels. We want to avoid any kind of legalization or argument
  // splitting.
  if (CC == CallingConv::AMDGPU_KERNEL)
    return lowerFormalArgumentsKernel(B, F, VRegs);

  const bool IsGraphics = AMDGPU::isGraphics(CC);
  const bool IsEntryFunc = AMDGPU::isEntryFunctionCC(CC);

  MachineFunction &MF = B.getMF();
  MachineBasicBlock &MBB = B.getMBB();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &Subtarget = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const DataLayout &DL = F.getDataLayout();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CC, F.isVarArg(), MF, ArgLocs, F.getContext());
  const GCNUserSGPRUsageInfo &UserSGPRInfo = Info->getUserSGPRInfo();

  if (UserSGPRInfo.hasImplicitBufferPtr()) {
    Register ImplicitBufferPtrReg = Info->addImplicitBufferPtr(*TRI);
    MF.addLiveIn(ImplicitBufferPtrReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(ImplicitBufferPtrReg);
  }

  // FIXME: This probably isn't defined for mesa
  if (UserSGPRInfo.hasFlatScratchInit() && !Subtarget.isAmdPalOS()) {
    Register FlatScratchInitReg = Info->addFlatScratchInit(*TRI);
    MF.addLiveIn(FlatScratchInitReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(FlatScratchInitReg);
  }

  SmallVector<ArgInfo, 32> SplitArgs;
  unsigned Idx = 0;
  unsigned PSInputNum = 0;

  // Insert the hidden sret parameter if the return value won't fit in the
  // return registers.
  if (!FLI.CanLowerReturn)
    insertSRetIncomingArgument(F, SplitArgs, FLI.DemoteRegister, MRI, DL);

  for (auto &Arg : F.args()) {
    if (DL.getTypeStoreSize(Arg.getType()) == 0)
      continue;

    const bool InReg = Arg.hasAttribute(Attribute::InReg);

    if (Arg.hasAttribute(Attribute::SwiftSelf) ||
        Arg.hasAttribute(Attribute::SwiftError) ||
        Arg.hasAttribute(Attribute::Nest))
      return false;

    if (CC == CallingConv::AMDGPU_PS && !InReg && PSInputNum <= 15) {
      const bool ArgUsed = !Arg.use_empty();
      bool SkipArg = !ArgUsed && !Info->isPSInputAllocated(PSInputNum);

      if (!SkipArg) {
        Info->markPSInputAllocated(PSInputNum);
        if (ArgUsed)
          Info->markPSInputEnabled(PSInputNum);
      }

      ++PSInputNum;

      if (SkipArg) {
        for (Register R : VRegs[Idx])
          B.buildUndef(R);

        ++Idx;
        continue;
      }
    }

    ArgInfo OrigArg(VRegs[Idx], Arg, Idx);
    const unsigned OrigArgIdx = Idx + AttributeList::FirstArgIndex;
    setArgFlags(OrigArg, OrigArgIdx, DL, F);

    splitToValueTypes(OrigArg, SplitArgs, DL, CC);
    ++Idx;
  }

  // At least one interpolation mode must be enabled or else the GPU will
  // hang.
  //
  // Check PSInputAddr instead of PSInputEnable. The idea is that if the user
  // set PSInputAddr, the user wants to enable some bits after the compilation
  // based on run-time states. Since we can't know what the final PSInputEna
  // will look like, so we shouldn't do anything here and the user should take
  // responsibility for the correct programming.
  //
  // Otherwise, the following restrictions apply:
  // - At least one of PERSP_* (0xF) or LINEAR_* (0x70) must be enabled.
  // - If POS_W_FLOAT (11) is enabled, at least one of PERSP_* must be
  //   enabled too.
  if (CC == CallingConv::AMDGPU_PS) {
    if ((Info->getPSInputAddr() & 0x7F) == 0 ||
        ((Info->getPSInputAddr() & 0xF) == 0 &&
         Info->isPSInputAllocated(11))) {
      CCInfo.AllocateReg(AMDGPU::VGPR0);
      CCInfo.AllocateReg(AMDGPU::VGPR1);
      Info->markPSInputAllocated(0);
      Info->markPSInputEnabled(0);
    }

    if (Subtarget.isAmdPalOS()) {
      // For isAmdPalOS, the user does not enable some bits after compilation
      // based on run-time states; the register values being generated here are
      // the final ones set in hardware. Therefore we need to apply the
      // workaround to PSInputAddr and PSInputEnable together.  (The case where
      // a bit is set in PSInputAddr but not PSInputEnable is where the frontend
      // set up an input arg for a particular interpolation mode, but nothing
      // uses that input arg. Really we should have an earlier pass that removes
      // such an arg.)
      unsigned PsInputBits = Info->getPSInputAddr() & Info->getPSInputEnable();
      if ((PsInputBits & 0x7F) == 0 ||
          ((PsInputBits & 0xF) == 0 &&
           (PsInputBits >> 11 & 1)))
        Info->markPSInputEnabled(llvm::countr_zero(Info->getPSInputAddr()));
    }
  }

  const SITargetLowering &TLI = *getTLI<SITargetLowering>();
  CCAssignFn *AssignFn = TLI.CCAssignFnForCall(CC, F.isVarArg());

  if (!MBB.empty())
    B.setInstr(*MBB.begin());

  if (!IsEntryFunc && !IsGraphics) {
    // For the fixed ABI, pass workitem IDs in the last argument register.
    TLI.allocateSpecialInputVGPRsFixed(CCInfo, MF, *TRI, *Info);

    if (!Subtarget.enableFlatScratch())
      CCInfo.AllocateReg(Info->getScratchRSrcReg());
    TLI.allocateSpecialInputSGPRs(CCInfo, MF, *TRI, *Info);
  }

  IncomingValueAssigner Assigner(AssignFn);
  if (!determineAssignments(Assigner, SplitArgs, CCInfo))
    return false;

  FormalArgHandler Handler(B, MRI);
  if (!handleAssignments(Handler, SplitArgs, CCInfo, ArgLocs, B))
    return false;

  uint64_t StackSize = Assigner.StackSize;

  // Start adding system SGPRs.
  if (IsEntryFunc)
    TLI.allocateSystemSGPRs(CCInfo, MF, *Info, CC, IsGraphics);

  // When we tail call, we need to check if the callee's arguments will fit on
  // the caller's stack. So, whenever we lower formal arguments, we should keep
  // track of this information, since we might lower a tail call in this
  // function later.
  Info->setBytesInStackArgArea(StackSize);

  // Move back to the end of the basic block.
  B.setMBB(MBB);

  return true;
}

bool AMDGPUCallLowering::passSpecialInputs(MachineIRBuilder &MIRBuilder,
                                           CCState &CCInfo,
                                           SmallVectorImpl<std::pair<MCRegister, Register>> &ArgRegs,
                                           CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();

  // If there's no call site, this doesn't correspond to a call from the IR and
  // doesn't need implicit inputs.
  if (!Info.CB)
    return true;

  const AMDGPUFunctionArgInfo *CalleeArgInfo
    = &AMDGPUArgumentUsageInfo::FixedABIFunctionInfo;

  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const AMDGPUFunctionArgInfo &CallerArgInfo = MFI->getArgInfo();


  // TODO: Unify with private memory register handling. This is complicated by
  // the fact that at least in kernels, the input argument is not necessarily
  // in the same location as the input.
  AMDGPUFunctionArgInfo::PreloadedValue InputRegs[] = {
    AMDGPUFunctionArgInfo::DISPATCH_PTR,
    AMDGPUFunctionArgInfo::QUEUE_PTR,
    AMDGPUFunctionArgInfo::IMPLICIT_ARG_PTR,
    AMDGPUFunctionArgInfo::DISPATCH_ID,
    AMDGPUFunctionArgInfo::WORKGROUP_ID_X,
    AMDGPUFunctionArgInfo::WORKGROUP_ID_Y,
    AMDGPUFunctionArgInfo::WORKGROUP_ID_Z,
    AMDGPUFunctionArgInfo::LDS_KERNEL_ID,
  };

  static constexpr StringLiteral ImplicitAttrNames[] = {
    "amdgpu-no-dispatch-ptr",
    "amdgpu-no-queue-ptr",
    "amdgpu-no-implicitarg-ptr",
    "amdgpu-no-dispatch-id",
    "amdgpu-no-workgroup-id-x",
    "amdgpu-no-workgroup-id-y",
    "amdgpu-no-workgroup-id-z",
    "amdgpu-no-lds-kernel-id",
  };

  MachineRegisterInfo &MRI = MF.getRegInfo();

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const AMDGPULegalizerInfo *LI
    = static_cast<const AMDGPULegalizerInfo*>(ST.getLegalizerInfo());

  unsigned I = 0;
  for (auto InputID : InputRegs) {
    const ArgDescriptor *OutgoingArg;
    const TargetRegisterClass *ArgRC;
    LLT ArgTy;

    // If the callee does not use the attribute value, skip copying the value.
    if (Info.CB->hasFnAttr(ImplicitAttrNames[I++]))
      continue;

    std::tie(OutgoingArg, ArgRC, ArgTy) =
        CalleeArgInfo->getPreloadedValue(InputID);
    if (!OutgoingArg)
      continue;

    const ArgDescriptor *IncomingArg;
    const TargetRegisterClass *IncomingArgRC;
    std::tie(IncomingArg, IncomingArgRC, ArgTy) =
        CallerArgInfo.getPreloadedValue(InputID);
    assert(IncomingArgRC == ArgRC);

    Register InputReg = MRI.createGenericVirtualRegister(ArgTy);

    if (IncomingArg) {
      LI->loadInputValue(InputReg, MIRBuilder, IncomingArg, ArgRC, ArgTy);
    } else if (InputID == AMDGPUFunctionArgInfo::IMPLICIT_ARG_PTR) {
      LI->getImplicitArgPtr(InputReg, MRI, MIRBuilder);
    } else if (InputID == AMDGPUFunctionArgInfo::LDS_KERNEL_ID) {
      std::optional<uint32_t> Id =
          AMDGPUMachineFunction::getLDSKernelIdMetadata(MF.getFunction());
      if (Id) {
        MIRBuilder.buildConstant(InputReg, *Id);
      } else {
        MIRBuilder.buildUndef(InputReg);
      }
    } else {
      // We may have proven the input wasn't needed, although the ABI is
      // requiring it. We just need to allocate the register appropriately.
      MIRBuilder.buildUndef(InputReg);
    }

    if (OutgoingArg->isRegister()) {
      ArgRegs.emplace_back(OutgoingArg->getRegister(), InputReg);
      if (!CCInfo.AllocateReg(OutgoingArg->getRegister()))
        report_fatal_error("failed to allocate implicit input argument");
    } else {
      LLVM_DEBUG(dbgs() << "Unhandled stack passed implicit input argument\n");
      return false;
    }
  }

  // Pack workitem IDs into a single register or pass it as is if already
  // packed.
  const ArgDescriptor *OutgoingArg;
  const TargetRegisterClass *ArgRC;
  LLT ArgTy;

  std::tie(OutgoingArg, ArgRC, ArgTy) =
      CalleeArgInfo->getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_X);
  if (!OutgoingArg)
    std::tie(OutgoingArg, ArgRC, ArgTy) =
        CalleeArgInfo->getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_Y);
  if (!OutgoingArg)
    std::tie(OutgoingArg, ArgRC, ArgTy) =
        CalleeArgInfo->getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_Z);
  if (!OutgoingArg)
    return false;

  auto WorkitemIDX =
      CallerArgInfo.getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_X);
  auto WorkitemIDY =
      CallerArgInfo.getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_Y);
  auto WorkitemIDZ =
      CallerArgInfo.getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_Z);

  const ArgDescriptor *IncomingArgX = std::get<0>(WorkitemIDX);
  const ArgDescriptor *IncomingArgY = std::get<0>(WorkitemIDY);
  const ArgDescriptor *IncomingArgZ = std::get<0>(WorkitemIDZ);
  const LLT S32 = LLT::scalar(32);

  const bool NeedWorkItemIDX = !Info.CB->hasFnAttr("amdgpu-no-workitem-id-x");
  const bool NeedWorkItemIDY = !Info.CB->hasFnAttr("amdgpu-no-workitem-id-y");
  const bool NeedWorkItemIDZ = !Info.CB->hasFnAttr("amdgpu-no-workitem-id-z");

  // If incoming ids are not packed we need to pack them.
  // FIXME: Should consider known workgroup size to eliminate known 0 cases.
  Register InputReg;
  if (IncomingArgX && !IncomingArgX->isMasked() && CalleeArgInfo->WorkItemIDX &&
      NeedWorkItemIDX) {
    if (ST.getMaxWorkitemID(MF.getFunction(), 0) != 0) {
      InputReg = MRI.createGenericVirtualRegister(S32);
      LI->loadInputValue(InputReg, MIRBuilder, IncomingArgX,
                         std::get<1>(WorkitemIDX), std::get<2>(WorkitemIDX));
    } else {
      InputReg = MIRBuilder.buildConstant(S32, 0).getReg(0);
    }
  }

  if (IncomingArgY && !IncomingArgY->isMasked() && CalleeArgInfo->WorkItemIDY &&
      NeedWorkItemIDY && ST.getMaxWorkitemID(MF.getFunction(), 1) != 0) {
    Register Y = MRI.createGenericVirtualRegister(S32);
    LI->loadInputValue(Y, MIRBuilder, IncomingArgY, std::get<1>(WorkitemIDY),
                       std::get<2>(WorkitemIDY));

    Y = MIRBuilder.buildShl(S32, Y, MIRBuilder.buildConstant(S32, 10)).getReg(0);
    InputReg = InputReg ? MIRBuilder.buildOr(S32, InputReg, Y).getReg(0) : Y;
  }

  if (IncomingArgZ && !IncomingArgZ->isMasked() && CalleeArgInfo->WorkItemIDZ &&
      NeedWorkItemIDZ && ST.getMaxWorkitemID(MF.getFunction(), 2) != 0) {
    Register Z = MRI.createGenericVirtualRegister(S32);
    LI->loadInputValue(Z, MIRBuilder, IncomingArgZ, std::get<1>(WorkitemIDZ),
                       std::get<2>(WorkitemIDZ));

    Z = MIRBuilder.buildShl(S32, Z, MIRBuilder.buildConstant(S32, 20)).getReg(0);
    InputReg = InputReg ? MIRBuilder.buildOr(S32, InputReg, Z).getReg(0) : Z;
  }

  if (!InputReg &&
      (NeedWorkItemIDX || NeedWorkItemIDY || NeedWorkItemIDZ)) {
    InputReg = MRI.createGenericVirtualRegister(S32);
    if (!IncomingArgX && !IncomingArgY && !IncomingArgZ) {
      // We're in a situation where the outgoing function requires the workitem
      // ID, but the calling function does not have it (e.g a graphics function
      // calling a C calling convention function). This is illegal, but we need
      // to produce something.
      MIRBuilder.buildUndef(InputReg);
    } else {
      // Workitem ids are already packed, any of present incoming arguments will
      // carry all required fields.
      ArgDescriptor IncomingArg = ArgDescriptor::createArg(
        IncomingArgX ? *IncomingArgX :
        IncomingArgY ? *IncomingArgY : *IncomingArgZ, ~0u);
      LI->loadInputValue(InputReg, MIRBuilder, &IncomingArg,
                         &AMDGPU::VGPR_32RegClass, S32);
    }
  }

  if (OutgoingArg->isRegister()) {
    if (InputReg)
      ArgRegs.emplace_back(OutgoingArg->getRegister(), InputReg);

    if (!CCInfo.AllocateReg(OutgoingArg->getRegister()))
      report_fatal_error("failed to allocate implicit input argument");
  } else {
    LLVM_DEBUG(dbgs() << "Unhandled stack passed implicit input argument\n");
    return false;
  }

  return true;
}

/// Returns a pair containing the fixed CCAssignFn and the vararg CCAssignFn for
/// CC.
static std::pair<CCAssignFn *, CCAssignFn *>
getAssignFnsForCC(CallingConv::ID CC, const SITargetLowering &TLI) {
  return {TLI.CCAssignFnForCall(CC, false), TLI.CCAssignFnForCall(CC, true)};
}

static unsigned getCallOpcode(const MachineFunction &CallerF, bool IsIndirect,
                              bool IsTailCall, bool isWave32,
                              CallingConv::ID CC) {
  // For calls to amdgpu_cs_chain functions, the address is known to be uniform.
  assert((AMDGPU::isChainCC(CC) || !IsIndirect || !IsTailCall) &&
         "Indirect calls can't be tail calls, "
         "because the address can be divergent");
  if (!IsTailCall)
    return AMDGPU::G_SI_CALL;

  if (AMDGPU::isChainCC(CC))
    return isWave32 ? AMDGPU::SI_CS_CHAIN_TC_W32 : AMDGPU::SI_CS_CHAIN_TC_W64;

  return CC == CallingConv::AMDGPU_Gfx ? AMDGPU::SI_TCRETURN_GFX :
                                         AMDGPU::SI_TCRETURN;
}

// Add operands to call instruction to track the callee.
static bool addCallTargetOperands(MachineInstrBuilder &CallInst,
                                  MachineIRBuilder &MIRBuilder,
                                  AMDGPUCallLowering::CallLoweringInfo &Info) {
  if (Info.Callee.isReg()) {
    CallInst.addReg(Info.Callee.getReg());
    CallInst.addImm(0);
  } else if (Info.Callee.isGlobal() && Info.Callee.getOffset() == 0) {
    // The call lowering lightly assumed we can directly encode a call target in
    // the instruction, which is not the case. Materialize the address here.
    const GlobalValue *GV = Info.Callee.getGlobal();
    auto Ptr = MIRBuilder.buildGlobalValue(
      LLT::pointer(GV->getAddressSpace(), 64), GV);
    CallInst.addReg(Ptr.getReg(0));
    CallInst.add(Info.Callee);
  } else
    return false;

  return true;
}

bool AMDGPUCallLowering::doCallerAndCalleePassArgsTheSameWay(
    CallLoweringInfo &Info, MachineFunction &MF,
    SmallVectorImpl<ArgInfo> &InArgs) const {
  const Function &CallerF = MF.getFunction();
  CallingConv::ID CalleeCC = Info.CallConv;
  CallingConv::ID CallerCC = CallerF.getCallingConv();

  // If the calling conventions match, then everything must be the same.
  if (CalleeCC == CallerCC)
    return true;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();

  // Make sure that the caller and callee preserve all of the same registers.
  auto TRI = ST.getRegisterInfo();

  const uint32_t *CallerPreserved = TRI->getCallPreservedMask(MF, CallerCC);
  const uint32_t *CalleePreserved = TRI->getCallPreservedMask(MF, CalleeCC);
  if (!TRI->regmaskSubsetEqual(CallerPreserved, CalleePreserved))
    return false;

  // Check if the caller and callee will handle arguments in the same way.
  const SITargetLowering &TLI = *getTLI<SITargetLowering>();
  CCAssignFn *CalleeAssignFnFixed;
  CCAssignFn *CalleeAssignFnVarArg;
  std::tie(CalleeAssignFnFixed, CalleeAssignFnVarArg) =
      getAssignFnsForCC(CalleeCC, TLI);

  CCAssignFn *CallerAssignFnFixed;
  CCAssignFn *CallerAssignFnVarArg;
  std::tie(CallerAssignFnFixed, CallerAssignFnVarArg) =
      getAssignFnsForCC(CallerCC, TLI);

  // FIXME: We are not accounting for potential differences in implicitly passed
  // inputs, but only the fixed ABI is supported now anyway.
  IncomingValueAssigner CalleeAssigner(CalleeAssignFnFixed,
                                       CalleeAssignFnVarArg);
  IncomingValueAssigner CallerAssigner(CallerAssignFnFixed,
                                       CallerAssignFnVarArg);
  return resultsCompatible(Info, MF, InArgs, CalleeAssigner, CallerAssigner);
}

bool AMDGPUCallLowering::areCalleeOutgoingArgsTailCallable(
    CallLoweringInfo &Info, MachineFunction &MF,
    SmallVectorImpl<ArgInfo> &OutArgs) const {
  // If there are no outgoing arguments, then we are done.
  if (OutArgs.empty())
    return true;

  const Function &CallerF = MF.getFunction();
  CallingConv::ID CalleeCC = Info.CallConv;
  CallingConv::ID CallerCC = CallerF.getCallingConv();
  const SITargetLowering &TLI = *getTLI<SITargetLowering>();

  CCAssignFn *AssignFnFixed;
  CCAssignFn *AssignFnVarArg;
  std::tie(AssignFnFixed, AssignFnVarArg) = getAssignFnsForCC(CalleeCC, TLI);

  // We have outgoing arguments. Make sure that we can tail call with them.
  SmallVector<CCValAssign, 16> OutLocs;
  CCState OutInfo(CalleeCC, false, MF, OutLocs, CallerF.getContext());
  OutgoingValueAssigner Assigner(AssignFnFixed, AssignFnVarArg);

  if (!determineAssignments(Assigner, OutArgs, OutInfo)) {
    LLVM_DEBUG(dbgs() << "... Could not analyze call operands.\n");
    return false;
  }

  // Make sure that they can fit on the caller's stack.
  const SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  if (OutInfo.getStackSize() > FuncInfo->getBytesInStackArgArea()) {
    LLVM_DEBUG(dbgs() << "... Cannot fit call operands on caller's stack.\n");
    return false;
  }

  // Verify that the parameters in callee-saved registers match.
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const uint32_t *CallerPreservedMask = TRI->getCallPreservedMask(MF, CallerCC);
  MachineRegisterInfo &MRI = MF.getRegInfo();
  return parametersInCSRMatch(MRI, CallerPreservedMask, OutLocs, OutArgs);
}

/// Return true if the calling convention is one that we can guarantee TCO for.
static bool canGuaranteeTCO(CallingConv::ID CC) {
  return CC == CallingConv::Fast;
}

/// Return true if we might ever do TCO for calls with this calling convention.
static bool mayTailCallThisCC(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::C:
  case CallingConv::AMDGPU_Gfx:
    return true;
  default:
    return canGuaranteeTCO(CC);
  }
}

bool AMDGPUCallLowering::isEligibleForTailCallOptimization(
    MachineIRBuilder &B, CallLoweringInfo &Info,
    SmallVectorImpl<ArgInfo> &InArgs, SmallVectorImpl<ArgInfo> &OutArgs) const {
  // Must pass all target-independent checks in order to tail call optimize.
  if (!Info.IsTailCall)
    return false;

  // Indirect calls can't be tail calls, because the address can be divergent.
  // TODO Check divergence info if the call really is divergent.
  if (Info.Callee.isReg())
    return false;

  MachineFunction &MF = B.getMF();
  const Function &CallerF = MF.getFunction();
  CallingConv::ID CalleeCC = Info.CallConv;
  CallingConv::ID CallerCC = CallerF.getCallingConv();

  const SIRegisterInfo *TRI = MF.getSubtarget<GCNSubtarget>().getRegisterInfo();
  const uint32_t *CallerPreserved = TRI->getCallPreservedMask(MF, CallerCC);
  // Kernels aren't callable, and don't have a live in return address so it
  // doesn't make sense to do a tail call with entry functions.
  if (!CallerPreserved)
    return false;

  if (!mayTailCallThisCC(CalleeCC)) {
    LLVM_DEBUG(dbgs() << "... Calling convention cannot be tail called.\n");
    return false;
  }

  if (any_of(CallerF.args(), [](const Argument &A) {
        return A.hasByValAttr() || A.hasSwiftErrorAttr();
      })) {
    LLVM_DEBUG(dbgs() << "... Cannot tail call from callers with byval "
                         "or swifterror arguments\n");
    return false;
  }

  // If we have -tailcallopt, then we're done.
  if (MF.getTarget().Options.GuaranteedTailCallOpt)
    return canGuaranteeTCO(CalleeCC) && CalleeCC == CallerF.getCallingConv();

  // Verify that the incoming and outgoing arguments from the callee are
  // safe to tail call.
  if (!doCallerAndCalleePassArgsTheSameWay(Info, MF, InArgs)) {
    LLVM_DEBUG(
        dbgs()
        << "... Caller and callee have incompatible calling conventions.\n");
    return false;
  }

  if (!areCalleeOutgoingArgsTailCallable(Info, MF, OutArgs))
    return false;

  LLVM_DEBUG(dbgs() << "... Call is eligible for tail call optimization.\n");
  return true;
}

// Insert outgoing implicit arguments for a call, by inserting copies to the
// implicit argument registers and adding the necessary implicit uses to the
// call instruction.
void AMDGPUCallLowering::handleImplicitCallArguments(
    MachineIRBuilder &MIRBuilder, MachineInstrBuilder &CallInst,
    const GCNSubtarget &ST, const SIMachineFunctionInfo &FuncInfo,
    CallingConv::ID CalleeCC,
    ArrayRef<std::pair<MCRegister, Register>> ImplicitArgRegs) const {
  if (!ST.enableFlatScratch()) {
    // Insert copies for the SRD. In the HSA case, this should be an identity
    // copy.
    auto ScratchRSrcReg = MIRBuilder.buildCopy(LLT::fixed_vector(4, 32),
                                               FuncInfo.getScratchRSrcReg());

    auto CalleeRSrcReg = AMDGPU::isChainCC(CalleeCC)
                             ? AMDGPU::SGPR48_SGPR49_SGPR50_SGPR51
                             : AMDGPU::SGPR0_SGPR1_SGPR2_SGPR3;

    MIRBuilder.buildCopy(CalleeRSrcReg, ScratchRSrcReg);
    CallInst.addReg(CalleeRSrcReg, RegState::Implicit);
  }

  for (std::pair<MCRegister, Register> ArgReg : ImplicitArgRegs) {
    MIRBuilder.buildCopy((Register)ArgReg.first, ArgReg.second);
    CallInst.addReg(ArgReg.first, RegState::Implicit);
  }
}

bool AMDGPUCallLowering::lowerTailCall(
    MachineIRBuilder &MIRBuilder, CallLoweringInfo &Info,
    SmallVectorImpl<ArgInfo> &OutArgs) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const SITargetLowering &TLI = *getTLI<SITargetLowering>();

  // True when we're tail calling, but without -tailcallopt.
  bool IsSibCall = !MF.getTarget().Options.GuaranteedTailCallOpt;

  // Find out which ABI gets to decide where things go.
  CallingConv::ID CalleeCC = Info.CallConv;
  CCAssignFn *AssignFnFixed;
  CCAssignFn *AssignFnVarArg;
  std::tie(AssignFnFixed, AssignFnVarArg) = getAssignFnsForCC(CalleeCC, TLI);

  MachineInstrBuilder CallSeqStart;
  if (!IsSibCall)
    CallSeqStart = MIRBuilder.buildInstr(AMDGPU::ADJCALLSTACKUP);

  unsigned Opc =
      getCallOpcode(MF, Info.Callee.isReg(), true, ST.isWave32(), CalleeCC);
  auto MIB = MIRBuilder.buildInstrNoInsert(Opc);
  if (!addCallTargetOperands(MIB, MIRBuilder, Info))
    return false;

  // Byte offset for the tail call. When we are sibcalling, this will always
  // be 0.
  MIB.addImm(0);

  // If this is a chain call, we need to pass in the EXEC mask.
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  if (AMDGPU::isChainCC(Info.CallConv)) {
    ArgInfo ExecArg = Info.OrigArgs[1];
    assert(ExecArg.Regs.size() == 1 && "Too many regs for EXEC");

    if (!ExecArg.Ty->isIntegerTy(ST.getWavefrontSize()))
      return false;

    if (auto CI = dyn_cast<ConstantInt>(ExecArg.OrigValue)) {
      MIB.addImm(CI->getSExtValue());
    } else {
      MIB.addReg(ExecArg.Regs[0]);
      unsigned Idx = MIB->getNumOperands() - 1;
      MIB->getOperand(Idx).setReg(constrainOperandRegClass(
          MF, *TRI, MRI, *ST.getInstrInfo(), *ST.getRegBankInfo(), *MIB,
          MIB->getDesc(), MIB->getOperand(Idx), Idx));
    }
  }

  // Tell the call which registers are clobbered.
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, CalleeCC);
  MIB.addRegMask(Mask);

  // FPDiff is the byte offset of the call's argument area from the callee's.
  // Stores to callee stack arguments will be placed in FixedStackSlots offset
  // by this amount for a tail call. In a sibling call it must be 0 because the
  // caller will deallocate the entire stack and the callee still expects its
  // arguments to begin at SP+0.
  int FPDiff = 0;

  // This will be 0 for sibcalls, potentially nonzero for tail calls produced
  // by -tailcallopt. For sibcalls, the memory operands for the call are
  // already available in the caller's incoming argument space.
  unsigned NumBytes = 0;
  if (!IsSibCall) {
    // We aren't sibcalling, so we need to compute FPDiff. We need to do this
    // before handling assignments, because FPDiff must be known for memory
    // arguments.
    unsigned NumReusableBytes = FuncInfo->getBytesInStackArgArea();
    SmallVector<CCValAssign, 16> OutLocs;
    CCState OutInfo(CalleeCC, false, MF, OutLocs, F.getContext());

    // FIXME: Not accounting for callee implicit inputs
    OutgoingValueAssigner CalleeAssigner(AssignFnFixed, AssignFnVarArg);
    if (!determineAssignments(CalleeAssigner, OutArgs, OutInfo))
      return false;

    // The callee will pop the argument stack as a tail call. Thus, we must
    // keep it 16-byte aligned.
    NumBytes = alignTo(OutInfo.getStackSize(), ST.getStackAlignment());

    // FPDiff will be negative if this tail call requires more space than we
    // would automatically have in our incoming argument space. Positive if we
    // actually shrink the stack.
    FPDiff = NumReusableBytes - NumBytes;

    // The stack pointer must be 16-byte aligned at all times it's used for a
    // memory operation, which in practice means at *all* times and in
    // particular across call boundaries. Therefore our own arguments started at
    // a 16-byte aligned SP and the delta applied for the tail call should
    // satisfy the same constraint.
    assert(isAligned(ST.getStackAlignment(), FPDiff) &&
           "unaligned stack on tail call");
  }

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(Info.CallConv, Info.IsVarArg, MF, ArgLocs, F.getContext());

  // We could pass MIB and directly add the implicit uses to the call
  // now. However, as an aesthetic choice, place implicit argument operands
  // after the ordinary user argument registers.
  SmallVector<std::pair<MCRegister, Register>, 12> ImplicitArgRegs;

  if (Info.CallConv != CallingConv::AMDGPU_Gfx &&
      !AMDGPU::isChainCC(Info.CallConv)) {
    // With a fixed ABI, allocate fixed registers before user arguments.
    if (!passSpecialInputs(MIRBuilder, CCInfo, ImplicitArgRegs, Info))
      return false;
  }

  OutgoingValueAssigner Assigner(AssignFnFixed, AssignFnVarArg);

  if (!determineAssignments(Assigner, OutArgs, CCInfo))
    return false;

  // Do the actual argument marshalling.
  AMDGPUOutgoingArgHandler Handler(MIRBuilder, MRI, MIB, true, FPDiff);
  if (!handleAssignments(Handler, OutArgs, CCInfo, ArgLocs, MIRBuilder))
    return false;

  if (Info.ConvergenceCtrlToken) {
    MIB.addUse(Info.ConvergenceCtrlToken, RegState::Implicit);
  }
  handleImplicitCallArguments(MIRBuilder, MIB, ST, *FuncInfo, CalleeCC,
                              ImplicitArgRegs);

  // If we have -tailcallopt, we need to adjust the stack. We'll do the call
  // sequence start and end here.
  if (!IsSibCall) {
    MIB->getOperand(1).setImm(FPDiff);
    CallSeqStart.addImm(NumBytes).addImm(0);
    // End the call sequence *before* emitting the call. Normally, we would
    // tidy the frame up after the call. However, here, we've laid out the
    // parameters so that when SP is reset, they will be in the correct
    // location.
    MIRBuilder.buildInstr(AMDGPU::ADJCALLSTACKDOWN).addImm(NumBytes).addImm(0);
  }

  // Now we can add the actual call instruction to the correct basic block.
  MIRBuilder.insertInstr(MIB);

  // If Callee is a reg, since it is used by a target specific
  // instruction, it must have a register class matching the
  // constraint of that instruction.

  // FIXME: We should define regbankselectable call instructions to handle
  // divergent call targets.
  if (MIB->getOperand(0).isReg()) {
    MIB->getOperand(0).setReg(constrainOperandRegClass(
        MF, *TRI, MRI, *ST.getInstrInfo(), *ST.getRegBankInfo(), *MIB,
        MIB->getDesc(), MIB->getOperand(0), 0));
  }

  MF.getFrameInfo().setHasTailCall();
  Info.LoweredTailCall = true;
  return true;
}

/// Lower a call to the @llvm.amdgcn.cs.chain intrinsic.
bool AMDGPUCallLowering::lowerChainCall(MachineIRBuilder &MIRBuilder,
                                        CallLoweringInfo &Info) const {
  ArgInfo Callee = Info.OrigArgs[0];
  ArgInfo SGPRArgs = Info.OrigArgs[2];
  ArgInfo VGPRArgs = Info.OrigArgs[3];
  ArgInfo Flags = Info.OrigArgs[4];

  assert(cast<ConstantInt>(Flags.OrigValue)->isZero() &&
         "Non-zero flags aren't supported yet.");
  assert(Info.OrigArgs.size() == 5 && "Additional args aren't supported yet.");

  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  const DataLayout &DL = F.getDataLayout();

  // The function to jump to is actually the first argument, so we'll change the
  // Callee and other info to match that before using our existing helper.
  const Value *CalleeV = Callee.OrigValue->stripPointerCasts();
  if (const Function *F = dyn_cast<Function>(CalleeV)) {
    Info.Callee = MachineOperand::CreateGA(F, 0);
    Info.CallConv = F->getCallingConv();
  } else {
    assert(Callee.Regs.size() == 1 && "Too many regs for the callee");
    Info.Callee = MachineOperand::CreateReg(Callee.Regs[0], false);
    Info.CallConv = CallingConv::AMDGPU_CS_Chain; // amdgpu_cs_chain_preserve
                                                  // behaves the same here.
  }

  // The function that we're calling cannot be vararg (only the intrinsic is).
  Info.IsVarArg = false;

  assert(std::all_of(SGPRArgs.Flags.begin(), SGPRArgs.Flags.end(),
                     [](ISD::ArgFlagsTy F) { return F.isInReg(); }) &&
         "SGPR arguments should be marked inreg");
  assert(std::none_of(VGPRArgs.Flags.begin(), VGPRArgs.Flags.end(),
                      [](ISD::ArgFlagsTy F) { return F.isInReg(); }) &&
         "VGPR arguments should not be marked inreg");

  SmallVector<ArgInfo, 8> OutArgs;
  splitToValueTypes(SGPRArgs, OutArgs, DL, Info.CallConv);
  splitToValueTypes(VGPRArgs, OutArgs, DL, Info.CallConv);

  Info.IsMustTailCall = true;
  return lowerTailCall(MIRBuilder, Info, OutArgs);
}

bool AMDGPUCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                   CallLoweringInfo &Info) const {
  if (Function *F = Info.CB->getCalledFunction())
    if (F->isIntrinsic()) {
      assert(F->getIntrinsicID() == Intrinsic::amdgcn_cs_chain &&
             "Unexpected intrinsic");
      return lowerChainCall(MIRBuilder, Info);
    }

  if (Info.IsVarArg) {
    LLVM_DEBUG(dbgs() << "Variadic functions not implemented\n");
    return false;
  }

  MachineFunction &MF = MIRBuilder.getMF();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();

  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const SITargetLowering &TLI = *getTLI<SITargetLowering>();
  const DataLayout &DL = F.getDataLayout();

  SmallVector<ArgInfo, 8> OutArgs;
  for (auto &OrigArg : Info.OrigArgs)
    splitToValueTypes(OrigArg, OutArgs, DL, Info.CallConv);

  SmallVector<ArgInfo, 8> InArgs;
  if (Info.CanLowerReturn && !Info.OrigRet.Ty->isVoidTy())
    splitToValueTypes(Info.OrigRet, InArgs, DL, Info.CallConv);

  // If we can lower as a tail call, do that instead.
  bool CanTailCallOpt =
      isEligibleForTailCallOptimization(MIRBuilder, Info, InArgs, OutArgs);

  // We must emit a tail call if we have musttail.
  if (Info.IsMustTailCall && !CanTailCallOpt) {
    LLVM_DEBUG(dbgs() << "Failed to lower musttail call as tail call\n");
    return false;
  }

  Info.IsTailCall = CanTailCallOpt;
  if (CanTailCallOpt)
    return lowerTailCall(MIRBuilder, Info, OutArgs);

  // Find out which ABI gets to decide where things go.
  CCAssignFn *AssignFnFixed;
  CCAssignFn *AssignFnVarArg;
  std::tie(AssignFnFixed, AssignFnVarArg) =
      getAssignFnsForCC(Info.CallConv, TLI);

  MIRBuilder.buildInstr(AMDGPU::ADJCALLSTACKUP)
    .addImm(0)
    .addImm(0);

  // Create a temporarily-floating call instruction so we can add the implicit
  // uses of arg registers.
  unsigned Opc = getCallOpcode(MF, Info.Callee.isReg(), false, ST.isWave32(),
                               Info.CallConv);

  auto MIB = MIRBuilder.buildInstrNoInsert(Opc);
  MIB.addDef(TRI->getReturnAddressReg(MF));

  if (!Info.IsConvergent)
    MIB.setMIFlag(MachineInstr::NoConvergent);

  if (!addCallTargetOperands(MIB, MIRBuilder, Info))
    return false;

  // Tell the call which registers are clobbered.
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, Info.CallConv);
  MIB.addRegMask(Mask);

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(Info.CallConv, Info.IsVarArg, MF, ArgLocs, F.getContext());

  // We could pass MIB and directly add the implicit uses to the call
  // now. However, as an aesthetic choice, place implicit argument operands
  // after the ordinary user argument registers.
  SmallVector<std::pair<MCRegister, Register>, 12> ImplicitArgRegs;

  if (Info.CallConv != CallingConv::AMDGPU_Gfx) {
    // With a fixed ABI, allocate fixed registers before user arguments.
    if (!passSpecialInputs(MIRBuilder, CCInfo, ImplicitArgRegs, Info))
      return false;
  }

  // Do the actual argument marshalling.
  SmallVector<Register, 8> PhysRegs;

  OutgoingValueAssigner Assigner(AssignFnFixed, AssignFnVarArg);
  if (!determineAssignments(Assigner, OutArgs, CCInfo))
    return false;

  AMDGPUOutgoingArgHandler Handler(MIRBuilder, MRI, MIB, false);
  if (!handleAssignments(Handler, OutArgs, CCInfo, ArgLocs, MIRBuilder))
    return false;

  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  if (Info.ConvergenceCtrlToken) {
    MIB.addUse(Info.ConvergenceCtrlToken, RegState::Implicit);
  }
  handleImplicitCallArguments(MIRBuilder, MIB, ST, *MFI, Info.CallConv,
                              ImplicitArgRegs);

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = CCInfo.getStackSize();

  // If Callee is a reg, since it is used by a target specific
  // instruction, it must have a register class matching the
  // constraint of that instruction.

  // FIXME: We should define regbankselectable call instructions to handle
  // divergent call targets.
  if (MIB->getOperand(1).isReg()) {
    MIB->getOperand(1).setReg(constrainOperandRegClass(
        MF, *TRI, MRI, *ST.getInstrInfo(),
        *ST.getRegBankInfo(), *MIB, MIB->getDesc(), MIB->getOperand(1),
        1));
  }

  // Now we can add the actual call instruction to the correct position.
  MIRBuilder.insertInstr(MIB);

  // Finally we can copy the returned value back into its virtual-register. In
  // symmetry with the arguments, the physical register must be an
  // implicit-define of the call instruction.
  if (Info.CanLowerReturn && !Info.OrigRet.Ty->isVoidTy()) {
    CCAssignFn *RetAssignFn = TLI.CCAssignFnForReturn(Info.CallConv,
                                                      Info.IsVarArg);
    IncomingValueAssigner Assigner(RetAssignFn);
    CallReturnHandler Handler(MIRBuilder, MRI, MIB);
    if (!determineAndHandleAssignments(Handler, Assigner, InArgs, MIRBuilder,
                                       Info.CallConv, Info.IsVarArg))
      return false;
  }

  uint64_t CalleePopBytes = NumBytes;

  MIRBuilder.buildInstr(AMDGPU::ADJCALLSTACKDOWN)
            .addImm(0)
            .addImm(CalleePopBytes);

  if (!Info.CanLowerReturn) {
    insertSRetLoads(MIRBuilder, Info.OrigRet.Ty, Info.OrigRet.Regs,
                    Info.DemoteRegister, Info.DemoteStackIndex);
  }

  return true;
}
