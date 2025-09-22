//===- llvm/CodeGen/TargetInstrInfo.h - Instruction Info --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the target machine instruction set to the code generator.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETINSTRINFO_H
#define LLVM_CODEGEN_TARGETINSTRINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Uniformity.h"
#include "llvm/CodeGen/MIRFormatter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineCombinerPattern.h"
#include "llvm/CodeGen/MachineCycleAnalysis.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineOutliner.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/ErrorHandling.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace llvm {

class DFAPacketizer;
class InstrItineraryData;
class LiveIntervals;
class LiveVariables;
class MachineLoop;
class MachineMemOperand;
class MachineRegisterInfo;
class MCAsmInfo;
class MCInst;
struct MCSchedModel;
class Module;
class ScheduleDAG;
class ScheduleDAGMI;
class ScheduleHazardRecognizer;
class SDNode;
class SelectionDAG;
class SMSchedule;
class SwingSchedulerDAG;
class RegScavenger;
class TargetRegisterClass;
class TargetRegisterInfo;
class TargetSchedModel;
class TargetSubtargetInfo;
enum class MachineTraceStrategy;

template <class T> class SmallVectorImpl;

using ParamLoadedValue = std::pair<MachineOperand, DIExpression*>;

struct DestSourcePair {
  const MachineOperand *Destination;
  const MachineOperand *Source;

  DestSourcePair(const MachineOperand &Dest, const MachineOperand &Src)
      : Destination(&Dest), Source(&Src) {}
};

/// Used to describe a register and immediate addition.
struct RegImmPair {
  Register Reg;
  int64_t Imm;

  RegImmPair(Register Reg, int64_t Imm) : Reg(Reg), Imm(Imm) {}
};

/// Used to describe addressing mode similar to ExtAddrMode in CodeGenPrepare.
/// It holds the register values, the scale value and the displacement.
/// It also holds a descriptor for the expression used to calculate the address
/// from the operands.
struct ExtAddrMode {
  enum class Formula {
    Basic = 0,         // BaseReg + ScaledReg * Scale + Displacement
    SExtScaledReg = 1, // BaseReg + sext(ScaledReg) * Scale + Displacement
    ZExtScaledReg = 2  // BaseReg + zext(ScaledReg) * Scale + Displacement
  };

  Register BaseReg;
  Register ScaledReg;
  int64_t Scale = 0;
  int64_t Displacement = 0;
  Formula Form = Formula::Basic;
  ExtAddrMode() = default;
};

//---------------------------------------------------------------------------
///
/// TargetInstrInfo - Interface to description of machine instruction set
///
class TargetInstrInfo : public MCInstrInfo {
public:
  TargetInstrInfo(unsigned CFSetupOpcode = ~0u, unsigned CFDestroyOpcode = ~0u,
                  unsigned CatchRetOpcode = ~0u, unsigned ReturnOpcode = ~0u)
      : CallFrameSetupOpcode(CFSetupOpcode),
        CallFrameDestroyOpcode(CFDestroyOpcode), CatchRetOpcode(CatchRetOpcode),
        ReturnOpcode(ReturnOpcode) {}
  TargetInstrInfo(const TargetInstrInfo &) = delete;
  TargetInstrInfo &operator=(const TargetInstrInfo &) = delete;
  virtual ~TargetInstrInfo();

  static bool isGenericOpcode(unsigned Opc) {
    return Opc <= TargetOpcode::GENERIC_OP_END;
  }

  static bool isGenericAtomicRMWOpcode(unsigned Opc) {
    return Opc >= TargetOpcode::GENERIC_ATOMICRMW_OP_START &&
           Opc <= TargetOpcode::GENERIC_ATOMICRMW_OP_END;
  }

  /// Given a machine instruction descriptor, returns the register
  /// class constraint for OpNum, or NULL.
  virtual
  const TargetRegisterClass *getRegClass(const MCInstrDesc &MCID, unsigned OpNum,
                                         const TargetRegisterInfo *TRI,
                                         const MachineFunction &MF) const;

  /// Return true if the instruction is trivially rematerializable, meaning it
  /// has no side effects and requires no operands that aren't always available.
  /// This means the only allowed uses are constants and unallocatable physical
  /// registers so that the instructions result is independent of the place
  /// in the function.
  bool isTriviallyReMaterializable(const MachineInstr &MI) const {
    return (MI.getOpcode() == TargetOpcode::IMPLICIT_DEF &&
            MI.getNumOperands() == 1) ||
           (MI.getDesc().isRematerializable() &&
            isReallyTriviallyReMaterializable(MI));
  }

  /// Given \p MO is a PhysReg use return if it can be ignored for the purpose
  /// of instruction rematerialization or sinking.
  virtual bool isIgnorableUse(const MachineOperand &MO) const {
    return false;
  }

  virtual bool isSafeToSink(MachineInstr &MI, MachineBasicBlock *SuccToSinkTo,
                            MachineCycleInfo *CI) const {
    return true;
  }

protected:
  /// For instructions with opcodes for which the M_REMATERIALIZABLE flag is
  /// set, this hook lets the target specify whether the instruction is actually
  /// trivially rematerializable, taking into consideration its operands. This
  /// predicate must return false if the instruction has any side effects other
  /// than producing a value, or if it requres any address registers that are
  /// not always available.
  virtual bool isReallyTriviallyReMaterializable(const MachineInstr &MI) const;

  /// This method commutes the operands of the given machine instruction MI.
  /// The operands to be commuted are specified by their indices OpIdx1 and
  /// OpIdx2.
  ///
  /// If a target has any instructions that are commutable but require
  /// converting to different instructions or making non-trivial changes
  /// to commute them, this method can be overloaded to do that.
  /// The default implementation simply swaps the commutable operands.
  ///
  /// If NewMI is false, MI is modified in place and returned; otherwise, a
  /// new machine instruction is created and returned.
  ///
  /// Do not call this method for a non-commutable instruction.
  /// Even though the instruction is commutable, the method may still
  /// fail to commute the operands, null pointer is returned in such cases.
  virtual MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                               unsigned OpIdx1,
                                               unsigned OpIdx2) const;

  /// Assigns the (CommutableOpIdx1, CommutableOpIdx2) pair of commutable
  /// operand indices to (ResultIdx1, ResultIdx2).
  /// One or both input values of the pair: (ResultIdx1, ResultIdx2) may be
  /// predefined to some indices or be undefined (designated by the special
  /// value 'CommuteAnyOperandIndex').
  /// The predefined result indices cannot be re-defined.
  /// The function returns true iff after the result pair redefinition
  /// the fixed result pair is equal to or equivalent to the source pair of
  /// indices: (CommutableOpIdx1, CommutableOpIdx2). It is assumed here that
  /// the pairs (x,y) and (y,x) are equivalent.
  static bool fixCommutedOpIndices(unsigned &ResultIdx1, unsigned &ResultIdx2,
                                   unsigned CommutableOpIdx1,
                                   unsigned CommutableOpIdx2);

