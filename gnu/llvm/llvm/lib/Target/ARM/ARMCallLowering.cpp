//===- llvm/lib/Target/ARM/ARMCallLowering.cpp - Call lowering ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements the lowering of LLVM calls to machine code calls for
/// GlobalISel.
//
//===----------------------------------------------------------------------===//

#include "ARMCallLowering.h"
#include "ARMBaseInstrInfo.h"
#include "ARMISelLowering.h"
#include "ARMSubtarget.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/LowLevelTypeUtils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <utility>

using namespace llvm;

// Whether Big-endian GISel is enabled, defaults to off, can be enabled for
// testing.
static cl::opt<bool>
    EnableGISelBigEndian("enable-arm-gisel-bigendian", cl::Hidden,
                         cl::init(false),
                         cl::desc("Enable Global-ISel Big Endian Lowering"));

ARMCallLowering::ARMCallLowering(const ARMTargetLowering &TLI)
    : CallLowering(&TLI) {}

static bool isSupportedType(const DataLayout &DL, const ARMTargetLowering &TLI,
                            Type *T) {
  if (T->isArrayTy())
    return isSupportedType(DL, TLI, T->getArrayElementType());

  if (T->isStructTy()) {
    // For now we only allow homogeneous structs that we can manipulate with
    // G_MERGE_VALUES and G_UNMERGE_VALUES
    auto StructT = cast<StructType>(T);
    for (unsigned i = 1, e = StructT->getNumElements(); i != e; ++i)
      if (StructT->getElementType(i) != StructT->getElementType(0))
        return false;
    return isSupportedType(DL, TLI, StructT->getElementType(0));
  }

  EVT VT = TLI.getValueType(DL, T, true);
  if (!VT.isSimple() || VT.isVector() ||
      !(VT.isInteger() || VT.isFloatingPoint()))
    return false;

  unsigned VTSize = VT.getSimpleVT().getSizeInBits();

  if (VTSize == 64)
    // FIXME: Support i64 too
    return VT.isFloatingPoint();

  return VTSize == 1 || VTSize == 8 || VTSize == 16 || VTSize == 32;
}

namespace {

/// Helper class for values going out through an ABI boundary (used for handling
/// function return values and call parameters).
struct ARMOutgoingValueHandler : public CallLowering::OutgoingValueHandler {
  ARMOutgoingValueHandler(MachineIRBuilder &MIRBuilder,
                          MachineRegisterInfo &MRI, MachineInstrBuilder &MIB)
      : OutgoingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    assert((Size == 1 || Size == 2 || Size == 4 || Size == 8) &&
           "Unsupported size");

    LLT p0 = LLT::pointer(0, 32);
    LLT s32 = LLT::scalar(32);
    auto SPReg = MIRBuilder.buildCopy(p0, Register(ARM::SP));

    auto OffsetReg = MIRBuilder.buildConstant(s32, Offset);

    auto AddrReg = MIRBuilder.buildPtrAdd(p0, SPReg, OffsetReg);

    MPO = MachinePointerInfo::getStack(MIRBuilder.getMF(), Offset);
    return AddrReg.getReg(0);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    assert(VA.isRegLoc() && "Value shouldn't be assigned to reg");
    assert(VA.getLocReg() == PhysReg && "Assigning to the wrong reg?");

    assert(VA.getValVT().getSizeInBits() <= 64 && "Unsupported value size");
    assert(VA.getLocVT().getSizeInBits() <= 64 && "Unsupported location size");

    Register ExtReg = extendRegister(ValVReg, VA);
    MIRBuilder.buildCopy(PhysReg, ExtReg);
    MIB.addUse(PhysReg, RegState::Implicit);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    Register ExtReg = extendRegister(ValVReg, VA);
    auto MMO = MIRBuilder.getMF().getMachineMemOperand(
        MPO, MachineMemOperand::MOStore, MemTy, Align(1));
    MIRBuilder.buildStore(ExtReg, Addr, *MMO);
  }

