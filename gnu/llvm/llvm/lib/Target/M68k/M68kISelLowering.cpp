//===-- M68kISelLowering.cpp - M68k DAG Lowering Impl -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the interfaces that M68k uses to lower LLVM code into a
/// selection DAG.
///
//===----------------------------------------------------------------------===//

#include "M68kISelLowering.h"
#include "M68kCallingConv.h"
#include "M68kMachineFunction.h"
#include "M68kSubtarget.h"
#include "M68kTargetMachine.h"
#include "M68kTargetObjectFile.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "M68k-isel"

STATISTIC(NumTailCalls, "Number of tail calls");

M68kTargetLowering::M68kTargetLowering(const M68kTargetMachine &TM,
                                       const M68kSubtarget &STI)
    : TargetLowering(TM), Subtarget(STI), TM(TM) {

  MVT PtrVT = MVT::i32;

  setBooleanContents(ZeroOrOneBooleanContent);

  auto *RegInfo = Subtarget.getRegisterInfo();
  setStackPointerRegisterToSaveRestore(RegInfo->getStackRegister());

  // Set up the register classes.
  addRegisterClass(MVT::i8, &M68k::DR8RegClass);
  addRegisterClass(MVT::i16, &M68k::XR16RegClass);
  addRegisterClass(MVT::i32, &M68k::XR32RegClass);

  for (auto VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
  }

  // We don't accept any truncstore of integer registers.
  setTruncStoreAction(MVT::i64, MVT::i32, Expand);
  setTruncStoreAction(MVT::i64, MVT::i16, Expand);
  setTruncStoreAction(MVT::i64, MVT::i8, Expand);
  setTruncStoreAction(MVT::i32, MVT::i16, Expand);
  setTruncStoreAction(MVT::i32, MVT::i8, Expand);
  setTruncStoreAction(MVT::i16, MVT::i8, Expand);

  setOperationAction({ISD::MUL, ISD::SDIV, ISD::UDIV}, MVT::i8, Promote);
  setOperationAction({ISD::MUL, ISD::SDIV, ISD::UDIV}, MVT::i16, Legal);
  if (Subtarget.atLeastM68020())
    setOperationAction({ISD::MUL, ISD::SDIV, ISD::UDIV}, MVT::i32, Legal);
  else
    setOperationAction({ISD::MUL, ISD::SDIV, ISD::UDIV}, MVT::i32, LibCall);
  setOperationAction(ISD::MUL, MVT::i64, LibCall);

  for (auto OP :
       {ISD::SREM, ISD::UREM, ISD::UDIVREM, ISD::SDIVREM,
        ISD::MULHS, ISD::MULHU, ISD::UMUL_LOHI, ISD::SMUL_LOHI}) {
    setOperationAction(OP, MVT::i8, Promote);
    setOperationAction(OP, MVT::i16, Legal);
    setOperationAction(OP, MVT::i32, LibCall);
  }

  for (auto OP : {ISD::UMUL_LOHI, ISD::SMUL_LOHI}) {
    setOperationAction(OP, MVT::i8, Expand);
    setOperationAction(OP, MVT::i16, Expand);
  }

  for (auto OP : {ISD::SMULO, ISD::UMULO}) {
    setOperationAction(OP, MVT::i8,  Custom);
    setOperationAction(OP, MVT::i16, Custom);
    setOperationAction(OP, MVT::i32, Custom);
  }

  for (auto OP : {ISD::SHL_PARTS, ISD::SRA_PARTS, ISD::SRL_PARTS})
    setOperationAction(OP, MVT::i32, Custom);

  // Add/Sub overflow ops with MVT::Glues are lowered to CCR dependences.
  for (auto VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::ADDC, VT, Custom);
    setOperationAction(ISD::ADDE, VT, Custom);
    setOperationAction(ISD::SUBC, VT, Custom);
    setOperationAction(ISD::SUBE, VT, Custom);
  }

  // SADDO and friends are legal with this setup, i hope
  for (auto VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::SADDO, VT, Custom);
    setOperationAction(ISD::UADDO, VT, Custom);
    setOperationAction(ISD::SSUBO, VT, Custom);
    setOperationAction(ISD::USUBO, VT, Custom);
  }

  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);

  for (auto VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::BR_CC, VT, Expand);
    setOperationAction(ISD::SELECT, VT, Custom);
    setOperationAction(ISD::SELECT_CC, VT, Expand);
    setOperationAction(ISD::SETCC, VT, Custom);
    setOperationAction(ISD::SETCCCARRY, VT, Custom);
  }

  for (auto VT : {MVT::i8, MVT::i16, MVT::i32}) {
    setOperationAction(ISD::BSWAP, VT, Expand);
    setOperationAction(ISD::CTTZ, VT, Expand);
    setOperationAction(ISD::CTLZ, VT, Expand);
    setOperationAction(ISD::CTPOP, VT, Expand);
  }

  setOperationAction(ISD::ConstantPool, MVT::i32, Custom);
  setOperationAction(ISD::JumpTable, MVT::i32, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setOperationAction(ISD::ExternalSymbol, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i32, Custom);

  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);

  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  setOperationAction(ISD::DYNAMIC_STACKALLOC, PtrVT, Custom);

  computeRegisterProperties(STI.getRegisterInfo());

  // We lower the `atomic-compare-and-swap` to `__sync_val_compare_and_swap`
  // for subtarget < M68020
  setMaxAtomicSizeInBitsSupported(32);
  setOperationAction(ISD::ATOMIC_CMP_SWAP, {MVT::i8, MVT::i16, MVT::i32},
                     Subtarget.atLeastM68020() ? Legal : LibCall);

  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Custom);

  // M68k does not have native read-modify-write support, so expand all of them
  // to `__sync_fetch_*` for target < M68020, otherwise expand to CmpxChg.
  // See `shouldExpandAtomicRMWInIR` below.
  setOperationAction(
      {
          ISD::ATOMIC_LOAD_ADD,
          ISD::ATOMIC_LOAD_SUB,
          ISD::ATOMIC_LOAD_AND,
          ISD::ATOMIC_LOAD_OR,
          ISD::ATOMIC_LOAD_XOR,
          ISD::ATOMIC_LOAD_NAND,
          ISD::ATOMIC_LOAD_MIN,
          ISD::ATOMIC_LOAD_MAX,
          ISD::ATOMIC_LOAD_UMIN,
          ISD::ATOMIC_LOAD_UMAX,
          ISD::ATOMIC_SWAP,
      },
      {MVT::i8, MVT::i16, MVT::i32}, LibCall);

  setMinFunctionAlignment(Align(2));
}

TargetLoweringBase::AtomicExpansionKind
M68kTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const {
  return Subtarget.atLeastM68020()
             ? TargetLoweringBase::AtomicExpansionKind::CmpXChg
             : TargetLoweringBase::AtomicExpansionKind::None;
}

Register
M68kTargetLowering::getExceptionPointerRegister(const Constant *) const {
  return M68k::D0;
}

Register
M68kTargetLowering::getExceptionSelectorRegister(const Constant *) const {
  return M68k::D1;
}

InlineAsm::ConstraintCode
M68kTargetLowering::getInlineAsmMemConstraint(StringRef ConstraintCode) const {
  return StringSwitch<InlineAsm::ConstraintCode>(ConstraintCode)
      .Case("Q", InlineAsm::ConstraintCode::Q)
      // We borrow ConstraintCode::Um for 'U'.
      .Case("U", InlineAsm::ConstraintCode::Um)
      .Default(TargetLowering::getInlineAsmMemConstraint(ConstraintCode));
}

EVT M68kTargetLowering::getSetCCResultType(const DataLayout &DL,
                                           LLVMContext &Context, EVT VT) const {
  // M68k SETcc producess either 0x00 or 0xFF
  return MVT::i8;
}

MVT M68kTargetLowering::getScalarShiftAmountTy(const DataLayout &DL,
                                               EVT Ty) const {
  if (Ty.isSimple()) {
    return Ty.getSimpleVT();
  }
  return MVT::getIntegerVT(DL.getPointerSizeInBits(0));
}

#include "M68kGenCallingConv.inc"

enum StructReturnType { NotStructReturn, RegStructReturn, StackStructReturn };

static StructReturnType
callIsStructReturn(const SmallVectorImpl<ISD::OutputArg> &Outs) {
  if (Outs.empty())
    return NotStructReturn;

  const ISD::ArgFlagsTy &Flags = Outs[0].Flags;
  if (!Flags.isSRet())
    return NotStructReturn;
  if (Flags.isInReg())
    return RegStructReturn;
  return StackStructReturn;
}

/// Determines whether a function uses struct return semantics.
static StructReturnType
argsAreStructReturn(const SmallVectorImpl<ISD::InputArg> &Ins) {
  if (Ins.empty())
    return NotStructReturn;

  const ISD::ArgFlagsTy &Flags = Ins[0].Flags;
  if (!Flags.isSRet())
    return NotStructReturn;
  if (Flags.isInReg())
    return RegStructReturn;
  return StackStructReturn;
}

/// Make a copy of an aggregate at address specified by "Src" to address
/// "Dst" with size and alignment information specified by the specific
/// parameter attribute. The copy will be passed as a byval function parameter.
static SDValue CreateCopyOfByValArgument(SDValue Src, SDValue Dst,
                                         SDValue Chain, ISD::ArgFlagsTy Flags,
                                         SelectionDAG &DAG, const SDLoc &DL) {
  SDValue SizeNode = DAG.getConstant(Flags.getByValSize(), DL, MVT::i32);

  return DAG.getMemcpy(
      Chain, DL, Dst, Src, SizeNode, Flags.getNonZeroByValAlign(),
      /*isVolatile=*/false, /*AlwaysInline=*/true,
      /*CI=*/nullptr, std::nullopt, MachinePointerInfo(), MachinePointerInfo());
}

/// Return true if the calling convention is one that we can guarantee TCO for.
static bool canGuaranteeTCO(CallingConv::ID CC) { return false; }

/// Return true if we might ever do TCO for calls with this calling convention.
static bool mayTailCallThisCC(CallingConv::ID CC) {
  switch (CC) {
  // C calling conventions:
  case CallingConv::C:
    return true;
  default:
    return canGuaranteeTCO(CC);
  }
}

/// Return true if the function is being made into a tailcall target by
/// changing its ABI.
static bool shouldGuaranteeTCO(CallingConv::ID CC, bool GuaranteedTailCallOpt) {
  return GuaranteedTailCallOpt && canGuaranteeTCO(CC);
}

/// Return true if the given stack call argument is already available in the
/// same position (relatively) of the caller's incoming argument stack.
static bool MatchingStackOffset(SDValue Arg, unsigned Offset,
                                ISD::ArgFlagsTy Flags, MachineFrameInfo &MFI,
                                const MachineRegisterInfo *MRI,
                                const M68kInstrInfo *TII,
                                const CCValAssign &VA) {
  unsigned Bytes = Arg.getValueType().getSizeInBits() / 8;

  for (;;) {
    // Look through nodes that don't alter the bits of the incoming value.
    unsigned Op = Arg.getOpcode();
    if (Op == ISD::ZERO_EXTEND || Op == ISD::ANY_EXTEND || Op == ISD::BITCAST) {
      Arg = Arg.getOperand(0);
      continue;
    }
    if (Op == ISD::TRUNCATE) {
      const SDValue &TruncInput = Arg.getOperand(0);
      if (TruncInput.getOpcode() == ISD::AssertZext &&
          cast<VTSDNode>(TruncInput.getOperand(1))->getVT() ==
              Arg.getValueType()) {
        Arg = TruncInput.getOperand(0);
        continue;
      }
    }
    break;
  }

  int FI = INT_MAX;
  if (Arg.getOpcode() == ISD::CopyFromReg) {
    Register VR = cast<RegisterSDNode>(Arg.getOperand(1))->getReg();
    if (!Register::isVirtualRegister(VR))
      return false;
    MachineInstr *Def = MRI->getVRegDef(VR);
    if (!Def)
      return false;
    if (!Flags.isByVal()) {
      if (!TII->isLoadFromStackSlot(*Def, FI))
        return false;
    } else {
      unsigned Opcode = Def->getOpcode();
      if ((Opcode == M68k::LEA32p || Opcode == M68k::LEA32f) &&
          Def->getOperand(1).isFI()) {
        FI = Def->getOperand(1).getIndex();
        Bytes = Flags.getByValSize();
      } else
        return false;
    }
  } else if (auto *Ld = dyn_cast<LoadSDNode>(Arg)) {
    if (Flags.isByVal())
      // ByVal argument is passed in as a pointer but it's now being
      // dereferenced. e.g.
      // define @foo(%struct.X* %A) {
      //   tail call @bar(%struct.X* byval %A)
      // }
      return false;
    SDValue Ptr = Ld->getBasePtr();
    FrameIndexSDNode *FINode = dyn_cast<FrameIndexSDNode>(Ptr);
    if (!FINode)
      return false;
    FI = FINode->getIndex();
  } else if (Arg.getOpcode() == ISD::FrameIndex && Flags.isByVal()) {
    FrameIndexSDNode *FINode = cast<FrameIndexSDNode>(Arg);
    FI = FINode->getIndex();
    Bytes = Flags.getByValSize();
  } else
    return false;

  assert(FI != INT_MAX);
  if (!MFI.isFixedObjectIndex(FI))
    return false;

  if (Offset != MFI.getObjectOffset(FI))
    return false;

  if (VA.getLocVT().getSizeInBits() > Arg.getValueType().getSizeInBits()) {
    // If the argument location is wider than the argument type, check that any
    // extension flags match.
    if (Flags.isZExt() != MFI.isObjectZExt(FI) ||
        Flags.isSExt() != MFI.isObjectSExt(FI)) {
      return false;
    }
  }

  return Bytes == MFI.getObjectSize(FI);
}

SDValue
M68kTargetLowering::getReturnAddressFrameIndex(SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  M68kMachineFunctionInfo *FuncInfo = MF.getInfo<M68kMachineFunctionInfo>();
  int ReturnAddrIndex = FuncInfo->getRAIndex();

  if (ReturnAddrIndex == 0) {
    // Set up a frame object for the return address.
    unsigned SlotSize = Subtarget.getSlotSize();
    ReturnAddrIndex = MF.getFrameInfo().CreateFixedObject(
        SlotSize, -(int64_t)SlotSize, false);
    FuncInfo->setRAIndex(ReturnAddrIndex);
  }

  return DAG.getFrameIndex(ReturnAddrIndex, getPointerTy(DAG.getDataLayout()));
}

SDValue M68kTargetLowering::EmitTailCallLoadRetAddr(SelectionDAG &DAG,
                                                    SDValue &OutRetAddr,
                                                    SDValue Chain,
                                                    bool IsTailCall, int FPDiff,
                                                    const SDLoc &DL) const {
  EVT VT = getPointerTy(DAG.getDataLayout());
  OutRetAddr = getReturnAddressFrameIndex(DAG);

  // Load the "old" Return address.
  OutRetAddr = DAG.getLoad(VT, DL, Chain, OutRetAddr, MachinePointerInfo());
  return SDValue(OutRetAddr.getNode(), 1);
}

SDValue M68kTargetLowering::EmitTailCallStoreRetAddr(
    SelectionDAG &DAG, MachineFunction &MF, SDValue Chain, SDValue RetFI,
    EVT PtrVT, unsigned SlotSize, int FPDiff, const SDLoc &DL) const {
  if (!FPDiff)
    return Chain;

  // Calculate the new stack slot for the return address.
  int NewFO = MF.getFrameInfo().CreateFixedObject(
      SlotSize, (int64_t)FPDiff - SlotSize, false);

  SDValue NewFI = DAG.getFrameIndex(NewFO, PtrVT);
  // Store the return address to the appropriate stack slot.
  Chain = DAG.getStore(
      Chain, DL, RetFI, NewFI,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), NewFO));
  return Chain;
}

SDValue
M68kTargetLowering::LowerMemArgument(SDValue Chain, CallingConv::ID CallConv,
                                     const SmallVectorImpl<ISD::InputArg> &Ins,
                                     const SDLoc &DL, SelectionDAG &DAG,
                                     const CCValAssign &VA,
                                     MachineFrameInfo &MFI,
                                     unsigned ArgIdx) const {
  // Create the nodes corresponding to a load from this parameter slot.
  ISD::ArgFlagsTy Flags = Ins[ArgIdx].Flags;
  EVT ValVT;

  // If value is passed by pointer we have address passed instead of the value
  // itself.
  if (VA.getLocInfo() == CCValAssign::Indirect)
    ValVT = VA.getLocVT();
  else
    ValVT = VA.getValVT();

  // Because we are dealing with BE architecture we need to offset loading of
  // partial types
  int Offset = VA.getLocMemOffset();
  if (VA.getValVT() == MVT::i8) {
    Offset += 3;
  } else if (VA.getValVT() == MVT::i16) {
    Offset += 2;
  }

  // TODO Interrupt handlers
  // Calculate SP offset of interrupt parameter, re-arrange the slot normally
  // taken by a return address.

  // FIXME For now, all byval parameter objects are marked mutable. This can
  // be changed with more analysis. In case of tail call optimization mark all
  // arguments mutable. Since they could be overwritten by lowering of arguments
  // in case of a tail call.
  bool AlwaysUseMutable = shouldGuaranteeTCO(
      CallConv, DAG.getTarget().Options.GuaranteedTailCallOpt);
  bool IsImmutable = !AlwaysUseMutable && !Flags.isByVal();

  if (Flags.isByVal()) {
    unsigned Bytes = Flags.getByValSize();
    if (Bytes == 0)
      Bytes = 1; // Don't create zero-sized stack objects.
    int FI = MFI.CreateFixedObject(Bytes, Offset, IsImmutable);
    // TODO Interrupt handlers
    // Adjust SP offset of interrupt parameter.
    return DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
  } else {
    int FI =
        MFI.CreateFixedObject(ValVT.getSizeInBits() / 8, Offset, IsImmutable);

    // Set SExt or ZExt flag.
    if (VA.getLocInfo() == CCValAssign::ZExt) {
      MFI.setObjectZExt(FI, true);
    } else if (VA.getLocInfo() == CCValAssign::SExt) {
      MFI.setObjectSExt(FI, true);
    }

    // TODO Interrupt handlers
    // Adjust SP offset of interrupt parameter.

    SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    SDValue Val = DAG.getLoad(
        ValVT, DL, Chain, FIN,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI));
    return VA.isExtInLoc() ? DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val)
                           : Val;
  }
}

