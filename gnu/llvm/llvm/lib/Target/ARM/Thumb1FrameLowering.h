//===- Thumb1FrameLowering.h - Thumb1-specific frame info stuff ---*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_THUMB1FRAMELOWERING_H
#define LLVM_LIB_TARGET_ARM_THUMB1FRAMELOWERING_H

#include "ARMFrameLowering.h"

namespace llvm {

class ARMSubtarget;
class MachineFunction;

class Thumb1FrameLowering : public ARMFrameLowering {
public:
  explicit Thumb1FrameLowering(const ARMSubtarget &sti);

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;
  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF,
                                MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;

  /// Check whether or not the given \p MBB can be used as a epilogue
  /// for the target.
  /// The epilogue will be inserted before the first terminator of that block.
  /// This method is used by the shrink-wrapping pass to decide if
  /// \p MBB will be correctly handled by the target.
  bool canUseAsEpilogue(const MachineBasicBlock &MBB) const override;

  /// Disable shrink wrap as tBfar/BL will be used to adjust for long jumps.
  bool enableShrinkWrapping(const MachineFunction &MF) const override {
    return false;
  }

private:
  /// Check if the frame lowering of \p MF needs a special fixup
  /// code sequence for the epilogue.
  /// Unlike T2 and ARM mode, the T1 pop instruction cannot restore
  /// to LR, and we can't pop the value directly to the PC when
  /// we need to update the SP after popping the value. So instead
  /// we have to emit:
  ///   POP {r3}
  ///   ADD sp, #offset
  ///   BX r3
  /// If this would clobber a return value, then generate this sequence instead:
  ///   MOV ip, r3
  ///   POP {r3}
  ///   ADD sp, #offset
  ///   MOV lr, r3
  ///   MOV r3, ip
  ///   BX lr
  bool needPopSpecialFixUp(const MachineFunction &MF) const;

  /// Emit the special fixup code sequence for the epilogue.
  /// \see needPopSpecialFixUp for more details.
  /// \p DoIt, tells this method whether or not to actually insert
  /// the code sequence in \p MBB. I.e., when \p DoIt is false,
  /// \p MBB is left untouched.
  /// \returns For \p DoIt == true: True when the emission succeeded
  /// false otherwise. For \p DoIt == false: True when the emission
  /// would have been possible, false otherwise.
  bool emitPopSpecialFixUp(MachineBasicBlock &MBB, bool DoIt) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_THUMB1FRAMELOWERING_H
