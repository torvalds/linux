//===-- MSP430ISelLowering.cpp - MSP430 DAG Lowering Implementation  ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MSP430TargetLowering class.
//
//===----------------------------------------------------------------------===//

#include "MSP430ISelLowering.h"
#include "MSP430.h"
#include "MSP430MachineFunctionInfo.h"
#include "MSP430Subtarget.h"
#include "MSP430TargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "msp430-lower"

static cl::opt<bool>MSP430NoLegalImmediate(
  "msp430-no-legal-immediate", cl::Hidden,
  cl::desc("Enable non legal immediates (for testing purposes only)"),
  cl::init(false));

MSP430TargetLowering::MSP430TargetLowering(const TargetMachine &TM,
                                           const MSP430Subtarget &STI)
    : TargetLowering(TM) {

  // Set up the register classes.
  addRegisterClass(MVT::i8,  &MSP430::GR8RegClass);
  addRegisterClass(MVT::i16, &MSP430::GR16RegClass);

  // Compute derived properties from the register classes
  computeRegisterProperties(STI.getRegisterInfo());

  // Provide all sorts of operation actions
  setStackPointerRegisterToSaveRestore(MSP430::SP);
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent); // FIXME: Is this correct?

  // We have post-incremented loads / stores.
  setIndexedLoadAction(ISD::POST_INC, MVT::i8, Legal);
  setIndexedLoadAction(ISD::POST_INC, MVT::i16, Legal);

  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD,  VT, MVT::i1,  Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1,  Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1,  Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i8,  Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i16, Expand);
  }

  // We don't have any truncstores
  setTruncStoreAction(MVT::i16, MVT::i8, Expand);

  setOperationAction(ISD::SRA,              MVT::i8,    Custom);
  setOperationAction(ISD::SHL,              MVT::i8,    Custom);
  setOperationAction(ISD::SRL,              MVT::i8,    Custom);
  setOperationAction(ISD::SRA,              MVT::i16,   Custom);
  setOperationAction(ISD::SHL,              MVT::i16,   Custom);
  setOperationAction(ISD::SRL,              MVT::i16,   Custom);
  setOperationAction(ISD::ROTL,             MVT::i8,    Expand);
  setOperationAction(ISD::ROTR,             MVT::i8,    Expand);
  setOperationAction(ISD::ROTL,             MVT::i16,   Expand);
  setOperationAction(ISD::ROTR,             MVT::i16,   Expand);
  setOperationAction(ISD::GlobalAddress,    MVT::i16,   Custom);
  setOperationAction(ISD::ExternalSymbol,   MVT::i16,   Custom);
  setOperationAction(ISD::BlockAddress,     MVT::i16,   Custom);
  setOperationAction(ISD::BR_JT,            MVT::Other, Expand);
  setOperationAction(ISD::BR_CC,            MVT::i8,    Custom);
  setOperationAction(ISD::BR_CC,            MVT::i16,   Custom);
  setOperationAction(ISD::BRCOND,           MVT::Other, Expand);
  setOperationAction(ISD::SETCC,            MVT::i8,    Custom);
  setOperationAction(ISD::SETCC,            MVT::i16,   Custom);
  setOperationAction(ISD::SELECT,           MVT::i8,    Expand);
  setOperationAction(ISD::SELECT,           MVT::i16,   Expand);
  setOperationAction(ISD::SELECT_CC,        MVT::i8,    Custom);
  setOperationAction(ISD::SELECT_CC,        MVT::i16,   Custom);
  setOperationAction(ISD::SIGN_EXTEND,      MVT::i16,   Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i8, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i16, Expand);
  setOperationAction(ISD::STACKSAVE,        MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE,     MVT::Other, Expand);

  setOperationAction(ISD::CTTZ,             MVT::i8,    Expand);
  setOperationAction(ISD::CTTZ,             MVT::i16,   Expand);
  setOperationAction(ISD::CTLZ,             MVT::i8,    Expand);
  setOperationAction(ISD::CTLZ,             MVT::i16,   Expand);
  setOperationAction(ISD::CTPOP,            MVT::i8,    Expand);
  setOperationAction(ISD::CTPOP,            MVT::i16,   Expand);

  setOperationAction(ISD::SHL_PARTS,        MVT::i8,    Expand);
  setOperationAction(ISD::SHL_PARTS,        MVT::i16,   Expand);
  setOperationAction(ISD::SRL_PARTS,        MVT::i8,    Expand);
  setOperationAction(ISD::SRL_PARTS,        MVT::i16,   Expand);
  setOperationAction(ISD::SRA_PARTS,        MVT::i8,    Expand);
  setOperationAction(ISD::SRA_PARTS,        MVT::i16,   Expand);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1,   Expand);

  // FIXME: Implement efficiently multiplication by a constant
  setOperationAction(ISD::MUL,              MVT::i8,    Promote);
  setOperationAction(ISD::MULHS,            MVT::i8,    Promote);
  setOperationAction(ISD::MULHU,            MVT::i8,    Promote);
  setOperationAction(ISD::SMUL_LOHI,        MVT::i8,    Promote);
  setOperationAction(ISD::UMUL_LOHI,        MVT::i8,    Promote);
  setOperationAction(ISD::MUL,              MVT::i16,   LibCall);
  setOperationAction(ISD::MULHS,            MVT::i16,   Expand);
  setOperationAction(ISD::MULHU,            MVT::i16,   Expand);
  setOperationAction(ISD::SMUL_LOHI,        MVT::i16,   Expand);
  setOperationAction(ISD::UMUL_LOHI,        MVT::i16,   Expand);

  setOperationAction(ISD::UDIV,             MVT::i8,    Promote);
  setOperationAction(ISD::UDIVREM,          MVT::i8,    Promote);
  setOperationAction(ISD::UREM,             MVT::i8,    Promote);
  setOperationAction(ISD::SDIV,             MVT::i8,    Promote);
  setOperationAction(ISD::SDIVREM,          MVT::i8,    Promote);
  setOperationAction(ISD::SREM,             MVT::i8,    Promote);
  setOperationAction(ISD::UDIV,             MVT::i16,   LibCall);
  setOperationAction(ISD::UDIVREM,          MVT::i16,   Expand);
  setOperationAction(ISD::UREM,             MVT::i16,   LibCall);
  setOperationAction(ISD::SDIV,             MVT::i16,   LibCall);
  setOperationAction(ISD::SDIVREM,          MVT::i16,   Expand);
  setOperationAction(ISD::SREM,             MVT::i16,   LibCall);

  // varargs support
  setOperationAction(ISD::VASTART,          MVT::Other, Custom);
  setOperationAction(ISD::VAARG,            MVT::Other, Expand);
  setOperationAction(ISD::VAEND,            MVT::Other, Expand);
  setOperationAction(ISD::VACOPY,           MVT::Other, Expand);
  setOperationAction(ISD::JumpTable,        MVT::i16,   Custom);

  // EABI Libcalls - EABI Section 6.2
  const struct {
    const RTLIB::Libcall Op;
    const char * const Name;
    const ISD::CondCode Cond;
  } LibraryCalls[] = {
    // Floating point conversions - EABI Table 6
    { RTLIB::FPROUND_F64_F32,   "__mspabi_cvtdf",   ISD::SETCC_INVALID },
    { RTLIB::FPEXT_F32_F64,     "__mspabi_cvtfd",   ISD::SETCC_INVALID },
    // The following is NOT implemented in libgcc
    //{ RTLIB::FPTOSINT_F64_I16,  "__mspabi_fixdi", ISD::SETCC_INVALID },
    { RTLIB::FPTOSINT_F64_I32,  "__mspabi_fixdli",  ISD::SETCC_INVALID },
    { RTLIB::FPTOSINT_F64_I64,  "__mspabi_fixdlli", ISD::SETCC_INVALID },
    // The following is NOT implemented in libgcc
    //{ RTLIB::FPTOUINT_F64_I16,  "__mspabi_fixdu", ISD::SETCC_INVALID },
    { RTLIB::FPTOUINT_F64_I32,  "__mspabi_fixdul",  ISD::SETCC_INVALID },
    { RTLIB::FPTOUINT_F64_I64,  "__mspabi_fixdull", ISD::SETCC_INVALID },
    // The following is NOT implemented in libgcc
    //{ RTLIB::FPTOSINT_F32_I16,  "__mspabi_fixfi", ISD::SETCC_INVALID },
    { RTLIB::FPTOSINT_F32_I32,  "__mspabi_fixfli",  ISD::SETCC_INVALID },
    { RTLIB::FPTOSINT_F32_I64,  "__mspabi_fixflli", ISD::SETCC_INVALID },
    // The following is NOT implemented in libgcc
    //{ RTLIB::FPTOUINT_F32_I16,  "__mspabi_fixfu", ISD::SETCC_INVALID },
    { RTLIB::FPTOUINT_F32_I32,  "__mspabi_fixful",  ISD::SETCC_INVALID },
    { RTLIB::FPTOUINT_F32_I64,  "__mspabi_fixfull", ISD::SETCC_INVALID },
    // TODO The following IS implemented in libgcc
    //{ RTLIB::SINTTOFP_I16_F64,  "__mspabi_fltid", ISD::SETCC_INVALID },
    { RTLIB::SINTTOFP_I32_F64,  "__mspabi_fltlid",  ISD::SETCC_INVALID },
    // TODO The following IS implemented in libgcc but is not in the EABI
    { RTLIB::SINTTOFP_I64_F64,  "__mspabi_fltllid", ISD::SETCC_INVALID },
    // TODO The following IS implemented in libgcc
    //{ RTLIB::UINTTOFP_I16_F64,  "__mspabi_fltud", ISD::SETCC_INVALID },
    { RTLIB::UINTTOFP_I32_F64,  "__mspabi_fltuld",  ISD::SETCC_INVALID },
    // The following IS implemented in libgcc but is not in the EABI
    { RTLIB::UINTTOFP_I64_F64,  "__mspabi_fltulld", ISD::SETCC_INVALID },
    // TODO The following IS implemented in libgcc
    //{ RTLIB::SINTTOFP_I16_F32,  "__mspabi_fltif", ISD::SETCC_INVALID },
    { RTLIB::SINTTOFP_I32_F32,  "__mspabi_fltlif",  ISD::SETCC_INVALID },
    // TODO The following IS implemented in libgcc but is not in the EABI
    { RTLIB::SINTTOFP_I64_F32,  "__mspabi_fltllif", ISD::SETCC_INVALID },
    // TODO The following IS implemented in libgcc
    //{ RTLIB::UINTTOFP_I16_F32,  "__mspabi_fltuf", ISD::SETCC_INVALID },
    { RTLIB::UINTTOFP_I32_F32,  "__mspabi_fltulf",  ISD::SETCC_INVALID },
    // The following IS implemented in libgcc but is not in the EABI
    { RTLIB::UINTTOFP_I64_F32,  "__mspabi_fltullf", ISD::SETCC_INVALID },

    // Floating point comparisons - EABI Table 7
    { RTLIB::OEQ_F64, "__mspabi_cmpd", ISD::SETEQ },
    { RTLIB::UNE_F64, "__mspabi_cmpd", ISD::SETNE },
    { RTLIB::OGE_F64, "__mspabi_cmpd", ISD::SETGE },
    { RTLIB::OLT_F64, "__mspabi_cmpd", ISD::SETLT },
    { RTLIB::OLE_F64, "__mspabi_cmpd", ISD::SETLE },
    { RTLIB::OGT_F64, "__mspabi_cmpd", ISD::SETGT },
    { RTLIB::OEQ_F32, "__mspabi_cmpf", ISD::SETEQ },
    { RTLIB::UNE_F32, "__mspabi_cmpf", ISD::SETNE },
    { RTLIB::OGE_F32, "__mspabi_cmpf", ISD::SETGE },
    { RTLIB::OLT_F32, "__mspabi_cmpf", ISD::SETLT },
    { RTLIB::OLE_F32, "__mspabi_cmpf", ISD::SETLE },
    { RTLIB::OGT_F32, "__mspabi_cmpf", ISD::SETGT },

    // Floating point arithmetic - EABI Table 8
    { RTLIB::ADD_F64,  "__mspabi_addd", ISD::SETCC_INVALID },
    { RTLIB::ADD_F32,  "__mspabi_addf", ISD::SETCC_INVALID },
    { RTLIB::DIV_F64,  "__mspabi_divd", ISD::SETCC_INVALID },
    { RTLIB::DIV_F32,  "__mspabi_divf", ISD::SETCC_INVALID },
    { RTLIB::MUL_F64,  "__mspabi_mpyd", ISD::SETCC_INVALID },
    { RTLIB::MUL_F32,  "__mspabi_mpyf", ISD::SETCC_INVALID },
    { RTLIB::SUB_F64,  "__mspabi_subd", ISD::SETCC_INVALID },
    { RTLIB::SUB_F32,  "__mspabi_subf", ISD::SETCC_INVALID },
    // The following are NOT implemented in libgcc
    // { RTLIB::NEG_F64,  "__mspabi_negd", ISD::SETCC_INVALID },
    // { RTLIB::NEG_F32,  "__mspabi_negf", ISD::SETCC_INVALID },

    // Universal Integer Operations - EABI Table 9
    { RTLIB::SDIV_I16,   "__mspabi_divi", ISD::SETCC_INVALID },
    { RTLIB::SDIV_I32,   "__mspabi_divli", ISD::SETCC_INVALID },
    { RTLIB::SDIV_I64,   "__mspabi_divlli", ISD::SETCC_INVALID },
    { RTLIB::UDIV_I16,   "__mspabi_divu", ISD::SETCC_INVALID },
    { RTLIB::UDIV_I32,   "__mspabi_divul", ISD::SETCC_INVALID },
    { RTLIB::UDIV_I64,   "__mspabi_divull", ISD::SETCC_INVALID },
    { RTLIB::SREM_I16,   "__mspabi_remi", ISD::SETCC_INVALID },
    { RTLIB::SREM_I32,   "__mspabi_remli", ISD::SETCC_INVALID },
    { RTLIB::SREM_I64,   "__mspabi_remlli", ISD::SETCC_INVALID },
    { RTLIB::UREM_I16,   "__mspabi_remu", ISD::SETCC_INVALID },
    { RTLIB::UREM_I32,   "__mspabi_remul", ISD::SETCC_INVALID },
    { RTLIB::UREM_I64,   "__mspabi_remull", ISD::SETCC_INVALID },

    // Bitwise Operations - EABI Table 10
    // TODO: __mspabi_[srli/srai/slli] ARE implemented in libgcc
    { RTLIB::SRL_I32,    "__mspabi_srll", ISD::SETCC_INVALID },
    { RTLIB::SRA_I32,    "__mspabi_sral", ISD::SETCC_INVALID },
    { RTLIB::SHL_I32,    "__mspabi_slll", ISD::SETCC_INVALID },
    // __mspabi_[srlll/srall/sllll/rlli/rlll] are NOT implemented in libgcc

  };

  for (const auto &LC : LibraryCalls) {
    setLibcallName(LC.Op, LC.Name);
    if (LC.Cond != ISD::SETCC_INVALID)
      setCmpLibcallCC(LC.Op, LC.Cond);
  }

  if (STI.hasHWMult16()) {
    const struct {
      const RTLIB::Libcall Op;
      const char * const Name;
    } LibraryCalls[] = {
      // Integer Multiply - EABI Table 9
      { RTLIB::MUL_I16,   "__mspabi_mpyi_hw" },
      { RTLIB::MUL_I32,   "__mspabi_mpyl_hw" },
      { RTLIB::MUL_I64,   "__mspabi_mpyll_hw" },
      // TODO The __mspabi_mpysl*_hw functions ARE implemented in libgcc
      // TODO The __mspabi_mpyul*_hw functions ARE implemented in libgcc
    };
    for (const auto &LC : LibraryCalls) {
      setLibcallName(LC.Op, LC.Name);
    }
  } else if (STI.hasHWMult32()) {
    const struct {
      const RTLIB::Libcall Op;
      const char * const Name;
    } LibraryCalls[] = {
      // Integer Multiply - EABI Table 9
      { RTLIB::MUL_I16,   "__mspabi_mpyi_hw" },
      { RTLIB::MUL_I32,   "__mspabi_mpyl_hw32" },
      { RTLIB::MUL_I64,   "__mspabi_mpyll_hw32" },
      // TODO The __mspabi_mpysl*_hw32 functions ARE implemented in libgcc
      // TODO The __mspabi_mpyul*_hw32 functions ARE implemented in libgcc
    };
    for (const auto &LC : LibraryCalls) {
      setLibcallName(LC.Op, LC.Name);
    }
  } else if (STI.hasHWMultF5()) {
    const struct {
      const RTLIB::Libcall Op;
      const char * const Name;
    } LibraryCalls[] = {
      // Integer Multiply - EABI Table 9
      { RTLIB::MUL_I16,   "__mspabi_mpyi_f5hw" },
      { RTLIB::MUL_I32,   "__mspabi_mpyl_f5hw" },
      { RTLIB::MUL_I64,   "__mspabi_mpyll_f5hw" },
      // TODO The __mspabi_mpysl*_f5hw functions ARE implemented in libgcc
      // TODO The __mspabi_mpyul*_f5hw functions ARE implemented in libgcc
    };
    for (const auto &LC : LibraryCalls) {
      setLibcallName(LC.Op, LC.Name);
    }
  } else { // NoHWMult
    const struct {
      const RTLIB::Libcall Op;
      const char * const Name;
    } LibraryCalls[] = {
      // Integer Multiply - EABI Table 9
      { RTLIB::MUL_I16,   "__mspabi_mpyi" },
      { RTLIB::MUL_I32,   "__mspabi_mpyl" },
      { RTLIB::MUL_I64,   "__mspabi_mpyll" },
      // The __mspabi_mpysl* functions are NOT implemented in libgcc
      // The __mspabi_mpyul* functions are NOT implemented in libgcc
    };
    for (const auto &LC : LibraryCalls) {
      setLibcallName(LC.Op, LC.Name);
    }
    setLibcallCallingConv(RTLIB::MUL_I64, CallingConv::MSP430_BUILTIN);
  }

  // Several of the runtime library functions use a special calling conv
  setLibcallCallingConv(RTLIB::UDIV_I64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::UREM_I64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::SDIV_I64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::SREM_I64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::ADD_F64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::SUB_F64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::MUL_F64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::DIV_F64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::OEQ_F64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::UNE_F64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::OGE_F64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::OLT_F64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::OLE_F64, CallingConv::MSP430_BUILTIN);
  setLibcallCallingConv(RTLIB::OGT_F64, CallingConv::MSP430_BUILTIN);
  // TODO: __mspabi_srall, __mspabi_srlll, __mspabi_sllll

  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));
  setMaxAtomicSizeInBitsSupported(0);
}

