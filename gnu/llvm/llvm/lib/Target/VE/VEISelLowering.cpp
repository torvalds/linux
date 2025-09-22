//===-- VEISelLowering.cpp - VE DAG Lowering Implementation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the interfaces that VE uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "VEISelLowering.h"
#include "MCTargetDesc/VEMCExpr.h"
#include "VECustomDAG.h"
#include "VEInstrBuilder.h"
#include "VEMachineFunctionInfo.h"
#include "VERegisterInfo.h"
#include "VETargetMachine.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
using namespace llvm;

#define DEBUG_TYPE "ve-lower"

//===----------------------------------------------------------------------===//
// Calling Convention Implementation
//===----------------------------------------------------------------------===//

#include "VEGenCallingConv.inc"

CCAssignFn *getReturnCC(CallingConv::ID CallConv) {
  switch (CallConv) {
  default:
    return RetCC_VE_C;
  case CallingConv::Fast:
    return RetCC_VE_Fast;
  }
}

CCAssignFn *getParamCC(CallingConv::ID CallConv, bool IsVarArg) {
  if (IsVarArg)
    return CC_VE2;
  switch (CallConv) {
  default:
    return CC_VE_C;
  case CallingConv::Fast:
    return CC_VE_Fast;
  }
}

bool VETargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context) const {
  CCAssignFn *RetCC = getReturnCC(CallConv);
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC);
}

static const MVT AllVectorVTs[] = {MVT::v256i32, MVT::v512i32, MVT::v256i64,
                                   MVT::v256f32, MVT::v512f32, MVT::v256f64};

static const MVT AllMaskVTs[] = {MVT::v256i1, MVT::v512i1};

static const MVT AllPackedVTs[] = {MVT::v512i32, MVT::v512f32};

void VETargetLowering::initRegisterClasses() {
  // Set up the register classes.
  addRegisterClass(MVT::i32, &VE::I32RegClass);
  addRegisterClass(MVT::i64, &VE::I64RegClass);
  addRegisterClass(MVT::f32, &VE::F32RegClass);
  addRegisterClass(MVT::f64, &VE::I64RegClass);
  addRegisterClass(MVT::f128, &VE::F128RegClass);

  if (Subtarget->enableVPU()) {
    for (MVT VecVT : AllVectorVTs)
      addRegisterClass(VecVT, &VE::V64RegClass);
    addRegisterClass(MVT::v256i1, &VE::VMRegClass);
    addRegisterClass(MVT::v512i1, &VE::VM512RegClass);
  }
}

void VETargetLowering::initSPUActions() {
  const auto &TM = getTargetMachine();
  /// Load & Store {

  // VE doesn't have i1 sign extending load.
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setTruncStoreAction(VT, MVT::i1, Expand);
  }

  // VE doesn't have floating point extload/truncstore, so expand them.
  for (MVT FPVT : MVT::fp_valuetypes()) {
    for (MVT OtherFPVT : MVT::fp_valuetypes()) {
      setLoadExtAction(ISD::EXTLOAD, FPVT, OtherFPVT, Expand);
      setTruncStoreAction(FPVT, OtherFPVT, Expand);
    }
  }

  // VE doesn't have fp128 load/store, so expand them in custom lower.
  setOperationAction(ISD::LOAD, MVT::f128, Custom);
  setOperationAction(ISD::STORE, MVT::f128, Custom);

  /// } Load & Store

  // Custom legalize address nodes into LO/HI parts.
  MVT PtrVT = MVT::getIntegerVT(TM.getPointerSizeInBits(0));
  setOperationAction(ISD::BlockAddress, PtrVT, Custom);
  setOperationAction(ISD::GlobalAddress, PtrVT, Custom);
  setOperationAction(ISD::GlobalTLSAddress, PtrVT, Custom);
  setOperationAction(ISD::ConstantPool, PtrVT, Custom);
  setOperationAction(ISD::JumpTable, PtrVT, Custom);

  /// VAARG handling {
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  // VAARG needs to be lowered to access with 8 bytes alignment.
  setOperationAction(ISD::VAARG, MVT::Other, Custom);
  // Use the default implementation.
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  /// } VAARG handling

  /// Stack {
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);

  // Use the default implementation.
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  /// } Stack

  /// Branch {

  // VE doesn't have BRCOND
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  // BR_JT is not implemented yet.
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  /// } Branch

  /// Int Ops {
  for (MVT IntVT : {MVT::i32, MVT::i64}) {
    // VE has no REM or DIVREM operations.
    setOperationAction(ISD::UREM, IntVT, Expand);
    setOperationAction(ISD::SREM, IntVT, Expand);
    setOperationAction(ISD::SDIVREM, IntVT, Expand);
    setOperationAction(ISD::UDIVREM, IntVT, Expand);

    // VE has no SHL_PARTS/SRA_PARTS/SRL_PARTS operations.
    setOperationAction(ISD::SHL_PARTS, IntVT, Expand);
    setOperationAction(ISD::SRA_PARTS, IntVT, Expand);
    setOperationAction(ISD::SRL_PARTS, IntVT, Expand);

    // VE has no MULHU/S or U/SMUL_LOHI operations.
    // TODO: Use MPD instruction to implement SMUL_LOHI for i32 type.
    setOperationAction(ISD::MULHU, IntVT, Expand);
    setOperationAction(ISD::MULHS, IntVT, Expand);
    setOperationAction(ISD::UMUL_LOHI, IntVT, Expand);
    setOperationAction(ISD::SMUL_LOHI, IntVT, Expand);

    // VE has no CTTZ, ROTL, ROTR operations.
    setOperationAction(ISD::CTTZ, IntVT, Expand);
    setOperationAction(ISD::ROTL, IntVT, Expand);
    setOperationAction(ISD::ROTR, IntVT, Expand);

    // VE has 64 bits instruction which works as i64 BSWAP operation.  This
    // instruction works fine as i32 BSWAP operation with an additional
    // parameter.  Use isel patterns to lower BSWAP.
    setOperationAction(ISD::BSWAP, IntVT, Legal);

    // VE has only 64 bits instructions which work as i64 BITREVERSE/CTLZ/CTPOP
    // operations.  Use isel patterns for i64, promote for i32.
    LegalizeAction Act = (IntVT == MVT::i32) ? Promote : Legal;
    setOperationAction(ISD::BITREVERSE, IntVT, Act);
    setOperationAction(ISD::CTLZ, IntVT, Act);
    setOperationAction(ISD::CTLZ_ZERO_UNDEF, IntVT, Act);
    setOperationAction(ISD::CTPOP, IntVT, Act);

    // VE has only 64 bits instructions which work as i64 AND/OR/XOR operations.
    // Use isel patterns for i64, promote for i32.
    setOperationAction(ISD::AND, IntVT, Act);
    setOperationAction(ISD::OR, IntVT, Act);
    setOperationAction(ISD::XOR, IntVT, Act);

    // Legal smax and smin
    setOperationAction(ISD::SMAX, IntVT, Legal);
    setOperationAction(ISD::SMIN, IntVT, Legal);
  }
  /// } Int Ops

  /// Conversion {
  // VE doesn't have instructions for fp<->uint, so expand them by llvm
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Promote); // use i64
  setOperationAction(ISD::UINT_TO_FP, MVT::i32, Promote); // use i64
  setOperationAction(ISD::FP_TO_UINT, MVT::i64, Expand);
  setOperationAction(ISD::UINT_TO_FP, MVT::i64, Expand);

  // fp16 not supported
  for (MVT FPVT : MVT::fp_valuetypes()) {
    setOperationAction(ISD::FP16_TO_FP, FPVT, Expand);
    setOperationAction(ISD::FP_TO_FP16, FPVT, Expand);
  }
  /// } Conversion

  /// Floating-point Ops {
  /// Note: Floating-point operations are fneg, fadd, fsub, fmul, fdiv, frem,
  ///       and fcmp.

  // VE doesn't have following floating point operations.
  for (MVT VT : MVT::fp_valuetypes()) {
    setOperationAction(ISD::FNEG, VT, Expand);
    setOperationAction(ISD::FREM, VT, Expand);
  }

  // VE doesn't have fdiv of f128.
  setOperationAction(ISD::FDIV, MVT::f128, Expand);

  for (MVT FPVT : {MVT::f32, MVT::f64}) {
    // f32 and f64 uses ConstantFP.  f128 uses ConstantPool.
    setOperationAction(ISD::ConstantFP, FPVT, Legal);
  }
  /// } Floating-point Ops

  /// Floating-point math functions {

  // VE doesn't have following floating point math functions.
  for (MVT VT : MVT::fp_valuetypes()) {
    setOperationAction(ISD::FABS, VT, Expand);
    setOperationAction(ISD::FCOPYSIGN, VT, Expand);
    setOperationAction(ISD::FCOS, VT, Expand);
    setOperationAction(ISD::FMA, VT, Expand);
    setOperationAction(ISD::FPOW, VT, Expand);
    setOperationAction(ISD::FSIN, VT, Expand);
    setOperationAction(ISD::FSQRT, VT, Expand);
  }

  // VE has single and double FMINNUM and FMAXNUM
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setOperationAction({ISD::FMAXNUM, ISD::FMINNUM}, VT, Legal);
  }

  /// } Floating-point math functions

  /// Atomic instructions {

  setMaxAtomicSizeInBitsSupported(64);
  setMinCmpXchgSizeInBits(32);
  setSupportsUnalignedAtomics(false);

  // Use custom inserter for ATOMIC_FENCE.
  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Custom);

  // Other atomic instructions.
  for (MVT VT : MVT::integer_valuetypes()) {
    // Support i8/i16 atomic swap.
    setOperationAction(ISD::ATOMIC_SWAP, VT, Custom);

    // FIXME: Support "atmam" instructions.
    setOperationAction(ISD::ATOMIC_LOAD_ADD, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_SUB, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_AND, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_OR, VT, Expand);

    // VE doesn't have follwing instructions.
    setOperationAction(ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_CLR, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_XOR, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_NAND, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_MIN, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_MAX, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_UMIN, VT, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_UMAX, VT, Expand);
  }

  /// } Atomic instructions

  /// SJLJ instructions {
  setOperationAction(ISD::EH_SJLJ_LONGJMP, MVT::Other, Custom);
  setOperationAction(ISD::EH_SJLJ_SETJMP, MVT::i32, Custom);
  setOperationAction(ISD::EH_SJLJ_SETUP_DISPATCH, MVT::Other, Custom);
  if (TM.Options.ExceptionModel == ExceptionHandling::SjLj)
    setLibcallName(RTLIB::UNWIND_RESUME, "_Unwind_SjLj_Resume");
  /// } SJLJ instructions

  // Intrinsic instructions
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);
}

void VETargetLowering::initVPUActions() {
  for (MVT LegalMaskVT : AllMaskVTs)
    setOperationAction(ISD::BUILD_VECTOR, LegalMaskVT, Custom);

  for (unsigned Opc : {ISD::AND, ISD::OR, ISD::XOR})
    setOperationAction(Opc, MVT::v512i1, Custom);

  for (MVT LegalVecVT : AllVectorVTs) {
    setOperationAction(ISD::BUILD_VECTOR, LegalVecVT, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT, LegalVecVT, Legal);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, LegalVecVT, Legal);
    // Translate all vector instructions with legal element types to VVP_*
    // nodes.
    // TODO We will custom-widen into VVP_* nodes in the future. While we are
    // buildling the infrastructure for this, we only do this for legal vector
    // VTs.
#define HANDLE_VP_TO_VVP(VP_OPC, VVP_NAME)                                     \
  setOperationAction(ISD::VP_OPC, LegalVecVT, Custom);
#define ADD_VVP_OP(VVP_NAME, ISD_NAME)                                         \
  setOperationAction(ISD::ISD_NAME, LegalVecVT, Custom);
    setOperationAction(ISD::EXPERIMENTAL_VP_STRIDED_LOAD, LegalVecVT, Custom);
    setOperationAction(ISD::EXPERIMENTAL_VP_STRIDED_STORE, LegalVecVT, Custom);
#include "VVPNodes.def"
  }

  for (MVT LegalPackedVT : AllPackedVTs) {
    setOperationAction(ISD::INSERT_VECTOR_ELT, LegalPackedVT, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, LegalPackedVT, Custom);
  }

  // vNt32, vNt64 ops (legal element types)
  for (MVT VT : MVT::vector_valuetypes()) {
    MVT ElemVT = VT.getVectorElementType();
    unsigned ElemBits = ElemVT.getScalarSizeInBits();
    if (ElemBits != 32 && ElemBits != 64)
      continue;

    for (unsigned MemOpc : {ISD::MLOAD, ISD::MSTORE, ISD::LOAD, ISD::STORE})
      setOperationAction(MemOpc, VT, Custom);

    const ISD::NodeType IntReductionOCs[] = {
        ISD::VECREDUCE_ADD,  ISD::VECREDUCE_MUL,  ISD::VECREDUCE_AND,
        ISD::VECREDUCE_OR,   ISD::VECREDUCE_XOR,  ISD::VECREDUCE_SMIN,
        ISD::VECREDUCE_SMAX, ISD::VECREDUCE_UMIN, ISD::VECREDUCE_UMAX};

    for (unsigned IntRedOpc : IntReductionOCs)
      setOperationAction(IntRedOpc, VT, Custom);
  }

  // v256i1 and v512i1 ops
  for (MVT MaskVT : AllMaskVTs) {
    // Custom lower mask ops
    setOperationAction(ISD::STORE, MaskVT, Custom);
    setOperationAction(ISD::LOAD, MaskVT, Custom);
  }
}

SDValue
VETargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                              bool IsVarArg,
                              const SmallVectorImpl<ISD::OutputArg> &Outs,
                              const SmallVectorImpl<SDValue> &OutVals,
                              const SDLoc &DL, SelectionDAG &DAG) const {
  // CCValAssign - represent the assignment of the return value to locations.
  SmallVector<CCValAssign, 16> RVLocs;

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Analyze return values.
  CCInfo.AnalyzeReturn(Outs, getReturnCC(CallConv));

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");
    assert(!VA.needsCustom() && "Unexpected custom lowering");
    SDValue OutVal = OutVals[i];

    // Integer return values must be sign or zero extended by the callee.
    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      OutVal = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), OutVal);
      break;
    case CCValAssign::ZExt:
      OutVal = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), OutVal);
      break;
    case CCValAssign::AExt:
      OutVal = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), OutVal);
      break;
    case CCValAssign::BCvt: {
      // Convert a float return value to i64 with padding.
      //     63     31   0
      //    +------+------+
      //    | float|   0  |
      //    +------+------+
      assert(VA.getLocVT() == MVT::i64);
      assert(VA.getValVT() == MVT::f32);
      SDValue Undef = SDValue(
          DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF, DL, MVT::i64), 0);
      SDValue Sub_f32 = DAG.getTargetConstant(VE::sub_f32, DL, MVT::i32);
      OutVal = SDValue(DAG.getMachineNode(TargetOpcode::INSERT_SUBREG, DL,
                                          MVT::i64, Undef, OutVal, Sub_f32),
                       0);
      break;
    }
    default:
      llvm_unreachable("Unknown loc info!");
    }

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVal, Glue);

    // Guarantee that all emitted copies are stuck together with flags.
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain; // Update chain.

  // Add the glue if we have it.
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(VEISD::RET_GLUE, DL, MVT::Other, RetOps);
}