  unsigned assignCustomValue(CallLowering::ArgInfo &Arg,
                             ArrayRef<CCValAssign> VAs,
                             std::function<void()> *Thunk) override {
    assert(Arg.Regs.size() == 1 && "Can't handle multple regs yet");

    const CCValAssign &VA = VAs[0];
    assert(VA.needsCustom() && "Value doesn't need custom handling");

    // Custom lowering for other types, such as f16, is currently not supported
    if (VA.getValVT() != MVT::f64)
      return 0;

    const CCValAssign &NextVA = VAs[1];
    assert(NextVA.needsCustom() && "Value doesn't need custom handling");
    assert(NextVA.getValVT() == MVT::f64 && "Unsupported type");

    assert(VA.getValNo() == NextVA.getValNo() &&
           "Values belong to different arguments");

    assert(VA.isRegLoc() && "Value should be in reg");
    assert(NextVA.isRegLoc() && "Value should be in reg");

    Register NewRegs[] = {MRI.createGenericVirtualRegister(LLT::scalar(32)),
                          MRI.createGenericVirtualRegister(LLT::scalar(32))};
    MIRBuilder.buildUnmerge(NewRegs, Arg.Regs[0]);

    bool IsLittle = MIRBuilder.getMF().getSubtarget<ARMSubtarget>().isLittle();
    if (!IsLittle)
      std::swap(NewRegs[0], NewRegs[1]);

    if (Thunk) {
      *Thunk = [=]() {
        assignValueToReg(NewRegs[0], VA.getLocReg(), VA);
        assignValueToReg(NewRegs[1], NextVA.getLocReg(), NextVA);
      };
      return 2;
    }
    assignValueToReg(NewRegs[0], VA.getLocReg(), VA);
    assignValueToReg(NewRegs[1], NextVA.getLocReg(), NextVA);
    return 2;
  }

  MachineInstrBuilder MIB;
};

} // end anonymous namespace

/// Lower the return value for the already existing \p Ret. This assumes that
/// \p MIRBuilder's insertion point is correct.
bool ARMCallLowering::lowerReturnVal(MachineIRBuilder &MIRBuilder,
                                     const Value *Val, ArrayRef<Register> VRegs,
                                     MachineInstrBuilder &Ret) const {
  if (!Val)
    // Nothing to do here.
    return true;

  auto &MF = MIRBuilder.getMF();
  const auto &F = MF.getFunction();

  const auto &DL = MF.getDataLayout();
  auto &TLI = *getTLI<ARMTargetLowering>();
  if (!isSupportedType(DL, TLI, Val->getType()))
    return false;

  ArgInfo OrigRetInfo(VRegs, Val->getType(), 0);
  setArgFlags(OrigRetInfo, AttributeList::ReturnIndex, DL, F);

  SmallVector<ArgInfo, 4> SplitRetInfos;
  splitToValueTypes(OrigRetInfo, SplitRetInfos, DL, F.getCallingConv());

  CCAssignFn *AssignFn =
      TLI.CCAssignFnForReturn(F.getCallingConv(), F.isVarArg());

  OutgoingValueAssigner RetAssigner(AssignFn);
  ARMOutgoingValueHandler RetHandler(MIRBuilder, MF.getRegInfo(), Ret);
  return determineAndHandleAssignments(RetHandler, RetAssigner, SplitRetInfos,
                                       MIRBuilder, F.getCallingConv(),
                                       F.isVarArg());
}

bool ARMCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                  const Value *Val, ArrayRef<Register> VRegs,
                                  FunctionLoweringInfo &FLI) const {
  assert(!Val == VRegs.empty() && "Return value without a vreg");

  auto const &ST = MIRBuilder.getMF().getSubtarget<ARMSubtarget>();
  unsigned Opcode = ST.getReturnOpcode();
  auto Ret = MIRBuilder.buildInstrNoInsert(Opcode).add(predOps(ARMCC::AL));

  if (!lowerReturnVal(MIRBuilder, Val, VRegs, Ret))
    return false;

  MIRBuilder.insertInstr(Ret);
  return true;
}

