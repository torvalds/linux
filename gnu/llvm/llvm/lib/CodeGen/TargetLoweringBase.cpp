//===- TargetLoweringBase.cpp - Implement the TargetLoweringBase class ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the TargetLoweringBase class.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/SizeOpts.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

static cl::opt<bool> JumpIsExpensiveOverride(
    "jump-is-expensive", cl::init(false),
    cl::desc("Do not create extra branches to split comparison logic."),
    cl::Hidden);

static cl::opt<unsigned> MinimumJumpTableEntries
  ("min-jump-table-entries", cl::init(4), cl::Hidden,
   cl::desc("Set minimum number of entries to use a jump table."));

static cl::opt<unsigned> MaximumJumpTableSize
  ("max-jump-table-size", cl::init(UINT_MAX), cl::Hidden,
   cl::desc("Set maximum size of jump tables."));

/// Minimum jump table density for normal functions.
static cl::opt<unsigned>
    JumpTableDensity("jump-table-density", cl::init(10), cl::Hidden,
                     cl::desc("Minimum density for building a jump table in "
                              "a normal function"));

/// Minimum jump table density for -Os or -Oz functions.
static cl::opt<unsigned> OptsizeJumpTableDensity(
    "optsize-jump-table-density", cl::init(40), cl::Hidden,
    cl::desc("Minimum density for building a jump table in "
             "an optsize function"));

// FIXME: This option is only to test if the strict fp operation processed
// correctly by preventing mutating strict fp operation to normal fp operation
// during development. When the backend supports strict float operation, this
// option will be meaningless.
static cl::opt<bool> DisableStrictNodeMutation("disable-strictnode-mutation",
       cl::desc("Don't mutate strict-float node to a legalize node"),
       cl::init(false), cl::Hidden);

/// GetFPLibCall - Helper to return the right libcall for the given floating
/// point type, or UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getFPLibCall(EVT VT,
                                   RTLIB::Libcall Call_F32,
                                   RTLIB::Libcall Call_F64,
                                   RTLIB::Libcall Call_F80,
                                   RTLIB::Libcall Call_F128,
                                   RTLIB::Libcall Call_PPCF128) {
  return
    VT == MVT::f32 ? Call_F32 :
    VT == MVT::f64 ? Call_F64 :
    VT == MVT::f80 ? Call_F80 :
    VT == MVT::f128 ? Call_F128 :
    VT == MVT::ppcf128 ? Call_PPCF128 :
    RTLIB::UNKNOWN_LIBCALL;
}

/// getFPEXT - Return the FPEXT_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getFPEXT(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::f16) {
    if (RetVT == MVT::f32)
      return FPEXT_F16_F32;
    if (RetVT == MVT::f64)
      return FPEXT_F16_F64;
    if (RetVT == MVT::f80)
      return FPEXT_F16_F80;
    if (RetVT == MVT::f128)
      return FPEXT_F16_F128;
  } else if (OpVT == MVT::f32) {
    if (RetVT == MVT::f64)
      return FPEXT_F32_F64;
    if (RetVT == MVT::f128)
      return FPEXT_F32_F128;
    if (RetVT == MVT::ppcf128)
      return FPEXT_F32_PPCF128;
  } else if (OpVT == MVT::f64) {
    if (RetVT == MVT::f128)
      return FPEXT_F64_F128;
    else if (RetVT == MVT::ppcf128)
      return FPEXT_F64_PPCF128;
  } else if (OpVT == MVT::f80) {
    if (RetVT == MVT::f128)
      return FPEXT_F80_F128;
  } else if (OpVT == MVT::bf16) {
    if (RetVT == MVT::f32)
      return FPEXT_BF16_F32;
  }

  return UNKNOWN_LIBCALL;
}

/// getFPROUND - Return the FPROUND_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getFPROUND(EVT OpVT, EVT RetVT) {
  if (RetVT == MVT::f16) {
    if (OpVT == MVT::f32)
      return FPROUND_F32_F16;
    if (OpVT == MVT::f64)
      return FPROUND_F64_F16;
    if (OpVT == MVT::f80)
      return FPROUND_F80_F16;
    if (OpVT == MVT::f128)
      return FPROUND_F128_F16;
    if (OpVT == MVT::ppcf128)
      return FPROUND_PPCF128_F16;
  } else if (RetVT == MVT::bf16) {
    if (OpVT == MVT::f32)
      return FPROUND_F32_BF16;
    if (OpVT == MVT::f64)
      return FPROUND_F64_BF16;
  } else if (RetVT == MVT::f32) {
    if (OpVT == MVT::f64)
      return FPROUND_F64_F32;
    if (OpVT == MVT::f80)
      return FPROUND_F80_F32;
    if (OpVT == MVT::f128)
      return FPROUND_F128_F32;
    if (OpVT == MVT::ppcf128)
      return FPROUND_PPCF128_F32;
  } else if (RetVT == MVT::f64) {
    if (OpVT == MVT::f80)
      return FPROUND_F80_F64;
    if (OpVT == MVT::f128)
      return FPROUND_F128_F64;
    if (OpVT == MVT::ppcf128)
      return FPROUND_PPCF128_F64;
  } else if (RetVT == MVT::f80) {
    if (OpVT == MVT::f128)
      return FPROUND_F128_F80;
  }

  return UNKNOWN_LIBCALL;
}

/// getFPTOSINT - Return the FPTOSINT_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getFPTOSINT(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::f16) {
    if (RetVT == MVT::i32)
      return FPTOSINT_F16_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_F16_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_F16_I128;
  } else if (OpVT == MVT::f32) {
    if (RetVT == MVT::i32)
      return FPTOSINT_F32_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_F32_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_F32_I128;
  } else if (OpVT == MVT::f64) {
    if (RetVT == MVT::i32)
      return FPTOSINT_F64_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_F64_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_F64_I128;
  } else if (OpVT == MVT::f80) {
    if (RetVT == MVT::i32)
      return FPTOSINT_F80_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_F80_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_F80_I128;
  } else if (OpVT == MVT::f128) {
    if (RetVT == MVT::i32)
      return FPTOSINT_F128_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_F128_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_F128_I128;
  } else if (OpVT == MVT::ppcf128) {
    if (RetVT == MVT::i32)
      return FPTOSINT_PPCF128_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_PPCF128_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_PPCF128_I128;
  }
  return UNKNOWN_LIBCALL;
}

/// getFPTOUINT - Return the FPTOUINT_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getFPTOUINT(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::f16) {
    if (RetVT == MVT::i32)
      return FPTOUINT_F16_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_F16_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_F16_I128;
  } else if (OpVT == MVT::f32) {
    if (RetVT == MVT::i32)
      return FPTOUINT_F32_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_F32_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_F32_I128;
  } else if (OpVT == MVT::f64) {
    if (RetVT == MVT::i32)
      return FPTOUINT_F64_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_F64_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_F64_I128;
  } else if (OpVT == MVT::f80) {
    if (RetVT == MVT::i32)
      return FPTOUINT_F80_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_F80_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_F80_I128;
  } else if (OpVT == MVT::f128) {
    if (RetVT == MVT::i32)
      return FPTOUINT_F128_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_F128_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_F128_I128;
  } else if (OpVT == MVT::ppcf128) {
    if (RetVT == MVT::i32)
      return FPTOUINT_PPCF128_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_PPCF128_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_PPCF128_I128;
  }
  return UNKNOWN_LIBCALL;
}

/// getSINTTOFP - Return the SINTTOFP_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getSINTTOFP(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::i32) {
    if (RetVT == MVT::f16)
      return SINTTOFP_I32_F16;
    if (RetVT == MVT::f32)
      return SINTTOFP_I32_F32;
    if (RetVT == MVT::f64)
      return SINTTOFP_I32_F64;
    if (RetVT == MVT::f80)
      return SINTTOFP_I32_F80;
    if (RetVT == MVT::f128)
      return SINTTOFP_I32_F128;
    if (RetVT == MVT::ppcf128)
      return SINTTOFP_I32_PPCF128;
  } else if (OpVT == MVT::i64) {
    if (RetVT == MVT::f16)
      return SINTTOFP_I64_F16;
    if (RetVT == MVT::f32)
      return SINTTOFP_I64_F32;
    if (RetVT == MVT::f64)
      return SINTTOFP_I64_F64;
    if (RetVT == MVT::f80)
      return SINTTOFP_I64_F80;
    if (RetVT == MVT::f128)
      return SINTTOFP_I64_F128;
    if (RetVT == MVT::ppcf128)
      return SINTTOFP_I64_PPCF128;
  } else if (OpVT == MVT::i128) {
    if (RetVT == MVT::f16)
      return SINTTOFP_I128_F16;
    if (RetVT == MVT::f32)
      return SINTTOFP_I128_F32;
    if (RetVT == MVT::f64)
      return SINTTOFP_I128_F64;
    if (RetVT == MVT::f80)
      return SINTTOFP_I128_F80;
    if (RetVT == MVT::f128)
      return SINTTOFP_I128_F128;
    if (RetVT == MVT::ppcf128)
      return SINTTOFP_I128_PPCF128;
  }
  return UNKNOWN_LIBCALL;
}

/// getUINTTOFP - Return the UINTTOFP_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getUINTTOFP(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::i32) {
    if (RetVT == MVT::f16)
      return UINTTOFP_I32_F16;
    if (RetVT == MVT::f32)
      return UINTTOFP_I32_F32;
    if (RetVT == MVT::f64)
      return UINTTOFP_I32_F64;
    if (RetVT == MVT::f80)
      return UINTTOFP_I32_F80;
    if (RetVT == MVT::f128)
      return UINTTOFP_I32_F128;
    if (RetVT == MVT::ppcf128)
      return UINTTOFP_I32_PPCF128;
  } else if (OpVT == MVT::i64) {
    if (RetVT == MVT::f16)
      return UINTTOFP_I64_F16;
    if (RetVT == MVT::f32)
      return UINTTOFP_I64_F32;
    if (RetVT == MVT::f64)
      return UINTTOFP_I64_F64;
    if (RetVT == MVT::f80)
      return UINTTOFP_I64_F80;
    if (RetVT == MVT::f128)
      return UINTTOFP_I64_F128;
    if (RetVT == MVT::ppcf128)
      return UINTTOFP_I64_PPCF128;
  } else if (OpVT == MVT::i128) {
    if (RetVT == MVT::f16)
      return UINTTOFP_I128_F16;
    if (RetVT == MVT::f32)
      return UINTTOFP_I128_F32;
    if (RetVT == MVT::f64)
      return UINTTOFP_I128_F64;
    if (RetVT == MVT::f80)
      return UINTTOFP_I128_F80;
    if (RetVT == MVT::f128)
      return UINTTOFP_I128_F128;
    if (RetVT == MVT::ppcf128)
      return UINTTOFP_I128_PPCF128;
  }
  return UNKNOWN_LIBCALL;
}

RTLIB::Libcall RTLIB::getPOWI(EVT RetVT) {
  return getFPLibCall(RetVT, POWI_F32, POWI_F64, POWI_F80, POWI_F128,
                      POWI_PPCF128);
}

RTLIB::Libcall RTLIB::getLDEXP(EVT RetVT) {
  return getFPLibCall(RetVT, LDEXP_F32, LDEXP_F64, LDEXP_F80, LDEXP_F128,
                      LDEXP_PPCF128);
}

RTLIB::Libcall RTLIB::getFREXP(EVT RetVT) {
  return getFPLibCall(RetVT, FREXP_F32, FREXP_F64, FREXP_F80, FREXP_F128,
                      FREXP_PPCF128);
}

RTLIB::Libcall RTLIB::getOutlineAtomicHelper(const Libcall (&LC)[5][4],
                                             AtomicOrdering Order,
                                             uint64_t MemSize) {
  unsigned ModeN, ModelN;
  switch (MemSize) {
  case 1:
    ModeN = 0;
    break;
  case 2:
    ModeN = 1;
    break;
  case 4:
    ModeN = 2;
    break;
  case 8:
    ModeN = 3;
    break;
  case 16:
    ModeN = 4;
    break;
  default:
    return RTLIB::UNKNOWN_LIBCALL;
  }

  switch (Order) {
  case AtomicOrdering::Monotonic:
    ModelN = 0;
    break;
  case AtomicOrdering::Acquire:
    ModelN = 1;
    break;
  case AtomicOrdering::Release:
    ModelN = 2;
    break;
  case AtomicOrdering::AcquireRelease:
  case AtomicOrdering::SequentiallyConsistent:
    ModelN = 3;
    break;
  default:
    return UNKNOWN_LIBCALL;
  }

  return LC[ModeN][ModelN];
}