SDValue MSP430TargetLowering::LowerOperation(SDValue Op,
                                             SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::SHL: // FALLTHROUGH
  case ISD::SRL:
  case ISD::SRA:              return LowerShifts(Op, DAG);
  case ISD::GlobalAddress:    return LowerGlobalAddress(Op, DAG);
  case ISD::BlockAddress:     return LowerBlockAddress(Op, DAG);
  case ISD::ExternalSymbol:   return LowerExternalSymbol(Op, DAG);
  case ISD::SETCC:            return LowerSETCC(Op, DAG);
  case ISD::BR_CC:            return LowerBR_CC(Op, DAG);
  case ISD::SELECT_CC:        return LowerSELECT_CC(Op, DAG);
  case ISD::SIGN_EXTEND:      return LowerSIGN_EXTEND(Op, DAG);
  case ISD::RETURNADDR:       return LowerRETURNADDR(Op, DAG);
  case ISD::FRAMEADDR:        return LowerFRAMEADDR(Op, DAG);
  case ISD::VASTART:          return LowerVASTART(Op, DAG);
  case ISD::JumpTable:        return LowerJumpTable(Op, DAG);
  default:
    llvm_unreachable("unimplemented operand");
  }
}

// Define non profitable transforms into shifts
bool MSP430TargetLowering::shouldAvoidTransformToShift(EVT VT,
                                                       unsigned Amount) const {
  return !(Amount == 8 || Amount == 9 || Amount<=2);
}

