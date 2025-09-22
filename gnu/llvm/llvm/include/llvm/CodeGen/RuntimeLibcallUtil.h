//===-- CodeGen/RuntimeLibcallUtil.h - Runtime Library Calls ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some helper functions for runtime library calls.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_RUNTIMELIBCALLS_H
#define LLVM_CODEGEN_RUNTIMELIBCALLS_H

#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/Support/AtomicOrdering.h"

namespace llvm {
namespace RTLIB {

/// GetFPLibCall - Helper to return the right libcall for the given floating
/// point type, or UNKNOWN_LIBCALL if there is none.
Libcall getFPLibCall(EVT VT, Libcall Call_F32, Libcall Call_F64,
                     Libcall Call_F80, Libcall Call_F128, Libcall Call_PPCF128);

/// getFPEXT - Return the FPEXT_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
Libcall getFPEXT(EVT OpVT, EVT RetVT);

/// getFPROUND - Return the FPROUND_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
Libcall getFPROUND(EVT OpVT, EVT RetVT);

/// getFPTOSINT - Return the FPTOSINT_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
Libcall getFPTOSINT(EVT OpVT, EVT RetVT);

/// getFPTOUINT - Return the FPTOUINT_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
Libcall getFPTOUINT(EVT OpVT, EVT RetVT);

/// getSINTTOFP - Return the SINTTOFP_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
Libcall getSINTTOFP(EVT OpVT, EVT RetVT);

/// getUINTTOFP - Return the UINTTOFP_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
Libcall getUINTTOFP(EVT OpVT, EVT RetVT);

/// getPOWI - Return the POWI_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
Libcall getPOWI(EVT RetVT);

/// getLDEXP - Return the LDEXP_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
Libcall getLDEXP(EVT RetVT);

/// getFREXP - Return the FREXP_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
Libcall getFREXP(EVT RetVT);

/// Return the SYNC_FETCH_AND_* value for the given opcode and type, or
/// UNKNOWN_LIBCALL if there is none.
Libcall getSYNC(unsigned Opc, MVT VT);

/// Return the outline atomics value for the given atomic ordering, access
/// size and set of libcalls for a given atomic, or UNKNOWN_LIBCALL if there
/// is none.
Libcall getOutlineAtomicHelper(const Libcall (&LC)[5][4], AtomicOrdering Order,
                               uint64_t MemSize);

/// Return the outline atomics value for the given opcode, atomic ordering
/// and type, or UNKNOWN_LIBCALL if there is none.
Libcall getOUTLINE_ATOMIC(unsigned Opc, AtomicOrdering Order, MVT VT);

/// getMEMCPY_ELEMENT_UNORDERED_ATOMIC - Return
/// MEMCPY_ELEMENT_UNORDERED_ATOMIC_* value for the given element size or
/// UNKNOW_LIBCALL if there is none.
Libcall getMEMCPY_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize);

/// getMEMMOVE_ELEMENT_UNORDERED_ATOMIC - Return
/// MEMMOVE_ELEMENT_UNORDERED_ATOMIC_* value for the given element size or
/// UNKNOW_LIBCALL if there is none.
Libcall getMEMMOVE_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize);

/// getMEMSET_ELEMENT_UNORDERED_ATOMIC - Return
/// MEMSET_ELEMENT_UNORDERED_ATOMIC_* value for the given element size or
/// UNKNOW_LIBCALL if there is none.
Libcall getMEMSET_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize);

/// Initialize the default condition code on the libcalls.
void initCmpLibcallCCs(ISD::CondCode *CmpLibcallCCs);

} // namespace RTLIB
} // namespace llvm

#endif
