//===-- CSKYISelLowering.cpp - CSKY DAG Lowering Implementation  ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that CSKY uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "CSKYISelLowering.h"
#include "CSKYCallingConv.h"
#include "CSKYConstantPoolValue.h"
#include "CSKYMachineFunctionInfo.h"
#include "CSKYRegisterInfo.h"
#include "CSKYSubtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "csky-isel-lowering"

STATISTIC(NumTailCalls, "Number of tail calls");

#include "CSKYGenCallingConv.inc"

static const MCPhysReg GPRArgRegs[] = {CSKY::R0, CSKY::R1, CSKY::R2, CSKY::R3};

CSKYTargetLowering::CSKYTargetLowering(const TargetMachine &TM,
                                       const CSKYSubtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {
  // Register Class
  addRegisterClass(MVT::i32, &CSKY::GPRRegClass);

  if (STI.useHardFloat()) {
    if (STI.hasFPUv2SingleFloat())
      addRegisterClass(MVT::f32, &CSKY::sFPR32RegClass);
    else if (STI.hasFPUv3SingleFloat())
      addRegisterClass(MVT::f32, &CSKY::FPR32RegClass);

    if (STI.hasFPUv2DoubleFloat())
      addRegisterClass(MVT::f64, &CSKY::sFPR64RegClass);
    else if (STI.hasFPUv3DoubleFloat())
      addRegisterClass(MVT::f64, &CSKY::FPR64RegClass);
  }

  setOperationAction(ISD::UADDO_CARRY, MVT::i32, Legal);
  setOperationAction(ISD::USUBO_CARRY, MVT::i32, Legal);
  setOperationAction(ISD::BITREVERSE, MVT::i32, Legal);

  setOperationAction(ISD::SREM, MVT::i32, Expand);
  setOperationAction(ISD::UREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::CTPOP, MVT::i32, Expand);
  setOperationAction(ISD::ROTR, MVT::i32, Expand);
  setOperationAction(ISD::SHL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRA_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);
  setOperationAction(ISD::BR_CC, MVT::i32, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  setOperationAction(ISD::MULHS, MVT::i32, Expand);
  setOperationAction(ISD::MULHU, MVT::i32, Expand);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);

  setLoadExtAction(ISD::EXTLOAD, MVT::i32, MVT::i1, Promote);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i1, Promote);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, MVT::i1, Promote);

  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::ExternalSymbol, MVT::i32, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i32, Custom);
  if (!Subtarget.hasE2()) {
    setOperationAction(ISD::ConstantPool, MVT::i32, Custom);
  }
  setOperationAction(ISD::JumpTable, MVT::i32, Custom);
  setOperationAction(ISD::VASTART, MVT::Other, Custom);

  if (!Subtarget.hasE2()) {
    setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i8, Expand);
    setLoadExtAction(ISD::SEXTLOAD, MVT::i32, MVT::i16, Expand);
    setOperationAction(ISD::CTLZ, MVT::i32, Expand);
    setOperationAction(ISD::BSWAP, MVT::i32, Expand);
  }

  if (!Subtarget.has2E3()) {
    setOperationAction(ISD::ABS, MVT::i32, Expand);
    setOperationAction(ISD::BITREVERSE, MVT::i32, Expand);
    setOperationAction(ISD::CTTZ, MVT::i32, Expand);
    setOperationAction(ISD::SDIV, MVT::i32, Expand);
    setOperationAction(ISD::UDIV, MVT::i32, Expand);
  }

  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Expand);

  // Float

  ISD::CondCode FPCCToExtend[] = {
      ISD::SETONE, ISD::SETUEQ, ISD::SETUGT,
      ISD::SETUGE, ISD::SETULT, ISD::SETULE,
  };

  ISD::NodeType FPOpToExpand[] = {
      ISD::FSIN, ISD::FCOS,      ISD::FSINCOS,    ISD::FPOW,
      ISD::FREM, ISD::FCOPYSIGN, ISD::FP16_TO_FP, ISD::FP_TO_FP16};

  if (STI.useHardFloat()) {

    MVT AllVTy[] = {MVT::f32, MVT::f64};

    for (auto VT : AllVTy) {
      setOperationAction(ISD::FREM, VT, Expand);
      setOperationAction(ISD::SELECT_CC, VT, Expand);
      setOperationAction(ISD::BR_CC, VT, Expand);

      for (auto CC : FPCCToExtend)
        setCondCodeAction(CC, VT, Expand);
      for (auto Op : FPOpToExpand)
        setOperationAction(Op, VT, Expand);
    }

    if (STI.hasFPUv2SingleFloat() || STI.hasFPUv3SingleFloat()) {
      setOperationAction(ISD::ConstantFP, MVT::f32, Legal);
      setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::f16, Expand);
      setTruncStoreAction(MVT::f32, MVT::f16, Expand);
    }
    if (STI.hasFPUv2DoubleFloat() || STI.hasFPUv3DoubleFloat()) {
      setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
      setTruncStoreAction(MVT::f64, MVT::f32, Expand);
      setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f16, Expand);
      setTruncStoreAction(MVT::f64, MVT::f16, Expand);
    }
  }

  // Compute derived properties from the register classes.
  computeRegisterProperties(STI.getRegisterInfo());

  setBooleanContents(UndefinedBooleanContent);
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);

  // TODO: Add atomic support fully.
  setMaxAtomicSizeInBitsSupported(0);

  setStackPointerRegisterToSaveRestore(CSKY::R14);
  setMinFunctionAlignment(Align(2));
  setSchedulingPreference(Sched::Source);
}

SDValue CSKYTargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("unimplemented op");
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return LowerExternalSymbol(Op, DAG);
  case ISD::GlobalTLSAddress:
    return LowerGlobalTLSAddress(Op, DAG);
  case ISD::JumpTable:
    return LowerJumpTable(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::ConstantPool:
    return LowerConstantPool(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::FRAMEADDR:
    return LowerFRAMEADDR(Op, DAG);
  case ISD::RETURNADDR:
    return LowerRETURNADDR(Op, DAG);
  }
}

EVT CSKYTargetLowering::getSetCCResultType(const DataLayout &DL,
                                           LLVMContext &Context, EVT VT) const {
  if (!VT.isVector())
    return MVT::i32;

  return VT.changeVectorElementTypeToInteger();
}

static SDValue convertValVTToLocVT(SelectionDAG &DAG, SDValue Val,
                                   const CCValAssign &VA, const SDLoc &DL) {
  EVT LocVT = VA.getLocVT();

  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
    break;
  case CCValAssign::BCvt:
    Val = DAG.getNode(ISD::BITCAST, DL, LocVT, Val);
    break;
  }
  return Val;
}