// Implemented to verify test case assertions in
// tests/codegen/msp430/shift-amount-threshold-b.ll
bool MSP430TargetLowering::isLegalICmpImmediate(int64_t Immed) const {
  if (MSP430NoLegalImmediate)
    return Immed >= -32 && Immed < 32;
  return TargetLowering::isLegalICmpImmediate(Immed);
}

//===----------------------------------------------------------------------===//
//                       MSP430 Inline Assembly Support
//===----------------------------------------------------------------------===//

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
TargetLowering::ConstraintType
MSP430TargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      return C_RegisterClass;
    default:
      break;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
MSP430TargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {
  if (Constraint.size() == 1) {
    // GCC Constraint Letters
    switch (Constraint[0]) {
    default: break;
    case 'r':   // GENERAL_REGS
      if (VT == MVT::i8)
        return std::make_pair(0U, &MSP430::GR8RegClass);

      return std::make_pair(0U, &MSP430::GR16RegClass);
    }
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//===----------------------------------------------------------------------===//

#include "MSP430GenCallingConv.inc"

/// For each argument in a function store the number of pieces it is composed
/// of.
template<typename ArgT>
static void ParseFunctionArgs(const SmallVectorImpl<ArgT> &Args,
                              SmallVectorImpl<unsigned> &Out) {
  unsigned CurrentArgIndex;

  if (Args.empty())
    return;

  CurrentArgIndex = Args[0].OrigArgIndex;
  Out.push_back(0);

  for (auto &Arg : Args) {
    if (CurrentArgIndex == Arg.OrigArgIndex) {
      Out.back() += 1;
    } else {
      Out.push_back(1);
      CurrentArgIndex = Arg.OrigArgIndex;
    }
  }
}

static void AnalyzeVarArgs(CCState &State,
                           const SmallVectorImpl<ISD::OutputArg> &Outs) {
  State.AnalyzeCallOperands(Outs, CC_MSP430_AssignStack);
}

static void AnalyzeVarArgs(CCState &State,
                           const SmallVectorImpl<ISD::InputArg> &Ins) {
  State.AnalyzeFormalArguments(Ins, CC_MSP430_AssignStack);
}

/// Analyze incoming and outgoing function arguments. We need custom C++ code
/// to handle special constraints in the ABI like reversing the order of the
/// pieces of splitted arguments. In addition, all pieces of a certain argument
/// have to be passed either using registers or the stack but never mixing both.
template<typename ArgT>
static void AnalyzeArguments(CCState &State,
                             SmallVectorImpl<CCValAssign> &ArgLocs,
                             const SmallVectorImpl<ArgT> &Args) {
  static const MCPhysReg CRegList[] = {
    MSP430::R12, MSP430::R13, MSP430::R14, MSP430::R15
  };
  static const unsigned CNbRegs = std::size(CRegList);
  static const MCPhysReg BuiltinRegList[] = {
    MSP430::R8, MSP430::R9, MSP430::R10, MSP430::R11,
    MSP430::R12, MSP430::R13, MSP430::R14, MSP430::R15
  };
  static const unsigned BuiltinNbRegs = std::size(BuiltinRegList);

  ArrayRef<MCPhysReg> RegList;
  unsigned NbRegs;

  bool Builtin = (State.getCallingConv() == CallingConv::MSP430_BUILTIN);
  if (Builtin) {
    RegList = BuiltinRegList;
    NbRegs = BuiltinNbRegs;
  } else {
    RegList = CRegList;
    NbRegs = CNbRegs;
  }

  if (State.isVarArg()) {
    AnalyzeVarArgs(State, Args);
    return;
  }

  SmallVector<unsigned, 4> ArgsParts;
  ParseFunctionArgs(Args, ArgsParts);

  if (Builtin) {
    assert(ArgsParts.size() == 2 &&
        "Builtin calling convention requires two arguments");
  }

  unsigned RegsLeft = NbRegs;
  bool UsedStack = false;
  unsigned ValNo = 0;

  for (unsigned i = 0, e = ArgsParts.size(); i != e; i++) {
    MVT ArgVT = Args[ValNo].VT;
    ISD::ArgFlagsTy ArgFlags = Args[ValNo].Flags;
    MVT LocVT = ArgVT;
    CCValAssign::LocInfo LocInfo = CCValAssign::Full;

    // Promote i8 to i16
    if (LocVT == MVT::i8) {
      LocVT = MVT::i16;
      if (ArgFlags.isSExt())
          LocInfo = CCValAssign::SExt;
      else if (ArgFlags.isZExt())
          LocInfo = CCValAssign::ZExt;
      else
          LocInfo = CCValAssign::AExt;
    }

    // Handle byval arguments
    if (ArgFlags.isByVal()) {
      State.HandleByVal(ValNo++, ArgVT, LocVT, LocInfo, 2, Align(2), ArgFlags);
      continue;
    }

    unsigned Parts = ArgsParts[i];

    if (Builtin) {
      assert(Parts == 4 &&
          "Builtin calling convention requires 64-bit arguments");
    }

    if (!UsedStack && Parts == 2 && RegsLeft == 1) {
      // Special case for 32-bit register split, see EABI section 3.3.3
      unsigned Reg = State.AllocateReg(RegList);
      State.addLoc(CCValAssign::getReg(ValNo++, ArgVT, Reg, LocVT, LocInfo));
      RegsLeft -= 1;

      UsedStack = true;
      CC_MSP430_AssignStack(ValNo++, ArgVT, LocVT, LocInfo, ArgFlags, State);
    } else if (Parts <= RegsLeft) {
      for (unsigned j = 0; j < Parts; j++) {
        unsigned Reg = State.AllocateReg(RegList);
        State.addLoc(CCValAssign::getReg(ValNo++, ArgVT, Reg, LocVT, LocInfo));
        RegsLeft--;
      }
    } else {
      UsedStack = true;
      for (unsigned j = 0; j < Parts; j++)
        CC_MSP430_AssignStack(ValNo++, ArgVT, LocVT, LocInfo, ArgFlags, State);
    }
  }
}

static void AnalyzeRetResult(CCState &State,
                             const SmallVectorImpl<ISD::InputArg> &Ins) {
  State.AnalyzeCallResult(Ins, RetCC_MSP430);
}

static void AnalyzeRetResult(CCState &State,
                             const SmallVectorImpl<ISD::OutputArg> &Outs) {
  State.AnalyzeReturn(Outs, RetCC_MSP430);
}

template<typename ArgT>
static void AnalyzeReturnValues(CCState &State,
                                SmallVectorImpl<CCValAssign> &RVLocs,
                                const SmallVectorImpl<ArgT> &Args) {
  AnalyzeRetResult(State, Args);
}

SDValue MSP430TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::C:
  case CallingConv::Fast:
    return LowerCCCArguments(Chain, CallConv, isVarArg, Ins, dl, DAG, InVals);
  case CallingConv::MSP430_INTR:
    if (Ins.empty())
      return Chain;
    report_fatal_error("ISRs cannot have arguments");
  }
}

