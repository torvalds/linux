//==- WebAssemblyMCTargetDesc.h - WebAssembly Target Descriptions -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides WebAssembly-specific target descriptions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYMCTARGETDESC_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_MCTARGETDESC_WEBASSEMBLYMCTARGETDESC_H

#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {

class MCAsmBackend;
class MCCodeEmitter;
class MCInstrInfo;
class MCObjectTargetWriter;
class Triple;

MCCodeEmitter *createWebAssemblyMCCodeEmitter(const MCInstrInfo &MCII,
                                              MCContext &Ctx);

MCAsmBackend *createWebAssemblyAsmBackend(const Triple &TT);

std::unique_ptr<MCObjectTargetWriter>
createWebAssemblyWasmObjectWriter(bool Is64Bit, bool IsEmscripten);

namespace WebAssembly {

// Exception handling / setjmp-longjmp handling command-line options
extern cl::opt<bool> WasmEnableEmEH;   // asm.js-style EH
extern cl::opt<bool> WasmEnableEmSjLj; // asm.js-style SjLJ
extern cl::opt<bool> WasmEnableEH;     // EH using Wasm EH instructions
extern cl::opt<bool> WasmEnableSjLj;   // SjLj using Wasm EH instructions
extern cl::opt<bool> WasmEnableExnref; // EH using new Wasm EH (exnref)

enum OperandType {
  /// Basic block label in a branch construct.
  OPERAND_BASIC_BLOCK = MCOI::OPERAND_FIRST_TARGET,
  /// Local index.
  OPERAND_LOCAL,
  /// Global index.
  OPERAND_GLOBAL,
  /// 32-bit integer immediates.
  OPERAND_I32IMM,
  /// 64-bit integer immediates.
  OPERAND_I64IMM,
  /// 32-bit floating-point immediates.
  OPERAND_F32IMM,
  /// 64-bit floating-point immediates.
  OPERAND_F64IMM,
  /// 8-bit vector lane immediate
  OPERAND_VEC_I8IMM,
  /// 16-bit vector lane immediate
  OPERAND_VEC_I16IMM,
  /// 32-bit vector lane immediate
  OPERAND_VEC_I32IMM,
  /// 64-bit vector lane immediate
  OPERAND_VEC_I64IMM,
  /// 32-bit unsigned function indices.
  OPERAND_FUNCTION32,
  /// 32-bit unsigned memory offsets.
  OPERAND_OFFSET32,
  /// 64-bit unsigned memory offsets.
  OPERAND_OFFSET64,
  /// p2align immediate for load and store address alignment.
  OPERAND_P2ALIGN,
  /// signature immediate for block/loop.
  OPERAND_SIGNATURE,
  /// type signature immediate for call_indirect.
  OPERAND_TYPEINDEX,
  /// Tag index.
  OPERAND_TAG,
  /// A list of branch targets for br_list.
  OPERAND_BRLIST,
  /// 32-bit unsigned table number.
  OPERAND_TABLE,
};
} // end namespace WebAssembly

namespace WebAssemblyII {

/// Target Operand Flag enum.
enum TOF {
  MO_NO_FLAG = 0,

  // On a symbol operand this indicates that the immediate is a wasm global
  // index.  The value of the wasm global will be set to the symbol address at
  // runtime.  This adds a level of indirection similar to the GOT on native
  // platforms.
  MO_GOT,

  // Same as MO_GOT but the address stored in the global is a TLS address.
  MO_GOT_TLS,

  // On a symbol operand this indicates that the immediate is the symbol
  // address relative the __memory_base wasm global.
  // Only applicable to data symbols.
  MO_MEMORY_BASE_REL,

  // On a symbol operand this indicates that the immediate is the symbol
  // address relative the __tls_base wasm global.
  // Only applicable to data symbols.
  MO_TLS_BASE_REL,

  // On a symbol operand this indicates that the immediate is the symbol
  // address relative the __table_base wasm global.
  // Only applicable to function symbols.
  MO_TABLE_BASE_REL,
};

} // end namespace WebAssemblyII

} // end namespace llvm

// Defines symbolic names for WebAssembly registers. This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "WebAssemblyGenRegisterInfo.inc"

// Defines symbolic names for the WebAssembly instructions.
//
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "WebAssemblyGenInstrInfo.inc"