public:
  /// These methods return the opcode of the frame setup/destroy instructions
  /// if they exist (-1 otherwise).  Some targets use pseudo instructions in
  /// order to abstract away the difference between operating with a frame
  /// pointer and operating without, through the use of these two instructions.
  /// A FrameSetup MI in MF implies MFI::AdjustsStack.
  ///
  unsigned getCallFrameSetupOpcode() const { return CallFrameSetupOpcode; }
  unsigned getCallFrameDestroyOpcode() const { return CallFrameDestroyOpcode; }

  /// Returns true if the argument is a frame pseudo instruction.
  bool isFrameInstr(const MachineInstr &I) const {
    return I.getOpcode() == getCallFrameSetupOpcode() ||
           I.getOpcode() == getCallFrameDestroyOpcode();
  }

  /// Returns true if the argument is a frame setup pseudo instruction.
  bool isFrameSetup(const MachineInstr &I) const {
    return I.getOpcode() == getCallFrameSetupOpcode();
  }

  /// Returns size of the frame associated with the given frame instruction.
  /// For frame setup instruction this is frame that is set up space set up
  /// after the instruction. For frame destroy instruction this is the frame
  /// freed by the caller.
  /// Note, in some cases a call frame (or a part of it) may be prepared prior
  /// to the frame setup instruction. It occurs in the calls that involve
  /// inalloca arguments. This function reports only the size of the frame part
  /// that is set up between the frame setup and destroy pseudo instructions.
  int64_t getFrameSize(const MachineInstr &I) const {
    assert(isFrameInstr(I) && "Not a frame instruction");
    assert(I.getOperand(0).getImm() >= 0);
    return I.getOperand(0).getImm();
  }

  /// Returns the total frame size, which is made up of the space set up inside
  /// the pair of frame start-stop instructions and the space that is set up
  /// prior to the pair.
  int64_t getFrameTotalSize(const MachineInstr &I) const {
    if (isFrameSetup(I)) {
      assert(I.getOperand(1).getImm() >= 0 &&
             "Frame size must not be negative");
      return getFrameSize(I) + I.getOperand(1).getImm();
    }
    return getFrameSize(I);
  }

  unsigned getCatchReturnOpcode() const { return CatchRetOpcode; }
  unsigned getReturnOpcode() const { return ReturnOpcode; }

  /// Returns the actual stack pointer adjustment made by an instruction
  /// as part of a call sequence. By default, only call frame setup/destroy
  /// instructions adjust the stack, but targets may want to override this
  /// to enable more fine-grained adjustment, or adjust by a different value.
  virtual int getSPAdjust(const MachineInstr &MI) const;

  /// Return true if the instruction is a "coalescable" extension instruction.
  /// That is, it's like a copy where it's legal for the source to overlap the
  /// destination. e.g. X86::MOVSX64rr32. If this returns true, then it's
  /// expected the pre-extension value is available as a subreg of the result
  /// register. This also returns the sub-register index in SubIdx.
  virtual bool isCoalescableExtInstr(const MachineInstr &MI, Register &SrcReg,
                                     Register &DstReg, unsigned &SubIdx) const {
    return false;
  }

  /// If the specified machine instruction is a direct
  /// load from a stack slot, return the virtual or physical register number of
  /// the destination along with the FrameIndex of the loaded stack slot.  If
  /// not, return 0.  This predicate must return 0 if the instruction has
  /// any side effects other than loading from the stack slot.
  virtual Register isLoadFromStackSlot(const MachineInstr &MI,
                                       int &FrameIndex) const {
    return 0;
  }

  /// Optional extension of isLoadFromStackSlot that returns the number of
  /// bytes loaded from the stack. This must be implemented if a backend
  /// supports partial stack slot spills/loads to further disambiguate
  /// what the load does.
  virtual Register isLoadFromStackSlot(const MachineInstr &MI,
                                       int &FrameIndex,
                                       unsigned &MemBytes) const {
    MemBytes = 0;
    return isLoadFromStackSlot(MI, FrameIndex);
  }

  /// Check for post-frame ptr elimination stack locations as well.
  /// This uses a heuristic so it isn't reliable for correctness.
  virtual Register isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                             int &FrameIndex) const {
    return 0;
  }

  /// If the specified machine instruction has a load from a stack slot,
  /// return true along with the FrameIndices of the loaded stack slot and the
  /// machine mem operands containing the reference.
  /// If not, return false.  Unlike isLoadFromStackSlot, this returns true for
  /// any instructions that loads from the stack.  This is just a hint, as some
  /// cases may be missed.
  virtual bool hasLoadFromStackSlot(
      const MachineInstr &MI,
      SmallVectorImpl<const MachineMemOperand *> &Accesses) const;

  /// If the specified machine instruction is a direct
  /// store to a stack slot, return the virtual or physical register number of
  /// the source reg along with the FrameIndex of the loaded stack slot.  If
  /// not, return 0.  This predicate must return 0 if the instruction has
  /// any side effects other than storing to the stack slot.
  virtual Register isStoreToStackSlot(const MachineInstr &MI,
                                      int &FrameIndex) const {
    return 0;
  }

  /// Optional extension of isStoreToStackSlot that returns the number of
  /// bytes stored to the stack. This must be implemented if a backend
  /// supports partial stack slot spills/loads to further disambiguate
  /// what the store does.
  virtual Register isStoreToStackSlot(const MachineInstr &MI,
                                      int &FrameIndex,
                                      unsigned &MemBytes) const {
    MemBytes = 0;
    return isStoreToStackSlot(MI, FrameIndex);
  }

  /// Check for post-frame ptr elimination stack locations as well.
  /// This uses a heuristic, so it isn't reliable for correctness.
  virtual Register isStoreToStackSlotPostFE(const MachineInstr &MI,
                                            int &FrameIndex) const {
    return 0;
  }

  /// If the specified machine instruction has a store to a stack slot,
  /// return true along with the FrameIndices of the loaded stack slot and the
  /// machine mem operands containing the reference.
  /// If not, return false.  Unlike isStoreToStackSlot,
  /// this returns true for any instructions that stores to the
  /// stack.  This is just a hint, as some cases may be missed.
  virtual bool hasStoreToStackSlot(
      const MachineInstr &MI,
      SmallVectorImpl<const MachineMemOperand *> &Accesses) const;

  /// Return true if the specified machine instruction
  /// is a copy of one stack slot to another and has no other effect.
  /// Provide the identity of the two frame indices.
  virtual bool isStackSlotCopy(const MachineInstr &MI, int &DestFrameIndex,
                               int &SrcFrameIndex) const {
    return false;
  }

  /// Compute the size in bytes and offset within a stack slot of a spilled
  /// register or subregister.
  ///
  /// \param [out] Size in bytes of the spilled value.
  /// \param [out] Offset in bytes within the stack slot.
  /// \returns true if both Size and Offset are successfully computed.
  ///
  /// Not all subregisters have computable spill slots. For example,
  /// subregisters registers may not be byte-sized, and a pair of discontiguous
  /// subregisters has no single offset.
  ///
  /// Targets with nontrivial bigendian implementations may need to override
  /// this, particularly to support spilled vector registers.
  virtual bool getStackSlotRange(const TargetRegisterClass *RC, unsigned SubIdx,
                                 unsigned &Size, unsigned &Offset,
                                 const MachineFunction &MF) const;

  /// Return true if the given instruction is terminator that is unspillable,
  /// according to isUnspillableTerminatorImpl.
  bool isUnspillableTerminator(const MachineInstr *MI) const {
    return MI->isTerminator() && isUnspillableTerminatorImpl(MI);
  }

  /// Returns the size in bytes of the specified MachineInstr, or ~0U
  /// when this function is not implemented by a target.
  virtual unsigned getInstSizeInBytes(const MachineInstr &MI) const {
    return ~0U;
  }

  /// Return true if the instruction is as cheap as a move instruction.
  ///
  /// Targets for different archs need to override this, and different
  /// micro-architectures can also be finely tuned inside.
  virtual bool isAsCheapAsAMove(const MachineInstr &MI) const {
    return MI.isAsCheapAsAMove();
  }

  /// Return true if the instruction should be sunk by MachineSink.
  ///
  /// MachineSink determines on its own whether the instruction is safe to sink;
  /// this gives the target a hook to override the default behavior with regards
  /// to which instructions should be sunk.
  virtual bool shouldSink(const MachineInstr &MI) const { return true; }

  /// Return false if the instruction should not be hoisted by MachineLICM.
  ///
  /// MachineLICM determines on its own whether the instruction is safe to
  /// hoist; this gives the target a hook to extend this assessment and prevent
  /// an instruction being hoisted from a given loop for target specific
  /// reasons.
  virtual bool shouldHoist(const MachineInstr &MI,
                           const MachineLoop *FromLoop) const {
    return true;
  }

  /// Re-issue the specified 'original' instruction at the
  /// specific location targeting a new destination register.
  /// The register in Orig->getOperand(0).getReg() will be substituted by
  /// DestReg:SubIdx. Any existing subreg index is preserved or composed with
  /// SubIdx.
  virtual void reMaterialize(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MI, Register DestReg,
                             unsigned SubIdx, const MachineInstr &Orig,
                             const TargetRegisterInfo &TRI) const;

  /// Clones instruction or the whole instruction bundle \p Orig and
  /// insert into \p MBB before \p InsertBefore. The target may update operands
  /// that are required to be unique.
  ///
  /// \p Orig must not return true for MachineInstr::isNotDuplicable().
  virtual MachineInstr &duplicate(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator InsertBefore,
                                  const MachineInstr &Orig) const;

  /// This method must be implemented by targets that
  /// set the M_CONVERTIBLE_TO_3_ADDR flag.  When this flag is set, the target
  /// may be able to convert a two-address instruction into one or more true
  /// three-address instructions on demand.  This allows the X86 target (for
  /// example) to convert ADD and SHL instructions into LEA instructions if they
  /// would require register copies due to two-addressness.
  ///
  /// This method returns a null pointer if the transformation cannot be
  /// performed, otherwise it returns the last new instruction.
  ///
  /// If \p LIS is not nullptr, the LiveIntervals info should be updated for
  /// replacing \p MI with new instructions, even though this function does not
  /// remove MI.
  virtual MachineInstr *convertToThreeAddress(MachineInstr &MI,
                                              LiveVariables *LV,
                                              LiveIntervals *LIS) const {
    return nullptr;
  }

  // This constant can be used as an input value of operand index passed to
  // the method findCommutedOpIndices() to tell the method that the
  // corresponding operand index is not pre-defined and that the method
  // can pick any commutable operand.
  static const unsigned CommuteAnyOperandIndex = ~0U;

  /// This method commutes the operands of the given machine instruction MI.
  ///
  /// The operands to be commuted are specified by their indices OpIdx1 and
  /// OpIdx2. OpIdx1 and OpIdx2 arguments may be set to a special value
  /// 'CommuteAnyOperandIndex', which means that the method is free to choose
  /// any arbitrarily chosen commutable operand. If both arguments are set to
  /// 'CommuteAnyOperandIndex' then the method looks for 2 different commutable
  /// operands; then commutes them if such operands could be found.
  ///
  /// If NewMI is false, MI is modified in place and returned; otherwise, a
  /// new machine instruction is created and returned.
  ///
  /// Do not call this method for a non-commutable instruction or
  /// for non-commuable operands.
  /// Even though the instruction is commutable, the method may still
  /// fail to commute the operands, null pointer is returned in such cases.
  MachineInstr *
  commuteInstruction(MachineInstr &MI, bool NewMI = false,
                     unsigned OpIdx1 = CommuteAnyOperandIndex,
                     unsigned OpIdx2 = CommuteAnyOperandIndex) const;

  /// Returns true iff the routine could find two commutable operands in the
  /// given machine instruction.
  /// The 'SrcOpIdx1' and 'SrcOpIdx2' are INPUT and OUTPUT arguments.
  /// If any of the INPUT values is set to the special value
  /// 'CommuteAnyOperandIndex' then the method arbitrarily picks a commutable
  /// operand, then returns its index in the corresponding argument.
  /// If both of INPUT values are set to 'CommuteAnyOperandIndex' then method
  /// looks for 2 commutable operands.
  /// If INPUT values refer to some operands of MI, then the method simply
  /// returns true if the corresponding operands are commutable and returns
  /// false otherwise.
  ///
  /// For example, calling this method this way:
  ///     unsigned Op1 = 1, Op2 = CommuteAnyOperandIndex;
  ///     findCommutedOpIndices(MI, Op1, Op2);
  /// can be interpreted as a query asking to find an operand that would be
  /// commutable with the operand#1.
  virtual bool findCommutedOpIndices(const MachineInstr &MI,
                                     unsigned &SrcOpIdx1,
                                     unsigned &SrcOpIdx2) const;

  /// Returns true if the target has a preference on the operands order of
  /// the given machine instruction. And specify if \p Commute is required to
  /// get the desired operands order.
  virtual bool hasCommutePreference(MachineInstr &MI, bool &Commute) const {
    return false;
  }

  /// A pair composed of a register and a sub-register index.
  /// Used to give some type checking when modeling Reg:SubReg.
  struct RegSubRegPair {
    Register Reg;
    unsigned SubReg;

    RegSubRegPair(Register Reg = Register(), unsigned SubReg = 0)
        : Reg(Reg), SubReg(SubReg) {}

    bool operator==(const RegSubRegPair& P) const {
      return Reg == P.Reg && SubReg == P.SubReg;
    }
    bool operator!=(const RegSubRegPair& P) const {
      return !(*this == P);
    }
  };

  /// A pair composed of a pair of a register and a sub-register index,
  /// and another sub-register index.
  /// Used to give some type checking when modeling Reg:SubReg1, SubReg2.
  struct RegSubRegPairAndIdx : RegSubRegPair {
    unsigned SubIdx;

    RegSubRegPairAndIdx(Register Reg = Register(), unsigned SubReg = 0,
                        unsigned SubIdx = 0)
        : RegSubRegPair(Reg, SubReg), SubIdx(SubIdx) {}
  };

  /// Build the equivalent inputs of a REG_SEQUENCE for the given \p MI
  /// and \p DefIdx.
  /// \p [out] InputRegs of the equivalent REG_SEQUENCE. Each element of
  /// the list is modeled as <Reg:SubReg, SubIdx>. Operands with the undef
  /// flag are not added to this list.
  /// E.g., REG_SEQUENCE %1:sub1, sub0, %2, sub1 would produce
  /// two elements:
  /// - %1:sub1, sub0
  /// - %2<:0>, sub1
  ///
  /// \returns true if it is possible to build such an input sequence
  /// with the pair \p MI, \p DefIdx. False otherwise.
  ///
  /// \pre MI.isRegSequence() or MI.isRegSequenceLike().
  ///
  /// \note The generic implementation does not provide any support for
  /// MI.isRegSequenceLike(). In other words, one has to override
  /// getRegSequenceLikeInputs for target specific instructions.
  bool
  getRegSequenceInputs(const MachineInstr &MI, unsigned DefIdx,
                       SmallVectorImpl<RegSubRegPairAndIdx> &InputRegs) const;

  /// Build the equivalent inputs of a EXTRACT_SUBREG for the given \p MI
  /// and \p DefIdx.
  /// \p [out] InputReg of the equivalent EXTRACT_SUBREG.
  /// E.g., EXTRACT_SUBREG %1:sub1, sub0, sub1 would produce:
  /// - %1:sub1, sub0
  ///
  /// \returns true if it is possible to build such an input sequence
  /// with the pair \p MI, \p DefIdx and the operand has no undef flag set.
  /// False otherwise.
  ///
  /// \pre MI.isExtractSubreg() or MI.isExtractSubregLike().
  ///
  /// \note The generic implementation does not provide any support for
  /// MI.isExtractSubregLike(). In other words, one has to override
  /// getExtractSubregLikeInputs for target specific instructions.
  bool getExtractSubregInputs(const MachineInstr &MI, unsigned DefIdx,
                              RegSubRegPairAndIdx &InputReg) const;

  /// Build the equivalent inputs of a INSERT_SUBREG for the given \p MI
  /// and \p DefIdx.
  /// \p [out] BaseReg and \p [out] InsertedReg contain
  /// the equivalent inputs of INSERT_SUBREG.
  /// E.g., INSERT_SUBREG %0:sub0, %1:sub1, sub3 would produce:
  /// - BaseReg: %0:sub0
  /// - InsertedReg: %1:sub1, sub3
  ///
  /// \returns true if it is possible to build such an input sequence
  /// with the pair \p MI, \p DefIdx and the operand has no undef flag set.
  /// False otherwise.
  ///
  /// \pre MI.isInsertSubreg() or MI.isInsertSubregLike().
  ///
  /// \note The generic implementation does not provide any support for
  /// MI.isInsertSubregLike(). In other words, one has to override
  /// getInsertSubregLikeInputs for target specific instructions.
  bool getInsertSubregInputs(const MachineInstr &MI, unsigned DefIdx,
                             RegSubRegPair &BaseReg,
                             RegSubRegPairAndIdx &InsertedReg) const;

  /// Return true if two machine instructions would produce identical values.
  /// By default, this is only true when the two instructions
  /// are deemed identical except for defs. If this function is called when the
  /// IR is still in SSA form, the caller can pass the MachineRegisterInfo for
  /// aggressive checks.
  virtual bool produceSameValue(const MachineInstr &MI0,
                                const MachineInstr &MI1,
                                const MachineRegisterInfo *MRI = nullptr) const;

  /// \returns true if a branch from an instruction with opcode \p BranchOpc
  ///  bytes is capable of jumping to a position \p BrOffset bytes away.
  virtual bool isBranchOffsetInRange(unsigned BranchOpc,
                                     int64_t BrOffset) const {
    llvm_unreachable("target did not implement");
  }

  /// \returns The block that branch instruction \p MI jumps to.
  virtual MachineBasicBlock *getBranchDestBlock(const MachineInstr &MI) const {
    llvm_unreachable("target did not implement");
  }

  /// Insert an unconditional indirect branch at the end of \p MBB to \p
  /// NewDestBB. Optionally, insert the clobbered register restoring in \p
  /// RestoreBB. \p BrOffset indicates the offset of \p NewDestBB relative to
  /// the offset of the position to insert the new branch.
  virtual void insertIndirectBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock &NewDestBB,
                                    MachineBasicBlock &RestoreBB,
                                    const DebugLoc &DL, int64_t BrOffset = 0,
                                    RegScavenger *RS = nullptr) const {
    llvm_unreachable("target did not implement");
  }

  /// Analyze the branching code at the end of MBB, returning
  /// true if it cannot be understood (e.g. it's a switch dispatch or isn't
  /// implemented for a target).  Upon success, this returns false and returns
  /// with the following information in various cases:
  ///
  /// 1. If this block ends with no branches (it just falls through to its succ)
  ///    just return false, leaving TBB/FBB null.
  /// 2. If this block ends with only an unconditional branch, it sets TBB to be
  ///    the destination block.
  /// 3. If this block ends with a conditional branch and it falls through to a
  ///    successor block, it sets TBB to be the branch destination block and a
  ///    list of operands that evaluate the condition. These operands can be
  ///    passed to other TargetInstrInfo methods to create new branches.
  /// 4. If this block ends with a conditional branch followed by an
  ///    unconditional branch, it returns the 'true' destination in TBB, the
  ///    'false' destination in FBB, and a list of operands that evaluate the
  ///    condition.  These operands can be passed to other TargetInstrInfo
  ///    methods to create new branches.
  ///
  /// Note that removeBranch and insertBranch must be implemented to support
  /// cases where this method returns success.
  ///
  /// If AllowModify is true, then this routine is allowed to modify the basic
  /// block (e.g. delete instructions after the unconditional branch).
  ///
  /// The CFG information in MBB.Predecessors and MBB.Successors must be valid
  /// before calling this function.
  virtual bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                             MachineBasicBlock *&FBB,
                             SmallVectorImpl<MachineOperand> &Cond,
                             bool AllowModify = false) const {
    return true;
  }

  /// Represents a predicate at the MachineFunction level.  The control flow a
  /// MachineBranchPredicate represents is:
  ///
  ///  Reg = LHS `Predicate` RHS         == ConditionDef
  ///  if Reg then goto TrueDest else goto FalseDest
  ///
  struct MachineBranchPredicate {
    enum ComparePredicate {
      PRED_EQ,     // True if two values are equal
      PRED_NE,     // True if two values are not equal
      PRED_INVALID // Sentinel value
    };

    ComparePredicate Predicate = PRED_INVALID;
    MachineOperand LHS = MachineOperand::CreateImm(0);
    MachineOperand RHS = MachineOperand::CreateImm(0);
    MachineBasicBlock *TrueDest = nullptr;
    MachineBasicBlock *FalseDest = nullptr;
    MachineInstr *ConditionDef = nullptr;

    /// SingleUseCondition is true if ConditionDef is dead except for the
    /// branch(es) at the end of the basic block.
    ///
    bool SingleUseCondition = false;

    explicit MachineBranchPredicate() = default;
  };

  /// Analyze the branching code at the end of MBB and parse it into the
  /// MachineBranchPredicate structure if possible.  Returns false on success
  /// and true on failure.
  ///
  /// If AllowModify is true, then this routine is allowed to modify the basic
  /// block (e.g. delete instructions after the unconditional branch).
  ///
  virtual bool analyzeBranchPredicate(MachineBasicBlock &MBB,
                                      MachineBranchPredicate &MBP,
                                      bool AllowModify = false) const {
    return true;
  }

  /// Remove the branching code at the end of the specific MBB.
  /// This is only invoked in cases where analyzeBranch returns success. It
  /// returns the number of instructions that were removed.
  /// If \p BytesRemoved is non-null, report the change in code size from the
  /// removed instructions.
  virtual unsigned removeBranch(MachineBasicBlock &MBB,
                                int *BytesRemoved = nullptr) const {
    llvm_unreachable("Target didn't implement TargetInstrInfo::removeBranch!");
  }

  /// Insert branch code into the end of the specified MachineBasicBlock. The
  /// operands to this method are the same as those returned by analyzeBranch.
  /// This is only invoked in cases where analyzeBranch returns success. It
  /// returns the number of instructions inserted. If \p BytesAdded is non-null,
  /// report the change in code size from the added instructions.
  ///
  /// It is also invoked by tail merging to add unconditional branches in
  /// cases where analyzeBranch doesn't apply because there was no original
  /// branch to analyze.  At least this much must be implemented, else tail
  /// merging needs to be disabled.
  ///
  /// The CFG information in MBB.Predecessors and MBB.Successors must be valid
  /// before calling this function.
  virtual unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                                MachineBasicBlock *FBB,
                                ArrayRef<MachineOperand> Cond,
                                const DebugLoc &DL,
                                int *BytesAdded = nullptr) const {
    llvm_unreachable("Target didn't implement TargetInstrInfo::insertBranch!");
  }

  unsigned insertUnconditionalBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *DestBB,
                                     const DebugLoc &DL,
                                     int *BytesAdded = nullptr) const {
    return insertBranch(MBB, DestBB, nullptr, ArrayRef<MachineOperand>(), DL,
                        BytesAdded);
  }

  /// Object returned by analyzeLoopForPipelining. Allows software pipelining
  /// implementations to query attributes of the loop being pipelined and to
  /// apply target-specific updates to the loop once pipelining is complete.
  class PipelinerLoopInfo {
  public:
    virtual ~PipelinerLoopInfo();
    /// Return true if the given instruction should not be pipelined and should
    /// be ignored. An example could be a loop comparison, or induction variable
    /// update with no users being pipelined.
    virtual bool shouldIgnoreForPipelining(const MachineInstr *MI) const = 0;

    /// Return true if the proposed schedule should used.  Otherwise return
    /// false to not pipeline the loop. This function should be used to ensure
    /// that pipelined loops meet target-specific quality heuristics.
    virtual bool shouldUseSchedule(SwingSchedulerDAG &SSD, SMSchedule &SMS) {
      return true;
    }

    /// Create a condition to determine if the trip count of the loop is greater
    /// than TC, where TC is always one more than for the previous prologue or
    /// 0 if this is being called for the outermost prologue.
    ///
    /// If the trip count is statically known to be greater than TC, return
    /// true. If the trip count is statically known to be not greater than TC,
    /// return false. Otherwise return nullopt and fill out Cond with the test
    /// condition.
    ///
    /// Note: This hook is guaranteed to be called from the innermost to the
    /// outermost prologue of the loop being software pipelined.
    virtual std::optional<bool>
    createTripCountGreaterCondition(int TC, MachineBasicBlock &MBB,
                                    SmallVectorImpl<MachineOperand> &Cond) = 0;

    /// Create a condition to determine if the remaining trip count for a phase
    /// is greater than TC. Some instructions such as comparisons may be
    /// inserted at the bottom of MBB. All instructions expanded for the
    /// phase must be inserted in MBB before calling this function.
    /// LastStage0Insts is the map from the original instructions scheduled at
    /// stage#0 to the expanded instructions for the last iteration of the
    /// kernel. LastStage0Insts is intended to obtain the instruction that
    /// refers the latest loop counter value.
    ///
    /// MBB can also be a predecessor of the prologue block. Then
    /// LastStage0Insts must be empty and the compared value is the initial
    /// value of the trip count.
    virtual void createRemainingIterationsGreaterCondition(
        int TC, MachineBasicBlock &MBB, SmallVectorImpl<MachineOperand> &Cond,
        DenseMap<MachineInstr *, MachineInstr *> &LastStage0Insts) {
      llvm_unreachable(
          "Target didn't implement "
          "PipelinerLoopInfo::createRemainingIterationsGreaterCondition!");
    }

    /// Modify the loop such that the trip count is
    /// OriginalTC + TripCountAdjust.
    virtual void adjustTripCount(int TripCountAdjust) = 0;

    /// Called when the loop's preheader has been modified to NewPreheader.
    virtual void setPreheader(MachineBasicBlock *NewPreheader) = 0;

    /// Called when the loop is being removed. Any instructions in the preheader
    /// should be removed.
    ///
    /// Once this function is called, no other functions on this object are
    /// valid; the loop has been removed.
    virtual void disposed() = 0;

    /// Return true if the target can expand pipelined schedule with modulo
    /// variable expansion.
    virtual bool isMVEExpanderSupported() { return false; }
  };

  /// Analyze loop L, which must be a single-basic-block loop, and if the
  /// conditions can be understood enough produce a PipelinerLoopInfo object.
  virtual std::unique_ptr<PipelinerLoopInfo>
  analyzeLoopForPipelining(MachineBasicBlock *LoopBB) const {
    return nullptr;
  }

  /// Analyze the loop code, return true if it cannot be understood. Upon
  /// success, this function returns false and returns information about the
  /// induction variable and compare instruction used at the end.
  virtual bool analyzeLoop(MachineLoop &L, MachineInstr *&IndVarInst,
                           MachineInstr *&CmpInst) const {
    return true;
  }

  /// Generate code to reduce the loop iteration by one and check if the loop
  /// is finished.  Return the value/register of the new loop count.  We need
  /// this function when peeling off one or more iterations of a loop. This
  /// function assumes the nth iteration is peeled first.
  virtual unsigned reduceLoopCount(MachineBasicBlock &MBB,
                                   MachineBasicBlock &PreHeader,
                                   MachineInstr *IndVar, MachineInstr &Cmp,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   SmallVectorImpl<MachineInstr *> &PrevInsts,
                                   unsigned Iter, unsigned MaxIter) const {
    llvm_unreachable("Target didn't implement ReduceLoopCount");
  }

  /// Delete the instruction OldInst and everything after it, replacing it with
  /// an unconditional branch to NewDest. This is used by the tail merging pass.
  virtual void ReplaceTailWithBranchTo(MachineBasicBlock::iterator Tail,
                                       MachineBasicBlock *NewDest) const;

  /// Return true if it's legal to split the given basic
  /// block at the specified instruction (i.e. instruction would be the start
  /// of a new basic block).
  virtual bool isLegalToSplitMBBAt(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI) const {
    return true;
  }

  /// Return true if it's profitable to predicate
  /// instructions with accumulated instruction latency of "NumCycles"
  /// of the specified basic block, where the probability of the instructions
  /// being executed is given by Probability, and Confidence is a measure
  /// of our confidence that it will be properly predicted.
  virtual bool isProfitableToIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                                   unsigned ExtraPredCycles,
                                   BranchProbability Probability) const {
    return false;
  }

  /// Second variant of isProfitableToIfCvt. This one
  /// checks for the case where two basic blocks from true and false path
  /// of a if-then-else (diamond) are predicated on mutually exclusive
  /// predicates, where the probability of the true path being taken is given
  /// by Probability, and Confidence is a measure of our confidence that it
  /// will be properly predicted.
  virtual bool isProfitableToIfCvt(MachineBasicBlock &TMBB, unsigned NumTCycles,
                                   unsigned ExtraTCycles,
                                   MachineBasicBlock &FMBB, unsigned NumFCycles,
                                   unsigned ExtraFCycles,
                                   BranchProbability Probability) const {
    return false;
  }

  /// Return true if it's profitable for if-converter to duplicate instructions
  /// of specified accumulated instruction latencies in the specified MBB to
  /// enable if-conversion.
  /// The probability of the instructions being executed is given by
  /// Probability, and Confidence is a measure of our confidence that it
  /// will be properly predicted.
  virtual bool isProfitableToDupForIfCvt(MachineBasicBlock &MBB,
                                         unsigned NumCycles,
                                         BranchProbability Probability) const {
    return false;
  }

  /// Return the increase in code size needed to predicate a contiguous run of
  /// NumInsts instructions.
  virtual unsigned extraSizeToPredicateInstructions(const MachineFunction &MF,
                                                    unsigned NumInsts) const {
    return 0;
  }

  /// Return an estimate for the code size reduction (in bytes) which will be
  /// caused by removing the given branch instruction during if-conversion.
  virtual unsigned predictBranchSizeForIfCvt(MachineInstr &MI) const {
    return getInstSizeInBytes(MI);
  }

  /// Return true if it's profitable to unpredicate
  /// one side of a 'diamond', i.e. two sides of if-else predicated on mutually
  /// exclusive predicates.
  /// e.g.
  ///   subeq  r0, r1, #1
  ///   addne  r0, r1, #1
  /// =>
  ///   sub    r0, r1, #1
  ///   addne  r0, r1, #1
  ///
  /// This may be profitable is conditional instructions are always executed.
  virtual bool isProfitableToUnpredicate(MachineBasicBlock &TMBB,
                                         MachineBasicBlock &FMBB) const {
    return false;
  }

  /// Return true if it is possible to insert a select
  /// instruction that chooses between TrueReg and FalseReg based on the
  /// condition code in Cond.
  ///
  /// When successful, also return the latency in cycles from TrueReg,
  /// FalseReg, and Cond to the destination register. In most cases, a select
  /// instruction will be 1 cycle, so CondCycles = TrueCycles = FalseCycles = 1
  ///
  /// Some x86 implementations have 2-cycle cmov instructions.
  ///
  /// @param MBB         Block where select instruction would be inserted.
  /// @param Cond        Condition returned by analyzeBranch.
  /// @param DstReg      Virtual dest register that the result should write to.
  /// @param TrueReg     Virtual register to select when Cond is true.
  /// @param FalseReg    Virtual register to select when Cond is false.
  /// @param CondCycles  Latency from Cond+Branch to select output.
  /// @param TrueCycles  Latency from TrueReg to select output.
  /// @param FalseCycles Latency from FalseReg to select output.
  virtual bool canInsertSelect(const MachineBasicBlock &MBB,
                               ArrayRef<MachineOperand> Cond, Register DstReg,
                               Register TrueReg, Register FalseReg,
                               int &CondCycles, int &TrueCycles,
                               int &FalseCycles) const {
    return false;
  }

  /// Insert a select instruction into MBB before I that will copy TrueReg to
  /// DstReg when Cond is true, and FalseReg to DstReg when Cond is false.
  ///
  /// This function can only be called after canInsertSelect() returned true.
  /// The condition in Cond comes from analyzeBranch, and it can be assumed
  /// that the same flags or registers required by Cond are available at the
  /// insertion point.
  ///
  /// @param MBB      Block where select instruction should be inserted.
  /// @param I        Insertion point.
  /// @param DL       Source location for debugging.
  /// @param DstReg   Virtual register to be defined by select instruction.
  /// @param Cond     Condition as computed by analyzeBranch.
  /// @param TrueReg  Virtual register to copy when Cond is true.
  /// @param FalseReg Virtual register to copy when Cons is false.
  virtual void insertSelect(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator I, const DebugLoc &DL,
                            Register DstReg, ArrayRef<MachineOperand> Cond,
                            Register TrueReg, Register FalseReg) const {
    llvm_unreachable("Target didn't implement TargetInstrInfo::insertSelect!");
  }

  /// Analyze the given select instruction, returning true if
  /// it cannot be understood. It is assumed that MI->isSelect() is true.
  ///
  /// When successful, return the controlling condition and the operands that
  /// determine the true and false result values.
  ///
  ///   Result = SELECT Cond, TrueOp, FalseOp
  ///
  /// Some targets can optimize select instructions, for example by predicating
  /// the instruction defining one of the operands. Such targets should set
  /// Optimizable.
  ///
  /// @param         MI Select instruction to analyze.
  /// @param Cond    Condition controlling the select.
  /// @param TrueOp  Operand number of the value selected when Cond is true.
  /// @param FalseOp Operand number of the value selected when Cond is false.
  /// @param Optimizable Returned as true if MI is optimizable.
  /// @returns False on success.
  virtual bool analyzeSelect(const MachineInstr &MI,
                             SmallVectorImpl<MachineOperand> &Cond,
                             unsigned &TrueOp, unsigned &FalseOp,
                             bool &Optimizable) const {
    assert(MI.getDesc().isSelect() && "MI must be a select instruction");
    return true;
  }

  /// Given a select instruction that was understood by
  /// analyzeSelect and returned Optimizable = true, attempt to optimize MI by
  /// merging it with one of its operands. Returns NULL on failure.
  ///
  /// When successful, returns the new select instruction. The client is
  /// responsible for deleting MI.
  ///
  /// If both sides of the select can be optimized, PreferFalse is used to pick
  /// a side.
  ///
  /// @param MI          Optimizable select instruction.
  /// @param NewMIs     Set that record all MIs in the basic block up to \p
  /// MI. Has to be updated with any newly created MI or deleted ones.
  /// @param PreferFalse Try to optimize FalseOp instead of TrueOp.
  /// @returns Optimized instruction or NULL.
  virtual MachineInstr *optimizeSelect(MachineInstr &MI,
                                       SmallPtrSetImpl<MachineInstr *> &NewMIs,
                                       bool PreferFalse = false) const {
    // This function must be implemented if Optimizable is ever set.
    llvm_unreachable("Target must implement TargetInstrInfo::optimizeSelect!");
  }

  /// Emit instructions to copy a pair of physical registers.
  ///
  /// This function should support copies within any legal register class as
  /// well as any cross-class copies created during instruction selection.
  ///
  /// The source and destination registers may overlap, which may require a
  /// careful implementation when multiple copy instructions are required for
  /// large registers. See for example the ARM target.
  virtual void copyPhysReg(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, const DebugLoc &DL,
                           MCRegister DestReg, MCRegister SrcReg,
                           bool KillSrc) const {
    llvm_unreachable("Target didn't implement TargetInstrInfo::copyPhysReg!");
  }

  /// Allow targets to tell MachineVerifier whether a specific register
  /// MachineOperand can be used as part of PC-relative addressing.
  /// PC-relative addressing modes in many CISC architectures contain
  /// (non-PC) registers as offsets or scaling values, which inherently
  /// tags the corresponding MachineOperand with OPERAND_PCREL.
  ///
  /// @param MO The MachineOperand in question. MO.isReg() should always
  /// be true.
  /// @return Whether this operand is allowed to be used PC-relatively.
  virtual bool isPCRelRegisterOperandLegal(const MachineOperand &MO) const {
    return false;
  }

  /// Return an index for MachineJumpTableInfo if \p insn is an indirect jump
  /// using a jump table, otherwise -1.
  virtual int getJumpTableIndex(const MachineInstr &MI) const { return -1; }

