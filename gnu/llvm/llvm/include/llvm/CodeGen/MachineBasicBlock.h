//===- llvm/CodeGen/MachineBasicBlock.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Collect the sequence of machine instructions for a basic block.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEBASICBLOCK_H
#define LLVM_CODEGEN_MACHINEBASICBLOCK_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/BranchProbability.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace llvm {

class BasicBlock;
class MachineFunction;
class MCSymbol;
class ModuleSlotTracker;
class Pass;
class Printable;
class SlotIndexes;
class StringRef;
class raw_ostream;
class LiveIntervals;
class TargetRegisterClass;
class TargetRegisterInfo;
template <typename IRUnitT, typename... ExtraArgTs> class AnalysisManager;
using MachineFunctionAnalysisManager = AnalysisManager<MachineFunction>;

// This structure uniquely identifies a basic block section.
// Possible values are
//  {Type: Default, Number: (unsigned)} (These are regular section IDs)
//  {Type: Exception, Number: 0}  (ExceptionSectionID)
//  {Type: Cold, Number: 0}  (ColdSectionID)
struct MBBSectionID {
  enum SectionType {
    Default = 0, // Regular section (these sections are distinguished by the
                 // Number field).
    Exception,   // Special section type for exception handling blocks
    Cold,        // Special section type for cold blocks
  } Type;
  unsigned Number;

  MBBSectionID(unsigned N) : Type(Default), Number(N) {}

  // Special unique sections for cold and exception blocks.
  const static MBBSectionID ColdSectionID;
  const static MBBSectionID ExceptionSectionID;

  bool operator==(const MBBSectionID &Other) const {
    return Type == Other.Type && Number == Other.Number;
  }

  bool operator!=(const MBBSectionID &Other) const { return !(*this == Other); }

private:
  // This is only used to construct the special cold and exception sections.
  MBBSectionID(SectionType T) : Type(T), Number(0) {}
};

template <> struct DenseMapInfo<MBBSectionID> {
  using TypeInfo = DenseMapInfo<MBBSectionID::SectionType>;
  using NumberInfo = DenseMapInfo<unsigned>;

  static inline MBBSectionID getEmptyKey() {
    return MBBSectionID(NumberInfo::getEmptyKey());
  }
  static inline MBBSectionID getTombstoneKey() {
    return MBBSectionID(NumberInfo::getTombstoneKey());
  }
  static unsigned getHashValue(const MBBSectionID &SecID) {
    return detail::combineHashValue(TypeInfo::getHashValue(SecID.Type),
                                    NumberInfo::getHashValue(SecID.Number));
  }
  static bool isEqual(const MBBSectionID &LHS, const MBBSectionID &RHS) {
    return LHS == RHS;
  }
};

// This structure represents the information for a basic block pertaining to
// the basic block sections profile.
struct UniqueBBID {
  unsigned BaseID;
  unsigned CloneID;
};

template <> struct ilist_traits<MachineInstr> {
private:
  friend class MachineBasicBlock; // Set by the owning MachineBasicBlock.

  MachineBasicBlock *Parent;

  using instr_iterator =
      simple_ilist<MachineInstr, ilist_sentinel_tracking<true>>::iterator;

public:
  void addNodeToList(MachineInstr *N);
  void removeNodeFromList(MachineInstr *N);
  void transferNodesFromList(ilist_traits &FromList, instr_iterator First,
                             instr_iterator Last);
  void deleteNode(MachineInstr *MI);
};