RTLIB::Libcall RTLIB::getOUTLINE_ATOMIC(unsigned Opc, AtomicOrdering Order,
                                        MVT VT) {
  if (!VT.isScalarInteger())
    return UNKNOWN_LIBCALL;
  uint64_t MemSize = VT.getScalarSizeInBits() / 8;

#define LCALLS(A, B)                                                           \
  { A##B##_RELAX, A##B##_ACQ, A##B##_REL, A##B##_ACQ_REL }
#define LCALL5(A)                                                              \
  LCALLS(A, 1), LCALLS(A, 2), LCALLS(A, 4), LCALLS(A, 8), LCALLS(A, 16)
  switch (Opc) {
  case ISD::ATOMIC_CMP_SWAP: {
    const Libcall LC[5][4] = {LCALL5(OUTLINE_ATOMIC_CAS)};
    return getOutlineAtomicHelper(LC, Order, MemSize);
  }
  case ISD::ATOMIC_SWAP: {
    const Libcall LC[5][4] = {LCALL5(OUTLINE_ATOMIC_SWP)};
    return getOutlineAtomicHelper(LC, Order, MemSize);
  }
  case ISD::ATOMIC_LOAD_ADD: {
    const Libcall LC[5][4] = {LCALL5(OUTLINE_ATOMIC_LDADD)};
    return getOutlineAtomicHelper(LC, Order, MemSize);
  }
  case ISD::ATOMIC_LOAD_OR: {
    const Libcall LC[5][4] = {LCALL5(OUTLINE_ATOMIC_LDSET)};
    return getOutlineAtomicHelper(LC, Order, MemSize);
  }
  case ISD::ATOMIC_LOAD_CLR: {
    const Libcall LC[5][4] = {LCALL5(OUTLINE_ATOMIC_LDCLR)};
    return getOutlineAtomicHelper(LC, Order, MemSize);
  }
  case ISD::ATOMIC_LOAD_XOR: {
    const Libcall LC[5][4] = {LCALL5(OUTLINE_ATOMIC_LDEOR)};
    return getOutlineAtomicHelper(LC, Order, MemSize);
  }
  default:
    return UNKNOWN_LIBCALL;
  }
#undef LCALLS
#undef LCALL5
}

RTLIB::Libcall RTLIB::getSYNC(unsigned Opc, MVT VT) {
#define OP_TO_LIBCALL(Name, Enum)                                              \
  case Name:                                                                   \
    switch (VT.SimpleTy) {                                                     \
    default:                                                                   \
      return UNKNOWN_LIBCALL;                                                  \
    case MVT::i8:                                                              \
      return Enum##_1;                                                         \
    case MVT::i16:                                                             \
      return Enum##_2;                                                         \
    case MVT::i32:                                                             \
      return Enum##_4;                                                         \
    case MVT::i64:                                                             \
      return Enum##_8;                                                         \
    case MVT::i128:                                                            \
      return Enum##_16;                                                        \
    }

  switch (Opc) {
    OP_TO_LIBCALL(ISD::ATOMIC_SWAP, SYNC_LOCK_TEST_AND_SET)
    OP_TO_LIBCALL(ISD::ATOMIC_CMP_SWAP, SYNC_VAL_COMPARE_AND_SWAP)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_ADD, SYNC_FETCH_AND_ADD)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_SUB, SYNC_FETCH_AND_SUB)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_AND, SYNC_FETCH_AND_AND)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_OR, SYNC_FETCH_AND_OR)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_XOR, SYNC_FETCH_AND_XOR)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_NAND, SYNC_FETCH_AND_NAND)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_MAX, SYNC_FETCH_AND_MAX)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_UMAX, SYNC_FETCH_AND_UMAX)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_MIN, SYNC_FETCH_AND_MIN)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_UMIN, SYNC_FETCH_AND_UMIN)
  }

#undef OP_TO_LIBCALL

  return UNKNOWN_LIBCALL;
}

RTLIB::Libcall RTLIB::getMEMCPY_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize) {
  switch (ElementSize) {
  case 1:
    return MEMCPY_ELEMENT_UNORDERED_ATOMIC_1;
  case 2:
    return MEMCPY_ELEMENT_UNORDERED_ATOMIC_2;
  case 4:
    return MEMCPY_ELEMENT_UNORDERED_ATOMIC_4;
  case 8:
    return MEMCPY_ELEMENT_UNORDERED_ATOMIC_8;
  case 16:
    return MEMCPY_ELEMENT_UNORDERED_ATOMIC_16;
  default:
    return UNKNOWN_LIBCALL;
  }
}

RTLIB::Libcall RTLIB::getMEMMOVE_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize) {
  switch (ElementSize) {
  case 1:
    return MEMMOVE_ELEMENT_UNORDERED_ATOMIC_1;
  case 2:
    return MEMMOVE_ELEMENT_UNORDERED_ATOMIC_2;
  case 4:
    return MEMMOVE_ELEMENT_UNORDERED_ATOMIC_4;
  case 8:
    return MEMMOVE_ELEMENT_UNORDERED_ATOMIC_8;
  case 16:
    return MEMMOVE_ELEMENT_UNORDERED_ATOMIC_16;
  default:
    return UNKNOWN_LIBCALL;
  }
}

RTLIB::Libcall RTLIB::getMEMSET_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize) {
  switch (ElementSize) {
  case 1:
    return MEMSET_ELEMENT_UNORDERED_ATOMIC_1;
  case 2:
    return MEMSET_ELEMENT_UNORDERED_ATOMIC_2;
  case 4:
    return MEMSET_ELEMENT_UNORDERED_ATOMIC_4;
  case 8:
    return MEMSET_ELEMENT_UNORDERED_ATOMIC_8;
  case 16:
    return MEMSET_ELEMENT_UNORDERED_ATOMIC_16;
  default:
    return UNKNOWN_LIBCALL;
  }
}

void RTLIB::initCmpLibcallCCs(ISD::CondCode *CmpLibcallCCs) {
  std::fill(CmpLibcallCCs, CmpLibcallCCs + RTLIB::UNKNOWN_LIBCALL,
            ISD::SETCC_INVALID);
  CmpLibcallCCs[RTLIB::OEQ_F32] = ISD::SETEQ;
  CmpLibcallCCs[RTLIB::OEQ_F64] = ISD::SETEQ;
  CmpLibcallCCs[RTLIB::OEQ_F128] = ISD::SETEQ;
  CmpLibcallCCs[RTLIB::OEQ_PPCF128] = ISD::SETEQ;
  CmpLibcallCCs[RTLIB::UNE_F32] = ISD::SETNE;
  CmpLibcallCCs[RTLIB::UNE_F64] = ISD::SETNE;
  CmpLibcallCCs[RTLIB::UNE_F128] = ISD::SETNE;
  CmpLibcallCCs[RTLIB::UNE_PPCF128] = ISD::SETNE;
  CmpLibcallCCs[RTLIB::OGE_F32] = ISD::SETGE;
  CmpLibcallCCs[RTLIB::OGE_F64] = ISD::SETGE;
  CmpLibcallCCs[RTLIB::OGE_F128] = ISD::SETGE;
  CmpLibcallCCs[RTLIB::OGE_PPCF128] = ISD::SETGE;
  CmpLibcallCCs[RTLIB::OLT_F32] = ISD::SETLT;
  CmpLibcallCCs[RTLIB::OLT_F64] = ISD::SETLT;
  CmpLibcallCCs[RTLIB::OLT_F128] = ISD::SETLT;
  CmpLibcallCCs[RTLIB::OLT_PPCF128] = ISD::SETLT;
  CmpLibcallCCs[RTLIB::OLE_F32] = ISD::SETLE;
  CmpLibcallCCs[RTLIB::OLE_F64] = ISD::SETLE;
  CmpLibcallCCs[RTLIB::OLE_F128] = ISD::SETLE;
  CmpLibcallCCs[RTLIB::OLE_PPCF128] = ISD::SETLE;
  CmpLibcallCCs[RTLIB::OGT_F32] = ISD::SETGT;
  CmpLibcallCCs[RTLIB::OGT_F64] = ISD::SETGT;
  CmpLibcallCCs[RTLIB::OGT_F128] = ISD::SETGT;
  CmpLibcallCCs[RTLIB::OGT_PPCF128] = ISD::SETGT;
  CmpLibcallCCs[RTLIB::UO_F32] = ISD::SETNE;
  CmpLibcallCCs[RTLIB::UO_F64] = ISD::SETNE;
  CmpLibcallCCs[RTLIB::UO_F128] = ISD::SETNE;
  CmpLibcallCCs[RTLIB::UO_PPCF128] = ISD::SETNE;
}

/// NOTE: The TargetMachine owns TLOF.
TargetLoweringBase::TargetLoweringBase(const TargetMachine &tm)
    : TM(tm), Libcalls(TM.getTargetTriple()) {
  initActions();

  // Perform these initializations only once.
  MaxStoresPerMemset = MaxStoresPerMemcpy = MaxStoresPerMemmove =
      MaxLoadsPerMemcmp = 8;
  MaxGluedStoresPerMemcpy = 0;
  MaxStoresPerMemsetOptSize = MaxStoresPerMemcpyOptSize =
      MaxStoresPerMemmoveOptSize = MaxLoadsPerMemcmpOptSize = 4;
  HasMultipleConditionRegisters = false;
  HasExtractBitsInsn = false;
  JumpIsExpensive = JumpIsExpensiveOverride;
  PredictableSelectIsExpensive = false;
  EnableExtLdPromotion = false;
  StackPointerRegisterToSaveRestore = 0;
  BooleanContents = UndefinedBooleanContent;
  BooleanFloatContents = UndefinedBooleanContent;
  BooleanVectorContents = UndefinedBooleanContent;
  SchedPreferenceInfo = Sched::ILP;
  GatherAllAliasesMaxDepth = 18;
  IsStrictFPEnabled = DisableStrictNodeMutation;
  MaxBytesForAlignment = 0;
  MaxAtomicSizeInBitsSupported = 0;

  // Assume that even with libcalls, no target supports wider than 128 bit
  // division.
  MaxDivRemBitWidthSupported = 128;

  MaxLargeFPConvertBitWidthSupported = llvm::IntegerType::MAX_INT_BITS;

  MinCmpXchgSizeInBits = 0;
  SupportsUnalignedAtomics = false;

  RTLIB::initCmpLibcallCCs(CmpLibcallCCs);
}

