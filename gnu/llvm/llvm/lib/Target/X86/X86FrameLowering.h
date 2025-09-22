//===-- X86TargetFrameLowering.h - Define frame lowering for X86 -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements X86-specific bits of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86FRAMELOWERING_H
#define LLVM_LIB_TARGET_X86_X86FRAMELOWERING_H

#include "X86ReturnProtectorLowering.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/TypeSize.h"

namespace llvm {

class MachineInstrBuilder;
class MCCFIInstruction;
class X86InstrInfo;
class X86Subtarget;
class X86RegisterInfo;
class X86ReturnProtectorLowering;

class X86FrameLowering : public TargetFrameLowering {
public:
  X86FrameLowering(const X86Subtarget &STI, MaybeAlign StackAlignOverride);

  // Cached subtarget predicates.

  const X86Subtarget &STI;
  const X86InstrInfo &TII;
  const X86RegisterInfo *TRI;
  const X86ReturnProtectorLowering RPL;

  bool SaveArgs;

  unsigned SlotSize;

  /// Is64Bit implies that x86_64 instructions are available.
  bool Is64Bit;

  bool IsLP64;

  /// True if the 64-bit frame or stack pointer should be used. True for most
  /// 64-bit targets with the exception of x32. If this is false, 32-bit
  /// instruction operands should be used to manipulate StackPtr and FramePtr.
  bool Uses64BitFramePtr;

  unsigned StackPtr;

  /// Emit target stack probe code. This is required for all
  /// large stack allocations on Windows. The caller is required to materialize
  /// the number of bytes to probe in RAX/EAX.
  /// \p InstrNum optionally contains a debug-info instruction number for the
  ///    new stack pointer.
  void emitStackProbe(MachineFunction &MF, MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                      bool InProlog,
                      std::optional<MachineFunction::DebugInstrOperandPair>
                          InstrNum = std::nullopt) const;

  bool stackProbeFunctionModifiesSP() const override;

  /// Replace a StackProbe inline-stub with the actual probe code inline.
  void inlineStackProbe(MachineFunction &MF,
                        MachineBasicBlock &PrologMBB) const override;

  void emitCalleeSavedFrameMovesFullCFA(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI) const override;

  void emitCalleeSavedFrameMoves(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 const DebugLoc &DL, bool IsPrologue) const;

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  const ReturnProtectorLowering *getReturnProtector() const override;

  void adjustForSegmentedStacks(MachineFunction &MF,
                                MachineBasicBlock &PrologueMBB) const override;

  void adjustForHiPEPrologue(MachineFunction &MF,
                             MachineBasicBlock &PrologueMBB) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS = nullptr) const override;

  bool
  assignCalleeSavedSpillSlots(MachineFunction &MF,
                              const TargetRegisterInfo *TRI,
                              std::vector<CalleeSavedInfo> &CSI) const override;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;

  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;

  bool hasFP(const MachineFunction &MF) const override;
  bool hasReservedCallFrame(const MachineFunction &MF) const override;
  bool canSimplifyCallFramePseudos(const MachineFunction &MF) const override;
  bool needsFrameIndexResolution(const MachineFunction &MF) const override;

  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;

  int getWin64EHFrameIndexRef(const MachineFunction &MF, int FI,
                              Register &SPReg) const;
  StackOffset getFrameIndexReferenceSP(const MachineFunction &MF, int FI,
                                       Register &SPReg, int Adjustment) const;
  StackOffset
  getFrameIndexReferencePreferSP(const MachineFunction &MF, int FI,
                                 Register &FrameReg,
                                 bool IgnoreSPUpdates) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;