namespace {

/// Helper class for values coming in through an ABI boundary (used for handling
/// formal arguments and call return values).
struct ARMIncomingValueHandler : public CallLowering::IncomingValueHandler {
  ARMIncomingValueHandler(MachineIRBuilder &MIRBuilder,
                          MachineRegisterInfo &MRI)
      : IncomingValueHandler(MIRBuilder, MRI) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    assert((Size == 1 || Size == 2 || Size == 4 || Size == 8) &&
           "Unsupported size");

    auto &MFI = MIRBuilder.getMF().getFrameInfo();

    // Byval is assumed to be writable memory, but other stack passed arguments
    // are not.
    const bool IsImmutable = !Flags.isByVal();

    int FI = MFI.CreateFixedObject(Size, Offset, IsImmutable);
    MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);

    return MIRBuilder.buildFrameIndex(LLT::pointer(MPO.getAddrSpace(), 32), FI)
        .getReg(0);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    if (VA.getLocInfo() == CCValAssign::SExt ||
        VA.getLocInfo() == CCValAssign::ZExt) {
      // If the value is zero- or sign-extended, its size becomes 4 bytes, so
      // that's what we should load.
      MemTy = LLT::scalar(32);
      assert(MRI.getType(ValVReg).isScalar() && "Only scalars supported atm");

      auto LoadVReg = buildLoad(LLT::scalar(32), Addr, MemTy, MPO);
      MIRBuilder.buildTrunc(ValVReg, LoadVReg);
    } else {
      // If the value is not extended, a simple load will suffice.
      buildLoad(ValVReg, Addr, MemTy, MPO);
    }
  }

  MachineInstrBuilder buildLoad(const DstOp &Res, Register Addr, LLT MemTy,
                                const MachinePointerInfo &MPO) {
    MachineFunction &MF = MIRBuilder.getMF();

    auto MMO = MF.getMachineMemOperand(MPO, MachineMemOperand::MOLoad, MemTy,
                                       inferAlignFromPtrInfo(MF, MPO));
    return MIRBuilder.buildLoad(Res, Addr, *MMO);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    assert(VA.isRegLoc() && "Value shouldn't be assigned to reg");
    assert(VA.getLocReg() == PhysReg && "Assigning to the wrong reg?");

    uint64_t ValSize = VA.getValVT().getFixedSizeInBits();
    uint64_t LocSize = VA.getLocVT().getFixedSizeInBits();

    assert(ValSize <= 64 && "Unsupported value size");
    assert(LocSize <= 64 && "Unsupported location size");

    markPhysRegUsed(PhysReg);
    if (ValSize == LocSize) {
      MIRBuilder.buildCopy(ValVReg, PhysReg);
    } else {
      assert(ValSize < LocSize && "Extensions not supported");

      // We cannot create a truncating copy, nor a trunc of a physical register.
      // Therefore, we need to copy the content of the physical register into a
      // virtual one and then truncate that.
      auto PhysRegToVReg = MIRBuilder.buildCopy(LLT::scalar(LocSize), PhysReg);
      MIRBuilder.buildTrunc(ValVReg, PhysRegToVReg);
    }
  }

  unsigned assignCustomValue(ARMCallLowering::ArgInfo &Arg,
                             ArrayRef<CCValAssign> VAs,
                             std::function<void()> *Thunk) override {
    assert(Arg.Regs.size() == 1 && "Can't handle multple regs yet");

    const CCValAssign &VA = VAs[0];
    assert(VA.needsCustom() && "Value doesn't need custom handling");

    // Custom lowering for other types, such as f16, is currently not supported
    if (VA.getValVT() != MVT::f64)
      return 0;

    const CCValAssign &NextVA = VAs[1];
    assert(NextVA.needsCustom() && "Value doesn't need custom handling");
    assert(NextVA.getValVT() == MVT::f64 && "Unsupported type");

    assert(VA.getValNo() == NextVA.getValNo() &&
           "Values belong to different arguments");

    assert(VA.isRegLoc() && "Value should be in reg");
    assert(NextVA.isRegLoc() && "Value should be in reg");

    Register NewRegs[] = {MRI.createGenericVirtualRegister(LLT::scalar(32)),
                          MRI.createGenericVirtualRegister(LLT::scalar(32))};

    assignValueToReg(NewRegs[0], VA.getLocReg(), VA);
    assignValueToReg(NewRegs[1], NextVA.getLocReg(), NextVA);

    bool IsLittle = MIRBuilder.getMF().getSubtarget<ARMSubtarget>().isLittle();
    if (!IsLittle)
      std::swap(NewRegs[0], NewRegs[1]);

    MIRBuilder.buildMergeLikeInstr(Arg.Regs[0], NewRegs);

    return 2;
  }

  /// Marking a physical register as used is different between formal
  /// parameters, where it's a basic block live-in, and call returns, where it's
  /// an implicit-def of the call instruction.
  virtual void markPhysRegUsed(unsigned PhysReg) = 0;
};