namespace llvm {
namespace WebAssembly {

/// Instruction opcodes emitted via means other than CodeGen.
static const unsigned Nop = 0x01;
static const unsigned End = 0x0b;

/// Return the default p2align value for a load or store with the given opcode.
inline unsigned GetDefaultP2AlignAny(unsigned Opc) {
  switch (Opc) {
#define WASM_LOAD_STORE(NAME) \
  case WebAssembly::NAME##_A32: \
  case WebAssembly::NAME##_A64: \
  case WebAssembly::NAME##_A32_S: \
  case WebAssembly::NAME##_A64_S:
  WASM_LOAD_STORE(LOAD8_S_I32)
  WASM_LOAD_STORE(LOAD8_U_I32)
  WASM_LOAD_STORE(LOAD8_S_I64)
  WASM_LOAD_STORE(LOAD8_U_I64)
  WASM_LOAD_STORE(ATOMIC_LOAD8_U_I32)
  WASM_LOAD_STORE(ATOMIC_LOAD8_U_I64)
  WASM_LOAD_STORE(STORE8_I32)
  WASM_LOAD_STORE(STORE8_I64)
  WASM_LOAD_STORE(ATOMIC_STORE8_I32)
  WASM_LOAD_STORE(ATOMIC_STORE8_I64)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_ADD_I32)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_ADD_I64)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_SUB_I32)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_SUB_I64)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_AND_I32)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_AND_I64)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_OR_I32)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_OR_I64)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_XOR_I32)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_XOR_I64)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_XCHG_I32)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_XCHG_I64)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_CMPXCHG_I32)
  WASM_LOAD_STORE(ATOMIC_RMW8_U_CMPXCHG_I64)
  WASM_LOAD_STORE(LOAD8_SPLAT)
  WASM_LOAD_STORE(LOAD_LANE_I8x16)
  WASM_LOAD_STORE(STORE_LANE_I8x16)
  return 0;
  WASM_LOAD_STORE(LOAD16_S_I32)
  WASM_LOAD_STORE(LOAD16_U_I32)
  WASM_LOAD_STORE(LOAD16_S_I64)
  WASM_LOAD_STORE(LOAD16_U_I64)
  WASM_LOAD_STORE(ATOMIC_LOAD16_U_I32)
  WASM_LOAD_STORE(ATOMIC_LOAD16_U_I64)
  WASM_LOAD_STORE(STORE16_I32)
  WASM_LOAD_STORE(STORE16_I64)
  WASM_LOAD_STORE(ATOMIC_STORE16_I32)
  WASM_LOAD_STORE(ATOMIC_STORE16_I64)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_ADD_I32)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_ADD_I64)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_SUB_I32)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_SUB_I64)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_AND_I32)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_AND_I64)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_OR_I32)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_OR_I64)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_XOR_I32)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_XOR_I64)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_XCHG_I32)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_XCHG_I64)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_CMPXCHG_I32)
  WASM_LOAD_STORE(ATOMIC_RMW16_U_CMPXCHG_I64)
  WASM_LOAD_STORE(LOAD16_SPLAT)
  WASM_LOAD_STORE(LOAD_LANE_I16x8)
  WASM_LOAD_STORE(STORE_LANE_I16x8)
  WASM_LOAD_STORE(LOAD_F16_F32)
  WASM_LOAD_STORE(STORE_F16_F32)
  return 1;
  WASM_LOAD_STORE(LOAD_I32)
  WASM_LOAD_STORE(LOAD_F32)
  WASM_LOAD_STORE(STORE_I32)
  WASM_LOAD_STORE(STORE_F32)
  WASM_LOAD_STORE(LOAD32_S_I64)
  WASM_LOAD_STORE(LOAD32_U_I64)
  WASM_LOAD_STORE(STORE32_I64)
  WASM_LOAD_STORE(ATOMIC_LOAD_I32)
  WASM_LOAD_STORE(ATOMIC_LOAD32_U_I64)
  WASM_LOAD_STORE(ATOMIC_STORE_I32)
  WASM_LOAD_STORE(ATOMIC_STORE32_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_ADD_I32)
  WASM_LOAD_STORE(ATOMIC_RMW32_U_ADD_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_SUB_I32)
  WASM_LOAD_STORE(ATOMIC_RMW32_U_SUB_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_AND_I32)
  WASM_LOAD_STORE(ATOMIC_RMW32_U_AND_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_OR_I32)
  WASM_LOAD_STORE(ATOMIC_RMW32_U_OR_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_XOR_I32)
  WASM_LOAD_STORE(ATOMIC_RMW32_U_XOR_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_XCHG_I32)
  WASM_LOAD_STORE(ATOMIC_RMW32_U_XCHG_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_CMPXCHG_I32)
  WASM_LOAD_STORE(ATOMIC_RMW32_U_CMPXCHG_I64)
  WASM_LOAD_STORE(MEMORY_ATOMIC_NOTIFY)
  WASM_LOAD_STORE(MEMORY_ATOMIC_WAIT32)
  WASM_LOAD_STORE(LOAD32_SPLAT)
  WASM_LOAD_STORE(LOAD_ZERO_I32x4)
  WASM_LOAD_STORE(LOAD_LANE_I32x4)
  WASM_LOAD_STORE(STORE_LANE_I32x4)
  return 2;
  WASM_LOAD_STORE(LOAD_I64)
  WASM_LOAD_STORE(LOAD_F64)
  WASM_LOAD_STORE(STORE_I64)
  WASM_LOAD_STORE(STORE_F64)
  WASM_LOAD_STORE(ATOMIC_LOAD_I64)
  WASM_LOAD_STORE(ATOMIC_STORE_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_ADD_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_SUB_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_AND_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_OR_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_XOR_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_XCHG_I64)
  WASM_LOAD_STORE(ATOMIC_RMW_CMPXCHG_I64)
  WASM_LOAD_STORE(MEMORY_ATOMIC_WAIT64)
  WASM_LOAD_STORE(LOAD64_SPLAT)
  WASM_LOAD_STORE(LOAD_EXTEND_S_I16x8)
  WASM_LOAD_STORE(LOAD_EXTEND_U_I16x8)
  WASM_LOAD_STORE(LOAD_EXTEND_S_I32x4)
  WASM_LOAD_STORE(LOAD_EXTEND_U_I32x4)
  WASM_LOAD_STORE(LOAD_EXTEND_S_I64x2)
  WASM_LOAD_STORE(LOAD_EXTEND_U_I64x2)
  WASM_LOAD_STORE(LOAD_ZERO_I64x2)
  WASM_LOAD_STORE(LOAD_LANE_I64x2)
  WASM_LOAD_STORE(STORE_LANE_I64x2)
  return 3;
  WASM_LOAD_STORE(LOAD_V128)
  WASM_LOAD_STORE(STORE_V128)
    return 4;
  default:
    return -1;
  }