static SDValue convertLocVTToValVT(SelectionDAG &DAG, SDValue Val,
                                   const CCValAssign &VA, const SDLoc &DL) {
  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
    break;
  case CCValAssign::BCvt:
    Val = DAG.getNode(ISD::BITCAST, DL, VA.getValVT(), Val);
    break;
  }
  return Val;
}

static SDValue unpackFromRegLoc(const CSKYSubtarget &Subtarget,
                                SelectionDAG &DAG, SDValue Chain,
                                const CCValAssign &VA, const SDLoc &DL) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  EVT LocVT = VA.getLocVT();
  SDValue Val;
  const TargetRegisterClass *RC;

  switch (LocVT.getSimpleVT().SimpleTy) {
  default:
    llvm_unreachable("Unexpected register type");
  case MVT::i32:
    RC = &CSKY::GPRRegClass;
    break;
  case MVT::f32:
    RC = Subtarget.hasFPUv2SingleFloat() ? &CSKY::sFPR32RegClass
                                         : &CSKY::FPR32RegClass;
    break;
  case MVT::f64:
    RC = Subtarget.hasFPUv2DoubleFloat() ? &CSKY::sFPR64RegClass
                                         : &CSKY::FPR64RegClass;
    break;
  }

  Register VReg = RegInfo.createVirtualRegister(RC);
  RegInfo.addLiveIn(VA.getLocReg(), VReg);
  Val = DAG.getCopyFromReg(Chain, DL, VReg, LocVT);

  return convertLocVTToValVT(DAG, Val, VA, DL);
}

static SDValue unpackFromMemLoc(SelectionDAG &DAG, SDValue Chain,
                                const CCValAssign &VA, const SDLoc &DL) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  EVT LocVT = VA.getLocVT();
  EVT ValVT = VA.getValVT();
  EVT PtrVT = MVT::getIntegerVT(DAG.getDataLayout().getPointerSizeInBits(0));
  int FI = MFI.CreateFixedObject(ValVT.getSizeInBits() / 8,
                                 VA.getLocMemOffset(), /*Immutable=*/true);
  SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
  SDValue Val;

  ISD::LoadExtType ExtType;
  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
  case CCValAssign::BCvt:
    ExtType = ISD::NON_EXTLOAD;
    break;
  }
  Val = DAG.getExtLoad(
      ExtType, DL, LocVT, Chain, FIN,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI), ValVT);
  return Val;
}

static SDValue unpack64(SelectionDAG &DAG, SDValue Chain, const CCValAssign &VA,
                        const SDLoc &DL) {
  assert(VA.getLocVT() == MVT::i32 &&
         (VA.getValVT() == MVT::f64 || VA.getValVT() == MVT::i64) &&
         "Unexpected VA");
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  if (VA.isMemLoc()) {
    // f64/i64 is passed on the stack.
    int FI = MFI.CreateFixedObject(8, VA.getLocMemOffset(), /*Immutable=*/true);
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i32);
    return DAG.getLoad(VA.getValVT(), DL, Chain, FIN,
                       MachinePointerInfo::getFixedStack(MF, FI));
  }

  assert(VA.isRegLoc() && "Expected register VA assignment");

  Register LoVReg = RegInfo.createVirtualRegister(&CSKY::GPRRegClass);
  RegInfo.addLiveIn(VA.getLocReg(), LoVReg);
  SDValue Lo = DAG.getCopyFromReg(Chain, DL, LoVReg, MVT::i32);
  SDValue Hi;
  if (VA.getLocReg() == CSKY::R3) {
    // Second half of f64/i64 is passed on the stack.
    int FI = MFI.CreateFixedObject(4, 0, /*Immutable=*/true);
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i32);
    Hi = DAG.getLoad(MVT::i32, DL, Chain, FIN,
                     MachinePointerInfo::getFixedStack(MF, FI));
  } else {
    // Second half of f64/i64 is passed in another GPR.
    Register HiVReg = RegInfo.createVirtualRegister(&CSKY::GPRRegClass);
    RegInfo.addLiveIn(VA.getLocReg() + 1, HiVReg);
    Hi = DAG.getCopyFromReg(Chain, DL, HiVReg, MVT::i32);
  }
  return DAG.getNode(CSKYISD::BITCAST_FROM_LOHI, DL, VA.getValVT(), Lo, Hi);
}

// Transform physical registers into virtual registers.
SDValue CSKYTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::C:
  case CallingConv::Fast:
    break;
  }

  MachineFunction &MF = DAG.getMachineFunction();

  // Used with vargs to acumulate store chains.
  std::vector<SDValue> OutChains;

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());

  CCInfo.AnalyzeFormalArguments(Ins, CCAssignFnForCall(CallConv, IsVarArg));

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue ArgValue;

    bool IsF64OnCSKY = VA.getLocVT() == MVT::i32 && VA.getValVT() == MVT::f64;

    if (IsF64OnCSKY)
      ArgValue = unpack64(DAG, Chain, VA, DL);
    else if (VA.isRegLoc())
      ArgValue = unpackFromRegLoc(Subtarget, DAG, Chain, VA, DL);
    else
      ArgValue = unpackFromMemLoc(DAG, Chain, VA, DL);

    InVals.push_back(ArgValue);
  }

  if (IsVarArg) {
    const unsigned XLenInBytes = 4;
    const MVT XLenVT = MVT::i32;

    ArrayRef<MCPhysReg> ArgRegs = ArrayRef(GPRArgRegs);
    unsigned Idx = CCInfo.getFirstUnallocated(ArgRegs);
    const TargetRegisterClass *RC = &CSKY::GPRRegClass;
    MachineFrameInfo &MFI = MF.getFrameInfo();
    MachineRegisterInfo &RegInfo = MF.getRegInfo();
    CSKYMachineFunctionInfo *CSKYFI = MF.getInfo<CSKYMachineFunctionInfo>();

    // Offset of the first variable argument from stack pointer, and size of
    // the vararg save area. For now, the varargs save area is either zero or
    // large enough to hold a0-a4.
    int VaArgOffset, VarArgsSaveSize;

    // If all registers are allocated, then all varargs must be passed on the
    // stack and we don't need to save any argregs.
    if (ArgRegs.size() == Idx) {
      VaArgOffset = CCInfo.getStackSize();
      VarArgsSaveSize = 0;
    } else {
      VarArgsSaveSize = XLenInBytes * (ArgRegs.size() - Idx);
      VaArgOffset = -VarArgsSaveSize;
    }

    // Record the frame index of the first variable argument
    // which is a value necessary to VASTART.
    int FI = MFI.CreateFixedObject(XLenInBytes, VaArgOffset, true);
    CSKYFI->setVarArgsFrameIndex(FI);

    // Copy the integer registers that may have been used for passing varargs
    // to the vararg save area.
    for (unsigned I = Idx; I < ArgRegs.size();
         ++I, VaArgOffset += XLenInBytes) {
      const Register Reg = RegInfo.createVirtualRegister(RC);
      RegInfo.addLiveIn(ArgRegs[I], Reg);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, Reg, XLenVT);
      FI = MFI.CreateFixedObject(XLenInBytes, VaArgOffset, true);
      SDValue PtrOff = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      SDValue Store = DAG.getStore(Chain, DL, ArgValue, PtrOff,
                                   MachinePointerInfo::getFixedStack(MF, FI));
      cast<StoreSDNode>(Store.getNode())
          ->getMemOperand()
          ->setValue((Value *)nullptr);
      OutChains.push_back(Store);
    }
    CSKYFI->setVarArgsSaveSize(VarArgsSaveSize);
  }

  // All stores are grouped in one node to allow the matching between
  // the size of Ins and InVals. This only happens for vararg functions.
  if (!OutChains.empty()) {
    OutChains.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
  }

  return Chain;
}