class MachineBasicBlock
    : public ilist_node_with_parent<MachineBasicBlock, MachineFunction> {
public:
  /// Pair of physical register and lane mask.
  /// This is not simply a std::pair typedef because the members should be named
  /// clearly as they both have an integer type.
  struct RegisterMaskPair {
  public:
    MCPhysReg PhysReg;
    LaneBitmask LaneMask;

    RegisterMaskPair(MCPhysReg PhysReg, LaneBitmask LaneMask)
        : PhysReg(PhysReg), LaneMask(LaneMask) {}

    bool operator==(const RegisterMaskPair &other) const {
      return PhysReg == other.PhysReg && LaneMask == other.LaneMask;
    }
  };

private:
  using Instructions = ilist<MachineInstr, ilist_sentinel_tracking<true>>;

  const BasicBlock *BB;
  int Number;

  /// The call frame size on entry to this basic block due to call frame setup
  /// instructions in a predecessor. This is usually zero, unless basic blocks
  /// are split in the middle of a call sequence.
  ///
  /// This information is only maintained until PrologEpilogInserter eliminates
  /// call frame pseudos.
  unsigned CallFrameSize = 0;

  MachineFunction *xParent;
  Instructions Insts;

  /// Keep track of the predecessor / successor basic blocks.
  std::vector<MachineBasicBlock *> Predecessors;
  std::vector<MachineBasicBlock *> Successors;

  /// Keep track of the probabilities to the successors. This vector has the
  /// same order as Successors, or it is empty if we don't use it (disable
  /// optimization).
  std::vector<BranchProbability> Probs;
  using probability_iterator = std::vector<BranchProbability>::iterator;
  using const_probability_iterator =
      std::vector<BranchProbability>::const_iterator;

  std::optional<uint64_t> IrrLoopHeaderWeight;

  /// Keep track of the physical registers that are livein of the basicblock.
  using LiveInVector = std::vector<RegisterMaskPair>;
  LiveInVector LiveIns;

  /// Alignment of the basic block. One if the basic block does not need to be
  /// aligned.
  Align Alignment;
  /// Maximum amount of bytes that can be added to align the basic block. If the
  /// alignment cannot be reached in this many bytes, no bytes are emitted.
  /// Zero to represent no maximum.
  unsigned MaxBytesForAlignment = 0;

  /// Indicate that this basic block is entered via an exception handler.
  bool IsEHPad = false;

  /// Indicate that this MachineBasicBlock is referenced somewhere other than
  /// as predecessor/successor, a terminator MachineInstr, or a jump table.
  bool MachineBlockAddressTaken = false;

  /// If this MachineBasicBlock corresponds to an IR-level "blockaddress"
  /// constant, this contains a pointer to that block.
  BasicBlock *AddressTakenIRBlock = nullptr;

  /// Indicate that this basic block needs its symbol be emitted regardless of
  /// whether the flow just falls-through to it.
  bool LabelMustBeEmitted = false;

  /// Indicate that this basic block is the entry block of an EH scope, i.e.,
  /// the block that used to have a catchpad or cleanuppad instruction in the
  /// LLVM IR.
  bool IsEHScopeEntry = false;

  /// Indicates if this is a target block of a catchret.
  bool IsEHCatchretTarget = false;

  /// Indicate that this basic block is the entry block of an EH funclet.
  bool IsEHFuncletEntry = false;

  /// Indicate that this basic block is the entry block of a cleanup funclet.
  bool IsCleanupFuncletEntry = false;

  /// Fixed unique ID assigned to this basic block upon creation. Used with
  /// basic block sections and basic block labels.
  std::optional<UniqueBBID> BBID;

  /// With basic block sections, this stores the Section ID of the basic block.
  MBBSectionID SectionID{0};

  // Indicate that this basic block begins a section.
  bool IsBeginSection = false;

  // Indicate that this basic block ends a section.
  bool IsEndSection = false;

  /// Indicate that this basic block is the indirect dest of an INLINEASM_BR.
  bool IsInlineAsmBrIndirectTarget = false;

  /// since getSymbol is a relatively heavy-weight operation, the symbol
  /// is only computed once and is cached.
  mutable MCSymbol *CachedMCSymbol = nullptr;

  /// Cached MCSymbol for this block (used if IsEHCatchRetTarget).
  mutable MCSymbol *CachedEHCatchretMCSymbol = nullptr;

  /// Marks the end of the basic block. Used during basic block sections to
  /// calculate the size of the basic block, or the BB section ending with it.
  mutable MCSymbol *CachedEndMCSymbol = nullptr;

  // Intrusive list support
  MachineBasicBlock() = default;

  explicit MachineBasicBlock(MachineFunction &MF, const BasicBlock *BB);

  ~MachineBasicBlock();

  // MachineBasicBlocks are allocated and owned by MachineFunction.
  friend class MachineFunction;

public:
  /// Return the LLVM basic block that this instance corresponded to originally.
  /// Note that this may be NULL if this instance does not correspond directly
  /// to an LLVM basic block.
  const BasicBlock *getBasicBlock() const { return BB; }

  /// Remove the reference to the underlying IR BasicBlock. This is for
  /// reduction tools and should generally not be used.
  void clearBasicBlock() {
    BB = nullptr;
  }

  /// Check if there is a name of corresponding LLVM basic block.
  bool hasName() const;

  /// Return the name of the corresponding LLVM basic block, or an empty string.
  StringRef getName() const;

  /// Return a formatted string to identify this block and its parent function.
  std::string getFullName() const;

  /// Test whether this block is used as something other than the target
  /// of a terminator, exception-handling target, or jump table. This is
  /// either the result of an IR-level "blockaddress", or some form
  /// of target-specific branch lowering.
  bool hasAddressTaken() const {
    return MachineBlockAddressTaken || AddressTakenIRBlock;
  }

  /// Test whether this block is used as something other than the target of a
  /// terminator, exception-handling target, jump table, or IR blockaddress.
  /// For example, its address might be loaded into a register, or
  /// stored in some branch table that isn't part of MachineJumpTableInfo.
  bool isMachineBlockAddressTaken() const { return MachineBlockAddressTaken; }

  /// Test whether this block is the target of an IR BlockAddress.  (There can
  /// more than one MBB associated with an IR BB where the address is taken.)
  bool isIRBlockAddressTaken() const { return AddressTakenIRBlock; }

  /// Retrieves the BasicBlock which corresponds to this MachineBasicBlock.
  BasicBlock *getAddressTakenIRBlock() const { return AddressTakenIRBlock; }

  /// Set this block to indicate that its address is used as something other
  /// than the target of a terminator, exception-handling target, jump table,
  /// or IR-level "blockaddress".
  void setMachineBlockAddressTaken() { MachineBlockAddressTaken = true; }

  /// Set this block to reflect that it corresponds to an IR-level basic block
  /// with a BlockAddress.
  void setAddressTakenIRBlock(BasicBlock *BB) { AddressTakenIRBlock = BB; }

  /// Test whether this block must have its label emitted.
  bool hasLabelMustBeEmitted() const { return LabelMustBeEmitted; }

  /// Set this block to reflect that, regardless how we flow to it, we need
  /// its label be emitted.
  void setLabelMustBeEmitted() { LabelMustBeEmitted = true; }

  /// Return the MachineFunction containing this basic block.
  const MachineFunction *getParent() const { return xParent; }
  MachineFunction *getParent() { return xParent; }

  using instr_iterator = Instructions::iterator;
  using const_instr_iterator = Instructions::const_iterator;
  using reverse_instr_iterator = Instructions::reverse_iterator;
  using const_reverse_instr_iterator = Instructions::const_reverse_iterator;

  using iterator = MachineInstrBundleIterator<MachineInstr>;
  using const_iterator = MachineInstrBundleIterator<const MachineInstr>;
  using reverse_iterator = MachineInstrBundleIterator<MachineInstr, true>;
  using const_reverse_iterator =
      MachineInstrBundleIterator<const MachineInstr, true>;

  unsigned size() const { return (unsigned)Insts.size(); }
  bool sizeWithoutDebugLargerThan(unsigned Limit) const;
  bool empty() const { return Insts.empty(); }

  MachineInstr       &instr_front()       { return Insts.front(); }
  MachineInstr       &instr_back()        { return Insts.back();  }
  const MachineInstr &instr_front() const { return Insts.front(); }
  const MachineInstr &instr_back()  const { return Insts.back();  }

  MachineInstr       &front()             { return Insts.front(); }
  MachineInstr       &back()              { return *--end();      }
  const MachineInstr &front()       const { return Insts.front(); }
  const MachineInstr &back()        const { return *--end();      }

  instr_iterator                instr_begin()       { return Insts.begin();  }
  const_instr_iterator          instr_begin() const { return Insts.begin();  }
  instr_iterator                  instr_end()       { return Insts.end();    }
  const_instr_iterator            instr_end() const { return Insts.end();    }
  reverse_instr_iterator       instr_rbegin()       { return Insts.rbegin(); }
  const_reverse_instr_iterator instr_rbegin() const { return Insts.rbegin(); }
  reverse_instr_iterator       instr_rend  ()       { return Insts.rend();   }
  const_reverse_instr_iterator instr_rend  () const { return Insts.rend();   }

  using instr_range = iterator_range<instr_iterator>;
  using const_instr_range = iterator_range<const_instr_iterator>;
  instr_range instrs() { return instr_range(instr_begin(), instr_end()); }
  const_instr_range instrs() const {
    return const_instr_range(instr_begin(), instr_end());
  }

  iterator                begin()       { return instr_begin();  }
  const_iterator          begin() const { return instr_begin();  }
  iterator                end  ()       { return instr_end();    }
  const_iterator          end  () const { return instr_end();    }
  reverse_iterator rbegin() {
    return reverse_iterator::getAtBundleBegin(instr_rbegin());
  }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator::getAtBundleBegin(instr_rbegin());
  }
  reverse_iterator rend() { return reverse_iterator(instr_rend()); }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(instr_rend());
  }

  /// Support for MachineInstr::getNextNode().
  static Instructions MachineBasicBlock::*getSublistAccess(MachineInstr *) {
    return &MachineBasicBlock::Insts;
  }

  inline iterator_range<iterator> terminators() {
    return make_range(getFirstTerminator(), end());
  }
  inline iterator_range<const_iterator> terminators() const {
    return make_range(getFirstTerminator(), end());
  }

  /// Returns a range that iterates over the phis in the basic block.
  inline iterator_range<iterator> phis() {
    return make_range(begin(), getFirstNonPHI());
  }
  inline iterator_range<const_iterator> phis() const {
    return const_cast<MachineBasicBlock *>(this)->phis();
  }

  // Machine-CFG iterators
  using pred_iterator = std::vector<MachineBasicBlock *>::iterator;
  using const_pred_iterator = std::vector<MachineBasicBlock *>::const_iterator;
  using succ_iterator = std::vector<MachineBasicBlock *>::iterator;
  using const_succ_iterator = std::vector<MachineBasicBlock *>::const_iterator;
  using pred_reverse_iterator =
      std::vector<MachineBasicBlock *>::reverse_iterator;
  using const_pred_reverse_iterator =
      std::vector<MachineBasicBlock *>::const_reverse_iterator;
  using succ_reverse_iterator =
      std::vector<MachineBasicBlock *>::reverse_iterator;
  using const_succ_reverse_iterator =
      std::vector<MachineBasicBlock *>::const_reverse_iterator;
  pred_iterator        pred_begin()       { return Predecessors.begin(); }
  const_pred_iterator  pred_begin() const { return Predecessors.begin(); }
  pred_iterator        pred_end()         { return Predecessors.end();   }
  const_pred_iterator  pred_end()   const { return Predecessors.end();   }
  pred_reverse_iterator        pred_rbegin()
                                          { return Predecessors.rbegin();}
  const_pred_reverse_iterator  pred_rbegin() const
                                          { return Predecessors.rbegin();}
  pred_reverse_iterator        pred_rend()
                                          { return Predecessors.rend();  }
  const_pred_reverse_iterator  pred_rend()   const
                                          { return Predecessors.rend();  }
  unsigned             pred_size()  const {
    return (unsigned)Predecessors.size();
  }
  bool                 pred_empty() const { return Predecessors.empty(); }
  succ_iterator        succ_begin()       { return Successors.begin();   }
  const_succ_iterator  succ_begin() const { return Successors.begin();   }
  succ_iterator        succ_end()         { return Successors.end();     }
  const_succ_iterator  succ_end()   const { return Successors.end();     }
  succ_reverse_iterator        succ_rbegin()
                                          { return Successors.rbegin();  }
  const_succ_reverse_iterator  succ_rbegin() const
                                          { return Successors.rbegin();  }
  succ_reverse_iterator        succ_rend()
                                          { return Successors.rend();    }
  const_succ_reverse_iterator  succ_rend()   const
                                          { return Successors.rend();    }
  unsigned             succ_size()  const {
    return (unsigned)Successors.size();
  }
  bool                 succ_empty() const { return Successors.empty();   }

  inline iterator_range<pred_iterator> predecessors() {
    return make_range(pred_begin(), pred_end());
  }
  inline iterator_range<const_pred_iterator> predecessors() const {
    return make_range(pred_begin(), pred_end());
  }
  inline iterator_range<succ_iterator> successors() {
    return make_range(succ_begin(), succ_end());
  }
  inline iterator_range<const_succ_iterator> successors() const {
    return make_range(succ_begin(), succ_end());
  }

  // LiveIn management methods.

  /// Adds the specified register as a live in. Note that it is an error to add
  /// the same register to the same set more than once unless the intention is
  /// to call sortUniqueLiveIns after all registers are added.
  void addLiveIn(MCRegister PhysReg,
                 LaneBitmask LaneMask = LaneBitmask::getAll()) {
    LiveIns.push_back(RegisterMaskPair(PhysReg, LaneMask));
  }
  void addLiveIn(const RegisterMaskPair &RegMaskPair) {
    LiveIns.push_back(RegMaskPair);
  }

  /// Sorts and uniques the LiveIns vector. It can be significantly faster to do
  /// this than repeatedly calling isLiveIn before calling addLiveIn for every
  /// LiveIn insertion.
  void sortUniqueLiveIns();

  /// Clear live in list.
  void clearLiveIns();

  /// Clear the live in list, and return the removed live in's in \p OldLiveIns.
  /// Requires that the vector \p OldLiveIns is empty.
  void clearLiveIns(std::vector<RegisterMaskPair> &OldLiveIns);

  /// Add PhysReg as live in to this block, and ensure that there is a copy of
  /// PhysReg to a virtual register of class RC. Return the virtual register
  /// that is a copy of the live in PhysReg.
  Register addLiveIn(MCRegister PhysReg, const TargetRegisterClass *RC);

  /// Remove the specified register from the live in set.
  void removeLiveIn(MCPhysReg Reg,
                    LaneBitmask LaneMask = LaneBitmask::getAll());

  /// Return true if the specified register is in the live in set.
  bool isLiveIn(MCPhysReg Reg,
                LaneBitmask LaneMask = LaneBitmask::getAll()) const;

  // Iteration support for live in sets.  These sets are kept in sorted
  // order by their register number.
  using livein_iterator = LiveInVector::const_iterator;

  /// Unlike livein_begin, this method does not check that the liveness
  /// information is accurate. Still for debug purposes it may be useful
  /// to have iterators that won't assert if the liveness information
  /// is not current.
  livein_iterator livein_begin_dbg() const { return LiveIns.begin(); }
  iterator_range<livein_iterator> liveins_dbg() const {
    return make_range(livein_begin_dbg(), livein_end());
  }

  livein_iterator livein_begin() const;
  livein_iterator livein_end()   const { return LiveIns.end(); }
  bool            livein_empty() const { return LiveIns.empty(); }
  iterator_range<livein_iterator> liveins() const {
    return make_range(livein_begin(), livein_end());
  }

  /// Remove entry from the livein set and return iterator to the next.
  livein_iterator removeLiveIn(livein_iterator I);

  const std::vector<RegisterMaskPair> &getLiveIns() const { return LiveIns; }

  class liveout_iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = RegisterMaskPair;
    using pointer = const RegisterMaskPair *;
    using reference = const RegisterMaskPair &;

    liveout_iterator(const MachineBasicBlock &MBB, MCPhysReg ExceptionPointer,
                     MCPhysReg ExceptionSelector, bool End)
        : ExceptionPointer(ExceptionPointer),
          ExceptionSelector(ExceptionSelector), BlockI(MBB.succ_begin()),
          BlockEnd(MBB.succ_end()) {
      if (End)
        BlockI = BlockEnd;
      else if (BlockI != BlockEnd) {
        LiveRegI = (*BlockI)->livein_begin();
        if (!advanceToValidPosition())
          return;
        if (LiveRegI->PhysReg == ExceptionPointer ||
            LiveRegI->PhysReg == ExceptionSelector)
          ++(*this);
      }
    }

    liveout_iterator &operator++() {
      do {
        ++LiveRegI;
        if (!advanceToValidPosition())
          return *this;
      } while ((*BlockI)->isEHPad() &&
               (LiveRegI->PhysReg == ExceptionPointer ||
                LiveRegI->PhysReg == ExceptionSelector));
      return *this;
    }

    liveout_iterator operator++(int) {
      liveout_iterator Tmp = *this;
      ++(*this);
      return Tmp;
    }

    reference operator*() const {
      return *LiveRegI;
    }

    pointer operator->() const {
      return &*LiveRegI;
    }

    bool operator==(const liveout_iterator &RHS) const {
      if (BlockI != BlockEnd)
        return BlockI == RHS.BlockI && LiveRegI == RHS.LiveRegI;
      return RHS.BlockI == BlockEnd;
    }

    bool operator!=(const liveout_iterator &RHS) const {
      return !(*this == RHS);
    }
  private:
    bool advanceToValidPosition() {
      if (LiveRegI != (*BlockI)->livein_end())
        return true;

      do {
        ++BlockI;
      } while (BlockI != BlockEnd && (*BlockI)->livein_empty());
      if (BlockI == BlockEnd)
        return false;

      LiveRegI = (*BlockI)->livein_begin();
      return true;
    }

    MCPhysReg ExceptionPointer, ExceptionSelector;
    const_succ_iterator BlockI;
    const_succ_iterator BlockEnd;
    livein_iterator LiveRegI;
  };

  /// Iterator scanning successor basic blocks' liveins to determine the
  /// registers potentially live at the end of this block. There may be
  /// duplicates or overlapping registers in the list returned.
  liveout_iterator liveout_begin() const;
  liveout_iterator liveout_end() const {
    return liveout_iterator(*this, 0, 0, true);
  }
  iterator_range<liveout_iterator> liveouts() const {
    return make_range(liveout_begin(), liveout_end());
  }

  /// Get the clobber mask for the start of this basic block. Funclets use this
  /// to prevent register allocation across funclet transitions.
  const uint32_t *getBeginClobberMask(const TargetRegisterInfo *TRI) const;

  /// Get the clobber mask for the end of the basic block.
  /// \see getBeginClobberMask()
  const uint32_t *getEndClobberMask(const TargetRegisterInfo *TRI) const;

  /// Return alignment of the basic block.
  Align getAlignment() const { return Alignment; }

  /// Set alignment of the basic block.
  void setAlignment(Align A) { Alignment = A; }

  void setAlignment(Align A, unsigned MaxBytes) {
    setAlignment(A);
    setMaxBytesForAlignment(MaxBytes);
  }

  /// Return the maximum amount of padding allowed for aligning the basic block.
  unsigned getMaxBytesForAlignment() const { return MaxBytesForAlignment; }

  /// Set the maximum amount of padding allowed for aligning the basic block
  void setMaxBytesForAlignment(unsigned MaxBytes) {
    MaxBytesForAlignment = MaxBytes;
  }

  /// Returns true if the block is a landing pad. That is this basic block is
  /// entered via an exception handler.
  bool isEHPad() const { return IsEHPad; }

  /// Indicates the block is a landing pad.  That is this basic block is entered
  /// via an exception handler.
  void setIsEHPad(bool V = true) { IsEHPad = V; }

  bool hasEHPadSuccessor() const;

  /// Returns true if this is the entry block of the function.
  bool isEntryBlock() const;

  /// Returns true if this is the entry block of an EH scope, i.e., the block
  /// that used to have a catchpad or cleanuppad instruction in the LLVM IR.
  bool isEHScopeEntry() const { return IsEHScopeEntry; }

  /// Indicates if this is the entry block of an EH scope, i.e., the block that
  /// that used to have a catchpad or cleanuppad instruction in the LLVM IR.
  void setIsEHScopeEntry(bool V = true) { IsEHScopeEntry = V; }

  /// Returns true if this is a target block of a catchret.
  bool isEHCatchretTarget() const { return IsEHCatchretTarget; }

  /// Indicates if this is a target block of a catchret.
  void setIsEHCatchretTarget(bool V = true) { IsEHCatchretTarget = V; }

  /// Returns true if this is the entry block of an EH funclet.
  bool isEHFuncletEntry() const { return IsEHFuncletEntry; }

  /// Indicates if this is the entry block of an EH funclet.
  void setIsEHFuncletEntry(bool V = true) { IsEHFuncletEntry = V; }

  /// Returns true if this is the entry block of a cleanup funclet.
  bool isCleanupFuncletEntry() const { return IsCleanupFuncletEntry; }

  /// Indicates if this is the entry block of a cleanup funclet.
  void setIsCleanupFuncletEntry(bool V = true) { IsCleanupFuncletEntry = V; }

  /// Returns true if this block begins any section.
  bool isBeginSection() const { return IsBeginSection; }

  /// Returns true if this block ends any section.
  bool isEndSection() const { return IsEndSection; }

  void setIsBeginSection(bool V = true) { IsBeginSection = V; }

  void setIsEndSection(bool V = true) { IsEndSection = V; }

  std::optional<UniqueBBID> getBBID() const { return BBID; }

  /// Returns the section ID of this basic block.
  MBBSectionID getSectionID() const { return SectionID; }

  /// Sets the fixed BBID of this basic block.
  void setBBID(const UniqueBBID &V) {
    assert(!BBID.has_value() && "Cannot change BBID.");
    BBID = V;
  }

  /// Sets the section ID for this basic block.
  void setSectionID(MBBSectionID V) { SectionID = V; }

  /// Returns the MCSymbol marking the end of this basic block.
  MCSymbol *getEndSymbol() const;

  /// Returns true if this block may have an INLINEASM_BR (overestimate, by
  /// checking if any of the successors are indirect targets of any inlineasm_br
  /// in the function).
  bool mayHaveInlineAsmBr() const;

  /// Returns true if this is the indirect dest of an INLINEASM_BR.
  bool isInlineAsmBrIndirectTarget() const {
    return IsInlineAsmBrIndirectTarget;
  }

  /// Indicates if this is the indirect dest of an INLINEASM_BR.
  void setIsInlineAsmBrIndirectTarget(bool V = true) {
    IsInlineAsmBrIndirectTarget = V;
  }

  /// Returns true if it is legal to hoist instructions into this block.
  bool isLegalToHoistInto() const;

  // Code Layout methods.

  /// Move 'this' block before or after the specified block.  This only moves
  /// the block, it does not modify the CFG or adjust potential fall-throughs at
  /// the end of the block.
  void moveBefore(MachineBasicBlock *NewAfter);
  void moveAfter(MachineBasicBlock *NewBefore);

  /// Returns true if this and MBB belong to the same section.
  bool sameSection(const MachineBasicBlock *MBB) const {
    return getSectionID() == MBB->getSectionID();
  }

  /// Update the terminator instructions in block to account for changes to
  /// block layout which may have been made. PreviousLayoutSuccessor should be
  /// set to the block which may have been used as fallthrough before the block
  /// layout was modified.  If the block previously fell through to that block,
  /// it may now need a branch. If it previously branched to another block, it
  /// may now be able to fallthrough to the current layout successor.
  void updateTerminator(MachineBasicBlock *PreviousLayoutSuccessor);

  // Machine-CFG mutators

  /// Add Succ as a successor of this MachineBasicBlock.  The Predecessors list
  /// of Succ is automatically updated. PROB parameter is stored in
  /// Probabilities list. The default probability is set as unknown. Mixing
  /// known and unknown probabilities in successor list is not allowed. When all
  /// successors have unknown probabilities, 1 / N is returned as the
  /// probability for each successor, where N is the number of successors.
  ///
  /// Note that duplicate Machine CFG edges are not allowed.
  void addSuccessor(MachineBasicBlock *Succ,
                    BranchProbability Prob = BranchProbability::getUnknown());

  /// Add Succ as a successor of this MachineBasicBlock.  The Predecessors list
  /// of Succ is automatically updated. The probability is not provided because
  /// BPI is not available (e.g. -O0 is used), in which case edge probabilities
  /// won't be used. Using this interface can save some space.
  void addSuccessorWithoutProb(MachineBasicBlock *Succ);

  /// Set successor probability of a given iterator.
  void setSuccProbability(succ_iterator I, BranchProbability Prob);

  /// Normalize probabilities of all successors so that the sum of them becomes
  /// one. This is usually done when the current update on this MBB is done, and
  /// the sum of its successors' probabilities is not guaranteed to be one. The
  /// user is responsible for the correct use of this function.
  /// MBB::removeSuccessor() has an option to do this automatically.
  void normalizeSuccProbs() {
    BranchProbability::normalizeProbabilities(Probs.begin(), Probs.end());
  }

  /// Validate successors' probabilities and check if the sum of them is
  /// approximate one. This only works in DEBUG mode.
  void validateSuccProbs() const;

  /// Remove successor from the successors list of this MachineBasicBlock. The
  /// Predecessors list of Succ is automatically updated.
  /// If NormalizeSuccProbs is true, then normalize successors' probabilities
  /// after the successor is removed.
  void removeSuccessor(MachineBasicBlock *Succ,
                       bool NormalizeSuccProbs = false);

  /// Remove specified successor from the successors list of this
  /// MachineBasicBlock. The Predecessors list of Succ is automatically updated.
  /// If NormalizeSuccProbs is true, then normalize successors' probabilities
  /// after the successor is removed.
  /// Return the iterator to the element after the one removed.
  succ_iterator removeSuccessor(succ_iterator I,
                                bool NormalizeSuccProbs = false);

  /// Replace successor OLD with NEW and update probability info.
  void replaceSuccessor(MachineBasicBlock *Old, MachineBasicBlock *New);

  /// Copy a successor (and any probability info) from original block to this
  /// block's. Uses an iterator into the original blocks successors.
  ///
  /// This is useful when doing a partial clone of successors. Afterward, the
  /// probabilities may need to be normalized.
  void copySuccessor(const MachineBasicBlock *Orig, succ_iterator I);

  /// Split the old successor into old plus new and updates the probability
  /// info.
  void splitSuccessor(MachineBasicBlock *Old, MachineBasicBlock *New,
                      bool NormalizeSuccProbs = false);

  /// Transfers all the successors from MBB to this machine basic block (i.e.,
  /// copies all the successors FromMBB and remove all the successors from
  /// FromMBB).
  void transferSuccessors(MachineBasicBlock *FromMBB);

  /// Transfers all the successors, as in transferSuccessors, and update PHI
  /// operands in the successor blocks which refer to FromMBB to refer to this.
  void transferSuccessorsAndUpdatePHIs(MachineBasicBlock *FromMBB);

  /// Return true if any of the successors have probabilities attached to them.
  bool hasSuccessorProbabilities() const { return !Probs.empty(); }

  /// Return true if the specified MBB is a predecessor of this block.
  bool isPredecessor(const MachineBasicBlock *MBB) const;

  /// Return true if the specified MBB is a successor of this block.
  bool isSuccessor(const MachineBasicBlock *MBB) const;

  /// Return true if the specified MBB will be emitted immediately after this
  /// block, such that if this block exits by falling through, control will
  /// transfer to the specified MBB. Note that MBB need not be a successor at
  /// all, for example if this block ends with an unconditional branch to some
  /// other block.
  bool isLayoutSuccessor(const MachineBasicBlock *MBB) const;

  /// Return the successor of this block if it has a single successor.
  /// Otherwise return a null pointer.
  ///
  const MachineBasicBlock *getSingleSuccessor() const;
  MachineBasicBlock *getSingleSuccessor() {
    return const_cast<MachineBasicBlock *>(
        static_cast<const MachineBasicBlock *>(this)->getSingleSuccessor());
  }

  /// Return the predecessor of this block if it has a single predecessor.
  /// Otherwise return a null pointer.
  ///
  const MachineBasicBlock *getSinglePredecessor() const;
  MachineBasicBlock *getSinglePredecessor() {
    return const_cast<MachineBasicBlock *>(
        static_cast<const MachineBasicBlock *>(this)->getSinglePredecessor());
  }

  /// Return the fallthrough block if the block can implicitly
  /// transfer control to the block after it by falling off the end of
  /// it. If an explicit branch to the fallthrough block is not allowed,
  /// set JumpToFallThrough to be false. Non-null return is a conservative
  /// answer.
  MachineBasicBlock *getFallThrough(bool JumpToFallThrough = true);

  /// Return the fallthrough block if the block can implicitly
  /// transfer control to it's successor, whether by a branch or
  /// a fallthrough. Non-null return is a conservative answer.
  MachineBasicBlock *getLogicalFallThrough() { return getFallThrough(false); }

  /// Return true if the block can implicitly transfer control to the
  /// block after it by falling off the end of it.  This should return
  /// false if it can reach the block after it, but it uses an
  /// explicit branch to do so (e.g., a table jump).  True is a
  /// conservative answer.
  bool canFallThrough();

  /// Returns a pointer to the first instruction in this block that is not a
  /// PHINode instruction. When adding instructions to the beginning of the
  /// basic block, they should be added before the returned value, not before
  /// the first instruction, which might be PHI.
  /// Returns end() is there's no non-PHI instruction.
  iterator getFirstNonPHI();
  const_iterator getFirstNonPHI() const {
    return const_cast<MachineBasicBlock *>(this)->getFirstNonPHI();
  }

  /// Return the first instruction in MBB after I that is not a PHI or a label.
  /// This is the correct point to insert lowered copies at the beginning of a
  /// basic block that must be before any debugging information.
  iterator SkipPHIsAndLabels(iterator I);

  /// Return the first instruction in MBB after I that is not a PHI, label or
  /// debug.  This is the correct point to insert copies at the beginning of a
  /// basic block. \p Reg is the register being used by a spill or defined for a
  /// restore/split during register allocation.
  iterator SkipPHIsLabelsAndDebug(iterator I, Register Reg = Register(),
                                  bool SkipPseudoOp = true);

  /// Returns an iterator to the first terminator instruction of this basic
  /// block. If a terminator does not exist, it returns end().
  iterator getFirstTerminator();
  const_iterator getFirstTerminator() const {
    return const_cast<MachineBasicBlock *>(this)->getFirstTerminator();
  }

  /// Same getFirstTerminator but it ignores bundles and return an
  /// instr_iterator instead.
  instr_iterator getFirstInstrTerminator();

  /// Finds the first terminator in a block by scanning forward. This can handle
  /// cases in GlobalISel where there may be non-terminator instructions between
  /// terminators, for which getFirstTerminator() will not work correctly.
  iterator getFirstTerminatorForward();

  /// Returns an iterator to the first non-debug instruction in the basic block,
  /// or end(). Skip any pseudo probe operation if \c SkipPseudoOp is true.
  /// Pseudo probes are like debug instructions which do not turn into real
  /// machine code. We try to use the function to skip both debug instructions
  /// and pseudo probe operations to avoid API proliferation. This should work
  /// most of the time when considering optimizing the rest of code in the
  /// block, except for certain cases where pseudo probes are designed to block
  /// the optimizations. For example, code merge like optimizations are supposed
  /// to be blocked by pseudo probes for better AutoFDO profile quality.
  /// Therefore, they should be considered as a valid instruction when this
  /// function is called in a context of such optimizations. On the other hand,
  /// \c SkipPseudoOp should be true when it's used in optimizations that
  /// unlikely hurt profile quality, e.g., without block merging. The default
  /// value of \c SkipPseudoOp is set to true to maximize code quality in
  /// general, with an explict false value passed in in a few places like branch
  /// folding and if-conversion to favor profile quality.
  iterator getFirstNonDebugInstr(bool SkipPseudoOp = true);
  const_iterator getFirstNonDebugInstr(bool SkipPseudoOp = true) const {
    return const_cast<MachineBasicBlock *>(this)->getFirstNonDebugInstr(
        SkipPseudoOp);
  }

  /// Returns an iterator to the last non-debug instruction in the basic block,
  /// or end(). Skip any pseudo operation if \c SkipPseudoOp is true.
  /// Pseudo probes are like debug instructions which do not turn into real
  /// machine code. We try to use the function to skip both debug instructions
  /// and pseudo probe operations to avoid API proliferation. This should work
  /// most of the time when considering optimizing the rest of code in the
  /// block, except for certain cases where pseudo probes are designed to block
  /// the optimizations. For example, code merge like optimizations are supposed
  /// to be blocked by pseudo probes for better AutoFDO profile quality.
  /// Therefore, they should be considered as a valid instruction when this
  /// function is called in a context of such optimizations. On the other hand,
  /// \c SkipPseudoOp should be true when it's used in optimizations that
  /// unlikely hurt profile quality, e.g., without block merging. The default
  /// value of \c SkipPseudoOp is set to true to maximize code quality in
  /// general, with an explict false value passed in in a few places like branch
  /// folding and if-conversion to favor profile quality.
  iterator getLastNonDebugInstr(bool SkipPseudoOp = true);
  const_iterator getLastNonDebugInstr(bool SkipPseudoOp = true) const {
    return const_cast<MachineBasicBlock *>(this)->getLastNonDebugInstr(
        SkipPseudoOp);
  }

  /// Convenience function that returns true if the block ends in a return
  /// instruction.
  bool isReturnBlock() const {
    return !empty() && back().isReturn();
  }

  /// Convenience function that returns true if the bock ends in a EH scope
  /// return instruction.
  bool isEHScopeReturnBlock() const {
    return !empty() && back().isEHScopeReturn();
  }

  /// Split a basic block into 2 pieces at \p SplitPoint. A new block will be
  /// inserted after this block, and all instructions after \p SplitInst moved
  /// to it (\p SplitInst will be in the original block). If \p LIS is provided,
  /// LiveIntervals will be appropriately updated. \return the newly inserted
  /// block.
  ///
  /// If \p UpdateLiveIns is true, this will ensure the live ins list is
  /// accurate, including for physreg uses/defs in the original block.
  MachineBasicBlock *splitAt(MachineInstr &SplitInst, bool UpdateLiveIns = true,
                             LiveIntervals *LIS = nullptr);

  /// Split the critical edge from this block to the given successor block, and
  /// return the newly created block, or null if splitting is not possible.
  ///
  /// This function updates LiveVariables, MachineDominatorTree, and
  /// MachineLoopInfo, as applicable.
  MachineBasicBlock *
  SplitCriticalEdge(MachineBasicBlock *Succ, Pass &P,
                    std::vector<SparseBitVector<>> *LiveInSets = nullptr) {
    return SplitCriticalEdge(Succ, &P, nullptr, LiveInSets);
  }

  MachineBasicBlock *
  SplitCriticalEdge(MachineBasicBlock *Succ,
                    MachineFunctionAnalysisManager &MFAM,
                    std::vector<SparseBitVector<>> *LiveInSets = nullptr) {
    return SplitCriticalEdge(Succ, nullptr, &MFAM, LiveInSets);
  }

  /// Check if the edge between this block and the given successor \p
  /// Succ, can be split. If this returns true a subsequent call to
  /// SplitCriticalEdge is guaranteed to return a valid basic block if
  /// no changes occurred in the meantime.
  bool canSplitCriticalEdge(const MachineBasicBlock *Succ) const;

  void pop_front() { Insts.pop_front(); }
  void pop_back() { Insts.pop_back(); }
  void push_back(MachineInstr *MI) { Insts.push_back(MI); }

  /// Insert MI into the instruction list before I, possibly inside a bundle.
  ///
  /// If the insertion point is inside a bundle, MI will be added to the bundle,
  /// otherwise MI will not be added to any bundle. That means this function
  /// alone can't be used to prepend or append instructions to bundles. See
  /// MIBundleBuilder::insert() for a more reliable way of doing that.
  instr_iterator insert(instr_iterator I, MachineInstr *M);

  /// Insert a range of instructions into the instruction list before I.
  template<typename IT>
  void insert(iterator I, IT S, IT E) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    Insts.insert(I.getInstrIterator(), S, E);
  }

  /// Insert MI into the instruction list before I.
  iterator insert(iterator I, MachineInstr *MI) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
           "Cannot insert instruction with bundle flags");
    return Insts.insert(I.getInstrIterator(), MI);
  }

  /// Insert MI into the instruction list after I.
  iterator insertAfter(iterator I, MachineInstr *MI) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
           "Cannot insert instruction with bundle flags");
    return Insts.insertAfter(I.getInstrIterator(), MI);
  }

  /// If I is bundled then insert MI into the instruction list after the end of
  /// the bundle, otherwise insert MI immediately after I.
  instr_iterator insertAfterBundle(instr_iterator I, MachineInstr *MI) {
    assert((I == instr_end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
           "Cannot insert instruction with bundle flags");
    while (I->isBundledWithSucc())
      ++I;
    return Insts.insertAfter(I, MI);
  }

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  instr_iterator erase(instr_iterator I);

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  instr_iterator erase_instr(MachineInstr *I) {
    return erase(instr_iterator(I));
  }

  /// Remove a range of instructions from the instruction list and delete them.
  iterator erase(iterator I, iterator E) {
    return Insts.erase(I.getInstrIterator(), E.getInstrIterator());
  }

  /// Remove an instruction or bundle from the instruction list and delete it.
  ///
  /// If I points to a bundle of instructions, they are all erased.
  iterator erase(iterator I) {
    return erase(I, std::next(I));
  }

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If I is the head of a bundle of instructions, the whole bundle will be
  /// erased.
  iterator erase(MachineInstr *I) {
    return erase(iterator(I));
  }

  /// Remove the unbundled instruction from the instruction list without
  /// deleting it.
  ///
  /// This function can not be used to remove bundled instructions, use
  /// remove_instr to remove individual instructions from a bundle.
  MachineInstr *remove(MachineInstr *I) {
    assert(!I->isBundled() && "Cannot remove bundled instructions");
    return Insts.remove(instr_iterator(I));
  }

  /// Remove the possibly bundled instruction from the instruction list
  /// without deleting it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  MachineInstr *remove_instr(MachineInstr *I);

  void clear() {
    Insts.clear();
  }

  /// Take an instruction from MBB 'Other' at the position From, and insert it
  /// into this MBB right before 'Where'.
  ///
  /// If From points to a bundle of instructions, the whole bundle is moved.
  void splice(iterator Where, MachineBasicBlock *Other, iterator From) {
    // The range splice() doesn't allow noop moves, but this one does.
    if (Where != From)
      splice(Where, Other, From, std::next(From));
  }

  /// Take a block of instructions from MBB 'Other' in the range [From, To),
  /// and insert them into this MBB right before 'Where'.
  ///
  /// The instruction at 'Where' must not be included in the range of
  /// instructions to move.
  void splice(iterator Where, MachineBasicBlock *Other,
              iterator From, iterator To) {
    Insts.splice(Where.getInstrIterator(), Other->Insts,
                 From.getInstrIterator(), To.getInstrIterator());
  }

  /// This method unlinks 'this' from the containing function, and returns it,
  /// but does not delete it.
  MachineBasicBlock *removeFromParent();

  /// This method unlinks 'this' from the containing function and deletes it.
  void eraseFromParent();

  /// Given a machine basic block that branched to 'Old', change the code and
  /// CFG so that it branches to 'New' instead.
  void ReplaceUsesOfBlockWith(MachineBasicBlock *Old, MachineBasicBlock *New);

  /// Update all phi nodes in this basic block to refer to basic block \p New
  /// instead of basic block \p Old.
  void replacePhiUsesWith(MachineBasicBlock *Old, MachineBasicBlock *New);

  /// Find the next valid DebugLoc starting at MBBI, skipping any debug
  /// instructions.  Return UnknownLoc if there is none.
  DebugLoc findDebugLoc(instr_iterator MBBI);
  DebugLoc findDebugLoc(iterator MBBI) {
    return findDebugLoc(MBBI.getInstrIterator());
  }

  /// Has exact same behavior as @ref findDebugLoc (it also searches towards the
  /// end of this MBB) except that this function takes a reverse iterator to
  /// identify the starting MI.
  DebugLoc rfindDebugLoc(reverse_instr_iterator MBBI);
  DebugLoc rfindDebugLoc(reverse_iterator MBBI) {
    return rfindDebugLoc(MBBI.getInstrIterator());
  }

  /// Find the previous valid DebugLoc preceding MBBI, skipping any debug
  /// instructions. It is possible to find the last DebugLoc in the MBB using
  /// findPrevDebugLoc(instr_end()).  Return UnknownLoc if there is none.
  DebugLoc findPrevDebugLoc(instr_iterator MBBI);
  DebugLoc findPrevDebugLoc(iterator MBBI) {
    return findPrevDebugLoc(MBBI.getInstrIterator());
  }

  /// Has exact same behavior as @ref findPrevDebugLoc (it also searches towards
  /// the beginning of this MBB) except that this function takes reverse
  /// iterator to identify the starting MI. A minor difference compared to
  /// findPrevDebugLoc is that we can't start scanning at "instr_end".
  DebugLoc rfindPrevDebugLoc(reverse_instr_iterator MBBI);
  DebugLoc rfindPrevDebugLoc(reverse_iterator MBBI) {
    return rfindPrevDebugLoc(MBBI.getInstrIterator());
  }

  /// Find and return the merged DebugLoc of the branch instructions of the
  /// block. Return UnknownLoc if there is none.
  DebugLoc findBranchDebugLoc();

  /// Possible outcome of a register liveness query to computeRegisterLiveness()
  enum LivenessQueryResult {
    LQR_Live,   ///< Register is known to be (at least partially) live.
    LQR_Dead,   ///< Register is known to be fully dead.
    LQR_Unknown ///< Register liveness not decidable from local neighborhood.
  };

  /// Return whether (physical) register \p Reg has been defined and not
  /// killed as of just before \p Before.
  ///
  /// Search is localised to a neighborhood of \p Neighborhood instructions
  /// before (searching for defs or kills) and \p Neighborhood instructions
  /// after (searching just for defs) \p Before.
  ///
  /// \p Reg must be a physical register.
  LivenessQueryResult computeRegisterLiveness(const TargetRegisterInfo *TRI,
                                              MCRegister Reg,
                                              const_iterator Before,
                                              unsigned Neighborhood = 10) const;

  // Debugging methods.
  void dump() const;
  void print(raw_ostream &OS, const SlotIndexes * = nullptr,
             bool IsStandalone = true) const;
  void print(raw_ostream &OS, ModuleSlotTracker &MST,
             const SlotIndexes * = nullptr, bool IsStandalone = true) const;

  enum PrintNameFlag {
    PrintNameIr = (1 << 0), ///< Add IR name where available
    PrintNameAttributes = (1 << 1), ///< Print attributes
  };

  void printName(raw_ostream &os, unsigned printNameFlags = PrintNameIr,
                 ModuleSlotTracker *moduleSlotTracker = nullptr) const;

  // Printing method used by LoopInfo.
  void printAsOperand(raw_ostream &OS, bool PrintType = true) const;

  /// MachineBasicBlocks are uniquely numbered at the function level, unless
  /// they're not in a MachineFunction yet, in which case this will return -1.
  int getNumber() const { return Number; }
  void setNumber(int N) { Number = N; }

  /// Return the call frame size on entry to this basic block.
  unsigned getCallFrameSize() const { return CallFrameSize; }
  /// Set the call frame size on entry to this basic block.
  void setCallFrameSize(unsigned N) { CallFrameSize = N; }

  /// Return the MCSymbol for this basic block.
  MCSymbol *getSymbol() const;

  /// Return the EHCatchret Symbol for this basic block.
  MCSymbol *getEHCatchretSymbol() const;

  std::optional<uint64_t> getIrrLoopHeaderWeight() const {
    return IrrLoopHeaderWeight;
  }

  void setIrrLoopHeaderWeight(uint64_t Weight) {
    IrrLoopHeaderWeight = Weight;
  }

  /// Return probability of the edge from this block to MBB. This method should
  /// NOT be called directly, but by using getEdgeProbability method from
  /// MachineBranchProbabilityInfo class.
  BranchProbability getSuccProbability(const_succ_iterator Succ) const;

private:
  /// Return probability iterator corresponding to the I successor iterator.
  probability_iterator getProbabilityIterator(succ_iterator I);
  const_probability_iterator
  getProbabilityIterator(const_succ_iterator I) const;

  friend class MachineBranchProbabilityInfo;
  friend class MIPrinter;

  // Methods used to maintain doubly linked list of blocks...
  friend struct ilist_callback_traits<MachineBasicBlock>;

  // Machine-CFG mutators

  /// Add Pred as a predecessor of this MachineBasicBlock. Don't do this
  /// unless you know what you're doing, because it doesn't update Pred's
  /// successors list. Use Pred->addSuccessor instead.
  void addPredecessor(MachineBasicBlock *Pred);

  /// Remove Pred as a predecessor of this MachineBasicBlock. Don't do this
  /// unless you know what you're doing, because it doesn't update Pred's
  /// successors list. Use Pred->removeSuccessor instead.
  void removePredecessor(MachineBasicBlock *Pred);

  // Helper method for new pass manager migration.
  MachineBasicBlock *
  SplitCriticalEdge(MachineBasicBlock *Succ, Pass *P,
                    MachineFunctionAnalysisManager *MFAM,
                    std::vector<SparseBitVector<>> *LiveInSets);
};