SDValue M68kTargetLowering::LowerMemOpCallTo(SDValue Chain, SDValue StackPtr,
                                             SDValue Arg, const SDLoc &DL,
                                             SelectionDAG &DAG,
                                             const CCValAssign &VA,
                                             ISD::ArgFlagsTy Flags) const {
  unsigned LocMemOffset = VA.getLocMemOffset();
  SDValue PtrOff = DAG.getIntPtrConstant(LocMemOffset, DL);
  PtrOff = DAG.getNode(ISD::ADD, DL, getPointerTy(DAG.getDataLayout()),
                       StackPtr, PtrOff);
  if (Flags.isByVal())
    return CreateCopyOfByValArgument(Arg, PtrOff, Chain, Flags, DAG, DL);

  return DAG.getStore(
      Chain, DL, Arg, PtrOff,
      MachinePointerInfo::getStack(DAG.getMachineFunction(), LocMemOffset));
}

//===----------------------------------------------------------------------===//
//                                   Call
//===----------------------------------------------------------------------===//

SDValue M68kTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool &IsTailCall = CLI.IsTailCall;
  bool IsVarArg = CLI.IsVarArg;

  MachineFunction &MF = DAG.getMachineFunction();
  StructReturnType SR = callIsStructReturn(Outs);
  bool IsSibcall = false;
  M68kMachineFunctionInfo *MFI = MF.getInfo<M68kMachineFunctionInfo>();
  // const M68kRegisterInfo *TRI = Subtarget.getRegisterInfo();

  if (CallConv == CallingConv::M68k_INTR)
    report_fatal_error("M68k interrupts may not be called directly");

  auto Attr = MF.getFunction().getFnAttribute("disable-tail-calls");
  if (Attr.getValueAsBool())
    IsTailCall = false;

  // FIXME Add tailcalls support

  bool IsMustTail = CLI.CB && CLI.CB->isMustTailCall();
  if (IsMustTail) {
    // Force this to be a tail call.  The verifier rules are enough to ensure
    // that we can lower this successfully without moving the return address
    // around.
    IsTailCall = true;
  } else if (IsTailCall) {
    // Check if it's really possible to do a tail call.
    IsTailCall = IsEligibleForTailCallOptimization(
        Callee, CallConv, IsVarArg, SR != NotStructReturn,
        MF.getFunction().hasStructRetAttr(), CLI.RetTy, Outs, OutVals, Ins,
        DAG);

    // Sibcalls are automatically detected tailcalls which do not require
    // ABI changes.
    if (!MF.getTarget().Options.GuaranteedTailCallOpt && IsTailCall)
      IsSibcall = true;

    if (IsTailCall)
      ++NumTailCalls;
  }

  assert(!(IsVarArg && canGuaranteeTCO(CallConv)) &&
         "Var args not supported with calling convention fastcc");

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  SmallVector<Type *, 4> ArgTypes;
  for (const auto &Arg : CLI.getArgs())
    ArgTypes.emplace_back(Arg.Ty);
  M68kCCState CCInfo(ArgTypes, CallConv, IsVarArg, MF, ArgLocs,
                     *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_M68k);

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = CCInfo.getAlignedCallFrameSize();
  if (IsSibcall) {
    // This is a sibcall. The memory operands are available in caller's
    // own caller's stack.
    NumBytes = 0;
  } else if (MF.getTarget().Options.GuaranteedTailCallOpt &&
             canGuaranteeTCO(CallConv)) {
    NumBytes = GetAlignedArgumentStackSize(NumBytes, DAG);
  }

  int FPDiff = 0;
  if (IsTailCall && !IsSibcall && !IsMustTail) {
    // Lower arguments at fp - stackoffset + fpdiff.
    unsigned NumBytesCallerPushed = MFI->getBytesToPopOnReturn();

    FPDiff = NumBytesCallerPushed - NumBytes;

    // Set the delta of movement of the returnaddr stackslot.
    // But only set if delta is greater than previous delta.
    if (FPDiff < MFI->getTCReturnAddrDelta())
      MFI->setTCReturnAddrDelta(FPDiff);
  }

  unsigned NumBytesToPush = NumBytes;
  unsigned NumBytesToPop = NumBytes;

  // If we have an inalloca argument, all stack space has already been allocated
  // for us and be right at the top of the stack.  We don't support multiple
  // arguments passed in memory when using inalloca.
  if (!Outs.empty() && Outs.back().Flags.isInAlloca()) {
    NumBytesToPush = 0;
    if (!ArgLocs.back().isMemLoc())
      report_fatal_error("cannot use inalloca attribute on a register "
                         "parameter");
    if (ArgLocs.back().getLocMemOffset() != 0)
      report_fatal_error("any parameter with the inalloca attribute must be "
                         "the only memory argument");
  }

  if (!IsSibcall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytesToPush,
                                 NumBytes - NumBytesToPush, DL);

  SDValue RetFI;
  // Load return address for tail calls.
  if (IsTailCall && FPDiff)
    Chain = EmitTailCallLoadRetAddr(DAG, RetFI, Chain, IsTailCall, FPDiff, DL);

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;

  // Walk the register/memloc assignments, inserting copies/loads.  In the case
  // of tail call optimization arguments are handle later.
  const M68kRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    ISD::ArgFlagsTy Flags = Outs[i].Flags;

    // Skip inalloca arguments, they have already been written.
    if (Flags.isInAlloca())
      continue;

    CCValAssign &VA = ArgLocs[i];
    EVT RegVT = VA.getLocVT();
    SDValue Arg = OutVals[i];
    bool IsByVal = Flags.isByVal();

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default:
      llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, RegVT, Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, RegVT, Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, RegVT, Arg);
      break;
    case CCValAssign::BCvt:
      Arg = DAG.getBitcast(RegVT, Arg);
      break;
    case CCValAssign::Indirect: {
      // Store the argument.
      SDValue SpillSlot = DAG.CreateStackTemporary(VA.getValVT());
      int FI = cast<FrameIndexSDNode>(SpillSlot)->getIndex();
      Chain = DAG.getStore(
          Chain, DL, Arg, SpillSlot,
          MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI));
      Arg = SpillSlot;
      break;
    }
    }

    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else if (!IsSibcall && (!IsTailCall || IsByVal)) {
      assert(VA.isMemLoc());
      if (!StackPtr.getNode()) {
        StackPtr = DAG.getCopyFromReg(Chain, DL, RegInfo->getStackRegister(),
                                      getPointerTy(DAG.getDataLayout()));
      }
      MemOpChains.push_back(
          LowerMemOpCallTo(Chain, StackPtr, Arg, DL, DAG, VA, Flags));
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  // FIXME Make sure PIC style GOT works as expected
  // The only time GOT is really needed is for Medium-PIC static data
  // otherwise we are happy with pc-rel or static references

  if (IsVarArg && IsMustTail) {
    const auto &Forwards = MFI->getForwardedMustTailRegParms();
    for (const auto &F : Forwards) {
      SDValue Val = DAG.getCopyFromReg(Chain, DL, F.VReg, F.VT);
      RegsToPass.push_back(std::make_pair(unsigned(F.PReg), Val));
    }
  }

  // For tail calls lower the arguments to the 'real' stack slots.  Sibcalls
  // don't need this because the eligibility check rejects calls that require
  // shuffling arguments passed in memory.
  if (!IsSibcall && IsTailCall) {
    // Force all the incoming stack arguments to be loaded from the stack
    // before any new outgoing arguments are stored to the stack, because the
    // outgoing stack slots may alias the incoming argument stack slots, and
    // the alias isn't otherwise explicit. This is slightly more conservative
    // than necessary, because it means that each store effectively depends
    // on every argument instead of just those arguments it would clobber.
    SDValue ArgChain = DAG.getStackArgumentTokenFactor(Chain);

    SmallVector<SDValue, 8> MemOpChains2;
    SDValue FIN;
    int FI = 0;
    for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
      CCValAssign &VA = ArgLocs[i];
      if (VA.isRegLoc())
        continue;
      assert(VA.isMemLoc());
      SDValue Arg = OutVals[i];
      ISD::ArgFlagsTy Flags = Outs[i].Flags;
      // Skip inalloca arguments.  They don't require any work.
      if (Flags.isInAlloca())
        continue;
      // Create frame index.
      int32_t Offset = VA.getLocMemOffset() + FPDiff;
      uint32_t OpSize = (VA.getLocVT().getSizeInBits() + 7) / 8;
      FI = MF.getFrameInfo().CreateFixedObject(OpSize, Offset, true);
      FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));

      if (Flags.isByVal()) {
        // Copy relative to framepointer.
        SDValue Source = DAG.getIntPtrConstant(VA.getLocMemOffset(), DL);
        if (!StackPtr.getNode()) {
          StackPtr = DAG.getCopyFromReg(Chain, DL, RegInfo->getStackRegister(),
                                        getPointerTy(DAG.getDataLayout()));
        }
        Source = DAG.getNode(ISD::ADD, DL, getPointerTy(DAG.getDataLayout()),
                             StackPtr, Source);

        MemOpChains2.push_back(
            CreateCopyOfByValArgument(Source, FIN, ArgChain, Flags, DAG, DL));
      } else {
        // Store relative to framepointer.
        MemOpChains2.push_back(DAG.getStore(
            ArgChain, DL, Arg, FIN,
            MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI)));
      }
    }

    if (!MemOpChains2.empty())
      Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains2);

    // Store the return address to the appropriate stack slot.
    Chain = EmitTailCallStoreRetAddr(DAG, MF, Chain, RetFI,
                                     getPointerTy(DAG.getDataLayout()),
                                     Subtarget.getSlotSize(), FPDiff, DL);
  }

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into registers.
  SDValue InGlue;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, DL, RegsToPass[i].first,
                             RegsToPass[i].second, InGlue);
    InGlue = Chain.getValue(1);
  }

  if (Callee->getOpcode() == ISD::GlobalAddress) {
    // If the callee is a GlobalAddress node (quite common, every direct call
    // is) turn it into a TargetGlobalAddress node so that legalize doesn't hack
    // it.
    GlobalAddressSDNode *G = cast<GlobalAddressSDNode>(Callee);

    // We should use extra load for direct calls to dllimported functions in
    // non-JIT mode.
    const GlobalValue *GV = G->getGlobal();
    if (!GV->hasDLLImportStorageClass()) {
      unsigned char OpFlags = Subtarget.classifyGlobalFunctionReference(GV);

      Callee = DAG.getTargetGlobalAddress(
          GV, DL, getPointerTy(DAG.getDataLayout()), G->getOffset(), OpFlags);

      if (OpFlags == M68kII::MO_GOTPCREL) {

        // Add a wrapper.
        Callee = DAG.getNode(M68kISD::WrapperPC, DL,
                             getPointerTy(DAG.getDataLayout()), Callee);

        // Add extra indirection
        Callee = DAG.getLoad(
            getPointerTy(DAG.getDataLayout()), DL, DAG.getEntryNode(), Callee,
            MachinePointerInfo::getGOT(DAG.getMachineFunction()));
      }
    }
  } else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    const Module *Mod = DAG.getMachineFunction().getFunction().getParent();
    unsigned char OpFlags =
        Subtarget.classifyGlobalFunctionReference(nullptr, *Mod);

    Callee = DAG.getTargetExternalSymbol(
        S->getSymbol(), getPointerTy(DAG.getDataLayout()), OpFlags);
  }

  // Returns a chain & a flag for retval copy to use.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;

  if (!IsSibcall && IsTailCall) {
    Chain = DAG.getCALLSEQ_END(Chain, NumBytesToPop, 0, InGlue, DL);
    InGlue = Chain.getValue(1);
  }

  Ops.push_back(Chain);
  Ops.push_back(Callee);

  if (IsTailCall)
    Ops.push_back(DAG.getConstant(FPDiff, DL, MVT::i32));

  // Add argument registers to the end of the list so that they are known live
  // into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const uint32_t *Mask = RegInfo->getCallPreservedMask(MF, CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");

  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InGlue.getNode())
    Ops.push_back(InGlue);

  if (IsTailCall) {
    MF.getFrameInfo().setHasTailCall();
    return DAG.getNode(M68kISD::TC_RETURN, DL, NodeTys, Ops);
  }

  Chain = DAG.getNode(M68kISD::CALL, DL, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  unsigned NumBytesForCalleeToPop;
  if (M68k::isCalleePop(CallConv, IsVarArg,
                        DAG.getTarget().Options.GuaranteedTailCallOpt)) {
    NumBytesForCalleeToPop = NumBytes; // Callee pops everything
  } else if (!canGuaranteeTCO(CallConv) && SR == StackStructReturn) {
    // If this is a call to a struct-return function, the callee
    // pops the hidden struct pointer, so we have to push it back.
    NumBytesForCalleeToPop = 4;
  } else {
    NumBytesForCalleeToPop = 0; // Callee pops nothing.
  }

  if (CLI.DoesNotReturn && !getTargetMachine().Options.TrapUnreachable) {
    // No need to reset the stack after the call if the call doesn't return. To
    // make the MI verify, we'll pretend the callee does it for us.
    NumBytesForCalleeToPop = NumBytes;
  }

  // Returns a flag for retval copy to use.
  if (!IsSibcall) {
    Chain = DAG.getCALLSEQ_END(Chain, NumBytesToPop, NumBytesForCalleeToPop,
                               InGlue, DL);
    InGlue = Chain.getValue(1);
  }

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InGlue, CallConv, IsVarArg, Ins, DL, DAG,
                         InVals);
}

SDValue M68kTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, RetCC_M68k);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    EVT CopyVT = VA.getLocVT();

    /// ??? is this correct?
    Chain = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), CopyVT, InGlue)
                .getValue(1);
    SDValue Val = Chain.getValue(0);

    if (VA.isExtInLoc() && VA.getValVT().getScalarType() == MVT::i1)
      Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);

    InGlue = Chain.getValue(2);
    InVals.push_back(Val);
  }

  return Chain;
}

//===----------------------------------------------------------------------===//
//            Formal Arguments Calling Convention Implementation
//===----------------------------------------------------------------------===//

SDValue M68kTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CCID, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  M68kMachineFunctionInfo *MMFI = MF.getInfo<M68kMachineFunctionInfo>();
  // const TargetFrameLowering &TFL = *Subtarget.getFrameLowering();

  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  SmallVector<Type *, 4> ArgTypes;
  for (const Argument &Arg : MF.getFunction().args())
    ArgTypes.emplace_back(Arg.getType());
  M68kCCState CCInfo(ArgTypes, CCID, IsVarArg, MF, ArgLocs, *DAG.getContext());

  CCInfo.AnalyzeFormalArguments(Ins, CC_M68k);

  unsigned LastVal = ~0U;
  SDValue ArgValue;
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    assert(VA.getValNo() != LastVal && "Same value in different locations");
    (void)LastVal;

    LastVal = VA.getValNo();

    if (VA.isRegLoc()) {
      EVT RegVT = VA.getLocVT();
      const TargetRegisterClass *RC;
      if (RegVT == MVT::i32)
        RC = &M68k::XR32RegClass;
      else
        llvm_unreachable("Unknown argument type!");

      Register Reg = MF.addLiveIn(VA.getLocReg(), RC);
      ArgValue = DAG.getCopyFromReg(Chain, DL, Reg, RegVT);

      // If this is an 8 or 16-bit value, it is really passed promoted to 32
      // bits.  Insert an assert[sz]ext to capture this, then truncate to the
      // right size.
      if (VA.getLocInfo() == CCValAssign::SExt) {
        ArgValue = DAG.getNode(ISD::AssertSext, DL, RegVT, ArgValue,
                               DAG.getValueType(VA.getValVT()));
      } else if (VA.getLocInfo() == CCValAssign::ZExt) {
        ArgValue = DAG.getNode(ISD::AssertZext, DL, RegVT, ArgValue,
                               DAG.getValueType(VA.getValVT()));
      } else if (VA.getLocInfo() == CCValAssign::BCvt) {
        ArgValue = DAG.getBitcast(VA.getValVT(), ArgValue);
      }

      if (VA.isExtInLoc()) {
        ArgValue = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), ArgValue);
      }
    } else {
      assert(VA.isMemLoc());
      ArgValue = LowerMemArgument(Chain, CCID, Ins, DL, DAG, VA, MFI, i);
    }

    // If value is passed via pointer - do a load.
    // TODO Make sure this handling on indirect arguments is correct
    if (VA.getLocInfo() == CCValAssign::Indirect)
      ArgValue =
          DAG.getLoad(VA.getValVT(), DL, Chain, ArgValue, MachinePointerInfo());

    InVals.push_back(ArgValue);
  }

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    // Swift calling convention does not require we copy the sret argument
    // into %D0 for the return. We don't set SRetReturnReg for Swift.
    if (CCID == CallingConv::Swift)
      continue;

    // ABI require that for returning structs by value we copy the sret argument
    // into %D0 for the return. Save the argument into a virtual register so
    // that we can access it from the return points.
    if (Ins[i].Flags.isSRet()) {
      unsigned Reg = MMFI->getSRetReturnReg();
      if (!Reg) {
        MVT PtrTy = getPointerTy(DAG.getDataLayout());
        Reg = MF.getRegInfo().createVirtualRegister(getRegClassFor(PtrTy));
        MMFI->setSRetReturnReg(Reg);
      }
      SDValue Copy = DAG.getCopyToReg(DAG.getEntryNode(), DL, Reg, InVals[i]);
      Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Copy, Chain);
      break;
    }
  }

  unsigned StackSize = CCInfo.getStackSize();
  // Align stack specially for tail calls.
  if (shouldGuaranteeTCO(CCID, MF.getTarget().Options.GuaranteedTailCallOpt))
    StackSize = GetAlignedArgumentStackSize(StackSize, DAG);

  // If the function takes variable number of arguments, make a frame index for
  // the start of the first vararg value... for expansion of llvm.va_start. We
  // can skip this if there are no va_start calls.
  if (MFI.hasVAStart()) {
    MMFI->setVarArgsFrameIndex(MFI.CreateFixedObject(1, StackSize, true));
  }

  if (IsVarArg && MFI.hasMustTailInVarArgFunc()) {
    // We forward some GPRs and some vector types.
    SmallVector<MVT, 2> RegParmTypes;
    MVT IntVT = MVT::i32;
    RegParmTypes.push_back(IntVT);

    // Compute the set of forwarded registers. The rest are scratch.
    // ??? what is this for?
    SmallVectorImpl<ForwardedRegister> &Forwards =
        MMFI->getForwardedMustTailRegParms();
    CCInfo.analyzeMustTailForwardedRegisters(Forwards, RegParmTypes, CC_M68k);

    // Copy all forwards from physical to virtual registers.
    for (ForwardedRegister &F : Forwards) {
      // FIXME Can we use a less constrained schedule?
      SDValue RegVal = DAG.getCopyFromReg(Chain, DL, F.VReg, F.VT);
      F.VReg = MF.getRegInfo().createVirtualRegister(getRegClassFor(F.VT));
      Chain = DAG.getCopyToReg(Chain, DL, F.VReg, RegVal);
    }
  }

  // Some CCs need callee pop.
  if (M68k::isCalleePop(CCID, IsVarArg,
                        MF.getTarget().Options.GuaranteedTailCallOpt)) {
    MMFI->setBytesToPopOnReturn(StackSize); // Callee pops everything.
  } else {
    MMFI->setBytesToPopOnReturn(0); // Callee pops nothing.
    // If this is an sret function, the return should pop the hidden pointer.
    if (!canGuaranteeTCO(CCID) && argsAreStructReturn(Ins) == StackStructReturn)
      MMFI->setBytesToPopOnReturn(4);
  }

  MMFI->setArgumentStackSize(StackSize);

  return Chain;
}

