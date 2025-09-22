//===-- SystemZCallingConv.h - Calling conventions for SystemZ --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZCALLINGCONV_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZCALLINGCONV_H

#include "SystemZSubtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/MC/MCRegisterInfo.h"

namespace llvm {
namespace SystemZ {
  const unsigned ELFNumArgGPRs = 5;
  extern const MCPhysReg ELFArgGPRs[ELFNumArgGPRs];

  const unsigned ELFNumArgFPRs = 4;
  extern const MCPhysReg ELFArgFPRs[ELFNumArgFPRs];

  const unsigned XPLINK64NumArgGPRs = 3;
  extern const MCPhysReg XPLINK64ArgGPRs[XPLINK64NumArgGPRs];

  const unsigned XPLINK64NumArgFPRs = 4;
  extern const MCPhysReg XPLINK64ArgFPRs[XPLINK64NumArgFPRs];
} // end namespace SystemZ

class SystemZCCState : public CCState {
private:
  /// Records whether the value was a fixed argument.
  /// See ISD::OutputArg::IsFixed.
  SmallVector<bool, 4> ArgIsFixed;

  /// Records whether the value was widened from a short vector type.
  SmallVector<bool, 4> ArgIsShortVector;

  // Check whether ArgVT is a short vector type.
  bool IsShortVectorType(EVT ArgVT) {
    return ArgVT.isVector() && ArgVT.getStoreSize() <= 8;
  }

public:
  SystemZCCState(CallingConv::ID CC, bool isVarArg, MachineFunction &MF,
                 SmallVectorImpl<CCValAssign> &locs, LLVMContext &C)
      : CCState(CC, isVarArg, MF, locs, C) {}

  void AnalyzeFormalArguments(const SmallVectorImpl<ISD::InputArg> &Ins,
                              CCAssignFn Fn) {
    // Formal arguments are always fixed.
    ArgIsFixed.clear();
    for (unsigned i = 0; i < Ins.size(); ++i)
      ArgIsFixed.push_back(true);
    // Record whether the call operand was a short vector.
    ArgIsShortVector.clear();
    for (unsigned i = 0; i < Ins.size(); ++i)
      ArgIsShortVector.push_back(IsShortVectorType(Ins[i].ArgVT));

    CCState::AnalyzeFormalArguments(Ins, Fn);
  }

  void AnalyzeCallOperands(const SmallVectorImpl<ISD::OutputArg> &Outs,
                           CCAssignFn Fn) {
    // Record whether the call operand was a fixed argument.
    ArgIsFixed.clear();
    for (unsigned i = 0; i < Outs.size(); ++i)
      ArgIsFixed.push_back(Outs[i].IsFixed);
    // Record whether the call operand was a short vector.
    ArgIsShortVector.clear();
    for (unsigned i = 0; i < Outs.size(); ++i)
      ArgIsShortVector.push_back(IsShortVectorType(Outs[i].ArgVT));

    CCState::AnalyzeCallOperands(Outs, Fn);
  }

  // This version of AnalyzeCallOperands in the base class is not usable
  // since we must provide a means of accessing ISD::OutputArg::IsFixed.
  void AnalyzeCallOperands(const SmallVectorImpl<MVT> &Outs,
                           SmallVectorImpl<ISD::ArgFlagsTy> &Flags,
                           CCAssignFn Fn) = delete;