SDValue
MSP430TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG                     = CLI.DAG;
  SDLoc &dl                             = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals     = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins   = CLI.Ins;
  SDValue Chain                         = CLI.Chain;
  SDValue Callee                        = CLI.Callee;
  bool &isTailCall                      = CLI.IsTailCall;
  CallingConv::ID CallConv              = CLI.CallConv;
  bool isVarArg                         = CLI.IsVarArg;

  // MSP430 target does not yet support tail call optimization.
  isTailCall = false;

  switch (CallConv) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::MSP430_BUILTIN:
  case CallingConv::Fast:
  case CallingConv::C:
    return LowerCCCCallTo(Chain, Callee, CallConv, isVarArg, isTailCall,
                          Outs, OutVals, Ins, dl, DAG, InVals);
  case CallingConv::MSP430_INTR:
    report_fatal_error("ISRs cannot be called directly");
  }
}

/// LowerCCCArguments - transform physical registers into virtual registers and
/// generate load operations for arguments places on the stack.
// FIXME: struct return stuff
SDValue MSP430TargetLowering::LowerCCCArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  MSP430MachineFunctionInfo *FuncInfo = MF.getInfo<MSP430MachineFunctionInfo>();

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  AnalyzeArguments(CCInfo, ArgLocs, Ins);

  // Create frame index for the start of the first vararg value
  if (isVarArg) {
    unsigned Offset = CCInfo.getStackSize();
    FuncInfo->setVarArgsFrameIndex(MFI.CreateFixedObject(1, Offset, true));
  }

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    if (VA.isRegLoc()) {
      // Arguments passed in registers
      EVT RegVT = VA.getLocVT();
      switch (RegVT.getSimpleVT().SimpleTy) {
      default:
        {
#ifndef NDEBUG
          errs() << "LowerFormalArguments Unhandled argument type: "
                 << RegVT << "\n";
#endif
          llvm_unreachable(nullptr);
        }
      case MVT::i16:
        Register VReg = RegInfo.createVirtualRegister(&MSP430::GR16RegClass);
        RegInfo.addLiveIn(VA.getLocReg(), VReg);
        SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, VReg, RegVT);

        // If this is an 8-bit value, it is really passed promoted to 16
        // bits. Insert an assert[sz]ext to capture this, then truncate to the
        // right size.
        if (VA.getLocInfo() == CCValAssign::SExt)
          ArgValue = DAG.getNode(ISD::AssertSext, dl, RegVT, ArgValue,
                                 DAG.getValueType(VA.getValVT()));
        else if (VA.getLocInfo() == CCValAssign::ZExt)
          ArgValue = DAG.getNode(ISD::AssertZext, dl, RegVT, ArgValue,
                                 DAG.getValueType(VA.getValVT()));

        if (VA.getLocInfo() != CCValAssign::Full)
          ArgValue = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), ArgValue);

        InVals.push_back(ArgValue);
      }
    } else {
      // Only arguments passed on the stack should make it here.
      assert(VA.isMemLoc());

      SDValue InVal;
      ISD::ArgFlagsTy Flags = Ins[i].Flags;

      if (Flags.isByVal()) {
        MVT PtrVT = VA.getLocVT();
        int FI = MFI.CreateFixedObject(Flags.getByValSize(),
                                       VA.getLocMemOffset(), true);
        InVal = DAG.getFrameIndex(FI, PtrVT);
      } else {
        // Load the argument to a virtual register
        unsigned ObjSize = VA.getLocVT().getSizeInBits()/8;
        if (ObjSize > 2) {
            errs() << "LowerFormalArguments Unhandled argument type: "
                << VA.getLocVT() << "\n";
        }
        // Create the frame index object for this incoming parameter...
        int FI = MFI.CreateFixedObject(ObjSize, VA.getLocMemOffset(), true);

        // Create the SelectionDAG nodes corresponding to a load
        //from this parameter
        SDValue FIN = DAG.getFrameIndex(FI, MVT::i16);
        InVal = DAG.getLoad(
            VA.getLocVT(), dl, Chain, FIN,
            MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI));
      }

      InVals.push_back(InVal);
    }
  }

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    if (Ins[i].Flags.isSRet()) {
      Register Reg = FuncInfo->getSRetReturnReg();
      if (!Reg) {
        Reg = MF.getRegInfo().createVirtualRegister(
            getRegClassFor(MVT::i16));
        FuncInfo->setSRetReturnReg(Reg);
      }
      SDValue Copy = DAG.getCopyToReg(DAG.getEntryNode(), dl, Reg, InVals[i]);
      Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Copy, Chain);
    }
  }

  return Chain;
}