bool CSKYTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> CSKYLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, CSKYLocs, Context);
  return CCInfo.CheckReturn(Outs, CCAssignFnForReturn(CallConv, IsVarArg));
}

SDValue
CSKYTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                bool IsVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  // Stores the assignment of the return value to a location.
  SmallVector<CCValAssign, 16> CSKYLocs;

  // Info about the registers and stack slot.
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), CSKYLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, CCAssignFnForReturn(CallConv, IsVarArg));

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0, e = CSKYLocs.size(); i < e; ++i) {
    SDValue Val = OutVals[i];
    CCValAssign &VA = CSKYLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    bool IsF64OnCSKY = VA.getLocVT() == MVT::i32 && VA.getValVT() == MVT::f64;

    if (IsF64OnCSKY) {

      assert(VA.isRegLoc() && "Expected return via registers");
      SDValue Split64 = DAG.getNode(CSKYISD::BITCAST_TO_LOHI, DL,
                                    DAG.getVTList(MVT::i32, MVT::i32), Val);
      SDValue Lo = Split64.getValue(0);
      SDValue Hi = Split64.getValue(1);

      Register RegLo = VA.getLocReg();
      assert(RegLo < CSKY::R31 && "Invalid register pair");
      Register RegHi = RegLo + 1;

      Chain = DAG.getCopyToReg(Chain, DL, RegLo, Lo, Glue);
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(RegLo, MVT::i32));
      Chain = DAG.getCopyToReg(Chain, DL, RegHi, Hi, Glue);
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(RegHi, MVT::i32));
    } else {
      // Handle a 'normal' return.
      Val = convertValVTToLocVT(DAG, Val, VA, DL);
      Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Val, Glue);

      // Guarantee that all emitted copies are stuck together.
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
    }
  }

  RetOps[0] = Chain; // Update chain.

  // Add the glue node if we have it.
  if (Glue.getNode()) {
    RetOps.push_back(Glue);
  }

  // Interrupt service routines use different return instructions.
  if (DAG.getMachineFunction().getFunction().hasFnAttribute("interrupt"))
    return DAG.getNode(CSKYISD::NIR, DL, MVT::Other, RetOps);

  return DAG.getNode(CSKYISD::RET, DL, MVT::Other, RetOps);
}