protected:
  /// Target-dependent implementation for IsCopyInstr.
  /// If the specific machine instruction is a instruction that moves/copies
  /// value from one register to another register return destination and source
  /// registers as machine operands.
  virtual std::optional<DestSourcePair>
  isCopyInstrImpl(const MachineInstr &MI) const {
    return std::nullopt;
  }

  virtual std::optional<DestSourcePair>
  isCopyLikeInstrImpl(const MachineInstr &MI) const {
    return std::nullopt;
  }

  /// Return true if the given terminator MI is not expected to spill. This
  /// sets the live interval as not spillable and adjusts phi node lowering to
  /// not introduce copies after the terminator. Use with care, these are
  /// currently used for hardware loop intrinsics in very controlled situations,
  /// created prior to registry allocation in loops that only have single phi
  /// users for the terminators value. They may run out of registers if not used
  /// carefully.
  virtual bool isUnspillableTerminatorImpl(const MachineInstr *MI) const {
    return false;
  }

public:
  /// If the specific machine instruction is a instruction that moves/copies
  /// value from one register to another register return destination and source
  /// registers as machine operands.
  /// For COPY-instruction the method naturally returns destination and source
  /// registers as machine operands, for all other instructions the method calls
  /// target-dependent implementation.
  std::optional<DestSourcePair> isCopyInstr(const MachineInstr &MI) const {
    if (MI.isCopy()) {
      return DestSourcePair{MI.getOperand(0), MI.getOperand(1)};
    }
    return isCopyInstrImpl(MI);
  }

  // Similar to `isCopyInstr`, but adds non-copy semantics on MIR, but
  // ultimately generates a copy instruction.
  std::optional<DestSourcePair> isCopyLikeInstr(const MachineInstr &MI) const {
    if (auto IsCopyInstr = isCopyInstr(MI))
      return IsCopyInstr;
    return isCopyLikeInstrImpl(MI);
  }

  bool isFullCopyInstr(const MachineInstr &MI) const {
    auto DestSrc = isCopyInstr(MI);
    if (!DestSrc)
      return false;

    const MachineOperand *DestRegOp = DestSrc->Destination;
    const MachineOperand *SrcRegOp = DestSrc->Source;
    return !DestRegOp->getSubReg() && !SrcRegOp->getSubReg();
  }

  /// If the specific machine instruction is an instruction that adds an
  /// immediate value and a register, and stores the result in the given
  /// register \c Reg, return a pair of the source register and the offset
  /// which has been added.
  virtual std::optional<RegImmPair> isAddImmediate(const MachineInstr &MI,
                                                   Register Reg) const {
    return std::nullopt;
  }

  /// Returns true if MI is an instruction that defines Reg to have a constant
  /// value and the value is recorded in ImmVal. The ImmVal is a result that
  /// should be interpreted as modulo size of Reg.
  virtual bool getConstValDefinedInReg(const MachineInstr &MI,
                                       const Register Reg,
                                       int64_t &ImmVal) const {
    return false;
  }

  /// Store the specified register of the given register class to the specified
  /// stack frame index. The store instruction is to be added to the given
  /// machine basic block before the specified machine instruction. If isKill
  /// is true, the register operand is the last use and must be marked kill. If
  /// \p SrcReg is being directly spilled as part of assigning a virtual
  /// register, \p VReg is the register being assigned. This additional register
  /// argument is needed for certain targets when invoked from RegAllocFast to
  /// map the spilled physical register to its virtual register. A null register
  /// can be passed elsewhere.
  virtual void storeRegToStackSlot(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI,
                                   Register SrcReg, bool isKill, int FrameIndex,
                                   const TargetRegisterClass *RC,
                                   const TargetRegisterInfo *TRI,
                                   Register VReg) const {
    llvm_unreachable("Target didn't implement "
                     "TargetInstrInfo::storeRegToStackSlot!");
  }

  /// Load the specified register of the given register class from the specified
  /// stack frame index. The load instruction is to be added to the given
  /// machine basic block before the specified machine instruction. If \p
  /// DestReg is being directly reloaded as part of assigning a virtual
  /// register, \p VReg is the register being assigned. This additional register
  /// argument is needed for certain targets when invoked from RegAllocFast to
  /// map the loaded physical register to its virtual register. A null register
  /// can be passed elsewhere.
  virtual void loadRegFromStackSlot(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MI,
                                    Register DestReg, int FrameIndex,
                                    const TargetRegisterClass *RC,
                                    const TargetRegisterInfo *TRI,
                                    Register VReg) const {
    llvm_unreachable("Target didn't implement "
                     "TargetInstrInfo::loadRegFromStackSlot!");
  }

  /// This function is called for all pseudo instructions
  /// that remain after register allocation. Many pseudo instructions are
  /// created to help register allocation. This is the place to convert them
  /// into real instructions. The target can edit MI in place, or it can insert
  /// new instructions and erase MI. The function should return true if
  /// anything was changed.
  virtual bool expandPostRAPseudo(MachineInstr &MI) const { return false; }

  /// Check whether the target can fold a load that feeds a subreg operand
  /// (or a subreg operand that feeds a store).
  /// For example, X86 may want to return true if it can fold
  /// movl (%esp), %eax
  /// subb, %al, ...
  /// Into:
  /// subb (%esp), ...
  ///
  /// Ideally, we'd like the target implementation of foldMemoryOperand() to
  /// reject subregs - but since this behavior used to be enforced in the
  /// target-independent code, moving this responsibility to the targets
  /// has the potential of causing nasty silent breakage in out-of-tree targets.
  virtual bool isSubregFoldable() const { return false; }

  /// For a patchpoint, stackmap, or statepoint intrinsic, return the range of
  /// operands which can't be folded into stack references. Operands outside
  /// of the range are most likely foldable but it is not guaranteed.
  /// These instructions are unique in that stack references for some operands
  /// have the same execution cost (e.g. none) as the unfolded register forms.
  /// The ranged return is guaranteed to include all operands which can't be
  /// folded at zero cost.
  virtual std::pair<unsigned, unsigned>
  getPatchpointUnfoldableRange(const MachineInstr &MI) const;

  /// Attempt to fold a load or store of the specified stack
  /// slot into the specified machine instruction for the specified operand(s).
  /// If this is possible, a new instruction is returned with the specified
  /// operand folded, otherwise NULL is returned.
  /// The new instruction is inserted before MI, and the client is responsible
  /// for removing the old instruction.
  /// If VRM is passed, the assigned physregs can be inspected by target to
  /// decide on using an opcode (note that those assignments can still change).
  MachineInstr *foldMemoryOperand(MachineInstr &MI, ArrayRef<unsigned> Ops,
                                  int FI,
                                  LiveIntervals *LIS = nullptr,
                                  VirtRegMap *VRM = nullptr) const;

  /// Same as the previous version except it allows folding of any load and
  /// store from / to any address, not just from a specific stack slot.
  MachineInstr *foldMemoryOperand(MachineInstr &MI, ArrayRef<unsigned> Ops,
                                  MachineInstr &LoadMI,
                                  LiveIntervals *LIS = nullptr) const;

  /// This function defines the logic to lower COPY instruction to
  /// target specific instruction(s).
  void lowerCopy(MachineInstr *MI, const TargetRegisterInfo *TRI) const;

  /// Return true when there is potentially a faster code sequence
  /// for an instruction chain ending in \p Root. All potential patterns are
  /// returned in the \p Pattern vector. Pattern should be sorted in priority
  /// order since the pattern evaluator stops checking as soon as it finds a
  /// faster sequence.
  /// \param Root - Instruction that could be combined with one of its operands
  /// \param Patterns - Vector of possible combination patterns
  virtual bool getMachineCombinerPatterns(MachineInstr &Root,
                                          SmallVectorImpl<unsigned> &Patterns,
                                          bool DoRegPressureReduce) const;

  /// Return true if target supports reassociation of instructions in machine
  /// combiner pass to reduce register pressure for a given BB.
  virtual bool
  shouldReduceRegisterPressure(const MachineBasicBlock *MBB,
                               const RegisterClassInfo *RegClassInfo) const {
    return false;
  }

  /// Fix up the placeholder we may add in genAlternativeCodeSequence().
  virtual void
  finalizeInsInstrs(MachineInstr &Root, unsigned &Pattern,
                    SmallVectorImpl<MachineInstr *> &InsInstrs) const {}

  /// Return true when a code sequence can improve throughput. It
  /// should be called only for instructions in loops.
  /// \param Pattern - combiner pattern
  virtual bool isThroughputPattern(unsigned Pattern) const;

  /// Return the objective of a combiner pattern.
  /// \param Pattern - combiner pattern
  virtual CombinerObjective getCombinerObjective(unsigned Pattern) const;

  /// Return true if the input \P Inst is part of a chain of dependent ops
  /// that are suitable for reassociation, otherwise return false.
  /// If the instruction's operands must be commuted to have a previous
  /// instruction of the same type define the first source operand, \P Commuted
  /// will be set to true.
  bool isReassociationCandidate(const MachineInstr &Inst, bool &Commuted) const;

  /// Return true when \P Inst is both associative and commutative. If \P Invert
  /// is true, then the inverse of \P Inst operation must be tested.
  virtual bool isAssociativeAndCommutative(const MachineInstr &Inst,
                                           bool Invert = false) const {
    return false;
  }

  /// Return the inverse operation opcode if it exists for \P Opcode (e.g. add
  /// for sub and vice versa).
  virtual std::optional<unsigned> getInverseOpcode(unsigned Opcode) const {
    return std::nullopt;
  }

  /// Return true when \P Opcode1 or its inversion is equal to \P Opcode2.
  bool areOpcodesEqualOrInverse(unsigned Opcode1, unsigned Opcode2) const;

  /// Return true when \P Inst has reassociable operands in the same \P MBB.
  virtual bool hasReassociableOperands(const MachineInstr &Inst,
                                       const MachineBasicBlock *MBB) const;

  /// Return true when \P Inst has reassociable sibling.
  virtual bool hasReassociableSibling(const MachineInstr &Inst,
                                      bool &Commuted) const;

  /// When getMachineCombinerPatterns() finds patterns, this function generates
  /// the instructions that could replace the original code sequence. The client
  /// has to decide whether the actual replacement is beneficial or not.
  /// \param Root - Instruction that could be combined with one of its operands
  /// \param Pattern - Combination pattern for Root
  /// \param InsInstrs - Vector of new instructions that implement P
  /// \param DelInstrs - Old instructions, including Root, that could be
  /// replaced by InsInstr
  /// \param InstIdxForVirtReg - map of virtual register to instruction in
  /// InsInstr that defines it
  virtual void genAlternativeCodeSequence(
      MachineInstr &Root, unsigned Pattern,
      SmallVectorImpl<MachineInstr *> &InsInstrs,
      SmallVectorImpl<MachineInstr *> &DelInstrs,
      DenseMap<unsigned, unsigned> &InstIdxForVirtReg) const;

  /// When calculate the latency of the root instruction, accumulate the
  /// latency of the sequence to the root latency.
  /// \param Root - Instruction that could be combined with one of its operands
  virtual bool accumulateInstrSeqToRootLatency(MachineInstr &Root) const {
    return true;
  }

  /// The returned array encodes the operand index for each parameter because
  /// the operands may be commuted; the operand indices for associative
  /// operations might also be target-specific. Each element specifies the index
  /// of {Prev, A, B, X, Y}.
  virtual void
  getReassociateOperandIndices(const MachineInstr &Root, unsigned Pattern,
                               std::array<unsigned, 5> &OperandIndices) const;

  /// Attempt to reassociate \P Root and \P Prev according to \P Pattern to
  /// reduce critical path length.
  void reassociateOps(MachineInstr &Root, MachineInstr &Prev, unsigned Pattern,
                      SmallVectorImpl<MachineInstr *> &InsInstrs,
                      SmallVectorImpl<MachineInstr *> &DelInstrs,
                      ArrayRef<unsigned> OperandIndices,
                      DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) const;

  /// Reassociation of some instructions requires inverse operations (e.g.
  /// (X + A) - Y => (X - Y) + A). This method returns a pair of new opcodes
  /// (new root opcode, new prev opcode) that must be used to reassociate \P
  /// Root and \P Prev accoring to \P Pattern.
  std::pair<unsigned, unsigned>
  getReassociationOpcodes(unsigned Pattern, const MachineInstr &Root,
                          const MachineInstr &Prev) const;

  /// The limit on resource length extension we accept in MachineCombiner Pass.
  virtual int getExtendResourceLenLimit() const { return 0; }

  /// This is an architecture-specific helper function of reassociateOps.
  /// Set special operand attributes for new instructions after reassociation.
  virtual void setSpecialOperandAttr(MachineInstr &OldMI1, MachineInstr &OldMI2,
                                     MachineInstr &NewMI1,
                                     MachineInstr &NewMI2) const {}

  /// Return true when a target supports MachineCombiner.
  virtual bool useMachineCombiner() const { return false; }

  /// Return a strategy that MachineCombiner must use when creating traces.
  virtual MachineTraceStrategy getMachineCombinerTraceStrategy() const;

  /// Return true if the given SDNode can be copied during scheduling
  /// even if it has glue.
  virtual bool canCopyGluedNodeDuringSchedule(SDNode *N) const { return false; }

