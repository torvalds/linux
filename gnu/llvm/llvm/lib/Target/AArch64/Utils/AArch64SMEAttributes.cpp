//===-- AArch64SMEAttributes.cpp - Helper for interpreting SME attributes -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AArch64SMEAttributes.h"
#include "llvm/IR/InstrTypes.h"
#include <cassert>

using namespace llvm;

void SMEAttrs::set(unsigned M, bool Enable) {
  if (Enable)
    Bitmask |= M;
  else
    Bitmask &= ~M;

  // Streaming Mode Attrs
  assert(!(hasStreamingInterface() && hasStreamingCompatibleInterface()) &&
         "SM_Enabled and SM_Compatible are mutually exclusive");

  // ZA Attrs
  assert(!(isNewZA() && (Bitmask & SME_ABI_Routine)) &&
         "ZA_New and SME_ABI_Routine are mutually exclusive");

  assert(
      (!sharesZA() ||
       (isNewZA() ^ isInZA() ^ isInOutZA() ^ isOutZA() ^ isPreservesZA())) &&
      "Attributes 'aarch64_new_za', 'aarch64_in_za', 'aarch64_out_za', "
      "'aarch64_inout_za' and 'aarch64_preserves_za' are mutually exclusive");

  // ZT0 Attrs
  assert(
      (!sharesZT0() || (isNewZT0() ^ isInZT0() ^ isInOutZT0() ^ isOutZT0() ^
                        isPreservesZT0())) &&
      "Attributes 'aarch64_new_zt0', 'aarch64_in_zt0', 'aarch64_out_zt0', "
      "'aarch64_inout_zt0' and 'aarch64_preserves_zt0' are mutually exclusive");
}

SMEAttrs::SMEAttrs(const CallBase &CB) {
  *this = SMEAttrs(CB.getAttributes());
  if (auto *F = CB.getCalledFunction()) {
    set(SMEAttrs(*F).Bitmask | SMEAttrs(F->getName()).Bitmask);
  }
}

SMEAttrs::SMEAttrs(StringRef FuncName) : Bitmask(0) {
  if (FuncName == "__arm_tpidr2_save" || FuncName == "__arm_sme_state")
    Bitmask |= (SMEAttrs::SM_Compatible | SMEAttrs::SME_ABI_Routine);
  if (FuncName == "__arm_tpidr2_restore")
    Bitmask |= SMEAttrs::SM_Compatible | encodeZAState(StateValue::In) |
               SMEAttrs::SME_ABI_Routine;
  if (FuncName == "__arm_sc_memcpy" || FuncName == "__arm_sc_memset" ||
      FuncName == "__arm_sc_memmove" || FuncName == "__arm_sc_memchr")
    Bitmask |= SMEAttrs::SM_Compatible;
}

SMEAttrs::SMEAttrs(const AttributeList &Attrs) {
  Bitmask = 0;
  if (Attrs.hasFnAttr("aarch64_pstate_sm_enabled"))
    Bitmask |= SM_Enabled;
  if (Attrs.hasFnAttr("aarch64_pstate_sm_compatible"))
    Bitmask |= SM_Compatible;
  if (Attrs.hasFnAttr("aarch64_pstate_sm_body"))
    Bitmask |= SM_Body;
  if (Attrs.hasFnAttr("aarch64_in_za"))
    Bitmask |= encodeZAState(StateValue::In);
  if (Attrs.hasFnAttr("aarch64_out_za"))
    Bitmask |= encodeZAState(StateValue::Out);
  if (Attrs.hasFnAttr("aarch64_inout_za"))
    Bitmask |= encodeZAState(StateValue::InOut);
  if (Attrs.hasFnAttr("aarch64_preserves_za"))
    Bitmask |= encodeZAState(StateValue::Preserved);
  if (Attrs.hasFnAttr("aarch64_new_za"))
    Bitmask |= encodeZAState(StateValue::New);
  if (Attrs.hasFnAttr("aarch64_in_zt0"))
    Bitmask |= encodeZT0State(StateValue::In);
  if (Attrs.hasFnAttr("aarch64_out_zt0"))
    Bitmask |= encodeZT0State(StateValue::Out);
  if (Attrs.hasFnAttr("aarch64_inout_zt0"))
    Bitmask |= encodeZT0State(StateValue::InOut);
  if (Attrs.hasFnAttr("aarch64_preserves_zt0"))
    Bitmask |= encodeZT0State(StateValue::Preserved);
  if (Attrs.hasFnAttr("aarch64_new_zt0"))
    Bitmask |= encodeZT0State(StateValue::New);
}

bool SMEAttrs::requiresSMChange(const SMEAttrs &Callee) const {
  if (Callee.hasStreamingCompatibleInterface())
    return false;

  // Both non-streaming
  if (hasNonStreamingInterfaceAndBody() && Callee.hasNonStreamingInterface())
    return false;

  // Both streaming
  if (hasStreamingInterfaceOrBody() && Callee.hasStreamingInterface())
    return false;

  return true;
}
