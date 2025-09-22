//===-- AArch64SMEAttributes.h - Helper for interpreting SME attributes -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_UTILS_AARCH64SMEATTRIBUTES_H
#define LLVM_LIB_TARGET_AARCH64_UTILS_AARCH64SMEATTRIBUTES_H

#include "llvm/IR/Function.h"

namespace llvm {

class Function;
class CallBase;
class AttributeList;

/// SMEAttrs is a utility class to parse the SME ACLE attributes on functions.
/// It helps determine a function's requirements for PSTATE.ZA and PSTATE.SM. It
/// has interfaces to query whether a streaming mode change or lazy-save
/// mechanism is required when going from one function to another (e.g. through
/// a call).
class SMEAttrs {
  unsigned Bitmask;

public:
  enum class StateValue {
    None = 0,
    In = 1,        // aarch64_in_zt0
    Out = 2,       // aarch64_out_zt0
    InOut = 3,     // aarch64_inout_zt0
    Preserved = 4, // aarch64_preserves_zt0
    New = 5        // aarch64_new_zt0
  };

  // Enum with bitmasks for each individual SME feature.
  enum Mask {
    Normal = 0,
    SM_Enabled = 1 << 0,      // aarch64_pstate_sm_enabled
    SM_Compatible = 1 << 1,   // aarch64_pstate_sm_compatible
    SM_Body = 1 << 2,         // aarch64_pstate_sm_body
    SME_ABI_Routine = 1 << 3, // Used for SME ABI routines to avoid lazy saves
    ZA_Shift = 4,
    ZA_Mask = 0b111 << ZA_Shift,
    ZT0_Shift = 7,
    ZT0_Mask = 0b111 << ZT0_Shift
  };

  SMEAttrs(unsigned Mask = Normal) : Bitmask(0) { set(Mask); }
  SMEAttrs(const Function &F) : SMEAttrs(F.getAttributes()) {}
  SMEAttrs(const CallBase &CB);
  SMEAttrs(const AttributeList &L);
  SMEAttrs(StringRef FuncName);

  void set(unsigned M, bool Enable = true);

  // Interfaces to query PSTATE.SM
  bool hasStreamingBody() const { return Bitmask & SM_Body; }
  bool hasStreamingInterface() const { return Bitmask & SM_Enabled; }
  bool hasStreamingInterfaceOrBody() const {
    return hasStreamingBody() || hasStreamingInterface();
  }
  bool hasStreamingCompatibleInterface() const {
    return Bitmask & SM_Compatible;
  }
  bool hasNonStreamingInterface() const {
    return !hasStreamingInterface() && !hasStreamingCompatibleInterface();
  }
  bool hasNonStreamingInterfaceAndBody() const {
    return hasNonStreamingInterface() && !hasStreamingBody();
  }

  /// \return true if a call from Caller -> Callee requires a change in
  /// streaming mode.
  bool requiresSMChange(const SMEAttrs &Callee) const;

  // Interfaces to query ZA
  static StateValue decodeZAState(unsigned Bitmask) {
    return static_cast<StateValue>((Bitmask & ZA_Mask) >> ZA_Shift);
  }
  static unsigned encodeZAState(StateValue S) {
    return static_cast<unsigned>(S) << ZA_Shift;
  }

  bool isNewZA() const { return decodeZAState(Bitmask) == StateValue::New; }
  bool isInZA() const { return decodeZAState(Bitmask) == StateValue::In; }
  bool isOutZA() const { return decodeZAState(Bitmask) == StateValue::Out; }
  bool isInOutZA() const { return decodeZAState(Bitmask) == StateValue::InOut; }
  bool isPreservesZA() const {
    return decodeZAState(Bitmask) == StateValue::Preserved;
  }
  bool sharesZA() const {
    StateValue State = decodeZAState(Bitmask);
    return State == StateValue::In || State == StateValue::Out ||
           State == StateValue::InOut || State == StateValue::Preserved;
  }
  bool hasSharedZAInterface() const { return sharesZA() || sharesZT0(); }
  bool hasPrivateZAInterface() const { return !hasSharedZAInterface(); }
  bool hasZAState() const { return isNewZA() || sharesZA(); }
  bool requiresLazySave(const SMEAttrs &Callee) const {
    return hasZAState() && Callee.hasPrivateZAInterface() &&
           !(Callee.Bitmask & SME_ABI_Routine);
  }

  // Interfaces to query ZT0 State
  static StateValue decodeZT0State(unsigned Bitmask) {
    return static_cast<StateValue>((Bitmask & ZT0_Mask) >> ZT0_Shift);
  }
  static unsigned encodeZT0State(StateValue S) {
    return static_cast<unsigned>(S) << ZT0_Shift;
  }

  bool isNewZT0() const { return decodeZT0State(Bitmask) == StateValue::New; }
  bool isInZT0() const { return decodeZT0State(Bitmask) == StateValue::In; }
  bool isOutZT0() const { return decodeZT0State(Bitmask) == StateValue::Out; }
  bool isInOutZT0() const {
    return decodeZT0State(Bitmask) == StateValue::InOut;
  }
  bool isPreservesZT0() const {
    return decodeZT0State(Bitmask) == StateValue::Preserved;
  }
  bool sharesZT0() const {
    StateValue State = decodeZT0State(Bitmask);
    return State == StateValue::In || State == StateValue::Out ||
           State == StateValue::InOut || State == StateValue::Preserved;
  }
  bool hasZT0State() const { return isNewZT0() || sharesZT0(); }
  bool requiresPreservingZT0(const SMEAttrs &Callee) const {
    return hasZT0State() && !Callee.sharesZT0();
  }
  bool requiresDisablingZABeforeCall(const SMEAttrs &Callee) const {
    return hasZT0State() && !hasZAState() && Callee.hasPrivateZAInterface() &&
           !(Callee.Bitmask & SME_ABI_Routine);
  }
  bool requiresEnablingZAAfterCall(const SMEAttrs &Callee) const {
    return requiresLazySave(Callee) || requiresDisablingZABeforeCall(Callee);
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_AARCH64_UTILS_AARCH64SMEATTRIBUTES_H