struct FormalArgHandler : public ARMIncomingValueHandler {
  FormalArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
      : ARMIncomingValueHandler(MIRBuilder, MRI) {}

  void markPhysRegUsed(unsigned PhysReg) override {
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }
};

} // end anonymous namespace

bool ARMCallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                           const Function &F,
                                           ArrayRef<ArrayRef<Register>> VRegs,
                                           FunctionLoweringInfo &FLI) const {
  auto &TLI = *getTLI<ARMTargetLowering>();
  auto Subtarget = TLI.getSubtarget();

  if (Subtarget->isThumb1Only())
    return false;

  // Quick exit if there aren't any args
  if (F.arg_empty())
    return true;

  if (F.isVarArg())
    return false;

  auto &MF = MIRBuilder.getMF();
  auto &MBB = MIRBuilder.getMBB();
  const auto &DL = MF.getDataLayout();

  for (auto &Arg : F.args()) {
    if (!isSupportedType(DL, TLI, Arg.getType()))
      return false;
    if (Arg.hasPassPointeeByValueCopyAttr())
      return false;
  }

  CCAssignFn *AssignFn =
      TLI.CCAssignFnForCall(F.getCallingConv(), F.isVarArg());

  OutgoingValueAssigner ArgAssigner(AssignFn);
  FormalArgHandler ArgHandler(MIRBuilder, MIRBuilder.getMF().getRegInfo());

  SmallVector<ArgInfo, 8> SplitArgInfos;
  unsigned Idx = 0;
  for (auto &Arg : F.args()) {
    ArgInfo OrigArgInfo(VRegs[Idx], Arg.getType(), Idx);

    setArgFlags(OrigArgInfo, Idx + AttributeList::FirstArgIndex, DL, F);
    splitToValueTypes(OrigArgInfo, SplitArgInfos, DL, F.getCallingConv());

    Idx++;
  }

  if (!MBB.empty())
    MIRBuilder.setInstr(*MBB.begin());

  if (!determineAndHandleAssignments(ArgHandler, ArgAssigner, SplitArgInfos,
                                     MIRBuilder, F.getCallingConv(),
                                     F.isVarArg()))
    return false;

  // Move back to the end of the basic block.
  MIRBuilder.setMBB(MBB);
  return true;
}

namespace {

struct CallReturnHandler : public ARMIncomingValueHandler {
  CallReturnHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                    MachineInstrBuilder MIB)
      : ARMIncomingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

  void markPhysRegUsed(unsigned PhysReg) override {
    MIB.addDef(PhysReg, RegState::Implicit);
  }

  MachineInstrBuilder MIB;
};

// FIXME: This should move to the ARMSubtarget when it supports all the opcodes.
unsigned getCallOpcode(const MachineFunction &MF, const ARMSubtarget &STI,
                       bool isDirect) {
  if (isDirect)
    return STI.isThumb() ? ARM::tBL : ARM::BL;

  if (STI.isThumb())
    return gettBLXrOpcode(MF);

  if (STI.hasV5TOps())
    return getBLXOpcode(MF);

  if (STI.hasV4TOps())
    return ARM::BX_CALL;

  return ARM::BMOVPCRX_CALL;
}
} // end anonymous namespace