//===----------------------------------------------------------------------===//
//              Return Value Calling Convention Implementation
//===----------------------------------------------------------------------===//

bool M68kTargetLowering::CanLowerReturn(
    CallingConv::ID CCID, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CCID, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_M68k);
}

SDValue
M68kTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CCID,
                                bool IsVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  M68kMachineFunctionInfo *MFI = MF.getInfo<M68kMachineFunctionInfo>();

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CCID, IsVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_M68k);

  SDValue Glue;
  SmallVector<SDValue, 6> RetOps;
  // Operand #0 = Chain (updated below)
  RetOps.push_back(Chain);
  // Operand #1 = Bytes To Pop
  RetOps.push_back(
      DAG.getTargetConstant(MFI->getBytesToPopOnReturn(), DL, MVT::i32));

  // Copy the result values into the output registers.
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");
    SDValue ValToCopy = OutVals[i];
    EVT ValVT = ValToCopy.getValueType();

    // Promote values to the appropriate types.
    if (VA.getLocInfo() == CCValAssign::SExt)
      ValToCopy = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), ValToCopy);
    else if (VA.getLocInfo() == CCValAssign::ZExt)
      ValToCopy = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), ValToCopy);
    else if (VA.getLocInfo() == CCValAssign::AExt) {
      if (ValVT.isVector() && ValVT.getVectorElementType() == MVT::i1)
        ValToCopy = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), ValToCopy);
      else
        ValToCopy = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), ValToCopy);
    } else if (VA.getLocInfo() == CCValAssign::BCvt)
      ValToCopy = DAG.getBitcast(VA.getLocVT(), ValToCopy);

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), ValToCopy, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  // Swift calling convention does not require we copy the sret argument
  // into %d0 for the return, and SRetReturnReg is not set for Swift.

  // ABI require that for returning structs by value we copy the sret argument
  // into %D0 for the return. Save the argument into a virtual register so that
  // we can access it from the return points.
  //
  // Checking Function.hasStructRetAttr() here is insufficient because the IR
  // may not have an explicit sret argument. If MFI.CanLowerReturn is
  // false, then an sret argument may be implicitly inserted in the SelDAG. In
  // either case MFI->setSRetReturnReg() will have been called.
  if (unsigned SRetReg = MFI->getSRetReturnReg()) {
    // ??? Can i just move this to the top and escape this explanation?
    // When we have both sret and another return value, we should use the
    // original Chain stored in RetOps[0], instead of the current Chain updated
    // in the above loop. If we only have sret, RetOps[0] equals to Chain.

    // For the case of sret and another return value, we have
    //   Chain_0 at the function entry
    //   Chain_1 = getCopyToReg(Chain_0) in the above loop
    // If we use Chain_1 in getCopyFromReg, we will have
    //   Val = getCopyFromReg(Chain_1)
    //   Chain_2 = getCopyToReg(Chain_1, Val) from below

    // getCopyToReg(Chain_0) will be glued together with
    // getCopyToReg(Chain_1, Val) into Unit A, getCopyFromReg(Chain_1) will be
    // in Unit B, and we will have cyclic dependency between Unit A and Unit B:
    //   Data dependency from Unit B to Unit A due to usage of Val in
    //     getCopyToReg(Chain_1, Val)
    //   Chain dependency from Unit A to Unit B

    // So here, we use RetOps[0] (i.e Chain_0) for getCopyFromReg.
    SDValue Val = DAG.getCopyFromReg(RetOps[0], DL, SRetReg,
                                     getPointerTy(MF.getDataLayout()));

    // ??? How will this work if CC does not use registers for args passing?
    // ??? What if I return multiple structs?
    unsigned RetValReg = M68k::D0;
    Chain = DAG.getCopyToReg(Chain, DL, RetValReg, Val, Glue);
    Glue = Chain.getValue(1);

    RetOps.push_back(
        DAG.getRegister(RetValReg, getPointerTy(DAG.getDataLayout())));
  }

  RetOps[0] = Chain; // Update chain.

  // Add the glue if we have it.
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(M68kISD::RET, DL, MVT::Other, RetOps);
}

//===----------------------------------------------------------------------===//
//                Fast Calling Convention (tail call) implementation
//===----------------------------------------------------------------------===//

//  Like std call, callee cleans arguments, convention except that ECX is
//  reserved for storing the tail called function address. Only 2 registers are
//  free for argument passing (inreg). Tail call optimization is performed
//  provided:
//                * tailcallopt is enabled
//                * caller/callee are fastcc
//  On M68k_64 architecture with GOT-style position independent code only
//  local (within module) calls are supported at the moment. To keep the stack
//  aligned according to platform abi the function GetAlignedArgumentStackSize
//  ensures that argument delta is always multiples of stack alignment. (Dynamic
//  linkers need this - darwin's dyld for example) If a tail called function
//  callee has more arguments than the caller the caller needs to make sure that
//  there is room to move the RETADDR to. This is achieved by reserving an area
//  the size of the argument delta right after the original RETADDR, but before
//  the saved framepointer or the spilled registers e.g. caller(arg1, arg2)
//  calls callee(arg1, arg2,arg3,arg4) stack layout:
//    arg1
//    arg2
//    RETADDR
//    [ new RETADDR
//      move area ]
//    (possible EBP)
//    ESI
//    EDI
//    local1 ..

/// Make the stack size align e.g 16n + 12 aligned for a 16-byte align
/// requirement.
unsigned
M68kTargetLowering::GetAlignedArgumentStackSize(unsigned StackSize,
                                                SelectionDAG &DAG) const {
  const TargetFrameLowering &TFI = *Subtarget.getFrameLowering();
  unsigned StackAlignment = TFI.getStackAlignment();
  uint64_t AlignMask = StackAlignment - 1;
  int64_t Offset = StackSize;
  unsigned SlotSize = Subtarget.getSlotSize();
  if ((Offset & AlignMask) <= (StackAlignment - SlotSize)) {
    // Number smaller than 12 so just add the difference.
    Offset += ((StackAlignment - SlotSize) - (Offset & AlignMask));
  } else {
    // Mask out lower bits, add stackalignment once plus the 12 bytes.
    Offset =
        ((~AlignMask) & Offset) + StackAlignment + (StackAlignment - SlotSize);
  }
  return Offset;
}

/// Check whether the call is eligible for tail call optimization. Targets
/// that want to do tail call optimization should implement this function.
bool M68kTargetLowering::IsEligibleForTailCallOptimization(
    SDValue Callee, CallingConv::ID CalleeCC, bool IsVarArg,
    bool IsCalleeStructRet, bool IsCallerStructRet, Type *RetTy,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, SelectionDAG &DAG) const {
  if (!mayTailCallThisCC(CalleeCC))
    return false;

  // If -tailcallopt is specified, make fastcc functions tail-callable.
  MachineFunction &MF = DAG.getMachineFunction();
  const auto &CallerF = MF.getFunction();

  CallingConv::ID CallerCC = CallerF.getCallingConv();
  bool CCMatch = CallerCC == CalleeCC;

  if (DAG.getTarget().Options.GuaranteedTailCallOpt) {
    if (canGuaranteeTCO(CalleeCC) && CCMatch)
      return true;
    return false;
  }

  // Look for obvious safe cases to perform tail call optimization that do not
  // require ABI changes. This is what gcc calls sibcall.

  // Can't do sibcall if stack needs to be dynamically re-aligned. PEI needs to
  // emit a special epilogue.
  const M68kRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  if (RegInfo->hasStackRealignment(MF))
    return false;

  // Also avoid sibcall optimization if either caller or callee uses struct
  // return semantics.
  if (IsCalleeStructRet || IsCallerStructRet)
    return false;

  // Do not sibcall optimize vararg calls unless all arguments are passed via
  // registers.
  LLVMContext &C = *DAG.getContext();
  if (IsVarArg && !Outs.empty()) {

    SmallVector<CCValAssign, 16> ArgLocs;
    CCState CCInfo(CalleeCC, IsVarArg, MF, ArgLocs, C);

    CCInfo.AnalyzeCallOperands(Outs, CC_M68k);
    for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i)
      if (!ArgLocs[i].isRegLoc())
        return false;
  }

  // Check that the call results are passed in the same way.
  if (!CCState::resultsCompatible(CalleeCC, CallerCC, MF, C, Ins, RetCC_M68k,
                                  RetCC_M68k))
    return false;

  // The callee has to preserve all registers the caller needs to preserve.
  const M68kRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *CallerPreserved = TRI->getCallPreservedMask(MF, CallerCC);
  if (!CCMatch) {
    const uint32_t *CalleePreserved = TRI->getCallPreservedMask(MF, CalleeCC);
    if (!TRI->regmaskSubsetEqual(CallerPreserved, CalleePreserved))
      return false;
  }

  unsigned StackArgsSize = 0;

  // If the callee takes no arguments then go on to check the results of the
  // call.
  if (!Outs.empty()) {
    // Check if stack adjustment is needed. For now, do not do this if any
    // argument is passed on the stack.
    SmallVector<CCValAssign, 16> ArgLocs;
    CCState CCInfo(CalleeCC, IsVarArg, MF, ArgLocs, C);

    CCInfo.AnalyzeCallOperands(Outs, CC_M68k);
    StackArgsSize = CCInfo.getStackSize();

    if (StackArgsSize) {
      // Check if the arguments are already laid out in the right way as
      // the caller's fixed stack objects.
      MachineFrameInfo &MFI = MF.getFrameInfo();
      const MachineRegisterInfo *MRI = &MF.getRegInfo();
      const M68kInstrInfo *TII = Subtarget.getInstrInfo();
      for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
        CCValAssign &VA = ArgLocs[i];
        SDValue Arg = OutVals[i];
        ISD::ArgFlagsTy Flags = Outs[i].Flags;
        if (VA.getLocInfo() == CCValAssign::Indirect)
          return false;
        if (!VA.isRegLoc()) {
          if (!MatchingStackOffset(Arg, VA.getLocMemOffset(), Flags, MFI, MRI,
                                   TII, VA))
            return false;
        }
      }
    }

    bool PositionIndependent = isPositionIndependent();
    // If the tailcall address may be in a register, then make sure it's
    // possible to register allocate for it. The call address can
    // only target %A0 or %A1 since the tail call must be scheduled after
    // callee-saved registers are restored. These happen to be the same
    // registers used to pass 'inreg' arguments so watch out for those.
    if ((!isa<GlobalAddressSDNode>(Callee) &&
         !isa<ExternalSymbolSDNode>(Callee)) ||
        PositionIndependent) {
      unsigned NumInRegs = 0;
      // In PIC we need an extra register to formulate the address computation
      // for the callee.
      unsigned MaxInRegs = PositionIndependent ? 1 : 2;

      for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
        CCValAssign &VA = ArgLocs[i];
        if (!VA.isRegLoc())
          continue;
        Register Reg = VA.getLocReg();
        switch (Reg) {
        default:
          break;
        case M68k::A0:
        case M68k::A1:
          if (++NumInRegs == MaxInRegs)
            return false;
          break;
        }
      }
    }

    const MachineRegisterInfo &MRI = MF.getRegInfo();
    if (!parametersInCSRMatch(MRI, CallerPreserved, ArgLocs, OutVals))
      return false;
  }

  bool CalleeWillPop = M68k::isCalleePop(
      CalleeCC, IsVarArg, MF.getTarget().Options.GuaranteedTailCallOpt);

  if (unsigned BytesToPop =
          MF.getInfo<M68kMachineFunctionInfo>()->getBytesToPopOnReturn()) {
    // If we have bytes to pop, the callee must pop them.
    bool CalleePopMatches = CalleeWillPop && BytesToPop == StackArgsSize;
    if (!CalleePopMatches)
      return false;
  } else if (CalleeWillPop && StackArgsSize > 0) {
    // If we don't have bytes to pop, make sure the callee doesn't pop any.
    return false;
  }

  return true;
}

//===----------------------------------------------------------------------===//
// Custom Lower
//===----------------------------------------------------------------------===//

SDValue M68kTargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Should not custom lower this!");
  case ISD::SADDO:
  case ISD::UADDO:
  case ISD::SSUBO:
  case ISD::USUBO:
  case ISD::SMULO:
  case ISD::UMULO:
    return LowerXALUO(Op, DAG);
  case ISD::SETCC:
    return LowerSETCC(Op, DAG);
  case ISD::SETCCCARRY:
    return LowerSETCCCARRY(Op, DAG);
  case ISD::SELECT:
    return LowerSELECT(Op, DAG);
  case ISD::BRCOND:
    return LowerBRCOND(Op, DAG);
  case ISD::ADDC:
  case ISD::ADDE:
  case ISD::SUBC:
  case ISD::SUBE:
    return LowerADDC_ADDE_SUBC_SUBE(Op, DAG);
  case ISD::ConstantPool:
    return LowerConstantPool(Op, DAG);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return LowerExternalSymbol(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::JumpTable:
    return LowerJumpTable(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    return LowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::SHL_PARTS:
    return LowerShiftLeftParts(Op, DAG);
  case ISD::SRA_PARTS:
    return LowerShiftRightParts(Op, DAG, true);
  case ISD::SRL_PARTS:
    return LowerShiftRightParts(Op, DAG, false);
  case ISD::ATOMIC_FENCE:
    return LowerATOMICFENCE(Op, DAG);
  case ISD::GlobalTLSAddress:
    return LowerGlobalTLSAddress(Op, DAG);
  }
}

SDValue M68kTargetLowering::LowerExternalSymbolCall(SelectionDAG &DAG,
                                                    SDLoc Loc,
                                                    llvm::StringRef SymbolName,
                                                    ArgListTy &&ArgList) const {
  PointerType *PtrTy = PointerType::get(*DAG.getContext(), 0);
  CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(Loc)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, PtrTy,
                    DAG.getExternalSymbol(SymbolName.data(),
                                          getPointerMemTy(DAG.getDataLayout())),
                    std::move(ArgList));
  return LowerCallTo(CLI).first;
}

SDValue M68kTargetLowering::getTLSGetAddr(GlobalAddressSDNode *GA,
                                          SelectionDAG &DAG,
                                          unsigned TargetFlags) const {
  SDValue GOT = DAG.getGLOBAL_OFFSET_TABLE(MVT::i32);
  SDValue TGA = DAG.getTargetGlobalAddress(
      GA->getGlobal(), GA, GA->getValueType(0), GA->getOffset(), TargetFlags);
  SDValue Arg = DAG.getNode(ISD::ADD, SDLoc(GA), MVT::i32, GOT, TGA);

  PointerType *PtrTy = PointerType::get(*DAG.getContext(), 0);

  ArgListTy Args;
  ArgListEntry Entry;
  Entry.Node = Arg;
  Entry.Ty = PtrTy;
  Args.push_back(Entry);
  return LowerExternalSymbolCall(DAG, SDLoc(GA), "__tls_get_addr",
                                 std::move(Args));
}

SDValue M68kTargetLowering::getM68kReadTp(SDLoc Loc, SelectionDAG &DAG) const {
  return LowerExternalSymbolCall(DAG, Loc, "__m68k_read_tp", ArgListTy());
}

SDValue M68kTargetLowering::LowerTLSGeneralDynamic(GlobalAddressSDNode *GA,
                                                   SelectionDAG &DAG) const {
  return getTLSGetAddr(GA, DAG, M68kII::MO_TLSGD);
}

