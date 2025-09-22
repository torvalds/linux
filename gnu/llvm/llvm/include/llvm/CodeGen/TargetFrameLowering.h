//===-- llvm/CodeGen/TargetFrameLowering.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interface to describe the layout of a stack frame on the target machine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETFRAMELOWERING_H
#define LLVM_CODEGEN_TARGETFRAMELOWERING_H

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/ReturnProtectorLowering.h"
#include "llvm/Support/TypeSize.h"
#include <vector>

namespace llvm {
  class BitVector;
  class CalleeSavedInfo;
  class MachineFunction;
  class RegScavenger;

namespace TargetStackID {
enum Value {
  Default = 0,
  SGPRSpill = 1,
  ScalableVector = 2,
  WasmLocal = 3,
  NoAlloc = 255
};
}

/// Information about stack frame layout on the target.  It holds the direction
/// of stack growth, the known stack alignment on entry to each function, and
/// the offset to the locals area.
///
/// The offset to the local area is the offset from the stack pointer on
/// function entry to the first location where function data (local variables,
/// spill locations) can be stored.
class TargetFrameLowering {
public:
  enum StackDirection {
    StackGrowsUp,        // Adding to the stack increases the stack address
    StackGrowsDown       // Adding to the stack decreases the stack address
  };

  // Maps a callee saved register to a stack slot with a fixed offset.
  struct SpillSlot {
    unsigned Reg;
    int64_t Offset; // Offset relative to stack pointer on function entry.
  };

  struct DwarfFrameBase {
    // The frame base may be either a register (the default), the CFA with an
    // offset, or a WebAssembly-specific location description.
    enum FrameBaseKind { Register, CFA, WasmFrameBase } Kind;
    struct WasmFrameBase {
      unsigned Kind; // Wasm local, global, or value stack
      unsigned Index;
    };
    union {
      // Used with FrameBaseKind::Register.
      unsigned Reg;
      // Used with FrameBaseKind::CFA.
      int64_t Offset;
      struct WasmFrameBase WasmLoc;
    } Location;
  };

private:
  StackDirection StackDir;
  Align StackAlignment;
  Align TransientStackAlignment;
  int LocalAreaOffset;
  bool StackRealignable;
public:
  TargetFrameLowering(StackDirection D, Align StackAl, int LAO,
                      Align TransAl = Align(1), bool StackReal = true)
      : StackDir(D), StackAlignment(StackAl), TransientStackAlignment(TransAl),
        LocalAreaOffset(LAO), StackRealignable(StackReal) {}

  virtual ~TargetFrameLowering();

  // These methods return information that describes the abstract stack layout
  // of the target machine.

  /// getStackGrowthDirection - Return the direction the stack grows
  ///
  StackDirection getStackGrowthDirection() const { return StackDir; }

  /// getStackAlignment - This method returns the number of bytes to which the
  /// stack pointer must be aligned on entry to a function.  Typically, this
  /// is the largest alignment for any data object in the target.
  ///
  unsigned getStackAlignment() const { return StackAlignment.value(); }
  /// getStackAlignment - This method returns the number of bytes to which the
  /// stack pointer must be aligned on entry to a function.  Typically, this
  /// is the largest alignment for any data object in the target.
  ///
  Align getStackAlign() const { return StackAlignment; }

  /// getStackThreshold - Return the maximum stack size
  ///
  virtual uint64_t getStackThreshold() const { return UINT_MAX; }

  /// alignSPAdjust - This method aligns the stack adjustment to the correct
  /// alignment.
  ///
  int alignSPAdjust(int SPAdj) const {
    if (SPAdj < 0) {
      SPAdj = -alignTo(-SPAdj, StackAlignment);
    } else {
      SPAdj = alignTo(SPAdj, StackAlignment);
    }
    return SPAdj;
  }

  /// getTransientStackAlignment - This method returns the number of bytes to
  /// which the stack pointer must be aligned at all times, even between
  /// calls.
  ///
  Align getTransientStackAlign() const { return TransientStackAlignment; }

  /// isStackRealignable - This method returns whether the stack can be
  /// realigned.
  bool isStackRealignable() const {
    return StackRealignable;
  }

  /// This method returns whether or not it is safe for an object with the
  /// given stack id to be bundled into the local area.
  virtual bool isStackIdSafeForLocalArea(unsigned StackId) const {
    return true;
  }

  /// getOffsetOfLocalArea - This method returns the offset of the local area
  /// from the stack pointer on entrance to a function.
  ///
  int getOffsetOfLocalArea() const { return LocalAreaOffset; }