  unsigned getWinEHParentFrameOffset(const MachineFunction &MF) const override;

  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                           RegScavenger *RS) const override;

  void
  processFunctionBeforeFrameIndicesReplaced(MachineFunction &MF,
                                            RegScavenger *RS) const override;

  /// Check the instruction before/after the passed instruction. If
  /// it is an ADD/SUB/LEA instruction it is deleted argument and the
  /// stack adjustment is returned as a positive value for ADD/LEA and
  /// a negative for SUB.
  int mergeSPUpdates(MachineBasicBlock &MBB, MachineBasicBlock::iterator &MBBI,
                     bool doMergeWithPrevious) const;

  /// Emit a series of instructions to increment / decrement the stack
  /// pointer by a constant value.
  void emitSPUpdate(MachineBasicBlock &MBB, MachineBasicBlock::iterator &MBBI,
                    const DebugLoc &DL, int64_t NumBytes, bool InEpilogue) const;

  /// Check that LEA can be used on SP in an epilogue sequence for \p MF.
  bool canUseLEAForSPInEpilogue(const MachineFunction &MF) const;

  /// Check whether or not the given \p MBB can be used as a prologue
  /// for the target.
  /// The prologue will be inserted first in this basic block.
  /// This method is used by the shrink-wrapping pass to decide if
  /// \p MBB will be correctly handled by the target.
  /// As soon as the target enable shrink-wrapping without overriding
  /// this method, we assume that each basic block is a valid
  /// prologue.
  bool canUseAsPrologue(const MachineBasicBlock &MBB) const override;

  /// Check whether or not the given \p MBB can be used as a epilogue
  /// for the target.
  /// The epilogue will be inserted before the first terminator of that block.
  /// This method is used by the shrink-wrapping pass to decide if
  /// \p MBB will be correctly handled by the target.
  bool canUseAsEpilogue(const MachineBasicBlock &MBB) const override;

  /// Returns true if the target will correctly handle shrink wrapping.
  bool enableShrinkWrapping(const MachineFunction &MF) const override;

  /// Order the symbols in the local stack.
  /// We want to place the local stack objects in some sort of sensible order.
  /// The heuristic we use is to try and pack them according to static number
  /// of uses and size in order to minimize code size.
  void orderFrameObjects(const MachineFunction &MF,
                         SmallVectorImpl<int> &ObjectsToAllocate) const override;

  /// Wraps up getting a CFI index and building a MachineInstr for it.
  void BuildCFI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                const DebugLoc &DL, const MCCFIInstruction &CFIInst,
                MachineInstr::MIFlag Flag = MachineInstr::NoFlags) const;

  /// Sets up EBP and optionally ESI based on the incoming EBP value.  Only
  /// needed for 32-bit. Used in funclet prologues and at catchret destinations.
  MachineBasicBlock::iterator
  restoreWin32EHStackPointers(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              const DebugLoc &DL, bool RestoreSP = false) const;

  void restoreWinEHStackPointersInParent(MachineFunction &MF) const;

  int getInitialCFAOffset(const MachineFunction &MF) const override;

  Register getInitialCFARegister(const MachineFunction &MF) const override;

  DwarfFrameBase getDwarfFrameBase(const MachineFunction &MF) const override;

  /// Return true if the function has a redzone (accessible bytes past the
  /// frame of the top of stack function) as part of it's ABI.
  bool has128ByteRedZone(const MachineFunction& MF) const;

private:
  bool isWin64Prologue(const MachineFunction &MF) const;

  bool needsDwarfCFI(const MachineFunction &MF) const;

  uint64_t calculateMaxStackAlign(const MachineFunction &MF) const;

  /// Emit target stack probe as a call to a helper function
  void emitStackProbeCall(
      MachineFunction &MF, MachineBasicBlock &MBB,
      MachineBasicBlock::iterator MBBI, const DebugLoc &DL, bool InProlog,
      std::optional<MachineFunction::DebugInstrOperandPair> InstrNum) const;

  /// Emit target stack probe as an inline sequence.
  void emitStackProbeInline(MachineFunction &MF, MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            const DebugLoc &DL, bool InProlog) const;
  void emitStackProbeInlineWindowsCoreCLR64(MachineFunction &MF,
                                            MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator MBBI,
                                            const DebugLoc &DL,
                                            bool InProlog) const;
  void emitStackProbeInlineGeneric(MachineFunction &MF, MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   const DebugLoc &DL, bool InProlog) const;

  void emitStackProbeInlineGenericBlock(MachineFunction &MF,
                                        MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        const DebugLoc &DL, uint64_t Offset,
                                        uint64_t Align) const;

  void emitStackProbeInlineGenericLoop(MachineFunction &MF,
                                       MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       const DebugLoc &DL, uint64_t Offset,
                                       uint64_t Align) const;

  /// Emit target zero call-used regs.
  void emitZeroCallUsedRegs(BitVector RegsToZero,
                            MachineBasicBlock &MBB) const override;

  void adjustFrameForMsvcCxxEh(MachineFunction &MF) const;

  /// Aligns the stack pointer by ANDing it with -MaxAlign.
  void BuildStackAlignAND(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                          unsigned Reg, uint64_t MaxAlign) const;

  /// Make small positive stack adjustments using POPs.
  bool adjustStackWithPops(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                           int Offset) const;

  /// Adjusts the stack pointer using LEA, SUB, or ADD.
  MachineInstrBuilder BuildStackAdjustment(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MBBI,
                                           const DebugLoc &DL, int64_t Offset,
                                           bool InEpilogue) const;

  unsigned getPSPSlotOffsetFromSP(const MachineFunction &MF) const;

  unsigned getWinEHFuncletFrameSize(const MachineFunction &MF) const;

  /// Materialize the catchret target MBB in RAX.
  void emitCatchRetReturnValue(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI,
                               MachineInstr *CatchRet) const;
};

} // End llvm namespace

#endif
