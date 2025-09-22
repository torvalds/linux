//===-- AArch64PointerAuth.h -- Harden code using PAuth ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64POINTERAUTH_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64POINTERAUTH_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {
namespace AArch64PAuth {

/// Variants of check performed on an authenticated pointer.
///
/// In cases such as authenticating the LR value when performing a tail call
/// or when re-signing a signed pointer with a different signing schema,
/// a failed authentication may not generate an exception on its own and may
/// create an authentication or signing oracle if not checked explicitly.
///
/// A number of check methods modify control flow in a similar way by
/// rewriting the code
///
/// ```
///   <authenticate LR>
///   <more instructions>
/// ```
///
/// as follows:
///
/// ```
///   <authenticate LR>
///   <method-specific checker>
/// ret_block:
///   <more instructions>
///   ...
///
/// break_block:
///   brk <code>
/// ```
enum class AuthCheckMethod {
  /// Do not check the value at all
  None,
  /// Perform a load to a temporary register
  DummyLoad,
  /// Check by comparing bits 62 and 61 of the authenticated address.
  ///
  /// This method modifies control flow and inserts the following checker:
  ///
  /// ```
  ///   eor Xtmp, Xn, Xn, lsl #1
  ///   tbnz Xtmp, #62, break_block
  /// ```
  HighBitsNoTBI,
  /// Check by comparing the authenticated value with an XPAC-ed one without
  /// using PAuth instructions not encoded as HINT. Can only be applied to LR.
  ///
  /// This method modifies control flow and inserts the following checker:
  ///
  /// ```
  ///   mov Xtmp, LR
  ///   xpaclri           ; encoded as "hint #7"
  ///   ; Note: at this point, the LR register contains the address as if
  ///   ; the authentication succeeded and the temporary register contains the
  ///   ; *real* result of authentication.
  ///   cmp Xtmp, LR
  ///   b.ne break_block
  /// ```
  XPACHint,
};

#define AUTH_CHECK_METHOD_CL_VALUES_COMMON                                     \
      clEnumValN(AArch64PAuth::AuthCheckMethod::None, "none",                  \
                 "Do not check authenticated address"),                        \
      clEnumValN(AArch64PAuth::AuthCheckMethod::DummyLoad, "load",             \
                 "Perform dummy load from authenticated address"),             \
      clEnumValN(AArch64PAuth::AuthCheckMethod::HighBitsNoTBI,                 \
                 "high-bits-notbi",                                            \
                 "Compare bits 62 and 61 of address (TBI should be disabled)")

#define AUTH_CHECK_METHOD_CL_VALUES_LR                                         \
      AUTH_CHECK_METHOD_CL_VALUES_COMMON,                                      \
      clEnumValN(AArch64PAuth::AuthCheckMethod::XPACHint, "xpac-hint",         \
                 "Compare with the result of XPACLRI")

/// Explicitly checks that pointer authentication succeeded.
///
/// Assuming AuthenticatedReg contains a value returned by one of the AUT*
/// instructions, check the value using Method just before the instruction
/// pointed to by MBBI. If the check succeeds, execution proceeds to the
/// instruction pointed to by MBBI, otherwise a CPU exception is generated.
///
/// Some of the methods may need to know if the pointer was authenticated
/// using an I-key or D-key and which register can be used as temporary.
/// If an explicit BRK instruction is used to generate an exception, BrkImm
/// specifies its immediate operand.
void checkAuthenticatedRegister(MachineBasicBlock::iterator MBBI,
                                AuthCheckMethod Method,
                                Register AuthenticatedReg, Register TmpReg,
                                bool UseIKey, unsigned BrkImm);

/// Returns the number of bytes added by checkAuthenticatedRegister.
unsigned getCheckerSizeInBytes(AuthCheckMethod Method);

} // end namespace AArch64PAuth
} // end namespace llvm

#endif