  /// Control the placement of special register scavenging spill slots when
  /// allocating a stack frame.
  ///
  /// If this returns true, the frame indexes used by the RegScavenger will be
  /// allocated closest to the incoming stack pointer.
  virtual bool allocateScavengingFrameIndexesNearIncomingSP(
    const MachineFunction &MF) const;

  /// assignCalleeSavedSpillSlots - Allows target to override spill slot
  /// assignment logic.  If implemented, assignCalleeSavedSpillSlots() should
  /// assign frame slots to all CSI entries and return true.  If this method
  /// returns false, spill slots will be assigned using generic implementation.
  /// assignCalleeSavedSpillSlots() may add, delete or rearrange elements of
  /// CSI.
  virtual bool assignCalleeSavedSpillSlots(MachineFunction &MF,
                                           const TargetRegisterInfo *TRI,
                                           std::vector<CalleeSavedInfo> &CSI,
                                           unsigned &MinCSFrameIndex,
                                           unsigned &MaxCSFrameIndex) const {
    return assignCalleeSavedSpillSlots(MF, TRI, CSI);
  }

  virtual bool
  assignCalleeSavedSpillSlots(MachineFunction &MF,
                              const TargetRegisterInfo *TRI,
                              std::vector<CalleeSavedInfo> &CSI) const {
    return false;
  }

  /// getCalleeSavedSpillSlots - This method returns a pointer to an array of
  /// pairs, that contains an entry for each callee saved register that must be
  /// spilled to a particular stack location if it is spilled.
  ///
  /// Each entry in this array contains a <register,offset> pair, indicating the
  /// fixed offset from the incoming stack pointer that each register should be
  /// spilled at. If a register is not listed here, the code generator is
  /// allowed to spill it anywhere it chooses.
  ///
  virtual const SpillSlot *
  getCalleeSavedSpillSlots(unsigned &NumEntries) const {
    NumEntries = 0;
    return nullptr;
  }

  /// targetHandlesStackFrameRounding - Returns true if the target is
  /// responsible for rounding up the stack frame (probably at emitPrologue
  /// time).
  virtual bool targetHandlesStackFrameRounding() const {
    return false;
  }

  /// Returns true if the target will correctly handle shrink wrapping.
  virtual bool enableShrinkWrapping(const MachineFunction &MF) const {
    return false;
  }

  /// Returns true if the stack slot holes in the fixed and callee-save stack
  /// area should be used when allocating other stack locations to reduce stack
  /// size.
  virtual bool enableStackSlotScavenging(const MachineFunction &MF) const {
    return false;
  }

  /// Returns true if the target can safely skip saving callee-saved registers
  /// for noreturn nounwind functions.
  virtual bool enableCalleeSaveSkip(const MachineFunction &MF) const;

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  virtual void emitPrologue(MachineFunction &MF,
                            MachineBasicBlock &MBB) const = 0;
  virtual void emitEpilogue(MachineFunction &MF,
                            MachineBasicBlock &MBB) const = 0;

  virtual const ReturnProtectorLowering *getReturnProtector() const {
    return nullptr;
  }

  /// emitZeroCallUsedRegs - Zeros out call used registers.
  virtual void emitZeroCallUsedRegs(BitVector RegsToZero,
                                    MachineBasicBlock &MBB) const {}

  /// With basic block sections, emit callee saved frame moves for basic blocks
  /// that are in a different section.
  virtual void
  emitCalleeSavedFrameMovesFullCFA(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI) const {}

  /// Returns true if we may need to fix the unwind information for the
  /// function.
  virtual bool enableCFIFixup(MachineFunction &MF) const;

  /// Emit CFI instructions that recreate the state of the unwind information
  /// upon fucntion entry.
  virtual void resetCFIToInitialState(MachineBasicBlock &MBB) const {}

  /// Replace a StackProbe stub (if any) with the actual probe code inline
  virtual void inlineStackProbe(MachineFunction &MF,
                                MachineBasicBlock &PrologueMBB) const {}

  /// Does the stack probe function call return with a modified stack pointer?
  virtual bool stackProbeFunctionModifiesSP() const { return false; }

  /// Adjust the prologue to have the function use segmented stacks. This works
  /// by adding a check even before the "normal" function prologue.
  virtual void adjustForSegmentedStacks(MachineFunction &MF,
                                        MachineBasicBlock &PrologueMBB) const {}

  /// Adjust the prologue to add Erlang Run-Time System (ERTS) specific code in
  /// the assembly prologue to explicitly handle the stack.
  virtual void adjustForHiPEPrologue(MachineFunction &MF,
                                     MachineBasicBlock &PrologueMBB) const {}

