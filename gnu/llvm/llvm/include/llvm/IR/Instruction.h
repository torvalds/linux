//===-- llvm/Instruction.h - Instruction class definition -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Instruction class, which is the
// base class for all of the LLVM instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INSTRUCTION_H
#define LLVM_IR_INSTRUCTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Bitfields.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/AtomicOrdering.h"
#include <cstdint>
#include <utility>

namespace llvm {

class BasicBlock;
class DataLayout;
class DbgMarker;
class FastMathFlags;
class MDNode;
class Module;
struct AAMDNodes;
class DbgMarker;
class DbgRecord;

template <> struct ilist_alloc_traits<Instruction> {
  static inline void deleteNode(Instruction *V);
};

iterator_range<simple_ilist<DbgRecord>::iterator>
getDbgRecordRange(DbgMarker *);

class InsertPosition {
  using InstListType = SymbolTableList<Instruction, ilist_iterator_bits<true>,
                                       ilist_parent<BasicBlock>>;
  InstListType::iterator InsertAt;

public:
  InsertPosition(std::nullptr_t) : InsertAt() {}
  // LLVM_DEPRECATED("Use BasicBlock::iterators for insertion instead",
  // "BasicBlock::iterator")
  InsertPosition(Instruction *InsertBefore);
  InsertPosition(BasicBlock *InsertAtEnd);
  InsertPosition(InstListType::iterator InsertAt) : InsertAt(InsertAt) {}
  operator InstListType::iterator() const { return InsertAt; }
  bool isValid() const { return InsertAt.isValid(); }
  BasicBlock *getBasicBlock() { return InsertAt.getNodeParent(); }
};

class Instruction : public User,
                    public ilist_node_with_parent<Instruction, BasicBlock,
                                                  ilist_iterator_bits<true>,
                                                  ilist_parent<BasicBlock>> {
public:
  using InstListType = SymbolTableList<Instruction, ilist_iterator_bits<true>,
                                       ilist_parent<BasicBlock>>;

private:
  DebugLoc DbgLoc;                         // 'dbg' Metadata cache.

  /// Relative order of this instruction in its parent basic block. Used for
  /// O(1) local dominance checks between instructions.
  mutable unsigned Order = 0;

public:
  /// Optional marker recording the position for debugging information that
  /// takes effect immediately before this instruction. Null unless there is
  /// debugging information present.
  DbgMarker *DebugMarker = nullptr;

  /// Clone any debug-info attached to \p From onto this instruction. Used to
  /// copy debugging information from one block to another, when copying entire
  /// blocks. \see DebugProgramInstruction.h , because the ordering of
  /// DbgRecords is still important, fine grain control of which instructions
  /// are moved and where they go is necessary.
  /// \p From The instruction to clone debug-info from.
  /// \p from_here Optional iterator to limit DbgRecords cloned to be a range
  /// from
  ///    from_here to end().
  /// \p InsertAtHead Whether the cloned DbgRecords should be placed at the end
  ///    or the beginning of existing DbgRecords attached to this.
  /// \returns A range over the newly cloned DbgRecords.
  iterator_range<simple_ilist<DbgRecord>::iterator> cloneDebugInfoFrom(
      const Instruction *From,
      std::optional<simple_ilist<DbgRecord>::iterator> FromHere = std::nullopt,
      bool InsertAtHead = false);

  /// Return a range over the DbgRecords attached to this instruction.
  iterator_range<simple_ilist<DbgRecord>::iterator> getDbgRecordRange() const {
    return llvm::getDbgRecordRange(DebugMarker);
  }

  /// Return an iterator to the position of the "Next" DbgRecord after this
  /// instruction, or std::nullopt. This is the position to pass to
  /// BasicBlock::reinsertInstInDbgRecords when re-inserting an instruction.
  std::optional<simple_ilist<DbgRecord>::iterator> getDbgReinsertionPosition();

  /// Returns true if any DbgRecords are attached to this instruction.
  bool hasDbgRecords() const;

  /// Transfer any DbgRecords on the position \p It onto this instruction,
  /// by simply adopting the sequence of DbgRecords (which is efficient) if
  /// possible, by merging two sequences otherwise.
  void adoptDbgRecords(BasicBlock *BB, InstListType::iterator It,
                       bool InsertAtHead);

  /// Erase any DbgRecords attached to this instruction.
  void dropDbgRecords();

  /// Erase a single DbgRecord \p I that is attached to this instruction.
  void dropOneDbgRecord(DbgRecord *I);

  /// Handle the debug-info implications of this instruction being removed. Any
  /// attached DbgRecords need to "fall" down onto the next instruction.
  void handleMarkerRemoval();

protected:
  // The 15 first bits of `Value::SubclassData` are available for subclasses of
  // `Instruction` to use.
  using OpaqueField = Bitfield::Element<uint16_t, 0, 15>;

  // Template alias so that all Instruction storing alignment use the same
  // definiton.
  // Valid alignments are powers of two from 2^0 to 2^MaxAlignmentExponent =
  // 2^32. We store them as Log2(Alignment), so we need 6 bits to encode the 33
  // possible values.
  template <unsigned Offset>
  using AlignmentBitfieldElementT =
      typename Bitfield::Element<unsigned, Offset, 6,
                                 Value::MaxAlignmentExponent>;

  template <unsigned Offset>
  using BoolBitfieldElementT = typename Bitfield::Element<bool, Offset, 1>;

  template <unsigned Offset>
  using AtomicOrderingBitfieldElementT =
      typename Bitfield::Element<AtomicOrdering, Offset, 3,
                                 AtomicOrdering::LAST>;

private:
  // The last bit is used to store whether the instruction has metadata attached
  // or not.
  using HasMetadataField = Bitfield::Element<bool, 15, 1>;

protected:
  ~Instruction(); // Use deleteValue() to delete a generic Instruction.

public:
  Instruction(const Instruction &) = delete;
  Instruction &operator=(const Instruction &) = delete;

  /// Specialize the methods defined in Value, as we know that an instruction
  /// can only be used by other instructions.
  Instruction       *user_back()       { return cast<Instruction>(*user_begin());}
  const Instruction *user_back() const { return cast<Instruction>(*user_begin());}

  /// Return the module owning the function this instruction belongs to
  /// or nullptr it the function does not have a module.
  ///
  /// Note: this is undefined behavior if the instruction does not have a
  /// parent, or the parent basic block does not have a parent function.
  const Module *getModule() const;
  Module *getModule() {
    return const_cast<Module *>(
                           static_cast<const Instruction *>(this)->getModule());
  }

  /// Return the function this instruction belongs to.
  ///
  /// Note: it is undefined behavior to call this on an instruction not
  /// currently inserted into a function.
  const Function *getFunction() const;
  Function *getFunction() {
    return const_cast<Function *>(
                         static_cast<const Instruction *>(this)->getFunction());
  }

  /// Get the data layout of the module this instruction belongs to.
  ///
  /// Requires the instruction to have a parent module.
  const DataLayout &getDataLayout() const;

  /// This method unlinks 'this' from the containing basic block, but does not
  /// delete it.
  void removeFromParent();

  /// This method unlinks 'this' from the containing basic block and deletes it.
  ///
  /// \returns an iterator pointing to the element after the erased one
  InstListType::iterator eraseFromParent();

  /// Insert an unlinked instruction into a basic block immediately before
  /// the specified instruction.
  void insertBefore(Instruction *InsertPos);
  void insertBefore(InstListType::iterator InsertPos);

  /// Insert an unlinked instruction into a basic block immediately after the
  /// specified instruction.
  void insertAfter(Instruction *InsertPos);

  /// Inserts an unlinked instruction into \p ParentBB at position \p It and
  /// returns the iterator of the inserted instruction.
  InstListType::iterator insertInto(BasicBlock *ParentBB,
                                    InstListType::iterator It);

  void insertBefore(BasicBlock &BB, InstListType::iterator InsertPos);

  /// Unlink this instruction from its current basic block and insert it into
  /// the basic block that MovePos lives in, right before MovePos.
  void moveBefore(Instruction *MovePos);

  /// Perform a \ref moveBefore operation, while signalling that the caller
  /// intends to preserve the original ordering of instructions. This implicitly
  /// means that any adjacent debug-info should move with this instruction.
  /// This method is currently a no-op placeholder, but it will become meaningful
  /// when the "RemoveDIs" project is enabled.
  void moveBeforePreserving(Instruction *MovePos);

private:
  /// RemoveDIs project: all other moves implemented with this method,
  /// centralising debug-info updates into one place.
  void moveBeforeImpl(BasicBlock &BB, InstListType::iterator I, bool Preserve);

public:
  /// Unlink this instruction and insert into BB before I.
  ///
  /// \pre I is a valid iterator into BB.
  void moveBefore(BasicBlock &BB, InstListType::iterator I);

  /// (See other overload for moveBeforePreserving).
  void moveBeforePreserving(BasicBlock &BB, InstListType::iterator I);

  /// Unlink this instruction from its current basic block and insert it into
  /// the basic block that MovePos lives in, right after MovePos.
  void moveAfter(Instruction *MovePos);

  /// See \ref moveBeforePreserving .
  void moveAfterPreserving(Instruction *MovePos);

  /// Given an instruction Other in the same basic block as this instruction,
  /// return true if this instruction comes before Other. In this worst case,
  /// this takes linear time in the number of instructions in the block. The
  /// results are cached, so in common cases when the block remains unmodified,
  /// it takes constant time.
  bool comesBefore(const Instruction *Other) const;

  /// Get the first insertion point at which the result of this instruction
  /// is defined. This is *not* the directly following instruction in a number
  /// of cases, e.g. phi nodes or terminators that return values. This function
  /// may return null if the insertion after the definition is not possible,
  /// e.g. due to a catchswitch terminator.
  std::optional<InstListType::iterator> getInsertionPointAfterDef();

  //===--------------------------------------------------------------------===//
  // Subclass classification.
  //===--------------------------------------------------------------------===//

  /// Returns a member of one of the enums like Instruction::Add.
  unsigned getOpcode() const { return getValueID() - InstructionVal; }

  const char *getOpcodeName() const { return getOpcodeName(getOpcode()); }
  bool isTerminator() const { return isTerminator(getOpcode()); }
  bool isUnaryOp() const { return isUnaryOp(getOpcode()); }
  bool isBinaryOp() const { return isBinaryOp(getOpcode()); }
  bool isIntDivRem() const { return isIntDivRem(getOpcode()); }
  bool isShift() const { return isShift(getOpcode()); }
  bool isCast() const { return isCast(getOpcode()); }
  bool isFuncletPad() const { return isFuncletPad(getOpcode()); }
  bool isSpecialTerminator() const { return isSpecialTerminator(getOpcode()); }

  /// It checks if this instruction is the only user of at least one of
  /// its operands.
  bool isOnlyUserOfAnyOperand();

  static const char *getOpcodeName(unsigned Opcode);

  static inline bool isTerminator(unsigned Opcode) {
    return Opcode >= TermOpsBegin && Opcode < TermOpsEnd;
  }

  static inline bool isUnaryOp(unsigned Opcode) {
    return Opcode >= UnaryOpsBegin && Opcode < UnaryOpsEnd;
  }
  static inline bool isBinaryOp(unsigned Opcode) {
    return Opcode >= BinaryOpsBegin && Opcode < BinaryOpsEnd;
  }

  static inline bool isIntDivRem(unsigned Opcode) {
    return Opcode == UDiv || Opcode == SDiv || Opcode == URem || Opcode == SRem;
  }

  /// Determine if the Opcode is one of the shift instructions.
  static inline bool isShift(unsigned Opcode) {
    return Opcode >= Shl && Opcode <= AShr;
  }

  /// Return true if this is a logical shift left or a logical shift right.
  inline bool isLogicalShift() const {
    return getOpcode() == Shl || getOpcode() == LShr;
  }

  /// Return true if this is an arithmetic shift right.
  inline bool isArithmeticShift() const {
    return getOpcode() == AShr;
  }

  /// Determine if the Opcode is and/or/xor.
  static inline bool isBitwiseLogicOp(unsigned Opcode) {
    return Opcode == And || Opcode == Or || Opcode == Xor;
  }

  /// Return true if this is and/or/xor.
  inline bool isBitwiseLogicOp() const {
    return isBitwiseLogicOp(getOpcode());
  }

  /// Determine if the Opcode is one of the CastInst instructions.
  static inline bool isCast(unsigned Opcode) {
    return Opcode >= CastOpsBegin && Opcode < CastOpsEnd;
  }

  /// Determine if the Opcode is one of the FuncletPadInst instructions.
  static inline bool isFuncletPad(unsigned Opcode) {
    return Opcode >= FuncletPadOpsBegin && Opcode < FuncletPadOpsEnd;
  }

  /// Returns true if the Opcode is a "special" terminator that does more than
  /// branch to a successor (e.g. have a side effect or return a value).
  static inline bool isSpecialTerminator(unsigned Opcode) {
    switch (Opcode) {
    case Instruction::CatchSwitch:
    case Instruction::CatchRet:
    case Instruction::CleanupRet:
    case Instruction::Invoke:
    case Instruction::Resume:
    case Instruction::CallBr:
      return true;
    default:
      return false;
    }
  }

  //===--------------------------------------------------------------------===//
  // Metadata manipulation.
  //===--------------------------------------------------------------------===//

  /// Return true if this instruction has any metadata attached to it.
  bool hasMetadata() const { return DbgLoc || Value::hasMetadata(); }

  /// Return true if this instruction has metadata attached to it other than a
  /// debug location.
  bool hasMetadataOtherThanDebugLoc() const { return Value::hasMetadata(); }

  /// Return true if this instruction has the given type of metadata attached.
  bool hasMetadata(unsigned KindID) const {
    return getMetadata(KindID) != nullptr;
  }

  /// Return true if this instruction has the given type of metadata attached.
  bool hasMetadata(StringRef Kind) const {
    return getMetadata(Kind) != nullptr;
  }

  /// Get the metadata of given kind attached to this Instruction.
  /// If the metadata is not found then return null.
  MDNode *getMetadata(unsigned KindID) const {
    // Handle 'dbg' as a special case since it is not stored in the hash table.
    if (KindID == LLVMContext::MD_dbg)
      return DbgLoc.getAsMDNode();
    return Value::getMetadata(KindID);
  }

  /// Get the metadata of given kind attached to this Instruction.
  /// If the metadata is not found then return null.
  MDNode *getMetadata(StringRef Kind) const {
    if (!hasMetadata()) return nullptr;
    return getMetadataImpl(Kind);
  }

  /// Get all metadata attached to this Instruction. The first element of each
  /// pair returned is the KindID, the second element is the metadata value.
  /// This list is returned sorted by the KindID.
  void
  getAllMetadata(SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs) const {
    if (hasMetadata())
      getAllMetadataImpl(MDs);
  }

  /// This does the same thing as getAllMetadata, except that it filters out the
  /// debug location.
  void getAllMetadataOtherThanDebugLoc(
      SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs) const {
    Value::getAllMetadata(MDs);
  }

  /// Set the metadata of the specified kind to the specified node. This updates
  /// or replaces metadata if already present, or removes it if Node is null.
  void setMetadata(unsigned KindID, MDNode *Node);
  void setMetadata(StringRef Kind, MDNode *Node);

  /// Copy metadata from \p SrcInst to this instruction. \p WL, if not empty,
  /// specifies the list of meta data that needs to be copied. If \p WL is
  /// empty, all meta data will be copied.
  void copyMetadata(const Instruction &SrcInst,
                    ArrayRef<unsigned> WL = ArrayRef<unsigned>());

  /// Erase all metadata that matches the predicate.
  void eraseMetadataIf(function_ref<bool(unsigned, MDNode *)> Pred);

  /// If the instruction has "branch_weights" MD_prof metadata and the MDNode
  /// has three operands (including name string), swap the order of the
  /// metadata.
  void swapProfMetadata();

  /// Drop all unknown metadata except for debug locations.
  /// @{
  /// Passes are required to drop metadata they don't understand. This is a
  /// convenience method for passes to do so.
  /// dropUBImplyingAttrsAndUnknownMetadata should be used instead of
  /// this API if the Instruction being modified is a call.
  void dropUnknownNonDebugMetadata(ArrayRef<unsigned> KnownIDs = std::nullopt);
  /// @}

  /// Adds an !annotation metadata node with \p Annotation to this instruction.
  /// If this instruction already has !annotation metadata, append \p Annotation
  /// to the existing node.
  void addAnnotationMetadata(StringRef Annotation);
  /// Adds an !annotation metadata node with an array of \p Annotations
  /// as a tuple to this instruction. If this instruction already has
  /// !annotation metadata, append the tuple to
  /// the existing node.
  void addAnnotationMetadata(SmallVector<StringRef> Annotations);
  /// Returns the AA metadata for this instruction.
  AAMDNodes getAAMetadata() const;

  /// Sets the AA metadata on this instruction from the AAMDNodes structure.
  void setAAMetadata(const AAMDNodes &N);

  /// Sets the nosanitize metadata on this instruction.
  void setNoSanitizeMetadata();

  /// Retrieve total raw weight values of a branch.
  /// Returns true on success with profile total weights filled in.
  /// Returns false if no metadata was found.
  bool extractProfTotalWeight(uint64_t &TotalVal) const;

  /// Set the debug location information for this instruction.
  void setDebugLoc(DebugLoc Loc) { DbgLoc = std::move(Loc); }

  /// Return the debug location for this node as a DebugLoc.
  const DebugLoc &getDebugLoc() const { return DbgLoc; }

  /// Fetch the debug location for this node, unless this is a debug intrinsic,
  /// in which case fetch the debug location of the next non-debug node.
  const DebugLoc &getStableDebugLoc() const;

  /// Set or clear the nuw flag on this instruction, which must be an operator
  /// which supports this flag. See LangRef.html for the meaning of this flag.
  void setHasNoUnsignedWrap(bool b = true);

  /// Set or clear the nsw flag on this instruction, which must be an operator
  /// which supports this flag. See LangRef.html for the meaning of this flag.
  void setHasNoSignedWrap(bool b = true);

  /// Set or clear the exact flag on this instruction, which must be an operator
  /// which supports this flag. See LangRef.html for the meaning of this flag.
  void setIsExact(bool b = true);

  /// Set or clear the nneg flag on this instruction, which must be a zext
  /// instruction.
  void setNonNeg(bool b = true);

  /// Determine whether the no unsigned wrap flag is set.
  bool hasNoUnsignedWrap() const LLVM_READONLY;

  /// Determine whether the no signed wrap flag is set.
  bool hasNoSignedWrap() const LLVM_READONLY;

  /// Determine whether the the nneg flag is set.
  bool hasNonNeg() const LLVM_READONLY;

  /// Return true if this operator has flags which may cause this instruction
  /// to evaluate to poison despite having non-poison inputs.
  bool hasPoisonGeneratingFlags() const LLVM_READONLY;

  /// Drops flags that may cause this instruction to evaluate to poison despite
  /// having non-poison inputs.
  void dropPoisonGeneratingFlags();

  /// Return true if this instruction has poison-generating metadata.
  bool hasPoisonGeneratingMetadata() const LLVM_READONLY;

  /// Drops metadata that may generate poison.
  void dropPoisonGeneratingMetadata();

  /// Return true if this instruction has poison-generating attribute.
  bool hasPoisonGeneratingReturnAttributes() const LLVM_READONLY;

  /// Drops return attributes that may generate poison.
  void dropPoisonGeneratingReturnAttributes();

  /// Return true if this instruction has poison-generating flags,
  /// return attributes or metadata.
  bool hasPoisonGeneratingAnnotations() const {
    return hasPoisonGeneratingFlags() ||
           hasPoisonGeneratingReturnAttributes() ||
           hasPoisonGeneratingMetadata();
  }

  /// Drops flags, return attributes and metadata that may generate poison.
  void dropPoisonGeneratingAnnotations() {
    dropPoisonGeneratingFlags();
    dropPoisonGeneratingReturnAttributes();
    dropPoisonGeneratingMetadata();
  }

  /// This function drops non-debug unknown metadata (through
  /// dropUnknownNonDebugMetadata). For calls, it also drops parameter and
  /// return attributes that can cause undefined behaviour. Both of these should
  /// be done by passes which move instructions in IR.
  void dropUBImplyingAttrsAndUnknownMetadata(ArrayRef<unsigned> KnownIDs = {});

  /// Drop any attributes or metadata that can cause immediate undefined
  /// behavior. Retain other attributes/metadata on a best-effort basis.
  /// This should be used when speculating instructions.
  void dropUBImplyingAttrsAndMetadata();

  /// Determine whether the exact flag is set.
  bool isExact() const LLVM_READONLY;

  /// Set or clear all fast-math-flags on this instruction, which must be an
  /// operator which supports this flag. See LangRef.html for the meaning of
  /// this flag.
  void setFast(bool B);

  /// Set or clear the reassociation flag on this instruction, which must be
  /// an operator which supports this flag. See LangRef.html for the meaning of
  /// this flag.
  void setHasAllowReassoc(bool B);

  /// Set or clear the no-nans flag on this instruction, which must be an
  /// operator which supports this flag. See LangRef.html for the meaning of
  /// this flag.
  void setHasNoNaNs(bool B);

  /// Set or clear the no-infs flag on this instruction, which must be an
  /// operator which supports this flag. See LangRef.html for the meaning of
  /// this flag.
  void setHasNoInfs(bool B);

  /// Set or clear the no-signed-zeros flag on this instruction, which must be
  /// an operator which supports this flag. See LangRef.html for the meaning of
  /// this flag.
  void setHasNoSignedZeros(bool B);

  /// Set or clear the allow-reciprocal flag on this instruction, which must be
  /// an operator which supports this flag. See LangRef.html for the meaning of
  /// this flag.
  void setHasAllowReciprocal(bool B);

  /// Set or clear the allow-contract flag on this instruction, which must be
  /// an operator which supports this flag. See LangRef.html for the meaning of
  /// this flag.
  void setHasAllowContract(bool B);

  /// Set or clear the approximate-math-functions flag on this instruction,
  /// which must be an operator which supports this flag. See LangRef.html for
  /// the meaning of this flag.
  void setHasApproxFunc(bool B);

  /// Convenience function for setting multiple fast-math flags on this
  /// instruction, which must be an operator which supports these flags. See
  /// LangRef.html for the meaning of these flags.
  void setFastMathFlags(FastMathFlags FMF);

  /// Convenience function for transferring all fast-math flag values to this
  /// instruction, which must be an operator which supports these flags. See
  /// LangRef.html for the meaning of these flags.
  void copyFastMathFlags(FastMathFlags FMF);

  /// Determine whether all fast-math-flags are set.
  bool isFast() const LLVM_READONLY;

  /// Determine whether the allow-reassociation flag is set.
  bool hasAllowReassoc() const LLVM_READONLY;

  /// Determine whether the no-NaNs flag is set.
  bool hasNoNaNs() const LLVM_READONLY;

  /// Determine whether the no-infs flag is set.
  bool hasNoInfs() const LLVM_READONLY;

  /// Determine whether the no-signed-zeros flag is set.
  bool hasNoSignedZeros() const LLVM_READONLY;

  /// Determine whether the allow-reciprocal flag is set.
  bool hasAllowReciprocal() const LLVM_READONLY;

  /// Determine whether the allow-contract flag is set.
  bool hasAllowContract() const LLVM_READONLY;

  /// Determine whether the approximate-math-functions flag is set.
  bool hasApproxFunc() const LLVM_READONLY;

  /// Convenience function for getting all the fast-math flags, which must be an
  /// operator which supports these flags. See LangRef.html for the meaning of
  /// these flags.
  FastMathFlags getFastMathFlags() const LLVM_READONLY;

  /// Copy I's fast-math flags
  void copyFastMathFlags(const Instruction *I);

  /// Convenience method to copy supported exact, fast-math, and (optionally)
  /// wrapping flags from V to this instruction.
  void copyIRFlags(const Value *V, bool IncludeWrapFlags = true);

  /// Logical 'and' of any supported wrapping, exact, and fast-math flags of
  /// V and this instruction.
  void andIRFlags(const Value *V);

  /// Merge 2 debug locations and apply it to the Instruction. If the
  /// instruction is a CallIns, we need to traverse the inline chain to find
  /// the common scope. This is not efficient for N-way merging as each time
  /// you merge 2 iterations, you need to rebuild the hashmap to find the
  /// common scope. However, we still choose this API because:
  ///  1) Simplicity: it takes 2 locations instead of a list of locations.
  ///  2) In worst case, it increases the complexity from O(N*I) to
  ///     O(2*N*I), where N is # of Instructions to merge, and I is the
  ///     maximum level of inline stack. So it is still linear.
  ///  3) Merging of call instructions should be extremely rare in real
  ///     applications, thus the N-way merging should be in code path.
  /// The DebugLoc attached to this instruction will be overwritten by the
  /// merged DebugLoc.
  void applyMergedLocation(DILocation *LocA, DILocation *LocB);

  /// Updates the debug location given that the instruction has been hoisted
  /// from a block to a predecessor of that block.
  /// Note: it is undefined behavior to call this on an instruction not
  /// currently inserted into a function.
  void updateLocationAfterHoist();

  /// Drop the instruction's debug location. This does not guarantee removal
  /// of the !dbg source location attachment, as it must set a line 0 location
  /// with scope information attached on call instructions. To guarantee
  /// removal of the !dbg attachment, use the \ref setDebugLoc() API.
  /// Note: it is undefined behavior to call this on an instruction not
  /// currently inserted into a function.
  void dropLocation();

  /// Merge the DIAssignID metadata from this instruction and those attached to
  /// instructions in \p SourceInstructions. This process performs a RAUW on
  /// the MetadataAsValue uses of the merged DIAssignID nodes. Not every
  /// instruction in \p SourceInstructions needs to have DIAssignID
  /// metadata. If none of them do then nothing happens. If this instruction
  /// does not have a DIAssignID attachment but at least one in \p
  /// SourceInstructions does then the merged one will be attached to
  /// it. However, instructions without attachments in \p SourceInstructions
  /// are not modified.
  void mergeDIAssignID(ArrayRef<const Instruction *> SourceInstructions);

private:
  // These are all implemented in Metadata.cpp.
  MDNode *getMetadataImpl(StringRef Kind) const;
  void
  getAllMetadataImpl(SmallVectorImpl<std::pair<unsigned, MDNode *>> &) const;

  /// Update the LLVMContext ID-to-Instruction(s) mapping. If \p ID is nullptr
  /// then clear the mapping for this instruction.
  void updateDIAssignIDMapping(DIAssignID *ID);

public:
  //===--------------------------------------------------------------------===//
  // Predicates and helper methods.
  //===--------------------------------------------------------------------===//

  /// Return true if the instruction is associative:
  ///
  ///   Associative operators satisfy:  x op (y op z) === (x op y) op z
  ///
  /// In LLVM, the Add, Mul, And, Or, and Xor operators are associative.
  ///
  bool isAssociative() const LLVM_READONLY;
  static bool isAssociative(unsigned Opcode) {
    return Opcode == And || Opcode == Or || Opcode == Xor ||
           Opcode == Add || Opcode == Mul;
  }

  /// Return true if the instruction is commutative:
  ///
  ///   Commutative operators satisfy: (x op y) === (y op x)
  ///
  /// In LLVM, these are the commutative operators, plus SetEQ and SetNE, when
  /// applied to any type.
  ///
  bool isCommutative() const LLVM_READONLY;
  static bool isCommutative(unsigned Opcode) {
    switch (Opcode) {
    case Add: case FAdd:
    case Mul: case FMul:
    case And: case Or: case Xor:
      return true;
    default:
      return false;
  }
  }

  /// Return true if the instruction is idempotent:
  ///
  ///   Idempotent operators satisfy:  x op x === x
  ///
  /// In LLVM, the And and Or operators are idempotent.
  ///
  bool isIdempotent() const { return isIdempotent(getOpcode()); }
  static bool isIdempotent(unsigned Opcode) {
    return Opcode == And || Opcode == Or;
  }

  /// Return true if the instruction is nilpotent:
  ///
  ///   Nilpotent operators satisfy:  x op x === Id,
  ///
  ///   where Id is the identity for the operator, i.e. a constant such that
  ///     x op Id === x and Id op x === x for all x.
  ///
  /// In LLVM, the Xor operator is nilpotent.
  ///
  bool isNilpotent() const { return isNilpotent(getOpcode()); }
  static bool isNilpotent(unsigned Opcode) {
    return Opcode == Xor;
  }

  /// Return true if this instruction may modify memory.
  bool mayWriteToMemory() const LLVM_READONLY;

  /// Return true if this instruction may read memory.
  bool mayReadFromMemory() const LLVM_READONLY;

  /// Return true if this instruction may read or write memory.
  bool mayReadOrWriteMemory() const {
    return mayReadFromMemory() || mayWriteToMemory();
  }

  /// Return true if this instruction has an AtomicOrdering of unordered or
  /// higher.
  bool isAtomic() const LLVM_READONLY;

  /// Return true if this atomic instruction loads from memory.
  bool hasAtomicLoad() const LLVM_READONLY;

  /// Return true if this atomic instruction stores to memory.
  bool hasAtomicStore() const LLVM_READONLY;

  /// Return true if this instruction has a volatile memory access.
  bool isVolatile() const LLVM_READONLY;

  /// Return the type this instruction accesses in memory, if any.
  Type *getAccessType() const LLVM_READONLY;

  /// Return true if this instruction may throw an exception.
  ///
  /// If IncludePhaseOneUnwind is set, this will also include cases where
  /// phase one unwinding may unwind past this frame due to skipping of
  /// cleanup landingpads.
  bool mayThrow(bool IncludePhaseOneUnwind = false) const LLVM_READONLY;

  /// Return true if this instruction behaves like a memory fence: it can load
  /// or store to memory location without being given a memory location.
  bool isFenceLike() const {
    switch (getOpcode()) {
    default:
      return false;
    // This list should be kept in sync with the list in mayWriteToMemory for
    // all opcodes which don't have a memory location.
    case Instruction::Fence:
    case Instruction::CatchPad:
    case Instruction::CatchRet:
    case Instruction::Call:
    case Instruction::Invoke:
      return true;
    }
  }

  /// Return true if the instruction may have side effects.
  ///
  /// Side effects are:
  ///  * Writing to memory.
  ///  * Unwinding.
  ///  * Not returning (e.g. an infinite loop).
  ///
  /// Note that this does not consider malloc and alloca to have side
  /// effects because the newly allocated memory is completely invisible to
  /// instructions which don't use the returned value.  For cases where this
  /// matters, isSafeToSpeculativelyExecute may be more appropriate.
  bool mayHaveSideEffects() const LLVM_READONLY;

  /// Return true if the instruction can be removed if the result is unused.
  ///
  /// When constant folding some instructions cannot be removed even if their
  /// results are unused. Specifically terminator instructions and calls that
  /// may have side effects cannot be removed without semantically changing the
  /// generated program.
  bool isSafeToRemove() const LLVM_READONLY;

  /// Return true if the instruction will return (unwinding is considered as
  /// a form of returning control flow here).
  bool willReturn() const LLVM_READONLY;

  /// Return true if the instruction is a variety of EH-block.
  bool isEHPad() const {
    switch (getOpcode()) {
    case Instruction::CatchSwitch:
    case Instruction::CatchPad:
    case Instruction::CleanupPad:
    case Instruction::LandingPad:
      return true;
    default:
      return false;
    }
  }

  /// Return true if the instruction is a llvm.lifetime.start or
  /// llvm.lifetime.end marker.
  bool isLifetimeStartOrEnd() const LLVM_READONLY;

  /// Return true if the instruction is a llvm.launder.invariant.group or
  /// llvm.strip.invariant.group.
  bool isLaunderOrStripInvariantGroup() const LLVM_READONLY;

  /// Return true if the instruction is a DbgInfoIntrinsic or PseudoProbeInst.
  bool isDebugOrPseudoInst() const LLVM_READONLY;

  /// Return a pointer to the next non-debug instruction in the same basic
  /// block as 'this', or nullptr if no such instruction exists. Skip any pseudo
  /// operations if \c SkipPseudoOp is true.
  const Instruction *
  getNextNonDebugInstruction(bool SkipPseudoOp = false) const;
  Instruction *getNextNonDebugInstruction(bool SkipPseudoOp = false) {
    return const_cast<Instruction *>(
        static_cast<const Instruction *>(this)->getNextNonDebugInstruction(
            SkipPseudoOp));
  }

  /// Return a pointer to the previous non-debug instruction in the same basic
  /// block as 'this', or nullptr if no such instruction exists. Skip any pseudo
  /// operations if \c SkipPseudoOp is true.
  const Instruction *
  getPrevNonDebugInstruction(bool SkipPseudoOp = false) const;
  Instruction *getPrevNonDebugInstruction(bool SkipPseudoOp = false) {
    return const_cast<Instruction *>(
        static_cast<const Instruction *>(this)->getPrevNonDebugInstruction(
            SkipPseudoOp));
  }

  /// Create a copy of 'this' instruction that is identical in all ways except
  /// the following:
  ///   * The instruction has no parent
  ///   * The instruction has no name
  ///
  Instruction *clone() const;

  /// Return true if the specified instruction is exactly identical to the
  /// current one. This means that all operands match and any extra information
  /// (e.g. load is volatile) agree.
  bool isIdenticalTo(const Instruction *I) const LLVM_READONLY;

  /// This is like isIdenticalTo, except that it ignores the
  /// SubclassOptionalData flags, which may specify conditions under which the
  /// instruction's result is undefined.
  bool isIdenticalToWhenDefined(const Instruction *I) const LLVM_READONLY;

  /// When checking for operation equivalence (using isSameOperationAs) it is
  /// sometimes useful to ignore certain attributes.
  enum OperationEquivalenceFlags {
    /// Check for equivalence ignoring load/store alignment.
    CompareIgnoringAlignment = 1<<0,
    /// Check for equivalence treating a type and a vector of that type
    /// as equivalent.
    CompareUsingScalarTypes = 1<<1
  };

  /// This function determines if the specified instruction executes the same
  /// operation as the current one. This means that the opcodes, type, operand
  /// types and any other factors affecting the operation must be the same. This
  /// is similar to isIdenticalTo except the operands themselves don't have to
  /// be identical.
  /// @returns true if the specified instruction is the same operation as
  /// the current one.
  /// Determine if one instruction is the same operation as another.
  bool isSameOperationAs(const Instruction *I, unsigned flags = 0) const LLVM_READONLY;

  /// This function determines if the speficied instruction has the same
  /// "special" characteristics as the current one. This means that opcode
  /// specific details are the same. As a common example, if we are comparing
  /// loads, then hasSameSpecialState would compare the alignments (among
  /// other things).
  /// @returns true if the specific instruction has the same opcde specific
  /// characteristics as the current one. Determine if one instruction has the
  /// same state as another.
  bool hasSameSpecialState(const Instruction *I2,
                           bool IgnoreAlignment = false) const LLVM_READONLY;

  /// Return true if there are any uses of this instruction in blocks other than
  /// the specified block. Note that PHI nodes are considered to evaluate their
  /// operands in the corresponding predecessor block.
  bool isUsedOutsideOfBlock(const BasicBlock *BB) const LLVM_READONLY;

  /// Return the number of successors that this instruction has. The instruction
  /// must be a terminator.
  unsigned getNumSuccessors() const LLVM_READONLY;

  /// Return the specified successor. This instruction must be a terminator.
  BasicBlock *getSuccessor(unsigned Idx) const LLVM_READONLY;

  /// Update the specified successor to point at the provided block. This
  /// instruction must be a terminator.
  void setSuccessor(unsigned Idx, BasicBlock *BB);

  /// Replace specified successor OldBB to point at the provided block.
  /// This instruction must be a terminator.
  void replaceSuccessorWith(BasicBlock *OldBB, BasicBlock *NewBB);

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Value *V) {
    return V->getValueID() >= Value::InstructionVal;
  }