SDValue M68kTargetLowering::LowerTLSLocalDynamic(GlobalAddressSDNode *GA,
                                                 SelectionDAG &DAG) const {
  SDValue Addr = getTLSGetAddr(GA, DAG, M68kII::MO_TLSLDM);
  SDValue TGA =
      DAG.getTargetGlobalAddress(GA->getGlobal(), GA, GA->getValueType(0),
                                 GA->getOffset(), M68kII::MO_TLSLD);
  return DAG.getNode(ISD::ADD, SDLoc(GA), MVT::i32, TGA, Addr);
}

SDValue M68kTargetLowering::LowerTLSInitialExec(GlobalAddressSDNode *GA,
                                                SelectionDAG &DAG) const {
  SDValue GOT = DAG.getGLOBAL_OFFSET_TABLE(MVT::i32);
  SDValue Tp = getM68kReadTp(SDLoc(GA), DAG);
  SDValue TGA =
      DAG.getTargetGlobalAddress(GA->getGlobal(), GA, GA->getValueType(0),
                                 GA->getOffset(), M68kII::MO_TLSIE);
  SDValue Addr = DAG.getNode(ISD::ADD, SDLoc(GA), MVT::i32, TGA, GOT);
  SDValue Offset =
      DAG.getLoad(MVT::i32, SDLoc(GA), DAG.getEntryNode(), Addr,
                  MachinePointerInfo::getGOT(DAG.getMachineFunction()));

  return DAG.getNode(ISD::ADD, SDLoc(GA), MVT::i32, Offset, Tp);
}

SDValue M68kTargetLowering::LowerTLSLocalExec(GlobalAddressSDNode *GA,
                                              SelectionDAG &DAG) const {
  SDValue Tp = getM68kReadTp(SDLoc(GA), DAG);
  SDValue TGA =
      DAG.getTargetGlobalAddress(GA->getGlobal(), GA, GA->getValueType(0),
                                 GA->getOffset(), M68kII::MO_TLSLE);
  return DAG.getNode(ISD::ADD, SDLoc(GA), MVT::i32, TGA, Tp);
}

SDValue M68kTargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  assert(Subtarget.isTargetELF());

  auto *GA = cast<GlobalAddressSDNode>(Op);
  TLSModel::Model AccessModel = DAG.getTarget().getTLSModel(GA->getGlobal());

  switch (AccessModel) {
  case TLSModel::GeneralDynamic:
    return LowerTLSGeneralDynamic(GA, DAG);
  case TLSModel::LocalDynamic:
    return LowerTLSLocalDynamic(GA, DAG);
  case TLSModel::InitialExec:
    return LowerTLSInitialExec(GA, DAG);
  case TLSModel::LocalExec:
    return LowerTLSLocalExec(GA, DAG);
  }

  llvm_unreachable("Unexpected TLS access model type");
}

bool M68kTargetLowering::decomposeMulByConstant(LLVMContext &Context, EVT VT,
                                                SDValue C) const {
  // Shifts and add instructions in M68000 and M68010 support
  // up to 32 bits, but mul only has 16-bit variant. So it's almost
  // certainly beneficial to lower 8/16/32-bit mul to their
  // add / shifts counterparts. But for 64-bits mul, it might be
  // safer to just leave it to compiler runtime implementations.
  return VT.bitsLE(MVT::i32) || Subtarget.atLeastM68020();
}

static bool isOverflowArithmetic(unsigned Opcode) {
  switch (Opcode) {
  case ISD::UADDO:
  case ISD::SADDO:
  case ISD::USUBO:
  case ISD::SSUBO:
  case ISD::UMULO:
  case ISD::SMULO:
    return true;
  default:
    return false;
  }
}

static void lowerOverflowArithmetic(SDValue Op, SelectionDAG &DAG,
                                    SDValue &Result, SDValue &CCR,
                                    unsigned &CC) {
  SDNode *N = Op.getNode();
  EVT VT = N->getValueType(0);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDLoc DL(Op);

  unsigned TruncOp = 0;
  auto PromoteMULO = [&](unsigned ExtOp) {
    // We don't have 8-bit multiplications, so promote i8 version of U/SMULO
    // to i16.
    // Ideally this should be done by legalizer but sadly there is no promotion
    // rule for U/SMULO at this moment.
    if (VT == MVT::i8) {
      LHS = DAG.getNode(ExtOp, DL, MVT::i16, LHS);
      RHS = DAG.getNode(ExtOp, DL, MVT::i16, RHS);
      VT = MVT::i16;
      TruncOp = ISD::TRUNCATE;
    }
  };

  bool NoOverflow = false;
  unsigned BaseOp = 0;
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Unknown ovf instruction!");
  case ISD::SADDO:
    BaseOp = M68kISD::ADD;
    CC = M68k::COND_VS;
    break;
  case ISD::UADDO:
    BaseOp = M68kISD::ADD;
    CC = M68k::COND_CS;
    break;
  case ISD::SSUBO:
    BaseOp = M68kISD::SUB;
    CC = M68k::COND_VS;
    break;
  case ISD::USUBO:
    BaseOp = M68kISD::SUB;
    CC = M68k::COND_CS;
    break;
  case ISD::UMULO:
    PromoteMULO(ISD::ZERO_EXTEND);
    NoOverflow = VT != MVT::i32;
    BaseOp = NoOverflow ? ISD::MUL : M68kISD::UMUL;
    CC = M68k::COND_VS;
    break;
  case ISD::SMULO:
    PromoteMULO(ISD::SIGN_EXTEND);
    NoOverflow = VT != MVT::i32;
    BaseOp = NoOverflow ? ISD::MUL : M68kISD::SMUL;
    CC = M68k::COND_VS;
    break;
  }

  SDVTList VTs;
  if (NoOverflow)
    VTs = DAG.getVTList(VT);
  else
    // Also sets CCR.
    VTs = DAG.getVTList(VT, MVT::i8);

  SDValue Arith = DAG.getNode(BaseOp, DL, VTs, LHS, RHS);
  Result = Arith.getValue(0);
  if (TruncOp)
    // Right now the only place to truncate is from i16 to i8.
    Result = DAG.getNode(TruncOp, DL, MVT::i8, Arith);

  if (NoOverflow)
    CCR = DAG.getConstant(0, DL, N->getValueType(1));
  else
    CCR = Arith.getValue(1);
}

SDValue M68kTargetLowering::LowerXALUO(SDValue Op, SelectionDAG &DAG) const {
  SDNode *N = Op.getNode();
  SDLoc DL(Op);

  // Lower the "add/sub/mul with overflow" instruction into a regular ins plus
  // a "setcc" instruction that checks the overflow flag.
  SDValue Result, CCR;
  unsigned CC;
  lowerOverflowArithmetic(Op, DAG, Result, CCR, CC);

  SDValue Overflow;
  if (isa<ConstantSDNode>(CCR)) {
    // It's likely a result of operations that will not overflow
    // hence no setcc is needed.
    Overflow = CCR;
  } else {
    // Generate a M68kISD::SETCC.
    Overflow = DAG.getNode(M68kISD::SETCC, DL, N->getValueType(1),
                           DAG.getConstant(CC, DL, MVT::i8), CCR);
  }

  return DAG.getNode(ISD::MERGE_VALUES, DL, N->getVTList(), Result, Overflow);
}

/// Create a BTST (Bit Test) node - Test bit \p BitNo in \p Src and set
/// condition according to equal/not-equal condition code \p CC.
static SDValue getBitTestCondition(SDValue Src, SDValue BitNo, ISD::CondCode CC,
                                   const SDLoc &DL, SelectionDAG &DAG) {
  // If Src is i8, promote it to i32 with any_extend.  There is no i8 BTST
  // instruction.  Since the shift amount is in-range-or-undefined, we know
  // that doing a bittest on the i32 value is ok.
  if (Src.getValueType() == MVT::i8 || Src.getValueType() == MVT::i16)
    Src = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i32, Src);

  // If the operand types disagree, extend the shift amount to match.  Since
  // BTST ignores high bits (like shifts) we can use anyextend.
  if (Src.getValueType() != BitNo.getValueType())
    BitNo = DAG.getNode(ISD::ANY_EXTEND, DL, Src.getValueType(), BitNo);

  SDValue BTST = DAG.getNode(M68kISD::BTST, DL, MVT::i32, Src, BitNo);

  // NOTE BTST sets CCR.Z flag
  M68k::CondCode Cond = CC == ISD::SETEQ ? M68k::COND_NE : M68k::COND_EQ;
  return DAG.getNode(M68kISD::SETCC, DL, MVT::i8,
                     DAG.getConstant(Cond, DL, MVT::i8), BTST);
}

/// Result of 'and' is compared against zero. Change to a BTST node if possible.
static SDValue LowerAndToBTST(SDValue And, ISD::CondCode CC, const SDLoc &DL,
                              SelectionDAG &DAG) {
  SDValue Op0 = And.getOperand(0);
  SDValue Op1 = And.getOperand(1);
  if (Op0.getOpcode() == ISD::TRUNCATE)
    Op0 = Op0.getOperand(0);
  if (Op1.getOpcode() == ISD::TRUNCATE)
    Op1 = Op1.getOperand(0);

  SDValue LHS, RHS;
  if (Op1.getOpcode() == ISD::SHL)
    std::swap(Op0, Op1);
  if (Op0.getOpcode() == ISD::SHL) {
    if (isOneConstant(Op0.getOperand(0))) {
      // If we looked past a truncate, check that it's only truncating away
      // known zeros.
      unsigned BitWidth = Op0.getValueSizeInBits();
      unsigned AndBitWidth = And.getValueSizeInBits();
      if (BitWidth > AndBitWidth) {
        auto Known = DAG.computeKnownBits(Op0);
        if (Known.countMinLeadingZeros() < BitWidth - AndBitWidth)
          return SDValue();
      }
      LHS = Op1;
      RHS = Op0.getOperand(1);
    }
  } else if (auto *AndRHS = dyn_cast<ConstantSDNode>(Op1)) {
    uint64_t AndRHSVal = AndRHS->getZExtValue();
    SDValue AndLHS = Op0;

    if (AndRHSVal == 1 && AndLHS.getOpcode() == ISD::SRL) {
      LHS = AndLHS.getOperand(0);
      RHS = AndLHS.getOperand(1);
    }

    // Use BTST if the immediate can't be encoded in a TEST instruction.
    if (!isUInt<32>(AndRHSVal) && isPowerOf2_64(AndRHSVal)) {
      LHS = AndLHS;
      RHS = DAG.getConstant(Log2_64_Ceil(AndRHSVal), DL, LHS.getValueType());
    }
  }

  if (LHS.getNode())
    return getBitTestCondition(LHS, RHS, CC, DL, DAG);

  return SDValue();
}

static M68k::CondCode TranslateIntegerM68kCC(ISD::CondCode SetCCOpcode) {
  switch (SetCCOpcode) {
  default:
    llvm_unreachable("Invalid integer condition!");
  case ISD::SETEQ:
    return M68k::COND_EQ;
  case ISD::SETGT:
    return M68k::COND_GT;
  case ISD::SETGE:
    return M68k::COND_GE;
  case ISD::SETLT:
    return M68k::COND_LT;
  case ISD::SETLE:
    return M68k::COND_LE;
  case ISD::SETNE:
    return M68k::COND_NE;
  case ISD::SETULT:
    return M68k::COND_CS;
  case ISD::SETUGE:
    return M68k::COND_CC;
  case ISD::SETUGT:
    return M68k::COND_HI;
  case ISD::SETULE:
    return M68k::COND_LS;
  }
}

/// Do a one-to-one translation of a ISD::CondCode to the M68k-specific
/// condition code, returning the condition code and the LHS/RHS of the
/// comparison to make.
static unsigned TranslateM68kCC(ISD::CondCode SetCCOpcode, const SDLoc &DL,
                                bool IsFP, SDValue &LHS, SDValue &RHS,
                                SelectionDAG &DAG) {
  if (!IsFP) {
    if (ConstantSDNode *RHSC = dyn_cast<ConstantSDNode>(RHS)) {
      if (SetCCOpcode == ISD::SETGT && RHSC->isAllOnes()) {
        // X > -1   -> X == 0, jump !sign.
        RHS = DAG.getConstant(0, DL, RHS.getValueType());
        return M68k::COND_PL;
      }
      if (SetCCOpcode == ISD::SETLT && RHSC->isZero()) {
        // X < 0   -> X == 0, jump on sign.
        return M68k::COND_MI;
      }
      if (SetCCOpcode == ISD::SETLT && RHSC->getZExtValue() == 1) {
        // X < 1   -> X <= 0
        RHS = DAG.getConstant(0, DL, RHS.getValueType());
        return M68k::COND_LE;
      }
    }

    return TranslateIntegerM68kCC(SetCCOpcode);
  }

  // First determine if it is required or is profitable to flip the operands.

  // If LHS is a foldable load, but RHS is not, flip the condition.
  if (ISD::isNON_EXTLoad(LHS.getNode()) && !ISD::isNON_EXTLoad(RHS.getNode())) {
    SetCCOpcode = getSetCCSwappedOperands(SetCCOpcode);
    std::swap(LHS, RHS);
  }

  switch (SetCCOpcode) {
  default:
    break;
  case ISD::SETOLT:
  case ISD::SETOLE:
  case ISD::SETUGT:
  case ISD::SETUGE:
    std::swap(LHS, RHS);
    break;
  }

  // On a floating point condition, the flags are set as follows:
  // ZF  PF  CF   op
  //  0 | 0 | 0 | X > Y
  //  0 | 0 | 1 | X < Y
  //  1 | 0 | 0 | X == Y
  //  1 | 1 | 1 | unordered
  switch (SetCCOpcode) {
  default:
    llvm_unreachable("Condcode should be pre-legalized away");
  case ISD::SETUEQ:
  case ISD::SETEQ:
    return M68k::COND_EQ;
  case ISD::SETOLT: // flipped
  case ISD::SETOGT:
  case ISD::SETGT:
    return M68k::COND_HI;
  case ISD::SETOLE: // flipped
  case ISD::SETOGE:
  case ISD::SETGE:
    return M68k::COND_CC;
  case ISD::SETUGT: // flipped
  case ISD::SETULT:
  case ISD::SETLT:
    return M68k::COND_CS;
  case ISD::SETUGE: // flipped
  case ISD::SETULE:
  case ISD::SETLE:
    return M68k::COND_LS;
  case ISD::SETONE:
  case ISD::SETNE:
    return M68k::COND_NE;
  case ISD::SETOEQ:
  case ISD::SETUNE:
    return M68k::COND_INVALID;
  }
}

// Convert (truncate (srl X, N) to i1) to (bt X, N)
static SDValue LowerTruncateToBTST(SDValue Op, ISD::CondCode CC,
                                   const SDLoc &DL, SelectionDAG &DAG) {

  assert(Op.getOpcode() == ISD::TRUNCATE && Op.getValueType() == MVT::i1 &&
         "Expected TRUNCATE to i1 node");

  if (Op.getOperand(0).getOpcode() != ISD::SRL)
    return SDValue();

  SDValue ShiftRight = Op.getOperand(0);
  return getBitTestCondition(ShiftRight.getOperand(0), ShiftRight.getOperand(1),
                             CC, DL, DAG);
}

/// \brief return true if \c Op has a use that doesn't just read flags.
static bool hasNonFlagsUse(SDValue Op) {
  for (SDNode::use_iterator UI = Op->use_begin(), UE = Op->use_end(); UI != UE;
       ++UI) {
    SDNode *User = *UI;
    unsigned UOpNo = UI.getOperandNo();
    if (User->getOpcode() == ISD::TRUNCATE && User->hasOneUse()) {
      // Look pass truncate.
      UOpNo = User->use_begin().getOperandNo();
      User = *User->use_begin();
    }

    if (User->getOpcode() != ISD::BRCOND && User->getOpcode() != ISD::SETCC &&
        !(User->getOpcode() == ISD::SELECT && UOpNo == 0))
      return true;
  }
  return false;
}