SDValue VETargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();

  // Get the base offset of the incoming arguments stack space.
  unsigned ArgsBaseOffset = Subtarget->getRsaSize();
  // Get the size of the preserved arguments area
  unsigned ArgsPreserved = 64;

  // Analyze arguments according to CC_VE.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  // Allocate the preserved area first.
  CCInfo.AllocateStack(ArgsPreserved, Align(8));
  // We already allocated the preserved area, so the stack offset computed
  // by CC_VE would be correct now.
  CCInfo.AnalyzeFormalArguments(Ins, getParamCC(CallConv, false));

  for (const CCValAssign &VA : ArgLocs) {
    assert(!VA.needsCustom() && "Unexpected custom lowering");
    if (VA.isRegLoc()) {
      // This argument is passed in a register.
      // All integer register arguments are promoted by the caller to i64.

      // Create a virtual register for the promoted live-in value.
      Register VReg =
          MF.addLiveIn(VA.getLocReg(), getRegClassFor(VA.getLocVT()));
      SDValue Arg = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());

      // The caller promoted the argument, so insert an Assert?ext SDNode so we
      // won't promote the value again in this function.
      switch (VA.getLocInfo()) {
      case CCValAssign::SExt:
        Arg = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), Arg,
                          DAG.getValueType(VA.getValVT()));
        break;
      case CCValAssign::ZExt:
        Arg = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), Arg,
                          DAG.getValueType(VA.getValVT()));
        break;
      case CCValAssign::BCvt: {
        // Extract a float argument from i64 with padding.
        //     63     31   0
        //    +------+------+
        //    | float|   0  |
        //    +------+------+
        assert(VA.getLocVT() == MVT::i64);
        assert(VA.getValVT() == MVT::f32);
        SDValue Sub_f32 = DAG.getTargetConstant(VE::sub_f32, DL, MVT::i32);
        Arg = SDValue(DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG, DL,
                                         MVT::f32, Arg, Sub_f32),
                      0);
        break;
      }
      default:
        break;
      }

      // Truncate the register down to the argument type.
      if (VA.isExtInLoc())
        Arg = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Arg);

      InVals.push_back(Arg);
      continue;
    }

    // The registers are exhausted. This argument was passed on the stack.
    assert(VA.isMemLoc());
    // The CC_VE_Full/Half functions compute stack offsets relative to the
    // beginning of the arguments area at %fp + the size of reserved area.
    unsigned Offset = VA.getLocMemOffset() + ArgsBaseOffset;
    unsigned ValSize = VA.getValVT().getSizeInBits() / 8;

    // Adjust offset for a float argument by adding 4 since the argument is
    // stored in 8 bytes buffer with offset like below.  LLVM generates
    // 4 bytes load instruction, so need to adjust offset here.  This
    // adjustment is required in only LowerFormalArguments.  In LowerCall,
    // a float argument is converted to i64 first, and stored as 8 bytes
    // data, which is required by ABI, so no need for adjustment.
    //    0      4
    //    +------+------+
    //    | empty| float|
    //    +------+------+
    if (VA.getValVT() == MVT::f32)
      Offset += 4;

    int FI = MF.getFrameInfo().CreateFixedObject(ValSize, Offset, true);
    InVals.push_back(
        DAG.getLoad(VA.getValVT(), DL, Chain,
                    DAG.getFrameIndex(FI, getPointerTy(MF.getDataLayout())),
                    MachinePointerInfo::getFixedStack(MF, FI)));
  }

  if (!IsVarArg)
    return Chain;

  // This function takes variable arguments, some of which may have been passed
  // in registers %s0-%s8.
  //
  // The va_start intrinsic needs to know the offset to the first variable
  // argument.
  // TODO: need to calculate offset correctly once we support f128.
  unsigned ArgOffset = ArgLocs.size() * 8;
  VEMachineFunctionInfo *FuncInfo = MF.getInfo<VEMachineFunctionInfo>();
  // Skip the reserved area at the top of stack.
  FuncInfo->setVarArgsFrameOffset(ArgOffset + ArgsBaseOffset);

  return Chain;
}

// FIXME? Maybe this could be a TableGen attribute on some registers and
// this table could be generated automatically from RegInfo.
Register VETargetLowering::getRegisterByName(const char *RegName, LLT VT,
                                             const MachineFunction &MF) const {
  Register Reg = StringSwitch<Register>(RegName)
                     .Case("sp", VE::SX11)    // Stack pointer
                     .Case("fp", VE::SX9)     // Frame pointer
                     .Case("sl", VE::SX8)     // Stack limit
                     .Case("lr", VE::SX10)    // Link register
                     .Case("tp", VE::SX14)    // Thread pointer
                     .Case("outer", VE::SX12) // Outer regiser
                     .Case("info", VE::SX17)  // Info area register
                     .Case("got", VE::SX15)   // Global offset table register
                     .Case("plt", VE::SX16) // Procedure linkage table register
                     .Default(0);

  if (Reg)
    return Reg;

  report_fatal_error("Invalid register name global variable");
}

//===----------------------------------------------------------------------===//
// TargetLowering Implementation
//===----------------------------------------------------------------------===//

SDValue VETargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                    SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  // VE target does not yet support tail call optimization.
  CLI.IsTailCall = false;

  // Get the base offset of the outgoing arguments stack space.
  unsigned ArgsBaseOffset = Subtarget->getRsaSize();
  // Get the size of the preserved arguments area
  unsigned ArgsPreserved = 8 * 8u;

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CLI.CallConv, CLI.IsVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  // Allocate the preserved area first.
  CCInfo.AllocateStack(ArgsPreserved, Align(8));
  // We already allocated the preserved area, so the stack offset computed
  // by CC_VE would be correct now.
  CCInfo.AnalyzeCallOperands(CLI.Outs, getParamCC(CLI.CallConv, false));

  // VE requires to use both register and stack for varargs or no-prototyped
  // functions.
  bool UseBoth = CLI.IsVarArg;

  // Analyze operands again if it is required to store BOTH.
  SmallVector<CCValAssign, 16> ArgLocs2;
  CCState CCInfo2(CLI.CallConv, CLI.IsVarArg, DAG.getMachineFunction(),
                  ArgLocs2, *DAG.getContext());
  if (UseBoth)
    CCInfo2.AnalyzeCallOperands(CLI.Outs, getParamCC(CLI.CallConv, true));

  // Get the size of the outgoing arguments stack space requirement.
  unsigned ArgsSize = CCInfo.getStackSize();

  // Keep stack frames 16-byte aligned.
  ArgsSize = alignTo(ArgsSize, 16);

  // Adjust the stack pointer to make room for the arguments.
  // FIXME: Use hasReservedCallFrame to avoid %sp adjustments around all calls
  // with more than 6 arguments.
  Chain = DAG.getCALLSEQ_START(Chain, ArgsSize, 0, DL);

  // Collect the set of registers to pass to the function and their values.
  // This will be emitted as a sequence of CopyToReg nodes glued to the call
  // instruction.
  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;

  // Collect chains from all the memory opeations that copy arguments to the
  // stack. They must follow the stack pointer adjustment above and precede the
  // call instruction itself.
  SmallVector<SDValue, 8> MemOpChains;

  // VE needs to get address of callee function in a register
  // So, prepare to copy it to SX12 here.

  // If the callee is a GlobalAddress node (quite common, every direct call is)
  // turn it into a TargetGlobalAddress node so that legalize doesn't hack it.
  // Likewise ExternalSymbol -> TargetExternalSymbol.
  SDValue Callee = CLI.Callee;

  bool IsPICCall = isPositionIndependent();

  // PC-relative references to external symbols should go through $stub.
  // If so, we need to prepare GlobalBaseReg first.
  const TargetMachine &TM = DAG.getTarget();
  const GlobalValue *GV = nullptr;
  auto *CalleeG = dyn_cast<GlobalAddressSDNode>(Callee);
  if (CalleeG)
    GV = CalleeG->getGlobal();
  bool Local = TM.shouldAssumeDSOLocal(GV);
  bool UsePlt = !Local;
  MachineFunction &MF = DAG.getMachineFunction();

  // Turn GlobalAddress/ExternalSymbol node into a value node
  // containing the address of them here.
  if (CalleeG) {
    if (IsPICCall) {
      if (UsePlt)
        Subtarget->getInstrInfo()->getGlobalBaseReg(&MF);
      Callee = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, 0);
      Callee = DAG.getNode(VEISD::GETFUNPLT, DL, PtrVT, Callee);
    } else {
      Callee =
          makeHiLoPair(Callee, VEMCExpr::VK_VE_HI32, VEMCExpr::VK_VE_LO32, DAG);
    }
  } else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    if (IsPICCall) {
      if (UsePlt)
        Subtarget->getInstrInfo()->getGlobalBaseReg(&MF);
      Callee = DAG.getTargetExternalSymbol(E->getSymbol(), PtrVT, 0);
      Callee = DAG.getNode(VEISD::GETFUNPLT, DL, PtrVT, Callee);
    } else {
      Callee =
          makeHiLoPair(Callee, VEMCExpr::VK_VE_HI32, VEMCExpr::VK_VE_LO32, DAG);
    }
  }

  RegsToPass.push_back(std::make_pair(VE::SX12, Callee));

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = CLI.OutVals[i];

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default:
      llvm_unreachable("Unknown location info!");
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::BCvt: {
      // Convert a float argument to i64 with padding.
      //     63     31   0
      //    +------+------+
      //    | float|   0  |
      //    +------+------+
      assert(VA.getLocVT() == MVT::i64);
      assert(VA.getValVT() == MVT::f32);
      SDValue Undef = SDValue(
          DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF, DL, MVT::i64), 0);
      SDValue Sub_f32 = DAG.getTargetConstant(VE::sub_f32, DL, MVT::i32);
      Arg = SDValue(DAG.getMachineNode(TargetOpcode::INSERT_SUBREG, DL,
                                       MVT::i64, Undef, Arg, Sub_f32),
                    0);
      break;
    }
    }

    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
      if (!UseBoth)
        continue;
      VA = ArgLocs2[i];
    }

    assert(VA.isMemLoc());

    // Create a store off the stack pointer for this argument.
    SDValue StackPtr = DAG.getRegister(VE::SX11, PtrVT);
    // The argument area starts at %fp/%sp + the size of reserved area.
    SDValue PtrOff =
        DAG.getIntPtrConstant(VA.getLocMemOffset() + ArgsBaseOffset, DL);
    PtrOff = DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr, PtrOff);
    MemOpChains.push_back(
        DAG.getStore(Chain, DL, Arg, PtrOff, MachinePointerInfo()));
  }

  // Emit all stores, make sure they occur before the call.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  // Build a sequence of CopyToReg nodes glued together with token chain and
  // glue operands which copy the outgoing args into registers. The InGlue is
  // necessary since all emitted instructions must be stuck together in order
  // to pass the live physical registers.
  SDValue InGlue;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, DL, RegsToPass[i].first,
                             RegsToPass[i].second, InGlue);
    InGlue = Chain.getValue(1);
  }

  // Build the operands for the call instruction itself.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const VERegisterInfo *TRI = Subtarget->getRegisterInfo();
  const uint32_t *Mask =
      TRI->getCallPreservedMask(DAG.getMachineFunction(), CLI.CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  // Make sure the CopyToReg nodes are glued to the call instruction which
  // consumes the registers.
  if (InGlue.getNode())
    Ops.push_back(InGlue);

  // Now the call itself.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(VEISD::CALL, DL, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  // Revert the stack pointer immediately after the call.
  Chain = DAG.getCALLSEQ_END(Chain, ArgsSize, 0, InGlue, DL);
  InGlue = Chain.getValue(1);

  // Now extract the return values. This is more or less the same as
  // LowerFormalArguments.

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState RVInfo(CLI.CallConv, CLI.IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Set inreg flag manually for codegen generated library calls that
  // return float.
  if (CLI.Ins.size() == 1 && CLI.Ins[0].VT == MVT::f32 && !CLI.CB)
    CLI.Ins[0].Flags.setInReg();

  RVInfo.AnalyzeCallResult(CLI.Ins, getReturnCC(CLI.CallConv));

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(!VA.needsCustom() && "Unexpected custom lowering");
    Register Reg = VA.getLocReg();

    // When returning 'inreg {i32, i32 }', two consecutive i32 arguments can
    // reside in the same register in the high and low bits. Reuse the
    // CopyFromReg previous node to avoid duplicate copies.
    SDValue RV;
    if (RegisterSDNode *SrcReg = dyn_cast<RegisterSDNode>(Chain.getOperand(1)))
      if (SrcReg->getReg() == Reg && Chain->getOpcode() == ISD::CopyFromReg)
        RV = Chain.getValue(0);

    // But usually we'll create a new CopyFromReg for a different register.
    if (!RV.getNode()) {
      RV = DAG.getCopyFromReg(Chain, DL, Reg, RVLocs[i].getLocVT(), InGlue);
      Chain = RV.getValue(1);
      InGlue = Chain.getValue(2);
    }

    // The callee promoted the return value, so insert an Assert?ext SDNode so
    // we won't promote the value again in this function.
    switch (VA.getLocInfo()) {
    case CCValAssign::SExt:
      RV = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), RV,
                       DAG.getValueType(VA.getValVT()));
      break;
    case CCValAssign::ZExt:
      RV = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), RV,
                       DAG.getValueType(VA.getValVT()));
      break;
    case CCValAssign::BCvt: {
      // Extract a float return value from i64 with padding.
      //     63     31   0
      //    +------+------+
      //    | float|   0  |
      //    +------+------+
      assert(VA.getLocVT() == MVT::i64);
      assert(VA.getValVT() == MVT::f32);
      SDValue Sub_f32 = DAG.getTargetConstant(VE::sub_f32, DL, MVT::i32);
      RV = SDValue(DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG, DL,
                                      MVT::f32, RV, Sub_f32),
                   0);
      break;
    }
    default:
      break;
    }

    // Truncate the register down to the return value type.
    if (VA.isExtInLoc())
      RV = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), RV);

    InVals.push_back(RV);
  }

  return Chain;
}

bool VETargetLowering::isOffsetFoldingLegal(
    const GlobalAddressSDNode *GA) const {
  // VE uses 64 bit addressing, so we need multiple instructions to generate
  // an address.  Folding address with offset increases the number of
  // instructions, so that we disable it here.  Offsets will be folded in
  // the DAG combine later if it worth to do so.
  return false;
}

/// isFPImmLegal - Returns true if the target can instruction select the
/// specified FP immediate natively. If false, the legalizer will
/// materialize the FP immediate as a load from a constant pool.
bool VETargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT,
                                    bool ForCodeSize) const {
  return VT == MVT::f32 || VT == MVT::f64;
}

/// Determine if the target supports unaligned memory accesses.
///
/// This function returns true if the target allows unaligned memory accesses
/// of the specified type in the given address space. If true, it also returns
/// whether the unaligned memory access is "fast" in the last argument by
/// reference. This is used, for example, in situations where an array
/// copy/move/set is converted to a sequence of store operations. Its use
/// helps to ensure that such replacements don't generate code that causes an
/// alignment error (trap) on the target machine.
bool VETargetLowering::allowsMisalignedMemoryAccesses(EVT VT,
                                                      unsigned AddrSpace,
                                                      Align A,
                                                      MachineMemOperand::Flags,
                                                      unsigned *Fast) const {
  if (Fast) {
    // It's fast anytime on VE
    *Fast = 1;
  }
  return true;
}

VETargetLowering::VETargetLowering(const TargetMachine &TM,
                                   const VESubtarget &STI)
    : TargetLowering(TM), Subtarget(&STI) {
  // Instructions which use registers as conditionals examine all the
  // bits (as does the pseudo SELECT_CC expansion). I don't think it
  // matters much whether it's ZeroOrOneBooleanContent, or
  // ZeroOrNegativeOneBooleanContent, so, arbitrarily choose the
  // former.
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);

  initRegisterClasses();
  initSPUActions();
  initVPUActions();

  setStackPointerRegisterToSaveRestore(VE::SX11);

  // We have target-specific dag combine patterns for the following nodes:
  setTargetDAGCombine(ISD::TRUNCATE);
  setTargetDAGCombine(ISD::SELECT);
  setTargetDAGCombine(ISD::SELECT_CC);

  // Set function alignment to 16 bytes
  setMinFunctionAlignment(Align(16));

  // VE stores all argument by 8 bytes alignment
  setMinStackArgumentAlignment(Align(8));

  computeRegisterProperties(Subtarget->getRegisterInfo());
}