// Lower a call to a callseq_start + CALL + callseq_end chain, and add input
// and output parameter nodes.
SDValue CSKYTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &IsTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  MVT XLenVT = MVT::i32;

  MachineFunction &MF = DAG.getMachineFunction();

  // Analyze the operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState ArgCCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());

  ArgCCInfo.AnalyzeCallOperands(Outs, CCAssignFnForCall(CallConv, IsVarArg));

  // Check if it's really possible to do a tail call.
  if (IsTailCall)
    IsTailCall = false; // TODO: TailCallOptimization;

  if (IsTailCall)
    ++NumTailCalls;
  else if (CLI.CB && CLI.CB->isMustTailCall())
    report_fatal_error("failed to perform tail call elimination on a call "
                       "site marked musttail");

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = ArgCCInfo.getStackSize();

  // Create local copies for byval args
  SmallVector<SDValue, 8> ByValArgs;
  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    if (!Flags.isByVal())
      continue;

    SDValue Arg = OutVals[i];
    unsigned Size = Flags.getByValSize();
    Align Alignment = Flags.getNonZeroByValAlign();

    int FI =
        MF.getFrameInfo().CreateStackObject(Size, Alignment, /*isSS=*/false);
    SDValue FIPtr = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    SDValue SizeNode = DAG.getConstant(Size, DL, XLenVT);

    Chain = DAG.getMemcpy(Chain, DL, FIPtr, Arg, SizeNode, Alignment,
                          /*IsVolatile=*/false,
                          /*AlwaysInline=*/false, /*CI=*/nullptr, IsTailCall,
                          MachinePointerInfo(), MachinePointerInfo());
    ByValArgs.push_back(FIPtr);
  }

  if (!IsTailCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, CLI.DL);

  // Copy argument values to their designated locations.
  SmallVector<std::pair<Register, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;
  for (unsigned i = 0, j = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue ArgValue = OutVals[i];
    ISD::ArgFlagsTy Flags = Outs[i].Flags;

    bool IsF64OnCSKY = VA.getLocVT() == MVT::i32 && VA.getValVT() == MVT::f64;

    if (IsF64OnCSKY && VA.isRegLoc()) {
      SDValue Split64 =
          DAG.getNode(CSKYISD::BITCAST_TO_LOHI, DL,
                      DAG.getVTList(MVT::i32, MVT::i32), ArgValue);
      SDValue Lo = Split64.getValue(0);
      SDValue Hi = Split64.getValue(1);

      Register RegLo = VA.getLocReg();
      RegsToPass.push_back(std::make_pair(RegLo, Lo));

      if (RegLo == CSKY::R3) {
        // Second half of f64/i64 is passed on the stack.
        // Work out the address of the stack slot.
        if (!StackPtr.getNode())
          StackPtr = DAG.getCopyFromReg(Chain, DL, CSKY::R14, PtrVT);
        // Emit the store.
        MemOpChains.push_back(
            DAG.getStore(Chain, DL, Hi, StackPtr, MachinePointerInfo()));
      } else {
        // Second half of f64/i64 is passed in another GPR.
        assert(RegLo < CSKY::R31 && "Invalid register pair");
        Register RegHigh = RegLo + 1;
        RegsToPass.push_back(std::make_pair(RegHigh, Hi));
      }
      continue;
    }

    ArgValue = convertValVTToLocVT(DAG, ArgValue, VA, DL);

    // Use local copy if it is a byval arg.
    if (Flags.isByVal())
      ArgValue = ByValArgs[j++];

    if (VA.isRegLoc()) {
      // Queue up the argument copies and emit them at the end.
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), ArgValue));
    } else {
      assert(VA.isMemLoc() && "Argument not register or memory");
      assert(!IsTailCall && "Tail call not allowed if stack is used "
                            "for passing parameters");

      // Work out the address of the stack slot.
      if (!StackPtr.getNode())
        StackPtr = DAG.getCopyFromReg(Chain, DL, CSKY::R14, PtrVT);
      SDValue Address =
          DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr,
                      DAG.getIntPtrConstant(VA.getLocMemOffset(), DL));

      // Emit the store.
      MemOpChains.push_back(
          DAG.getStore(Chain, DL, ArgValue, Address, MachinePointerInfo()));
    }
  }

  // Join the stores, which are independent of one another.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  SDValue Glue;

  // Build a sequence of copy-to-reg nodes, chained and glued together.
  for (auto &Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg.first, Reg.second, Glue);
    Glue = Chain.getValue(1);
  }

  SmallVector<SDValue, 8> Ops;
  EVT Ty = getPointerTy(DAG.getDataLayout());
  bool IsRegCall = false;

  Ops.push_back(Chain);

  if (GlobalAddressSDNode *S = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = S->getGlobal();
    bool IsLocal = getTargetMachine().shouldAssumeDSOLocal(GV);

    if (isPositionIndependent() || !Subtarget.has2E3()) {
      IsRegCall = true;
      Ops.push_back(getAddr<GlobalAddressSDNode, true>(S, DAG, IsLocal));
    } else {
      Ops.push_back(getTargetNode(cast<GlobalAddressSDNode>(Callee), DL, Ty,
                                  DAG, CSKYII::MO_None));
      Ops.push_back(getTargetConstantPoolValue(
          cast<GlobalAddressSDNode>(Callee), Ty, DAG, CSKYII::MO_None));
    }
  } else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    bool IsLocal = getTargetMachine().shouldAssumeDSOLocal(nullptr);

    if (isPositionIndependent() || !Subtarget.has2E3()) {
      IsRegCall = true;
      Ops.push_back(getAddr<ExternalSymbolSDNode, true>(S, DAG, IsLocal));
    } else {
      Ops.push_back(getTargetNode(cast<ExternalSymbolSDNode>(Callee), DL, Ty,
                                  DAG, CSKYII::MO_None));
      Ops.push_back(getTargetConstantPoolValue(
          cast<ExternalSymbolSDNode>(Callee), Ty, DAG, CSKYII::MO_None));
    }
  } else {
    IsRegCall = true;
    Ops.push_back(Callee);
  }

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (auto &Reg : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg.first, Reg.second.getValueType()));

  if (!IsTailCall) {
    // Add a register mask operand representing the call-preserved registers.
    const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
    const uint32_t *Mask = TRI->getCallPreservedMask(MF, CallConv);
    assert(Mask && "Missing call preserved mask for calling convention");
    Ops.push_back(DAG.getRegisterMask(Mask));
  }

  // Glue the call to the argument copies, if any.
  if (Glue.getNode())
    Ops.push_back(Glue);

  // Emit the call.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);

  if (IsTailCall) {
    MF.getFrameInfo().setHasTailCall();
    return DAG.getNode(IsRegCall ? CSKYISD::TAILReg : CSKYISD::TAIL, DL,
                       NodeTys, Ops);
  }

  Chain = DAG.getNode(IsRegCall ? CSKYISD::CALLReg : CSKYISD::CALL, DL, NodeTys,
                      Ops);
  DAG.addNoMergeSiteInfo(Chain.getNode(), CLI.NoMerge);
  Glue = Chain.getValue(1);

  // Mark the end of the call, which is glued to the call itself.
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> CSKYLocs;
  CCState RetCCInfo(CallConv, IsVarArg, MF, CSKYLocs, *DAG.getContext());
  RetCCInfo.AnalyzeCallResult(Ins, CCAssignFnForReturn(CallConv, IsVarArg));

  // Copy all of the result registers out of their specified physreg.
  for (auto &VA : CSKYLocs) {
    // Copy the value out
    SDValue RetValue =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), Glue);
    // Glue the RetValue to the end of the call sequence
    Chain = RetValue.getValue(1);
    Glue = RetValue.getValue(2);

    bool IsF64OnCSKY = VA.getLocVT() == MVT::i32 && VA.getValVT() == MVT::f64;

    if (IsF64OnCSKY) {
      assert(VA.getLocReg() == GPRArgRegs[0] && "Unexpected reg assignment");
      SDValue RetValue2 =
          DAG.getCopyFromReg(Chain, DL, GPRArgRegs[1], MVT::i32, Glue);
      Chain = RetValue2.getValue(1);
      Glue = RetValue2.getValue(2);
      RetValue = DAG.getNode(CSKYISD::BITCAST_FROM_LOHI, DL, VA.getValVT(),
                             RetValue, RetValue2);
    }

    RetValue = convertLocVTToValVT(DAG, RetValue, VA, DL);

    InVals.push_back(RetValue);
  }

  return Chain;
}

CCAssignFn *CSKYTargetLowering::CCAssignFnForReturn(CallingConv::ID CC,
                                                    bool IsVarArg) const {
  if (IsVarArg || !Subtarget.useHardFloatABI())
    return RetCC_CSKY_ABIV2_SOFT;
  else
    return RetCC_CSKY_ABIV2_FP;
}

CCAssignFn *CSKYTargetLowering::CCAssignFnForCall(CallingConv::ID CC,
                                                  bool IsVarArg) const {
  if (IsVarArg || !Subtarget.useHardFloatABI())
    return CC_CSKY_ABIV2_SOFT;
  else
    return CC_CSKY_ABIV2_FP;
}