bool ARMCallLowering::lowerCall(MachineIRBuilder &MIRBuilder, CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const auto &TLI = *getTLI<ARMTargetLowering>();
  const auto &DL = MF.getDataLayout();
  const auto &STI = MF.getSubtarget<ARMSubtarget>();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  if (STI.genLongCalls())
    return false;

  if (STI.isThumb1Only())
    return false;

  auto CallSeqStart = MIRBuilder.buildInstr(ARM::ADJCALLSTACKDOWN);

  // Create the call instruction so we can add the implicit uses of arg
  // registers, but don't insert it yet.
  bool IsDirect = !Info.Callee.isReg();
  auto CallOpcode = getCallOpcode(MF, STI, IsDirect);
  auto MIB = MIRBuilder.buildInstrNoInsert(CallOpcode);

  bool IsThumb = STI.isThumb();
  if (IsThumb)
    MIB.add(predOps(ARMCC::AL));

  MIB.add(Info.Callee);
  if (!IsDirect) {
    auto CalleeReg = Info.Callee.getReg();
    if (CalleeReg && !CalleeReg.isPhysical()) {
      unsigned CalleeIdx = IsThumb ? 2 : 0;
      MIB->getOperand(CalleeIdx).setReg(constrainOperandRegClass(
          MF, *TRI, MRI, *STI.getInstrInfo(), *STI.getRegBankInfo(),
          *MIB.getInstr(), MIB->getDesc(), Info.Callee, CalleeIdx));
    }
  }

  MIB.addRegMask(TRI->getCallPreservedMask(MF, Info.CallConv));

  SmallVector<ArgInfo, 8> ArgInfos;
  for (auto Arg : Info.OrigArgs) {
    if (!isSupportedType(DL, TLI, Arg.Ty))
      return false;

    if (Arg.Flags[0].isByVal())
      return false;

    splitToValueTypes(Arg, ArgInfos, DL, Info.CallConv);
  }

  auto ArgAssignFn = TLI.CCAssignFnForCall(Info.CallConv, Info.IsVarArg);
  OutgoingValueAssigner ArgAssigner(ArgAssignFn);
  ARMOutgoingValueHandler ArgHandler(MIRBuilder, MRI, MIB);
  if (!determineAndHandleAssignments(ArgHandler, ArgAssigner, ArgInfos,
                                     MIRBuilder, Info.CallConv, Info.IsVarArg))
    return false;

  // Now we can add the actual call instruction to the correct basic block.
  MIRBuilder.insertInstr(MIB);

  if (!Info.OrigRet.Ty->isVoidTy()) {
    if (!isSupportedType(DL, TLI, Info.OrigRet.Ty))
      return false;

    ArgInfos.clear();
    splitToValueTypes(Info.OrigRet, ArgInfos, DL, Info.CallConv);
    auto RetAssignFn = TLI.CCAssignFnForReturn(Info.CallConv, Info.IsVarArg);
    OutgoingValueAssigner Assigner(RetAssignFn);
    CallReturnHandler RetHandler(MIRBuilder, MRI, MIB);
    if (!determineAndHandleAssignments(RetHandler, Assigner, ArgInfos,
                                       MIRBuilder, Info.CallConv,
                                       Info.IsVarArg))
      return false;
  }

  // We now know the size of the stack - update the ADJCALLSTACKDOWN
  // accordingly.
  CallSeqStart.addImm(ArgAssigner.StackSize).addImm(0).add(predOps(ARMCC::AL));

  MIRBuilder.buildInstr(ARM::ADJCALLSTACKUP)
      .addImm(ArgAssigner.StackSize)
      .addImm(-1ULL)
      .add(predOps(ARMCC::AL));

  return true;
}

bool ARMCallLowering::enableBigEndian() const { return EnableGISelBigEndian; }