raw_ostream& operator<<(raw_ostream &OS, const MachineBasicBlock &MBB);

/// Prints a machine basic block reference.
///
/// The format is:
///   %bb.5           - a machine basic block with MBB.getNumber() == 5.
///
/// Usage: OS << printMBBReference(MBB) << '\n';
Printable printMBBReference(const MachineBasicBlock &MBB);

// This is useful when building IndexedMaps keyed on basic block pointers.
struct MBB2NumberFunctor {
  using argument_type = const MachineBasicBlock *;
  unsigned operator()(const MachineBasicBlock *MBB) const {
    return MBB->getNumber();
  }
};

//===--------------------------------------------------------------------===//
// GraphTraits specializations for machine basic block graphs (machine-CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a
// MachineFunction as a graph of MachineBasicBlocks.
//

template <> struct GraphTraits<MachineBasicBlock *> {
  using NodeRef = MachineBasicBlock *;
  using ChildIteratorType = MachineBasicBlock::succ_iterator;

  static NodeRef getEntryNode(MachineBasicBlock *BB) { return BB; }
  static ChildIteratorType child_begin(NodeRef N) { return N->succ_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->succ_end(); }
};

template <> struct GraphTraits<const MachineBasicBlock *> {
  using NodeRef = const MachineBasicBlock *;
  using ChildIteratorType = MachineBasicBlock::const_succ_iterator;

  static NodeRef getEntryNode(const MachineBasicBlock *BB) { return BB; }
  static ChildIteratorType child_begin(NodeRef N) { return N->succ_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->succ_end(); }
};