#undef WASM_LOAD_STORE
}

inline unsigned GetDefaultP2Align(unsigned Opc) {
  auto Align = GetDefaultP2AlignAny(Opc);
  if (Align == -1U) {
    llvm_unreachable("Only loads and stores have p2align values");
  }
  return Align;
}

inline bool isConst(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::CONST_I32:
  case WebAssembly::CONST_I32_S:
  case WebAssembly::CONST_I64:
  case WebAssembly::CONST_I64_S:
  case WebAssembly::CONST_F32:
  case WebAssembly::CONST_F32_S:
  case WebAssembly::CONST_F64:
  case WebAssembly::CONST_F64_S:
  case WebAssembly::CONST_V128_I8x16:
  case WebAssembly::CONST_V128_I8x16_S:
  case WebAssembly::CONST_V128_I16x8:
  case WebAssembly::CONST_V128_I16x8_S:
  case WebAssembly::CONST_V128_I32x4:
  case WebAssembly::CONST_V128_I32x4_S:
  case WebAssembly::CONST_V128_I64x2:
  case WebAssembly::CONST_V128_I64x2_S:
  case WebAssembly::CONST_V128_F32x4:
  case WebAssembly::CONST_V128_F32x4_S:
  case WebAssembly::CONST_V128_F64x2:
  case WebAssembly::CONST_V128_F64x2_S:
    return true;
  default:
    return false;
  }
}

inline bool isScalarConst(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::CONST_I32:
  case WebAssembly::CONST_I32_S:
  case WebAssembly::CONST_I64:
  case WebAssembly::CONST_I64_S:
  case WebAssembly::CONST_F32:
  case WebAssembly::CONST_F32_S:
  case WebAssembly::CONST_F64:
  case WebAssembly::CONST_F64_S:
    return true;
  default:
    return false;
  }
}