SDValue M68kTargetLowering::EmitTest(SDValue Op, unsigned M68kCC,
                                     const SDLoc &DL, SelectionDAG &DAG) const {

  // CF and OF aren't always set the way we want. Determine which
  // of these we need.
  bool NeedCF = false;
  bool NeedOF = false;
  switch (M68kCC) {
  default:
    break;
  case M68k::COND_HI:
  case M68k::COND_CC:
  case M68k::COND_CS:
  case M68k::COND_LS:
    NeedCF = true;
    break;
  case M68k::COND_GT:
  case M68k::COND_GE:
  case M68k::COND_LT:
  case M68k::COND_LE:
  case M68k::COND_VS:
  case M68k::COND_VC: {
    // Check if we really need to set the
    // Overflow flag. If NoSignedWrap is present
    // that is not actually needed.
    switch (Op->getOpcode()) {
    case ISD::ADD:
    case ISD::SUB:
    case ISD::MUL:
    case ISD::SHL: {
      if (Op.getNode()->getFlags().hasNoSignedWrap())
        break;
      [[fallthrough]];
    }
    default:
      NeedOF = true;
      break;
    }
    break;
  }
  }
  // See if we can use the CCR value from the operand instead of
  // doing a separate TEST. TEST always sets OF and CF to 0, so unless
  // we prove that the arithmetic won't overflow, we can't use OF or CF.
  if (Op.getResNo() != 0 || NeedOF || NeedCF) {
    // Emit a CMP with 0, which is the TEST pattern.
    return DAG.getNode(M68kISD::CMP, DL, MVT::i8,
                       DAG.getConstant(0, DL, Op.getValueType()), Op);
  }
  unsigned Opcode = 0;
  unsigned NumOperands = 0;

  // Truncate operations may prevent the merge of the SETCC instruction
  // and the arithmetic instruction before it. Attempt to truncate the operands
  // of the arithmetic instruction and use a reduced bit-width instruction.
  bool NeedTruncation = false;
  SDValue ArithOp = Op;
  if (Op->getOpcode() == ISD::TRUNCATE && Op->hasOneUse()) {
    SDValue Arith = Op->getOperand(0);
    // Both the trunc and the arithmetic op need to have one user each.
    if (Arith->hasOneUse())
      switch (Arith.getOpcode()) {
      default:
        break;
      case ISD::ADD:
      case ISD::SUB:
      case ISD::AND:
      case ISD::OR:
      case ISD::XOR: {
        NeedTruncation = true;
        ArithOp = Arith;
      }
      }
  }

  // NOTICE: In the code below we use ArithOp to hold the arithmetic operation
  // which may be the result of a CAST.  We use the variable 'Op', which is the
  // non-casted variable when we check for possible users.
  switch (ArithOp.getOpcode()) {
  case ISD::ADD:
    Opcode = M68kISD::ADD;
    NumOperands = 2;
    break;
  case ISD::SHL:
  case ISD::SRL:
    // If we have a constant logical shift that's only used in a comparison
    // against zero turn it into an equivalent AND. This allows turning it into
    // a TEST instruction later.
    if ((M68kCC == M68k::COND_EQ || M68kCC == M68k::COND_NE) &&
        Op->hasOneUse() && isa<ConstantSDNode>(Op->getOperand(1)) &&
        !hasNonFlagsUse(Op)) {
      EVT VT = Op.getValueType();
      unsigned BitWidth = VT.getSizeInBits();
      unsigned ShAmt = Op->getConstantOperandVal(1);
      if (ShAmt >= BitWidth) // Avoid undefined shifts.
        break;
      APInt Mask = ArithOp.getOpcode() == ISD::SRL
                       ? APInt::getHighBitsSet(BitWidth, BitWidth - ShAmt)
                       : APInt::getLowBitsSet(BitWidth, BitWidth - ShAmt);
      if (!Mask.isSignedIntN(32)) // Avoid large immediates.
        break;
      Op = DAG.getNode(ISD::AND, DL, VT, Op->getOperand(0),
                       DAG.getConstant(Mask, DL, VT));
    }
    break;

  case ISD::AND:
    // If the primary 'and' result isn't used, don't bother using
    // M68kISD::AND, because a TEST instruction will be better.
    if (!hasNonFlagsUse(Op)) {
      SDValue Op0 = ArithOp->getOperand(0);
      SDValue Op1 = ArithOp->getOperand(1);
      EVT VT = ArithOp.getValueType();
      bool IsAndn = isBitwiseNot(Op0) || isBitwiseNot(Op1);
      bool IsLegalAndnType = VT == MVT::i32 || VT == MVT::i64;

      // But if we can combine this into an ANDN operation, then create an AND
      // now and allow it to be pattern matched into an ANDN.
      if (/*!Subtarget.hasBMI() ||*/ !IsAndn || !IsLegalAndnType)
        break;
    }
    [[fallthrough]];
  case ISD::SUB:
  case ISD::OR:
  case ISD::XOR:
    // Due to the ISEL shortcoming noted above, be conservative if this op is
    // likely to be selected as part of a load-modify-store instruction.
    for (const auto *U : Op.getNode()->uses())
      if (U->getOpcode() == ISD::STORE)
        goto default_case;

    // Otherwise use a regular CCR-setting instruction.
    switch (ArithOp.getOpcode()) {
    default:
      llvm_unreachable("unexpected operator!");
    case ISD::SUB:
      Opcode = M68kISD::SUB;
      break;
    case ISD::XOR:
      Opcode = M68kISD::XOR;
      break;
    case ISD::AND:
      Opcode = M68kISD::AND;
      break;
    case ISD::OR:
      Opcode = M68kISD::OR;
      break;
    }

    NumOperands = 2;
    break;
  case M68kISD::ADD:
  case M68kISD::SUB:
  case M68kISD::OR:
  case M68kISD::XOR:
  case M68kISD::AND:
    return SDValue(Op.getNode(), 1);
  default:
  default_case:
    break;
  }

  // If we found that truncation is beneficial, perform the truncation and
  // update 'Op'.
  if (NeedTruncation) {
    EVT VT = Op.getValueType();
    SDValue WideVal = Op->getOperand(0);
    EVT WideVT = WideVal.getValueType();
    unsigned ConvertedOp = 0;
    // Use a target machine opcode to prevent further DAGCombine
    // optimizations that may separate the arithmetic operations
    // from the setcc node.
    switch (WideVal.getOpcode()) {
    default:
      break;
    case ISD::ADD:
      ConvertedOp = M68kISD::ADD;
      break;
    case ISD::SUB:
      ConvertedOp = M68kISD::SUB;
      break;
    case ISD::AND:
      ConvertedOp = M68kISD::AND;
      break;
    case ISD::OR:
      ConvertedOp = M68kISD::OR;
      break;
    case ISD::XOR:
      ConvertedOp = M68kISD::XOR;
      break;
    }

    if (ConvertedOp) {
      const TargetLowering &TLI = DAG.getTargetLoweringInfo();
      if (TLI.isOperationLegal(WideVal.getOpcode(), WideVT)) {
        SDValue V0 = DAG.getNode(ISD::TRUNCATE, DL, VT, WideVal.getOperand(0));
        SDValue V1 = DAG.getNode(ISD::TRUNCATE, DL, VT, WideVal.getOperand(1));
        Op = DAG.getNode(ConvertedOp, DL, VT, V0, V1);
      }
    }
  }

  if (Opcode == 0) {
    // Emit a CMP with 0, which is the TEST pattern.
    return DAG.getNode(M68kISD::CMP, DL, MVT::i8,
                       DAG.getConstant(0, DL, Op.getValueType()), Op);
  }
  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::i8);
  SmallVector<SDValue, 4> Ops(Op->op_begin(), Op->op_begin() + NumOperands);

  SDValue New = DAG.getNode(Opcode, DL, VTs, Ops);
  DAG.ReplaceAllUsesWith(Op, New);
  return SDValue(New.getNode(), 1);
}

/// \brief Return true if the condition is an unsigned comparison operation.
static bool isM68kCCUnsigned(unsigned M68kCC) {
  switch (M68kCC) {
  default:
    llvm_unreachable("Invalid integer condition!");
  case M68k::COND_EQ:
  case M68k::COND_NE:
  case M68k::COND_CS:
  case M68k::COND_HI:
  case M68k::COND_LS:
  case M68k::COND_CC:
    return true;
  case M68k::COND_GT:
  case M68k::COND_GE:
  case M68k::COND_LT:
  case M68k::COND_LE:
    return false;
  }
}

SDValue M68kTargetLowering::EmitCmp(SDValue Op0, SDValue Op1, unsigned M68kCC,
                                    const SDLoc &DL, SelectionDAG &DAG) const {
  if (isNullConstant(Op1))
    return EmitTest(Op0, M68kCC, DL, DAG);

  assert(!(isa<ConstantSDNode>(Op1) && Op0.getValueType() == MVT::i1) &&
         "Unexpected comparison operation for MVT::i1 operands");

  if ((Op0.getValueType() == MVT::i8 || Op0.getValueType() == MVT::i16 ||
       Op0.getValueType() == MVT::i32 || Op0.getValueType() == MVT::i64)) {
    // Only promote the compare up to I32 if it is a 16 bit operation
    // with an immediate.  16 bit immediates are to be avoided.
    if ((Op0.getValueType() == MVT::i16 &&
         (isa<ConstantSDNode>(Op0) || isa<ConstantSDNode>(Op1))) &&
        !DAG.getMachineFunction().getFunction().hasMinSize()) {
      unsigned ExtendOp =
          isM68kCCUnsigned(M68kCC) ? ISD::ZERO_EXTEND : ISD::SIGN_EXTEND;
      Op0 = DAG.getNode(ExtendOp, DL, MVT::i32, Op0);
      Op1 = DAG.getNode(ExtendOp, DL, MVT::i32, Op1);
    }
    // Use SUB instead of CMP to enable CSE between SUB and CMP.
    SDVTList VTs = DAG.getVTList(Op0.getValueType(), MVT::i8);
    SDValue Sub = DAG.getNode(M68kISD::SUB, DL, VTs, Op0, Op1);
    return SDValue(Sub.getNode(), 1);
  }
  return DAG.getNode(M68kISD::CMP, DL, MVT::i8, Op0, Op1);
}

/// Result of 'and' or 'trunc to i1' is compared against zero.
/// Change to a BTST node if possible.
SDValue M68kTargetLowering::LowerToBTST(SDValue Op, ISD::CondCode CC,
                                        const SDLoc &DL,
                                        SelectionDAG &DAG) const {
  if (Op.getOpcode() == ISD::AND)
    return LowerAndToBTST(Op, CC, DL, DAG);
  if (Op.getOpcode() == ISD::TRUNCATE && Op.getValueType() == MVT::i1)
    return LowerTruncateToBTST(Op, CC, DL, DAG);
  return SDValue();
}

SDValue M68kTargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  MVT VT = Op.getSimpleValueType();
  assert(VT == MVT::i8 && "SetCC type must be 8-bit integer");

  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  SDLoc DL(Op);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();

  // Optimize to BTST if possible.
  // Lower (X & (1 << N)) == 0 to BTST(X, N).
  // Lower ((X >>u N) & 1) != 0 to BTST(X, N).
  // Lower ((X >>s N) & 1) != 0 to BTST(X, N).
  // Lower (trunc (X >> N) to i1) to BTST(X, N).
  if (Op0.hasOneUse() && isNullConstant(Op1) &&
      (CC == ISD::SETEQ || CC == ISD::SETNE)) {
    if (SDValue NewSetCC = LowerToBTST(Op0, CC, DL, DAG)) {
      if (VT == MVT::i1)
        return DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, NewSetCC);
      return NewSetCC;
    }
  }

  // Look for X == 0, X == 1, X != 0, or X != 1.  We can simplify some forms of
  // these.
  if ((isOneConstant(Op1) || isNullConstant(Op1)) &&
      (CC == ISD::SETEQ || CC == ISD::SETNE)) {

    // If the input is a setcc, then reuse the input setcc or use a new one with
    // the inverted condition.
    if (Op0.getOpcode() == M68kISD::SETCC) {
      M68k::CondCode CCode = (M68k::CondCode)Op0.getConstantOperandVal(0);
      bool Invert = (CC == ISD::SETNE) ^ isNullConstant(Op1);
      if (!Invert)
        return Op0;

      CCode = M68k::GetOppositeBranchCondition(CCode);
      SDValue SetCC =
          DAG.getNode(M68kISD::SETCC, DL, MVT::i8,
                      DAG.getConstant(CCode, DL, MVT::i8), Op0.getOperand(1));
      if (VT == MVT::i1)
        return DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, SetCC);
      return SetCC;
    }
  }
  if (Op0.getValueType() == MVT::i1 && (CC == ISD::SETEQ || CC == ISD::SETNE)) {
    if (isOneConstant(Op1)) {
      ISD::CondCode NewCC = ISD::GlobalISel::getSetCCInverse(CC, true);
      return DAG.getSetCC(DL, VT, Op0, DAG.getConstant(0, DL, MVT::i1), NewCC);
    }
    if (!isNullConstant(Op1)) {
      SDValue Xor = DAG.getNode(ISD::XOR, DL, MVT::i1, Op0, Op1);
      return DAG.getSetCC(DL, VT, Xor, DAG.getConstant(0, DL, MVT::i1), CC);
    }
  }

  bool IsFP = Op1.getSimpleValueType().isFloatingPoint();
  unsigned M68kCC = TranslateM68kCC(CC, DL, IsFP, Op0, Op1, DAG);
  if (M68kCC == M68k::COND_INVALID)
    return SDValue();

  SDValue CCR = EmitCmp(Op0, Op1, M68kCC, DL, DAG);
  return DAG.getNode(M68kISD::SETCC, DL, MVT::i8,
                     DAG.getConstant(M68kCC, DL, MVT::i8), CCR);
}

SDValue M68kTargetLowering::LowerSETCCCARRY(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue Carry = Op.getOperand(2);
  SDValue Cond = Op.getOperand(3);
  SDLoc DL(Op);

  assert(LHS.getSimpleValueType().isInteger() && "SETCCCARRY is integer only.");
  M68k::CondCode CC = TranslateIntegerM68kCC(cast<CondCodeSDNode>(Cond)->get());

  EVT CarryVT = Carry.getValueType();
  APInt NegOne = APInt::getAllOnes(CarryVT.getScalarSizeInBits());
  Carry = DAG.getNode(M68kISD::ADD, DL, DAG.getVTList(CarryVT, MVT::i32), Carry,
                      DAG.getConstant(NegOne, DL, CarryVT));

  SDVTList VTs = DAG.getVTList(LHS.getValueType(), MVT::i32);
  SDValue Cmp =
      DAG.getNode(M68kISD::SUBX, DL, VTs, LHS, RHS, Carry.getValue(1));

  return DAG.getNode(M68kISD::SETCC, DL, MVT::i8,
                     DAG.getConstant(CC, DL, MVT::i8), Cmp.getValue(1));
}

/// Return true if opcode is a M68k logical comparison.
static bool isM68kLogicalCmp(SDValue Op) {
  unsigned Opc = Op.getNode()->getOpcode();
  if (Opc == M68kISD::CMP)
    return true;
  if (Op.getResNo() == 1 &&
      (Opc == M68kISD::ADD || Opc == M68kISD::SUB || Opc == M68kISD::ADDX ||
       Opc == M68kISD::SUBX || Opc == M68kISD::SMUL || Opc == M68kISD::UMUL ||
       Opc == M68kISD::OR || Opc == M68kISD::XOR || Opc == M68kISD::AND))
    return true;

  if (Op.getResNo() == 2 && Opc == M68kISD::UMUL)
    return true;

  return false;
}

static bool isTruncWithZeroHighBitsInput(SDValue V, SelectionDAG &DAG) {
  if (V.getOpcode() != ISD::TRUNCATE)
    return false;

  SDValue VOp0 = V.getOperand(0);
  unsigned InBits = VOp0.getValueSizeInBits();
  unsigned Bits = V.getValueSizeInBits();
  return DAG.MaskedValueIsZero(VOp0,
                               APInt::getHighBitsSet(InBits, InBits - Bits));
}