void TargetLoweringBase::initActions() {
  // All operations default to being supported.
  memset(OpActions, 0, sizeof(OpActions));
  memset(LoadExtActions, 0, sizeof(LoadExtActions));
  memset(TruncStoreActions, 0, sizeof(TruncStoreActions));
  memset(IndexedModeActions, 0, sizeof(IndexedModeActions));
  memset(CondCodeActions, 0, sizeof(CondCodeActions));
  std::fill(std::begin(RegClassForVT), std::end(RegClassForVT), nullptr);
  std::fill(std::begin(TargetDAGCombineArray),
            std::end(TargetDAGCombineArray), 0);

  // Let extending atomic loads be unsupported by default.
  for (MVT ValVT : MVT::all_valuetypes())
    for (MVT MemVT : MVT::all_valuetypes())
      setAtomicLoadExtAction({ISD::SEXTLOAD, ISD::ZEXTLOAD}, ValVT, MemVT,
                             Expand);

  // We're somewhat special casing MVT::i2 and MVT::i4. Ideally we want to
  // remove this and targets should individually set these types if not legal.
  for (ISD::NodeType NT : enum_seq(ISD::DELETED_NODE, ISD::BUILTIN_OP_END,
                                   force_iteration_on_noniterable_enum)) {
    for (MVT VT : {MVT::i2, MVT::i4})
      OpActions[(unsigned)VT.SimpleTy][NT] = Expand;
  }
  for (MVT AVT : MVT::all_valuetypes()) {
    for (MVT VT : {MVT::i2, MVT::i4, MVT::v128i2, MVT::v64i4}) {
      setTruncStoreAction(AVT, VT, Expand);
      setLoadExtAction(ISD::EXTLOAD, AVT, VT, Expand);
      setLoadExtAction(ISD::ZEXTLOAD, AVT, VT, Expand);
    }
  }
  for (unsigned IM = (unsigned)ISD::PRE_INC;
       IM != (unsigned)ISD::LAST_INDEXED_MODE; ++IM) {
    for (MVT VT : {MVT::i2, MVT::i4}) {
      setIndexedLoadAction(IM, VT, Expand);
      setIndexedStoreAction(IM, VT, Expand);
      setIndexedMaskedLoadAction(IM, VT, Expand);
      setIndexedMaskedStoreAction(IM, VT, Expand);
    }
  }

  for (MVT VT : MVT::fp_valuetypes()) {
    MVT IntVT = MVT::getIntegerVT(VT.getFixedSizeInBits());
    if (IntVT.isValid()) {
      setOperationAction(ISD::ATOMIC_SWAP, VT, Promote);
      AddPromotedToType(ISD::ATOMIC_SWAP, VT, IntVT);
    }
  }

  // Set default actions for various operations.
  for (MVT VT : MVT::all_valuetypes()) {
    // Default all indexed load / store to expand.
    for (unsigned IM = (unsigned)ISD::PRE_INC;
         IM != (unsigned)ISD::LAST_INDEXED_MODE; ++IM) {
      setIndexedLoadAction(IM, VT, Expand);
      setIndexedStoreAction(IM, VT, Expand);
      setIndexedMaskedLoadAction(IM, VT, Expand);
      setIndexedMaskedStoreAction(IM, VT, Expand);
    }

    // Most backends expect to see the node which just returns the value loaded.
    setOperationAction(ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, VT, Expand);

    // These operations default to expand.
    setOperationAction({ISD::FGETSIGN,       ISD::CONCAT_VECTORS,
                        ISD::FMINNUM,        ISD::FMAXNUM,
                        ISD::FMINNUM_IEEE,   ISD::FMAXNUM_IEEE,
                        ISD::FMINIMUM,       ISD::FMAXIMUM,
                        ISD::FMAD,           ISD::SMIN,
                        ISD::SMAX,           ISD::UMIN,
                        ISD::UMAX,           ISD::ABS,
                        ISD::FSHL,           ISD::FSHR,
                        ISD::SADDSAT,        ISD::UADDSAT,
                        ISD::SSUBSAT,        ISD::USUBSAT,
                        ISD::SSHLSAT,        ISD::USHLSAT,
                        ISD::SMULFIX,        ISD::SMULFIXSAT,
                        ISD::UMULFIX,        ISD::UMULFIXSAT,
                        ISD::SDIVFIX,        ISD::SDIVFIXSAT,
                        ISD::UDIVFIX,        ISD::UDIVFIXSAT,
                        ISD::FP_TO_SINT_SAT, ISD::FP_TO_UINT_SAT,
                        ISD::IS_FPCLASS},
                       VT, Expand);

    // Overflow operations default to expand
    setOperationAction({ISD::SADDO, ISD::SSUBO, ISD::UADDO, ISD::USUBO,
                        ISD::SMULO, ISD::UMULO},
                       VT, Expand);

    // Carry-using overflow operations default to expand.
    setOperationAction({ISD::UADDO_CARRY, ISD::USUBO_CARRY, ISD::SETCCCARRY,
                        ISD::SADDO_CARRY, ISD::SSUBO_CARRY},
                       VT, Expand);

    // ADDC/ADDE/SUBC/SUBE default to expand.
    setOperationAction({ISD::ADDC, ISD::ADDE, ISD::SUBC, ISD::SUBE}, VT,
                       Expand);

    // [US]CMP default to expand
    setOperationAction({ISD::UCMP, ISD::SCMP}, VT, Expand);

    // Halving adds
    setOperationAction(
        {ISD::AVGFLOORS, ISD::AVGFLOORU, ISD::AVGCEILS, ISD::AVGCEILU}, VT,
        Expand);

    // Absolute difference
    setOperationAction({ISD::ABDS, ISD::ABDU}, VT, Expand);

    // These default to Expand so they will be expanded to CTLZ/CTTZ by default.
    setOperationAction({ISD::CTLZ_ZERO_UNDEF, ISD::CTTZ_ZERO_UNDEF}, VT,
                       Expand);

    setOperationAction({ISD::BITREVERSE, ISD::PARITY}, VT, Expand);

    // These library functions default to expand.
    setOperationAction({ISD::FROUND, ISD::FPOWI, ISD::FLDEXP, ISD::FFREXP}, VT,
                       Expand);

    // These operations default to expand for vector types.
    if (VT.isVector())
      setOperationAction(
          {ISD::FCOPYSIGN, ISD::SIGN_EXTEND_INREG, ISD::ANY_EXTEND_VECTOR_INREG,
           ISD::SIGN_EXTEND_VECTOR_INREG, ISD::ZERO_EXTEND_VECTOR_INREG,
           ISD::SPLAT_VECTOR, ISD::LRINT, ISD::LLRINT, ISD::FTAN, ISD::FACOS,
           ISD::FASIN, ISD::FATAN, ISD::FCOSH, ISD::FSINH, ISD::FTANH},
          VT, Expand);

      // Constrained floating-point operations default to expand.
#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
    setOperationAction(ISD::STRICT_##DAGN, VT, Expand);
#include "llvm/IR/ConstrainedOps.def"

    // For most targets @llvm.get.dynamic.area.offset just returns 0.
    setOperationAction(ISD::GET_DYNAMIC_AREA_OFFSET, VT, Expand);

    // Vector reduction default to expand.
    setOperationAction(
        {ISD::VECREDUCE_FADD, ISD::VECREDUCE_FMUL, ISD::VECREDUCE_ADD,
         ISD::VECREDUCE_MUL, ISD::VECREDUCE_AND, ISD::VECREDUCE_OR,
         ISD::VECREDUCE_XOR, ISD::VECREDUCE_SMAX, ISD::VECREDUCE_SMIN,
         ISD::VECREDUCE_UMAX, ISD::VECREDUCE_UMIN, ISD::VECREDUCE_FMAX,
         ISD::VECREDUCE_FMIN, ISD::VECREDUCE_FMAXIMUM, ISD::VECREDUCE_FMINIMUM,
         ISD::VECREDUCE_SEQ_FADD, ISD::VECREDUCE_SEQ_FMUL},
        VT, Expand);

    // Named vector shuffles default to expand.
    setOperationAction(ISD::VECTOR_SPLICE, VT, Expand);

    // Only some target support this vector operation. Most need to expand it.
    setOperationAction(ISD::VECTOR_COMPRESS, VT, Expand);

    // VP operations default to expand.
#define BEGIN_REGISTER_VP_SDNODE(SDOPC, ...)                                   \
    setOperationAction(ISD::SDOPC, VT, Expand);
#include "llvm/IR/VPIntrinsics.def"

    // FP environment operations default to expand.
    setOperationAction(ISD::GET_FPENV, VT, Expand);
    setOperationAction(ISD::SET_FPENV, VT, Expand);
    setOperationAction(ISD::RESET_FPENV, VT, Expand);
  }

  // Most targets ignore the @llvm.prefetch intrinsic.
  setOperationAction(ISD::PREFETCH, MVT::Other, Expand);

  // Most targets also ignore the @llvm.readcyclecounter intrinsic.
  setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, Expand);

  // Most targets also ignore the @llvm.readsteadycounter intrinsic.
  setOperationAction(ISD::READSTEADYCOUNTER, MVT::i64, Expand);

  // ConstantFP nodes default to expand.  Targets can either change this to
  // Legal, in which case all fp constants are legal, or use isFPImmLegal()
  // to optimize expansions for certain constants.
  setOperationAction(ISD::ConstantFP,
                     {MVT::bf16, MVT::f16, MVT::f32, MVT::f64, MVT::f80, MVT::f128},
                     Expand);

  // These library functions default to expand.
  setOperationAction({ISD::FCBRT,      ISD::FLOG,    ISD::FLOG2,  ISD::FLOG10,
                      ISD::FEXP,       ISD::FEXP2,   ISD::FEXP10, ISD::FFLOOR,
                      ISD::FNEARBYINT, ISD::FCEIL,   ISD::FRINT,  ISD::FTRUNC,
                      ISD::LROUND,     ISD::LLROUND, ISD::LRINT,  ISD::LLRINT,
                      ISD::FROUNDEVEN, ISD::FTAN,    ISD::FACOS,  ISD::FASIN,
                      ISD::FATAN,      ISD::FCOSH,   ISD::FSINH,  ISD::FTANH},
                     {MVT::f32, MVT::f64, MVT::f128}, Expand);

  setOperationAction({ISD::FTAN, ISD::FACOS, ISD::FASIN, ISD::FATAN, ISD::FCOSH,
                      ISD::FSINH, ISD::FTANH},
                     MVT::f16, Promote);
  // Default ISD::TRAP to expand (which turns it into abort).
  setOperationAction(ISD::TRAP, MVT::Other, Expand);

  // On most systems, DEBUGTRAP and TRAP have no difference. The "Expand"
  // here is to inform DAG Legalizer to replace DEBUGTRAP with TRAP.
  setOperationAction(ISD::DEBUGTRAP, MVT::Other, Expand);

  setOperationAction(ISD::UBSANTRAP, MVT::Other, Expand);

  setOperationAction(ISD::GET_FPENV_MEM, MVT::Other, Expand);
  setOperationAction(ISD::SET_FPENV_MEM, MVT::Other, Expand);

  for (MVT VT : {MVT::i8, MVT::i16, MVT::i32, MVT::i64}) {
    setOperationAction(ISD::GET_FPMODE, VT, Expand);
    setOperationAction(ISD::SET_FPMODE, VT, Expand);
  }
  setOperationAction(ISD::RESET_FPMODE, MVT::Other, Expand);

  // This one by default will call __clear_cache unless the target
  // wants something different.
  setOperationAction(ISD::CLEAR_CACHE, MVT::Other, LibCall);
}

MVT TargetLoweringBase::getScalarShiftAmountTy(const DataLayout &DL,
                                               EVT) const {
  return MVT::getIntegerVT(DL.getPointerSizeInBits(0));
}

EVT TargetLoweringBase::getShiftAmountTy(EVT LHSTy,
                                         const DataLayout &DL) const {
  assert(LHSTy.isInteger() && "Shift amount is not an integer type!");
  if (LHSTy.isVector())
    return LHSTy;
  MVT ShiftVT = getScalarShiftAmountTy(DL, LHSTy);
  // If any possible shift value won't fit in the prefered type, just use
  // something safe. Assume it will be legalized when the shift is expanded.
  if (ShiftVT.getSizeInBits() < Log2_32_Ceil(LHSTy.getSizeInBits()))
    ShiftVT = MVT::i32;
  assert(ShiftVT.getSizeInBits() >= Log2_32_Ceil(LHSTy.getSizeInBits()) &&
         "ShiftVT is still too small!");
  return ShiftVT;
}

bool TargetLoweringBase::canOpTrap(unsigned Op, EVT VT) const {
  assert(isTypeLegal(VT));
  switch (Op) {
  default:
    return false;
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::SREM:
  case ISD::UREM:
    return true;
  }
}

bool TargetLoweringBase::isFreeAddrSpaceCast(unsigned SrcAS,
                                             unsigned DestAS) const {
  return TM.isNoopAddrSpaceCast(SrcAS, DestAS);
}

unsigned TargetLoweringBase::getBitWidthForCttzElements(
    Type *RetTy, ElementCount EC, bool ZeroIsPoison,
    const ConstantRange *VScaleRange) const {
  // Find the smallest "sensible" element type to use for the expansion.
  ConstantRange CR(APInt(64, EC.getKnownMinValue()));
  if (EC.isScalable())
    CR = CR.umul_sat(*VScaleRange);

  if (ZeroIsPoison)
    CR = CR.subtract(APInt(64, 1));

  unsigned EltWidth = RetTy->getScalarSizeInBits();
  EltWidth = std::min(EltWidth, (unsigned)CR.getActiveBits());
  EltWidth = std::max(llvm::bit_ceil(EltWidth), (unsigned)8);

  return EltWidth;
}