// Provide specializations of GraphTraits to be able to treat a
// MachineFunction as a graph of MachineBasicBlocks and to walk it
// in inverse order.  Inverse order for a function is considered
// to be when traversing the predecessor edges of a MBB
// instead of the successor edges.
//
template <> struct GraphTraits<Inverse<MachineBasicBlock*>> {
  using NodeRef = MachineBasicBlock *;
  using ChildIteratorType = MachineBasicBlock::pred_iterator;

  static NodeRef getEntryNode(Inverse<MachineBasicBlock *> G) {
    return G.Graph;
  }

  static ChildIteratorType child_begin(NodeRef N) { return N->pred_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->pred_end(); }
};

template <> struct GraphTraits<Inverse<const MachineBasicBlock*>> {
  using NodeRef = const MachineBasicBlock *;
  using ChildIteratorType = MachineBasicBlock::const_pred_iterator;

  static NodeRef getEntryNode(Inverse<const MachineBasicBlock *> G) {
    return G.Graph;
  }

  static ChildIteratorType child_begin(NodeRef N) { return N->pred_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->pred_end(); }
};

// These accessors are handy for sharing templated code between IR and MIR.
inline auto successors(const MachineBasicBlock *BB) { return BB->successors(); }
inline auto predecessors(const MachineBasicBlock *BB) {
  return BB->predecessors();
}