SDValue M68kTargetLowering::LowerSELECT(SDValue Op, SelectionDAG &DAG) const {
  bool addTest = true;
  SDValue Cond = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  SDValue Op2 = Op.getOperand(2);
  SDLoc DL(Op);
  SDValue CC;

  if (Cond.getOpcode() == ISD::SETCC) {
    if (SDValue NewCond = LowerSETCC(Cond, DAG))
      Cond = NewCond;
  }

  // (select (x == 0), -1, y) -> (sign_bit (x - 1)) | y
  // (select (x == 0), y, -1) -> ~(sign_bit (x - 1)) | y
  // (select (x != 0), y, -1) -> (sign_bit (x - 1)) | y
  // (select (x != 0), -1, y) -> ~(sign_bit (x - 1)) | y
  if (Cond.getOpcode() == M68kISD::SETCC &&
      Cond.getOperand(1).getOpcode() == M68kISD::CMP &&
      isNullConstant(Cond.getOperand(1).getOperand(0))) {
    SDValue Cmp = Cond.getOperand(1);

    unsigned CondCode = Cond.getConstantOperandVal(0);

    if ((isAllOnesConstant(Op1) || isAllOnesConstant(Op2)) &&
        (CondCode == M68k::COND_EQ || CondCode == M68k::COND_NE)) {
      SDValue Y = isAllOnesConstant(Op2) ? Op1 : Op2;

      SDValue CmpOp0 = Cmp.getOperand(1);
      // Apply further optimizations for special cases
      // (select (x != 0), -1, 0) -> neg & sbb
      // (select (x == 0), 0, -1) -> neg & sbb
      if (isNullConstant(Y) &&
          (isAllOnesConstant(Op1) == (CondCode == M68k::COND_NE))) {

        SDVTList VTs = DAG.getVTList(CmpOp0.getValueType(), MVT::i32);

        SDValue Neg =
            DAG.getNode(M68kISD::SUB, DL, VTs,
                        DAG.getConstant(0, DL, CmpOp0.getValueType()), CmpOp0);

        SDValue Res = DAG.getNode(M68kISD::SETCC_CARRY, DL, Op.getValueType(),
                                  DAG.getConstant(M68k::COND_CS, DL, MVT::i8),
                                  SDValue(Neg.getNode(), 1));
        return Res;
      }

      Cmp = DAG.getNode(M68kISD::CMP, DL, MVT::i8,
                        DAG.getConstant(1, DL, CmpOp0.getValueType()), CmpOp0);

      SDValue Res = // Res = 0 or -1.
          DAG.getNode(M68kISD::SETCC_CARRY, DL, Op.getValueType(),
                      DAG.getConstant(M68k::COND_CS, DL, MVT::i8), Cmp);

      if (isAllOnesConstant(Op1) != (CondCode == M68k::COND_EQ))
        Res = DAG.getNOT(DL, Res, Res.getValueType());

      if (!isNullConstant(Op2))
        Res = DAG.getNode(ISD::OR, DL, Res.getValueType(), Res, Y);
      return Res;
    }
  }

  // Look past (and (setcc_carry (cmp ...)), 1).
  if (Cond.getOpcode() == ISD::AND &&
      Cond.getOperand(0).getOpcode() == M68kISD::SETCC_CARRY &&
      isOneConstant(Cond.getOperand(1)))
    Cond = Cond.getOperand(0);

  // If condition flag is set by a M68kISD::CMP, then use it as the condition
  // setting operand in place of the M68kISD::SETCC.
  unsigned CondOpcode = Cond.getOpcode();
  if (CondOpcode == M68kISD::SETCC || CondOpcode == M68kISD::SETCC_CARRY) {
    CC = Cond.getOperand(0);

    SDValue Cmp = Cond.getOperand(1);
    unsigned Opc = Cmp.getOpcode();

    bool IllegalFPCMov = false;

    if ((isM68kLogicalCmp(Cmp) && !IllegalFPCMov) || Opc == M68kISD::BTST) {
      Cond = Cmp;
      addTest = false;
    }
  } else if (isOverflowArithmetic(CondOpcode)) {
    // Result is unused here.
    SDValue Result;
    unsigned CCode;
    lowerOverflowArithmetic(Cond, DAG, Result, Cond, CCode);
    CC = DAG.getConstant(CCode, DL, MVT::i8);
    addTest = false;
  }

  if (addTest) {
    // Look past the truncate if the high bits are known zero.
    if (isTruncWithZeroHighBitsInput(Cond, DAG))
      Cond = Cond.getOperand(0);

    // We know the result of AND is compared against zero. Try to match
    // it to BT.
    if (Cond.getOpcode() == ISD::AND && Cond.hasOneUse()) {
      if (SDValue NewSetCC = LowerToBTST(Cond, ISD::SETNE, DL, DAG)) {
        CC = NewSetCC.getOperand(0);
        Cond = NewSetCC.getOperand(1);
        addTest = false;
      }
    }
  }

  if (addTest) {
    CC = DAG.getConstant(M68k::COND_NE, DL, MVT::i8);
    Cond = EmitTest(Cond, M68k::COND_NE, DL, DAG);
  }

  // a <  b ? -1 :  0 -> RES = ~setcc_carry
  // a <  b ?  0 : -1 -> RES = setcc_carry
  // a >= b ? -1 :  0 -> RES = setcc_carry
  // a >= b ?  0 : -1 -> RES = ~setcc_carry
  if (Cond.getOpcode() == M68kISD::SUB) {
    unsigned CondCode = CC->getAsZExtVal();

    if ((CondCode == M68k::COND_CC || CondCode == M68k::COND_CS) &&
        (isAllOnesConstant(Op1) || isAllOnesConstant(Op2)) &&
        (isNullConstant(Op1) || isNullConstant(Op2))) {
      SDValue Res =
          DAG.getNode(M68kISD::SETCC_CARRY, DL, Op.getValueType(),
                      DAG.getConstant(M68k::COND_CS, DL, MVT::i8), Cond);
      if (isAllOnesConstant(Op1) != (CondCode == M68k::COND_CS))
        return DAG.getNOT(DL, Res, Res.getValueType());
      return Res;
    }
  }

  // M68k doesn't have an i8 cmov. If both operands are the result of a
  // truncate widen the cmov and push the truncate through. This avoids
  // introducing a new branch during isel and doesn't add any extensions.
  if (Op.getValueType() == MVT::i8 && Op1.getOpcode() == ISD::TRUNCATE &&
      Op2.getOpcode() == ISD::TRUNCATE) {
    SDValue T1 = Op1.getOperand(0), T2 = Op2.getOperand(0);
    if (T1.getValueType() == T2.getValueType() &&
        // Block CopyFromReg so partial register stalls are avoided.
        T1.getOpcode() != ISD::CopyFromReg &&
        T2.getOpcode() != ISD::CopyFromReg) {
      SDVTList VTs = DAG.getVTList(T1.getValueType(), MVT::Glue);
      SDValue Cmov = DAG.getNode(M68kISD::CMOV, DL, VTs, T2, T1, CC, Cond);
      return DAG.getNode(ISD::TRUNCATE, DL, Op.getValueType(), Cmov);
    }
  }

  // Simple optimization when Cond is a constant to avoid generating
  // M68kISD::CMOV if possible.
  // TODO: Generalize this to use SelectionDAG::computeKnownBits.
  if (auto *Const = dyn_cast<ConstantSDNode>(Cond.getNode())) {
    const APInt &C = Const->getAPIntValue();
    if (C.countr_zero() >= 5)
      return Op2;
    else if (C.countr_one() >= 5)
      return Op1;
  }

  // M68kISD::CMOV means set the result (which is operand 1) to the RHS if
  // condition is true.
  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::Glue);
  SDValue Ops[] = {Op2, Op1, CC, Cond};
  return DAG.getNode(M68kISD::CMOV, DL, VTs, Ops);
}

/// Return true if node is an ISD::AND or ISD::OR of two M68k::SETcc nodes
/// each of which has no other use apart from the AND / OR.
static bool isAndOrOfSetCCs(SDValue Op, unsigned &Opc) {
  Opc = Op.getOpcode();
  if (Opc != ISD::OR && Opc != ISD::AND)
    return false;
  return (M68k::IsSETCC(Op.getOperand(0).getOpcode()) &&
          Op.getOperand(0).hasOneUse() &&
          M68k::IsSETCC(Op.getOperand(1).getOpcode()) &&
          Op.getOperand(1).hasOneUse());
}

/// Return true if node is an ISD::XOR of a M68kISD::SETCC and 1 and that the
/// SETCC node has a single use.
static bool isXor1OfSetCC(SDValue Op) {
  if (Op.getOpcode() != ISD::XOR)
    return false;
  if (isOneConstant(Op.getOperand(1)))
    return Op.getOperand(0).getOpcode() == M68kISD::SETCC &&
           Op.getOperand(0).hasOneUse();
  return false;
}

SDValue M68kTargetLowering::LowerBRCOND(SDValue Op, SelectionDAG &DAG) const {
  bool AddTest = true;
  SDValue Chain = Op.getOperand(0);
  SDValue Cond = Op.getOperand(1);
  SDValue Dest = Op.getOperand(2);
  SDLoc DL(Op);
  SDValue CC;
  bool Inverted = false;

  if (Cond.getOpcode() == ISD::SETCC) {
    // Check for setcc([su]{add,sub}o == 0).
    if (cast<CondCodeSDNode>(Cond.getOperand(2))->get() == ISD::SETEQ &&
        isNullConstant(Cond.getOperand(1)) &&
        Cond.getOperand(0).getResNo() == 1 &&
        (Cond.getOperand(0).getOpcode() == ISD::SADDO ||
         Cond.getOperand(0).getOpcode() == ISD::UADDO ||
         Cond.getOperand(0).getOpcode() == ISD::SSUBO ||
         Cond.getOperand(0).getOpcode() == ISD::USUBO)) {
      Inverted = true;
      Cond = Cond.getOperand(0);
    } else {
      if (SDValue NewCond = LowerSETCC(Cond, DAG))
        Cond = NewCond;
    }
  }

  // Look pass (and (setcc_carry (cmp ...)), 1).
  if (Cond.getOpcode() == ISD::AND &&
      Cond.getOperand(0).getOpcode() == M68kISD::SETCC_CARRY &&
      isOneConstant(Cond.getOperand(1)))
    Cond = Cond.getOperand(0);

  // If condition flag is set by a M68kISD::CMP, then use it as the condition
  // setting operand in place of the M68kISD::SETCC.
  unsigned CondOpcode = Cond.getOpcode();
  if (CondOpcode == M68kISD::SETCC || CondOpcode == M68kISD::SETCC_CARRY) {
    CC = Cond.getOperand(0);

    SDValue Cmp = Cond.getOperand(1);
    unsigned Opc = Cmp.getOpcode();

    if (isM68kLogicalCmp(Cmp) || Opc == M68kISD::BTST) {
      Cond = Cmp;
      AddTest = false;
    } else {
      switch (CC->getAsZExtVal()) {
      default:
        break;
      case M68k::COND_VS:
      case M68k::COND_CS:
        // These can only come from an arithmetic instruction with overflow,
        // e.g. SADDO, UADDO.
        Cond = Cond.getNode()->getOperand(1);
        AddTest = false;
        break;
      }
    }
  }
  CondOpcode = Cond.getOpcode();
  if (isOverflowArithmetic(CondOpcode)) {
    SDValue Result;
    unsigned CCode;
    lowerOverflowArithmetic(Cond, DAG, Result, Cond, CCode);

    if (Inverted)
      CCode = M68k::GetOppositeBranchCondition((M68k::CondCode)CCode);
    CC = DAG.getConstant(CCode, DL, MVT::i8);

    AddTest = false;
  } else {
    unsigned CondOpc;
    if (Cond.hasOneUse() && isAndOrOfSetCCs(Cond, CondOpc)) {
      SDValue Cmp = Cond.getOperand(0).getOperand(1);
      if (CondOpc == ISD::OR) {
        // Also, recognize the pattern generated by an FCMP_UNE. We can emit
        // two branches instead of an explicit OR instruction with a
        // separate test.
        if (Cmp == Cond.getOperand(1).getOperand(1) && isM68kLogicalCmp(Cmp)) {
          CC = Cond.getOperand(0).getOperand(0);
          Chain = DAG.getNode(M68kISD::BRCOND, DL, Op.getValueType(), Chain,
                              Dest, CC, Cmp);
          CC = Cond.getOperand(1).getOperand(0);
          Cond = Cmp;
          AddTest = false;
        }
      } else { // ISD::AND
        // Also, recognize the pattern generated by an FCMP_OEQ. We can emit
        // two branches instead of an explicit AND instruction with a
        // separate test. However, we only do this if this block doesn't
        // have a fall-through edge, because this requires an explicit
        // jmp when the condition is false.
        if (Cmp == Cond.getOperand(1).getOperand(1) && isM68kLogicalCmp(Cmp) &&
            Op.getNode()->hasOneUse()) {
          M68k::CondCode CCode =
              (M68k::CondCode)Cond.getOperand(0).getConstantOperandVal(0);
          CCode = M68k::GetOppositeBranchCondition(CCode);
          CC = DAG.getConstant(CCode, DL, MVT::i8);
          SDNode *User = *Op.getNode()->use_begin();
          // Look for an unconditional branch following this conditional branch.
          // We need this because we need to reverse the successors in order
          // to implement FCMP_OEQ.
          if (User->getOpcode() == ISD::BR) {
            SDValue FalseBB = User->getOperand(1);
            SDNode *NewBR =
                DAG.UpdateNodeOperands(User, User->getOperand(0), Dest);
            assert(NewBR == User);
            (void)NewBR;
            Dest = FalseBB;

            Chain = DAG.getNode(M68kISD::BRCOND, DL, Op.getValueType(), Chain,
                                Dest, CC, Cmp);
            M68k::CondCode CCode =
                (M68k::CondCode)Cond.getOperand(1).getConstantOperandVal(0);
            CCode = M68k::GetOppositeBranchCondition(CCode);
            CC = DAG.getConstant(CCode, DL, MVT::i8);
            Cond = Cmp;
            AddTest = false;
          }
        }
      }
    } else if (Cond.hasOneUse() && isXor1OfSetCC(Cond)) {
      // Recognize for xorb (setcc), 1 patterns. The xor inverts the condition.
      // It should be transformed during dag combiner except when the condition
      // is set by a arithmetics with overflow node.
      M68k::CondCode CCode =
          (M68k::CondCode)Cond.getOperand(0).getConstantOperandVal(0);
      CCode = M68k::GetOppositeBranchCondition(CCode);
      CC = DAG.getConstant(CCode, DL, MVT::i8);
      Cond = Cond.getOperand(0).getOperand(1);
      AddTest = false;
    }
  }

  if (AddTest) {
    // Look pass the truncate if the high bits are known zero.
    if (isTruncWithZeroHighBitsInput(Cond, DAG))
      Cond = Cond.getOperand(0);

    // We know the result is compared against zero. Try to match it to BT.
    if (Cond.hasOneUse()) {
      if (SDValue NewSetCC = LowerToBTST(Cond, ISD::SETNE, DL, DAG)) {
        CC = NewSetCC.getOperand(0);
        Cond = NewSetCC.getOperand(1);
        AddTest = false;
      }
    }
  }

  if (AddTest) {
    M68k::CondCode MxCond = Inverted ? M68k::COND_EQ : M68k::COND_NE;
    CC = DAG.getConstant(MxCond, DL, MVT::i8);
    Cond = EmitTest(Cond, MxCond, DL, DAG);
  }
  return DAG.getNode(M68kISD::BRCOND, DL, Op.getValueType(), Chain, Dest, CC,
                     Cond);
}

SDValue M68kTargetLowering::LowerADDC_ADDE_SUBC_SUBE(SDValue Op,
                                                     SelectionDAG &DAG) const {
  MVT VT = Op.getNode()->getSimpleValueType(0);

  // Let legalize expand this if it isn't a legal type yet.
  if (!DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return SDValue();

  SDVTList VTs = DAG.getVTList(VT, MVT::i8);

  unsigned Opc;
  bool ExtraOp = false;
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Invalid code");
  case ISD::ADDC:
    Opc = M68kISD::ADD;
    break;
  case ISD::ADDE:
    Opc = M68kISD::ADDX;
    ExtraOp = true;
    break;
  case ISD::SUBC:
    Opc = M68kISD::SUB;
    break;
  case ISD::SUBE:
    Opc = M68kISD::SUBX;
    ExtraOp = true;
    break;
  }

  if (!ExtraOp)
    return DAG.getNode(Opc, SDLoc(Op), VTs, Op.getOperand(0), Op.getOperand(1));
  return DAG.getNode(Opc, SDLoc(Op), VTs, Op.getOperand(0), Op.getOperand(1),
                     Op.getOperand(2));
}

// ConstantPool, JumpTable, GlobalAddress, and ExternalSymbol are lowered as
// their target countpart wrapped in the M68kISD::Wrapper node. Suppose N is
// one of the above mentioned nodes. It has to be wrapped because otherwise
// Select(N) returns N. So the raw TargetGlobalAddress nodes, etc. can only
// be used to form addressing mode. These wrapped nodes will be selected
// into MOV32ri.
SDValue M68kTargetLowering::LowerConstantPool(SDValue Op,
                                              SelectionDAG &DAG) const {
  ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);

  // In PIC mode (unless we're in PCRel PIC mode) we add an offset to the
  // global base reg.
  unsigned char OpFlag = Subtarget.classifyLocalReference(nullptr);

  unsigned WrapperKind = M68kISD::Wrapper;
  if (M68kII::isPCRelGlobalReference(OpFlag)) {
    WrapperKind = M68kISD::WrapperPC;
  }

  MVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue Result = DAG.getTargetConstantPool(
      CP->getConstVal(), PtrVT, CP->getAlign(), CP->getOffset(), OpFlag);

  SDLoc DL(CP);
  Result = DAG.getNode(WrapperKind, DL, PtrVT, Result);

  // With PIC, the address is actually $g + Offset.
  if (M68kII::isGlobalRelativeToPICBase(OpFlag)) {
    Result = DAG.getNode(ISD::ADD, DL, PtrVT,
                         DAG.getNode(M68kISD::GLOBAL_BASE_REG, SDLoc(), PtrVT),
                         Result);
  }

  return Result;
}

SDValue M68kTargetLowering::LowerExternalSymbol(SDValue Op,
                                                SelectionDAG &DAG) const {
  const char *Sym = cast<ExternalSymbolSDNode>(Op)->getSymbol();

  // In PIC mode (unless we're in PCRel PIC mode) we add an offset to the
  // global base reg.
  const Module *Mod = DAG.getMachineFunction().getFunction().getParent();
  unsigned char OpFlag = Subtarget.classifyExternalReference(*Mod);

  unsigned WrapperKind = M68kISD::Wrapper;
  if (M68kII::isPCRelGlobalReference(OpFlag)) {
    WrapperKind = M68kISD::WrapperPC;
  }

  auto PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue Result = DAG.getTargetExternalSymbol(Sym, PtrVT, OpFlag);

  SDLoc DL(Op);
  Result = DAG.getNode(WrapperKind, DL, PtrVT, Result);

  // With PIC, the address is actually $g + Offset.
  if (M68kII::isGlobalRelativeToPICBase(OpFlag)) {
    Result = DAG.getNode(ISD::ADD, DL, PtrVT,
                         DAG.getNode(M68kISD::GLOBAL_BASE_REG, SDLoc(), PtrVT),
                         Result);
  }

  // For symbols that require a load from a stub to get the address, emit the
  // load.
  if (M68kII::isGlobalStubReference(OpFlag)) {
    Result = DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), Result,
                         MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  }

  return Result;
}

SDValue M68kTargetLowering::LowerBlockAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  unsigned char OpFlags = Subtarget.classifyBlockAddressReference();
  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();
  int64_t Offset = cast<BlockAddressSDNode>(Op)->getOffset();
  SDLoc DL(Op);
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  // Create the TargetBlockAddressAddress node.
  SDValue Result = DAG.getTargetBlockAddress(BA, PtrVT, Offset, OpFlags);

  if (M68kII::isPCRelBlockReference(OpFlags)) {
    Result = DAG.getNode(M68kISD::WrapperPC, DL, PtrVT, Result);
  } else {
    Result = DAG.getNode(M68kISD::Wrapper, DL, PtrVT, Result);
  }

  // With PIC, the address is actually $g + Offset.
  if (M68kII::isGlobalRelativeToPICBase(OpFlags)) {
    Result =
        DAG.getNode(ISD::ADD, DL, PtrVT,
                    DAG.getNode(M68kISD::GLOBAL_BASE_REG, DL, PtrVT), Result);
  }

  return Result;
}

SDValue M68kTargetLowering::LowerGlobalAddress(const GlobalValue *GV,
                                               const SDLoc &DL, int64_t Offset,
                                               SelectionDAG &DAG) const {
  unsigned char OpFlags = Subtarget.classifyGlobalReference(GV);
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  // Create the TargetGlobalAddress node, folding in the constant
  // offset if it is legal.
  SDValue Result;
  if (M68kII::isDirectGlobalReference(OpFlags)) {
    Result = DAG.getTargetGlobalAddress(GV, DL, PtrVT, Offset);
    Offset = 0;
  } else {
    Result = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, OpFlags);
  }

  if (M68kII::isPCRelGlobalReference(OpFlags))
    Result = DAG.getNode(M68kISD::WrapperPC, DL, PtrVT, Result);
  else
    Result = DAG.getNode(M68kISD::Wrapper, DL, PtrVT, Result);

  // With PIC, the address is actually $g + Offset.
  if (M68kII::isGlobalRelativeToPICBase(OpFlags)) {
    Result =
        DAG.getNode(ISD::ADD, DL, PtrVT,
                    DAG.getNode(M68kISD::GLOBAL_BASE_REG, DL, PtrVT), Result);
  }

  // For globals that require a load from a stub to get the address, emit the
  // load.
  if (M68kII::isGlobalStubReference(OpFlags)) {
    Result = DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), Result,
                         MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  }

  // If there was a non-zero offset that we didn't fold, create an explicit
  // addition for it.
  if (Offset != 0) {
    Result = DAG.getNode(ISD::ADD, DL, PtrVT, Result,
                         DAG.getConstant(Offset, DL, PtrVT));
  }

  return Result;
}