  /// spillCalleeSavedRegisters - Issues instruction(s) to spill all callee
  /// saved registers and returns true if it isn't possible / profitable to do
  /// so by issuing a series of store instructions via
  /// storeRegToStackSlot(). Returns false otherwise.
  virtual bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         ArrayRef<CalleeSavedInfo> CSI,
                                         const TargetRegisterInfo *TRI) const {
    return false;
  }

  /// restoreCalleeSavedRegisters - Issues instruction(s) to restore all callee
  /// saved registers and returns true if it isn't possible / profitable to do
  /// so by issuing a series of load instructions via loadRegToStackSlot().
  /// If it returns true, and any of the registers in CSI is not restored,
  /// it sets the corresponding Restored flag in CSI to false.
  /// Returns false otherwise.
  virtual bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const {
    return false;
  }

  /// Return true if the target wants to keep the frame pointer regardless of
  /// the function attribute "frame-pointer".
  virtual bool keepFramePointer(const MachineFunction &MF) const {
    return false;
  }

  /// hasFP - Return true if the specified function should have a dedicated
  /// frame pointer register. For most targets this is true only if the function
  /// has variable sized allocas or if frame pointer elimination is disabled.
  virtual bool hasFP(const MachineFunction &MF) const = 0;

  /// hasReservedCallFrame - Under normal circumstances, when a frame pointer is
  /// not required, we reserve argument space for call sites in the function
  /// immediately on entry to the current function. This eliminates the need for
  /// add/sub sp brackets around call sites. Returns true if the call frame is
  /// included as part of the stack frame.
  virtual bool hasReservedCallFrame(const MachineFunction &MF) const {
    return !hasFP(MF);
  }

  /// canSimplifyCallFramePseudos - When possible, it's best to simplify the
  /// call frame pseudo ops before doing frame index elimination. This is
  /// possible only when frame index references between the pseudos won't
  /// need adjusting for the call frame adjustments. Normally, that's true
  /// if the function has a reserved call frame or a frame pointer. Some
  /// targets (Thumb2, for example) may have more complicated criteria,
  /// however, and can override this behavior.
  virtual bool canSimplifyCallFramePseudos(const MachineFunction &MF) const {
    return hasReservedCallFrame(MF) || hasFP(MF);
  }

  // needsFrameIndexResolution - Do we need to perform FI resolution for
  // this function. Normally, this is required only when the function
  // has any stack objects. However, targets may want to override this.
  virtual bool needsFrameIndexResolution(const MachineFunction &MF) const;