const char *VETargetLowering::getTargetNodeName(unsigned Opcode) const {
#define TARGET_NODE_CASE(NAME)                                                 \
  case VEISD::NAME:                                                            \
    return "VEISD::" #NAME;
  switch ((VEISD::NodeType)Opcode) {
  case VEISD::FIRST_NUMBER:
    break;
    TARGET_NODE_CASE(CMPI)
    TARGET_NODE_CASE(CMPU)
    TARGET_NODE_CASE(CMPF)
    TARGET_NODE_CASE(CMPQ)
    TARGET_NODE_CASE(CMOV)
    TARGET_NODE_CASE(CALL)
    TARGET_NODE_CASE(EH_SJLJ_LONGJMP)
    TARGET_NODE_CASE(EH_SJLJ_SETJMP)
    TARGET_NODE_CASE(EH_SJLJ_SETUP_DISPATCH)
    TARGET_NODE_CASE(GETFUNPLT)
    TARGET_NODE_CASE(GETSTACKTOP)
    TARGET_NODE_CASE(GETTLSADDR)
    TARGET_NODE_CASE(GLOBAL_BASE_REG)
    TARGET_NODE_CASE(Hi)
    TARGET_NODE_CASE(Lo)
    TARGET_NODE_CASE(RET_GLUE)
    TARGET_NODE_CASE(TS1AM)
    TARGET_NODE_CASE(VEC_UNPACK_LO)
    TARGET_NODE_CASE(VEC_UNPACK_HI)
    TARGET_NODE_CASE(VEC_PACK)
    TARGET_NODE_CASE(VEC_BROADCAST)
    TARGET_NODE_CASE(REPL_I32)
    TARGET_NODE_CASE(REPL_F32)

    TARGET_NODE_CASE(LEGALAVL)

    // Register the VVP_* SDNodes.
#define ADD_VVP_OP(VVP_NAME, ...) TARGET_NODE_CASE(VVP_NAME)
#include "VVPNodes.def"
  }
#undef TARGET_NODE_CASE
  return nullptr;
}

EVT VETargetLowering::getSetCCResultType(const DataLayout &, LLVMContext &,
                                         EVT VT) const {
  return MVT::i32;
}

// Convert to a target node and set target flags.
SDValue VETargetLowering::withTargetFlags(SDValue Op, unsigned TF,
                                          SelectionDAG &DAG) const {
  if (const GlobalAddressSDNode *GA = dyn_cast<GlobalAddressSDNode>(Op))
    return DAG.getTargetGlobalAddress(GA->getGlobal(), SDLoc(GA),
                                      GA->getValueType(0), GA->getOffset(), TF);

  if (const BlockAddressSDNode *BA = dyn_cast<BlockAddressSDNode>(Op))
    return DAG.getTargetBlockAddress(BA->getBlockAddress(), Op.getValueType(),
                                     0, TF);

  if (const ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(Op))
    return DAG.getTargetConstantPool(CP->getConstVal(), CP->getValueType(0),
                                     CP->getAlign(), CP->getOffset(), TF);

  if (const ExternalSymbolSDNode *ES = dyn_cast<ExternalSymbolSDNode>(Op))
    return DAG.getTargetExternalSymbol(ES->getSymbol(), ES->getValueType(0),
                                       TF);

  if (const JumpTableSDNode *JT = dyn_cast<JumpTableSDNode>(Op))
    return DAG.getTargetJumpTable(JT->getIndex(), JT->getValueType(0), TF);

  llvm_unreachable("Unhandled address SDNode");
}

// Split Op into high and low parts according to HiTF and LoTF.
// Return an ADD node combining the parts.
SDValue VETargetLowering::makeHiLoPair(SDValue Op, unsigned HiTF, unsigned LoTF,
                                       SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue Hi = DAG.getNode(VEISD::Hi, DL, VT, withTargetFlags(Op, HiTF, DAG));
  SDValue Lo = DAG.getNode(VEISD::Lo, DL, VT, withTargetFlags(Op, LoTF, DAG));
  return DAG.getNode(ISD::ADD, DL, VT, Hi, Lo);
}

// Build SDNodes for producing an address from a GlobalAddress, ConstantPool,
// or ExternalSymbol SDNode.
SDValue VETargetLowering::makeAddress(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT PtrVT = Op.getValueType();

  // Handle PIC mode first. VE needs a got load for every variable!
  if (isPositionIndependent()) {
    auto GlobalN = dyn_cast<GlobalAddressSDNode>(Op);

    if (isa<ConstantPoolSDNode>(Op) || isa<JumpTableSDNode>(Op) ||
        (GlobalN && GlobalN->getGlobal()->hasLocalLinkage())) {
      // Create following instructions for local linkage PIC code.
      //     lea %reg, label@gotoff_lo
      //     and %reg, %reg, (32)0
      //     lea.sl %reg, label@gotoff_hi(%reg, %got)
      SDValue HiLo = makeHiLoPair(Op, VEMCExpr::VK_VE_GOTOFF_HI32,
                                  VEMCExpr::VK_VE_GOTOFF_LO32, DAG);
      SDValue GlobalBase = DAG.getNode(VEISD::GLOBAL_BASE_REG, DL, PtrVT);
      return DAG.getNode(ISD::ADD, DL, PtrVT, GlobalBase, HiLo);
    }
    // Create following instructions for not local linkage PIC code.
    //     lea %reg, label@got_lo
    //     and %reg, %reg, (32)0
    //     lea.sl %reg, label@got_hi(%reg)
    //     ld %reg, (%reg, %got)
    SDValue HiLo = makeHiLoPair(Op, VEMCExpr::VK_VE_GOT_HI32,
                                VEMCExpr::VK_VE_GOT_LO32, DAG);
    SDValue GlobalBase = DAG.getNode(VEISD::GLOBAL_BASE_REG, DL, PtrVT);
    SDValue AbsAddr = DAG.getNode(ISD::ADD, DL, PtrVT, GlobalBase, HiLo);
    return DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), AbsAddr,
                       MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  }

  // This is one of the absolute code models.
  switch (getTargetMachine().getCodeModel()) {
  default:
    llvm_unreachable("Unsupported absolute code model");
  case CodeModel::Small:
  case CodeModel::Medium:
  case CodeModel::Large:
    // abs64.
    return makeHiLoPair(Op, VEMCExpr::VK_VE_HI32, VEMCExpr::VK_VE_LO32, DAG);
  }
}

/// Custom Lower {

// The mappings for emitLeading/TrailingFence for VE is designed by following
// http://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
Instruction *VETargetLowering::emitLeadingFence(IRBuilderBase &Builder,
                                                Instruction *Inst,
                                                AtomicOrdering Ord) const {
  switch (Ord) {
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
    llvm_unreachable("Invalid fence: unordered/non-atomic");
  case AtomicOrdering::Monotonic:
  case AtomicOrdering::Acquire:
    return nullptr; // Nothing to do
  case AtomicOrdering::Release:
  case AtomicOrdering::AcquireRelease:
    return Builder.CreateFence(AtomicOrdering::Release);
  case AtomicOrdering::SequentiallyConsistent:
    if (!Inst->hasAtomicStore())
      return nullptr; // Nothing to do
    return Builder.CreateFence(AtomicOrdering::SequentiallyConsistent);
  }
  llvm_unreachable("Unknown fence ordering in emitLeadingFence");
}

Instruction *VETargetLowering::emitTrailingFence(IRBuilderBase &Builder,
                                                 Instruction *Inst,
                                                 AtomicOrdering Ord) const {
  switch (Ord) {
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
    llvm_unreachable("Invalid fence: unordered/not-atomic");
  case AtomicOrdering::Monotonic:
  case AtomicOrdering::Release:
    return nullptr; // Nothing to do
  case AtomicOrdering::Acquire:
  case AtomicOrdering::AcquireRelease:
    return Builder.CreateFence(AtomicOrdering::Acquire);
  case AtomicOrdering::SequentiallyConsistent:
    return Builder.CreateFence(AtomicOrdering::SequentiallyConsistent);
  }
  llvm_unreachable("Unknown fence ordering in emitTrailingFence");
}

SDValue VETargetLowering::lowerATOMIC_FENCE(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  AtomicOrdering FenceOrdering =
      static_cast<AtomicOrdering>(Op.getConstantOperandVal(1));
  SyncScope::ID FenceSSID =
      static_cast<SyncScope::ID>(Op.getConstantOperandVal(2));

  // VE uses Release consistency, so need a fence instruction if it is a
  // cross-thread fence.
  if (FenceSSID == SyncScope::System) {
    switch (FenceOrdering) {
    case AtomicOrdering::NotAtomic:
    case AtomicOrdering::Unordered:
    case AtomicOrdering::Monotonic:
      // No need to generate fencem instruction here.
      break;
    case AtomicOrdering::Acquire:
      // Generate "fencem 2" as acquire fence.
      return SDValue(DAG.getMachineNode(VE::FENCEM, DL, MVT::Other,
                                        DAG.getTargetConstant(2, DL, MVT::i32),
                                        Op.getOperand(0)),
                     0);
    case AtomicOrdering::Release:
      // Generate "fencem 1" as release fence.
      return SDValue(DAG.getMachineNode(VE::FENCEM, DL, MVT::Other,
                                        DAG.getTargetConstant(1, DL, MVT::i32),
                                        Op.getOperand(0)),
                     0);
    case AtomicOrdering::AcquireRelease:
    case AtomicOrdering::SequentiallyConsistent:
      // Generate "fencem 3" as acq_rel and seq_cst fence.
      // FIXME: "fencem 3" doesn't wait for PCIe deveices accesses,
      //        so  seq_cst may require more instruction for them.
      return SDValue(DAG.getMachineNode(VE::FENCEM, DL, MVT::Other,
                                        DAG.getTargetConstant(3, DL, MVT::i32),
                                        Op.getOperand(0)),
                     0);
    }
  }

  // MEMBARRIER is a compiler barrier; it codegens to a no-op.
  return DAG.getNode(ISD::MEMBARRIER, DL, MVT::Other, Op.getOperand(0));
}

TargetLowering::AtomicExpansionKind
VETargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const {
  // We have TS1AM implementation for i8/i16/i32/i64, so use it.
  if (AI->getOperation() == AtomicRMWInst::Xchg) {
    return AtomicExpansionKind::None;
  }
  // FIXME: Support "ATMAM" instruction for LOAD_ADD/SUB/AND/OR.

  // Otherwise, expand it using compare and exchange instruction to not call
  // __sync_fetch_and_* functions.
  return AtomicExpansionKind::CmpXChg;
}

static SDValue prepareTS1AM(SDValue Op, SelectionDAG &DAG, SDValue &Flag,
                            SDValue &Bits) {
  SDLoc DL(Op);
  AtomicSDNode *N = cast<AtomicSDNode>(Op);
  SDValue Ptr = N->getOperand(1);
  SDValue Val = N->getOperand(2);
  EVT PtrVT = Ptr.getValueType();
  bool Byte = N->getMemoryVT() == MVT::i8;
  //   Remainder = AND Ptr, 3
  //   Flag = 1 << Remainder  ; If Byte is true (1 byte swap flag)
  //   Flag = 3 << Remainder  ; If Byte is false (2 bytes swap flag)
  //   Bits = Remainder << 3
  //   NewVal = Val << Bits
  SDValue Const3 = DAG.getConstant(3, DL, PtrVT);
  SDValue Remainder = DAG.getNode(ISD::AND, DL, PtrVT, {Ptr, Const3});
  SDValue Mask = Byte ? DAG.getConstant(1, DL, MVT::i32)
                      : DAG.getConstant(3, DL, MVT::i32);
  Flag = DAG.getNode(ISD::SHL, DL, MVT::i32, {Mask, Remainder});
  Bits = DAG.getNode(ISD::SHL, DL, PtrVT, {Remainder, Const3});
  return DAG.getNode(ISD::SHL, DL, Val.getValueType(), {Val, Bits});
}

static SDValue finalizeTS1AM(SDValue Op, SelectionDAG &DAG, SDValue Data,
                             SDValue Bits) {
  SDLoc DL(Op);
  EVT VT = Data.getValueType();
  bool Byte = cast<AtomicSDNode>(Op)->getMemoryVT() == MVT::i8;
  //   NewData = Data >> Bits
  //   Result = NewData & 0xff   ; If Byte is true (1 byte)
  //   Result = NewData & 0xffff ; If Byte is false (2 bytes)

  SDValue NewData = DAG.getNode(ISD::SRL, DL, VT, Data, Bits);
  return DAG.getNode(ISD::AND, DL, VT,
                     {NewData, DAG.getConstant(Byte ? 0xff : 0xffff, DL, VT)});
}

SDValue VETargetLowering::lowerATOMIC_SWAP(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  AtomicSDNode *N = cast<AtomicSDNode>(Op);

  if (N->getMemoryVT() == MVT::i8) {
    // For i8, use "ts1am"
    //   Input:
    //     ATOMIC_SWAP Ptr, Val, Order
    //
    //   Output:
    //     Remainder = AND Ptr, 3
    //     Flag = 1 << Remainder   ; 1 byte swap flag for TS1AM inst.
    //     Bits = Remainder << 3
    //     NewVal = Val << Bits
    //
    //     Aligned = AND Ptr, -4
    //     Data = TS1AM Aligned, Flag, NewVal
    //
    //     NewData = Data >> Bits
    //     Result = NewData & 0xff ; 1 byte result
    SDValue Flag;
    SDValue Bits;
    SDValue NewVal = prepareTS1AM(Op, DAG, Flag, Bits);

    SDValue Ptr = N->getOperand(1);
    SDValue Aligned = DAG.getNode(ISD::AND, DL, Ptr.getValueType(),
                                  {Ptr, DAG.getConstant(-4, DL, MVT::i64)});
    SDValue TS1AM = DAG.getAtomic(VEISD::TS1AM, DL, N->getMemoryVT(),
                                  DAG.getVTList(Op.getNode()->getValueType(0),
                                                Op.getNode()->getValueType(1)),
                                  {N->getChain(), Aligned, Flag, NewVal},
                                  N->getMemOperand());

    SDValue Result = finalizeTS1AM(Op, DAG, TS1AM, Bits);
    SDValue Chain = TS1AM.getValue(1);
    return DAG.getMergeValues({Result, Chain}, DL);
  }
  if (N->getMemoryVT() == MVT::i16) {
    // For i16, use "ts1am"
    SDValue Flag;
    SDValue Bits;
    SDValue NewVal = prepareTS1AM(Op, DAG, Flag, Bits);

    SDValue Ptr = N->getOperand(1);
    SDValue Aligned = DAG.getNode(ISD::AND, DL, Ptr.getValueType(),
                                  {Ptr, DAG.getConstant(-4, DL, MVT::i64)});
    SDValue TS1AM = DAG.getAtomic(VEISD::TS1AM, DL, N->getMemoryVT(),
                                  DAG.getVTList(Op.getNode()->getValueType(0),
                                                Op.getNode()->getValueType(1)),
                                  {N->getChain(), Aligned, Flag, NewVal},
                                  N->getMemOperand());

    SDValue Result = finalizeTS1AM(Op, DAG, TS1AM, Bits);
    SDValue Chain = TS1AM.getValue(1);
    return DAG.getMergeValues({Result, Chain}, DL);
  }
  // Otherwise, let llvm legalize it.
  return Op;
}

SDValue VETargetLowering::lowerGlobalAddress(SDValue Op,
                                             SelectionDAG &DAG) const {
  return makeAddress(Op, DAG);
}

SDValue VETargetLowering::lowerBlockAddress(SDValue Op,
                                            SelectionDAG &DAG) const {
  return makeAddress(Op, DAG);
}

SDValue VETargetLowering::lowerConstantPool(SDValue Op,
                                            SelectionDAG &DAG) const {
  return makeAddress(Op, DAG);
}