SDValue M68kTargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  int64_t Offset = cast<GlobalAddressSDNode>(Op)->getOffset();
  return LowerGlobalAddress(GV, SDLoc(Op), Offset, DAG);
}

//===----------------------------------------------------------------------===//
// Custom Lower Jump Table
//===----------------------------------------------------------------------===//

SDValue M68kTargetLowering::LowerJumpTable(SDValue Op,
                                           SelectionDAG &DAG) const {
  JumpTableSDNode *JT = cast<JumpTableSDNode>(Op);

  // In PIC mode (unless we're in PCRel PIC mode) we add an offset to the
  // global base reg.
  unsigned char OpFlag = Subtarget.classifyLocalReference(nullptr);

  unsigned WrapperKind = M68kISD::Wrapper;
  if (M68kII::isPCRelGlobalReference(OpFlag)) {
    WrapperKind = M68kISD::WrapperPC;
  }

  auto PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue Result = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, OpFlag);
  SDLoc DL(JT);
  Result = DAG.getNode(WrapperKind, DL, PtrVT, Result);

  // With PIC, the address is actually $g + Offset.
  if (M68kII::isGlobalRelativeToPICBase(OpFlag)) {
    Result = DAG.getNode(ISD::ADD, DL, PtrVT,
                         DAG.getNode(M68kISD::GLOBAL_BASE_REG, SDLoc(), PtrVT),
                         Result);
  }

  return Result;
}

unsigned M68kTargetLowering::getJumpTableEncoding() const {
  return Subtarget.getJumpTableEncoding();
}

const MCExpr *M68kTargetLowering::LowerCustomJumpTableEntry(
    const MachineJumpTableInfo *MJTI, const MachineBasicBlock *MBB,
    unsigned uid, MCContext &Ctx) const {
  return MCSymbolRefExpr::create(MBB->getSymbol(), MCSymbolRefExpr::VK_GOTOFF,
                                 Ctx);
}

SDValue M68kTargetLowering::getPICJumpTableRelocBase(SDValue Table,
                                                     SelectionDAG &DAG) const {
  if (getJumpTableEncoding() == MachineJumpTableInfo::EK_Custom32)
    return DAG.getNode(M68kISD::GLOBAL_BASE_REG, SDLoc(),
                       getPointerTy(DAG.getDataLayout()));

  // MachineJumpTableInfo::EK_LabelDifference32 entry
  return Table;
}

// NOTE This only used for MachineJumpTableInfo::EK_LabelDifference32 entries
const MCExpr *M68kTargetLowering::getPICJumpTableRelocBaseExpr(
    const MachineFunction *MF, unsigned JTI, MCContext &Ctx) const {
  return MCSymbolRefExpr::create(MF->getJTISymbol(JTI, Ctx), Ctx);
}

M68kTargetLowering::ConstraintType
M68kTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() > 0) {
    switch (Constraint[0]) {
    case 'a':
    case 'd':
      return C_RegisterClass;
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
      return C_Immediate;
    case 'C':
      if (Constraint.size() == 2)
        switch (Constraint[1]) {
        case '0':
        case 'i':
        case 'j':
          return C_Immediate;
        default:
          break;
        }
      break;
    case 'Q':
    case 'U':
      return C_Memory;
    default:
      break;
    }
  }

  return TargetLowering::getConstraintType(Constraint);
}

void M68kTargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                      StringRef Constraint,
                                                      std::vector<SDValue> &Ops,
                                                      SelectionDAG &DAG) const {
  SDValue Result;

  if (Constraint.size() == 1) {
    // Constant constraints
    switch (Constraint[0]) {
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P': {
      auto *C = dyn_cast<ConstantSDNode>(Op);
      if (!C)
        return;

      int64_t Val = C->getSExtValue();
      switch (Constraint[0]) {
      case 'I': // constant integer in the range [1,8]
        if (Val > 0 && Val <= 8)
          break;
        return;
      case 'J': // constant signed 16-bit integer
        if (isInt<16>(Val))
          break;
        return;
      case 'K': // constant that is NOT in the range of [-0x80, 0x80)
        if (Val < -0x80 || Val >= 0x80)
          break;
        return;
      case 'L': // constant integer in the range [-8,-1]
        if (Val < 0 && Val >= -8)
          break;
        return;
      case 'M': // constant that is NOT in the range of [-0x100, 0x100]
        if (Val < -0x100 || Val >= 0x100)
          break;
        return;
      case 'N': // constant integer in the range [24,31]
        if (Val >= 24 && Val <= 31)
          break;
        return;
      case 'O': // constant integer 16
        if (Val == 16)
          break;
        return;
      case 'P': // constant integer in the range [8,15]
        if (Val >= 8 && Val <= 15)
          break;
        return;
      default:
        llvm_unreachable("Unhandled constant constraint");
      }

      Result = DAG.getTargetConstant(Val, SDLoc(Op), Op.getValueType());
      break;
    }
    default:
      break;
    }
  }

  if (Constraint.size() == 2) {
    switch (Constraint[0]) {
    case 'C':
      // Constant constraints start with 'C'
      switch (Constraint[1]) {
      case '0':
      case 'i':
      case 'j': {
        auto *C = dyn_cast<ConstantSDNode>(Op);
        if (!C)
          break;

        int64_t Val = C->getSExtValue();
        switch (Constraint[1]) {
        case '0': // constant integer 0
          if (!Val)
            break;
          return;
        case 'i': // constant integer
          break;
        case 'j': // integer constant that doesn't fit in 16 bits
          if (!isInt<16>(C->getSExtValue()))
            break;
          return;
        default:
          llvm_unreachable("Unhandled constant constraint");
        }

        Result = DAG.getTargetConstant(Val, SDLoc(Op), Op.getValueType());
        break;
      }
      default:
        break;
      }
      break;
    default:
      break;
    }
  }

  if (Result.getNode()) {
    Ops.push_back(Result);
    return;
  }

  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

std::pair<unsigned, const TargetRegisterClass *>
M68kTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                 StringRef Constraint,
                                                 MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
    case 'd':
      switch (VT.SimpleTy) {
      case MVT::i8:
        return std::make_pair(0U, &M68k::DR8RegClass);
      case MVT::i16:
        return std::make_pair(0U, &M68k::DR16RegClass);
      case MVT::i32:
        return std::make_pair(0U, &M68k::DR32RegClass);
      default:
        break;
      }
      break;
    case 'a':
      switch (VT.SimpleTy) {
      case MVT::i16:
        return std::make_pair(0U, &M68k::AR16RegClass);
      case MVT::i32:
        return std::make_pair(0U, &M68k::AR32RegClass);
      default:
        break;
      }
      break;
    default:
      break;
    }
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

/// Determines whether the callee is required to pop its own arguments.
/// Callee pop is necessary to support tail calls.
bool M68k::isCalleePop(CallingConv::ID CC, bool IsVarArg, bool GuaranteeTCO) {
  return CC == CallingConv::M68k_RTD && !IsVarArg;
}

// Return true if it is OK for this CMOV pseudo-opcode to be cascaded
// together with other CMOV pseudo-opcodes into a single basic-block with
// conditional jump around it.
static bool isCMOVPseudo(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case M68k::CMOV8d:
  case M68k::CMOV16d:
  case M68k::CMOV32r:
    return true;

  default:
    return false;
  }
}

// The CCR operand of SelectItr might be missing a kill marker
// because there were multiple uses of CCR, and ISel didn't know
// which to mark. Figure out whether SelectItr should have had a
// kill marker, and set it if it should. Returns the correct kill
// marker value.
static bool checkAndUpdateCCRKill(MachineBasicBlock::iterator SelectItr,
                                  MachineBasicBlock *BB,
                                  const TargetRegisterInfo *TRI) {
  // Scan forward through BB for a use/def of CCR.
  MachineBasicBlock::iterator miI(std::next(SelectItr));
  for (MachineBasicBlock::iterator miE = BB->end(); miI != miE; ++miI) {
    const MachineInstr &mi = *miI;
    if (mi.readsRegister(M68k::CCR, /*TRI=*/nullptr))
      return false;
    if (mi.definesRegister(M68k::CCR, /*TRI=*/nullptr))
      break; // Should have kill-flag - update below.
  }

  // If we hit the end of the block, check whether CCR is live into a
  // successor.
  if (miI == BB->end())
    for (const auto *SBB : BB->successors())
      if (SBB->isLiveIn(M68k::CCR))
        return false;

  // We found a def, or hit the end of the basic block and CCR wasn't live
  // out. SelectMI should have a kill flag on CCR.
  SelectItr->addRegisterKilled(M68k::CCR, TRI);
  return true;
}

MachineBasicBlock *
M68kTargetLowering::EmitLoweredSelect(MachineInstr &MI,
                                      MachineBasicBlock *MBB) const {
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  // To "insert" a SELECT_CC instruction, we actually have to insert the
  // diamond control-flow pattern.  The incoming instruction knows the
  // destination vreg to set, the condition code register to branch on, the
  // true/false values to select between, and a branch opcode to use.
  const BasicBlock *BB = MBB->getBasicBlock();
  MachineFunction::iterator It = ++MBB->getIterator();

  //  ThisMBB:
  //  ...
  //   TrueVal = ...
  //   cmp ccX, r1, r2
  //   bcc Copy1MBB
  //   fallthrough --> Copy0MBB
  MachineBasicBlock *ThisMBB = MBB;
  MachineFunction *F = MBB->getParent();

  // This code lowers all pseudo-CMOV instructions. Generally it lowers these
  // as described above, by inserting a MBB, and then making a PHI at the join
  // point to select the true and false operands of the CMOV in the PHI.
  //
  // The code also handles two different cases of multiple CMOV opcodes
  // in a row.
  //
  // Case 1:
  // In this case, there are multiple CMOVs in a row, all which are based on
  // the same condition setting (or the exact opposite condition setting).
  // In this case we can lower all the CMOVs using a single inserted MBB, and
  // then make a number of PHIs at the join point to model the CMOVs. The only
  // trickiness here, is that in a case like:
  //
  // t2 = CMOV cond1 t1, f1
  // t3 = CMOV cond1 t2, f2
  //
  // when rewriting this into PHIs, we have to perform some renaming on the
  // temps since you cannot have a PHI operand refer to a PHI result earlier
  // in the same block.  The "simple" but wrong lowering would be:
  //
  // t2 = PHI t1(BB1), f1(BB2)
  // t3 = PHI t2(BB1), f2(BB2)
  //
  // but clearly t2 is not defined in BB1, so that is incorrect. The proper
  // renaming is to note that on the path through BB1, t2 is really just a
  // copy of t1, and do that renaming, properly generating:
  //
  // t2 = PHI t1(BB1), f1(BB2)
  // t3 = PHI t1(BB1), f2(BB2)
  //
  // Case 2, we lower cascaded CMOVs such as
  //
  //   (CMOV (CMOV F, T, cc1), T, cc2)
  //
  // to two successives branches.
  MachineInstr *CascadedCMOV = nullptr;
  MachineInstr *LastCMOV = &MI;
  M68k::CondCode CC = M68k::CondCode(MI.getOperand(3).getImm());
  M68k::CondCode OppCC = M68k::GetOppositeBranchCondition(CC);
  MachineBasicBlock::iterator NextMIIt =
      std::next(MachineBasicBlock::iterator(MI));

  // Check for case 1, where there are multiple CMOVs with the same condition
  // first.  Of the two cases of multiple CMOV lowerings, case 1 reduces the
  // number of jumps the most.

  if (isCMOVPseudo(MI)) {
    // See if we have a string of CMOVS with the same condition.
    while (NextMIIt != MBB->end() && isCMOVPseudo(*NextMIIt) &&
           (NextMIIt->getOperand(3).getImm() == CC ||
            NextMIIt->getOperand(3).getImm() == OppCC)) {
      LastCMOV = &*NextMIIt;
      ++NextMIIt;
    }
  }

  // This checks for case 2, but only do this if we didn't already find
  // case 1, as indicated by LastCMOV == MI.
  if (LastCMOV == &MI && NextMIIt != MBB->end() &&
      NextMIIt->getOpcode() == MI.getOpcode() &&
      NextMIIt->getOperand(2).getReg() == MI.getOperand(2).getReg() &&
      NextMIIt->getOperand(1).getReg() == MI.getOperand(0).getReg() &&
      NextMIIt->getOperand(1).isKill()) {
    CascadedCMOV = &*NextMIIt;
  }

  MachineBasicBlock *Jcc1MBB = nullptr;

  // If we have a cascaded CMOV, we lower it to two successive branches to
  // the same block.  CCR is used by both, so mark it as live in the second.
  if (CascadedCMOV) {
    Jcc1MBB = F->CreateMachineBasicBlock(BB);
    F->insert(It, Jcc1MBB);
    Jcc1MBB->addLiveIn(M68k::CCR);
  }

  MachineBasicBlock *Copy0MBB = F->CreateMachineBasicBlock(BB);
  MachineBasicBlock *SinkMBB = F->CreateMachineBasicBlock(BB);
  F->insert(It, Copy0MBB);
  F->insert(It, SinkMBB);

  // Set the call frame size on entry to the new basic blocks.
  unsigned CallFrameSize = TII->getCallFrameSizeAt(MI);
  Copy0MBB->setCallFrameSize(CallFrameSize);
  SinkMBB->setCallFrameSize(CallFrameSize);

  // If the CCR register isn't dead in the terminator, then claim that it's
  // live into the sink and copy blocks.
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();

  MachineInstr *LastCCRSUser = CascadedCMOV ? CascadedCMOV : LastCMOV;
  if (!LastCCRSUser->killsRegister(M68k::CCR, /*TRI=*/nullptr) &&
      !checkAndUpdateCCRKill(LastCCRSUser, MBB, TRI)) {
    Copy0MBB->addLiveIn(M68k::CCR);
    SinkMBB->addLiveIn(M68k::CCR);
  }

  // Transfer the remainder of MBB and its successor edges to SinkMBB.
  SinkMBB->splice(SinkMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(LastCMOV)), MBB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(MBB);

  // Add the true and fallthrough blocks as its successors.
  if (CascadedCMOV) {
    // The fallthrough block may be Jcc1MBB, if we have a cascaded CMOV.
    MBB->addSuccessor(Jcc1MBB);

    // In that case, Jcc1MBB will itself fallthrough the Copy0MBB, and
    // jump to the SinkMBB.
    Jcc1MBB->addSuccessor(Copy0MBB);
    Jcc1MBB->addSuccessor(SinkMBB);
  } else {
    MBB->addSuccessor(Copy0MBB);
  }

  // The true block target of the first (or only) branch is always SinkMBB.
  MBB->addSuccessor(SinkMBB);

  // Create the conditional branch instruction.
  unsigned Opc = M68k::GetCondBranchFromCond(CC);
  BuildMI(MBB, DL, TII->get(Opc)).addMBB(SinkMBB);

  if (CascadedCMOV) {
    unsigned Opc2 = M68k::GetCondBranchFromCond(
        (M68k::CondCode)CascadedCMOV->getOperand(3).getImm());
    BuildMI(Jcc1MBB, DL, TII->get(Opc2)).addMBB(SinkMBB);
  }

  //  Copy0MBB:
  //   %FalseValue = ...
  //   # fallthrough to SinkMBB
  Copy0MBB->addSuccessor(SinkMBB);

  //  SinkMBB:
  //   %Result = phi [ %FalseValue, Copy0MBB ], [ %TrueValue, ThisMBB ]
  //  ...
  MachineBasicBlock::iterator MIItBegin = MachineBasicBlock::iterator(MI);
  MachineBasicBlock::iterator MIItEnd =
      std::next(MachineBasicBlock::iterator(LastCMOV));
  MachineBasicBlock::iterator SinkInsertionPoint = SinkMBB->begin();
  DenseMap<unsigned, std::pair<unsigned, unsigned>> RegRewriteTable;
  MachineInstrBuilder MIB;

  // As we are creating the PHIs, we have to be careful if there is more than
  // one.  Later CMOVs may reference the results of earlier CMOVs, but later
  // PHIs have to reference the individual true/false inputs from earlier PHIs.
  // That also means that PHI construction must work forward from earlier to
  // later, and that the code must maintain a mapping from earlier PHI's
  // destination registers, and the registers that went into the PHI.

  for (MachineBasicBlock::iterator MIIt = MIItBegin; MIIt != MIItEnd; ++MIIt) {
    Register DestReg = MIIt->getOperand(0).getReg();
    Register Op1Reg = MIIt->getOperand(1).getReg();
    Register Op2Reg = MIIt->getOperand(2).getReg();

    // If this CMOV we are generating is the opposite condition from
    // the jump we generated, then we have to swap the operands for the
    // PHI that is going to be generated.
    if (MIIt->getOperand(3).getImm() == OppCC)
      std::swap(Op1Reg, Op2Reg);

    if (RegRewriteTable.find(Op1Reg) != RegRewriteTable.end())
      Op1Reg = RegRewriteTable[Op1Reg].first;

    if (RegRewriteTable.find(Op2Reg) != RegRewriteTable.end())
      Op2Reg = RegRewriteTable[Op2Reg].second;

    MIB =
        BuildMI(*SinkMBB, SinkInsertionPoint, DL, TII->get(M68k::PHI), DestReg)
            .addReg(Op1Reg)
            .addMBB(Copy0MBB)
            .addReg(Op2Reg)
            .addMBB(ThisMBB);

    // Add this PHI to the rewrite table.
    RegRewriteTable[DestReg] = std::make_pair(Op1Reg, Op2Reg);
  }

  // If we have a cascaded CMOV, the second Jcc provides the same incoming
  // value as the first Jcc (the True operand of the SELECT_CC/CMOV nodes).
  if (CascadedCMOV) {
    MIB.addReg(MI.getOperand(2).getReg()).addMBB(Jcc1MBB);
    // Copy the PHI result to the register defined by the second CMOV.
    BuildMI(*SinkMBB, std::next(MachineBasicBlock::iterator(MIB.getInstr())),
            DL, TII->get(TargetOpcode::COPY),
            CascadedCMOV->getOperand(0).getReg())
        .addReg(MI.getOperand(0).getReg());
    CascadedCMOV->eraseFromParent();
  }

  // Now remove the CMOV(s).
  for (MachineBasicBlock::iterator MIIt = MIItBegin; MIIt != MIItEnd;)
    (MIIt++)->eraseFromParent();

  return SinkMBB;
}