  bool IsFixed(unsigned ValNo) { return ArgIsFixed[ValNo]; }
  bool IsShortVector(unsigned ValNo) { return ArgIsShortVector[ValNo]; }
};

// Handle i128 argument types.  These need to be passed by implicit
// reference.  This could be as simple as the following .td line:
//    CCIfType<[i128], CCPassIndirect<i64>>,
// except that i128 is not a legal type, and therefore gets split by
// common code into a pair of i64 arguments.
inline bool CC_SystemZ_I128Indirect(unsigned &ValNo, MVT &ValVT,
                                    MVT &LocVT,
                                    CCValAssign::LocInfo &LocInfo,
                                    ISD::ArgFlagsTy &ArgFlags,
                                    CCState &State) {
  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // ArgFlags.isSplit() is true on the first part of a i128 argument;
  // PendingMembers.empty() is false on all subsequent parts.
  if (!ArgFlags.isSplit() && PendingMembers.empty())
    return false;

  // Push a pending Indirect value location for each part.
  LocVT = MVT::i64;
  LocInfo = CCValAssign::Indirect;
  PendingMembers.push_back(CCValAssign::getPending(ValNo, ValVT,
                                                   LocVT, LocInfo));
  if (!ArgFlags.isSplitEnd())
    return true;

  // OK, we've collected all parts in the pending list.  Allocate
  // the location (register or stack slot) for the indirect pointer.
  // (This duplicates the usual i64 calling convention rules.)
  unsigned Reg;
  const SystemZSubtarget &Subtarget =
      State.getMachineFunction().getSubtarget<SystemZSubtarget>();
  if (Subtarget.isTargetELF())
    Reg = State.AllocateReg(SystemZ::ELFArgGPRs);
  else if (Subtarget.isTargetXPLINK64())
    Reg = State.AllocateReg(SystemZ::XPLINK64ArgGPRs);
  else
    llvm_unreachable("Unknown Calling Convention!");

  unsigned Offset = Reg && !Subtarget.isTargetXPLINK64()
                        ? 0
                        : State.AllocateStack(8, Align(8));

  // Use that same location for all the pending parts.
  for (auto &It : PendingMembers) {
    if (Reg)
      It.convertToReg(Reg);
    else
      It.convertToMem(Offset);
    State.addLoc(It);
  }

  PendingMembers.clear();

  return true;
}

inline bool CC_XPLINK64_Shadow_Reg(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                   CCValAssign::LocInfo &LocInfo,
                                   ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  if (LocVT == MVT::f32 || LocVT == MVT::f64) {
    State.AllocateReg(SystemZ::XPLINK64ArgGPRs);
  }
  if (LocVT == MVT::f128 || LocVT.is128BitVector()) {
    // Shadow next two GPRs, if available.
    State.AllocateReg(SystemZ::XPLINK64ArgGPRs);
    State.AllocateReg(SystemZ::XPLINK64ArgGPRs);

    // Quad precision floating point needs to
    // go inside pre-defined FPR pair.
    if (LocVT == MVT::f128) {
      for (unsigned I = 0; I < SystemZ::XPLINK64NumArgFPRs; I += 2)
        if (State.isAllocated(SystemZ::XPLINK64ArgFPRs[I]))
          State.AllocateReg(SystemZ::XPLINK64ArgFPRs[I + 1]);
    }
  }
  return false;
}

inline bool CC_XPLINK64_Allocate128BitVararg(unsigned &ValNo, MVT &ValVT,
                                             MVT &LocVT,
                                             CCValAssign::LocInfo &LocInfo,
                                             ISD::ArgFlagsTy &ArgFlags,
                                             CCState &State) {
  // For any C or C++ program, this should always be
  // false, since it is illegal to have a function
  // where the first argument is variadic. Therefore
  // the first fixed argument should already have
  // allocated GPR1 either through shadowing it or
  // using it for parameter passing.
  State.AllocateReg(SystemZ::R1D);

  bool AllocGPR2 = State.AllocateReg(SystemZ::R2D);
  bool AllocGPR3 = State.AllocateReg(SystemZ::R3D);

  // If GPR2 and GPR3 are available, then we may pass vararg in R2Q.
  // If only GPR3 is available, we need to set custom handling to copy
  // hi bits into GPR3.
  // Either way, we allocate on the stack.
  if (AllocGPR3) {
    // For f128 and vector var arg case, set the bitcast flag to bitcast to
    // i128.
    LocVT = MVT::i128;
    LocInfo = CCValAssign::BCvt;
    auto Offset = State.AllocateStack(16, Align(8));
    if (AllocGPR2)
      State.addLoc(
          CCValAssign::getReg(ValNo, ValVT, SystemZ::R2Q, LocVT, LocInfo));
    else
      State.addLoc(
          CCValAssign::getCustomMem(ValNo, ValVT, Offset, LocVT, LocInfo));
    return true;
  }

  return false;
}

inline bool RetCC_SystemZ_Error(unsigned &, MVT &, MVT &,
                                CCValAssign::LocInfo &, ISD::ArgFlagsTy &,
                                CCState &) {
  llvm_unreachable("Return value calling convention currently unsupported.");
}

inline bool CC_SystemZ_Error(unsigned &, MVT &, MVT &, CCValAssign::LocInfo &,
                             ISD::ArgFlagsTy &, CCState &) {
  llvm_unreachable("Argument calling convention currently unsupported.");
}

inline bool CC_SystemZ_GHC_Error(unsigned &, MVT &, MVT &,
                                 CCValAssign::LocInfo &, ISD::ArgFlagsTy &,
                                 CCState &) {
  report_fatal_error("No registers left in GHC calling convention");
  return false;
}

} // end namespace llvm

#endif