void TargetLoweringBase::setJumpIsExpensive(bool isExpensive) {
  // If the command-line option was specified, ignore this request.
  if (!JumpIsExpensiveOverride.getNumOccurrences())
    JumpIsExpensive = isExpensive;
}

TargetLoweringBase::LegalizeKind
TargetLoweringBase::getTypeConversion(LLVMContext &Context, EVT VT) const {
  // If this is a simple type, use the ComputeRegisterProp mechanism.
  if (VT.isSimple()) {
    MVT SVT = VT.getSimpleVT();
    assert((unsigned)SVT.SimpleTy < std::size(TransformToType));
    MVT NVT = TransformToType[SVT.SimpleTy];
    LegalizeTypeAction LA = ValueTypeActions.getTypeAction(SVT);

    assert((LA == TypeLegal || LA == TypeSoftenFloat ||
            LA == TypeSoftPromoteHalf ||
            (NVT.isVector() ||
             ValueTypeActions.getTypeAction(NVT) != TypePromoteInteger)) &&
           "Promote may not follow Expand or Promote");

    if (LA == TypeSplitVector)
      return LegalizeKind(LA, EVT(SVT).getHalfNumVectorElementsVT(Context));
    if (LA == TypeScalarizeVector)
      return LegalizeKind(LA, SVT.getVectorElementType());
    return LegalizeKind(LA, NVT);
  }

  // Handle Extended Scalar Types.
  if (!VT.isVector()) {
    assert(VT.isInteger() && "Float types must be simple");
    unsigned BitSize = VT.getSizeInBits();
    // First promote to a power-of-two size, then expand if necessary.
    if (BitSize < 8 || !isPowerOf2_32(BitSize)) {
      EVT NVT = VT.getRoundIntegerType(Context);
      assert(NVT != VT && "Unable to round integer VT");
      LegalizeKind NextStep = getTypeConversion(Context, NVT);
      // Avoid multi-step promotion.
      if (NextStep.first == TypePromoteInteger)
        return NextStep;
      // Return rounded integer type.
      return LegalizeKind(TypePromoteInteger, NVT);
    }

    return LegalizeKind(TypeExpandInteger,
                        EVT::getIntegerVT(Context, VT.getSizeInBits() / 2));
  }

  // Handle vector types.
  ElementCount NumElts = VT.getVectorElementCount();
  EVT EltVT = VT.getVectorElementType();

  // Vectors with only one element are always scalarized.
  if (NumElts.isScalar())
    return LegalizeKind(TypeScalarizeVector, EltVT);

  // Try to widen vector elements until the element type is a power of two and
  // promote it to a legal type later on, for example:
  // <3 x i8> -> <4 x i8> -> <4 x i32>
  if (EltVT.isInteger()) {
    // Vectors with a number of elements that is not a power of two are always
    // widened, for example <3 x i8> -> <4 x i8>.
    if (!VT.isPow2VectorType()) {
      NumElts = NumElts.coefficientNextPowerOf2();
      EVT NVT = EVT::getVectorVT(Context, EltVT, NumElts);
      return LegalizeKind(TypeWidenVector, NVT);
    }

    // Examine the element type.
    LegalizeKind LK = getTypeConversion(Context, EltVT);

    // If type is to be expanded, split the vector.
    //  <4 x i140> -> <2 x i140>
    if (LK.first == TypeExpandInteger) {
      if (VT.getVectorElementCount().isScalable())
        return LegalizeKind(TypeScalarizeScalableVector, EltVT);
      return LegalizeKind(TypeSplitVector,
                          VT.getHalfNumVectorElementsVT(Context));
    }

    // Promote the integer element types until a legal vector type is found
    // or until the element integer type is too big. If a legal type was not
    // found, fallback to the usual mechanism of widening/splitting the
    // vector.
    EVT OldEltVT = EltVT;
    while (true) {
      // Increase the bitwidth of the element to the next pow-of-two
      // (which is greater than 8 bits).
      EltVT = EVT::getIntegerVT(Context, 1 + EltVT.getSizeInBits())
                  .getRoundIntegerType(Context);

      // Stop trying when getting a non-simple element type.
      // Note that vector elements may be greater than legal vector element
      // types. Example: X86 XMM registers hold 64bit element on 32bit
      // systems.
      if (!EltVT.isSimple())
        break;

      // Build a new vector type and check if it is legal.
      MVT NVT = MVT::getVectorVT(EltVT.getSimpleVT(), NumElts);
      // Found a legal promoted vector type.
      if (NVT != MVT() && ValueTypeActions.getTypeAction(NVT) == TypeLegal)
        return LegalizeKind(TypePromoteInteger,
                            EVT::getVectorVT(Context, EltVT, NumElts));
    }

    // Reset the type to the unexpanded type if we did not find a legal vector
    // type with a promoted vector element type.
    EltVT = OldEltVT;
  }

  // Try to widen the vector until a legal type is found.
  // If there is no wider legal type, split the vector.
  while (true) {
    // Round up to the next power of 2.
    NumElts = NumElts.coefficientNextPowerOf2();

    // If there is no simple vector type with this many elements then there
    // cannot be a larger legal vector type.  Note that this assumes that
    // there are no skipped intermediate vector types in the simple types.
    if (!EltVT.isSimple())
      break;
    MVT LargerVector = MVT::getVectorVT(EltVT.getSimpleVT(), NumElts);
    if (LargerVector == MVT())
      break;

    // If this type is legal then widen the vector.
    if (ValueTypeActions.getTypeAction(LargerVector) == TypeLegal)
      return LegalizeKind(TypeWidenVector, LargerVector);
  }

  // Widen odd vectors to next power of two.
  if (!VT.isPow2VectorType()) {
    EVT NVT = VT.getPow2VectorType(Context);
    return LegalizeKind(TypeWidenVector, NVT);
  }

  if (VT.getVectorElementCount() == ElementCount::getScalable(1))
    return LegalizeKind(TypeScalarizeScalableVector, EltVT);

  // Vectors with illegal element types are expanded.
  EVT NVT = EVT::getVectorVT(Context, EltVT,
                             VT.getVectorElementCount().divideCoefficientBy(2));
  return LegalizeKind(TypeSplitVector, NVT);
}

static unsigned getVectorTypeBreakdownMVT(MVT VT, MVT &IntermediateVT,
                                          unsigned &NumIntermediates,
                                          MVT &RegisterVT,
                                          TargetLoweringBase *TLI) {
  // Figure out the right, legal destination reg to copy into.
  ElementCount EC = VT.getVectorElementCount();
  MVT EltTy = VT.getVectorElementType();

  unsigned NumVectorRegs = 1;

  // Scalable vectors cannot be scalarized, so splitting or widening is
  // required.
  if (VT.isScalableVector() && !isPowerOf2_32(EC.getKnownMinValue()))
    llvm_unreachable(
        "Splitting or widening of non-power-of-2 MVTs is not implemented.");

  // FIXME: We don't support non-power-of-2-sized vectors for now.
  // Ideally we could break down into LHS/RHS like LegalizeDAG does.
  if (!isPowerOf2_32(EC.getKnownMinValue())) {
    // Split EC to unit size (scalable property is preserved).
    NumVectorRegs = EC.getKnownMinValue();
    EC = ElementCount::getFixed(1);
  }

  // Divide the input until we get to a supported size. This will
  // always end up with an EC that represent a scalar or a scalable
  // scalar.
  while (EC.getKnownMinValue() > 1 &&
         !TLI->isTypeLegal(MVT::getVectorVT(EltTy, EC))) {
    EC = EC.divideCoefficientBy(2);
    NumVectorRegs <<= 1;
  }

  NumIntermediates = NumVectorRegs;

  MVT NewVT = MVT::getVectorVT(EltTy, EC);
  if (!TLI->isTypeLegal(NewVT))
    NewVT = EltTy;
  IntermediateVT = NewVT;

  unsigned LaneSizeInBits = NewVT.getScalarSizeInBits();

  // Convert sizes such as i33 to i64.
  LaneSizeInBits = llvm::bit_ceil(LaneSizeInBits);

  MVT DestVT = TLI->getRegisterType(NewVT);
  RegisterVT = DestVT;
  if (EVT(DestVT).bitsLT(NewVT))    // Value is expanded, e.g. i64 -> i16.
    return NumVectorRegs * (LaneSizeInBits / DestVT.getScalarSizeInBits());

  // Otherwise, promotion or legal types use the same number of registers as
  // the vector decimated to the appropriate level.
  return NumVectorRegs;
}

/// isLegalRC - Return true if the value types that can be represented by the
/// specified register class are all legal.
bool TargetLoweringBase::isLegalRC(const TargetRegisterInfo &TRI,
                                   const TargetRegisterClass &RC) const {
  for (const auto *I = TRI.legalclasstypes_begin(RC); *I != MVT::Other; ++I)
    if (isTypeLegal(*I))
      return true;
  return false;
}

/// Replace/modify any TargetFrameIndex operands with a targte-dependent
/// sequence of memory operands that is recognized by PrologEpilogInserter.
MachineBasicBlock *
TargetLoweringBase::emitPatchPoint(MachineInstr &InitialMI,
                                   MachineBasicBlock *MBB) const {
  MachineInstr *MI = &InitialMI;
  MachineFunction &MF = *MI->getMF();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // We're handling multiple types of operands here:
  // PATCHPOINT MetaArgs - live-in, read only, direct
  // STATEPOINT Deopt Spill - live-through, read only, indirect
  // STATEPOINT Deopt Alloca - live-through, read only, direct
  // (We're currently conservative and mark the deopt slots read/write in
  // practice.)
  // STATEPOINT GC Spill - live-through, read/write, indirect
  // STATEPOINT GC Alloca - live-through, read/write, direct
  // The live-in vs live-through is handled already (the live through ones are
  // all stack slots), but we need to handle the different type of stackmap
  // operands and memory effects here.

  if (llvm::none_of(MI->operands(),
                    [](MachineOperand &Operand) { return Operand.isFI(); }))
    return MBB;

  MachineInstrBuilder MIB = BuildMI(MF, MI->getDebugLoc(), MI->getDesc());

  // Inherit previous memory operands.
  MIB.cloneMemRefs(*MI);

  for (unsigned i = 0; i < MI->getNumOperands(); ++i) {
    MachineOperand &MO = MI->getOperand(i);
    if (!MO.isFI()) {
      // Index of Def operand this Use it tied to.
      // Since Defs are coming before Uses, if Use is tied, then
      // index of Def must be smaller that index of that Use.
      // Also, Defs preserve their position in new MI.
      unsigned TiedTo = i;
      if (MO.isReg() && MO.isTied())
        TiedTo = MI->findTiedOperandIdx(i);
      MIB.add(MO);
      if (TiedTo < i)
        MIB->tieOperands(TiedTo, MIB->getNumOperands() - 1);
      continue;
    }

    // foldMemoryOperand builds a new MI after replacing a single FI operand
    // with the canonical set of five x86 addressing-mode operands.
    int FI = MO.getIndex();

    // Add frame index operands recognized by stackmaps.cpp
    if (MFI.isStatepointSpillSlotObjectIndex(FI)) {
      // indirect-mem-ref tag, size, #FI, offset.
      // Used for spills inserted by StatepointLowering.  This codepath is not
      // used for patchpoints/stackmaps at all, for these spilling is done via
      // foldMemoryOperand callback only.
      assert(MI->getOpcode() == TargetOpcode::STATEPOINT && "sanity");
      MIB.addImm(StackMaps::IndirectMemRefOp);
      MIB.addImm(MFI.getObjectSize(FI));
      MIB.add(MO);
      MIB.addImm(0);
    } else {
      // direct-mem-ref tag, #FI, offset.
      // Used by patchpoint, and direct alloca arguments to statepoints
      MIB.addImm(StackMaps::DirectMemRefOp);
      MIB.add(MO);
      MIB.addImm(0);
    }

    assert(MIB->mayLoad() && "Folded a stackmap use to a non-load!");

    // Add a new memory operand for this FI.
    assert(MFI.getObjectOffset(FI) != -1);

    // Note: STATEPOINT MMOs are added during SelectionDAG.  STACKMAP, and
    // PATCHPOINT should be updated to do the same. (TODO)
    if (MI->getOpcode() != TargetOpcode::STATEPOINT) {
      auto Flags = MachineMemOperand::MOLoad;
      MachineMemOperand *MMO = MF.getMachineMemOperand(
          MachinePointerInfo::getFixedStack(MF, FI), Flags,
          MF.getDataLayout().getPointerSize(), MFI.getObjectAlign(FI));
      MIB->addMemOperand(MF, MMO);
    }
  }
  MBB->insert(MachineBasicBlock::iterator(MI), MIB);
  MI->eraseFromParent();
  return MBB;
}