protected:
  /// Target-dependent implementation for foldMemoryOperand.
  /// Target-independent code in foldMemoryOperand will
  /// take care of adding a MachineMemOperand to the newly created instruction.
  /// The instruction and any auxiliary instructions necessary will be inserted
  /// at InsertPt.
  virtual MachineInstr *
  foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                        ArrayRef<unsigned> Ops,
                        MachineBasicBlock::iterator InsertPt, int FrameIndex,
                        LiveIntervals *LIS = nullptr,
                        VirtRegMap *VRM = nullptr) const {
    return nullptr;
  }

  /// Target-dependent implementation for foldMemoryOperand.
  /// Target-independent code in foldMemoryOperand will
  /// take care of adding a MachineMemOperand to the newly created instruction.
  /// The instruction and any auxiliary instructions necessary will be inserted
  /// at InsertPt.
  virtual MachineInstr *foldMemoryOperandImpl(
      MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
      MachineBasicBlock::iterator InsertPt, MachineInstr &LoadMI,
      LiveIntervals *LIS = nullptr) const {
    return nullptr;
  }

  /// Target-dependent implementation of getRegSequenceInputs.
  ///
  /// \returns true if it is possible to build the equivalent
  /// REG_SEQUENCE inputs with the pair \p MI, \p DefIdx. False otherwise.
  ///
  /// \pre MI.isRegSequenceLike().
  ///
  /// \see TargetInstrInfo::getRegSequenceInputs.
  virtual bool getRegSequenceLikeInputs(
      const MachineInstr &MI, unsigned DefIdx,
      SmallVectorImpl<RegSubRegPairAndIdx> &InputRegs) const {
    return false;
  }

  /// Target-dependent implementation of getExtractSubregInputs.
  ///
  /// \returns true if it is possible to build the equivalent
  /// EXTRACT_SUBREG inputs with the pair \p MI, \p DefIdx. False otherwise.
  ///
  /// \pre MI.isExtractSubregLike().
  ///
  /// \see TargetInstrInfo::getExtractSubregInputs.
  virtual bool getExtractSubregLikeInputs(const MachineInstr &MI,
                                          unsigned DefIdx,
                                          RegSubRegPairAndIdx &InputReg) const {
    return false;
  }

  /// Target-dependent implementation of getInsertSubregInputs.
  ///
  /// \returns true if it is possible to build the equivalent
  /// INSERT_SUBREG inputs with the pair \p MI, \p DefIdx. False otherwise.
  ///
  /// \pre MI.isInsertSubregLike().
  ///
  /// \see TargetInstrInfo::getInsertSubregInputs.
  virtual bool
  getInsertSubregLikeInputs(const MachineInstr &MI, unsigned DefIdx,
                            RegSubRegPair &BaseReg,
                            RegSubRegPairAndIdx &InsertedReg) const {
    return false;
  }