SDValue
VETargetLowering::lowerToTLSGeneralDynamicModel(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);

  // Generate the following code:
  //   t1: ch,glue = callseq_start t0, 0, 0
  //   t2: i64,ch,glue = VEISD::GETTLSADDR t1, label, t1:1
  //   t3: ch,glue = callseq_end t2, 0, 0, t2:2
  //   t4: i64,ch,glue = CopyFromReg t3, Register:i64 $sx0, t3:1
  SDValue Label = withTargetFlags(Op, 0, DAG);
  EVT PtrVT = Op.getValueType();

  // Lowering the machine isd will make sure everything is in the right
  // location.
  SDValue Chain = DAG.getEntryNode();
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  const uint32_t *Mask = Subtarget->getRegisterInfo()->getCallPreservedMask(
      DAG.getMachineFunction(), CallingConv::C);
  Chain = DAG.getCALLSEQ_START(Chain, 64, 0, DL);
  SDValue Args[] = {Chain, Label, DAG.getRegisterMask(Mask), Chain.getValue(1)};
  Chain = DAG.getNode(VEISD::GETTLSADDR, DL, NodeTys, Args);
  Chain = DAG.getCALLSEQ_END(Chain, 64, 0, Chain.getValue(1), DL);
  Chain = DAG.getCopyFromReg(Chain, DL, VE::SX0, PtrVT, Chain.getValue(1));

  // GETTLSADDR will be codegen'ed as call. Inform MFI that function has calls.
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setHasCalls(true);

  // Also generate code to prepare a GOT register if it is PIC.
  if (isPositionIndependent()) {
    MachineFunction &MF = DAG.getMachineFunction();
    Subtarget->getInstrInfo()->getGlobalBaseReg(&MF);
  }

  return Chain;
}

SDValue VETargetLowering::lowerGlobalTLSAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  // The current implementation of nld (2.26) doesn't allow local exec model
  // code described in VE-tls_v1.1.pdf (*1) as its input. Instead, we always
  // generate the general dynamic model code sequence.
  //
  // *1: https://www.nec.com/en/global/prod/hpc/aurora/document/VE-tls_v1.1.pdf
  return lowerToTLSGeneralDynamicModel(Op, DAG);
}

SDValue VETargetLowering::lowerJumpTable(SDValue Op, SelectionDAG &DAG) const {
  return makeAddress(Op, DAG);
}

// Lower a f128 load into two f64 loads.
static SDValue lowerLoadF128(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  LoadSDNode *LdNode = dyn_cast<LoadSDNode>(Op.getNode());
  assert(LdNode && LdNode->getOffset().isUndef() && "Unexpected node type");
  Align Alignment = LdNode->getAlign();
  if (Alignment > 8)
    Alignment = Align(8);

  SDValue Lo64 =
      DAG.getLoad(MVT::f64, DL, LdNode->getChain(), LdNode->getBasePtr(),
                  LdNode->getPointerInfo(), Alignment,
                  LdNode->isVolatile() ? MachineMemOperand::MOVolatile
                                       : MachineMemOperand::MONone);
  EVT AddrVT = LdNode->getBasePtr().getValueType();
  SDValue HiPtr = DAG.getNode(ISD::ADD, DL, AddrVT, LdNode->getBasePtr(),
                              DAG.getConstant(8, DL, AddrVT));
  SDValue Hi64 =
      DAG.getLoad(MVT::f64, DL, LdNode->getChain(), HiPtr,
                  LdNode->getPointerInfo(), Alignment,
                  LdNode->isVolatile() ? MachineMemOperand::MOVolatile
                                       : MachineMemOperand::MONone);

  SDValue SubRegEven = DAG.getTargetConstant(VE::sub_even, DL, MVT::i32);
  SDValue SubRegOdd = DAG.getTargetConstant(VE::sub_odd, DL, MVT::i32);

  // VE stores Hi64 to 8(addr) and Lo64 to 0(addr)
  SDNode *InFP128 =
      DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF, DL, MVT::f128);
  InFP128 = DAG.getMachineNode(TargetOpcode::INSERT_SUBREG, DL, MVT::f128,
                               SDValue(InFP128, 0), Hi64, SubRegEven);
  InFP128 = DAG.getMachineNode(TargetOpcode::INSERT_SUBREG, DL, MVT::f128,
                               SDValue(InFP128, 0), Lo64, SubRegOdd);
  SDValue OutChains[2] = {SDValue(Lo64.getNode(), 1),
                          SDValue(Hi64.getNode(), 1)};
  SDValue OutChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
  SDValue Ops[2] = {SDValue(InFP128, 0), OutChain};
  return DAG.getMergeValues(Ops, DL);
}

// Lower a vXi1 load into following instructions
//   LDrii %1, (,%addr)
//   LVMxir  %vm, 0, %1
//   LDrii %2, 8(,%addr)
//   LVMxir  %vm, 0, %2
//   ...
static SDValue lowerLoadI1(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  LoadSDNode *LdNode = dyn_cast<LoadSDNode>(Op.getNode());
  assert(LdNode && LdNode->getOffset().isUndef() && "Unexpected node type");

  SDValue BasePtr = LdNode->getBasePtr();
  Align Alignment = LdNode->getAlign();
  if (Alignment > 8)
    Alignment = Align(8);

  EVT AddrVT = BasePtr.getValueType();
  EVT MemVT = LdNode->getMemoryVT();
  if (MemVT == MVT::v256i1 || MemVT == MVT::v4i64) {
    SDValue OutChains[4];
    SDNode *VM = DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF, DL, MemVT);
    for (int i = 0; i < 4; ++i) {
      // Generate load dag and prepare chains.
      SDValue Addr = DAG.getNode(ISD::ADD, DL, AddrVT, BasePtr,
                                 DAG.getConstant(8 * i, DL, AddrVT));
      SDValue Val =
          DAG.getLoad(MVT::i64, DL, LdNode->getChain(), Addr,
                      LdNode->getPointerInfo(), Alignment,
                      LdNode->isVolatile() ? MachineMemOperand::MOVolatile
                                           : MachineMemOperand::MONone);
      OutChains[i] = SDValue(Val.getNode(), 1);

      VM = DAG.getMachineNode(VE::LVMir_m, DL, MVT::i64,
                              DAG.getTargetConstant(i, DL, MVT::i64), Val,
                              SDValue(VM, 0));
    }
    SDValue OutChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
    SDValue Ops[2] = {SDValue(VM, 0), OutChain};
    return DAG.getMergeValues(Ops, DL);
  } else if (MemVT == MVT::v512i1 || MemVT == MVT::v8i64) {
    SDValue OutChains[8];
    SDNode *VM = DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF, DL, MemVT);
    for (int i = 0; i < 8; ++i) {
      // Generate load dag and prepare chains.
      SDValue Addr = DAG.getNode(ISD::ADD, DL, AddrVT, BasePtr,
                                 DAG.getConstant(8 * i, DL, AddrVT));
      SDValue Val =
          DAG.getLoad(MVT::i64, DL, LdNode->getChain(), Addr,
                      LdNode->getPointerInfo(), Alignment,
                      LdNode->isVolatile() ? MachineMemOperand::MOVolatile
                                           : MachineMemOperand::MONone);
      OutChains[i] = SDValue(Val.getNode(), 1);

      VM = DAG.getMachineNode(VE::LVMyir_y, DL, MVT::i64,
                              DAG.getTargetConstant(i, DL, MVT::i64), Val,
                              SDValue(VM, 0));
    }
    SDValue OutChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
    SDValue Ops[2] = {SDValue(VM, 0), OutChain};
    return DAG.getMergeValues(Ops, DL);
  } else {
    // Otherwise, ask llvm to expand it.
    return SDValue();
  }
}

SDValue VETargetLowering::lowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  LoadSDNode *LdNode = cast<LoadSDNode>(Op.getNode());
  EVT MemVT = LdNode->getMemoryVT();

  // If VPU is enabled, always expand non-mask vector loads to VVP
  if (Subtarget->enableVPU() && MemVT.isVector() && !isMaskType(MemVT))
    return lowerToVVP(Op, DAG);

  SDValue BasePtr = LdNode->getBasePtr();
  if (isa<FrameIndexSDNode>(BasePtr.getNode())) {
    // Do not expand store instruction with frame index here because of
    // dependency problems.  We expand it later in eliminateFrameIndex().
    return Op;
  }

  if (MemVT == MVT::f128)
    return lowerLoadF128(Op, DAG);
  if (isMaskType(MemVT))
    return lowerLoadI1(Op, DAG);

  return Op;
}

// Lower a f128 store into two f64 stores.
static SDValue lowerStoreF128(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  StoreSDNode *StNode = dyn_cast<StoreSDNode>(Op.getNode());
  assert(StNode && StNode->getOffset().isUndef() && "Unexpected node type");

  SDValue SubRegEven = DAG.getTargetConstant(VE::sub_even, DL, MVT::i32);
  SDValue SubRegOdd = DAG.getTargetConstant(VE::sub_odd, DL, MVT::i32);

  SDNode *Hi64 = DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG, DL, MVT::i64,
                                    StNode->getValue(), SubRegEven);
  SDNode *Lo64 = DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG, DL, MVT::i64,
                                    StNode->getValue(), SubRegOdd);

  Align Alignment = StNode->getAlign();
  if (Alignment > 8)
    Alignment = Align(8);

  // VE stores Hi64 to 8(addr) and Lo64 to 0(addr)
  SDValue OutChains[2];
  OutChains[0] =
      DAG.getStore(StNode->getChain(), DL, SDValue(Lo64, 0),
                   StNode->getBasePtr(), MachinePointerInfo(), Alignment,
                   StNode->isVolatile() ? MachineMemOperand::MOVolatile
                                        : MachineMemOperand::MONone);
  EVT AddrVT = StNode->getBasePtr().getValueType();
  SDValue HiPtr = DAG.getNode(ISD::ADD, DL, AddrVT, StNode->getBasePtr(),
                              DAG.getConstant(8, DL, AddrVT));
  OutChains[1] =
      DAG.getStore(StNode->getChain(), DL, SDValue(Hi64, 0), HiPtr,
                   MachinePointerInfo(), Alignment,
                   StNode->isVolatile() ? MachineMemOperand::MOVolatile
                                        : MachineMemOperand::MONone);
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
}

// Lower a vXi1 store into following instructions
//   SVMi  %1, %vm, 0
//   STrii %1, (,%addr)
//   SVMi  %2, %vm, 1
//   STrii %2, 8(,%addr)
//   ...
static SDValue lowerStoreI1(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  StoreSDNode *StNode = dyn_cast<StoreSDNode>(Op.getNode());
  assert(StNode && StNode->getOffset().isUndef() && "Unexpected node type");

  SDValue BasePtr = StNode->getBasePtr();
  Align Alignment = StNode->getAlign();
  if (Alignment > 8)
    Alignment = Align(8);
  EVT AddrVT = BasePtr.getValueType();
  EVT MemVT = StNode->getMemoryVT();
  if (MemVT == MVT::v256i1 || MemVT == MVT::v4i64) {
    SDValue OutChains[4];
    for (int i = 0; i < 4; ++i) {
      SDNode *V =
          DAG.getMachineNode(VE::SVMmi, DL, MVT::i64, StNode->getValue(),
                             DAG.getTargetConstant(i, DL, MVT::i64));
      SDValue Addr = DAG.getNode(ISD::ADD, DL, AddrVT, BasePtr,
                                 DAG.getConstant(8 * i, DL, AddrVT));
      OutChains[i] =
          DAG.getStore(StNode->getChain(), DL, SDValue(V, 0), Addr,
                       MachinePointerInfo(), Alignment,
                       StNode->isVolatile() ? MachineMemOperand::MOVolatile
                                            : MachineMemOperand::MONone);
    }
    return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
  } else if (MemVT == MVT::v512i1 || MemVT == MVT::v8i64) {
    SDValue OutChains[8];
    for (int i = 0; i < 8; ++i) {
      SDNode *V =
          DAG.getMachineNode(VE::SVMyi, DL, MVT::i64, StNode->getValue(),
                             DAG.getTargetConstant(i, DL, MVT::i64));
      SDValue Addr = DAG.getNode(ISD::ADD, DL, AddrVT, BasePtr,
                                 DAG.getConstant(8 * i, DL, AddrVT));
      OutChains[i] =
          DAG.getStore(StNode->getChain(), DL, SDValue(V, 0), Addr,
                       MachinePointerInfo(), Alignment,
                       StNode->isVolatile() ? MachineMemOperand::MOVolatile
                                            : MachineMemOperand::MONone);
    }
    return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
  } else {
    // Otherwise, ask llvm to expand it.
    return SDValue();
  }
}

SDValue VETargetLowering::lowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  StoreSDNode *StNode = cast<StoreSDNode>(Op.getNode());
  assert(StNode && StNode->getOffset().isUndef() && "Unexpected node type");
  EVT MemVT = StNode->getMemoryVT();

  // If VPU is enabled, always expand non-mask vector stores to VVP
  if (Subtarget->enableVPU() && MemVT.isVector() && !isMaskType(MemVT))
    return lowerToVVP(Op, DAG);

  SDValue BasePtr = StNode->getBasePtr();
  if (isa<FrameIndexSDNode>(BasePtr.getNode())) {
    // Do not expand store instruction with frame index here because of
    // dependency problems.  We expand it later in eliminateFrameIndex().
    return Op;
  }

  if (MemVT == MVT::f128)
    return lowerStoreF128(Op, DAG);
  if (isMaskType(MemVT))
    return lowerStoreI1(Op, DAG);

  // Otherwise, ask llvm to expand it.
  return SDValue();
}

SDValue VETargetLowering::lowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  VEMachineFunctionInfo *FuncInfo = MF.getInfo<VEMachineFunctionInfo>();
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  // Need frame address to find the address of VarArgsFrameIndex.
  MF.getFrameInfo().setFrameAddressIsTaken(true);

  // vastart just stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  SDLoc DL(Op);
  SDValue Offset =
      DAG.getNode(ISD::ADD, DL, PtrVT, DAG.getRegister(VE::SX9, PtrVT),
                  DAG.getIntPtrConstant(FuncInfo->getVarArgsFrameOffset(), DL));
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, Offset, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue VETargetLowering::lowerVAARG(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  EVT VT = Node->getValueType(0);
  SDValue InChain = Node->getOperand(0);
  SDValue VAListPtr = Node->getOperand(1);
  EVT PtrVT = VAListPtr.getValueType();
  const Value *SV = cast<SrcValueSDNode>(Node->getOperand(2))->getValue();
  SDLoc DL(Node);
  SDValue VAList =
      DAG.getLoad(PtrVT, DL, InChain, VAListPtr, MachinePointerInfo(SV));
  SDValue Chain = VAList.getValue(1);
  SDValue NextPtr;

  if (VT == MVT::f128) {
    // VE f128 values must be stored with 16 bytes alignment.  We don't
    // know the actual alignment of VAList, so we take alignment of it
    // dynamically.
    int Align = 16;
    VAList = DAG.getNode(ISD::ADD, DL, PtrVT, VAList,
                         DAG.getConstant(Align - 1, DL, PtrVT));
    VAList = DAG.getNode(ISD::AND, DL, PtrVT, VAList,
                         DAG.getConstant(-Align, DL, PtrVT));
    // Increment the pointer, VAList, by 16 to the next vaarg.
    NextPtr =
        DAG.getNode(ISD::ADD, DL, PtrVT, VAList, DAG.getIntPtrConstant(16, DL));
  } else if (VT == MVT::f32) {
    // float --> need special handling like below.
    //    0      4
    //    +------+------+
    //    | empty| float|
    //    +------+------+
    // Increment the pointer, VAList, by 8 to the next vaarg.
    NextPtr =
        DAG.getNode(ISD::ADD, DL, PtrVT, VAList, DAG.getIntPtrConstant(8, DL));
    // Then, adjust VAList.
    unsigned InternalOffset = 4;
    VAList = DAG.getNode(ISD::ADD, DL, PtrVT, VAList,
                         DAG.getConstant(InternalOffset, DL, PtrVT));
  } else {
    // Increment the pointer, VAList, by 8 to the next vaarg.
    NextPtr =
        DAG.getNode(ISD::ADD, DL, PtrVT, VAList, DAG.getIntPtrConstant(8, DL));
  }

  // Store the incremented VAList to the legalized pointer.
  InChain = DAG.getStore(Chain, DL, NextPtr, VAListPtr, MachinePointerInfo(SV));

  // Load the actual argument out of the pointer VAList.
  // We can't count on greater alignment than the word size.
  return DAG.getLoad(
      VT, DL, InChain, VAList, MachinePointerInfo(),
      Align(std::min(PtrVT.getSizeInBits(), VT.getSizeInBits()) / 8));
}