/// findRepresentativeClass - Return the largest legal super-reg register class
/// of the register class for the specified type and its associated "cost".
// This function is in TargetLowering because it uses RegClassForVT which would
// need to be moved to TargetRegisterInfo and would necessitate moving
// isTypeLegal over as well - a massive change that would just require
// TargetLowering having a TargetRegisterInfo class member that it would use.
std::pair<const TargetRegisterClass *, uint8_t>
TargetLoweringBase::findRepresentativeClass(const TargetRegisterInfo *TRI,
                                            MVT VT) const {
  const TargetRegisterClass *RC = RegClassForVT[VT.SimpleTy];
  if (!RC)
    return std::make_pair(RC, 0);

  // Compute the set of all super-register classes.
  BitVector SuperRegRC(TRI->getNumRegClasses());
  for (SuperRegClassIterator RCI(RC, TRI); RCI.isValid(); ++RCI)
    SuperRegRC.setBitsInMask(RCI.getMask());

  // Find the first legal register class with the largest spill size.
  const TargetRegisterClass *BestRC = RC;
  for (unsigned i : SuperRegRC.set_bits()) {
    const TargetRegisterClass *SuperRC = TRI->getRegClass(i);
    // We want the largest possible spill size.
    if (TRI->getSpillSize(*SuperRC) <= TRI->getSpillSize(*BestRC))
      continue;
    if (!isLegalRC(*TRI, *SuperRC))
      continue;
    BestRC = SuperRC;
  }
  return std::make_pair(BestRC, 1);
}

/// computeRegisterProperties - Once all of the register classes are added,
/// this allows us to compute derived properties we expose.
void TargetLoweringBase::computeRegisterProperties(
    const TargetRegisterInfo *TRI) {
  // Everything defaults to needing one register.
  for (unsigned i = 0; i != MVT::VALUETYPE_SIZE; ++i) {
    NumRegistersForVT[i] = 1;
    RegisterTypeForVT[i] = TransformToType[i] = (MVT::SimpleValueType)i;
  }
  // ...except isVoid, which doesn't need any registers.
  NumRegistersForVT[MVT::isVoid] = 0;

  // Find the largest integer register class.
  unsigned LargestIntReg = MVT::LAST_INTEGER_VALUETYPE;
  for (; RegClassForVT[LargestIntReg] == nullptr; --LargestIntReg)
    assert(LargestIntReg != MVT::i1 && "No integer registers defined!");

  // Every integer value type larger than this largest register takes twice as
  // many registers to represent as the previous ValueType.
  for (unsigned ExpandedReg = LargestIntReg + 1;
       ExpandedReg <= MVT::LAST_INTEGER_VALUETYPE; ++ExpandedReg) {
    NumRegistersForVT[ExpandedReg] = 2*NumRegistersForVT[ExpandedReg-1];
    RegisterTypeForVT[ExpandedReg] = (MVT::SimpleValueType)LargestIntReg;
    TransformToType[ExpandedReg] = (MVT::SimpleValueType)(ExpandedReg - 1);
    ValueTypeActions.setTypeAction((MVT::SimpleValueType)ExpandedReg,
                                   TypeExpandInteger);
  }

  // Inspect all of the ValueType's smaller than the largest integer
  // register to see which ones need promotion.
  unsigned LegalIntReg = LargestIntReg;
  for (unsigned IntReg = LargestIntReg - 1;
       IntReg >= (unsigned)MVT::i1; --IntReg) {
    MVT IVT = (MVT::SimpleValueType)IntReg;
    if (isTypeLegal(IVT)) {
      LegalIntReg = IntReg;
    } else {
      RegisterTypeForVT[IntReg] = TransformToType[IntReg] =
        (MVT::SimpleValueType)LegalIntReg;
      ValueTypeActions.setTypeAction(IVT, TypePromoteInteger);
    }
  }

  // ppcf128 type is really two f64's.
  if (!isTypeLegal(MVT::ppcf128)) {
    if (isTypeLegal(MVT::f64)) {
      NumRegistersForVT[MVT::ppcf128] = 2*NumRegistersForVT[MVT::f64];
      RegisterTypeForVT[MVT::ppcf128] = MVT::f64;
      TransformToType[MVT::ppcf128] = MVT::f64;
      ValueTypeActions.setTypeAction(MVT::ppcf128, TypeExpandFloat);
    } else {
      NumRegistersForVT[MVT::ppcf128] = NumRegistersForVT[MVT::i128];
      RegisterTypeForVT[MVT::ppcf128] = RegisterTypeForVT[MVT::i128];
      TransformToType[MVT::ppcf128] = MVT::i128;
      ValueTypeActions.setTypeAction(MVT::ppcf128, TypeSoftenFloat);
    }
  }

  // Decide how to handle f128. If the target does not have native f128 support,
  // expand it to i128 and we will be generating soft float library calls.
  if (!isTypeLegal(MVT::f128)) {
    NumRegistersForVT[MVT::f128] = NumRegistersForVT[MVT::i128];
    RegisterTypeForVT[MVT::f128] = RegisterTypeForVT[MVT::i128];
    TransformToType[MVT::f128] = MVT::i128;
    ValueTypeActions.setTypeAction(MVT::f128, TypeSoftenFloat);
  }

  // Decide how to handle f80. If the target does not have native f80 support,
  // expand it to i96 and we will be generating soft float library calls.
  if (!isTypeLegal(MVT::f80)) {
    NumRegistersForVT[MVT::f80] = 3*NumRegistersForVT[MVT::i32];
    RegisterTypeForVT[MVT::f80] = RegisterTypeForVT[MVT::i32];
    TransformToType[MVT::f80] = MVT::i32;
    ValueTypeActions.setTypeAction(MVT::f80, TypeSoftenFloat);
  }

  // Decide how to handle f64. If the target does not have native f64 support,
  // expand it to i64 and we will be generating soft float library calls.
  if (!isTypeLegal(MVT::f64)) {
    NumRegistersForVT[MVT::f64] = NumRegistersForVT[MVT::i64];
    RegisterTypeForVT[MVT::f64] = RegisterTypeForVT[MVT::i64];
    TransformToType[MVT::f64] = MVT::i64;
    ValueTypeActions.setTypeAction(MVT::f64, TypeSoftenFloat);
  }

  // Decide how to handle f32. If the target does not have native f32 support,
  // expand it to i32 and we will be generating soft float library calls.
  if (!isTypeLegal(MVT::f32)) {
    NumRegistersForVT[MVT::f32] = NumRegistersForVT[MVT::i32];
    RegisterTypeForVT[MVT::f32] = RegisterTypeForVT[MVT::i32];
    TransformToType[MVT::f32] = MVT::i32;
    ValueTypeActions.setTypeAction(MVT::f32, TypeSoftenFloat);
  }

  // Decide how to handle f16. If the target does not have native f16 support,
  // promote it to f32, because there are no f16 library calls (except for
  // conversions).
  if (!isTypeLegal(MVT::f16)) {
    // Allow targets to control how we legalize half.
    bool SoftPromoteHalfType = softPromoteHalfType();
    bool UseFPRegsForHalfType = !SoftPromoteHalfType || useFPRegsForHalfType();

    if (!UseFPRegsForHalfType) {
      NumRegistersForVT[MVT::f16] = NumRegistersForVT[MVT::i16];
      RegisterTypeForVT[MVT::f16] = RegisterTypeForVT[MVT::i16];
    } else {
      NumRegistersForVT[MVT::f16] = NumRegistersForVT[MVT::f32];
      RegisterTypeForVT[MVT::f16] = RegisterTypeForVT[MVT::f32];
    }
    TransformToType[MVT::f16] = MVT::f32;
    if (SoftPromoteHalfType) {
      ValueTypeActions.setTypeAction(MVT::f16, TypeSoftPromoteHalf);
    } else {
      ValueTypeActions.setTypeAction(MVT::f16, TypePromoteFloat);
    }
  }

  // Decide how to handle bf16. If the target does not have native bf16 support,
  // promote it to f32, because there are no bf16 library calls (except for
  // converting from f32 to bf16).
  if (!isTypeLegal(MVT::bf16)) {
    NumRegistersForVT[MVT::bf16] = NumRegistersForVT[MVT::f32];
    RegisterTypeForVT[MVT::bf16] = RegisterTypeForVT[MVT::f32];
    TransformToType[MVT::bf16] = MVT::f32;
    ValueTypeActions.setTypeAction(MVT::bf16, TypeSoftPromoteHalf);
  }

  // Loop over all of the vector value types to see which need transformations.
  for (unsigned i = MVT::FIRST_VECTOR_VALUETYPE;
       i <= (unsigned)MVT::LAST_VECTOR_VALUETYPE; ++i) {
    MVT VT = (MVT::SimpleValueType) i;
    if (isTypeLegal(VT))
      continue;

    MVT EltVT = VT.getVectorElementType();
    ElementCount EC = VT.getVectorElementCount();
    bool IsLegalWiderType = false;
    bool IsScalable = VT.isScalableVector();
    LegalizeTypeAction PreferredAction = getPreferredVectorAction(VT);
    switch (PreferredAction) {
    case TypePromoteInteger: {
      MVT::SimpleValueType EndVT = IsScalable ?
                                   MVT::LAST_INTEGER_SCALABLE_VECTOR_VALUETYPE :
                                   MVT::LAST_INTEGER_FIXEDLEN_VECTOR_VALUETYPE;
      // Try to promote the elements of integer vectors. If no legal
      // promotion was found, fall through to the widen-vector method.
      for (unsigned nVT = i + 1;
           (MVT::SimpleValueType)nVT <= EndVT; ++nVT) {
        MVT SVT = (MVT::SimpleValueType) nVT;
        // Promote vectors of integers to vectors with the same number
        // of elements, with a wider element type.
        if (SVT.getScalarSizeInBits() > EltVT.getFixedSizeInBits() &&
            SVT.getVectorElementCount() == EC && isTypeLegal(SVT)) {
          TransformToType[i] = SVT;
          RegisterTypeForVT[i] = SVT;
          NumRegistersForVT[i] = 1;
          ValueTypeActions.setTypeAction(VT, TypePromoteInteger);
          IsLegalWiderType = true;
          break;
        }
      }
      if (IsLegalWiderType)
        break;
      [[fallthrough]];
    }

    case TypeWidenVector:
      if (isPowerOf2_32(EC.getKnownMinValue())) {
        // Try to widen the vector.
        for (unsigned nVT = i + 1; nVT <= MVT::LAST_VECTOR_VALUETYPE; ++nVT) {
          MVT SVT = (MVT::SimpleValueType) nVT;
          if (SVT.getVectorElementType() == EltVT &&
              SVT.isScalableVector() == IsScalable &&
              SVT.getVectorElementCount().getKnownMinValue() >
                  EC.getKnownMinValue() &&
              isTypeLegal(SVT)) {
            TransformToType[i] = SVT;
            RegisterTypeForVT[i] = SVT;
            NumRegistersForVT[i] = 1;
            ValueTypeActions.setTypeAction(VT, TypeWidenVector);
            IsLegalWiderType = true;
            break;
          }
        }
        if (IsLegalWiderType)
          break;
      } else {
        // Only widen to the next power of 2 to keep consistency with EVT.
        MVT NVT = VT.getPow2VectorType();
        if (isTypeLegal(NVT)) {
          TransformToType[i] = NVT;
          ValueTypeActions.setTypeAction(VT, TypeWidenVector);
          RegisterTypeForVT[i] = NVT;
          NumRegistersForVT[i] = 1;
          break;
        }
      }
      [[fallthrough]];

    case TypeSplitVector:
    case TypeScalarizeVector: {
      MVT IntermediateVT;
      MVT RegisterVT;
      unsigned NumIntermediates;
      unsigned NumRegisters = getVectorTypeBreakdownMVT(VT, IntermediateVT,
          NumIntermediates, RegisterVT, this);
      NumRegistersForVT[i] = NumRegisters;
      assert(NumRegistersForVT[i] == NumRegisters &&
             "NumRegistersForVT size cannot represent NumRegisters!");
      RegisterTypeForVT[i] = RegisterVT;

      MVT NVT = VT.getPow2VectorType();
      if (NVT == VT) {
        // Type is already a power of 2.  The default action is to split.
        TransformToType[i] = MVT::Other;
        if (PreferredAction == TypeScalarizeVector)
          ValueTypeActions.setTypeAction(VT, TypeScalarizeVector);
        else if (PreferredAction == TypeSplitVector)
          ValueTypeActions.setTypeAction(VT, TypeSplitVector);
        else if (EC.getKnownMinValue() > 1)
          ValueTypeActions.setTypeAction(VT, TypeSplitVector);
        else
          ValueTypeActions.setTypeAction(VT, EC.isScalable()
                                                 ? TypeScalarizeScalableVector
                                                 : TypeScalarizeVector);
      } else {
        TransformToType[i] = NVT;
        ValueTypeActions.setTypeAction(VT, TypeWidenVector);
      }
      break;
    }
    default:
      llvm_unreachable("Unknown vector legalization action!");
    }
  }

  // Determine the 'representative' register class for each value type.
  // An representative register class is the largest (meaning one which is
  // not a sub-register class / subreg register class) legal register class for
  // a group of value types. For example, on i386, i8, i16, and i32
  // representative would be GR32; while on x86_64 it's GR64.
  for (unsigned i = 0; i != MVT::VALUETYPE_SIZE; ++i) {
    const TargetRegisterClass* RRC;
    uint8_t Cost;
    std::tie(RRC, Cost) = findRepresentativeClass(TRI, (MVT::SimpleValueType)i);
    RepRegClassForVT[i] = RRC;
    RepRegClassCostForVT[i] = Cost;
  }
}