static CSKYCP::CSKYCPModifier getModifier(unsigned Flags) {

  if (Flags == CSKYII::MO_ADDR32)
    return CSKYCP::ADDR;
  else if (Flags == CSKYII::MO_GOT32)
    return CSKYCP::GOT;
  else if (Flags == CSKYII::MO_GOTOFF)
    return CSKYCP::GOTOFF;
  else if (Flags == CSKYII::MO_PLT32)
    return CSKYCP::PLT;
  else if (Flags == CSKYII::MO_None)
    return CSKYCP::NO_MOD;
  else
    assert(0 && "unknown CSKYII Modifier");
  return CSKYCP::NO_MOD;
}

SDValue CSKYTargetLowering::getTargetConstantPoolValue(GlobalAddressSDNode *N,
                                                       EVT Ty,
                                                       SelectionDAG &DAG,
                                                       unsigned Flags) const {
  CSKYConstantPoolValue *CPV = CSKYConstantPoolConstant::Create(
      N->getGlobal(), CSKYCP::CPValue, 0, getModifier(Flags), false);

  return DAG.getTargetConstantPool(CPV, Ty);
}

CSKYTargetLowering::ConstraintType
CSKYTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'a':
    case 'b':
    case 'v':
    case 'w':
    case 'y':
      return C_RegisterClass;
    case 'c':
    case 'l':
    case 'h':
    case 'z':
      return C_Register;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
CSKYTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                 StringRef Constraint,
                                                 MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      return std::make_pair(0U, &CSKY::GPRRegClass);
    case 'a':
      return std::make_pair(0U, &CSKY::mGPRRegClass);
    case 'b':
      return std::make_pair(0U, &CSKY::sGPRRegClass);
    case 'z':
      return std::make_pair(CSKY::R14, &CSKY::GPRRegClass);
    case 'c':
      return std::make_pair(CSKY::C, &CSKY::CARRYRegClass);
    case 'w':
      if ((Subtarget.hasFPUv2SingleFloat() ||
           Subtarget.hasFPUv3SingleFloat()) &&
          VT == MVT::f32)
        return std::make_pair(0U, &CSKY::sFPR32RegClass);
      if ((Subtarget.hasFPUv2DoubleFloat() ||
           Subtarget.hasFPUv3DoubleFloat()) &&
          VT == MVT::f64)
        return std::make_pair(0U, &CSKY::sFPR64RegClass);
      break;
    case 'v':
      if (Subtarget.hasFPUv2SingleFloat() && VT == MVT::f32)
        return std::make_pair(0U, &CSKY::sFPR32RegClass);
      if (Subtarget.hasFPUv3SingleFloat() && VT == MVT::f32)
        return std::make_pair(0U, &CSKY::FPR32RegClass);
      if (Subtarget.hasFPUv2DoubleFloat() && VT == MVT::f64)
        return std::make_pair(0U, &CSKY::sFPR64RegClass);
      if (Subtarget.hasFPUv3DoubleFloat() && VT == MVT::f64)
        return std::make_pair(0U, &CSKY::FPR64RegClass);
      break;
    default:
      break;
    }
  }

  if (Constraint == "{c}")
    return std::make_pair(CSKY::C, &CSKY::CARRYRegClass);

  // Clang will correctly decode the usage of register name aliases into their
  // official names. However, other frontends like `rustc` do not. This allows
  // users of these frontends to use the ABI names for registers in LLVM-style
  // register constraints.
  unsigned XRegFromAlias = StringSwitch<unsigned>(Constraint.lower())
                               .Case("{a0}", CSKY::R0)
                               .Case("{a1}", CSKY::R1)
                               .Case("{a2}", CSKY::R2)
                               .Case("{a3}", CSKY::R3)
                               .Case("{l0}", CSKY::R4)
                               .Case("{l1}", CSKY::R5)
                               .Case("{l2}", CSKY::R6)
                               .Case("{l3}", CSKY::R7)
                               .Case("{l4}", CSKY::R8)
                               .Case("{l5}", CSKY::R9)
                               .Case("{l6}", CSKY::R10)
                               .Case("{l7}", CSKY::R11)
                               .Case("{t0}", CSKY::R12)
                               .Case("{t1}", CSKY::R13)
                               .Case("{sp}", CSKY::R14)
                               .Case("{lr}", CSKY::R15)
                               .Case("{l8}", CSKY::R16)
                               .Case("{l9}", CSKY::R17)
                               .Case("{t2}", CSKY::R18)
                               .Case("{t3}", CSKY::R19)
                               .Case("{t4}", CSKY::R20)
                               .Case("{t5}", CSKY::R21)
                               .Case("{t6}", CSKY::R22)
                               .Cases("{t7}", "{fp}", CSKY::R23)
                               .Cases("{t8}", "{top}", CSKY::R24)
                               .Cases("{t9}", "{bsp}", CSKY::R25)
                               .Case("{r26}", CSKY::R26)
                               .Case("{r27}", CSKY::R27)
                               .Cases("{gb}", "{rgb}", "{rdb}", CSKY::R28)
                               .Cases("{tb}", "{rtb}", CSKY::R29)
                               .Case("{svbr}", CSKY::R30)
                               .Case("{tls}", CSKY::R31)
                               .Default(CSKY::NoRegister);

  if (XRegFromAlias != CSKY::NoRegister)
    return std::make_pair(XRegFromAlias, &CSKY::GPRRegClass);

  // Since TargetLowering::getRegForInlineAsmConstraint uses the name of the
  // TableGen record rather than the AsmName to choose registers for InlineAsm
  // constraints, plus we want to match those names to the widest floating point
  // register type available, manually select floating point registers here.
  //
  // The second case is the ABI name of the register, so that frontends can also
  // use the ABI names in register constraint lists.
  if (Subtarget.useHardFloat()) {
    unsigned FReg = StringSwitch<unsigned>(Constraint.lower())
                        .Cases("{fr0}", "{vr0}", CSKY::F0_32)
                        .Cases("{fr1}", "{vr1}", CSKY::F1_32)
                        .Cases("{fr2}", "{vr2}", CSKY::F2_32)
                        .Cases("{fr3}", "{vr3}", CSKY::F3_32)
                        .Cases("{fr4}", "{vr4}", CSKY::F4_32)
                        .Cases("{fr5}", "{vr5}", CSKY::F5_32)
                        .Cases("{fr6}", "{vr6}", CSKY::F6_32)
                        .Cases("{fr7}", "{vr7}", CSKY::F7_32)
                        .Cases("{fr8}", "{vr8}", CSKY::F8_32)
                        .Cases("{fr9}", "{vr9}", CSKY::F9_32)
                        .Cases("{fr10}", "{vr10}", CSKY::F10_32)
                        .Cases("{fr11}", "{vr11}", CSKY::F11_32)
                        .Cases("{fr12}", "{vr12}", CSKY::F12_32)
                        .Cases("{fr13}", "{vr13}", CSKY::F13_32)
                        .Cases("{fr14}", "{vr14}", CSKY::F14_32)
                        .Cases("{fr15}", "{vr15}", CSKY::F15_32)
                        .Cases("{fr16}", "{vr16}", CSKY::F16_32)
                        .Cases("{fr17}", "{vr17}", CSKY::F17_32)
                        .Cases("{fr18}", "{vr18}", CSKY::F18_32)
                        .Cases("{fr19}", "{vr19}", CSKY::F19_32)
                        .Cases("{fr20}", "{vr20}", CSKY::F20_32)
                        .Cases("{fr21}", "{vr21}", CSKY::F21_32)
                        .Cases("{fr22}", "{vr22}", CSKY::F22_32)
                        .Cases("{fr23}", "{vr23}", CSKY::F23_32)
                        .Cases("{fr24}", "{vr24}", CSKY::F24_32)
                        .Cases("{fr25}", "{vr25}", CSKY::F25_32)
                        .Cases("{fr26}", "{vr26}", CSKY::F26_32)
                        .Cases("{fr27}", "{vr27}", CSKY::F27_32)
                        .Cases("{fr28}", "{vr28}", CSKY::F28_32)
                        .Cases("{fr29}", "{vr29}", CSKY::F29_32)
                        .Cases("{fr30}", "{vr30}", CSKY::F30_32)
                        .Cases("{fr31}", "{vr31}", CSKY::F31_32)
                        .Default(CSKY::NoRegister);
    if (FReg != CSKY::NoRegister) {
      assert(CSKY::F0_32 <= FReg && FReg <= CSKY::F31_32 && "Unknown fp-reg");
      unsigned RegNo = FReg - CSKY::F0_32;
      unsigned DReg = CSKY::F0_64 + RegNo;

      if (Subtarget.hasFPUv2DoubleFloat())
        return std::make_pair(DReg, &CSKY::sFPR64RegClass);
      else if (Subtarget.hasFPUv3DoubleFloat())
        return std::make_pair(DReg, &CSKY::FPR64RegClass);
      else if (Subtarget.hasFPUv2SingleFloat())
        return std::make_pair(FReg, &CSKY::sFPR32RegClass);
      else if (Subtarget.hasFPUv3SingleFloat())
        return std::make_pair(FReg, &CSKY::FPR32RegClass);
    }
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

static MachineBasicBlock *
emitSelectPseudo(MachineInstr &MI, MachineBasicBlock *BB, unsigned Opcode) {

  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  // To "insert" a SELECT instruction, we actually have to insert the
  // diamond control-flow pattern.  The incoming instruction knows the
  // destination vreg to set, the condition code register to branch on, the
  // true/false values to select between, and a branch opcode to use.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  //  thisMBB:
  //  ...
  //   TrueVal = ...
  //   bt32 c, sinkMBB
  //   fallthrough --> copyMBB
  MachineBasicBlock *thisMBB = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *copyMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *sinkMBB = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, copyMBB);
  F->insert(It, sinkMBB);

  // Transfer the remainder of BB and its successor edges to sinkMBB.
  sinkMBB->splice(sinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  sinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  // Next, add the true and fallthrough blocks as its successors.
  BB->addSuccessor(copyMBB);
  BB->addSuccessor(sinkMBB);

  // bt32 condition, sinkMBB
  BuildMI(BB, DL, TII.get(Opcode))
      .addReg(MI.getOperand(1).getReg())
      .addMBB(sinkMBB);

  //  copyMBB:
  //   %FalseValue = ...
  //   # fallthrough to sinkMBB
  BB = copyMBB;

  // Update machine-CFG edges
  BB->addSuccessor(sinkMBB);

  //  sinkMBB:
  //   %Result = phi [ %TrueValue, thisMBB ], [ %FalseValue, copyMBB ]
  //  ...
  BB = sinkMBB;

  BuildMI(*BB, BB->begin(), DL, TII.get(CSKY::PHI), MI.getOperand(0).getReg())
      .addReg(MI.getOperand(2).getReg())
      .addMBB(thisMBB)
      .addReg(MI.getOperand(3).getReg())
      .addMBB(copyMBB);

  MI.eraseFromParent(); // The pseudo instruction is gone now.

  return BB;
}

MachineBasicBlock *
CSKYTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unexpected instr type to insert");
  case CSKY::FSELS:
  case CSKY::FSELD:
    if (Subtarget.hasE2())
      return emitSelectPseudo(MI, BB, CSKY::BT32);
    else
      return emitSelectPseudo(MI, BB, CSKY::BT16);
  case CSKY::ISEL32:
    return emitSelectPseudo(MI, BB, CSKY::BT32);
  case CSKY::ISEL16:
    return emitSelectPseudo(MI, BB, CSKY::BT16);
  }
}