SDValue VETargetLowering::lowerDYNAMIC_STACKALLOC(SDValue Op,
                                                  SelectionDAG &DAG) const {
  // Generate following code.
  //   (void)__llvm_grow_stack(size);
  //   ret = GETSTACKTOP;        // pseudo instruction
  SDLoc DL(Op);

  // Get the inputs.
  SDNode *Node = Op.getNode();
  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);
  MaybeAlign Alignment(Op.getConstantOperandVal(2));
  EVT VT = Node->getValueType(0);

  // Chain the dynamic stack allocation so that it doesn't modify the stack
  // pointer when other instructions are using the stack.
  Chain = DAG.getCALLSEQ_START(Chain, 0, 0, DL);

  const TargetFrameLowering &TFI = *Subtarget->getFrameLowering();
  Align StackAlign = TFI.getStackAlign();
  bool NeedsAlign = Alignment.valueOrOne() > StackAlign;

  // Prepare arguments
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  Entry.Node = Size;
  Entry.Ty = Entry.Node.getValueType().getTypeForEVT(*DAG.getContext());
  Args.push_back(Entry);
  if (NeedsAlign) {
    Entry.Node = DAG.getConstant(~(Alignment->value() - 1ULL), DL, VT);
    Entry.Ty = Entry.Node.getValueType().getTypeForEVT(*DAG.getContext());
    Args.push_back(Entry);
  }
  Type *RetTy = Type::getVoidTy(*DAG.getContext());

  EVT PtrVT = Op.getValueType();
  SDValue Callee;
  if (NeedsAlign) {
    Callee = DAG.getTargetExternalSymbol("__ve_grow_stack_align", PtrVT, 0);
  } else {
    Callee = DAG.getTargetExternalSymbol("__ve_grow_stack", PtrVT, 0);
  }

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(Chain)
      .setCallee(CallingConv::PreserveAll, RetTy, Callee, std::move(Args))
      .setDiscardResult(true);
  std::pair<SDValue, SDValue> pair = LowerCallTo(CLI);
  Chain = pair.second;
  SDValue Result = DAG.getNode(VEISD::GETSTACKTOP, DL, VT, Chain);
  if (NeedsAlign) {
    Result = DAG.getNode(ISD::ADD, DL, VT, Result,
                         DAG.getConstant((Alignment->value() - 1ULL), DL, VT));
    Result = DAG.getNode(ISD::AND, DL, VT, Result,
                         DAG.getConstant(~(Alignment->value() - 1ULL), DL, VT));
  }
  //  Chain = Result.getValue(1);
  Chain = DAG.getCALLSEQ_END(Chain, 0, 0, SDValue(), DL);

  SDValue Ops[2] = {Result, Chain};
  return DAG.getMergeValues(Ops, DL);
}

SDValue VETargetLowering::lowerEH_SJLJ_LONGJMP(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(VEISD::EH_SJLJ_LONGJMP, DL, MVT::Other, Op.getOperand(0),
                     Op.getOperand(1));
}

SDValue VETargetLowering::lowerEH_SJLJ_SETJMP(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(VEISD::EH_SJLJ_SETJMP, DL,
                     DAG.getVTList(MVT::i32, MVT::Other), Op.getOperand(0),
                     Op.getOperand(1));
}

SDValue VETargetLowering::lowerEH_SJLJ_SETUP_DISPATCH(SDValue Op,
                                                      SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(VEISD::EH_SJLJ_SETUP_DISPATCH, DL, MVT::Other,
                     Op.getOperand(0));
}

static SDValue lowerFRAMEADDR(SDValue Op, SelectionDAG &DAG,
                              const VETargetLowering &TLI,
                              const VESubtarget *Subtarget) {
  SDLoc DL(Op);
  MachineFunction &MF = DAG.getMachineFunction();
  EVT PtrVT = TLI.getPointerTy(MF.getDataLayout());

  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  unsigned Depth = Op.getConstantOperandVal(0);
  const VERegisterInfo *RegInfo = Subtarget->getRegisterInfo();
  Register FrameReg = RegInfo->getFrameRegister(MF);
  SDValue FrameAddr =
      DAG.getCopyFromReg(DAG.getEntryNode(), DL, FrameReg, PtrVT);
  while (Depth--)
    FrameAddr = DAG.getLoad(Op.getValueType(), DL, DAG.getEntryNode(),
                            FrameAddr, MachinePointerInfo());
  return FrameAddr;
}

static SDValue lowerRETURNADDR(SDValue Op, SelectionDAG &DAG,
                               const VETargetLowering &TLI,
                               const VESubtarget *Subtarget) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (TLI.verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  SDValue FrameAddr = lowerFRAMEADDR(Op, DAG, TLI, Subtarget);

  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue Offset = DAG.getConstant(8, DL, VT);
  return DAG.getLoad(VT, DL, DAG.getEntryNode(),
                     DAG.getNode(ISD::ADD, DL, VT, FrameAddr, Offset),
                     MachinePointerInfo());
}

SDValue VETargetLowering::lowerINTRINSIC_WO_CHAIN(SDValue Op,
                                                  SelectionDAG &DAG) const {
  SDLoc DL(Op);
  unsigned IntNo = Op.getConstantOperandVal(0);
  switch (IntNo) {
  default: // Don't custom lower most intrinsics.
    return SDValue();
  case Intrinsic::eh_sjlj_lsda: {
    MachineFunction &MF = DAG.getMachineFunction();
    MVT VT = Op.getSimpleValueType();
    const VETargetMachine *TM =
        static_cast<const VETargetMachine *>(&DAG.getTarget());

    // Create GCC_except_tableXX string.  The real symbol for that will be
    // generated in EHStreamer::emitExceptionTable() later.  So, we just
    // borrow it's name here.
    TM->getStrList()->push_back(std::string(
        (Twine("GCC_except_table") + Twine(MF.getFunctionNumber())).str()));
    SDValue Addr =
        DAG.getTargetExternalSymbol(TM->getStrList()->back().c_str(), VT, 0);
    if (isPositionIndependent()) {
      Addr = makeHiLoPair(Addr, VEMCExpr::VK_VE_GOTOFF_HI32,
                          VEMCExpr::VK_VE_GOTOFF_LO32, DAG);
      SDValue GlobalBase = DAG.getNode(VEISD::GLOBAL_BASE_REG, DL, VT);
      return DAG.getNode(ISD::ADD, DL, VT, GlobalBase, Addr);
    }
    return makeHiLoPair(Addr, VEMCExpr::VK_VE_HI32, VEMCExpr::VK_VE_LO32, DAG);
  }
  }
}

static bool getUniqueInsertion(SDNode *N, unsigned &UniqueIdx) {
  if (!isa<BuildVectorSDNode>(N))
    return false;
  const auto *BVN = cast<BuildVectorSDNode>(N);

  // Find first non-undef insertion.
  unsigned Idx;
  for (Idx = 0; Idx < BVN->getNumOperands(); ++Idx) {
    auto ElemV = BVN->getOperand(Idx);
    if (!ElemV->isUndef())
      break;
  }
  // Catch the (hypothetical) all-undef case.
  if (Idx == BVN->getNumOperands())
    return false;
  // Remember insertion.
  UniqueIdx = Idx++;
  // Verify that all other insertions are undef.
  for (; Idx < BVN->getNumOperands(); ++Idx) {
    auto ElemV = BVN->getOperand(Idx);
    if (!ElemV->isUndef())
      return false;
  }
  return true;
}

static SDValue getSplatValue(SDNode *N) {
  if (auto *BuildVec = dyn_cast<BuildVectorSDNode>(N)) {
    return BuildVec->getSplatValue();
  }
  return SDValue();
}

SDValue VETargetLowering::lowerBUILD_VECTOR(SDValue Op,
                                            SelectionDAG &DAG) const {
  VECustomDAG CDAG(DAG, Op);
  MVT ResultVT = Op.getSimpleValueType();

  // If there is just one element, expand to INSERT_VECTOR_ELT.
  unsigned UniqueIdx;
  if (getUniqueInsertion(Op.getNode(), UniqueIdx)) {
    SDValue AccuV = CDAG.getUNDEF(Op.getValueType());
    auto ElemV = Op->getOperand(UniqueIdx);
    SDValue IdxV = CDAG.getConstant(UniqueIdx, MVT::i64);
    return CDAG.getNode(ISD::INSERT_VECTOR_ELT, ResultVT, {AccuV, ElemV, IdxV});
  }

  // Else emit a broadcast.
  if (SDValue ScalarV = getSplatValue(Op.getNode())) {
    unsigned NumEls = ResultVT.getVectorNumElements();
    auto AVL = CDAG.getConstant(NumEls, MVT::i32);
    return CDAG.getBroadcast(ResultVT, ScalarV, AVL);
  }

  // Expand
  return SDValue();
}

TargetLowering::LegalizeAction
VETargetLowering::getCustomOperationAction(SDNode &Op) const {
  // Custom legalization on VVP_* and VEC_* opcodes is required to pack-legalize
  // these operations (transform nodes such that their AVL parameter refers to
  // packs of 64bit, instead of number of elements.

  // Packing opcodes are created with a pack-legal AVL (LEGALAVL). No need to
  // re-visit them.
  if (isPackingSupportOpcode(Op.getOpcode()))
    return Legal;

  // Custom lower to legalize AVL for packed mode.
  if (isVVPOrVEC(Op.getOpcode()))
    return Custom;
  return Legal;
}

SDValue VETargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  LLVM_DEBUG(dbgs() << "::LowerOperation "; Op.dump(&DAG));
  unsigned Opcode = Op.getOpcode();

  /// Scalar isel.
  switch (Opcode) {
  case ISD::ATOMIC_FENCE:
    return lowerATOMIC_FENCE(Op, DAG);
  case ISD::ATOMIC_SWAP:
    return lowerATOMIC_SWAP(Op, DAG);
  case ISD::BlockAddress:
    return lowerBlockAddress(Op, DAG);
  case ISD::ConstantPool:
    return lowerConstantPool(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    return lowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::EH_SJLJ_LONGJMP:
    return lowerEH_SJLJ_LONGJMP(Op, DAG);
  case ISD::EH_SJLJ_SETJMP:
    return lowerEH_SJLJ_SETJMP(Op, DAG);
  case ISD::EH_SJLJ_SETUP_DISPATCH:
    return lowerEH_SJLJ_SETUP_DISPATCH(Op, DAG);
  case ISD::FRAMEADDR:
    return lowerFRAMEADDR(Op, DAG, *this, Subtarget);
  case ISD::GlobalAddress:
    return lowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress:
    return lowerGlobalTLSAddress(Op, DAG);
  case ISD::INTRINSIC_WO_CHAIN:
    return lowerINTRINSIC_WO_CHAIN(Op, DAG);
  case ISD::JumpTable:
    return lowerJumpTable(Op, DAG);
  case ISD::LOAD:
    return lowerLOAD(Op, DAG);
  case ISD::RETURNADDR:
    return lowerRETURNADDR(Op, DAG, *this, Subtarget);
  case ISD::BUILD_VECTOR:
    return lowerBUILD_VECTOR(Op, DAG);
  case ISD::STORE:
    return lowerSTORE(Op, DAG);
  case ISD::VASTART:
    return lowerVASTART(Op, DAG);
  case ISD::VAARG:
    return lowerVAARG(Op, DAG);

  case ISD::INSERT_VECTOR_ELT:
    return lowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::EXTRACT_VECTOR_ELT:
    return lowerEXTRACT_VECTOR_ELT(Op, DAG);
  }

  /// Vector isel.
  if (ISD::isVPOpcode(Opcode))
    return lowerToVVP(Op, DAG);

  switch (Opcode) {
  default:
    llvm_unreachable("Should not custom lower this!");

  // Legalize the AVL of this internal node.
  case VEISD::VEC_BROADCAST:
#define ADD_VVP_OP(VVP_NAME, ...) case VEISD::VVP_NAME:
#include "VVPNodes.def"
    // AVL already legalized.
    if (getAnnotatedNodeAVL(Op).second)
      return Op;
    return legalizeInternalVectorOp(Op, DAG);

    // Translate into a VEC_*/VVP_* layer operation.
  case ISD::MLOAD:
  case ISD::MSTORE:
#define ADD_VVP_OP(VVP_NAME, ISD_NAME) case ISD::ISD_NAME:
#include "VVPNodes.def"
    if (isMaskArithmetic(Op) && isPackedVectorType(Op.getValueType()))
      return splitMaskArithmetic(Op, DAG);
    return lowerToVVP(Op, DAG);
  }
}
/// } Custom Lower

void VETargetLowering::ReplaceNodeResults(SDNode *N,
                                          SmallVectorImpl<SDValue> &Results,
                                          SelectionDAG &DAG) const {
  switch (N->getOpcode()) {
  case ISD::ATOMIC_SWAP:
    // Let LLVM expand atomic swap instruction through LowerOperation.
    return;
  default:
    LLVM_DEBUG(N->dumpr(&DAG));
    llvm_unreachable("Do not know how to custom type legalize this operation!");
  }
}

/// JumpTable for VE.
///
///   VE cannot generate relocatable symbol in jump table.  VE cannot
///   generate expressions using symbols in both text segment and data
///   segment like below.
///             .4byte  .LBB0_2-.LJTI0_0
///   So, we generate offset from the top of function like below as
///   a custom label.
///             .4byte  .LBB0_2-<function name>

unsigned VETargetLowering::getJumpTableEncoding() const {
  // Use custom label for PIC.
  if (isPositionIndependent())
    return MachineJumpTableInfo::EK_Custom32;

  // Otherwise, use the normal jump table encoding heuristics.
  return TargetLowering::getJumpTableEncoding();
}

const MCExpr *VETargetLowering::LowerCustomJumpTableEntry(
    const MachineJumpTableInfo *MJTI, const MachineBasicBlock *MBB,
    unsigned Uid, MCContext &Ctx) const {
  assert(isPositionIndependent());

  // Generate custom label for PIC like below.
  //    .4bytes  .LBB0_2-<function name>
  const auto *Value = MCSymbolRefExpr::create(MBB->getSymbol(), Ctx);
  MCSymbol *Sym = Ctx.getOrCreateSymbol(MBB->getParent()->getName().data());
  const auto *Base = MCSymbolRefExpr::create(Sym, Ctx);
  return MCBinaryExpr::createSub(Value, Base, Ctx);
}

SDValue VETargetLowering::getPICJumpTableRelocBase(SDValue Table,
                                                   SelectionDAG &DAG) const {
  assert(isPositionIndependent());
  SDLoc DL(Table);
  Function *Function = &DAG.getMachineFunction().getFunction();
  assert(Function != nullptr);
  auto PtrTy = getPointerTy(DAG.getDataLayout(), Function->getAddressSpace());

  // In the jump table, we have following values in PIC mode.
  //    .4bytes  .LBB0_2-<function name>
  // We need to add this value and the address of this function to generate
  // .LBB0_2 label correctly under PIC mode.  So, we want to generate following
  // instructions:
  //     lea %reg, fun@gotoff_lo
  //     and %reg, %reg, (32)0
  //     lea.sl %reg, fun@gotoff_hi(%reg, %got)
  // In order to do so, we need to genarate correctly marked DAG node using
  // makeHiLoPair.
  SDValue Op = DAG.getGlobalAddress(Function, DL, PtrTy);
  SDValue HiLo = makeHiLoPair(Op, VEMCExpr::VK_VE_GOTOFF_HI32,
                              VEMCExpr::VK_VE_GOTOFF_LO32, DAG);
  SDValue GlobalBase = DAG.getNode(VEISD::GLOBAL_BASE_REG, DL, PtrTy);
  return DAG.getNode(ISD::ADD, DL, PtrTy, GlobalBase, HiLo);
}