inline bool isArgument(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::ARGUMENT_i32:
  case WebAssembly::ARGUMENT_i32_S:
  case WebAssembly::ARGUMENT_i64:
  case WebAssembly::ARGUMENT_i64_S:
  case WebAssembly::ARGUMENT_f32:
  case WebAssembly::ARGUMENT_f32_S:
  case WebAssembly::ARGUMENT_f64:
  case WebAssembly::ARGUMENT_f64_S:
  case WebAssembly::ARGUMENT_v16i8:
  case WebAssembly::ARGUMENT_v16i8_S:
  case WebAssembly::ARGUMENT_v8i16:
  case WebAssembly::ARGUMENT_v8i16_S:
  case WebAssembly::ARGUMENT_v4i32:
  case WebAssembly::ARGUMENT_v4i32_S:
  case WebAssembly::ARGUMENT_v2i64:
  case WebAssembly::ARGUMENT_v2i64_S:
  case WebAssembly::ARGUMENT_v8f16:
  case WebAssembly::ARGUMENT_v8f16_S:
  case WebAssembly::ARGUMENT_v4f32:
  case WebAssembly::ARGUMENT_v4f32_S:
  case WebAssembly::ARGUMENT_v2f64:
  case WebAssembly::ARGUMENT_v2f64_S:
  case WebAssembly::ARGUMENT_funcref:
  case WebAssembly::ARGUMENT_funcref_S:
  case WebAssembly::ARGUMENT_externref:
  case WebAssembly::ARGUMENT_externref_S:
  case WebAssembly::ARGUMENT_exnref:
  case WebAssembly::ARGUMENT_exnref_S:
    return true;
  default:
    return false;
  }
}

inline bool isCopy(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::COPY_I32:
  case WebAssembly::COPY_I32_S:
  case WebAssembly::COPY_I64:
  case WebAssembly::COPY_I64_S:
  case WebAssembly::COPY_F32:
  case WebAssembly::COPY_F32_S:
  case WebAssembly::COPY_F64:
  case WebAssembly::COPY_F64_S:
  case WebAssembly::COPY_V128:
  case WebAssembly::COPY_V128_S:
  case WebAssembly::COPY_FUNCREF:
  case WebAssembly::COPY_FUNCREF_S:
  case WebAssembly::COPY_EXTERNREF:
  case WebAssembly::COPY_EXTERNREF_S:
  case WebAssembly::COPY_EXNREF:
  case WebAssembly::COPY_EXNREF_S:
    return true;
  default:
    return false;
  }
}

inline bool isTee(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::TEE_I32:
  case WebAssembly::TEE_I32_S:
  case WebAssembly::TEE_I64:
  case WebAssembly::TEE_I64_S:
  case WebAssembly::TEE_F32:
  case WebAssembly::TEE_F32_S:
  case WebAssembly::TEE_F64:
  case WebAssembly::TEE_F64_S:
  case WebAssembly::TEE_V128:
  case WebAssembly::TEE_V128_S:
  case WebAssembly::TEE_FUNCREF:
  case WebAssembly::TEE_FUNCREF_S:
  case WebAssembly::TEE_EXTERNREF:
  case WebAssembly::TEE_EXTERNREF_S:
  case WebAssembly::TEE_EXNREF:
  case WebAssembly::TEE_EXNREF_S:
    return true;
  default:
    return false;
  }
}

inline bool isCallDirect(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::CALL:
  case WebAssembly::CALL_S:
  case WebAssembly::RET_CALL:
  case WebAssembly::RET_CALL_S:
    return true;
  default:
    return false;
  }
}

inline bool isCallIndirect(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::CALL_INDIRECT:
  case WebAssembly::CALL_INDIRECT_S:
  case WebAssembly::RET_CALL_INDIRECT:
  case WebAssembly::RET_CALL_INDIRECT_S:
    return true;
  default:
    return false;
  }
}

inline bool isBrTable(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::BR_TABLE_I32:
  case WebAssembly::BR_TABLE_I32_S:
  case WebAssembly::BR_TABLE_I64:
  case WebAssembly::BR_TABLE_I64_S:
    return true;
  default:
    return false;
  }
}