SDValue CSKYTargetLowering::getTargetConstantPoolValue(ExternalSymbolSDNode *N,
                                                       EVT Ty,
                                                       SelectionDAG &DAG,
                                                       unsigned Flags) const {
  CSKYConstantPoolValue *CPV =
      CSKYConstantPoolSymbol::Create(Type::getInt32Ty(*DAG.getContext()),
                                     N->getSymbol(), 0, getModifier(Flags));

  return DAG.getTargetConstantPool(CPV, Ty);
}

SDValue CSKYTargetLowering::getTargetConstantPoolValue(JumpTableSDNode *N,
                                                       EVT Ty,
                                                       SelectionDAG &DAG,
                                                       unsigned Flags) const {
  CSKYConstantPoolValue *CPV =
      CSKYConstantPoolJT::Create(Type::getInt32Ty(*DAG.getContext()),
                                 N->getIndex(), 0, getModifier(Flags));
  return DAG.getTargetConstantPool(CPV, Ty);
}

SDValue CSKYTargetLowering::getTargetConstantPoolValue(BlockAddressSDNode *N,
                                                       EVT Ty,
                                                       SelectionDAG &DAG,
                                                       unsigned Flags) const {
  assert(N->getOffset() == 0);
  CSKYConstantPoolValue *CPV = CSKYConstantPoolConstant::Create(
      N->getBlockAddress(), CSKYCP::CPBlockAddress, 0, getModifier(Flags),
      false);
  return DAG.getTargetConstantPool(CPV, Ty);
}