Register VETargetLowering::prepareMBB(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator I,
                                      MachineBasicBlock *TargetBB,
                                      const DebugLoc &DL) const {
  MachineFunction *MF = MBB.getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const VEInstrInfo *TII = Subtarget->getInstrInfo();

  const TargetRegisterClass *RC = &VE::I64RegClass;
  Register Tmp1 = MRI.createVirtualRegister(RC);
  Register Tmp2 = MRI.createVirtualRegister(RC);
  Register Result = MRI.createVirtualRegister(RC);

  if (isPositionIndependent()) {
    // Create following instructions for local linkage PIC code.
    //     lea %Tmp1, TargetBB@gotoff_lo
    //     and %Tmp2, %Tmp1, (32)0
    //     lea.sl %Result, TargetBB@gotoff_hi(%Tmp2, %s15) ; %s15 is GOT
    BuildMI(MBB, I, DL, TII->get(VE::LEAzii), Tmp1)
        .addImm(0)
        .addImm(0)
        .addMBB(TargetBB, VEMCExpr::VK_VE_GOTOFF_LO32);
    BuildMI(MBB, I, DL, TII->get(VE::ANDrm), Tmp2)
        .addReg(Tmp1, getKillRegState(true))
        .addImm(M0(32));
    BuildMI(MBB, I, DL, TII->get(VE::LEASLrri), Result)
        .addReg(VE::SX15)
        .addReg(Tmp2, getKillRegState(true))
        .addMBB(TargetBB, VEMCExpr::VK_VE_GOTOFF_HI32);
  } else {
    // Create following instructions for non-PIC code.
    //     lea     %Tmp1, TargetBB@lo
    //     and     %Tmp2, %Tmp1, (32)0
    //     lea.sl  %Result, TargetBB@hi(%Tmp2)
    BuildMI(MBB, I, DL, TII->get(VE::LEAzii), Tmp1)
        .addImm(0)
        .addImm(0)
        .addMBB(TargetBB, VEMCExpr::VK_VE_LO32);
    BuildMI(MBB, I, DL, TII->get(VE::ANDrm), Tmp2)
        .addReg(Tmp1, getKillRegState(true))
        .addImm(M0(32));
    BuildMI(MBB, I, DL, TII->get(VE::LEASLrii), Result)
        .addReg(Tmp2, getKillRegState(true))
        .addImm(0)
        .addMBB(TargetBB, VEMCExpr::VK_VE_HI32);
  }
  return Result;
}

Register VETargetLowering::prepareSymbol(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator I,
                                         StringRef Symbol, const DebugLoc &DL,
                                         bool IsLocal = false,
                                         bool IsCall = false) const {
  MachineFunction *MF = MBB.getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const VEInstrInfo *TII = Subtarget->getInstrInfo();

  const TargetRegisterClass *RC = &VE::I64RegClass;
  Register Result = MRI.createVirtualRegister(RC);

  if (isPositionIndependent()) {
    if (IsCall && !IsLocal) {
      // Create following instructions for non-local linkage PIC code function
      // calls.  These instructions uses IC and magic number -24, so we expand
      // them in VEAsmPrinter.cpp from GETFUNPLT pseudo instruction.
      //     lea %Reg, Symbol@plt_lo(-24)
      //     and %Reg, %Reg, (32)0
      //     sic %s16
      //     lea.sl %Result, Symbol@plt_hi(%Reg, %s16) ; %s16 is PLT
      BuildMI(MBB, I, DL, TII->get(VE::GETFUNPLT), Result)
          .addExternalSymbol("abort");
    } else if (IsLocal) {
      Register Tmp1 = MRI.createVirtualRegister(RC);
      Register Tmp2 = MRI.createVirtualRegister(RC);
      // Create following instructions for local linkage PIC code.
      //     lea %Tmp1, Symbol@gotoff_lo
      //     and %Tmp2, %Tmp1, (32)0
      //     lea.sl %Result, Symbol@gotoff_hi(%Tmp2, %s15) ; %s15 is GOT
      BuildMI(MBB, I, DL, TII->get(VE::LEAzii), Tmp1)
          .addImm(0)
          .addImm(0)
          .addExternalSymbol(Symbol.data(), VEMCExpr::VK_VE_GOTOFF_LO32);
      BuildMI(MBB, I, DL, TII->get(VE::ANDrm), Tmp2)
          .addReg(Tmp1, getKillRegState(true))
          .addImm(M0(32));
      BuildMI(MBB, I, DL, TII->get(VE::LEASLrri), Result)
          .addReg(VE::SX15)
          .addReg(Tmp2, getKillRegState(true))
          .addExternalSymbol(Symbol.data(), VEMCExpr::VK_VE_GOTOFF_HI32);
    } else {
      Register Tmp1 = MRI.createVirtualRegister(RC);
      Register Tmp2 = MRI.createVirtualRegister(RC);
      // Create following instructions for not local linkage PIC code.
      //     lea %Tmp1, Symbol@got_lo
      //     and %Tmp2, %Tmp1, (32)0
      //     lea.sl %Tmp3, Symbol@gotoff_hi(%Tmp2, %s15) ; %s15 is GOT
      //     ld %Result, 0(%Tmp3)
      Register Tmp3 = MRI.createVirtualRegister(RC);
      BuildMI(MBB, I, DL, TII->get(VE::LEAzii), Tmp1)
          .addImm(0)
          .addImm(0)
          .addExternalSymbol(Symbol.data(), VEMCExpr::VK_VE_GOT_LO32);
      BuildMI(MBB, I, DL, TII->get(VE::ANDrm), Tmp2)
          .addReg(Tmp1, getKillRegState(true))
          .addImm(M0(32));
      BuildMI(MBB, I, DL, TII->get(VE::LEASLrri), Tmp3)
          .addReg(VE::SX15)
          .addReg(Tmp2, getKillRegState(true))
          .addExternalSymbol(Symbol.data(), VEMCExpr::VK_VE_GOT_HI32);
      BuildMI(MBB, I, DL, TII->get(VE::LDrii), Result)
          .addReg(Tmp3, getKillRegState(true))
          .addImm(0)
          .addImm(0);
    }
  } else {
    Register Tmp1 = MRI.createVirtualRegister(RC);
    Register Tmp2 = MRI.createVirtualRegister(RC);
    // Create following instructions for non-PIC code.
    //     lea     %Tmp1, Symbol@lo
    //     and     %Tmp2, %Tmp1, (32)0
    //     lea.sl  %Result, Symbol@hi(%Tmp2)
    BuildMI(MBB, I, DL, TII->get(VE::LEAzii), Tmp1)
        .addImm(0)
        .addImm(0)
        .addExternalSymbol(Symbol.data(), VEMCExpr::VK_VE_LO32);
    BuildMI(MBB, I, DL, TII->get(VE::ANDrm), Tmp2)
        .addReg(Tmp1, getKillRegState(true))
        .addImm(M0(32));
    BuildMI(MBB, I, DL, TII->get(VE::LEASLrii), Result)
        .addReg(Tmp2, getKillRegState(true))
        .addImm(0)
        .addExternalSymbol(Symbol.data(), VEMCExpr::VK_VE_HI32);
  }
  return Result;
}

void VETargetLowering::setupEntryBlockForSjLj(MachineInstr &MI,
                                              MachineBasicBlock *MBB,
                                              MachineBasicBlock *DispatchBB,
                                              int FI, int Offset) const {
  DebugLoc DL = MI.getDebugLoc();
  const VEInstrInfo *TII = Subtarget->getInstrInfo();

  Register LabelReg =
      prepareMBB(*MBB, MachineBasicBlock::iterator(MI), DispatchBB, DL);

  // Store an address of DispatchBB to a given jmpbuf[1] where has next IC
  // referenced by longjmp (throw) later.
  MachineInstrBuilder MIB = BuildMI(*MBB, MI, DL, TII->get(VE::STrii));
  addFrameReference(MIB, FI, Offset); // jmpbuf[1]
  MIB.addReg(LabelReg, getKillRegState(true));
}