MachineBasicBlock *
M68kTargetLowering::EmitLoweredSegAlloca(MachineInstr &MI,
                                         MachineBasicBlock *BB) const {
  llvm_unreachable("Cannot lower Segmented Stack Alloca with stack-split on");
}

MachineBasicBlock *
M68kTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unexpected instr type to insert");
  case M68k::CMOV8d:
  case M68k::CMOV16d:
  case M68k::CMOV32r:
    return EmitLoweredSelect(MI, BB);
  case M68k::SALLOCA:
    return EmitLoweredSegAlloca(MI, BB);
  }
}

SDValue M68kTargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto PtrVT = getPointerTy(MF.getDataLayout());
  M68kMachineFunctionInfo *FuncInfo = MF.getInfo<M68kMachineFunctionInfo>();

  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  SDLoc DL(Op);

  // vastart just stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  SDValue FR = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);
  return DAG.getStore(Op.getOperand(0), DL, FR, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue M68kTargetLowering::LowerATOMICFENCE(SDValue Op,
                                             SelectionDAG &DAG) const {
  // Lower to a memory barrier created from inline asm.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  LLVMContext &Ctx = *DAG.getContext();

  const unsigned Flags = InlineAsm::Extra_MayLoad | InlineAsm::Extra_MayStore |
                         InlineAsm::Extra_HasSideEffects;
  const SDValue AsmOperands[4] = {
      Op.getOperand(0), // Input chain
      DAG.getTargetExternalSymbol(
          "", TLI.getProgramPointerTy(
                  DAG.getDataLayout())),   // Empty inline asm string
      DAG.getMDNode(MDNode::get(Ctx, {})), // (empty) srcloc
      DAG.getTargetConstant(Flags, SDLoc(Op),
                            TLI.getPointerTy(DAG.getDataLayout())), // Flags
  };

  return DAG.getNode(ISD::INLINEASM, SDLoc(Op),
                     DAG.getVTList(MVT::Other, MVT::Glue), AsmOperands);
}

// Lower dynamic stack allocation to _alloca call for Cygwin/Mingw targets.
// Calls to _alloca are needed to probe the stack when allocating more than 4k
// bytes in one go. Touching the stack at 4K increments is necessary to ensure
// that the guard pages used by the OS virtual memory manager are allocated in
// correct sequence.
SDValue M68kTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                    SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  bool SplitStack = MF.shouldSplitStack();

  SDLoc DL(Op);

  // Get the inputs.
  SDNode *Node = Op.getNode();
  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);
  unsigned Align = Op.getConstantOperandVal(2);
  EVT VT = Node->getValueType(0);

  // Chain the dynamic stack allocation so that it doesn't modify the stack
  // pointer when other instructions are using the stack.
  Chain = DAG.getCALLSEQ_START(Chain, 0, 0, DL);

  SDValue Result;
  if (SplitStack) {
    auto &MRI = MF.getRegInfo();
    auto SPTy = getPointerTy(DAG.getDataLayout());
    auto *ARClass = getRegClassFor(SPTy);
    Register Vreg = MRI.createVirtualRegister(ARClass);
    Chain = DAG.getCopyToReg(Chain, DL, Vreg, Size);
    Result = DAG.getNode(M68kISD::SEG_ALLOCA, DL, SPTy, Chain,
                         DAG.getRegister(Vreg, SPTy));
  } else {
    auto &TLI = DAG.getTargetLoweringInfo();
    Register SPReg = TLI.getStackPointerRegisterToSaveRestore();
    assert(SPReg && "Target cannot require DYNAMIC_STACKALLOC expansion and"
                    " not tell us which reg is the stack pointer!");

    SDValue SP = DAG.getCopyFromReg(Chain, DL, SPReg, VT);
    Chain = SP.getValue(1);
    const TargetFrameLowering &TFI = *Subtarget.getFrameLowering();
    unsigned StackAlign = TFI.getStackAlignment();
    Result = DAG.getNode(ISD::SUB, DL, VT, SP, Size); // Value
    if (Align > StackAlign)
      Result = DAG.getNode(ISD::AND, DL, VT, Result,
                           DAG.getConstant(-(uint64_t)Align, DL, VT));
    Chain = DAG.getCopyToReg(Chain, DL, SPReg, Result); // Output chain
  }

  Chain = DAG.getCALLSEQ_END(Chain, 0, 0, SDValue(), DL);

  SDValue Ops[2] = {Result, Chain};
  return DAG.getMergeValues(Ops, DL);
}

SDValue M68kTargetLowering::LowerShiftLeftParts(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Shamt = Op.getOperand(2);
  EVT VT = Lo.getValueType();

  // if Shamt - register size < 0: // Shamt < register size
  //   Lo = Lo << Shamt
  //   Hi = (Hi << Shamt) | ((Lo >>u 1) >>u (register size - 1 ^ Shamt))
  // else:
  //   Lo = 0
  //   Hi = Lo << (Shamt - register size)

  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue One = DAG.getConstant(1, DL, VT);
  SDValue MinusRegisterSize = DAG.getConstant(-32, DL, VT);
  SDValue RegisterSizeMinus1 = DAG.getConstant(32 - 1, DL, VT);
  SDValue ShamtMinusRegisterSize =
      DAG.getNode(ISD::ADD, DL, VT, Shamt, MinusRegisterSize);
  SDValue RegisterSizeMinus1Shamt =
      DAG.getNode(ISD::XOR, DL, VT, RegisterSizeMinus1, Shamt);

  SDValue LoTrue = DAG.getNode(ISD::SHL, DL, VT, Lo, Shamt);
  SDValue ShiftRight1Lo = DAG.getNode(ISD::SRL, DL, VT, Lo, One);
  SDValue ShiftRightLo =
      DAG.getNode(ISD::SRL, DL, VT, ShiftRight1Lo, RegisterSizeMinus1Shamt);
  SDValue ShiftLeftHi = DAG.getNode(ISD::SHL, DL, VT, Hi, Shamt);
  SDValue HiTrue = DAG.getNode(ISD::OR, DL, VT, ShiftLeftHi, ShiftRightLo);
  SDValue HiFalse = DAG.getNode(ISD::SHL, DL, VT, Lo, ShamtMinusRegisterSize);

  SDValue CC =
      DAG.getSetCC(DL, MVT::i8, ShamtMinusRegisterSize, Zero, ISD::SETLT);

  Lo = DAG.getNode(ISD::SELECT, DL, VT, CC, LoTrue, Zero);
  Hi = DAG.getNode(ISD::SELECT, DL, VT, CC, HiTrue, HiFalse);

  return DAG.getMergeValues({Lo, Hi}, DL);
}

SDValue M68kTargetLowering::LowerShiftRightParts(SDValue Op, SelectionDAG &DAG,
                                                 bool IsSRA) const {
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Shamt = Op.getOperand(2);
  EVT VT = Lo.getValueType();

  // SRA expansion:
  //   if Shamt - register size < 0: // Shamt < register size
  //     Lo = (Lo >>u Shamt) | ((Hi << 1) << (register size - 1 ^ Shamt))
  //     Hi = Hi >>s Shamt
  //   else:
  //     Lo = Hi >>s (Shamt - register size);
  //     Hi = Hi >>s (register size - 1)
  //
  // SRL expansion:
  //   if Shamt - register size < 0: // Shamt < register size
  //     Lo = (Lo >>u Shamt) | ((Hi << 1) << (register size - 1 ^ Shamt))
  //     Hi = Hi >>u Shamt
  //   else:
  //     Lo = Hi >>u (Shamt - register size);
  //     Hi = 0;

  unsigned ShiftRightOp = IsSRA ? ISD::SRA : ISD::SRL;

  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue One = DAG.getConstant(1, DL, VT);
  SDValue MinusRegisterSize = DAG.getConstant(-32, DL, VT);
  SDValue RegisterSizeMinus1 = DAG.getConstant(32 - 1, DL, VT);
  SDValue ShamtMinusRegisterSize =
      DAG.getNode(ISD::ADD, DL, VT, Shamt, MinusRegisterSize);
  SDValue RegisterSizeMinus1Shamt =
      DAG.getNode(ISD::XOR, DL, VT, RegisterSizeMinus1, Shamt);

  SDValue ShiftRightLo = DAG.getNode(ISD::SRL, DL, VT, Lo, Shamt);
  SDValue ShiftLeftHi1 = DAG.getNode(ISD::SHL, DL, VT, Hi, One);
  SDValue ShiftLeftHi =
      DAG.getNode(ISD::SHL, DL, VT, ShiftLeftHi1, RegisterSizeMinus1Shamt);
  SDValue LoTrue = DAG.getNode(ISD::OR, DL, VT, ShiftRightLo, ShiftLeftHi);
  SDValue HiTrue = DAG.getNode(ShiftRightOp, DL, VT, Hi, Shamt);
  SDValue LoFalse =
      DAG.getNode(ShiftRightOp, DL, VT, Hi, ShamtMinusRegisterSize);
  SDValue HiFalse =
      IsSRA ? DAG.getNode(ISD::SRA, DL, VT, Hi, RegisterSizeMinus1) : Zero;

  SDValue CC =
      DAG.getSetCC(DL, MVT::i8, ShamtMinusRegisterSize, Zero, ISD::SETLT);

  Lo = DAG.getNode(ISD::SELECT, DL, VT, CC, LoTrue, LoFalse);
  Hi = DAG.getNode(ISD::SELECT, DL, VT, CC, HiTrue, HiFalse);

  return DAG.getMergeValues({Lo, Hi}, DL);
}

//===----------------------------------------------------------------------===//
// DAG Combine
//===----------------------------------------------------------------------===//

static SDValue getSETCC(M68k::CondCode Cond, SDValue CCR, const SDLoc &dl,
                        SelectionDAG &DAG) {
  return DAG.getNode(M68kISD::SETCC, dl, MVT::i8,
                     DAG.getConstant(Cond, dl, MVT::i8), CCR);
}
// When legalizing carry, we create carries via add X, -1
// If that comes from an actual carry, via setcc, we use the
// carry directly.
static SDValue combineCarryThroughADD(SDValue CCR) {
  if (CCR.getOpcode() == M68kISD::ADD) {
    if (isAllOnesConstant(CCR.getOperand(1))) {
      SDValue Carry = CCR.getOperand(0);
      while (Carry.getOpcode() == ISD::TRUNCATE ||
             Carry.getOpcode() == ISD::ZERO_EXTEND ||
             Carry.getOpcode() == ISD::SIGN_EXTEND ||
             Carry.getOpcode() == ISD::ANY_EXTEND ||
             (Carry.getOpcode() == ISD::AND &&
              isOneConstant(Carry.getOperand(1))))
        Carry = Carry.getOperand(0);
      if (Carry.getOpcode() == M68kISD::SETCC ||
          Carry.getOpcode() == M68kISD::SETCC_CARRY) {
        if (Carry.getConstantOperandVal(0) == M68k::COND_CS)
          return Carry.getOperand(1);
      }
    }
  }

  return SDValue();
}

/// Optimize a CCR definition used according to the condition code \p CC into
/// a simpler CCR value, potentially returning a new \p CC and replacing uses
/// of chain values.
static SDValue combineSetCCCCR(SDValue CCR, M68k::CondCode &CC,
                               SelectionDAG &DAG,
                               const M68kSubtarget &Subtarget) {
  if (CC == M68k::COND_CS)
    if (SDValue Flags = combineCarryThroughADD(CCR))
      return Flags;

  return SDValue();
}

// Optimize  RES = M68kISD::SETCC CONDCODE, CCR_INPUT
static SDValue combineM68kSetCC(SDNode *N, SelectionDAG &DAG,
                                const M68kSubtarget &Subtarget) {
  SDLoc DL(N);
  M68k::CondCode CC = M68k::CondCode(N->getConstantOperandVal(0));
  SDValue CCR = N->getOperand(1);

  // Try to simplify the CCR and condition code operands.
  if (SDValue Flags = combineSetCCCCR(CCR, CC, DAG, Subtarget))
    return getSETCC(CC, Flags, DL, DAG);

  return SDValue();
}
static SDValue combineM68kBrCond(SDNode *N, SelectionDAG &DAG,
                                 const M68kSubtarget &Subtarget) {
  SDLoc DL(N);
  M68k::CondCode CC = M68k::CondCode(N->getConstantOperandVal(2));
  SDValue CCR = N->getOperand(3);

  // Try to simplify the CCR and condition code operands.
  // Make sure to not keep references to operands, as combineSetCCCCR can
  // RAUW them under us.
  if (SDValue Flags = combineSetCCCCR(CCR, CC, DAG, Subtarget)) {
    SDValue Cond = DAG.getConstant(CC, DL, MVT::i8);
    return DAG.getNode(M68kISD::BRCOND, DL, N->getVTList(), N->getOperand(0),
                       N->getOperand(1), Cond, Flags);
  }

  return SDValue();
}

static SDValue combineSUBX(SDNode *N, SelectionDAG &DAG) {
  if (SDValue Flags = combineCarryThroughADD(N->getOperand(2))) {
    MVT VT = N->getSimpleValueType(0);
    SDVTList VTs = DAG.getVTList(VT, MVT::i32);
    return DAG.getNode(M68kISD::SUBX, SDLoc(N), VTs, N->getOperand(0),
                       N->getOperand(1), Flags);
  }

  return SDValue();
}

// Optimize RES, CCR = M68kISD::ADDX LHS, RHS, CCR
static SDValue combineADDX(SDNode *N, SelectionDAG &DAG,
                           TargetLowering::DAGCombinerInfo &DCI) {
  if (SDValue Flags = combineCarryThroughADD(N->getOperand(2))) {
    MVT VT = N->getSimpleValueType(0);
    SDVTList VTs = DAG.getVTList(VT, MVT::i32);
    return DAG.getNode(M68kISD::ADDX, SDLoc(N), VTs, N->getOperand(0),
                       N->getOperand(1), Flags);
  }

  return SDValue();
}

SDValue M68kTargetLowering::PerformDAGCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  switch (N->getOpcode()) {
  case M68kISD::SUBX:
    return combineSUBX(N, DAG);
  case M68kISD::ADDX:
    return combineADDX(N, DAG, DCI);
  case M68kISD::SETCC:
    return combineM68kSetCC(N, DAG, Subtarget);
  case M68kISD::BRCOND:
    return combineM68kBrCond(N, DAG, Subtarget);
  }

  return SDValue();
}

//===----------------------------------------------------------------------===//
// M68kISD Node Names
//===----------------------------------------------------------------------===//
const char *M68kTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case M68kISD::CALL:
    return "M68kISD::CALL";
  case M68kISD::TAIL_CALL:
    return "M68kISD::TAIL_CALL";
  case M68kISD::RET:
    return "M68kISD::RET";
  case M68kISD::TC_RETURN:
    return "M68kISD::TC_RETURN";
  case M68kISD::ADD:
    return "M68kISD::ADD";
  case M68kISD::SUB:
    return "M68kISD::SUB";
  case M68kISD::ADDX:
    return "M68kISD::ADDX";
  case M68kISD::SUBX:
    return "M68kISD::SUBX";
  case M68kISD::SMUL:
    return "M68kISD::SMUL";
  case M68kISD::UMUL:
    return "M68kISD::UMUL";
  case M68kISD::OR:
    return "M68kISD::OR";
  case M68kISD::XOR:
    return "M68kISD::XOR";
  case M68kISD::AND:
    return "M68kISD::AND";
  case M68kISD::CMP:
    return "M68kISD::CMP";
  case M68kISD::BTST:
    return "M68kISD::BTST";
  case M68kISD::SELECT:
    return "M68kISD::SELECT";
  case M68kISD::CMOV:
    return "M68kISD::CMOV";
  case M68kISD::BRCOND:
    return "M68kISD::BRCOND";
  case M68kISD::SETCC:
    return "M68kISD::SETCC";
  case M68kISD::SETCC_CARRY:
    return "M68kISD::SETCC_CARRY";
  case M68kISD::GLOBAL_BASE_REG:
    return "M68kISD::GLOBAL_BASE_REG";
  case M68kISD::Wrapper:
    return "M68kISD::Wrapper";
  case M68kISD::WrapperPC:
    return "M68kISD::WrapperPC";
  case M68kISD::SEG_ALLOCA:
    return "M68kISD::SEG_ALLOCA";
  default:
    return NULL;
  }
}

CCAssignFn *M68kTargetLowering::getCCAssignFn(CallingConv::ID CC, bool Return,
                                              bool IsVarArg) const {
  if (Return)
    return RetCC_M68k_C;
  else
    return CC_M68k_C;
}