SDValue CSKYTargetLowering::getTargetConstantPoolValue(ConstantPoolSDNode *N,
                                                       EVT Ty,
                                                       SelectionDAG &DAG,
                                                       unsigned Flags) const {
  assert(N->getOffset() == 0);
  CSKYConstantPoolValue *CPV = CSKYConstantPoolConstant::Create(
      N->getConstVal(), Type::getInt32Ty(*DAG.getContext()),
      CSKYCP::CPConstPool, 0, getModifier(Flags), false);
  return DAG.getTargetConstantPool(CPV, Ty);
}

SDValue CSKYTargetLowering::getTargetNode(GlobalAddressSDNode *N, SDLoc DL,
                                          EVT Ty, SelectionDAG &DAG,
                                          unsigned Flags) const {
  return DAG.getTargetGlobalAddress(N->getGlobal(), DL, Ty, 0, Flags);
}

SDValue CSKYTargetLowering::getTargetNode(ExternalSymbolSDNode *N, SDLoc DL,
                                          EVT Ty, SelectionDAG &DAG,
                                          unsigned Flags) const {
  return DAG.getTargetExternalSymbol(N->getSymbol(), Ty, Flags);
}

SDValue CSKYTargetLowering::getTargetNode(JumpTableSDNode *N, SDLoc DL, EVT Ty,
                                          SelectionDAG &DAG,
                                          unsigned Flags) const {
  return DAG.getTargetJumpTable(N->getIndex(), Ty, Flags);
}

SDValue CSKYTargetLowering::getTargetNode(BlockAddressSDNode *N, SDLoc DL,
                                          EVT Ty, SelectionDAG &DAG,
                                          unsigned Flags) const {
  return DAG.getTargetBlockAddress(N->getBlockAddress(), Ty, N->getOffset(),
                                   Flags);
}

SDValue CSKYTargetLowering::getTargetNode(ConstantPoolSDNode *N, SDLoc DL,
                                          EVT Ty, SelectionDAG &DAG,
                                          unsigned Flags) const {

  return DAG.getTargetConstantPool(N->getConstVal(), Ty, N->getAlign(),
                                   N->getOffset(), Flags);
}

const char *CSKYTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  default:
    llvm_unreachable("unknown CSKYISD node");
  case CSKYISD::NIE:
    return "CSKYISD::NIE";
  case CSKYISD::NIR:
    return "CSKYISD::NIR";
  case CSKYISD::RET:
    return "CSKYISD::RET";
  case CSKYISD::CALL:
    return "CSKYISD::CALL";
  case CSKYISD::CALLReg:
    return "CSKYISD::CALLReg";
  case CSKYISD::TAIL:
    return "CSKYISD::TAIL";
  case CSKYISD::TAILReg:
    return "CSKYISD::TAILReg";
  case CSKYISD::LOAD_ADDR:
    return "CSKYISD::LOAD_ADDR";
  case CSKYISD::BITCAST_TO_LOHI:
    return "CSKYISD::BITCAST_TO_LOHI";
  case CSKYISD::BITCAST_FROM_LOHI:
    return "CSKYISD::BITCAST_FROM_LOHI";
  }
}

SDValue CSKYTargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  int64_t Offset = N->getOffset();

  const GlobalValue *GV = N->getGlobal();
  bool IsLocal = getTargetMachine().shouldAssumeDSOLocal(GV);
  SDValue Addr = getAddr<GlobalAddressSDNode, false>(N, DAG, IsLocal);

  // In order to maximise the opportunity for common subexpression elimination,
  // emit a separate ADD node for the global address offset instead of folding
  // it in the global address node. Later peephole optimisations may choose to
  // fold it back in when profitable.
  if (Offset != 0)
    return DAG.getNode(ISD::ADD, DL, Ty, Addr,
                       DAG.getConstant(Offset, DL, MVT::i32));
  return Addr;
}

SDValue CSKYTargetLowering::LowerExternalSymbol(SDValue Op,
                                                SelectionDAG &DAG) const {
  ExternalSymbolSDNode *N = cast<ExternalSymbolSDNode>(Op);

  return getAddr(N, DAG, false);
}

SDValue CSKYTargetLowering::LowerJumpTable(SDValue Op,
                                           SelectionDAG &DAG) const {
  JumpTableSDNode *N = cast<JumpTableSDNode>(Op);

  return getAddr<JumpTableSDNode, false>(N, DAG);
}

SDValue CSKYTargetLowering::LowerBlockAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  BlockAddressSDNode *N = cast<BlockAddressSDNode>(Op);

  return getAddr(N, DAG);
}

SDValue CSKYTargetLowering::LowerConstantPool(SDValue Op,
                                              SelectionDAG &DAG) const {
  assert(!Subtarget.hasE2());
  ConstantPoolSDNode *N = cast<ConstantPoolSDNode>(Op);

  return getAddr(N, DAG);
}

SDValue CSKYTargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  CSKYMachineFunctionInfo *FuncInfo = MF.getInfo<CSKYMachineFunctionInfo>();

  SDLoc DL(Op);
  SDValue FI = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(),
                                 getPointerTy(MF.getDataLayout()));

  // vastart just stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, FI, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue CSKYTargetLowering::LowerFRAMEADDR(SDValue Op,
                                           SelectionDAG &DAG) const {
  const CSKYRegisterInfo &RI = *Subtarget.getRegisterInfo();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned Depth = Op.getConstantOperandVal(0);
  Register FrameReg = RI.getFrameRegister(MF);
  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), dl, FrameReg, VT);
  while (Depth--)
    FrameAddr = DAG.getLoad(VT, dl, DAG.getEntryNode(), FrameAddr,
                            MachinePointerInfo());
  return FrameAddr;
}

SDValue CSKYTargetLowering::LowerRETURNADDR(SDValue Op,
                                            SelectionDAG &DAG) const {
  const CSKYRegisterInfo &RI = *Subtarget.getRegisterInfo();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned Depth = Op.getConstantOperandVal(0);
  if (Depth) {
    SDValue FrameAddr = LowerFRAMEADDR(Op, DAG);
    SDValue Offset = DAG.getConstant(4, dl, MVT::i32);
    return DAG.getLoad(VT, dl, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, dl, VT, FrameAddr, Offset),
                       MachinePointerInfo());
  }
  // Return the value of the return address register, marking it an implicit
  // live-in.
  unsigned Reg = MF.addLiveIn(RI.getRARegister(), getRegClassFor(MVT::i32));
  return DAG.getCopyFromReg(DAG.getEntryNode(), dl, Reg, VT);
}

Register CSKYTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  return CSKY::R0;
}