EVT TargetLoweringBase::getSetCCResultType(const DataLayout &DL, LLVMContext &,
                                           EVT VT) const {
  assert(!VT.isVector() && "No default SetCC type for vectors!");
  return getPointerTy(DL).SimpleTy;
}

MVT::SimpleValueType TargetLoweringBase::getCmpLibcallReturnType() const {
  return MVT::i32; // return the default value
}

/// getVectorTypeBreakdown - Vector types are broken down into some number of
/// legal first class types.  For example, MVT::v8f32 maps to 2 MVT::v4f32
/// with Altivec or SSE1, or 8 promoted MVT::f64 values with the X86 FP stack.
/// Similarly, MVT::v2i64 turns into 4 MVT::i32 values with both PPC and X86.
///
/// This method returns the number of registers needed, and the VT for each
/// register.  It also returns the VT and quantity of the intermediate values
/// before they are promoted/expanded.
unsigned TargetLoweringBase::getVectorTypeBreakdown(LLVMContext &Context,
                                                    EVT VT, EVT &IntermediateVT,
                                                    unsigned &NumIntermediates,
                                                    MVT &RegisterVT) const {
  ElementCount EltCnt = VT.getVectorElementCount();

  // If there is a wider vector type with the same element type as this one,
  // or a promoted vector type that has the same number of elements which
  // are wider, then we should convert to that legal vector type.
  // This handles things like <2 x float> -> <4 x float> and
  // <4 x i1> -> <4 x i32>.
  LegalizeTypeAction TA = getTypeAction(Context, VT);
  if (!EltCnt.isScalar() &&
      (TA == TypeWidenVector || TA == TypePromoteInteger)) {
    EVT RegisterEVT = getTypeToTransformTo(Context, VT);
    if (isTypeLegal(RegisterEVT)) {
      IntermediateVT = RegisterEVT;
      RegisterVT = RegisterEVT.getSimpleVT();
      NumIntermediates = 1;
      return 1;
    }
  }

  // Figure out the right, legal destination reg to copy into.
  EVT EltTy = VT.getVectorElementType();

  unsigned NumVectorRegs = 1;

  // Scalable vectors cannot be scalarized, so handle the legalisation of the
  // types like done elsewhere in SelectionDAG.
  if (EltCnt.isScalable()) {
    LegalizeKind LK;
    EVT PartVT = VT;
    do {
      // Iterate until we've found a legal (part) type to hold VT.
      LK = getTypeConversion(Context, PartVT);
      PartVT = LK.second;
    } while (LK.first != TypeLegal);

    if (!PartVT.isVector()) {
      report_fatal_error(
          "Don't know how to legalize this scalable vector type");
    }

    NumIntermediates =
        divideCeil(VT.getVectorElementCount().getKnownMinValue(),
                   PartVT.getVectorElementCount().getKnownMinValue());
    IntermediateVT = PartVT;
    RegisterVT = getRegisterType(Context, IntermediateVT);
    return NumIntermediates;
  }

  // FIXME: We don't support non-power-of-2-sized vectors for now.  Ideally
  // we could break down into LHS/RHS like LegalizeDAG does.
  if (!isPowerOf2_32(EltCnt.getKnownMinValue())) {
    NumVectorRegs = EltCnt.getKnownMinValue();
    EltCnt = ElementCount::getFixed(1);
  }

  // Divide the input until we get to a supported size.  This will always
  // end with a scalar if the target doesn't support vectors.
  while (EltCnt.getKnownMinValue() > 1 &&
         !isTypeLegal(EVT::getVectorVT(Context, EltTy, EltCnt))) {
    EltCnt = EltCnt.divideCoefficientBy(2);
    NumVectorRegs <<= 1;
  }

  NumIntermediates = NumVectorRegs;

  EVT NewVT = EVT::getVectorVT(Context, EltTy, EltCnt);
  if (!isTypeLegal(NewVT))
    NewVT = EltTy;
  IntermediateVT = NewVT;

  MVT DestVT = getRegisterType(Context, NewVT);
  RegisterVT = DestVT;

  if (EVT(DestVT).bitsLT(NewVT)) {  // Value is expanded, e.g. i64 -> i16.
    TypeSize NewVTSize = NewVT.getSizeInBits();
    // Convert sizes such as i33 to i64.
    if (!llvm::has_single_bit<uint32_t>(NewVTSize.getKnownMinValue()))
      NewVTSize = NewVTSize.coefficientNextPowerOf2();
    return NumVectorRegs*(NewVTSize/DestVT.getSizeInBits());
  }

  // Otherwise, promotion or legal types use the same number of registers as
  // the vector decimated to the appropriate level.
  return NumVectorRegs;
}

bool TargetLoweringBase::isSuitableForJumpTable(const SwitchInst *SI,
                                                uint64_t NumCases,
                                                uint64_t Range,
                                                ProfileSummaryInfo *PSI,
                                                BlockFrequencyInfo *BFI) const {
  // FIXME: This function check the maximum table size and density, but the
  // minimum size is not checked. It would be nice if the minimum size is
  // also combined within this function. Currently, the minimum size check is
  // performed in findJumpTable() in SelectionDAGBuiler and
  // getEstimatedNumberOfCaseClusters() in BasicTTIImpl.
  const bool OptForSize =
      SI->getParent()->getParent()->hasOptSize() ||
      llvm::shouldOptimizeForSize(SI->getParent(), PSI, BFI);
  const unsigned MinDensity = getMinimumJumpTableDensity(OptForSize);
  const unsigned MaxJumpTableSize = getMaximumJumpTableSize();

  // Check whether the number of cases is small enough and
  // the range is dense enough for a jump table.
  return (OptForSize || Range <= MaxJumpTableSize) &&
         (NumCases * 100 >= Range * MinDensity);
}

MVT TargetLoweringBase::getPreferredSwitchConditionType(LLVMContext &Context,
                                                        EVT ConditionVT) const {
  return getRegisterType(Context, ConditionVT);
}

/// Get the EVTs and ArgFlags collections that represent the legalized return
/// type of the given function.  This does not require a DAG or a return value,
/// and is suitable for use before any DAGs for the function are constructed.
/// TODO: Move this out of TargetLowering.cpp.
void llvm::GetReturnInfo(CallingConv::ID CC, Type *ReturnType,
                         AttributeList attr,
                         SmallVectorImpl<ISD::OutputArg> &Outs,
                         const TargetLowering &TLI, const DataLayout &DL) {
  SmallVector<EVT, 4> ValueVTs;
  ComputeValueVTs(TLI, DL, ReturnType, ValueVTs);
  unsigned NumValues = ValueVTs.size();
  if (NumValues == 0) return;

  for (unsigned j = 0, f = NumValues; j != f; ++j) {
    EVT VT = ValueVTs[j];
    ISD::NodeType ExtendKind = ISD::ANY_EXTEND;

    if (attr.hasRetAttr(Attribute::SExt))
      ExtendKind = ISD::SIGN_EXTEND;
    else if (attr.hasRetAttr(Attribute::ZExt))
      ExtendKind = ISD::ZERO_EXTEND;

    if (ExtendKind != ISD::ANY_EXTEND && VT.isInteger())
      VT = TLI.getTypeForExtReturn(ReturnType->getContext(), VT, ExtendKind);

    unsigned NumParts =
        TLI.getNumRegistersForCallingConv(ReturnType->getContext(), CC, VT);
    MVT PartVT =
        TLI.getRegisterTypeForCallingConv(ReturnType->getContext(), CC, VT);

    // 'inreg' on function refers to return value
    ISD::ArgFlagsTy Flags = ISD::ArgFlagsTy();
    if (attr.hasRetAttr(Attribute::InReg))
      Flags.setInReg();

    // Propagate extension type if any
    if (attr.hasRetAttr(Attribute::SExt))
      Flags.setSExt();
    else if (attr.hasRetAttr(Attribute::ZExt))
      Flags.setZExt();

    for (unsigned i = 0; i < NumParts; ++i) {
      ISD::ArgFlagsTy OutFlags = Flags;
      if (NumParts > 1 && i == 0)
        OutFlags.setSplit();
      else if (i == NumParts - 1 && i != 0)
        OutFlags.setSplitEnd();

      Outs.push_back(
          ISD::OutputArg(OutFlags, PartVT, VT, /*isfixed=*/true, 0, 0));
    }
  }
}

/// getByValTypeAlignment - Return the desired alignment for ByVal aggregate
/// function arguments in the caller parameter area.  This is the actual
/// alignment, not its logarithm.
uint64_t TargetLoweringBase::getByValTypeAlignment(Type *Ty,
                                                   const DataLayout &DL) const {
  return DL.getABITypeAlign(Ty).value();
}

bool TargetLoweringBase::allowsMemoryAccessForAlignment(
    LLVMContext &Context, const DataLayout &DL, EVT VT, unsigned AddrSpace,
    Align Alignment, MachineMemOperand::Flags Flags, unsigned *Fast) const {
  // Check if the specified alignment is sufficient based on the data layout.
  // TODO: While using the data layout works in practice, a better solution
  // would be to implement this check directly (make this a virtual function).
  // For example, the ABI alignment may change based on software platform while
  // this function should only be affected by hardware implementation.
  Type *Ty = VT.getTypeForEVT(Context);
  if (VT.isZeroSized() || Alignment >= DL.getABITypeAlign(Ty)) {
    // Assume that an access that meets the ABI-specified alignment is fast.
    if (Fast != nullptr)
      *Fast = 1;
    return true;
  }

  // This is a misaligned access.
  return allowsMisalignedMemoryAccesses(VT, AddrSpace, Alignment, Flags, Fast);
}

bool TargetLoweringBase::allowsMemoryAccessForAlignment(
    LLVMContext &Context, const DataLayout &DL, EVT VT,
    const MachineMemOperand &MMO, unsigned *Fast) const {
  return allowsMemoryAccessForAlignment(Context, DL, VT, MMO.getAddrSpace(),
                                        MMO.getAlign(), MMO.getFlags(), Fast);
}