/// MachineInstrSpan provides an interface to get an iteration range
/// containing the instruction it was initialized with, along with all
/// those instructions inserted prior to or following that instruction
/// at some point after the MachineInstrSpan is constructed.
class MachineInstrSpan {
  MachineBasicBlock &MBB;
  MachineBasicBlock::iterator I, B, E;

public:
  MachineInstrSpan(MachineBasicBlock::iterator I, MachineBasicBlock *BB)
      : MBB(*BB), I(I), B(I == MBB.begin() ? MBB.end() : std::prev(I)),
        E(std::next(I)) {
    assert(I == BB->end() || I->getParent() == BB);
  }

  MachineBasicBlock::iterator begin() {
    return B == MBB.end() ? MBB.begin() : std::next(B);
  }
  MachineBasicBlock::iterator end() { return E; }
  bool empty() { return begin() == end(); }

  MachineBasicBlock::iterator getInitial() { return I; }
};

/// Increment \p It until it points to a non-debug instruction or to \p End
/// and return the resulting iterator. This function should only be used
/// MachineBasicBlock::{iterator, const_iterator, instr_iterator,
/// const_instr_iterator} and the respective reverse iterators.
template <typename IterT>
inline IterT skipDebugInstructionsForward(IterT It, IterT End,
                                          bool SkipPseudoOp = true) {
  while (It != End &&
         (It->isDebugInstr() || (SkipPseudoOp && It->isPseudoProbe())))
    ++It;
  return It;
}