Register CSKYTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  return CSKY::R1;
}

SDValue CSKYTargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  int64_t Offset = N->getOffset();
  MVT XLenVT = MVT::i32;

  TLSModel::Model Model = getTargetMachine().getTLSModel(N->getGlobal());
  SDValue Addr;
  switch (Model) {
  case TLSModel::LocalExec:
    Addr = getStaticTLSAddr(N, DAG, /*UseGOT=*/false);
    break;
  case TLSModel::InitialExec:
    Addr = getStaticTLSAddr(N, DAG, /*UseGOT=*/true);
    break;
  case TLSModel::LocalDynamic:
  case TLSModel::GeneralDynamic:
    Addr = getDynamicTLSAddr(N, DAG);
    break;
  }

  // In order to maximise the opportunity for common subexpression elimination,
  // emit a separate ADD node for the global address offset instead of folding
  // it in the global address node. Later peephole optimisations may choose to
  // fold it back in when profitable.
  if (Offset != 0)
    return DAG.getNode(ISD::ADD, DL, Ty, Addr,
                       DAG.getConstant(Offset, DL, XLenVT));
  return Addr;
}

SDValue CSKYTargetLowering::getStaticTLSAddr(GlobalAddressSDNode *N,
                                             SelectionDAG &DAG,
                                             bool UseGOT) const {
  MachineFunction &MF = DAG.getMachineFunction();
  CSKYMachineFunctionInfo *CFI = MF.getInfo<CSKYMachineFunctionInfo>();

  unsigned CSKYPCLabelIndex = CFI->createPICLabelUId();

  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());

  CSKYCP::CSKYCPModifier Flag = UseGOT ? CSKYCP::TLSIE : CSKYCP::TLSLE;
  bool AddCurrentAddr = UseGOT ? true : false;
  unsigned char PCAjust = UseGOT ? 4 : 0;

  CSKYConstantPoolValue *CPV =
      CSKYConstantPoolConstant::Create(N->getGlobal(), CSKYCP::CPValue, PCAjust,
                                       Flag, AddCurrentAddr, CSKYPCLabelIndex);
  SDValue CAddr = DAG.getTargetConstantPool(CPV, Ty);

  SDValue Load;
  if (UseGOT) {
    SDValue PICLabel = DAG.getTargetConstant(CSKYPCLabelIndex, DL, MVT::i32);
    auto *LRWGRS = DAG.getMachineNode(CSKY::PseudoTLSLA32, DL, {Ty, Ty},
                                      {CAddr, PICLabel});
    auto LRWADDGRS =
        DAG.getNode(ISD::ADD, DL, Ty, SDValue(LRWGRS, 0), SDValue(LRWGRS, 1));
    Load = DAG.getLoad(Ty, DL, DAG.getEntryNode(), LRWADDGRS,
                       MachinePointerInfo(N->getGlobal()));
  } else {
    Load = SDValue(DAG.getMachineNode(CSKY::LRW32, DL, Ty, CAddr), 0);
  }

  // Add the thread pointer.
  SDValue TPReg = DAG.getRegister(CSKY::R31, MVT::i32);
  return DAG.getNode(ISD::ADD, DL, Ty, Load, TPReg);
}

SDValue CSKYTargetLowering::getDynamicTLSAddr(GlobalAddressSDNode *N,
                                              SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  CSKYMachineFunctionInfo *CFI = MF.getInfo<CSKYMachineFunctionInfo>();

  unsigned CSKYPCLabelIndex = CFI->createPICLabelUId();

  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());
  IntegerType *CallTy = Type::getIntNTy(*DAG.getContext(), Ty.getSizeInBits());

  CSKYConstantPoolValue *CPV =
      CSKYConstantPoolConstant::Create(N->getGlobal(), CSKYCP::CPValue, 4,
                                       CSKYCP::TLSGD, true, CSKYPCLabelIndex);
  SDValue Addr = DAG.getTargetConstantPool(CPV, Ty);
  SDValue PICLabel = DAG.getTargetConstant(CSKYPCLabelIndex, DL, MVT::i32);

  auto *LRWGRS =
      DAG.getMachineNode(CSKY::PseudoTLSLA32, DL, {Ty, Ty}, {Addr, PICLabel});

  auto Load =
      DAG.getNode(ISD::ADD, DL, Ty, SDValue(LRWGRS, 0), SDValue(LRWGRS, 1));

  // Prepare argument list to generate call.
  ArgListTy Args;
  ArgListEntry Entry;
  Entry.Node = Load;
  Entry.Ty = CallTy;
  Args.push_back(Entry);

  // Setup call to __tls_get_addr.
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::C, CallTy,
                    DAG.getExternalSymbol("__tls_get_addr", Ty),
                    std::move(Args));
  SDValue V = LowerCallTo(CLI).first;

  return V;
}

bool CSKYTargetLowering::decomposeMulByConstant(LLVMContext &Context, EVT VT,
                                                SDValue C) const {
  if (!VT.isScalarInteger())
    return false;

  // Omit if data size exceeds.
  if (VT.getSizeInBits() > Subtarget.XLen)
    return false;

  if (auto *ConstNode = dyn_cast<ConstantSDNode>(C.getNode())) {
    const APInt &Imm = ConstNode->getAPIntValue();
    // Break MULT to LSLI + ADDU/SUBU.
    if ((Imm + 1).isPowerOf2() || (Imm - 1).isPowerOf2() ||
        (1 - Imm).isPowerOf2())
      return true;
    // Only break MULT for sub targets without MULT32, since an extra
    // instruction will be generated against the above 3 cases. We leave it
    // unchanged on sub targets with MULT32, since not sure it is better.
    if (!Subtarget.hasE2() && (-1 - Imm).isPowerOf2())
      return true;
    // Break (MULT x, imm) to ([IXH32|IXW32|IXD32] (LSLI32 x, i0), x) when
    // imm=(1<<i0)+[2|4|8] and imm has to be composed via a MOVIH32/ORI32 pair.
    if (Imm.ugt(0xffff) && ((Imm - 2).isPowerOf2() || (Imm - 4).isPowerOf2()) &&
        Subtarget.hasE2())
      return true;
    if (Imm.ugt(0xffff) && (Imm - 8).isPowerOf2() && Subtarget.has2E3())
      return true;
  }

  return false;
}

bool CSKYTargetLowering::isCheapToSpeculateCttz(Type *Ty) const {
  return Subtarget.has2E3();
}

bool CSKYTargetLowering::isCheapToSpeculateCtlz(Type *Ty) const {
  return Subtarget.hasE2();
}