bool
MSP430TargetLowering::CanLowerReturn(CallingConv::ID CallConv,
                                     MachineFunction &MF,
                                     bool IsVarArg,
                                     const SmallVectorImpl<ISD::OutputArg> &Outs,
                                     LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_MSP430);
}

SDValue
MSP430TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                  bool isVarArg,
                                  const SmallVectorImpl<ISD::OutputArg> &Outs,
                                  const SmallVectorImpl<SDValue> &OutVals,
                                  const SDLoc &dl, SelectionDAG &DAG) const {

  MachineFunction &MF = DAG.getMachineFunction();

  // CCValAssign - represent the assignment of the return value to a location
  SmallVector<CCValAssign, 16> RVLocs;

  // ISRs cannot return any value.
  if (CallConv == CallingConv::MSP430_INTR && !Outs.empty())
    report_fatal_error("ISRs cannot return any value");

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Analize return values.
  AnalyzeReturnValues(CCInfo, RVLocs, Outs);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(),
                             OutVals[i], Glue);

    // Guarantee that all emitted copies are stuck together,
    // avoiding something bad.
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  if (MF.getFunction().hasStructRetAttr()) {
    MSP430MachineFunctionInfo *FuncInfo = MF.getInfo<MSP430MachineFunctionInfo>();
    Register Reg = FuncInfo->getSRetReturnReg();

    if (!Reg)
      llvm_unreachable("sret virtual register not created in entry block");

    MVT PtrVT = getFrameIndexTy(DAG.getDataLayout());
    SDValue Val =
      DAG.getCopyFromReg(Chain, dl, Reg, PtrVT);
    unsigned R12 = MSP430::R12;

    Chain = DAG.getCopyToReg(Chain, dl, R12, Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(R12, PtrVT));
  }

  unsigned Opc = (CallConv == CallingConv::MSP430_INTR ?
                  MSP430ISD::RETI_GLUE : MSP430ISD::RET_GLUE);

  RetOps[0] = Chain;  // Update chain.

  // Add the glue if we have it.
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(Opc, dl, MVT::Other, RetOps);
}

/// LowerCCCCallTo - functions arguments are copied from virtual regs to
/// (physical regs)/(stack frame), CALLSEQ_START and CALLSEQ_END are emitted.
SDValue MSP430TargetLowering::LowerCCCCallTo(
    SDValue Chain, SDValue Callee, CallingConv::ID CallConv, bool isVarArg,
    bool isTailCall, const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  AnalyzeArguments(CCInfo, ArgLocs, Outs);

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = CCInfo.getStackSize();
  MVT PtrVT = getFrameIndexTy(DAG.getDataLayout());

  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);

  SmallVector<std::pair<unsigned, SDValue>, 4> RegsToPass;
  SmallVector<SDValue, 12> MemOpChains;
  SDValue StackPtr;

  // Walk the register/memloc assignments, inserting copies/loads.
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];

    SDValue Arg = OutVals[i];

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
      default: llvm_unreachable("Unknown loc info!");
      case CCValAssign::Full: break;
      case CCValAssign::SExt:
        Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
        break;
      case CCValAssign::ZExt:
        Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
        break;
      case CCValAssign::AExt:
        Arg = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Arg);
        break;
    }

    // Arguments that can be passed on register must be kept at RegsToPass
    // vector
    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else {
      assert(VA.isMemLoc());

      if (!StackPtr.getNode())
        StackPtr = DAG.getCopyFromReg(Chain, dl, MSP430::SP, PtrVT);

      SDValue PtrOff =
          DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr,
                      DAG.getIntPtrConstant(VA.getLocMemOffset(), dl));

      SDValue MemOp;
      ISD::ArgFlagsTy Flags = Outs[i].Flags;

      if (Flags.isByVal()) {
        SDValue SizeNode = DAG.getConstant(Flags.getByValSize(), dl, MVT::i16);
        MemOp = DAG.getMemcpy(Chain, dl, PtrOff, Arg, SizeNode,
                              Flags.getNonZeroByValAlign(),
                              /*isVolatile*/ false,
                              /*AlwaysInline=*/true,
                              /*CI=*/nullptr, std::nullopt,
                              MachinePointerInfo(), MachinePointerInfo());
      } else {
        MemOp = DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo());
      }

      MemOpChains.push_back(MemOp);
    }
  }

  // Transform all store nodes into one single node because all store nodes are
  // independent of each other.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Build a sequence of copy-to-reg nodes chained together with token chain and
  // flag operands which copy the outgoing args into registers.  The InGlue in
  // necessary since all emitted instructions must be stuck together.
  SDValue InGlue;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InGlue);
    InGlue = Chain.getValue(1);
  }

  // If the callee is a GlobalAddress node (quite common, every direct call is)
  // turn it into a TargetGlobalAddress node so that legalize doesn't hack it.
  // Likewise ExternalSymbol -> TargetExternalSymbol.
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, MVT::i16);
  else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i16);

  // Returns a chain & a flag for retval copy to use.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  if (InGlue.getNode())
    Ops.push_back(InGlue);

  Chain = DAG.getNode(MSP430ISD::CALL, dl, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, dl);
  InGlue = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InGlue, CallConv, isVarArg, Ins, dl,
                         DAG, InVals);
}

/// LowerCallResult - Lower the result values of a call into the
/// appropriate copies out of appropriate physical registers.
///
SDValue MSP430TargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  AnalyzeReturnValues(CCInfo, RVLocs, Ins);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    Chain = DAG.getCopyFromReg(Chain, dl, RVLocs[i].getLocReg(),
                               RVLocs[i].getValVT(), InGlue).getValue(1);
    InGlue = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }

  return Chain;
}

SDValue MSP430TargetLowering::LowerShifts(SDValue Op,
                                          SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  SDNode* N = Op.getNode();
  EVT VT = Op.getValueType();
  SDLoc dl(N);

  // Expand non-constant shifts to loops:
  if (!isa<ConstantSDNode>(N->getOperand(1)))
    return Op;

  uint64_t ShiftAmount = N->getConstantOperandVal(1);

  // Expand the stuff into sequence of shifts.
  SDValue Victim = N->getOperand(0);

  if (ShiftAmount >= 8) {
    assert(VT == MVT::i16 && "Can not shift i8 by 8 and more");
    switch(Opc) {
    default:
      llvm_unreachable("Unknown shift");
    case ISD::SHL:
      // foo << (8 + N) => swpb(zext(foo)) << N
      Victim = DAG.getZeroExtendInReg(Victim, dl, MVT::i8);
      Victim = DAG.getNode(ISD::BSWAP, dl, VT, Victim);
      break;
    case ISD::SRA:
    case ISD::SRL:
      // foo >> (8 + N) => sxt(swpb(foo)) >> N
      Victim = DAG.getNode(ISD::BSWAP, dl, VT, Victim);
      Victim = (Opc == ISD::SRA)
                   ? DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, VT, Victim,
                                 DAG.getValueType(MVT::i8))
                   : DAG.getZeroExtendInReg(Victim, dl, MVT::i8);
      break;
    }
    ShiftAmount -= 8;
  }

  if (Opc == ISD::SRL && ShiftAmount) {
    // Emit a special goodness here:
    // srl A, 1 => clrc; rrc A
    Victim = DAG.getNode(MSP430ISD::RRCL, dl, VT, Victim);
    ShiftAmount -= 1;
  }

  while (ShiftAmount--)
    Victim = DAG.getNode((Opc == ISD::SHL ? MSP430ISD::RLA : MSP430ISD::RRA),
                         dl, VT, Victim);

  return Victim;
}

SDValue MSP430TargetLowering::LowerGlobalAddress(SDValue Op,
                                                 SelectionDAG &DAG) const {
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  int64_t Offset = cast<GlobalAddressSDNode>(Op)->getOffset();
  EVT PtrVT = Op.getValueType();

  // Create the TargetGlobalAddress node, folding in the constant offset.
  SDValue Result = DAG.getTargetGlobalAddress(GV, SDLoc(Op), PtrVT, Offset);
  return DAG.getNode(MSP430ISD::Wrapper, SDLoc(Op), PtrVT, Result);
}