public:
  /// unfoldMemoryOperand - Separate a single instruction which folded a load or
  /// a store or a load and a store into two or more instruction. If this is
  /// possible, returns true as well as the new instructions by reference.
  virtual bool
  unfoldMemoryOperand(MachineFunction &MF, MachineInstr &MI, unsigned Reg,
                      bool UnfoldLoad, bool UnfoldStore,
                      SmallVectorImpl<MachineInstr *> &NewMIs) const {
    return false;
  }

  virtual bool unfoldMemoryOperand(SelectionDAG &DAG, SDNode *N,
                                   SmallVectorImpl<SDNode *> &NewNodes) const {
    return false;
  }

  /// Returns the opcode of the would be new
  /// instruction after load / store are unfolded from an instruction of the
  /// specified opcode. It returns zero if the specified unfolding is not
  /// possible. If LoadRegIndex is non-null, it is filled in with the operand
  /// index of the operand which will hold the register holding the loaded
  /// value.
  virtual unsigned
  getOpcodeAfterMemoryUnfold(unsigned Opc, bool UnfoldLoad, bool UnfoldStore,
                             unsigned *LoadRegIndex = nullptr) const {
    return 0;
  }

  /// This is used by the pre-regalloc scheduler to determine if two loads are
  /// loading from the same base address. It should only return true if the base
  /// pointers are the same and the only differences between the two addresses
  /// are the offset. It also returns the offsets by reference.
  virtual bool areLoadsFromSameBasePtr(SDNode *Load1, SDNode *Load2,
                                       int64_t &Offset1,
                                       int64_t &Offset2) const {
    return false;
  }

  /// This is a used by the pre-regalloc scheduler to determine (in conjunction
  /// with areLoadsFromSameBasePtr) if two loads should be scheduled together.
  /// On some targets if two loads are loading from
  /// addresses in the same cache line, it's better if they are scheduled
  /// together. This function takes two integers that represent the load offsets
  /// from the common base address. It returns true if it decides it's desirable
  /// to schedule the two loads together. "NumLoads" is the number of loads that
  /// have already been scheduled after Load1.
  virtual bool shouldScheduleLoadsNear(SDNode *Load1, SDNode *Load2,
                                       int64_t Offset1, int64_t Offset2,
                                       unsigned NumLoads) const {
    return false;
  }

  /// Get the base operand and byte offset of an instruction that reads/writes
  /// memory. This is a convenience function for callers that are only prepared
  /// to handle a single base operand.
  /// FIXME: Move Offset and OffsetIsScalable to some ElementCount-style
  /// abstraction that supports negative offsets.
  bool getMemOperandWithOffset(const MachineInstr &MI,
                               const MachineOperand *&BaseOp, int64_t &Offset,
                               bool &OffsetIsScalable,
                               const TargetRegisterInfo *TRI) const;

  /// Get zero or more base operands and the byte offset of an instruction that
  /// reads/writes memory. Note that there may be zero base operands if the
  /// instruction accesses a constant address.
  /// It returns false if MI does not read/write memory.
  /// It returns false if base operands and offset could not be determined.
  /// It is not guaranteed to always recognize base operands and offsets in all
  /// cases.
  /// FIXME: Move Offset and OffsetIsScalable to some ElementCount-style
  /// abstraction that supports negative offsets.
  virtual bool getMemOperandsWithOffsetWidth(
      const MachineInstr &MI, SmallVectorImpl<const MachineOperand *> &BaseOps,
      int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
      const TargetRegisterInfo *TRI) const {
    return false;
  }

  /// Return true if the instruction contains a base register and offset. If
  /// true, the function also sets the operand position in the instruction
  /// for the base register and offset.
  virtual bool getBaseAndOffsetPosition(const MachineInstr &MI,
                                        unsigned &BasePos,
                                        unsigned &OffsetPos) const {
    return false;
  }

  /// Target dependent implementation to get the values constituting the address
  /// MachineInstr that is accessing memory. These values are returned as a
  /// struct ExtAddrMode which contains all relevant information to make up the
  /// address.
  virtual std::optional<ExtAddrMode>
  getAddrModeFromMemoryOp(const MachineInstr &MemI,
                          const TargetRegisterInfo *TRI) const {
    return std::nullopt;
  }

  /// Check if it's possible and beneficial to fold the addressing computation
  /// `AddrI` into the addressing mode of the load/store instruction `MemI`. The
  /// memory instruction is a user of the virtual register `Reg`, which in turn
  /// is the ultimate destination of zero or more COPY instructions from the
  /// output register of `AddrI`.
  /// Return the adddressing mode after folding in `AM`.
  virtual bool canFoldIntoAddrMode(const MachineInstr &MemI, Register Reg,
                                   const MachineInstr &AddrI,
                                   ExtAddrMode &AM) const {
    return false;
  }

  /// Emit a load/store instruction with the same value register as `MemI`, but
  /// using the address from `AM`. The addressing mode must have been obtained
  /// from `canFoldIntoAddr` for the same memory instruction.
  virtual MachineInstr *emitLdStWithAddr(MachineInstr &MemI,
                                         const ExtAddrMode &AM) const {
    llvm_unreachable("target did not implement emitLdStWithAddr()");
  }

  /// Returns true if MI's Def is NullValueReg, and the MI
  /// does not change the Zero value. i.e. cases such as rax = shr rax, X where
  /// NullValueReg = rax. Note that if the NullValueReg is non-zero, this
  /// function can return true even if becomes zero. Specifically cases such as
  /// NullValueReg = shl NullValueReg, 63.
  virtual bool preservesZeroValueInReg(const MachineInstr *MI,
                                       const Register NullValueReg,
                                       const TargetRegisterInfo *TRI) const {
    return false;
  }

  /// If the instruction is an increment of a constant value, return the amount.
  virtual bool getIncrementValue(const MachineInstr &MI, int &Value) const {
    return false;
  }

  /// Returns true if the two given memory operations should be scheduled
  /// adjacent. Note that you have to add:
  ///   DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  /// or
  ///   DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  /// to TargetPassConfig::createMachineScheduler() to have an effect.
  ///
  /// \p BaseOps1 and \p BaseOps2 are memory operands of two memory operations.
  /// \p Offset1 and \p Offset2 are the byte offsets for the memory
  /// operations.
  /// \p OffsetIsScalable1 and \p OffsetIsScalable2 indicate if the offset is
  /// scaled by a runtime quantity.
  /// \p ClusterSize is the number of operations in the resulting load/store
  /// cluster if this hook returns true.
  /// \p NumBytes is the number of bytes that will be loaded from all the
  /// clustered loads if this hook returns true.
  virtual bool shouldClusterMemOps(ArrayRef<const MachineOperand *> BaseOps1,
                                   int64_t Offset1, bool OffsetIsScalable1,
                                   ArrayRef<const MachineOperand *> BaseOps2,
                                   int64_t Offset2, bool OffsetIsScalable2,
                                   unsigned ClusterSize,
                                   unsigned NumBytes) const {
    llvm_unreachable("target did not implement shouldClusterMemOps()");
  }

  /// Reverses the branch condition of the specified condition list,
  /// returning false on success and true if it cannot be reversed.
  virtual bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
    return true;
  }

  /// Insert a noop into the instruction stream at the specified point.
  virtual void insertNoop(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MI) const;

  /// Insert noops into the instruction stream at the specified point.
  virtual void insertNoops(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI,
                           unsigned Quantity) const;

  /// Return the noop instruction to use for a noop.
  virtual MCInst getNop() const;

  /// Return true for post-incremented instructions.
  virtual bool isPostIncrement(const MachineInstr &MI) const { return false; }

  /// Returns true if the instruction is already predicated.
  virtual bool isPredicated(const MachineInstr &MI) const { return false; }

  /// Assumes the instruction is already predicated and returns true if the
  /// instruction can be predicated again.
  virtual bool canPredicatePredicatedInstr(const MachineInstr &MI) const {
    assert(isPredicated(MI) && "Instruction is not predicated");
    return false;
  }

  // Returns a MIRPrinter comment for this machine operand.
  virtual std::string
  createMIROperandComment(const MachineInstr &MI, const MachineOperand &Op,
                          unsigned OpIdx, const TargetRegisterInfo *TRI) const;

  /// Returns true if the instruction is a
  /// terminator instruction that has not been predicated.
  bool isUnpredicatedTerminator(const MachineInstr &MI) const;

  /// Returns true if MI is an unconditional tail call.
  virtual bool isUnconditionalTailCall(const MachineInstr &MI) const {
    return false;
  }

  /// Returns true if the tail call can be made conditional on BranchCond.
  virtual bool canMakeTailCallConditional(SmallVectorImpl<MachineOperand> &Cond,
                                          const MachineInstr &TailCall) const {
    return false;
  }

  /// Replace the conditional branch in MBB with a conditional tail call.
  virtual void replaceBranchWithTailCall(MachineBasicBlock &MBB,
                                         SmallVectorImpl<MachineOperand> &Cond,
                                         const MachineInstr &TailCall) const {
    llvm_unreachable("Target didn't implement replaceBranchWithTailCall!");
  }

  /// Convert the instruction into a predicated instruction.
  /// It returns true if the operation was successful.
  virtual bool PredicateInstruction(MachineInstr &MI,
                                    ArrayRef<MachineOperand> Pred) const;

  /// Returns true if the first specified predicate
  /// subsumes the second, e.g. GE subsumes GT.
  virtual bool SubsumesPredicate(ArrayRef<MachineOperand> Pred1,
                                 ArrayRef<MachineOperand> Pred2) const {
    return false;
  }

  /// If the specified instruction defines any predicate
  /// or condition code register(s) used for predication, returns true as well
  /// as the definition predicate(s) by reference.
  /// SkipDead should be set to false at any point that dead
  /// predicate instructions should be considered as being defined.
  /// A dead predicate instruction is one that is guaranteed to be removed
  /// after a call to PredicateInstruction.
  virtual bool ClobbersPredicate(MachineInstr &MI,
                                 std::vector<MachineOperand> &Pred,
                                 bool SkipDead) const {
    return false;
  }

  /// Return true if the specified instruction can be predicated.
  /// By default, this returns true for every instruction with a
  /// PredicateOperand.
  virtual bool isPredicable(const MachineInstr &MI) const {
    return MI.getDesc().isPredicable();
  }

  /// Return true if it's safe to move a machine
  /// instruction that defines the specified register class.
  virtual bool isSafeToMoveRegClassDefs(const TargetRegisterClass *RC) const {
    return true;
  }

  /// Test if the given instruction should be considered a scheduling boundary.
  /// This primarily includes labels and terminators.
  virtual bool isSchedulingBoundary(const MachineInstr &MI,
                                    const MachineBasicBlock *MBB,
                                    const MachineFunction &MF) const;

  /// Measure the specified inline asm to determine an approximation of its
  /// length.
  virtual unsigned getInlineAsmLength(
    const char *Str, const MCAsmInfo &MAI,
    const TargetSubtargetInfo *STI = nullptr) const;

  /// Allocate and return a hazard recognizer to use for this target when
  /// scheduling the machine instructions before register allocation.
  virtual ScheduleHazardRecognizer *
  CreateTargetHazardRecognizer(const TargetSubtargetInfo *STI,
                               const ScheduleDAG *DAG) const;

  /// Allocate and return a hazard recognizer to use for this target when
  /// scheduling the machine instructions before register allocation.
  virtual ScheduleHazardRecognizer *
  CreateTargetMIHazardRecognizer(const InstrItineraryData *,
                                 const ScheduleDAGMI *DAG) const;

  /// Allocate and return a hazard recognizer to use for this target when
  /// scheduling the machine instructions after register allocation.
  virtual ScheduleHazardRecognizer *
  CreateTargetPostRAHazardRecognizer(const InstrItineraryData *,
                                     const ScheduleDAG *DAG) const;

  /// Allocate and return a hazard recognizer to use for by non-scheduling
  /// passes.
  virtual ScheduleHazardRecognizer *
  CreateTargetPostRAHazardRecognizer(const MachineFunction &MF) const {
    return nullptr;
  }

  /// Provide a global flag for disabling the PreRA hazard recognizer that
  /// targets may choose to honor.
  bool usePreRAHazardRecognizer() const;

  /// For a comparison instruction, return the source registers
  /// in SrcReg and SrcReg2 if having two register operands, and the value it
  /// compares against in CmpValue. Return true if the comparison instruction
  /// can be analyzed.
  virtual bool analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                              Register &SrcReg2, int64_t &Mask,
                              int64_t &Value) const {
    return false;
  }

  /// See if the comparison instruction can be converted
  /// into something more efficient. E.g., on ARM most instructions can set the
  /// flags register, obviating the need for a separate CMP.
  virtual bool optimizeCompareInstr(MachineInstr &CmpInstr, Register SrcReg,
                                    Register SrcReg2, int64_t Mask,
                                    int64_t Value,
                                    const MachineRegisterInfo *MRI) const {
    return false;
  }
  virtual bool optimizeCondBranch(MachineInstr &MI) const { return false; }

  /// Try to remove the load by folding it to a register operand at the use.
  /// We fold the load instructions if and only if the
  /// def and use are in the same BB. We only look at one load and see
  /// whether it can be folded into MI. FoldAsLoadDefReg is the virtual register
  /// defined by the load we are trying to fold. DefMI returns the machine
  /// instruction that defines FoldAsLoadDefReg, and the function returns
  /// the machine instruction generated due to folding.
  virtual MachineInstr *optimizeLoadInstr(MachineInstr &MI,
                                          const MachineRegisterInfo *MRI,
                                          Register &FoldAsLoadDefReg,
                                          MachineInstr *&DefMI) const {
    return nullptr;
  }

  /// 'Reg' is known to be defined by a move immediate instruction,
  /// try to fold the immediate into the use instruction.
  /// If MRI->hasOneNonDBGUse(Reg) is true, and this function returns true,
  /// then the caller may assume that DefMI has been erased from its parent
  /// block. The caller may assume that it will not be erased by this
  /// function otherwise.
  virtual bool foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI,
                             Register Reg, MachineRegisterInfo *MRI) const {
    return false;
  }

  /// Return the number of u-operations the given machine
  /// instruction will be decoded to on the target cpu. The itinerary's
  /// IssueWidth is the number of microops that can be dispatched each
  /// cycle. An instruction with zero microops takes no dispatch resources.
  virtual unsigned getNumMicroOps(const InstrItineraryData *ItinData,
                                  const MachineInstr &MI) const;

  /// Return true for pseudo instructions that don't consume any
  /// machine resources in their current form. These are common cases that the
  /// scheduler should consider free, rather than conservatively handling them
  /// as instructions with no itinerary.
  bool isZeroCost(unsigned Opcode) const {
    return Opcode <= TargetOpcode::COPY;
  }

  virtual std::optional<unsigned>
  getOperandLatency(const InstrItineraryData *ItinData, SDNode *DefNode,
                    unsigned DefIdx, SDNode *UseNode, unsigned UseIdx) const;

  /// Compute and return the use operand latency of a given pair of def and use.
  /// In most cases, the static scheduling itinerary was enough to determine the
  /// operand latency. But it may not be possible for instructions with variable
  /// number of defs / uses.
  ///
  /// This is a raw interface to the itinerary that may be directly overridden
  /// by a target. Use computeOperandLatency to get the best estimate of
  /// latency.
  virtual std::optional<unsigned>
  getOperandLatency(const InstrItineraryData *ItinData,
                    const MachineInstr &DefMI, unsigned DefIdx,
                    const MachineInstr &UseMI, unsigned UseIdx) const;

  /// Compute the instruction latency of a given instruction.
  /// If the instruction has higher cost when predicated, it's returned via
  /// PredCost.
  virtual unsigned getInstrLatency(const InstrItineraryData *ItinData,
                                   const MachineInstr &MI,
                                   unsigned *PredCost = nullptr) const;

  virtual unsigned getPredicationCost(const MachineInstr &MI) const;

  virtual unsigned getInstrLatency(const InstrItineraryData *ItinData,
                                   SDNode *Node) const;

  /// Return the default expected latency for a def based on its opcode.
  unsigned defaultDefLatency(const MCSchedModel &SchedModel,
                             const MachineInstr &DefMI) const;

  /// Return true if this opcode has high latency to its result.
  virtual bool isHighLatencyDef(int opc) const { return false; }

  /// Compute operand latency between a def of 'Reg'
  /// and a use in the current loop. Return true if the target considered
  /// it 'high'. This is used by optimization passes such as machine LICM to
  /// determine whether it makes sense to hoist an instruction out even in a
  /// high register pressure situation.
  virtual bool hasHighOperandLatency(const TargetSchedModel &SchedModel,
                                     const MachineRegisterInfo *MRI,
                                     const MachineInstr &DefMI, unsigned DefIdx,
                                     const MachineInstr &UseMI,
                                     unsigned UseIdx) const {
    return false;
  }

  /// Compute operand latency of a def of 'Reg'. Return true
  /// if the target considered it 'low'.
  virtual bool hasLowDefLatency(const TargetSchedModel &SchedModel,
                                const MachineInstr &DefMI,
                                unsigned DefIdx) const;

  /// Perform target-specific instruction verification.
  virtual bool verifyInstruction(const MachineInstr &MI,
                                 StringRef &ErrInfo) const {
    return true;
  }

  /// Return the current execution domain and bit mask of
  /// possible domains for instruction.
  ///
  /// Some micro-architectures have multiple execution domains, and multiple
  /// opcodes that perform the same operation in different domains.  For
  /// example, the x86 architecture provides the por, orps, and orpd
  /// instructions that all do the same thing.  There is a latency penalty if a
  /// register is written in one domain and read in another.
  ///
  /// This function returns a pair (domain, mask) containing the execution
  /// domain of MI, and a bit mask of possible domains.  The setExecutionDomain
  /// function can be used to change the opcode to one of the domains in the
  /// bit mask.  Instructions whose execution domain can't be changed should
  /// return a 0 mask.
  ///
  /// The execution domain numbers don't have any special meaning except domain
  /// 0 is used for instructions that are not associated with any interesting
  /// execution domain.
  ///
  virtual std::pair<uint16_t, uint16_t>
  getExecutionDomain(const MachineInstr &MI) const {
    return std::make_pair(0, 0);
  }

  /// Change the opcode of MI to execute in Domain.
  ///
  /// The bit (1 << Domain) must be set in the mask returned from
  /// getExecutionDomain(MI).
  virtual void setExecutionDomain(MachineInstr &MI, unsigned Domain) const {}

  /// Returns the preferred minimum clearance
  /// before an instruction with an unwanted partial register update.
  ///
  /// Some instructions only write part of a register, and implicitly need to
  /// read the other parts of the register.  This may cause unwanted stalls
  /// preventing otherwise unrelated instructions from executing in parallel in
  /// an out-of-order CPU.
  ///
  /// For example, the x86 instruction cvtsi2ss writes its result to bits
  /// [31:0] of the destination xmm register. Bits [127:32] are unaffected, so
  /// the instruction needs to wait for the old value of the register to become
  /// available:
  ///
  ///   addps %xmm1, %xmm0
  ///   movaps %xmm0, (%rax)
  ///   cvtsi2ss %rbx, %xmm0
  ///
  /// In the code above, the cvtsi2ss instruction needs to wait for the addps
  /// instruction before it can issue, even though the high bits of %xmm0
  /// probably aren't needed.
  ///
  /// This hook returns the preferred clearance before MI, measured in
  /// instructions.  Other defs of MI's operand OpNum are avoided in the last N
  /// instructions before MI.  It should only return a positive value for
  /// unwanted dependencies.  If the old bits of the defined register have
  /// useful values, or if MI is determined to otherwise read the dependency,
  /// the hook should return 0.
  ///
  /// The unwanted dependency may be handled by:
  ///
  /// 1. Allocating the same register for an MI def and use.  That makes the
  ///    unwanted dependency identical to a required dependency.
  ///
  /// 2. Allocating a register for the def that has no defs in the previous N
  ///    instructions.
  ///
  /// 3. Calling breakPartialRegDependency() with the same arguments.  This
  ///    allows the target to insert a dependency breaking instruction.
  ///
  virtual unsigned
  getPartialRegUpdateClearance(const MachineInstr &MI, unsigned OpNum,
                               const TargetRegisterInfo *TRI) const {
    // The default implementation returns 0 for no partial register dependency.
    return 0;
  }

  /// Return the minimum clearance before an instruction that reads an
  /// unused register.
  ///
  /// For example, AVX instructions may copy part of a register operand into
  /// the unused high bits of the destination register.
  ///
  /// vcvtsi2sdq %rax, undef %xmm0, %xmm14
  ///
  /// In the code above, vcvtsi2sdq copies %xmm0[127:64] into %xmm14 creating a
  /// false dependence on any previous write to %xmm0.
  ///
  /// This hook works similarly to getPartialRegUpdateClearance, except that it
  /// does not take an operand index. Instead sets \p OpNum to the index of the
  /// unused register.
  virtual unsigned getUndefRegClearance(const MachineInstr &MI, unsigned OpNum,
                                        const TargetRegisterInfo *TRI) const {
    // The default implementation returns 0 for no undef register dependency.
    return 0;
  }

  /// Insert a dependency-breaking instruction
  /// before MI to eliminate an unwanted dependency on OpNum.
  ///
  /// If it wasn't possible to avoid a def in the last N instructions before MI
  /// (see getPartialRegUpdateClearance), this hook will be called to break the
  /// unwanted dependency.
  ///
  /// On x86, an xorps instruction can be used as a dependency breaker:
  ///
  ///   addps %xmm1, %xmm0
  ///   movaps %xmm0, (%rax)
  ///   xorps %xmm0, %xmm0
  ///   cvtsi2ss %rbx, %xmm0
  ///
  /// An <imp-kill> operand should be added to MI if an instruction was
  /// inserted.  This ties the instructions together in the post-ra scheduler.
  ///
  virtual void breakPartialRegDependency(MachineInstr &MI, unsigned OpNum,
                                         const TargetRegisterInfo *TRI) const {}

  /// Create machine specific model for scheduling.
  virtual DFAPacketizer *
  CreateTargetScheduleState(const TargetSubtargetInfo &) const {
    return nullptr;
  }

  /// Sometimes, it is possible for the target
  /// to tell, even without aliasing information, that two MIs access different
  /// memory addresses. This function returns true if two MIs access different
  /// memory addresses and false otherwise.
  ///
  /// Assumes any physical registers used to compute addresses have the same
  /// value for both instructions. (This is the most useful assumption for
  /// post-RA scheduling.)
  ///
  /// See also MachineInstr::mayAlias, which is implemented on top of this
  /// function.
  virtual bool
  areMemAccessesTriviallyDisjoint(const MachineInstr &MIa,
                                  const MachineInstr &MIb) const {
    assert(MIa.mayLoadOrStore() &&
           "MIa must load from or modify a memory location");
    assert(MIb.mayLoadOrStore() &&
           "MIb must load from or modify a memory location");
    return false;
  }

  /// Return the value to use for the MachineCSE's LookAheadLimit,
  /// which is a heuristic used for CSE'ing phys reg defs.
  virtual unsigned getMachineCSELookAheadLimit() const {
    // The default lookahead is small to prevent unprofitable quadratic
    // behavior.
    return 5;
  }

  /// Return the maximal number of alias checks on memory operands. For
  /// instructions with more than one memory operands, the alias check on a
  /// single MachineInstr pair has quadratic overhead and results in
  /// unacceptable performance in the worst case. The limit here is to clamp
  /// that maximal checks performed. Usually, that's the product of memory
  /// operand numbers from that pair of MachineInstr to be checked. For
  /// instance, with two MachineInstrs with 4 and 5 memory operands
  /// correspondingly, a total of 20 checks are required. With this limit set to
  /// 16, their alias check is skipped. We choose to limit the product instead
  /// of the individual instruction as targets may have special MachineInstrs
  /// with a considerably high number of memory operands, such as `ldm` in ARM.
  /// Setting this limit per MachineInstr would result in either too high
  /// overhead or too rigid restriction.
  virtual unsigned getMemOperandAACheckLimit() const { return 16; }

  /// Return an array that contains the ids of the target indices (used for the
  /// TargetIndex machine operand) and their names.
  ///
  /// MIR Serialization is able to serialize only the target indices that are
  /// defined by this method.
  virtual ArrayRef<std::pair<int, const char *>>
  getSerializableTargetIndices() const {
    return std::nullopt;
  }

  /// Decompose the machine operand's target flags into two values - the direct
  /// target flag value and any of bit flags that are applied.
  virtual std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned /*TF*/) const {
    return std::make_pair(0u, 0u);
  }

  /// Return an array that contains the direct target flag values and their
  /// names.
  ///
  /// MIR Serialization is able to serialize only the target flags that are
  /// defined by this method.
  virtual ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const {
    return std::nullopt;
  }

  /// Return an array that contains the bitmask target flag values and their
  /// names.
  ///
  /// MIR Serialization is able to serialize only the target flags that are
  /// defined by this method.
  virtual ArrayRef<std::pair<unsigned, const char *>>
  getSerializableBitmaskMachineOperandTargetFlags() const {
    return std::nullopt;
  }

  /// Return an array that contains the MMO target flag values and their
  /// names.
  ///
  /// MIR Serialization is able to serialize only the MMO target flags that are
  /// defined by this method.
  virtual ArrayRef<std::pair<MachineMemOperand::Flags, const char *>>
  getSerializableMachineMemOperandTargetFlags() const {
    return std::nullopt;
  }

  /// Determines whether \p Inst is a tail call instruction. Override this
  /// method on targets that do not properly set MCID::Return and MCID::Call on
  /// tail call instructions."
  virtual bool isTailCall(const MachineInstr &Inst) const {
    return Inst.isReturn() && Inst.isCall();
  }

  /// True if the instruction is bound to the top of its basic block and no
  /// other instructions shall be inserted before it. This can be implemented
  /// to prevent register allocator to insert spills for \p Reg before such
  /// instructions.
  virtual bool isBasicBlockPrologue(const MachineInstr &MI,
                                    Register Reg = Register()) const {
    return false;
  }

  /// Allows targets to use appropriate copy instruction while spilitting live
  /// range of a register in register allocation.
  virtual unsigned getLiveRangeSplitOpcode(Register Reg,
                                           const MachineFunction &MF) const {
    return TargetOpcode::COPY;
  }

  /// During PHI eleimination lets target to make necessary checks and
  /// insert the copy to the PHI destination register in a target specific
  /// manner.
  virtual MachineInstr *createPHIDestinationCopy(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator InsPt,
      const DebugLoc &DL, Register Src, Register Dst) const {
    return BuildMI(MBB, InsPt, DL, get(TargetOpcode::COPY), Dst)
        .addReg(Src);
  }

  /// During PHI eleimination lets target to make necessary checks and
  /// insert the copy to the PHI destination register in a target specific
  /// manner.
  virtual MachineInstr *createPHISourceCopy(MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator InsPt,
                                            const DebugLoc &DL, Register Src,
                                            unsigned SrcSubReg,
                                            Register Dst) const {
    return BuildMI(MBB, InsPt, DL, get(TargetOpcode::COPY), Dst)
        .addReg(Src, 0, SrcSubReg);
  }

  /// Returns a \p outliner::OutlinedFunction struct containing target-specific
  /// information for a set of outlining candidates. Returns std::nullopt if the
  /// candidates are not suitable for outlining.
  virtual std::optional<outliner::OutlinedFunction> getOutliningCandidateInfo(
      std::vector<outliner::Candidate> &RepeatedSequenceLocs) const {
    llvm_unreachable(
        "Target didn't implement TargetInstrInfo::getOutliningCandidateInfo!");
  }

  /// Optional target hook to create the LLVM IR attributes for the outlined
  /// function. If overridden, the overriding function must call the default
  /// implementation.
  virtual void mergeOutliningCandidateAttributes(
      Function &F, std::vector<outliner::Candidate> &Candidates) const;