/// Decrement \p It until it points to a non-debug instruction or to \p Begin
/// and return the resulting iterator. This function should only be used
/// MachineBasicBlock::{iterator, const_iterator, instr_iterator,
/// const_instr_iterator} and the respective reverse iterators.
template <class IterT>
inline IterT skipDebugInstructionsBackward(IterT It, IterT Begin,
                                           bool SkipPseudoOp = true) {
  while (It != Begin &&
         (It->isDebugInstr() || (SkipPseudoOp && It->isPseudoProbe())))
    --It;
  return It;
}

/// Increment \p It, then continue incrementing it while it points to a debug
/// instruction. A replacement for std::next.
template <typename IterT>
inline IterT next_nodbg(IterT It, IterT End, bool SkipPseudoOp = true) {
  return skipDebugInstructionsForward(std::next(It), End, SkipPseudoOp);
}

/// Decrement \p It, then continue decrementing it while it points to a debug
/// instruction. A replacement for std::prev.
template <typename IterT>
inline IterT prev_nodbg(IterT It, IterT Begin, bool SkipPseudoOp = true) {
  return skipDebugInstructionsBackward(std::prev(It), Begin, SkipPseudoOp);
}

/// Construct a range iterator which begins at \p It and moves forwards until
/// \p End is reached, skipping any debug instructions.
template <typename IterT>
inline auto instructionsWithoutDebug(IterT It, IterT End,
                                     bool SkipPseudoOp = true) {
  return make_filter_range(make_range(It, End), [=](const MachineInstr &MI) {
    return !MI.isDebugInstr() && !(SkipPseudoOp && MI.isPseudoProbe());
  });
}

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEBASICBLOCK_H