  //----------------------------------------------------------------------
  // Exported enumerations.
  //
  enum TermOps {       // These terminate basic blocks
#define  FIRST_TERM_INST(N)             TermOpsBegin = N,
#define HANDLE_TERM_INST(N, OPC, CLASS) OPC = N,
#define   LAST_TERM_INST(N)             TermOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  enum UnaryOps {
#define  FIRST_UNARY_INST(N)             UnaryOpsBegin = N,
#define HANDLE_UNARY_INST(N, OPC, CLASS) OPC = N,
#define   LAST_UNARY_INST(N)             UnaryOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  enum BinaryOps {
#define  FIRST_BINARY_INST(N)             BinaryOpsBegin = N,
#define HANDLE_BINARY_INST(N, OPC, CLASS) OPC = N,
#define   LAST_BINARY_INST(N)             BinaryOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  enum MemoryOps {
#define  FIRST_MEMORY_INST(N)             MemoryOpsBegin = N,
#define HANDLE_MEMORY_INST(N, OPC, CLASS) OPC = N,
#define   LAST_MEMORY_INST(N)             MemoryOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  enum CastOps {
#define  FIRST_CAST_INST(N)             CastOpsBegin = N,
#define HANDLE_CAST_INST(N, OPC, CLASS) OPC = N,
#define   LAST_CAST_INST(N)             CastOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  enum FuncletPadOps {
#define  FIRST_FUNCLETPAD_INST(N)             FuncletPadOpsBegin = N,
#define HANDLE_FUNCLETPAD_INST(N, OPC, CLASS) OPC = N,
#define   LAST_FUNCLETPAD_INST(N)             FuncletPadOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  enum OtherOps {
#define  FIRST_OTHER_INST(N)             OtherOpsBegin = N,
#define HANDLE_OTHER_INST(N, OPC, CLASS) OPC = N,
#define   LAST_OTHER_INST(N)             OtherOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

private:
  friend class SymbolTableListTraits<Instruction, ilist_iterator_bits<true>,
                                     ilist_parent<BasicBlock>>;
  friend class BasicBlock; // For renumbering.