inline bool isMarker(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::BLOCK:
  case WebAssembly::BLOCK_S:
  case WebAssembly::END_BLOCK:
  case WebAssembly::END_BLOCK_S:
  case WebAssembly::LOOP:
  case WebAssembly::LOOP_S:
  case WebAssembly::END_LOOP:
  case WebAssembly::END_LOOP_S:
  case WebAssembly::TRY:
  case WebAssembly::TRY_S:
  case WebAssembly::END_TRY:
  case WebAssembly::END_TRY_S:
    return true;
  default:
    return false;
  }
}

inline bool isCatch(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::CATCH:
  case WebAssembly::CATCH_S:
  case WebAssembly::CATCH_ALL:
  case WebAssembly::CATCH_ALL_S:
    return true;
  default:
    return false;
  }
}

inline bool isLocalGet(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::LOCAL_GET_I32:
  case WebAssembly::LOCAL_GET_I32_S:
  case WebAssembly::LOCAL_GET_I64:
  case WebAssembly::LOCAL_GET_I64_S:
  case WebAssembly::LOCAL_GET_F32:
  case WebAssembly::LOCAL_GET_F32_S:
  case WebAssembly::LOCAL_GET_F64:
  case WebAssembly::LOCAL_GET_F64_S:
  case WebAssembly::LOCAL_GET_V128:
  case WebAssembly::LOCAL_GET_V128_S:
  case WebAssembly::LOCAL_GET_FUNCREF:
  case WebAssembly::LOCAL_GET_FUNCREF_S:
  case WebAssembly::LOCAL_GET_EXTERNREF:
  case WebAssembly::LOCAL_GET_EXTERNREF_S:
  case WebAssembly::LOCAL_GET_EXNREF:
  case WebAssembly::LOCAL_GET_EXNREF_S:
    return true;
  default:
    return false;
  }
}

inline bool isLocalSet(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::LOCAL_SET_I32:
  case WebAssembly::LOCAL_SET_I32_S:
  case WebAssembly::LOCAL_SET_I64:
  case WebAssembly::LOCAL_SET_I64_S:
  case WebAssembly::LOCAL_SET_F32:
  case WebAssembly::LOCAL_SET_F32_S:
  case WebAssembly::LOCAL_SET_F64:
  case WebAssembly::LOCAL_SET_F64_S:
  case WebAssembly::LOCAL_SET_V128:
  case WebAssembly::LOCAL_SET_V128_S:
  case WebAssembly::LOCAL_SET_FUNCREF:
  case WebAssembly::LOCAL_SET_FUNCREF_S:
  case WebAssembly::LOCAL_SET_EXTERNREF:
  case WebAssembly::LOCAL_SET_EXTERNREF_S:
  case WebAssembly::LOCAL_SET_EXNREF:
  case WebAssembly::LOCAL_SET_EXNREF_S:
    return true;
  default:
    return false;
  }
}

inline bool isLocalTee(unsigned Opc) {
  switch (Opc) {
  case WebAssembly::LOCAL_TEE_I32:
  case WebAssembly::LOCAL_TEE_I32_S:
  case WebAssembly::LOCAL_TEE_I64:
  case WebAssembly::LOCAL_TEE_I64_S:
  case WebAssembly::LOCAL_TEE_F32:
  case WebAssembly::LOCAL_TEE_F32_S:
  case WebAssembly::LOCAL_TEE_F64:
  case WebAssembly::LOCAL_TEE_F64_S:
  case WebAssembly::LOCAL_TEE_V128:
  case WebAssembly::LOCAL_TEE_V128_S:
  case WebAssembly::LOCAL_TEE_FUNCREF:
  case WebAssembly::LOCAL_TEE_FUNCREF_S:
  case WebAssembly::LOCAL_TEE_EXTERNREF:
  case WebAssembly::LOCAL_TEE_EXTERNREF_S:
  case WebAssembly::LOCAL_TEE_EXNREF:
  case WebAssembly::LOCAL_TEE_EXNREF_S:
    return true;
  default:
    return false;
  }
}

static const unsigned UnusedReg = -1u;

// For a given stackified WAReg, return the id number to print with push/pop.
unsigned inline getWARegStackId(unsigned Reg) {
  assert(Reg & INT32_MIN);
  return Reg & INT32_MAX;
}

} // end namespace WebAssembly
} // end namespace llvm

#define GET_SUBTARGETINFO_ENUM
#include "WebAssemblyGenSubtargetInfo.inc"

#endif