  /// getFrameIndexReference - This method should return the base register
  /// and offset used to reference a frame index location. The offset is
  /// returned directly, and the base register is returned via FrameReg.
  virtual StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                             Register &FrameReg) const;

  /// Same as \c getFrameIndexReference, except that the stack pointer (as
  /// opposed to the frame pointer) will be the preferred value for \p
  /// FrameReg. This is generally used for emitting statepoint or EH tables that
  /// use offsets from RSP.  If \p IgnoreSPUpdates is true, the returned
  /// offset is only guaranteed to be valid with respect to the value of SP at
  /// the end of the prologue.
  virtual StackOffset
  getFrameIndexReferencePreferSP(const MachineFunction &MF, int FI,
                                 Register &FrameReg,
                                 bool IgnoreSPUpdates) const {
    // Always safe to dispatch to getFrameIndexReference.
    return getFrameIndexReference(MF, FI, FrameReg);
  }

  /// getNonLocalFrameIndexReference - This method returns the offset used to
  /// reference a frame index location. The offset can be from either FP/BP/SP
  /// based on which base register is returned by llvm.localaddress.
  virtual StackOffset getNonLocalFrameIndexReference(const MachineFunction &MF,
                                                     int FI) const {
    // By default, dispatch to getFrameIndexReference. Interested targets can
    // override this.
    Register FrameReg;
    return getFrameIndexReference(MF, FI, FrameReg);
  }

  /// getFrameIndexReferenceFromSP - This method returns the offset from the
  /// stack pointer to the slot of the specified index. This function serves to
  /// provide a comparable offset from a single reference point (the value of
  /// the stack-pointer at function entry) that can be used for analysis.
  virtual StackOffset getFrameIndexReferenceFromSP(const MachineFunction &MF,
                                                   int FI) const;

  /// Returns the callee-saved registers as computed by determineCalleeSaves
  /// in the BitVector \p SavedRegs.
  virtual void getCalleeSaves(const MachineFunction &MF,
                                  BitVector &SavedRegs) const;

  /// This method determines which of the registers reported by
  /// TargetRegisterInfo::getCalleeSavedRegs() should actually get saved.
  /// The default implementation checks populates the \p SavedRegs bitset with
  /// all registers which are modified in the function, targets may override
  /// this function to save additional registers.
  /// This method also sets up the register scavenger ensuring there is a free
  /// register or a frameindex available.
  /// This method should not be called by any passes outside of PEI, because
  /// it may change state passed in by \p MF and \p RS. The preferred
  /// interface outside PEI is getCalleeSaves.
  virtual void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                                    RegScavenger *RS = nullptr) const;

  /// processFunctionBeforeFrameFinalized - This method is called immediately
  /// before the specified function's frame layout (MF.getFrameInfo()) is
  /// finalized.  Once the frame is finalized, MO_FrameIndex operands are
  /// replaced with direct constants.  This method is optional.
  ///
  virtual void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                             RegScavenger *RS = nullptr) const {
  }

  /// processFunctionBeforeFrameIndicesReplaced - This method is called
  /// immediately before MO_FrameIndex operands are eliminated, but after the
  /// frame is finalized. This method is optional.
  virtual void
  processFunctionBeforeFrameIndicesReplaced(MachineFunction &MF,
                                            RegScavenger *RS = nullptr) const {}

  virtual unsigned getWinEHParentFrameOffset(const MachineFunction &MF) const {
    report_fatal_error("WinEH not implemented for this target");
  }

  /// This method is called during prolog/epilog code insertion to eliminate
  /// call frame setup and destroy pseudo instructions (but only if the Target
  /// is using them).  It is responsible for eliminating these instructions,
  /// replacing them with concrete instructions.  This method need only be
  /// implemented if using call frame setup/destroy pseudo instructions.
  /// Returns an iterator pointing to the instruction after the replaced one.
  virtual MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF,
                                MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const {
    llvm_unreachable("Call Frame Pseudo Instructions do not exist on this "
                     "target!");
  }


  /// Order the symbols in the local stack frame.
  /// The list of objects that we want to order is in \p objectsToAllocate as
  /// indices into the MachineFrameInfo. The array can be reordered in any way
  /// upon return. The contents of the array, however, may not be modified (i.e.
  /// only their order may be changed).
  /// By default, just maintain the original order.
  virtual void
  orderFrameObjects(const MachineFunction &MF,
                    SmallVectorImpl<int> &objectsToAllocate) const {
  }

  /// Check whether or not the given \p MBB can be used as a prologue
  /// for the target.
  /// The prologue will be inserted first in this basic block.
  /// This method is used by the shrink-wrapping pass to decide if
  /// \p MBB will be correctly handled by the target.
  /// As soon as the target enable shrink-wrapping without overriding
  /// this method, we assume that each basic block is a valid
  /// prologue.
  virtual bool canUseAsPrologue(const MachineBasicBlock &MBB) const {
    return true;
  }

  /// Check whether or not the given \p MBB can be used as a epilogue
  /// for the target.
  /// The epilogue will be inserted before the first terminator of that block.
  /// This method is used by the shrink-wrapping pass to decide if
  /// \p MBB will be correctly handled by the target.
  /// As soon as the target enable shrink-wrapping without overriding
  /// this method, we assume that each basic block is a valid
  /// epilogue.
  virtual bool canUseAsEpilogue(const MachineBasicBlock &MBB) const {
    return true;
  }

  /// Returns the StackID that scalable vectors should be associated with.
  virtual TargetStackID::Value getStackIDForScalableVectors() const {
    return TargetStackID::Default;
  }

  virtual bool isSupportedStackID(TargetStackID::Value ID) const {
    switch (ID) {
    default:
      return false;
    case TargetStackID::Default:
    case TargetStackID::NoAlloc:
      return true;
    }
  }

  /// Check if given function is safe for not having callee saved registers.
  /// This is used when interprocedural register allocation is enabled.
  static bool isSafeForNoCSROpt(const Function &F);

  /// Check if the no-CSR optimisation is profitable for the given function.
  virtual bool isProfitableForNoCSROpt(const Function &F) const {
    return true;
  }

  /// Return initial CFA offset value i.e. the one valid at the beginning of the
  /// function (before any stack operations).
  virtual int getInitialCFAOffset(const MachineFunction &MF) const;

  /// Return initial CFA register value i.e. the one valid at the beginning of
  /// the function (before any stack operations).
  virtual Register getInitialCFARegister(const MachineFunction &MF) const;

  /// Return the frame base information to be encoded in the DWARF subprogram
  /// debug info.
  virtual DwarfFrameBase getDwarfFrameBase(const MachineFunction &MF) const;

  /// This method is called at the end of prolog/epilog code insertion, so
  /// targets can emit remarks based on the final frame layout.
  virtual void emitRemarks(const MachineFunction &MF,
                           MachineOptimizationRemarkEmitter *ORE) const {};
};

} // End llvm namespace

#endif