bool TargetLoweringBase::allowsMemoryAccess(LLVMContext &Context,
                                            const DataLayout &DL, EVT VT,
                                            unsigned AddrSpace, Align Alignment,
                                            MachineMemOperand::Flags Flags,
                                            unsigned *Fast) const {
  return allowsMemoryAccessForAlignment(Context, DL, VT, AddrSpace, Alignment,
                                        Flags, Fast);
}

bool TargetLoweringBase::allowsMemoryAccess(LLVMContext &Context,
                                            const DataLayout &DL, EVT VT,
                                            const MachineMemOperand &MMO,
                                            unsigned *Fast) const {
  return allowsMemoryAccess(Context, DL, VT, MMO.getAddrSpace(), MMO.getAlign(),
                            MMO.getFlags(), Fast);
}

bool TargetLoweringBase::allowsMemoryAccess(LLVMContext &Context,
                                            const DataLayout &DL, LLT Ty,
                                            const MachineMemOperand &MMO,
                                            unsigned *Fast) const {
  EVT VT = getApproximateEVTForLLT(Ty, DL, Context);
  return allowsMemoryAccess(Context, DL, VT, MMO.getAddrSpace(), MMO.getAlign(),
                            MMO.getFlags(), Fast);
}

//===----------------------------------------------------------------------===//
//  TargetTransformInfo Helpers
//===----------------------------------------------------------------------===//

int TargetLoweringBase::InstructionOpcodeToISD(unsigned Opcode) const {
  enum InstructionOpcodes {
#define HANDLE_INST(NUM, OPCODE, CLASS) OPCODE = NUM,
#define LAST_OTHER_INST(NUM) InstructionOpcodesCount = NUM
#include "llvm/IR/Instruction.def"
  };
  switch (static_cast<InstructionOpcodes>(Opcode)) {
  case Ret:            return 0;
  case Br:             return 0;
  case Switch:         return 0;
  case IndirectBr:     return 0;
  case Invoke:         return 0;
  case CallBr:         return 0;
  case Resume:         return 0;
  case Unreachable:    return 0;
  case CleanupRet:     return 0;
  case CatchRet:       return 0;
  case CatchPad:       return 0;
  case CatchSwitch:    return 0;
  case CleanupPad:     return 0;
  case FNeg:           return ISD::FNEG;
  case Add:            return ISD::ADD;
  case FAdd:           return ISD::FADD;
  case Sub:            return ISD::SUB;
  case FSub:           return ISD::FSUB;
  case Mul:            return ISD::MUL;
  case FMul:           return ISD::FMUL;
  case UDiv:           return ISD::UDIV;
  case SDiv:           return ISD::SDIV;
  case FDiv:           return ISD::FDIV;
  case URem:           return ISD::UREM;
  case SRem:           return ISD::SREM;
  case FRem:           return ISD::FREM;
  case Shl:            return ISD::SHL;
  case LShr:           return ISD::SRL;
  case AShr:           return ISD::SRA;
  case And:            return ISD::AND;
  case Or:             return ISD::OR;
  case Xor:            return ISD::XOR;
  case Alloca:         return 0;
  case Load:           return ISD::LOAD;
  case Store:          return ISD::STORE;
  case GetElementPtr:  return 0;
  case Fence:          return 0;
  case AtomicCmpXchg:  return 0;
  case AtomicRMW:      return 0;
  case Trunc:          return ISD::TRUNCATE;
  case ZExt:           return ISD::ZERO_EXTEND;
  case SExt:           return ISD::SIGN_EXTEND;
  case FPToUI:         return ISD::FP_TO_UINT;
  case FPToSI:         return ISD::FP_TO_SINT;
  case UIToFP:         return ISD::UINT_TO_FP;
  case SIToFP:         return ISD::SINT_TO_FP;
  case FPTrunc:        return ISD::FP_ROUND;
  case FPExt:          return ISD::FP_EXTEND;
  case PtrToInt:       return ISD::BITCAST;
  case IntToPtr:       return ISD::BITCAST;
  case BitCast:        return ISD::BITCAST;
  case AddrSpaceCast:  return ISD::ADDRSPACECAST;
  case ICmp:           return ISD::SETCC;
  case FCmp:           return ISD::SETCC;
  case PHI:            return 0;
  case Call:           return 0;
  case Select:         return ISD::SELECT;
  case UserOp1:        return 0;
  case UserOp2:        return 0;
  case VAArg:          return 0;
  case ExtractElement: return ISD::EXTRACT_VECTOR_ELT;
  case InsertElement:  return ISD::INSERT_VECTOR_ELT;
  case ShuffleVector:  return ISD::VECTOR_SHUFFLE;
  case ExtractValue:   return ISD::MERGE_VALUES;
  case InsertValue:    return ISD::MERGE_VALUES;
  case LandingPad:     return 0;
  case Freeze:         return ISD::FREEZE;
  }

  llvm_unreachable("Unknown instruction type encountered!");
}

Value *
TargetLoweringBase::getDefaultSafeStackPointerLocation(IRBuilderBase &IRB,
                                                       bool UseTLS) const {
  // compiler-rt provides a variable with a magic name.  Targets that do not
  // link with compiler-rt may also provide such a variable.
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  const char *UnsafeStackPtrVar = "__safestack_unsafe_stack_ptr";
  auto UnsafeStackPtr =
      dyn_cast_or_null<GlobalVariable>(M->getNamedValue(UnsafeStackPtrVar));

  Type *StackPtrTy = PointerType::getUnqual(M->getContext());

  if (!UnsafeStackPtr) {
    auto TLSModel = UseTLS ?
        GlobalValue::InitialExecTLSModel :
        GlobalValue::NotThreadLocal;
    // The global variable is not defined yet, define it ourselves.
    // We use the initial-exec TLS model because we do not support the
    // variable living anywhere other than in the main executable.
    UnsafeStackPtr = new GlobalVariable(
        *M, StackPtrTy, false, GlobalValue::ExternalLinkage, nullptr,
        UnsafeStackPtrVar, nullptr, TLSModel);
  } else {
    // The variable exists, check its type and attributes.
    if (UnsafeStackPtr->getValueType() != StackPtrTy)
      report_fatal_error(Twine(UnsafeStackPtrVar) + " must have void* type");
    if (UseTLS != UnsafeStackPtr->isThreadLocal())
      report_fatal_error(Twine(UnsafeStackPtrVar) + " must " +
                         (UseTLS ? "" : "not ") + "be thread-local");
  }
  return UnsafeStackPtr;
}

Value *
TargetLoweringBase::getSafeStackPointerLocation(IRBuilderBase &IRB) const {
  if (!TM.getTargetTriple().isAndroid())
    return getDefaultSafeStackPointerLocation(IRB, true);

  // Android provides a libc function to retrieve the address of the current
  // thread's unsafe stack pointer.
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  auto *PtrTy = PointerType::getUnqual(M->getContext());
  FunctionCallee Fn =
      M->getOrInsertFunction("__safestack_pointer_address", PtrTy);
  return IRB.CreateCall(Fn);
}

//===----------------------------------------------------------------------===//
//  Loop Strength Reduction hooks
//===----------------------------------------------------------------------===//

/// isLegalAddressingMode - Return true if the addressing mode represented
/// by AM is legal for this target, for a load/store of the specified type.
bool TargetLoweringBase::isLegalAddressingMode(const DataLayout &DL,
                                               const AddrMode &AM, Type *Ty,
                                               unsigned AS, Instruction *I) const {
  // The default implementation of this implements a conservative RISCy, r+r and
  // r+i addr mode.

  // Scalable offsets not supported
  if (AM.ScalableOffset)
    return false;

  // Allows a sign-extended 16-bit immediate field.
  if (AM.BaseOffs <= -(1LL << 16) || AM.BaseOffs >= (1LL << 16)-1)
    return false;

  // No global is ever allowed as a base.
  if (AM.BaseGV)
    return false;

  // Only support r+r,
  switch (AM.Scale) {
  case 0:  // "r+i" or just "i", depending on HasBaseReg.
    break;
  case 1:
    if (AM.HasBaseReg && AM.BaseOffs)  // "r+r+i" is not allowed.
      return false;
    // Otherwise we have r+r or r+i.
    break;
  case 2:
    if (AM.HasBaseReg || AM.BaseOffs)  // 2*r+r  or  2*r+i is not allowed.
      return false;
    // Allow 2*r as r+r.
    break;
  default: // Don't allow n * r
    return false;
  }

  return true;
}

//===----------------------------------------------------------------------===//
//  Stack Protector
//===----------------------------------------------------------------------===//

// For OpenBSD return its special guard variable. Otherwise return nullptr,
// so that SelectionDAG handle SSP.
Value *TargetLoweringBase::getIRStackGuard(IRBuilderBase &IRB) const {
  if (getTargetMachine().getTargetTriple().isOSOpenBSD()) {
    Module &M = *IRB.GetInsertBlock()->getParent()->getParent();
    PointerType *PtrTy = PointerType::getUnqual(M.getContext());
    Constant *C = M.getOrInsertGlobal("__guard_local", PtrTy);
    if (GlobalVariable *G = dyn_cast_or_null<GlobalVariable>(C))
      G->setVisibility(GlobalValue::HiddenVisibility);
    return C;
  }
  return nullptr;
}

// Currently only support "standard" __stack_chk_guard.
// TODO: add LOAD_STACK_GUARD support.
void TargetLoweringBase::insertSSPDeclarations(Module &M) const {
  if (!M.getNamedValue("__stack_chk_guard")) {
    auto *GV = new GlobalVariable(M, PointerType::getUnqual(M.getContext()),
                                  false, GlobalVariable::ExternalLinkage,
                                  nullptr, "__stack_chk_guard");

    // FreeBSD has "__stack_chk_guard" defined externally on libc.so
    if (M.getDirectAccessExternalData() &&
        !TM.getTargetTriple().isWindowsGNUEnvironment() &&
        !(TM.getTargetTriple().isPPC64() &&
          TM.getTargetTriple().isOSFreeBSD()) &&
        (!TM.getTargetTriple().isOSDarwin() ||
         TM.getRelocationModel() == Reloc::Static))
      GV->setDSOLocal(true);
  }
}

// Currently only support "standard" __stack_chk_guard.
// TODO: add LOAD_STACK_GUARD support.
Value *TargetLoweringBase::getSDagStackGuard(const Module &M) const {
  return M.getNamedValue("__stack_chk_guard");
}

Function *TargetLoweringBase::getSSPStackGuardCheck(const Module &M) const {
  return nullptr;
}

unsigned TargetLoweringBase::getMinimumJumpTableEntries() const {
  return MinimumJumpTableEntries;
}

void TargetLoweringBase::setMinimumJumpTableEntries(unsigned Val) {
  MinimumJumpTableEntries = Val;
}

unsigned TargetLoweringBase::getMinimumJumpTableDensity(bool OptForSize) const {
  return OptForSize ? OptsizeJumpTableDensity : JumpTableDensity;
}

unsigned TargetLoweringBase::getMaximumJumpTableSize() const {
  return MaximumJumpTableSize;
}

void TargetLoweringBase::setMaximumJumpTableSize(unsigned Val) {
  MaximumJumpTableSize = Val;
}

bool TargetLoweringBase::isJumpTableRelative() const {
  return getTargetMachine().isPositionIndependent();
}

Align TargetLoweringBase::getPrefLoopAlignment(MachineLoop *ML) const {
  if (TM.Options.LoopAlignment)
    return Align(TM.Options.LoopAlignment);
  return PrefLoopAlignment;
}

unsigned TargetLoweringBase::getMaxPermittedBytesForAlignment(
    MachineBasicBlock *MBB) const {
  return MaxBytesForAlignment;
}

//===----------------------------------------------------------------------===//
//  Reciprocal Estimates
//===----------------------------------------------------------------------===//

/// Get the reciprocal estimate attribute string for a function that will
/// override the target defaults.
static StringRef getRecipEstimateForFunc(MachineFunction &MF) {
  const Function &F = MF.getFunction();
  return F.getFnAttribute("reciprocal-estimates").getValueAsString();
}