protected:
  /// Target-dependent implementation for getOutliningTypeImpl.
  virtual outliner::InstrType
  getOutliningTypeImpl(MachineBasicBlock::iterator &MIT, unsigned Flags) const {
    llvm_unreachable(
        "Target didn't implement TargetInstrInfo::getOutliningTypeImpl!");
  }

public:
  /// Returns how or if \p MIT should be outlined. \p Flags is the
  /// target-specific information returned by isMBBSafeToOutlineFrom.
  outliner::InstrType
  getOutliningType(MachineBasicBlock::iterator &MIT, unsigned Flags) const;

  /// Optional target hook that returns true if \p MBB is safe to outline from,
  /// and returns any target-specific information in \p Flags.
  virtual bool isMBBSafeToOutlineFrom(MachineBasicBlock &MBB,
                                      unsigned &Flags) const;

  /// Optional target hook which partitions \p MBB into outlinable ranges for
  /// instruction mapping purposes. Each range is defined by two iterators:
  /// [start, end).
  ///
  /// Ranges are expected to be ordered top-down. That is, ranges closer to the
  /// top of the block should come before ranges closer to the end of the block.
  ///
  /// Ranges cannot overlap.
  ///
  /// If an entire block is mappable, then its range is [MBB.begin(), MBB.end())
  ///
  /// All instructions not present in an outlinable range are considered
  /// illegal.
  virtual SmallVector<
      std::pair<MachineBasicBlock::iterator, MachineBasicBlock::iterator>>
  getOutlinableRanges(MachineBasicBlock &MBB, unsigned &Flags) const {
    return {std::make_pair(MBB.begin(), MBB.end())};
  }

  /// Insert a custom frame for outlined functions.
  virtual void buildOutlinedFrame(MachineBasicBlock &MBB, MachineFunction &MF,
                                  const outliner::OutlinedFunction &OF) const {
    llvm_unreachable(
        "Target didn't implement TargetInstrInfo::buildOutlinedFrame!");
  }

  /// Insert a call to an outlined function into the program.
  /// Returns an iterator to the spot where we inserted the call. This must be
  /// implemented by the target.
  virtual MachineBasicBlock::iterator
  insertOutlinedCall(Module &M, MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator &It, MachineFunction &MF,
                     outliner::Candidate &C) const {
    llvm_unreachable(
        "Target didn't implement TargetInstrInfo::insertOutlinedCall!");
  }

  /// Insert an architecture-specific instruction to clear a register. If you
  /// need to avoid sideeffects (e.g. avoid XOR on x86, which sets EFLAGS), set
  /// \p AllowSideEffects to \p false.
  virtual void buildClearRegister(Register Reg, MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator Iter,
                                  DebugLoc &DL,
                                  bool AllowSideEffects = true) const {
#if 0
    // FIXME: This should exist once all platforms that use stack protectors
    // implements it.
    llvm_unreachable(
        "Target didn't implement TargetInstrInfo::buildClearRegister!");
#endif
  }

  /// Return true if the function can safely be outlined from.
  /// A function \p MF is considered safe for outlining if an outlined function
  /// produced from instructions in F will produce a program which produces the
  /// same output for any set of given inputs.
  virtual bool isFunctionSafeToOutlineFrom(MachineFunction &MF,
                                           bool OutlineFromLinkOnceODRs) const {
    llvm_unreachable("Target didn't implement "
                     "TargetInstrInfo::isFunctionSafeToOutlineFrom!");
  }

  /// Return true if the function should be outlined from by default.
  virtual bool shouldOutlineFromFunctionByDefault(MachineFunction &MF) const {
    return false;
  }

  /// Return true if the function is a viable candidate for machine function
  /// splitting. The criteria for if a function can be split may vary by target.
  virtual bool isFunctionSafeToSplit(const MachineFunction &MF) const;

  /// Return true if the MachineBasicBlock can safely be split to the cold
  /// section. On AArch64, certain instructions may cause a block to be unsafe
  /// to split to the cold section.
  virtual bool isMBBSafeToSplitToCold(const MachineBasicBlock &MBB) const {
    return true;
  }

  /// Produce the expression describing the \p MI loading a value into
  /// the physical register \p Reg. This hook should only be used with
  /// \p MIs belonging to VReg-less functions.
  virtual std::optional<ParamLoadedValue>
  describeLoadedValue(const MachineInstr &MI, Register Reg) const;

  /// Given the generic extension instruction \p ExtMI, returns true if this
  /// extension is a likely candidate for being folded into an another
  /// instruction.
  virtual bool isExtendLikelyToBeFolded(MachineInstr &ExtMI,
                                        MachineRegisterInfo &MRI) const {
    return false;
  }

  /// Return MIR formatter to format/parse MIR operands.  Target can override
  /// this virtual function and return target specific MIR formatter.
  virtual const MIRFormatter *getMIRFormatter() const {
    if (!Formatter)
      Formatter = std::make_unique<MIRFormatter>();
    return Formatter.get();
  }

  /// Returns the target-specific default value for tail duplication.
  /// This value will be used if the tail-dup-placement-threshold argument is
  /// not provided.
  virtual unsigned getTailDuplicateSize(CodeGenOptLevel OptLevel) const {
    return OptLevel >= CodeGenOptLevel::Aggressive ? 4 : 2;
  }

  /// Returns the target-specific default value for tail merging.
  /// This value will be used if the tail-merge-size argument is not provided.
  virtual unsigned getTailMergeSize(const MachineFunction &MF) const {
    return 3;
  }

  /// Returns the callee operand from the given \p MI.
  virtual const MachineOperand &getCalleeOperand(const MachineInstr &MI) const {
    return MI.getOperand(0);
  }

  /// Return the uniformity behavior of the given instruction.
  virtual InstructionUniformity
  getInstructionUniformity(const MachineInstr &MI) const {
    return InstructionUniformity::Default;
  }

  /// Returns true if the given \p MI defines a TargetIndex operand that can be
  /// tracked by their offset, can have values, and can have debug info
  /// associated with it. If so, sets \p Index and \p Offset of the target index
  /// operand.
  virtual bool isExplicitTargetIndexDef(const MachineInstr &MI, int &Index,
                                        int64_t &Offset) const {
    return false;
  }

  // Get the call frame size just before MI.
  unsigned getCallFrameSizeAt(MachineInstr &MI) const;

  /// Fills in the necessary MachineOperands to refer to a frame index.
  /// The best way to understand this is to print `asm(""::"m"(x));` after
  /// finalize-isel. Example:
  /// INLINEASM ... 262190 /* mem:m */, %stack.0.x.addr, 1, $noreg, 0, $noreg
  /// we would add placeholders for:                     ^  ^       ^  ^
  virtual void getFrameIndexOperands(SmallVectorImpl<MachineOperand> &Ops,
                                     int FI) const {
    llvm_unreachable("unknown number of operands necessary");
  }

  /// Gets the opcode for the Pseudo Instruction used to initialize
  /// the undef value. If no Instruction is available, this will
  /// fail compilation.
  virtual unsigned getUndefInitOpcode(unsigned RegClassID) const {
    (void)RegClassID;

    llvm_unreachable("Unexpected register class.");
  }