SDValue MSP430TargetLowering::LowerExternalSymbol(SDValue Op,
                                                  SelectionDAG &DAG) const {
  SDLoc dl(Op);
  const char *Sym = cast<ExternalSymbolSDNode>(Op)->getSymbol();
  EVT PtrVT = Op.getValueType();
  SDValue Result = DAG.getTargetExternalSymbol(Sym, PtrVT);

  return DAG.getNode(MSP430ISD::Wrapper, dl, PtrVT, Result);
}

SDValue MSP430TargetLowering::LowerBlockAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc dl(Op);
  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();
  EVT PtrVT = Op.getValueType();
  SDValue Result = DAG.getTargetBlockAddress(BA, PtrVT);

  return DAG.getNode(MSP430ISD::Wrapper, dl, PtrVT, Result);
}

static SDValue EmitCMP(SDValue &LHS, SDValue &RHS, SDValue &TargetCC,
                       ISD::CondCode CC, const SDLoc &dl, SelectionDAG &DAG) {
  // FIXME: Handle bittests someday
  assert(!LHS.getValueType().isFloatingPoint() && "We don't handle FP yet");

  // FIXME: Handle jump negative someday
  MSP430CC::CondCodes TCC = MSP430CC::COND_INVALID;
  switch (CC) {
  default: llvm_unreachable("Invalid integer condition!");
  case ISD::SETEQ:
    TCC = MSP430CC::COND_E;     // aka COND_Z
    // Minor optimization: if LHS is a constant, swap operands, then the
    // constant can be folded into comparison.
    if (LHS.getOpcode() == ISD::Constant)
      std::swap(LHS, RHS);
    break;
  case ISD::SETNE:
    TCC = MSP430CC::COND_NE;    // aka COND_NZ
    // Minor optimization: if LHS is a constant, swap operands, then the
    // constant can be folded into comparison.
    if (LHS.getOpcode() == ISD::Constant)
      std::swap(LHS, RHS);
    break;
  case ISD::SETULE:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ISD::SETUGE:
    // Turn lhs u>= rhs with lhs constant into rhs u< lhs+1, this allows us to
    // fold constant into instruction.
    if (const ConstantSDNode * C = dyn_cast<ConstantSDNode>(LHS)) {
      LHS = RHS;
      RHS = DAG.getConstant(C->getSExtValue() + 1, dl, C->getValueType(0));
      TCC = MSP430CC::COND_LO;
      break;
    }
    TCC = MSP430CC::COND_HS;    // aka COND_C
    break;
  case ISD::SETUGT:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ISD::SETULT:
    // Turn lhs u< rhs with lhs constant into rhs u>= lhs+1, this allows us to
    // fold constant into instruction.
    if (const ConstantSDNode * C = dyn_cast<ConstantSDNode>(LHS)) {
      LHS = RHS;
      RHS = DAG.getConstant(C->getSExtValue() + 1, dl, C->getValueType(0));
      TCC = MSP430CC::COND_HS;
      break;
    }
    TCC = MSP430CC::COND_LO;    // aka COND_NC
    break;
  case ISD::SETLE:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ISD::SETGE:
    // Turn lhs >= rhs with lhs constant into rhs < lhs+1, this allows us to
    // fold constant into instruction.
    if (const ConstantSDNode * C = dyn_cast<ConstantSDNode>(LHS)) {
      LHS = RHS;
      RHS = DAG.getConstant(C->getSExtValue() + 1, dl, C->getValueType(0));
      TCC = MSP430CC::COND_L;
      break;
    }
    TCC = MSP430CC::COND_GE;
    break;
  case ISD::SETGT:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ISD::SETLT:
    // Turn lhs < rhs with lhs constant into rhs >= lhs+1, this allows us to
    // fold constant into instruction.
    if (const ConstantSDNode * C = dyn_cast<ConstantSDNode>(LHS)) {
      LHS = RHS;
      RHS = DAG.getConstant(C->getSExtValue() + 1, dl, C->getValueType(0));
      TCC = MSP430CC::COND_GE;
      break;
    }
    TCC = MSP430CC::COND_L;
    break;
  }

  TargetCC = DAG.getConstant(TCC, dl, MVT::i8);
  return DAG.getNode(MSP430ISD::CMP, dl, MVT::Glue, LHS, RHS);
}


SDValue MSP430TargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS   = Op.getOperand(2);
  SDValue RHS   = Op.getOperand(3);
  SDValue Dest  = Op.getOperand(4);
  SDLoc dl  (Op);

  SDValue TargetCC;
  SDValue Flag = EmitCMP(LHS, RHS, TargetCC, CC, dl, DAG);

  return DAG.getNode(MSP430ISD::BR_CC, dl, Op.getValueType(),
                     Chain, Dest, TargetCC, Flag);
}

SDValue MSP430TargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  SDValue LHS   = Op.getOperand(0);
  SDValue RHS   = Op.getOperand(1);
  SDLoc dl  (Op);

  // If we are doing an AND and testing against zero, then the CMP
  // will not be generated.  The AND (or BIT) will generate the condition codes,
  // but they are different from CMP.
  // FIXME: since we're doing a post-processing, use a pseudoinstr here, so
  // lowering & isel wouldn't diverge.
  bool andCC = isNullConstant(RHS) && LHS.hasOneUse() &&
               (LHS.getOpcode() == ISD::AND ||
                (LHS.getOpcode() == ISD::TRUNCATE &&
                 LHS.getOperand(0).getOpcode() == ISD::AND));
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDValue TargetCC;
  SDValue Flag = EmitCMP(LHS, RHS, TargetCC, CC, dl, DAG);

  // Get the condition codes directly from the status register, if its easy.
  // Otherwise a branch will be generated.  Note that the AND and BIT
  // instructions generate different flags than CMP, the carry bit can be used
  // for NE/EQ.
  bool Invert = false;
  bool Shift = false;
  bool Convert = true;
  switch (TargetCC->getAsZExtVal()) {
  default:
    Convert = false;
    break;
   case MSP430CC::COND_HS:
     // Res = SR & 1, no processing is required
     break;
   case MSP430CC::COND_LO:
     // Res = ~(SR & 1)
     Invert = true;
     break;
   case MSP430CC::COND_NE:
     if (andCC) {
       // C = ~Z, thus Res = SR & 1, no processing is required
     } else {
       // Res = ~((SR >> 1) & 1)
       Shift = true;
       Invert = true;
     }
     break;
   case MSP430CC::COND_E:
     Shift = true;
     // C = ~Z for AND instruction, thus we can put Res = ~(SR & 1), however,
     // Res = (SR >> 1) & 1 is 1 word shorter.
     break;
   }
  EVT VT = Op.getValueType();
  SDValue One  = DAG.getConstant(1, dl, VT);
  if (Convert) {
    SDValue SR = DAG.getCopyFromReg(DAG.getEntryNode(), dl, MSP430::SR,
                                    MVT::i16, Flag);
    if (Shift)
      // FIXME: somewhere this is turned into a SRL, lower it MSP specific?
      SR = DAG.getNode(ISD::SRA, dl, MVT::i16, SR, One);
    SR = DAG.getNode(ISD::AND, dl, MVT::i16, SR, One);
    if (Invert)
      SR = DAG.getNode(ISD::XOR, dl, MVT::i16, SR, One);
    return SR;
  } else {
    SDValue Zero = DAG.getConstant(0, dl, VT);
    SDValue Ops[] = {One, Zero, TargetCC, Flag};
    return DAG.getNode(MSP430ISD::SELECT_CC, dl, Op.getValueType(), Ops);
  }
}