MachineBasicBlock *
VETargetLowering::emitEHSjLjSetJmp(MachineInstr &MI,
                                   MachineBasicBlock *MBB) const {
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  const TargetRegisterInfo *TRI = Subtarget->getRegisterInfo();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  const BasicBlock *BB = MBB->getBasicBlock();
  MachineFunction::iterator I = ++MBB->getIterator();

  // Memory Reference.
  SmallVector<MachineMemOperand *, 2> MMOs(MI.memoperands_begin(),
                                           MI.memoperands_end());
  Register BufReg = MI.getOperand(1).getReg();

  Register DstReg;

  DstReg = MI.getOperand(0).getReg();
  const TargetRegisterClass *RC = MRI.getRegClass(DstReg);
  assert(TRI->isTypeLegalForClass(*RC, MVT::i32) && "Invalid destination!");
  (void)TRI;
  Register MainDestReg = MRI.createVirtualRegister(RC);
  Register RestoreDestReg = MRI.createVirtualRegister(RC);

  // For `v = call @llvm.eh.sjlj.setjmp(buf)`, we generate following
  // instructions.  SP/FP must be saved in jmpbuf before `llvm.eh.sjlj.setjmp`.
  //
  // ThisMBB:
  //   buf[3] = %s17 iff %s17 is used as BP
  //   buf[1] = RestoreMBB as IC after longjmp
  //   # SjLjSetup RestoreMBB
  //
  // MainMBB:
  //   v_main = 0
  //
  // SinkMBB:
  //   v = phi(v_main, MainMBB, v_restore, RestoreMBB)
  //   ...
  //
  // RestoreMBB:
  //   %s17 = buf[3] = iff %s17 is used as BP
  //   v_restore = 1
  //   goto SinkMBB

  MachineBasicBlock *ThisMBB = MBB;
  MachineBasicBlock *MainMBB = MF->CreateMachineBasicBlock(BB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(BB);
  MachineBasicBlock *RestoreMBB = MF->CreateMachineBasicBlock(BB);
  MF->insert(I, MainMBB);
  MF->insert(I, SinkMBB);
  MF->push_back(RestoreMBB);
  RestoreMBB->setMachineBlockAddressTaken();

  // Transfer the remainder of BB and its successor edges to SinkMBB.
  SinkMBB->splice(SinkMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(MBB);

  // ThisMBB:
  Register LabelReg =
      prepareMBB(*MBB, MachineBasicBlock::iterator(MI), RestoreMBB, DL);

  // Store BP in buf[3] iff this function is using BP.
  const VEFrameLowering *TFI = Subtarget->getFrameLowering();
  if (TFI->hasBP(*MF)) {
    MachineInstrBuilder MIB = BuildMI(*MBB, MI, DL, TII->get(VE::STrii));
    MIB.addReg(BufReg);
    MIB.addImm(0);
    MIB.addImm(24);
    MIB.addReg(VE::SX17);
    MIB.setMemRefs(MMOs);
  }

  // Store IP in buf[1].
  MachineInstrBuilder MIB = BuildMI(*MBB, MI, DL, TII->get(VE::STrii));
  MIB.add(MI.getOperand(1)); // we can preserve the kill flags here.
  MIB.addImm(0);
  MIB.addImm(8);
  MIB.addReg(LabelReg, getKillRegState(true));
  MIB.setMemRefs(MMOs);

  // SP/FP are already stored in jmpbuf before `llvm.eh.sjlj.setjmp`.

  // Insert setup.
  MIB =
      BuildMI(*ThisMBB, MI, DL, TII->get(VE::EH_SjLj_Setup)).addMBB(RestoreMBB);

  const VERegisterInfo *RegInfo = Subtarget->getRegisterInfo();
  MIB.addRegMask(RegInfo->getNoPreservedMask());
  ThisMBB->addSuccessor(MainMBB);
  ThisMBB->addSuccessor(RestoreMBB);

  // MainMBB:
  BuildMI(MainMBB, DL, TII->get(VE::LEAzii), MainDestReg)
      .addImm(0)
      .addImm(0)
      .addImm(0);
  MainMBB->addSuccessor(SinkMBB);

  // SinkMBB:
  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII->get(VE::PHI), DstReg)
      .addReg(MainDestReg)
      .addMBB(MainMBB)
      .addReg(RestoreDestReg)
      .addMBB(RestoreMBB);

  // RestoreMBB:
  // Restore BP from buf[3] iff this function is using BP.  The address of
  // buf is in SX10.
  // FIXME: Better to not use SX10 here
  if (TFI->hasBP(*MF)) {
    MachineInstrBuilder MIB =
        BuildMI(RestoreMBB, DL, TII->get(VE::LDrii), VE::SX17);
    MIB.addReg(VE::SX10);
    MIB.addImm(0);
    MIB.addImm(24);
    MIB.setMemRefs(MMOs);
  }
  BuildMI(RestoreMBB, DL, TII->get(VE::LEAzii), RestoreDestReg)
      .addImm(0)
      .addImm(0)
      .addImm(1);
  BuildMI(RestoreMBB, DL, TII->get(VE::BRCFLa_t)).addMBB(SinkMBB);
  RestoreMBB->addSuccessor(SinkMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

MachineBasicBlock *
VETargetLowering::emitEHSjLjLongJmp(MachineInstr &MI,
                                    MachineBasicBlock *MBB) const {
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  // Memory Reference.
  SmallVector<MachineMemOperand *, 2> MMOs(MI.memoperands_begin(),
                                           MI.memoperands_end());
  Register BufReg = MI.getOperand(0).getReg();

  Register Tmp = MRI.createVirtualRegister(&VE::I64RegClass);
  // Since FP is only updated here but NOT referenced, it's treated as GPR.
  Register FP = VE::SX9;
  Register SP = VE::SX11;

  MachineInstrBuilder MIB;

  MachineBasicBlock *ThisMBB = MBB;

  // For `call @llvm.eh.sjlj.longjmp(buf)`, we generate following instructions.
  //
  // ThisMBB:
  //   %fp = load buf[0]
  //   %jmp = load buf[1]
  //   %s10 = buf        ; Store an address of buf to SX10 for RestoreMBB
  //   %sp = load buf[2] ; generated by llvm.eh.sjlj.setjmp.
  //   jmp %jmp

  // Reload FP.
  MIB = BuildMI(*ThisMBB, MI, DL, TII->get(VE::LDrii), FP);
  MIB.addReg(BufReg);
  MIB.addImm(0);
  MIB.addImm(0);
  MIB.setMemRefs(MMOs);

  // Reload IP.
  MIB = BuildMI(*ThisMBB, MI, DL, TII->get(VE::LDrii), Tmp);
  MIB.addReg(BufReg);
  MIB.addImm(0);
  MIB.addImm(8);
  MIB.setMemRefs(MMOs);

  // Copy BufReg to SX10 for later use in setjmp.
  // FIXME: Better to not use SX10 here
  BuildMI(*ThisMBB, MI, DL, TII->get(VE::ORri), VE::SX10)
      .addReg(BufReg)
      .addImm(0);

  // Reload SP.
  MIB = BuildMI(*ThisMBB, MI, DL, TII->get(VE::LDrii), SP);
  MIB.add(MI.getOperand(0)); // we can preserve the kill flags here.
  MIB.addImm(0);
  MIB.addImm(16);
  MIB.setMemRefs(MMOs);

  // Jump.
  BuildMI(*ThisMBB, MI, DL, TII->get(VE::BCFLari_t))
      .addReg(Tmp, getKillRegState(true))
      .addImm(0);

  MI.eraseFromParent();
  return ThisMBB;
}

MachineBasicBlock *
VETargetLowering::emitSjLjDispatchBlock(MachineInstr &MI,
                                        MachineBasicBlock *BB) const {
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = BB->getParent();
  MachineFrameInfo &MFI = MF->getFrameInfo();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const VEInstrInfo *TII = Subtarget->getInstrInfo();
  int FI = MFI.getFunctionContextIndex();

  // Get a mapping of the call site numbers to all of the landing pads they're
  // associated with.
  DenseMap<unsigned, SmallVector<MachineBasicBlock *, 2>> CallSiteNumToLPad;
  unsigned MaxCSNum = 0;
  for (auto &MBB : *MF) {
    if (!MBB.isEHPad())
      continue;

    MCSymbol *Sym = nullptr;
    for (const auto &MI : MBB) {
      if (MI.isDebugInstr())
        continue;

      assert(MI.isEHLabel() && "expected EH_LABEL");
      Sym = MI.getOperand(0).getMCSymbol();
      break;
    }

    if (!MF->hasCallSiteLandingPad(Sym))
      continue;

    for (unsigned CSI : MF->getCallSiteLandingPad(Sym)) {
      CallSiteNumToLPad[CSI].push_back(&MBB);
      MaxCSNum = std::max(MaxCSNum, CSI);
    }
  }

  // Get an ordered list of the machine basic blocks for the jump table.
  std::vector<MachineBasicBlock *> LPadList;
  SmallPtrSet<MachineBasicBlock *, 32> InvokeBBs;
  LPadList.reserve(CallSiteNumToLPad.size());

  for (unsigned CSI = 1; CSI <= MaxCSNum; ++CSI) {
    for (auto &LP : CallSiteNumToLPad[CSI]) {
      LPadList.push_back(LP);
      InvokeBBs.insert(LP->pred_begin(), LP->pred_end());
    }
  }

  assert(!LPadList.empty() &&
         "No landing pad destinations for the dispatch jump table!");

  // The %fn_context is allocated like below (from --print-after=sjljehprepare):
  //   %fn_context = alloca { i8*, i64, [4 x i64], i8*, i8*, [5 x i8*] }
  //
  // This `[5 x i8*]` is jmpbuf, so jmpbuf[1] is FI+72.
  // First `i64` is callsite, so callsite is FI+8.
  static const int OffsetIC = 72;
  static const int OffsetCS = 8;

  // Create the MBBs for the dispatch code like following:
  //
  // ThisMBB:
  //   Prepare DispatchBB address and store it to buf[1].
  //   ...
  //
  // DispatchBB:
  //   %s15 = GETGOT iff isPositionIndependent
  //   %callsite = load callsite
  //   brgt.l.t #size of callsites, %callsite, DispContBB
  //
  // TrapBB:
  //   Call abort.
  //
  // DispContBB:
  //   %breg = address of jump table
  //   %pc = load and calculate next pc from %breg and %callsite
  //   jmp %pc

  // Shove the dispatch's address into the return slot in the function context.
  MachineBasicBlock *DispatchBB = MF->CreateMachineBasicBlock();
  DispatchBB->setIsEHPad(true);

  // Trap BB will causes trap like `assert(0)`.
  MachineBasicBlock *TrapBB = MF->CreateMachineBasicBlock();
  DispatchBB->addSuccessor(TrapBB);

  MachineBasicBlock *DispContBB = MF->CreateMachineBasicBlock();
  DispatchBB->addSuccessor(DispContBB);

  // Insert MBBs.
  MF->push_back(DispatchBB);
  MF->push_back(DispContBB);
  MF->push_back(TrapBB);

  // Insert code to call abort in the TrapBB.
  Register Abort = prepareSymbol(*TrapBB, TrapBB->end(), "abort", DL,
                                 /* Local */ false, /* Call */ true);
  BuildMI(TrapBB, DL, TII->get(VE::BSICrii), VE::SX10)
      .addReg(Abort, getKillRegState(true))
      .addImm(0)
      .addImm(0);

  // Insert code into the entry block that creates and registers the function
  // context.
  setupEntryBlockForSjLj(MI, BB, DispatchBB, FI, OffsetIC);

  // Create the jump table and associated information
  unsigned JTE = getJumpTableEncoding();
  MachineJumpTableInfo *JTI = MF->getOrCreateJumpTableInfo(JTE);
  unsigned MJTI = JTI->createJumpTableIndex(LPadList);

  const VERegisterInfo &RI = TII->getRegisterInfo();
  // Add a register mask with no preserved registers.  This results in all
  // registers being marked as clobbered.
  BuildMI(DispatchBB, DL, TII->get(VE::NOP))
      .addRegMask(RI.getNoPreservedMask());

  if (isPositionIndependent()) {
    // Force to generate GETGOT, since current implementation doesn't store GOT
    // register.
    BuildMI(DispatchBB, DL, TII->get(VE::GETGOT), VE::SX15);
  }

  // IReg is used as an index in a memory operand and therefore can't be SP
  const TargetRegisterClass *RC = &VE::I64RegClass;
  Register IReg = MRI.createVirtualRegister(RC);
  addFrameReference(BuildMI(DispatchBB, DL, TII->get(VE::LDLZXrii), IReg), FI,
                    OffsetCS);
  if (LPadList.size() < 64) {
    BuildMI(DispatchBB, DL, TII->get(VE::BRCFLir_t))
        .addImm(VECC::CC_ILE)
        .addImm(LPadList.size())
        .addReg(IReg)
        .addMBB(TrapBB);
  } else {
    assert(LPadList.size() <= 0x7FFFFFFF && "Too large Landing Pad!");
    Register TmpReg = MRI.createVirtualRegister(RC);
    BuildMI(DispatchBB, DL, TII->get(VE::LEAzii), TmpReg)
        .addImm(0)
        .addImm(0)
        .addImm(LPadList.size());
    BuildMI(DispatchBB, DL, TII->get(VE::BRCFLrr_t))
        .addImm(VECC::CC_ILE)
        .addReg(TmpReg, getKillRegState(true))
        .addReg(IReg)
        .addMBB(TrapBB);
  }

  Register BReg = MRI.createVirtualRegister(RC);
  Register Tmp1 = MRI.createVirtualRegister(RC);
  Register Tmp2 = MRI.createVirtualRegister(RC);

  if (isPositionIndependent()) {
    // Create following instructions for local linkage PIC code.
    //     lea    %Tmp1, .LJTI0_0@gotoff_lo
    //     and    %Tmp2, %Tmp1, (32)0
    //     lea.sl %BReg, .LJTI0_0@gotoff_hi(%Tmp2, %s15) ; %s15 is GOT
    BuildMI(DispContBB, DL, TII->get(VE::LEAzii), Tmp1)
        .addImm(0)
        .addImm(0)
        .addJumpTableIndex(MJTI, VEMCExpr::VK_VE_GOTOFF_LO32);
    BuildMI(DispContBB, DL, TII->get(VE::ANDrm), Tmp2)
        .addReg(Tmp1, getKillRegState(true))
        .addImm(M0(32));
    BuildMI(DispContBB, DL, TII->get(VE::LEASLrri), BReg)
        .addReg(VE::SX15)
        .addReg(Tmp2, getKillRegState(true))
        .addJumpTableIndex(MJTI, VEMCExpr::VK_VE_GOTOFF_HI32);
  } else {
    // Create following instructions for non-PIC code.
    //     lea     %Tmp1, .LJTI0_0@lo
    //     and     %Tmp2, %Tmp1, (32)0
    //     lea.sl  %BReg, .LJTI0_0@hi(%Tmp2)
    BuildMI(DispContBB, DL, TII->get(VE::LEAzii), Tmp1)
        .addImm(0)
        .addImm(0)
        .addJumpTableIndex(MJTI, VEMCExpr::VK_VE_LO32);
    BuildMI(DispContBB, DL, TII->get(VE::ANDrm), Tmp2)
        .addReg(Tmp1, getKillRegState(true))
        .addImm(M0(32));
    BuildMI(DispContBB, DL, TII->get(VE::LEASLrii), BReg)
        .addReg(Tmp2, getKillRegState(true))
        .addImm(0)
        .addJumpTableIndex(MJTI, VEMCExpr::VK_VE_HI32);
  }

  switch (JTE) {
  case MachineJumpTableInfo::EK_BlockAddress: {
    // Generate simple block address code for no-PIC model.
    //     sll %Tmp1, %IReg, 3
    //     lds %TReg, 0(%Tmp1, %BReg)
    //     bcfla %TReg

    Register TReg = MRI.createVirtualRegister(RC);
    Register Tmp1 = MRI.createVirtualRegister(RC);

    BuildMI(DispContBB, DL, TII->get(VE::SLLri), Tmp1)
        .addReg(IReg, getKillRegState(true))
        .addImm(3);
    BuildMI(DispContBB, DL, TII->get(VE::LDrri), TReg)
        .addReg(BReg, getKillRegState(true))
        .addReg(Tmp1, getKillRegState(true))
        .addImm(0);
    BuildMI(DispContBB, DL, TII->get(VE::BCFLari_t))
        .addReg(TReg, getKillRegState(true))
        .addImm(0);
    break;
  }
  case MachineJumpTableInfo::EK_Custom32: {
    // Generate block address code using differences from the function pointer
    // for PIC model.
    //     sll %Tmp1, %IReg, 2
    //     ldl.zx %OReg, 0(%Tmp1, %BReg)
    //     Prepare function address in BReg2.
    //     adds.l %TReg, %BReg2, %OReg
    //     bcfla %TReg

    assert(isPositionIndependent());
    Register OReg = MRI.createVirtualRegister(RC);
    Register TReg = MRI.createVirtualRegister(RC);
    Register Tmp1 = MRI.createVirtualRegister(RC);

    BuildMI(DispContBB, DL, TII->get(VE::SLLri), Tmp1)
        .addReg(IReg, getKillRegState(true))
        .addImm(2);
    BuildMI(DispContBB, DL, TII->get(VE::LDLZXrri), OReg)
        .addReg(BReg, getKillRegState(true))
        .addReg(Tmp1, getKillRegState(true))
        .addImm(0);
    Register BReg2 =
        prepareSymbol(*DispContBB, DispContBB->end(),
                      DispContBB->getParent()->getName(), DL, /* Local */ true);
    BuildMI(DispContBB, DL, TII->get(VE::ADDSLrr), TReg)
        .addReg(OReg, getKillRegState(true))
        .addReg(BReg2, getKillRegState(true));
    BuildMI(DispContBB, DL, TII->get(VE::BCFLari_t))
        .addReg(TReg, getKillRegState(true))
        .addImm(0);
    break;
  }
  default:
    llvm_unreachable("Unexpected jump table encoding");
  }

  // Add the jump table entries as successors to the MBB.
  SmallPtrSet<MachineBasicBlock *, 8> SeenMBBs;
  for (auto &LP : LPadList)
    if (SeenMBBs.insert(LP).second)
      DispContBB->addSuccessor(LP);

  // N.B. the order the invoke BBs are processed in doesn't matter here.
  SmallVector<MachineBasicBlock *, 64> MBBLPads;
  const MCPhysReg *SavedRegs = MF->getRegInfo().getCalleeSavedRegs();
  for (MachineBasicBlock *MBB : InvokeBBs) {
    // Remove the landing pad successor from the invoke block and replace it
    // with the new dispatch block.
    // Keep a copy of Successors since it's modified inside the loop.
    SmallVector<MachineBasicBlock *, 8> Successors(MBB->succ_rbegin(),
                                                   MBB->succ_rend());
    // FIXME: Avoid quadratic complexity.
    for (auto *MBBS : Successors) {
      if (MBBS->isEHPad()) {
        MBB->removeSuccessor(MBBS);
        MBBLPads.push_back(MBBS);
      }
    }

    MBB->addSuccessor(DispatchBB);

    // Find the invoke call and mark all of the callee-saved registers as
    // 'implicit defined' so that they're spilled.  This prevents code from
    // moving instructions to before the EH block, where they will never be
    // executed.
    for (auto &II : reverse(*MBB)) {
      if (!II.isCall())
        continue;

      DenseMap<Register, bool> DefRegs;
      for (auto &MOp : II.operands())
        if (MOp.isReg())
          DefRegs[MOp.getReg()] = true;

      MachineInstrBuilder MIB(*MF, &II);
      for (unsigned RI = 0; SavedRegs[RI]; ++RI) {
        Register Reg = SavedRegs[RI];
        if (!DefRegs[Reg])
          MIB.addReg(Reg, RegState::ImplicitDefine | RegState::Dead);
      }

      break;
    }
  }

  // Mark all former landing pads as non-landing pads.  The dispatch is the only
  // landing pad now.
  for (auto &LP : MBBLPads)
    LP->setIsEHPad(false);

  // The instruction is gone now.
  MI.eraseFromParent();
  return BB;
}

MachineBasicBlock *
VETargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                              MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unknown Custom Instruction!");
  case VE::EH_SjLj_LongJmp:
    return emitEHSjLjLongJmp(MI, BB);
  case VE::EH_SjLj_SetJmp:
    return emitEHSjLjSetJmp(MI, BB);
  case VE::EH_SjLj_Setup_Dispatch:
    return emitSjLjDispatchBlock(MI, BB);
  }
}

static bool isSimm7(SDValue V) {
  EVT VT = V.getValueType();
  if (VT.isVector())
    return false;

  if (VT.isInteger()) {
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(V))
      return isInt<7>(C->getSExtValue());
  } else if (VT.isFloatingPoint()) {
    if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(V)) {
      if (VT == MVT::f32 || VT == MVT::f64) {
        const APInt &Imm = C->getValueAPF().bitcastToAPInt();
        uint64_t Val = Imm.getSExtValue();
        if (Imm.getBitWidth() == 32)
          Val <<= 32; // Immediate value of float place at higher bits on VE.
        return isInt<7>(Val);
      }
    }
  }
  return false;
}

static bool isMImm(SDValue V) {
  EVT VT = V.getValueType();
  if (VT.isVector())
    return false;

  if (VT.isInteger()) {
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(V))
      return isMImmVal(getImmVal(C));
  } else if (VT.isFloatingPoint()) {
    if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(V)) {
      if (VT == MVT::f32) {
        // Float value places at higher bits, so ignore lower 32 bits.
        return isMImm32Val(getFpImmVal(C) >> 32);
      } else if (VT == MVT::f64) {
        return isMImmVal(getFpImmVal(C));
      }
    }
  }
  return false;
}

static unsigned decideComp(EVT SrcVT, ISD::CondCode CC) {
  if (SrcVT.isFloatingPoint()) {
    if (SrcVT == MVT::f128)
      return VEISD::CMPQ;
    return VEISD::CMPF;
  }
  return isSignedIntSetCC(CC) ? VEISD::CMPI : VEISD::CMPU;
}

static EVT decideCompType(EVT SrcVT) {
  if (SrcVT == MVT::f128)
    return MVT::f64;
  return SrcVT;
}

static bool safeWithoutCompWithNull(EVT SrcVT, ISD::CondCode CC,
                                    bool WithCMov) {
  if (SrcVT.isFloatingPoint()) {
    // For the case of floating point setcc, only unordered comparison
    // or general comparison with -enable-no-nans-fp-math option reach
    // here, so it is safe even if values are NaN.  Only f128 doesn't
    // safe since VE uses f64 result of f128 comparison.
    return SrcVT != MVT::f128;
  }
  if (isIntEqualitySetCC(CC)) {
    // For the case of equal or not equal, it is safe without comparison with 0.
    return true;
  }
  if (WithCMov) {
    // For the case of integer setcc with cmov, all signed comparison with 0
    // are safe.
    return isSignedIntSetCC(CC);
  }
  // For the case of integer setcc, only signed 64 bits comparison is safe.
  // For unsigned, "CMPU 0x80000000, 0" has to be greater than 0, but it becomes
  // less than 0 witout CMPU.  For 32 bits, other half of 32 bits are
  // uncoditional, so it is not safe too without CMPI..
  return isSignedIntSetCC(CC) && SrcVT == MVT::i64;
}