private:
  mutable std::unique_ptr<MIRFormatter> Formatter;
  unsigned CallFrameSetupOpcode, CallFrameDestroyOpcode;
  unsigned CatchRetOpcode;
  unsigned ReturnOpcode;
};

/// Provide DenseMapInfo for TargetInstrInfo::RegSubRegPair.
template <> struct DenseMapInfo<TargetInstrInfo::RegSubRegPair> {
  using RegInfo = DenseMapInfo<unsigned>;

  static inline TargetInstrInfo::RegSubRegPair getEmptyKey() {
    return TargetInstrInfo::RegSubRegPair(RegInfo::getEmptyKey(),
                                          RegInfo::getEmptyKey());
  }

  static inline TargetInstrInfo::RegSubRegPair getTombstoneKey() {
    return TargetInstrInfo::RegSubRegPair(RegInfo::getTombstoneKey(),
                                          RegInfo::getTombstoneKey());
  }

  /// Reuse getHashValue implementation from
  /// std::pair<unsigned, unsigned>.
  static unsigned getHashValue(const TargetInstrInfo::RegSubRegPair &Val) {
    std::pair<unsigned, unsigned> PairVal = std::make_pair(Val.Reg, Val.SubReg);
    return DenseMapInfo<std::pair<unsigned, unsigned>>::getHashValue(PairVal);
  }

  static bool isEqual(const TargetInstrInfo::RegSubRegPair &LHS,
                      const TargetInstrInfo::RegSubRegPair &RHS) {
    return RegInfo::isEqual(LHS.Reg, RHS.Reg) &&
           RegInfo::isEqual(LHS.SubReg, RHS.SubReg);
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETINSTRINFO_H