SDValue MSP430TargetLowering::LowerSELECT_CC(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDValue LHS    = Op.getOperand(0);
  SDValue RHS    = Op.getOperand(1);
  SDValue TrueV  = Op.getOperand(2);
  SDValue FalseV = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc dl   (Op);

  SDValue TargetCC;
  SDValue Flag = EmitCMP(LHS, RHS, TargetCC, CC, dl, DAG);

  SDValue Ops[] = {TrueV, FalseV, TargetCC, Flag};

  return DAG.getNode(MSP430ISD::SELECT_CC, dl, Op.getValueType(), Ops);
}

SDValue MSP430TargetLowering::LowerSIGN_EXTEND(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDValue Val = Op.getOperand(0);
  EVT VT      = Op.getValueType();
  SDLoc dl(Op);

  assert(VT == MVT::i16 && "Only support i16 for now!");

  return DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, VT,
                     DAG.getNode(ISD::ANY_EXTEND, dl, VT, Val),
                     DAG.getValueType(Val.getValueType()));
}

SDValue
MSP430TargetLowering::getReturnAddressFrameIndex(SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MSP430MachineFunctionInfo *FuncInfo = MF.getInfo<MSP430MachineFunctionInfo>();
  int ReturnAddrIndex = FuncInfo->getRAIndex();
  MVT PtrVT = getFrameIndexTy(MF.getDataLayout());

  if (ReturnAddrIndex == 0) {
    // Set up a frame object for the return address.
    uint64_t SlotSize = PtrVT.getStoreSize();
    ReturnAddrIndex = MF.getFrameInfo().CreateFixedObject(SlotSize, -SlotSize,
                                                           true);
    FuncInfo->setRAIndex(ReturnAddrIndex);
  }

  return DAG.getFrameIndex(ReturnAddrIndex, PtrVT);
}

SDValue MSP430TargetLowering::LowerRETURNADDR(SDValue Op,
                                              SelectionDAG &DAG) const {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  unsigned Depth = Op.getConstantOperandVal(0);
  SDLoc dl(Op);
  EVT PtrVT = Op.getValueType();

  if (Depth > 0) {
    SDValue FrameAddr = LowerFRAMEADDR(Op, DAG);
    SDValue Offset =
      DAG.getConstant(PtrVT.getStoreSize(), dl, MVT::i16);
    return DAG.getLoad(PtrVT, dl, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, dl, PtrVT, FrameAddr, Offset),
                       MachinePointerInfo());
  }

  // Just load the return address.
  SDValue RetAddrFI = getReturnAddressFrameIndex(DAG);
  return DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), RetAddrFI,
                     MachinePointerInfo());
}

SDValue MSP430TargetLowering::LowerFRAMEADDR(SDValue Op,
                                             SelectionDAG &DAG) const {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc dl(Op);  // FIXME probably not meaningful
  unsigned Depth = Op.getConstantOperandVal(0);
  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), dl,
                                         MSP430::R4, VT);
  while (Depth--)
    FrameAddr = DAG.getLoad(VT, dl, DAG.getEntryNode(), FrameAddr,
                            MachinePointerInfo());
  return FrameAddr;
}

SDValue MSP430TargetLowering::LowerVASTART(SDValue Op,
                                           SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MSP430MachineFunctionInfo *FuncInfo = MF.getInfo<MSP430MachineFunctionInfo>();

  SDValue Ptr = Op.getOperand(1);
  EVT PtrVT = Ptr.getValueType();

  // Frame index of first vararg argument
  SDValue FrameIndex =
      DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();

  // Create a store of the frame index to the location operand
  return DAG.getStore(Op.getOperand(0), SDLoc(Op), FrameIndex, Ptr,
                      MachinePointerInfo(SV));
}

SDValue MSP430TargetLowering::LowerJumpTable(SDValue Op,
                                             SelectionDAG &DAG) const {
    JumpTableSDNode *JT = cast<JumpTableSDNode>(Op);
    EVT PtrVT = Op.getValueType();
    SDValue Result = DAG.getTargetJumpTable(JT->getIndex(), PtrVT);
    return DAG.getNode(MSP430ISD::Wrapper, SDLoc(JT), PtrVT, Result);
}

/// getPostIndexedAddressParts - returns true by value, base pointer and
/// offset pointer and addressing mode by reference if this node can be
/// combined with a load / store to form a post-indexed load / store.
bool MSP430TargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                      SDValue &Base,
                                                      SDValue &Offset,
                                                      ISD::MemIndexedMode &AM,
                                                      SelectionDAG &DAG) const {

  LoadSDNode *LD = cast<LoadSDNode>(N);
  if (LD->getExtensionType() != ISD::NON_EXTLOAD)
    return false;

  EVT VT = LD->getMemoryVT();
  if (VT != MVT::i8 && VT != MVT::i16)
    return false;

  if (Op->getOpcode() != ISD::ADD)
    return false;

  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Op->getOperand(1))) {
    uint64_t RHSC = RHS->getZExtValue();
    if ((VT == MVT::i16 && RHSC != 2) ||
        (VT == MVT::i8 && RHSC != 1))
      return false;

    Base = Op->getOperand(0);
    Offset = DAG.getConstant(RHSC, SDLoc(N), VT);
    AM = ISD::POST_INC;
    return true;
  }

  return false;
}


const char *MSP430TargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((MSP430ISD::NodeType)Opcode) {
  case MSP430ISD::FIRST_NUMBER:       break;
  case MSP430ISD::RET_GLUE:           return "MSP430ISD::RET_GLUE";
  case MSP430ISD::RETI_GLUE:          return "MSP430ISD::RETI_GLUE";
  case MSP430ISD::RRA:                return "MSP430ISD::RRA";
  case MSP430ISD::RLA:                return "MSP430ISD::RLA";
  case MSP430ISD::RRC:                return "MSP430ISD::RRC";
  case MSP430ISD::RRCL:               return "MSP430ISD::RRCL";
  case MSP430ISD::CALL:               return "MSP430ISD::CALL";
  case MSP430ISD::Wrapper:            return "MSP430ISD::Wrapper";
  case MSP430ISD::BR_CC:              return "MSP430ISD::BR_CC";
  case MSP430ISD::CMP:                return "MSP430ISD::CMP";
  case MSP430ISD::SETCC:              return "MSP430ISD::SETCC";
  case MSP430ISD::SELECT_CC:          return "MSP430ISD::SELECT_CC";
  case MSP430ISD::DADD:               return "MSP430ISD::DADD";
  }
  return nullptr;
}

bool MSP430TargetLowering::isTruncateFree(Type *Ty1,
                                          Type *Ty2) const {
  if (!Ty1->isIntegerTy() || !Ty2->isIntegerTy())
    return false;

  return (Ty1->getPrimitiveSizeInBits().getFixedValue() >
          Ty2->getPrimitiveSizeInBits().getFixedValue());
}

bool MSP430TargetLowering::isTruncateFree(EVT VT1, EVT VT2) const {
  if (!VT1.isInteger() || !VT2.isInteger())
    return false;

  return (VT1.getFixedSizeInBits() > VT2.getFixedSizeInBits());
}

bool MSP430TargetLowering::isZExtFree(Type *Ty1, Type *Ty2) const {
  // MSP430 implicitly zero-extends 8-bit results in 16-bit registers.
  return false && Ty1->isIntegerTy(8) && Ty2->isIntegerTy(16);
}

bool MSP430TargetLowering::isZExtFree(EVT VT1, EVT VT2) const {
  // MSP430 implicitly zero-extends 8-bit results in 16-bit registers.
  return false && VT1 == MVT::i8 && VT2 == MVT::i16;
}

//===----------------------------------------------------------------------===//
//  Other Lowering Code
//===----------------------------------------------------------------------===//