static SDValue generateComparison(EVT VT, SDValue LHS, SDValue RHS,
                                  ISD::CondCode CC, bool WithCMov,
                                  const SDLoc &DL, SelectionDAG &DAG) {
  // Compare values.  If RHS is 0 and it is safe to calculate without
  // comparison, we don't generate an instruction for comparison.
  EVT CompVT = decideCompType(VT);
  if (CompVT == VT && safeWithoutCompWithNull(VT, CC, WithCMov) &&
      (isNullConstant(RHS) || isNullFPConstant(RHS))) {
    return LHS;
  }
  return DAG.getNode(decideComp(VT, CC), DL, CompVT, LHS, RHS);
}

SDValue VETargetLowering::combineSelect(SDNode *N,
                                        DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::SELECT &&
         "Should be called with a SELECT node");
  ISD::CondCode CC = ISD::CondCode::SETNE;
  SDValue Cond = N->getOperand(0);
  SDValue True = N->getOperand(1);
  SDValue False = N->getOperand(2);

  // We handle only scalar SELECT.
  EVT VT = N->getValueType(0);
  if (VT.isVector())
    return SDValue();

  // Peform combineSelect after leagalize DAG.
  if (!DCI.isAfterLegalizeDAG())
    return SDValue();

  EVT VT0 = Cond.getValueType();
  if (isMImm(True)) {
    // VE's condition move can handle MImm in True clause, so nothing to do.
  } else if (isMImm(False)) {
    // VE's condition move can handle MImm in True clause, so swap True and
    // False clauses if False has MImm value.  And, update condition code.
    std::swap(True, False);
    CC = getSetCCInverse(CC, VT0);
  }

  SDLoc DL(N);
  SelectionDAG &DAG = DCI.DAG;
  VECC::CondCode VECCVal;
  if (VT0.isFloatingPoint()) {
    VECCVal = fpCondCode2Fcc(CC);
  } else {
    VECCVal = intCondCode2Icc(CC);
  }
  SDValue Ops[] = {Cond, True, False,
                   DAG.getConstant(VECCVal, DL, MVT::i32)};
  return DAG.getNode(VEISD::CMOV, DL, VT, Ops);
}

SDValue VETargetLowering::combineSelectCC(SDNode *N,
                                          DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::SELECT_CC &&
         "Should be called with a SELECT_CC node");
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(4))->get();
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDValue True = N->getOperand(2);
  SDValue False = N->getOperand(3);

  // We handle only scalar SELECT_CC.
  EVT VT = N->getValueType(0);
  if (VT.isVector())
    return SDValue();

  // Peform combineSelectCC after leagalize DAG.
  if (!DCI.isAfterLegalizeDAG())
    return SDValue();

  // We handle only i32/i64/f32/f64/f128 comparisons.
  EVT LHSVT = LHS.getValueType();
  assert(LHSVT == RHS.getValueType());
  switch (LHSVT.getSimpleVT().SimpleTy) {
  case MVT::i32:
  case MVT::i64:
  case MVT::f32:
  case MVT::f64:
  case MVT::f128:
    break;
  default:
    // Return SDValue to let llvm handle other types.
    return SDValue();
  }

  if (isMImm(RHS)) {
    // VE's comparison can handle MImm in RHS, so nothing to do.
  } else if (isSimm7(RHS)) {
    // VE's comparison can handle Simm7 in LHS, so swap LHS and RHS, and
    // update condition code.
    std::swap(LHS, RHS);
    CC = getSetCCSwappedOperands(CC);
  }
  if (isMImm(True)) {
    // VE's condition move can handle MImm in True clause, so nothing to do.
  } else if (isMImm(False)) {
    // VE's condition move can handle MImm in True clause, so swap True and
    // False clauses if False has MImm value.  And, update condition code.
    std::swap(True, False);
    CC = getSetCCInverse(CC, LHSVT);
  }

  SDLoc DL(N);
  SelectionDAG &DAG = DCI.DAG;

  bool WithCMov = true;
  SDValue CompNode = generateComparison(LHSVT, LHS, RHS, CC, WithCMov, DL, DAG);

  VECC::CondCode VECCVal;
  if (LHSVT.isFloatingPoint()) {
    VECCVal = fpCondCode2Fcc(CC);
  } else {
    VECCVal = intCondCode2Icc(CC);
  }
  SDValue Ops[] = {CompNode, True, False,
                   DAG.getConstant(VECCVal, DL, MVT::i32)};
  return DAG.getNode(VEISD::CMOV, DL, VT, Ops);
}

static bool isI32InsnAllUses(const SDNode *User, const SDNode *N);
static bool isI32Insn(const SDNode *User, const SDNode *N) {
  switch (User->getOpcode()) {
  default:
    return false;
  case ISD::ADD:
  case ISD::SUB:
  case ISD::MUL:
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::SETCC:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::SHL:
  case ISD::SRA:
  case ISD::BSWAP:
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::BR_CC:
  case ISD::BITCAST:
  case ISD::ATOMIC_CMP_SWAP:
  case ISD::ATOMIC_SWAP:
  case VEISD::CMPU:
  case VEISD::CMPI:
    return true;
  case ISD::SRL:
    if (N->getOperand(0).getOpcode() != ISD::SRL)
      return true;
    // (srl (trunc (srl ...))) may be optimized by combining srl, so
    // doesn't optimize trunc now.
    return false;
  case ISD::SELECT_CC:
    if (User->getOperand(2).getNode() != N &&
        User->getOperand(3).getNode() != N)
      return true;
    return isI32InsnAllUses(User, N);
  case VEISD::CMOV:
    // CMOV in (cmov (trunc ...), true, false, int-comparison) is safe.
    // However, trunc in true or false clauses is not safe.
    if (User->getOperand(1).getNode() != N &&
        User->getOperand(2).getNode() != N &&
        isa<ConstantSDNode>(User->getOperand(3))) {
      VECC::CondCode VECCVal =
          static_cast<VECC::CondCode>(User->getConstantOperandVal(3));
      return isIntVECondCode(VECCVal);
    }
    [[fallthrough]];
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
  case ISD::SELECT:
  case ISD::CopyToReg:
    // Check all use of selections, bit operations, and copies.  If all of them
    // are safe, optimize truncate to extract_subreg.
    return isI32InsnAllUses(User, N);
  }
}

static bool isI32InsnAllUses(const SDNode *User, const SDNode *N) {
  // Check all use of User node.  If all of them are safe, optimize
  // truncate to extract_subreg.
  for (const SDNode *U : User->uses()) {
    switch (U->getOpcode()) {
    default:
      // If the use is an instruction which treats the source operand as i32,
      // it is safe to avoid truncate here.
      if (isI32Insn(U, N))
        continue;
      break;
    case ISD::ANY_EXTEND:
    case ISD::SIGN_EXTEND:
    case ISD::ZERO_EXTEND: {
      // Special optimizations to the combination of ext and trunc.
      // (ext ... (select ... (trunc ...))) is safe to avoid truncate here
      // since this truncate instruction clears higher 32 bits which is filled
      // by one of ext instructions later.
      assert(N->getValueType(0) == MVT::i32 &&
             "find truncate to not i32 integer");
      if (User->getOpcode() == ISD::SELECT_CC ||
          User->getOpcode() == ISD::SELECT || User->getOpcode() == VEISD::CMOV)
        continue;
      break;
    }
    }
    return false;
  }
  return true;
}

// Optimize TRUNCATE in DAG combining.  Optimizing it in CUSTOM lower is
// sometime too early.  Optimizing it in DAG pattern matching in VEInstrInfo.td
// is sometime too late.  So, doing it at here.
SDValue VETargetLowering::combineTRUNCATE(SDNode *N,
                                          DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::TRUNCATE &&
         "Should be called with a TRUNCATE node");

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  // We prefer to do this when all types are legal.
  if (!DCI.isAfterLegalizeDAG())
    return SDValue();

  // Skip combine TRUNCATE atm if the operand of TRUNCATE might be a constant.
  if (N->getOperand(0)->getOpcode() == ISD::SELECT_CC &&
      isa<ConstantSDNode>(N->getOperand(0)->getOperand(0)) &&
      isa<ConstantSDNode>(N->getOperand(0)->getOperand(1)))
    return SDValue();

  // Check all use of this TRUNCATE.
  for (const SDNode *User : N->uses()) {
    // Make sure that we're not going to replace TRUNCATE for non i32
    // instructions.
    //
    // FIXME: Although we could sometimes handle this, and it does occur in
    // practice that one of the condition inputs to the select is also one of
    // the outputs, we currently can't deal with this.
    if (isI32Insn(User, N))
      continue;

    return SDValue();
  }

  SDValue SubI32 = DAG.getTargetConstant(VE::sub_i32, DL, MVT::i32);
  return SDValue(DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG, DL, VT,
                                    N->getOperand(0), SubI32),
                 0);
}

SDValue VETargetLowering::PerformDAGCombine(SDNode *N,
                                            DAGCombinerInfo &DCI) const {
  switch (N->getOpcode()) {
  default:
    break;
  case ISD::SELECT:
    return combineSelect(N, DCI);
  case ISD::SELECT_CC:
    return combineSelectCC(N, DCI);
  case ISD::TRUNCATE:
    return combineTRUNCATE(N, DCI);
  }

  return SDValue();
}

//===----------------------------------------------------------------------===//
// VE Inline Assembly Support
//===----------------------------------------------------------------------===//

VETargetLowering::ConstraintType
VETargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'v': // vector registers
      return C_RegisterClass;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
VETargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                               StringRef Constraint,
                                               MVT VT) const {
  const TargetRegisterClass *RC = nullptr;
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
    case 'r':
      RC = &VE::I64RegClass;
      break;
    case 'v':
      RC = &VE::V64RegClass;
      break;
    }
    return std::make_pair(0U, RC);
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

//===----------------------------------------------------------------------===//
// VE Target Optimization Support
//===----------------------------------------------------------------------===//

unsigned VETargetLowering::getMinimumJumpTableEntries() const {
  // Specify 8 for PIC model to relieve the impact of PIC load instructions.
  if (isJumpTableRelative())
    return 8;

  return TargetLowering::getMinimumJumpTableEntries();
}

bool VETargetLowering::hasAndNot(SDValue Y) const {
  EVT VT = Y.getValueType();

  // VE doesn't have vector and not instruction.
  if (VT.isVector())
    return false;

  // VE allows different immediate values for X and Y where ~X & Y.
  // Only simm7 works for X, and only mimm works for Y on VE.  However, this
  // function is used to check whether an immediate value is OK for and-not
  // instruction as both X and Y.  Generating additional instruction to
  // retrieve an immediate value is no good since the purpose of this
  // function is to convert a series of 3 instructions to another series of
  // 3 instructions with better parallelism.  Therefore, we return false
  // for all immediate values now.
  // FIXME: Change hasAndNot function to have two operands to make it work
  //        correctly with Aurora VE.
  if (isa<ConstantSDNode>(Y))
    return false;

  // It's ok for generic registers.
  return true;
}

SDValue VETargetLowering::lowerEXTRACT_VECTOR_ELT(SDValue Op,
                                                  SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::EXTRACT_VECTOR_ELT && "Unknown opcode!");
  MVT VT = Op.getOperand(0).getSimpleValueType();

  // Special treatment for packed V64 types.
  assert(VT == MVT::v512i32 || VT == MVT::v512f32);
  (void)VT;
  // Example of codes:
  //   %packed_v = extractelt %vr, %idx / 2
  //   %v = %packed_v >> (%idx % 2 * 32)
  //   %res = %v & 0xffffffff

  SDValue Vec = Op.getOperand(0);
  SDValue Idx = Op.getOperand(1);
  SDLoc DL(Op);
  SDValue Result = Op;
  if (false /* Idx->isConstant() */) {
    // TODO: optimized implementation using constant values
  } else {
    SDValue Const1 = DAG.getConstant(1, DL, MVT::i64);
    SDValue HalfIdx = DAG.getNode(ISD::SRL, DL, MVT::i64, {Idx, Const1});
    SDValue PackedElt =
        SDValue(DAG.getMachineNode(VE::LVSvr, DL, MVT::i64, {Vec, HalfIdx}), 0);
    SDValue AndIdx = DAG.getNode(ISD::AND, DL, MVT::i64, {Idx, Const1});
    SDValue Shift = DAG.getNode(ISD::XOR, DL, MVT::i64, {AndIdx, Const1});
    SDValue Const5 = DAG.getConstant(5, DL, MVT::i64);
    Shift = DAG.getNode(ISD::SHL, DL, MVT::i64, {Shift, Const5});
    PackedElt = DAG.getNode(ISD::SRL, DL, MVT::i64, {PackedElt, Shift});
    SDValue Mask = DAG.getConstant(0xFFFFFFFFL, DL, MVT::i64);
    PackedElt = DAG.getNode(ISD::AND, DL, MVT::i64, {PackedElt, Mask});
    SDValue SubI32 = DAG.getTargetConstant(VE::sub_i32, DL, MVT::i32);
    Result = SDValue(DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG, DL,
                                        MVT::i32, PackedElt, SubI32),
                     0);

    if (Op.getSimpleValueType() == MVT::f32) {
      Result = DAG.getBitcast(MVT::f32, Result);
    } else {
      assert(Op.getSimpleValueType() == MVT::i32);
    }
  }
  return Result;
}

SDValue VETargetLowering::lowerINSERT_VECTOR_ELT(SDValue Op,
                                                 SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::INSERT_VECTOR_ELT && "Unknown opcode!");
  MVT VT = Op.getOperand(0).getSimpleValueType();

  // Special treatment for packed V64 types.
  assert(VT == MVT::v512i32 || VT == MVT::v512f32);
  (void)VT;
  // The v512i32 and v512f32 starts from upper bits (0..31).  This "upper
  // bits" required `val << 32` from C implementation's point of view.
  //
  // Example of codes:
  //   %packed_elt = extractelt %vr, (%idx >> 1)
  //   %shift = ((%idx & 1) ^ 1) << 5
  //   %packed_elt &= 0xffffffff00000000 >> shift
  //   %packed_elt |= (zext %val) << shift
  //   %vr = insertelt %vr, %packed_elt, (%idx >> 1)

  SDLoc DL(Op);
  SDValue Vec = Op.getOperand(0);
  SDValue Val = Op.getOperand(1);
  SDValue Idx = Op.getOperand(2);
  if (Idx.getSimpleValueType() == MVT::i32)
    Idx = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Idx);
  if (Val.getSimpleValueType() == MVT::f32)
    Val = DAG.getBitcast(MVT::i32, Val);
  assert(Val.getSimpleValueType() == MVT::i32);
  Val = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Val);

  SDValue Result = Op;
  if (false /* Idx->isConstant()*/) {
    // TODO: optimized implementation using constant values
  } else {
    SDValue Const1 = DAG.getConstant(1, DL, MVT::i64);
    SDValue HalfIdx = DAG.getNode(ISD::SRL, DL, MVT::i64, {Idx, Const1});
    SDValue PackedElt =
        SDValue(DAG.getMachineNode(VE::LVSvr, DL, MVT::i64, {Vec, HalfIdx}), 0);
    SDValue AndIdx = DAG.getNode(ISD::AND, DL, MVT::i64, {Idx, Const1});
    SDValue Shift = DAG.getNode(ISD::XOR, DL, MVT::i64, {AndIdx, Const1});
    SDValue Const5 = DAG.getConstant(5, DL, MVT::i64);
    Shift = DAG.getNode(ISD::SHL, DL, MVT::i64, {Shift, Const5});
    SDValue Mask = DAG.getConstant(0xFFFFFFFF00000000L, DL, MVT::i64);
    Mask = DAG.getNode(ISD::SRL, DL, MVT::i64, {Mask, Shift});
    PackedElt = DAG.getNode(ISD::AND, DL, MVT::i64, {PackedElt, Mask});
    Val = DAG.getNode(ISD::SHL, DL, MVT::i64, {Val, Shift});
    PackedElt = DAG.getNode(ISD::OR, DL, MVT::i64, {PackedElt, Val});
    Result =
        SDValue(DAG.getMachineNode(VE::LSVrr_v, DL, Vec.getSimpleValueType(),
                                   {HalfIdx, PackedElt, Vec}),
                0);
  }
  return Result;
}
