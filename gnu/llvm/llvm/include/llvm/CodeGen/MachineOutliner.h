//===---- MachineOutliner.h - Outliner data structures ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Contains all data structures shared between the outliner implemented in
/// MachineOutliner.cpp and target implementations of the outliner.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEOUTLINER_H
#define LLVM_CODEGEN_MACHINEOUTLINER_H

#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include <initializer_list>

namespace llvm {
namespace outliner {

/// Represents how an instruction should be mapped by the outliner.
/// \p Legal instructions are those which are safe to outline.
/// \p LegalTerminator instructions are safe to outline, but only as the
/// last instruction in a sequence.
/// \p Illegal instructions are those which cannot be outlined.
/// \p Invisible instructions are instructions which can be outlined, but
/// shouldn't actually impact the outlining result.
enum InstrType { Legal, LegalTerminator, Illegal, Invisible };

/// An individual sequence of instructions to be replaced with a call to
/// an outlined function.
struct Candidate {
private:
  /// The start index of this \p Candidate in the instruction list.
  unsigned StartIdx = 0;

  /// The number of instructions in this \p Candidate.
  unsigned Len = 0;

  // The first instruction in this \p Candidate.
  MachineBasicBlock::iterator FirstInst;

  // The last instruction in this \p Candidate.
  MachineBasicBlock::iterator LastInst;

  // The basic block that contains this Candidate.
  MachineBasicBlock *MBB = nullptr;

  /// Cost of calling an outlined function from this point as defined by the
  /// target.
  unsigned CallOverhead = 0;

  /// Liveness information for this Candidate. Tracks from the end of the
  /// block containing this Candidate to the beginning of its sequence.
  ///
  /// Optional. Can be used to fine-tune the cost model, or fine-tune legality
  /// decisions.
  LiveRegUnits FromEndOfBlockToStartOfSeq;

  /// Liveness information restricted to this Candidate's instruction sequence.
  ///
  /// Optional. Can be used to fine-tune the cost model, or fine-tune legality
  /// decisions.
  LiveRegUnits InSeq;

  /// True if FromEndOfBlockToStartOfSeq has been initialized.
  bool FromEndOfBlockToStartOfSeqWasSet = false;

  /// True if InSeq has been initialized.
  bool InSeqWasSet = false;

  /// Populate FromEndOfBlockToStartOfSeq with liveness information.
  void initFromEndOfBlockToStartOfSeq(const TargetRegisterInfo &TRI) {
    assert(MBB->getParent()->getRegInfo().tracksLiveness() &&
           "Candidate's Machine Function must track liveness");
    // Only initialize once.
    if (FromEndOfBlockToStartOfSeqWasSet)
      return;
    FromEndOfBlockToStartOfSeqWasSet = true;
    FromEndOfBlockToStartOfSeq.init(TRI);
    FromEndOfBlockToStartOfSeq.addLiveOuts(*MBB);
    // Compute liveness from the end of the block up to the beginning of the
    // outlining candidate.
    for (auto &MI : make_range(MBB->rbegin(),
                               (MachineBasicBlock::reverse_iterator)begin()))
      FromEndOfBlockToStartOfSeq.stepBackward(MI);
  }

  /// Populate InSeq with liveness information.
  void initInSeq(const TargetRegisterInfo &TRI) {
    assert(MBB->getParent()->getRegInfo().tracksLiveness() &&
           "Candidate's Machine Function must track liveness");
    // Only initialize once.
    if (InSeqWasSet)
      return;
    InSeqWasSet = true;
    InSeq.init(TRI);
    for (auto &MI : *this)
      InSeq.accumulate(MI);
  }

public:
  /// The index of this \p Candidate's \p OutlinedFunction in the list of
  /// \p OutlinedFunctions.
  unsigned FunctionIdx = 0;

  /// Identifier denoting the instructions to emit to call an outlined function
  /// from this point. Defined by the target.
  unsigned CallConstructionID = 0;

  /// Target-specific flags for this Candidate's MBB.
  unsigned Flags = 0x0;

  /// Return the number of instructions in this Candidate.
  unsigned getLength() const { return Len; }

  /// Return the start index of this candidate.
  unsigned getStartIdx() const { return StartIdx; }

  /// Return the end index of this candidate.
  unsigned getEndIdx() const { return StartIdx + Len - 1; }

  /// Set the CallConstructionID and CallOverhead of this candidate to CID and
  /// CO respectively.
  void setCallInfo(unsigned CID, unsigned CO) {
    CallConstructionID = CID;
    CallOverhead = CO;
  }

  /// Returns the call overhead of this candidate if it is in the list.
  unsigned getCallOverhead() const { return CallOverhead; }

  MachineBasicBlock::iterator begin() { return FirstInst; }
  MachineBasicBlock::iterator end() { return std::next(LastInst); }

  MachineInstr &front() { return *FirstInst; }
  MachineInstr &back() { return *LastInst; }
  MachineFunction *getMF() const { return MBB->getParent(); }
  MachineBasicBlock *getMBB() const { return MBB; }