MachineBasicBlock *
MSP430TargetLowering::EmitShiftInstr(MachineInstr &MI,
                                     MachineBasicBlock *BB) const {
  MachineFunction *F = BB->getParent();
  MachineRegisterInfo &RI = F->getRegInfo();
  DebugLoc dl = MI.getDebugLoc();
  const TargetInstrInfo &TII = *F->getSubtarget().getInstrInfo();

  unsigned Opc;
  bool ClearCarry = false;
  const TargetRegisterClass * RC;
  switch (MI.getOpcode()) {
  default: llvm_unreachable("Invalid shift opcode!");
  case MSP430::Shl8:
    Opc = MSP430::ADD8rr;
    RC = &MSP430::GR8RegClass;
    break;
  case MSP430::Shl16:
    Opc = MSP430::ADD16rr;
    RC = &MSP430::GR16RegClass;
    break;
  case MSP430::Sra8:
    Opc = MSP430::RRA8r;
    RC = &MSP430::GR8RegClass;
    break;
  case MSP430::Sra16:
    Opc = MSP430::RRA16r;
    RC = &MSP430::GR16RegClass;
    break;
  case MSP430::Srl8:
    ClearCarry = true;
    Opc = MSP430::RRC8r;
    RC = &MSP430::GR8RegClass;
    break;
  case MSP430::Srl16:
    ClearCarry = true;
    Opc = MSP430::RRC16r;
    RC = &MSP430::GR16RegClass;
    break;
  case MSP430::Rrcl8:
  case MSP430::Rrcl16: {
    BuildMI(*BB, MI, dl, TII.get(MSP430::BIC16rc), MSP430::SR)
      .addReg(MSP430::SR).addImm(1);
    Register SrcReg = MI.getOperand(1).getReg();
    Register DstReg = MI.getOperand(0).getReg();
    unsigned RrcOpc = MI.getOpcode() == MSP430::Rrcl16
                    ? MSP430::RRC16r : MSP430::RRC8r;
    BuildMI(*BB, MI, dl, TII.get(RrcOpc), DstReg)
      .addReg(SrcReg);
    MI.eraseFromParent(); // The pseudo instruction is gone now.
    return BB;
  }
  }

  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator I = ++BB->getIterator();

  // Create loop block
  MachineBasicBlock *LoopBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *RemBB  = F->CreateMachineBasicBlock(LLVM_BB);

  F->insert(I, LoopBB);
  F->insert(I, RemBB);

  // Update machine-CFG edges by transferring all successors of the current
  // block to the block containing instructions after shift.
  RemBB->splice(RemBB->begin(), BB, std::next(MachineBasicBlock::iterator(MI)),
                BB->end());
  RemBB->transferSuccessorsAndUpdatePHIs(BB);

  // Add edges BB => LoopBB => RemBB, BB => RemBB, LoopBB => LoopBB
  BB->addSuccessor(LoopBB);
  BB->addSuccessor(RemBB);
  LoopBB->addSuccessor(RemBB);
  LoopBB->addSuccessor(LoopBB);

  Register ShiftAmtReg = RI.createVirtualRegister(&MSP430::GR8RegClass);
  Register ShiftAmtReg2 = RI.createVirtualRegister(&MSP430::GR8RegClass);
  Register ShiftReg = RI.createVirtualRegister(RC);
  Register ShiftReg2 = RI.createVirtualRegister(RC);
  Register ShiftAmtSrcReg = MI.getOperand(2).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  Register DstReg = MI.getOperand(0).getReg();

  // BB:
  // cmp 0, N
  // je RemBB
  BuildMI(BB, dl, TII.get(MSP430::CMP8ri))
    .addReg(ShiftAmtSrcReg).addImm(0);
  BuildMI(BB, dl, TII.get(MSP430::JCC))
    .addMBB(RemBB)
    .addImm(MSP430CC::COND_E);

  // LoopBB:
  // ShiftReg = phi [%SrcReg, BB], [%ShiftReg2, LoopBB]
  // ShiftAmt = phi [%N, BB],      [%ShiftAmt2, LoopBB]
  // ShiftReg2 = shift ShiftReg
  // ShiftAmt2 = ShiftAmt - 1;
  BuildMI(LoopBB, dl, TII.get(MSP430::PHI), ShiftReg)
    .addReg(SrcReg).addMBB(BB)
    .addReg(ShiftReg2).addMBB(LoopBB);
  BuildMI(LoopBB, dl, TII.get(MSP430::PHI), ShiftAmtReg)
    .addReg(ShiftAmtSrcReg).addMBB(BB)
    .addReg(ShiftAmtReg2).addMBB(LoopBB);
  if (ClearCarry)
    BuildMI(LoopBB, dl, TII.get(MSP430::BIC16rc), MSP430::SR)
      .addReg(MSP430::SR).addImm(1);
  if (Opc == MSP430::ADD8rr || Opc == MSP430::ADD16rr)
    BuildMI(LoopBB, dl, TII.get(Opc), ShiftReg2)
      .addReg(ShiftReg)
      .addReg(ShiftReg);
  else
    BuildMI(LoopBB, dl, TII.get(Opc), ShiftReg2)
      .addReg(ShiftReg);
  BuildMI(LoopBB, dl, TII.get(MSP430::SUB8ri), ShiftAmtReg2)
    .addReg(ShiftAmtReg).addImm(1);
  BuildMI(LoopBB, dl, TII.get(MSP430::JCC))
    .addMBB(LoopBB)
    .addImm(MSP430CC::COND_NE);

  // RemBB:
  // DestReg = phi [%SrcReg, BB], [%ShiftReg, LoopBB]
  BuildMI(*RemBB, RemBB->begin(), dl, TII.get(MSP430::PHI), DstReg)
    .addReg(SrcReg).addMBB(BB)
    .addReg(ShiftReg2).addMBB(LoopBB);

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return RemBB;
}

MachineBasicBlock *
MSP430TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                  MachineBasicBlock *BB) const {
  unsigned Opc = MI.getOpcode();

  if (Opc == MSP430::Shl8  || Opc == MSP430::Shl16 ||
      Opc == MSP430::Sra8  || Opc == MSP430::Sra16 ||
      Opc == MSP430::Srl8  || Opc == MSP430::Srl16 ||
      Opc == MSP430::Rrcl8 || Opc == MSP430::Rrcl16)
    return EmitShiftInstr(MI, BB);

  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  assert((Opc == MSP430::Select16 || Opc == MSP430::Select8) &&
         "Unexpected instr type to insert");

  // To "insert" a SELECT instruction, we actually have to insert the diamond
  // control-flow pattern.  The incoming instruction knows the destination vreg
  // to set, the condition code register to branch on, the true/false values to
  // select between, and a branch opcode to use.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator I = ++BB->getIterator();

  //  thisMBB:
  //  ...
  //   TrueVal = ...
  //   cmpTY ccX, r1, r2
  //   jCC copy1MBB
  //   fallthrough --> copy0MBB
  MachineBasicBlock *thisMBB = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *copy1MBB = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(I, copy0MBB);
  F->insert(I, copy1MBB);
  // Update machine-CFG edges by transferring all successors of the current
  // block to the new block which will contain the Phi node for the select.
  copy1MBB->splice(copy1MBB->begin(), BB,
                   std::next(MachineBasicBlock::iterator(MI)), BB->end());
  copy1MBB->transferSuccessorsAndUpdatePHIs(BB);
  // Next, add the true and fallthrough blocks as its successors.
  BB->addSuccessor(copy0MBB);
  BB->addSuccessor(copy1MBB);

  BuildMI(BB, dl, TII.get(MSP430::JCC))
      .addMBB(copy1MBB)
      .addImm(MI.getOperand(3).getImm());

  //  copy0MBB:
  //   %FalseValue = ...
  //   # fallthrough to copy1MBB
  BB = copy0MBB;

  // Update machine-CFG edges
  BB->addSuccessor(copy1MBB);

  //  copy1MBB:
  //   %Result = phi [ %FalseValue, copy0MBB ], [ %TrueValue, thisMBB ]
  //  ...
  BB = copy1MBB;
  BuildMI(*BB, BB->begin(), dl, TII.get(MSP430::PHI), MI.getOperand(0).getReg())
      .addReg(MI.getOperand(2).getReg())
      .addMBB(copy0MBB)
      .addReg(MI.getOperand(1).getReg())
      .addMBB(thisMBB);

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}