  // Shadow Value::setValueSubclassData with a private forwarding method so that
  // subclasses cannot accidentally use it.
  void setValueSubclassData(unsigned short D) {
    Value::setValueSubclassData(D);
  }

  unsigned short getSubclassDataFromValue() const {
    return Value::getSubclassDataFromValue();
  }

protected:
  // Instruction subclasses can stick up to 15 bits of stuff into the
  // SubclassData field of instruction with these members.

  template <typename BitfieldElement>
  typename BitfieldElement::Type getSubclassData() const {
    static_assert(
        std::is_same<BitfieldElement, HasMetadataField>::value ||
            !Bitfield::isOverlapping<BitfieldElement, HasMetadataField>(),
        "Must not overlap with the metadata bit");
    return Bitfield::get<BitfieldElement>(getSubclassDataFromValue());
  }

  template <typename BitfieldElement>
  void setSubclassData(typename BitfieldElement::Type Value) {
    static_assert(
        std::is_same<BitfieldElement, HasMetadataField>::value ||
            !Bitfield::isOverlapping<BitfieldElement, HasMetadataField>(),
        "Must not overlap with the metadata bit");
    auto Storage = getSubclassDataFromValue();
    Bitfield::set<BitfieldElement>(Storage, Value);
    setValueSubclassData(Storage);
  }

  Instruction(Type *Ty, unsigned iType, Use *Ops, unsigned NumOps,
              InsertPosition InsertBefore = nullptr);

private:
  /// Create a copy of this instruction.
  Instruction *cloneImpl() const;
};

inline void ilist_alloc_traits<Instruction>::deleteNode(Instruction *V) {
  V->deleteValue();
}

} // end namespace llvm

#endif // LLVM_IR_INSTRUCTION_H