  /// \returns True if \p Reg is available from the end of the block to the
  /// beginning of the sequence.
  ///
  /// This query considers the following range:
  ///
  /// in_seq_1
  /// in_seq_2
  /// ...
  /// in_seq_n
  /// not_in_seq_1
  /// ...
  /// <end of block>
  bool isAvailableAcrossAndOutOfSeq(Register Reg,
                                    const TargetRegisterInfo &TRI) {
    if (!FromEndOfBlockToStartOfSeqWasSet)
      initFromEndOfBlockToStartOfSeq(TRI);
    return FromEndOfBlockToStartOfSeq.available(Reg);
  }

  /// \returns True if `isAvailableAcrossAndOutOfSeq` fails for any register
  /// in \p Regs.
  bool isAnyUnavailableAcrossOrOutOfSeq(std::initializer_list<Register> Regs,
                                        const TargetRegisterInfo &TRI) {
    if (!FromEndOfBlockToStartOfSeqWasSet)
      initFromEndOfBlockToStartOfSeq(TRI);
    return any_of(Regs, [&](Register Reg) {
      return !FromEndOfBlockToStartOfSeq.available(Reg);
    });
  }

  /// \returns True if \p Reg is available within the sequence itself.
  ///
  /// This query considers the following range:
  ///
  /// in_seq_1
  /// in_seq_2
  /// ...
  /// in_seq_n
  bool isAvailableInsideSeq(Register Reg, const TargetRegisterInfo &TRI) {
    if (!InSeqWasSet)
      initInSeq(TRI);
    return InSeq.available(Reg);
  }

  /// The number of instructions that would be saved by outlining every
  /// candidate of this type.
  ///
  /// This is a fixed value which is not updated during the candidate pruning
  /// process. It is only used for deciding which candidate to keep if two
  /// candidates overlap. The true benefit is stored in the OutlinedFunction
  /// for some given candidate.
  unsigned Benefit = 0;

  Candidate(unsigned StartIdx, unsigned Len,
            MachineBasicBlock::iterator &FirstInst,
            MachineBasicBlock::iterator &LastInst, MachineBasicBlock *MBB,
            unsigned FunctionIdx, unsigned Flags)
      : StartIdx(StartIdx), Len(Len), FirstInst(FirstInst), LastInst(LastInst),
        MBB(MBB), FunctionIdx(FunctionIdx), Flags(Flags) {}
  Candidate() = delete;

  /// Used to ensure that \p Candidates are outlined in an order that
  /// preserves the start and end indices of other \p Candidates.
  bool operator<(const Candidate &RHS) const {
    return getStartIdx() > RHS.getStartIdx();
  }

};

/// The information necessary to create an outlined function for some
/// class of candidate.
struct OutlinedFunction {

public:
  std::vector<Candidate> Candidates;

  /// The actual outlined function created.
  /// This is initialized after we go through and create the actual function.
  MachineFunction *MF = nullptr;

  /// Represents the size of a sequence in bytes. (Some instructions vary
  /// widely in size, so just counting the instructions isn't very useful.)
  unsigned SequenceSize = 0;

  /// Target-defined overhead of constructing a frame for this function.
  unsigned FrameOverhead = 0;

  /// Target-defined identifier for constructing a frame for this function.
  unsigned FrameConstructionID = 0;

  /// Return the number of candidates for this \p OutlinedFunction.
  unsigned getOccurrenceCount() const { return Candidates.size(); }

  /// Return the number of bytes it would take to outline this
  /// function.
  unsigned getOutliningCost() const {
    unsigned CallOverhead = 0;
    for (const Candidate &C : Candidates)
      CallOverhead += C.getCallOverhead();
    return CallOverhead + SequenceSize + FrameOverhead;
  }

  /// Return the size in bytes of the unoutlined sequences.
  unsigned getNotOutlinedCost() const {
    return getOccurrenceCount() * SequenceSize;
  }

  /// Return the number of instructions that would be saved by outlining
  /// this function.
  unsigned getBenefit() const {
    unsigned NotOutlinedCost = getNotOutlinedCost();
    unsigned OutlinedCost = getOutliningCost();
    return (NotOutlinedCost < OutlinedCost) ? 0
                                            : NotOutlinedCost - OutlinedCost;
  }

  /// Return the number of instructions in this sequence.
  unsigned getNumInstrs() const { return Candidates[0].getLength(); }

  OutlinedFunction(std::vector<Candidate> &Candidates, unsigned SequenceSize,
                   unsigned FrameOverhead, unsigned FrameConstructionID)
      : Candidates(Candidates), SequenceSize(SequenceSize),
        FrameOverhead(FrameOverhead), FrameConstructionID(FrameConstructionID) {
    const unsigned B = getBenefit();
    for (Candidate &C : Candidates)
      C.Benefit = B;
  }

  OutlinedFunction() = delete;
};
} // namespace outliner
} // namespace llvm

#endif