/// Construct a string for the given reciprocal operation of the given type.
/// This string should match the corresponding option to the front-end's
/// "-mrecip" flag assuming those strings have been passed through in an
/// attribute string. For example, "vec-divf" for a division of a vXf32.
static std::string getReciprocalOpName(bool IsSqrt, EVT VT) {
  std::string Name = VT.isVector() ? "vec-" : "";

  Name += IsSqrt ? "sqrt" : "div";

  // TODO: Handle other float types?
  if (VT.getScalarType() == MVT::f64) {
    Name += "d";
  } else if (VT.getScalarType() == MVT::f16) {
    Name += "h";
  } else {
    assert(VT.getScalarType() == MVT::f32 &&
           "Unexpected FP type for reciprocal estimate");
    Name += "f";
  }

  return Name;
}

/// Return the character position and value (a single numeric character) of a
/// customized refinement operation in the input string if it exists. Return
/// false if there is no customized refinement step count.
static bool parseRefinementStep(StringRef In, size_t &Position,
                                uint8_t &Value) {
  const char RefStepToken = ':';
  Position = In.find(RefStepToken);
  if (Position == StringRef::npos)
    return false;

  StringRef RefStepString = In.substr(Position + 1);
  // Allow exactly one numeric character for the additional refinement
  // step parameter.
  if (RefStepString.size() == 1) {
    char RefStepChar = RefStepString[0];
    if (isDigit(RefStepChar)) {
      Value = RefStepChar - '0';
      return true;
    }
  }
  report_fatal_error("Invalid refinement step for -recip.");
}

/// For the input attribute string, return one of the ReciprocalEstimate enum
/// status values (enabled, disabled, or not specified) for this operation on
/// the specified data type.
static int getOpEnabled(bool IsSqrt, EVT VT, StringRef Override) {
  if (Override.empty())
    return TargetLoweringBase::ReciprocalEstimate::Unspecified;

  SmallVector<StringRef, 4> OverrideVector;
  Override.split(OverrideVector, ',');
  unsigned NumArgs = OverrideVector.size();

  // Check if "all", "none", or "default" was specified.
  if (NumArgs == 1) {
    // Look for an optional setting of the number of refinement steps needed
    // for this type of reciprocal operation.
    size_t RefPos;
    uint8_t RefSteps;
    if (parseRefinementStep(Override, RefPos, RefSteps)) {
      // Split the string for further processing.
      Override = Override.substr(0, RefPos);
    }

    // All reciprocal types are enabled.
    if (Override == "all")
      return TargetLoweringBase::ReciprocalEstimate::Enabled;

    // All reciprocal types are disabled.
    if (Override == "none")
      return TargetLoweringBase::ReciprocalEstimate::Disabled;

    // Target defaults for enablement are used.
    if (Override == "default")
      return TargetLoweringBase::ReciprocalEstimate::Unspecified;
  }

  // The attribute string may omit the size suffix ('f'/'d').
  std::string VTName = getReciprocalOpName(IsSqrt, VT);
  std::string VTNameNoSize = VTName;
  VTNameNoSize.pop_back();
  static const char DisabledPrefix = '!';

  for (StringRef RecipType : OverrideVector) {
    size_t RefPos;
    uint8_t RefSteps;
    if (parseRefinementStep(RecipType, RefPos, RefSteps))
      RecipType = RecipType.substr(0, RefPos);

    // Ignore the disablement token for string matching.
    bool IsDisabled = RecipType[0] == DisabledPrefix;
    if (IsDisabled)
      RecipType = RecipType.substr(1);

    if (RecipType == VTName || RecipType == VTNameNoSize)
      return IsDisabled ? TargetLoweringBase::ReciprocalEstimate::Disabled
                        : TargetLoweringBase::ReciprocalEstimate::Enabled;
  }

  return TargetLoweringBase::ReciprocalEstimate::Unspecified;
}

/// For the input attribute string, return the customized refinement step count
/// for this operation on the specified data type. If the step count does not
/// exist, return the ReciprocalEstimate enum value for unspecified.
static int getOpRefinementSteps(bool IsSqrt, EVT VT, StringRef Override) {
  if (Override.empty())
    return TargetLoweringBase::ReciprocalEstimate::Unspecified;

  SmallVector<StringRef, 4> OverrideVector;
  Override.split(OverrideVector, ',');
  unsigned NumArgs = OverrideVector.size();

  // Check if "all", "default", or "none" was specified.
  if (NumArgs == 1) {
    // Look for an optional setting of the number of refinement steps needed
    // for this type of reciprocal operation.
    size_t RefPos;
    uint8_t RefSteps;
    if (!parseRefinementStep(Override, RefPos, RefSteps))
      return TargetLoweringBase::ReciprocalEstimate::Unspecified;

    // Split the string for further processing.
    Override = Override.substr(0, RefPos);
    assert(Override != "none" &&
           "Disabled reciprocals, but specifed refinement steps?");

    // If this is a general override, return the specified number of steps.
    if (Override == "all" || Override == "default")
      return RefSteps;
  }

  // The attribute string may omit the size suffix ('f'/'d').
  std::string VTName = getReciprocalOpName(IsSqrt, VT);
  std::string VTNameNoSize = VTName;
  VTNameNoSize.pop_back();

  for (StringRef RecipType : OverrideVector) {
    size_t RefPos;
    uint8_t RefSteps;
    if (!parseRefinementStep(RecipType, RefPos, RefSteps))
      continue;

    RecipType = RecipType.substr(0, RefPos);
    if (RecipType == VTName || RecipType == VTNameNoSize)
      return RefSteps;
  }

  return TargetLoweringBase::ReciprocalEstimate::Unspecified;
}

int TargetLoweringBase::getRecipEstimateSqrtEnabled(EVT VT,
                                                    MachineFunction &MF) const {
  return getOpEnabled(true, VT, getRecipEstimateForFunc(MF));
}

int TargetLoweringBase::getRecipEstimateDivEnabled(EVT VT,
                                                   MachineFunction &MF) const {
  return getOpEnabled(false, VT, getRecipEstimateForFunc(MF));
}

int TargetLoweringBase::getSqrtRefinementSteps(EVT VT,
                                               MachineFunction &MF) const {
  return getOpRefinementSteps(true, VT, getRecipEstimateForFunc(MF));
}

int TargetLoweringBase::getDivRefinementSteps(EVT VT,
                                              MachineFunction &MF) const {
  return getOpRefinementSteps(false, VT, getRecipEstimateForFunc(MF));
}

bool TargetLoweringBase::isLoadBitCastBeneficial(
    EVT LoadVT, EVT BitcastVT, const SelectionDAG &DAG,
    const MachineMemOperand &MMO) const {
  // Single-element vectors are scalarized, so we should generally avoid having
  // any memory operations on such types, as they would get scalarized too.
  if (LoadVT.isFixedLengthVector() && BitcastVT.isFixedLengthVector() &&
      BitcastVT.getVectorNumElements() == 1)
    return false;

  // Don't do if we could do an indexed load on the original type, but not on
  // the new one.
  if (!LoadVT.isSimple() || !BitcastVT.isSimple())
    return true;

  MVT LoadMVT = LoadVT.getSimpleVT();

  // Don't bother doing this if it's just going to be promoted again later, as
  // doing so might interfere with other combines.
  if (getOperationAction(ISD::LOAD, LoadMVT) == Promote &&
      getTypeToPromoteTo(ISD::LOAD, LoadMVT) == BitcastVT.getSimpleVT())
    return false;

  unsigned Fast = 0;
  return allowsMemoryAccess(*DAG.getContext(), DAG.getDataLayout(), BitcastVT,
                            MMO, &Fast) &&
         Fast;
}

void TargetLoweringBase::finalizeLowering(MachineFunction &MF) const {
  MF.getRegInfo().freezeReservedRegs();
}

MachineMemOperand::Flags TargetLoweringBase::getLoadMemOperandFlags(
    const LoadInst &LI, const DataLayout &DL, AssumptionCache *AC,
    const TargetLibraryInfo *LibInfo) const {
  MachineMemOperand::Flags Flags = MachineMemOperand::MOLoad;
  if (LI.isVolatile())
    Flags |= MachineMemOperand::MOVolatile;

  if (LI.hasMetadata(LLVMContext::MD_nontemporal))
    Flags |= MachineMemOperand::MONonTemporal;

  if (LI.hasMetadata(LLVMContext::MD_invariant_load))
    Flags |= MachineMemOperand::MOInvariant;

  if (isDereferenceableAndAlignedPointer(LI.getPointerOperand(), LI.getType(),
                                         LI.getAlign(), DL, &LI, AC,
                                         /*DT=*/nullptr, LibInfo))
    Flags |= MachineMemOperand::MODereferenceable;

  Flags |= getTargetMMOFlags(LI);
  return Flags;
}

MachineMemOperand::Flags
TargetLoweringBase::getStoreMemOperandFlags(const StoreInst &SI,
                                            const DataLayout &DL) const {
  MachineMemOperand::Flags Flags = MachineMemOperand::MOStore;

  if (SI.isVolatile())
    Flags |= MachineMemOperand::MOVolatile;

  if (SI.hasMetadata(LLVMContext::MD_nontemporal))
    Flags |= MachineMemOperand::MONonTemporal;

  // FIXME: Not preserving dereferenceable
  Flags |= getTargetMMOFlags(SI);
  return Flags;
}

MachineMemOperand::Flags
TargetLoweringBase::getAtomicMemOperandFlags(const Instruction &AI,
                                             const DataLayout &DL) const {
  auto Flags = MachineMemOperand::MOLoad | MachineMemOperand::MOStore;

  if (const AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(&AI)) {
    if (RMW->isVolatile())
      Flags |= MachineMemOperand::MOVolatile;
  } else if (const AtomicCmpXchgInst *CmpX = dyn_cast<AtomicCmpXchgInst>(&AI)) {
    if (CmpX->isVolatile())
      Flags |= MachineMemOperand::MOVolatile;
  } else
    llvm_unreachable("not an atomic instruction");

  // FIXME: Not preserving dereferenceable
  Flags |= getTargetMMOFlags(AI);
  return Flags;
}

Instruction *TargetLoweringBase::emitLeadingFence(IRBuilderBase &Builder,
                                                  Instruction *Inst,
                                                  AtomicOrdering Ord) const {
  if (isReleaseOrStronger(Ord) && Inst->hasAtomicStore())
    return Builder.CreateFence(Ord);
  else
    return nullptr;
}

Instruction *TargetLoweringBase::emitTrailingFence(IRBuilderBase &Builder,
                                                   Instruction *Inst,
                                                   AtomicOrdering Ord) const {
  if (isAcquireOrStronger(Ord))
    return Builder.CreateFence(Ord);
  else
    return nullptr;
}

//===----------------------------------------------------------------------===//
//  GlobalISel Hooks
//===----------------------------------------------------------------------===//

bool TargetLoweringBase::shouldLocalize(const MachineInstr &MI,
                                        const TargetTransformInfo *TTI) const {
  auto &MF = *MI.getMF();
  auto &MRI = MF.getRegInfo();
  // Assuming a spill and reload of a value has a cost of 1 instruction each,
  // this helper function computes the maximum number of uses we should consider
  // for remat. E.g. on arm64 global addresses take 2 insts to materialize. We
  // break even in terms of code size when the original MI has 2 users vs
  // choosing to potentially spill. Any more than 2 users we we have a net code
  // size increase. This doesn't take into account register pressure though.
  auto maxUses = [](unsigned RematCost) {
    // A cost of 1 means remats are basically free.
    if (RematCost == 1)
      return std::numeric_limits<unsigned>::max();
    if (RematCost == 2)
      return 2U;

    // Remat is too expensive, only sink if there's one user.
    if (RematCost > 2)
      return 1U;
    llvm_unreachable("Unexpected remat cost");
  };

  switch (MI.getOpcode()) {
  default:
    return false;
  // Constants-like instructions should be close to their users.
  // We don't want long live-ranges for them.
  case TargetOpcode::G_CONSTANT:
  case TargetOpcode::G_FCONSTANT:
  case TargetOpcode::G_FRAME_INDEX:
  case TargetOpcode::G_INTTOPTR:
    return true;
  case TargetOpcode::G_GLOBAL_VALUE: {
    unsigned RematCost = TTI->getGISelRematGlobalCost();
    Register Reg = MI.getOperand(0).getReg();
    unsigned MaxUses = maxUses(RematCost);
    if (MaxUses == UINT_MAX)
      return true; // Remats are "free" so always localize.
    return MRI.hasAtMostUserInstrs(Reg, MaxUses);
  }
  }
}
