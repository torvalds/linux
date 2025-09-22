//===- MemorySSA.h - Build Memory SSA ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file exposes an interface to building/using memory SSA to
/// walk memory instructions using a use/def graph.
///
/// Memory SSA class builds an SSA form that links together memory access
/// instructions such as loads, stores, atomics, and calls. Additionally, it
/// does a trivial form of "heap versioning" Every time the memory state changes
/// in the program, we generate a new heap version. It generates
/// MemoryDef/Uses/Phis that are overlayed on top of the existing instructions.
///
/// As a trivial example,
/// define i32 @main() #0 {
/// entry:
///   %call = call noalias i8* @_Znwm(i64 4) #2
///   %0 = bitcast i8* %call to i32*
///   %call1 = call noalias i8* @_Znwm(i64 4) #2
///   %1 = bitcast i8* %call1 to i32*
///   store i32 5, i32* %0, align 4
///   store i32 7, i32* %1, align 4
///   %2 = load i32* %0, align 4
///   %3 = load i32* %1, align 4
///   %add = add nsw i32 %2, %3
///   ret i32 %add
/// }
///
/// Will become
/// define i32 @main() #0 {
/// entry:
///   ; 1 = MemoryDef(0)
///   %call = call noalias i8* @_Znwm(i64 4) #3
///   %2 = bitcast i8* %call to i32*
///   ; 2 = MemoryDef(1)
///   %call1 = call noalias i8* @_Znwm(i64 4) #3
///   %4 = bitcast i8* %call1 to i32*
///   ; 3 = MemoryDef(2)
///   store i32 5, i32* %2, align 4
///   ; 4 = MemoryDef(3)
///   store i32 7, i32* %4, align 4
///   ; MemoryUse(3)
///   %7 = load i32* %2, align 4
///   ; MemoryUse(4)
///   %8 = load i32* %4, align 4
///   %add = add nsw i32 %7, %8
///   ret i32 %add
/// }
///
/// Given this form, all the stores that could ever effect the load at %8 can be
/// gotten by using the MemoryUse associated with it, and walking from use to
/// def until you hit the top of the function.
///
/// Each def also has a list of users associated with it, so you can walk from
/// both def to users, and users to defs. Note that we disambiguate MemoryUses,
/// but not the RHS of MemoryDefs. You can see this above at %7, which would
/// otherwise be a MemoryUse(4). Being disambiguated means that for a given
/// store, all the MemoryUses on its use lists are may-aliases of that store
/// (but the MemoryDefs on its use list may not be).
///
/// MemoryDefs are not disambiguated because it would require multiple reaching
/// definitions, which would require multiple phis, and multiple memoryaccesses
/// per instruction.
///
/// In addition to the def/use graph described above, MemoryDefs also contain
/// an "optimized" definition use.  The "optimized" use points to some def
/// reachable through the memory def chain.  The optimized def *may* (but is
/// not required to) alias the original MemoryDef, but no def *closer* to the
/// source def may alias it.  As the name implies, the purpose of the optimized
/// use is to allow caching of clobber searches for memory defs.  The optimized
/// def may be nullptr, in which case clients must walk the defining access
/// chain.
///
/// When iterating the uses of a MemoryDef, both defining uses and optimized
/// uses will be encountered.  If only one type is needed, the client must
/// filter the use walk.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYSSA_H
#define LLVM_ANALYSIS_MEMORYSSA_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/PHITransAddr.h"
#include "llvm/IR/DerivedUser.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/Pass.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>

namespace llvm {

template <class GraphType> struct GraphTraits;
class BasicBlock;
class Function;
class Loop;
class Instruction;
class LLVMContext;
class MemoryAccess;
class MemorySSAWalker;
class Module;
class Use;
class Value;
class raw_ostream;

namespace MSSAHelpers {

struct AllAccessTag {};
struct DefsOnlyTag {};

} // end namespace MSSAHelpers

enum : unsigned {
  // Used to signify what the default invalid ID is for MemoryAccess's
  // getID()
  INVALID_MEMORYACCESS_ID = -1U
};

template <class T> class memoryaccess_def_iterator_base;
using memoryaccess_def_iterator = memoryaccess_def_iterator_base<MemoryAccess>;
using const_memoryaccess_def_iterator =
    memoryaccess_def_iterator_base<const MemoryAccess>;

// The base for all memory accesses. All memory accesses in a block are
// linked together using an intrusive list.
class MemoryAccess
    : public DerivedUser,
      public ilist_node<MemoryAccess, ilist_tag<MSSAHelpers::AllAccessTag>>,
      public ilist_node<MemoryAccess, ilist_tag<MSSAHelpers::DefsOnlyTag>> {
public:
  using AllAccessType =
      ilist_node<MemoryAccess, ilist_tag<MSSAHelpers::AllAccessTag>>;
  using DefsOnlyType =
      ilist_node<MemoryAccess, ilist_tag<MSSAHelpers::DefsOnlyTag>>;

  MemoryAccess(const MemoryAccess &) = delete;
  MemoryAccess &operator=(const MemoryAccess &) = delete;

  void *operator new(size_t) = delete;

  // Methods for support type inquiry through isa, cast, and
  // dyn_cast
  static bool classof(const Value *V) {
    unsigned ID = V->getValueID();
    return ID == MemoryUseVal || ID == MemoryPhiVal || ID == MemoryDefVal;
  }

  BasicBlock *getBlock() const { return Block; }

  void print(raw_ostream &OS) const;
  void dump() const;

  /// The user iterators for a memory access
  using iterator = user_iterator;
  using const_iterator = const_user_iterator;

  /// This iterator walks over all of the defs in a given
  /// MemoryAccess. For MemoryPhi nodes, this walks arguments. For
  /// MemoryUse/MemoryDef, this walks the defining access.
  memoryaccess_def_iterator defs_begin();
  const_memoryaccess_def_iterator defs_begin() const;
  memoryaccess_def_iterator defs_end();
  const_memoryaccess_def_iterator defs_end() const;

  /// Get the iterators for the all access list and the defs only list
  /// We default to the all access list.
  AllAccessType::self_iterator getIterator() {
    return this->AllAccessType::getIterator();
  }
  AllAccessType::const_self_iterator getIterator() const {
    return this->AllAccessType::getIterator();
  }
  AllAccessType::reverse_self_iterator getReverseIterator() {
    return this->AllAccessType::getReverseIterator();
  }
  AllAccessType::const_reverse_self_iterator getReverseIterator() const {
    return this->AllAccessType::getReverseIterator();
  }
  DefsOnlyType::self_iterator getDefsIterator() {
    return this->DefsOnlyType::getIterator();
  }
  DefsOnlyType::const_self_iterator getDefsIterator() const {
    return this->DefsOnlyType::getIterator();
  }
  DefsOnlyType::reverse_self_iterator getReverseDefsIterator() {
    return this->DefsOnlyType::getReverseIterator();
  }
  DefsOnlyType::const_reverse_self_iterator getReverseDefsIterator() const {
    return this->DefsOnlyType::getReverseIterator();
  }

protected:
  friend class MemoryDef;
  friend class MemoryPhi;
  friend class MemorySSA;
  friend class MemoryUse;
  friend class MemoryUseOrDef;

  /// Used by MemorySSA to change the block of a MemoryAccess when it is
  /// moved.
  void setBlock(BasicBlock *BB) { Block = BB; }

  /// Used for debugging and tracking things about MemoryAccesses.
  /// Guaranteed unique among MemoryAccesses, no guarantees otherwise.
  inline unsigned getID() const;

  MemoryAccess(LLVMContext &C, unsigned Vty, DeleteValueTy DeleteValue,
               BasicBlock *BB, unsigned NumOperands)
      : DerivedUser(Type::getVoidTy(C), Vty, nullptr, NumOperands, DeleteValue),
        Block(BB) {}

  // Use deleteValue() to delete a generic MemoryAccess.
  ~MemoryAccess() = default;

private:
  BasicBlock *Block;
};

template <>
struct ilist_alloc_traits<MemoryAccess> {
  static void deleteNode(MemoryAccess *MA) { MA->deleteValue(); }
};

inline raw_ostream &operator<<(raw_ostream &OS, const MemoryAccess &MA) {
  MA.print(OS);
  return OS;
}

/// Class that has the common methods + fields of memory uses/defs. It's
/// a little awkward to have, but there are many cases where we want either a
/// use or def, and there are many cases where uses are needed (defs aren't
/// acceptable), and vice-versa.
///
/// This class should never be instantiated directly; make a MemoryUse or
/// MemoryDef instead.
class MemoryUseOrDef : public MemoryAccess {
public:
  void *operator new(size_t) = delete;

  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(MemoryAccess);

  /// Get the instruction that this MemoryUse represents.
  Instruction *getMemoryInst() const { return MemoryInstruction; }

  /// Get the access that produces the memory state used by this Use.
  MemoryAccess *getDefiningAccess() const { return getOperand(0); }

  static bool classof(const Value *MA) {
    return MA->getValueID() == MemoryUseVal || MA->getValueID() == MemoryDefVal;
  }

  /// Do we have an optimized use?
  inline bool isOptimized() const;
  /// Return the MemoryAccess associated with the optimized use, or nullptr.
  inline MemoryAccess *getOptimized() const;
  /// Sets the optimized use for a MemoryDef.
  inline void setOptimized(MemoryAccess *);

  /// Reset the ID of what this MemoryUse was optimized to, causing it to
  /// be rewalked by the walker if necessary.
  /// This really should only be called by tests.
  inline void resetOptimized();

protected:
  friend class MemorySSA;
  friend class MemorySSAUpdater;

  MemoryUseOrDef(LLVMContext &C, MemoryAccess *DMA, unsigned Vty,
                 DeleteValueTy DeleteValue, Instruction *MI, BasicBlock *BB,
                 unsigned NumOperands)
      : MemoryAccess(C, Vty, DeleteValue, BB, NumOperands),
        MemoryInstruction(MI) {
    setDefiningAccess(DMA);
  }

  // Use deleteValue() to delete a generic MemoryUseOrDef.
  ~MemoryUseOrDef() = default;

  void setDefiningAccess(MemoryAccess *DMA, bool Optimized = false) {
    if (!Optimized) {
      setOperand(0, DMA);
      return;
    }
    setOptimized(DMA);
  }

private:
  Instruction *MemoryInstruction;
};

/// Represents read-only accesses to memory
///
/// In particular, the set of Instructions that will be represented by
/// MemoryUse's is exactly the set of Instructions for which
/// AliasAnalysis::getModRefInfo returns "Ref".
class MemoryUse final : public MemoryUseOrDef {
public:
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(MemoryAccess);

  MemoryUse(LLVMContext &C, MemoryAccess *DMA, Instruction *MI, BasicBlock *BB)
      : MemoryUseOrDef(C, DMA, MemoryUseVal, deleteMe, MI, BB,
                       /*NumOperands=*/1) {}

  // allocate space for exactly one operand
  void *operator new(size_t S) { return User::operator new(S, 1); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  static bool classof(const Value *MA) {
    return MA->getValueID() == MemoryUseVal;
  }

  void print(raw_ostream &OS) const;

  void setOptimized(MemoryAccess *DMA) {
    OptimizedID = DMA->getID();
    setOperand(0, DMA);
  }

  /// Whether the MemoryUse is optimized. If ensureOptimizedUses() was called,
  /// uses will usually be optimized, but this is not guaranteed (e.g. due to
  /// invalidation and optimization limits.)
  bool isOptimized() const {
    return getDefiningAccess() && OptimizedID == getDefiningAccess()->getID();
  }

  MemoryAccess *getOptimized() const {
    return getDefiningAccess();
  }

  void resetOptimized() {
    OptimizedID = INVALID_MEMORYACCESS_ID;
  }

protected:
  friend class MemorySSA;

private:
  static void deleteMe(DerivedUser *Self);

  unsigned OptimizedID = INVALID_MEMORYACCESS_ID;
};

template <>
struct OperandTraits<MemoryUse> : public FixedNumOperandTraits<MemoryUse, 1> {};
DEFINE_TRANSPARENT_OPERAND_ACCESSORS(MemoryUse, MemoryAccess)

/// Represents a read-write access to memory, whether it is a must-alias,
/// or a may-alias.
///
/// In particular, the set of Instructions that will be represented by
/// MemoryDef's is exactly the set of Instructions for which
/// AliasAnalysis::getModRefInfo returns "Mod" or "ModRef".
/// Note that, in order to provide def-def chains, all defs also have a use
/// associated with them. This use points to the nearest reaching
/// MemoryDef/MemoryPhi.
class MemoryDef final : public MemoryUseOrDef {
public:
  friend class MemorySSA;

  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(MemoryAccess);

  MemoryDef(LLVMContext &C, MemoryAccess *DMA, Instruction *MI, BasicBlock *BB,
            unsigned Ver)
      : MemoryUseOrDef(C, DMA, MemoryDefVal, deleteMe, MI, BB,
                       /*NumOperands=*/2),
        ID(Ver) {}

  // allocate space for exactly two operands
  void *operator new(size_t S) { return User::operator new(S, 2); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  static bool classof(const Value *MA) {
    return MA->getValueID() == MemoryDefVal;
  }

  void setOptimized(MemoryAccess *MA) {
    setOperand(1, MA);
    OptimizedID = MA->getID();
  }

  MemoryAccess *getOptimized() const {
    return cast_or_null<MemoryAccess>(getOperand(1));
  }

  bool isOptimized() const {
    return getOptimized() && OptimizedID == getOptimized()->getID();
  }

  void resetOptimized() {
    OptimizedID = INVALID_MEMORYACCESS_ID;
    setOperand(1, nullptr);
  }

  void print(raw_ostream &OS) const;

  unsigned getID() const { return ID; }

private:
  static void deleteMe(DerivedUser *Self);

  const unsigned ID;
  unsigned OptimizedID = INVALID_MEMORYACCESS_ID;
};

template <>
struct OperandTraits<MemoryDef> : public FixedNumOperandTraits<MemoryDef, 2> {};
DEFINE_TRANSPARENT_OPERAND_ACCESSORS(MemoryDef, MemoryAccess)

template <>
struct OperandTraits<MemoryUseOrDef> {
  static Use *op_begin(MemoryUseOrDef *MUD) {
    if (auto *MU = dyn_cast<MemoryUse>(MUD))
      return OperandTraits<MemoryUse>::op_begin(MU);
    return OperandTraits<MemoryDef>::op_begin(cast<MemoryDef>(MUD));
  }

  static Use *op_end(MemoryUseOrDef *MUD) {
    if (auto *MU = dyn_cast<MemoryUse>(MUD))
      return OperandTraits<MemoryUse>::op_end(MU);
    return OperandTraits<MemoryDef>::op_end(cast<MemoryDef>(MUD));
  }

  static unsigned operands(const MemoryUseOrDef *MUD) {
    if (const auto *MU = dyn_cast<MemoryUse>(MUD))
      return OperandTraits<MemoryUse>::operands(MU);
    return OperandTraits<MemoryDef>::operands(cast<MemoryDef>(MUD));
  }
};
DEFINE_TRANSPARENT_OPERAND_ACCESSORS(MemoryUseOrDef, MemoryAccess)

/// Represents phi nodes for memory accesses.
///
/// These have the same semantic as regular phi nodes, with the exception that
/// only one phi will ever exist in a given basic block.
/// Guaranteeing one phi per block means guaranteeing there is only ever one
/// valid reaching MemoryDef/MemoryPHI along each path to the phi node.
/// This is ensured by not allowing disambiguation of the RHS of a MemoryDef or
/// a MemoryPhi's operands.
/// That is, given
/// if (a) {
///   store %a
///   store %b
/// }
/// it *must* be transformed into
/// if (a) {
///    1 = MemoryDef(liveOnEntry)
///    store %a
///    2 = MemoryDef(1)
///    store %b
/// }
/// and *not*
/// if (a) {
///    1 = MemoryDef(liveOnEntry)
///    store %a
///    2 = MemoryDef(liveOnEntry)
///    store %b
/// }
/// even if the two stores do not conflict. Otherwise, both 1 and 2 reach the
/// end of the branch, and if there are not two phi nodes, one will be
/// disconnected completely from the SSA graph below that point.
/// Because MemoryUse's do not generate new definitions, they do not have this
/// issue.
class MemoryPhi final : public MemoryAccess {
  // allocate space for exactly zero operands
  void *operator new(size_t S) { return User::operator new(S); }

public:
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(MemoryAccess);

  MemoryPhi(LLVMContext &C, BasicBlock *BB, unsigned Ver, unsigned NumPreds = 0)
      : MemoryAccess(C, MemoryPhiVal, deleteMe, BB, 0), ID(Ver),
        ReservedSpace(NumPreds) {
    allocHungoffUses(ReservedSpace);
  }

  // Block iterator interface. This provides access to the list of incoming
  // basic blocks, which parallels the list of incoming values.
  using block_iterator = BasicBlock **;
  using const_block_iterator = BasicBlock *const *;

  block_iterator block_begin() {
    return reinterpret_cast<block_iterator>(op_begin() + ReservedSpace);
  }

  const_block_iterator block_begin() const {
    return reinterpret_cast<const_block_iterator>(op_begin() + ReservedSpace);
  }

  block_iterator block_end() { return block_begin() + getNumOperands(); }

  const_block_iterator block_end() const {
    return block_begin() + getNumOperands();
  }

  iterator_range<block_iterator> blocks() {
    return make_range(block_begin(), block_end());
  }

  iterator_range<const_block_iterator> blocks() const {
    return make_range(block_begin(), block_end());
  }

  op_range incoming_values() { return operands(); }

  const_op_range incoming_values() const { return operands(); }

  /// Return the number of incoming edges
  unsigned getNumIncomingValues() const { return getNumOperands(); }

  /// Return incoming value number x
  MemoryAccess *getIncomingValue(unsigned I) const { return getOperand(I); }
  void setIncomingValue(unsigned I, MemoryAccess *V) {
    assert(V && "PHI node got a null value!");
    setOperand(I, V);
  }

  static unsigned getOperandNumForIncomingValue(unsigned I) { return I; }
  static unsigned getIncomingValueNumForOperand(unsigned I) { return I; }

  /// Return incoming basic block number @p i.
  BasicBlock *getIncomingBlock(unsigned I) const { return block_begin()[I]; }

  /// Return incoming basic block corresponding
  /// to an operand of the PHI.
  BasicBlock *getIncomingBlock(const Use &U) const {
    assert(this == U.getUser() && "Iterator doesn't point to PHI's Uses?");
    return getIncomingBlock(unsigned(&U - op_begin()));
  }

  /// Return incoming basic block corresponding
  /// to value use iterator.
  BasicBlock *getIncomingBlock(MemoryAccess::const_user_iterator I) const {
    return getIncomingBlock(I.getUse());
  }

  void setIncomingBlock(unsigned I, BasicBlock *BB) {
    assert(BB && "PHI node got a null basic block!");
    block_begin()[I] = BB;
  }

  /// Add an incoming value to the end of the PHI list
  void addIncoming(MemoryAccess *V, BasicBlock *BB) {
    if (getNumOperands() == ReservedSpace)
      growOperands(); // Get more space!
    // Initialize some new operands.
    setNumHungOffUseOperands(getNumOperands() + 1);
    setIncomingValue(getNumOperands() - 1, V);
    setIncomingBlock(getNumOperands() - 1, BB);
  }

  /// Return the first index of the specified basic
  /// block in the value list for this PHI.  Returns -1 if no instance.
  int getBasicBlockIndex(const BasicBlock *BB) const {
    for (unsigned I = 0, E = getNumOperands(); I != E; ++I)
      if (block_begin()[I] == BB)
        return I;
    return -1;
  }

  MemoryAccess *getIncomingValueForBlock(const BasicBlock *BB) const {
    int Idx = getBasicBlockIndex(BB);
    assert(Idx >= 0 && "Invalid basic block argument!");
    return getIncomingValue(Idx);
  }

  // After deleting incoming position I, the order of incoming may be changed.
  void unorderedDeleteIncoming(unsigned I) {
    unsigned E = getNumOperands();
    assert(I < E && "Cannot remove out of bounds Phi entry.");
    // MemoryPhi must have at least two incoming values, otherwise the MemoryPhi
    // itself should be deleted.
    assert(E >= 2 && "Cannot only remove incoming values in MemoryPhis with "
                     "at least 2 values.");
    setIncomingValue(I, getIncomingValue(E - 1));
    setIncomingBlock(I, block_begin()[E - 1]);
    setOperand(E - 1, nullptr);
    block_begin()[E - 1] = nullptr;
    setNumHungOffUseOperands(getNumOperands() - 1);
  }

  // After deleting entries that satisfy Pred, remaining entries may have
  // changed order.
  template <typename Fn> void unorderedDeleteIncomingIf(Fn &&Pred) {
    for (unsigned I = 0, E = getNumOperands(); I != E; ++I)
      if (Pred(getIncomingValue(I), getIncomingBlock(I))) {
        unorderedDeleteIncoming(I);
        E = getNumOperands();
        --I;
      }
    assert(getNumOperands() >= 1 &&
           "Cannot remove all incoming blocks in a MemoryPhi.");
  }

  // After deleting incoming block BB, the incoming blocks order may be changed.
  void unorderedDeleteIncomingBlock(const BasicBlock *BB) {
    unorderedDeleteIncomingIf(
        [&](const MemoryAccess *, const BasicBlock *B) { return BB == B; });
  }

  // After deleting incoming memory access MA, the incoming accesses order may
  // be changed.
  void unorderedDeleteIncomingValue(const MemoryAccess *MA) {
    unorderedDeleteIncomingIf(
        [&](const MemoryAccess *M, const BasicBlock *) { return MA == M; });
  }

  static bool classof(const Value *V) {
    return V->getValueID() == MemoryPhiVal;
  }

  void print(raw_ostream &OS) const;

  unsigned getID() const { return ID; }

protected:
  friend class MemorySSA;

  /// this is more complicated than the generic
  /// User::allocHungoffUses, because we have to allocate Uses for the incoming
  /// values and pointers to the incoming blocks, all in one allocation.
  void allocHungoffUses(unsigned N) {
    User::allocHungoffUses(N, /* IsPhi */ true);
  }

private:
  // For debugging only
  const unsigned ID;
  unsigned ReservedSpace;

  /// This grows the operand list in response to a push_back style of
  /// operation.  This grows the number of ops by 1.5 times.
  void growOperands() {
    unsigned E = getNumOperands();
    // 2 op PHI nodes are VERY common, so reserve at least enough for that.
    ReservedSpace = std::max(E + E / 2, 2u);
    growHungoffUses(ReservedSpace, /* IsPhi */ true);
  }

  static void deleteMe(DerivedUser *Self);
};

inline unsigned MemoryAccess::getID() const {
  assert((isa<MemoryDef>(this) || isa<MemoryPhi>(this)) &&
         "only memory defs and phis have ids");
  if (const auto *MD = dyn_cast<MemoryDef>(this))
    return MD->getID();
  return cast<MemoryPhi>(this)->getID();
}

inline bool MemoryUseOrDef::isOptimized() const {
  if (const auto *MD = dyn_cast<MemoryDef>(this))
    return MD->isOptimized();
  return cast<MemoryUse>(this)->isOptimized();
}

inline MemoryAccess *MemoryUseOrDef::getOptimized() const {
  if (const auto *MD = dyn_cast<MemoryDef>(this))
    return MD->getOptimized();
  return cast<MemoryUse>(this)->getOptimized();
}

inline void MemoryUseOrDef::setOptimized(MemoryAccess *MA) {
  if (auto *MD = dyn_cast<MemoryDef>(this))
    MD->setOptimized(MA);
  else
    cast<MemoryUse>(this)->setOptimized(MA);
}

inline void MemoryUseOrDef::resetOptimized() {
  if (auto *MD = dyn_cast<MemoryDef>(this))
    MD->resetOptimized();
  else
    cast<MemoryUse>(this)->resetOptimized();
}

template <> struct OperandTraits<MemoryPhi> : public HungoffOperandTraits<2> {};
DEFINE_TRANSPARENT_OPERAND_ACCESSORS(MemoryPhi, MemoryAccess)

/// Encapsulates MemorySSA, including all data associated with memory
/// accesses.
class MemorySSA {
public:
  MemorySSA(Function &, AliasAnalysis *, DominatorTree *);
  MemorySSA(Loop &, AliasAnalysis *, DominatorTree *);

  // MemorySSA must remain where it's constructed; Walkers it creates store
  // pointers to it.
  MemorySSA(MemorySSA &&) = delete;

  ~MemorySSA();

  MemorySSAWalker *getWalker();
  MemorySSAWalker *getSkipSelfWalker();

  /// Given a memory Mod/Ref'ing instruction, get the MemorySSA
  /// access associated with it. If passed a basic block gets the memory phi
  /// node that exists for that block, if there is one. Otherwise, this will get
  /// a MemoryUseOrDef.
  MemoryUseOrDef *getMemoryAccess(const Instruction *I) const {
    return cast_or_null<MemoryUseOrDef>(ValueToMemoryAccess.lookup(I));
  }

  MemoryPhi *getMemoryAccess(const BasicBlock *BB) const {
    return cast_or_null<MemoryPhi>(ValueToMemoryAccess.lookup(cast<Value>(BB)));
  }

  DominatorTree &getDomTree() const { return *DT; }

  void dump() const;
  void print(raw_ostream &) const;

  /// Return true if \p MA represents the live on entry value
  ///
  /// Loads and stores from pointer arguments and other global values may be
  /// defined by memory operations that do not occur in the current function, so
  /// they may be live on entry to the function. MemorySSA represents such
  /// memory state by the live on entry definition, which is guaranteed to occur
  /// before any other memory access in the function.
  inline bool isLiveOnEntryDef(const MemoryAccess *MA) const {
    return MA == LiveOnEntryDef.get();
  }

  inline MemoryAccess *getLiveOnEntryDef() const {
    return LiveOnEntryDef.get();
  }

  // Sadly, iplists, by default, owns and deletes pointers added to the
  // list. It's not currently possible to have two iplists for the same type,
  // where one owns the pointers, and one does not. This is because the traits
  // are per-type, not per-tag.  If this ever changes, we should make the
  // DefList an iplist.
  using AccessList = iplist<MemoryAccess, ilist_tag<MSSAHelpers::AllAccessTag>>;
  using DefsList =
      simple_ilist<MemoryAccess, ilist_tag<MSSAHelpers::DefsOnlyTag>>;

  /// Return the list of MemoryAccess's for a given basic block.
  ///
  /// This list is not modifiable by the user.
  const AccessList *getBlockAccesses(const BasicBlock *BB) const {
    return getWritableBlockAccesses(BB);
  }

  /// Return the list of MemoryDef's and MemoryPhi's for a given basic
  /// block.
  ///
  /// This list is not modifiable by the user.
  const DefsList *getBlockDefs(const BasicBlock *BB) const {
    return getWritableBlockDefs(BB);
  }

  /// Given two memory accesses in the same basic block, determine
  /// whether MemoryAccess \p A dominates MemoryAccess \p B.
  bool locallyDominates(const MemoryAccess *A, const MemoryAccess *B) const;

  /// Given two memory accesses in potentially different blocks,
  /// determine whether MemoryAccess \p A dominates MemoryAccess \p B.
  bool dominates(const MemoryAccess *A, const MemoryAccess *B) const;

  /// Given a MemoryAccess and a Use, determine whether MemoryAccess \p A
  /// dominates Use \p B.
  bool dominates(const MemoryAccess *A, const Use &B) const;

  enum class VerificationLevel { Fast, Full };
  /// Verify that MemorySSA is self consistent (IE definitions dominate
  /// all uses, uses appear in the right places).  This is used by unit tests.
  void verifyMemorySSA(VerificationLevel = VerificationLevel::Fast) const;

  /// Used in various insertion functions to specify whether we are talking
  /// about the beginning or end of a block.
  enum InsertionPlace { Beginning, End, BeforeTerminator };

  /// By default, uses are *not* optimized during MemorySSA construction.
  /// Calling this method will attempt to optimize all MemoryUses, if this has
  /// not happened yet for this MemorySSA instance. This should be done if you
  /// plan to query the clobbering access for most uses, or if you walk the
  /// def-use chain of uses.
  void ensureOptimizedUses();

  AliasAnalysis &getAA() { return *AA; }

protected:
  // Used by Memory SSA dumpers and wrapper pass
  friend class MemorySSAUpdater;

  template <typename IterT>
  void verifyOrderingDominationAndDefUses(
      IterT Blocks, VerificationLevel = VerificationLevel::Fast) const;
  template <typename IterT> void verifyDominationNumbers(IterT Blocks) const;
  template <typename IterT> void verifyPrevDefInPhis(IterT Blocks) const;

  // This is used by the use optimizer and updater.
  AccessList *getWritableBlockAccesses(const BasicBlock *BB) const {
    auto It = PerBlockAccesses.find(BB);
    return It == PerBlockAccesses.end() ? nullptr : It->second.get();
  }

  // This is used by the use optimizer and updater.
  DefsList *getWritableBlockDefs(const BasicBlock *BB) const {
    auto It = PerBlockDefs.find(BB);
    return It == PerBlockDefs.end() ? nullptr : It->second.get();
  }

  // These is used by the updater to perform various internal MemorySSA
  // machinsations.  They do not always leave the IR in a correct state, and
  // relies on the updater to fixup what it breaks, so it is not public.

  void moveTo(MemoryUseOrDef *What, BasicBlock *BB, AccessList::iterator Where);
  void moveTo(MemoryAccess *What, BasicBlock *BB, InsertionPlace Point);

  // Rename the dominator tree branch rooted at BB.
  void renamePass(BasicBlock *BB, MemoryAccess *IncomingVal,
                  SmallPtrSetImpl<BasicBlock *> &Visited) {
    renamePass(DT->getNode(BB), IncomingVal, Visited, true, true);
  }

  void removeFromLookups(MemoryAccess *);
  void removeFromLists(MemoryAccess *, bool ShouldDelete = true);
  void insertIntoListsForBlock(MemoryAccess *, const BasicBlock *,
                               InsertionPlace);
  void insertIntoListsBefore(MemoryAccess *, const BasicBlock *,
                             AccessList::iterator);
  MemoryUseOrDef *createDefinedAccess(Instruction *, MemoryAccess *,
                                      const MemoryUseOrDef *Template = nullptr,
                                      bool CreationMustSucceed = true);

private:
  class ClobberWalkerBase;
  class CachingWalker;
  class SkipSelfWalker;
  class OptimizeUses;

  CachingWalker *getWalkerImpl();
  template <typename IterT>
  void buildMemorySSA(BatchAAResults &BAA, IterT Blocks);

  void prepareForMoveTo(MemoryAccess *, BasicBlock *);
  void verifyUseInDefs(MemoryAccess *, MemoryAccess *) const;

  using AccessMap = DenseMap<const BasicBlock *, std::unique_ptr<AccessList>>;
  using DefsMap = DenseMap<const BasicBlock *, std::unique_ptr<DefsList>>;

  void markUnreachableAsLiveOnEntry(BasicBlock *BB);
  MemoryPhi *createMemoryPhi(BasicBlock *BB);
  template <typename AliasAnalysisType>
  MemoryUseOrDef *createNewAccess(Instruction *, AliasAnalysisType *,
                                  const MemoryUseOrDef *Template = nullptr);
  void placePHINodes(const SmallPtrSetImpl<BasicBlock *> &);
  MemoryAccess *renameBlock(BasicBlock *, MemoryAccess *, bool);
  void renameSuccessorPhis(BasicBlock *, MemoryAccess *, bool);
  void renamePass(DomTreeNode *, MemoryAccess *IncomingVal,
                  SmallPtrSetImpl<BasicBlock *> &Visited,
                  bool SkipVisited = false, bool RenameAllUses = false);
  AccessList *getOrCreateAccessList(const BasicBlock *);
  DefsList *getOrCreateDefsList(const BasicBlock *);
  void renumberBlock(const BasicBlock *) const;
  AliasAnalysis *AA = nullptr;
  DominatorTree *DT;
  Function *F = nullptr;
  Loop *L = nullptr;

  // Memory SSA mappings
  DenseMap<const Value *, MemoryAccess *> ValueToMemoryAccess;

  // These two mappings contain the main block to access/def mappings for
  // MemorySSA. The list contained in PerBlockAccesses really owns all the
  // MemoryAccesses.
  // Both maps maintain the invariant that if a block is found in them, the
  // corresponding list is not empty, and if a block is not found in them, the
  // corresponding list is empty.
  AccessMap PerBlockAccesses;
  DefsMap PerBlockDefs;
  std::unique_ptr<MemoryAccess, ValueDeleter> LiveOnEntryDef;

  // Domination mappings
  // Note that the numbering is local to a block, even though the map is
  // global.
  mutable SmallPtrSet<const BasicBlock *, 16> BlockNumberingValid;
  mutable DenseMap<const MemoryAccess *, unsigned long> BlockNumbering;

  // Memory SSA building info
  std::unique_ptr<ClobberWalkerBase> WalkerBase;
  std::unique_ptr<CachingWalker> Walker;
  std::unique_ptr<SkipSelfWalker> SkipWalker;
  unsigned NextID = 0;
  bool IsOptimized = false;
};

/// Enables verification of MemorySSA.
///
/// The checks which this flag enables is exensive and disabled by default
/// unless `EXPENSIVE_CHECKS` is defined.  The flag `-verify-memoryssa` can be
/// used to selectively enable the verification without re-compilation.
extern bool VerifyMemorySSA;

// Internal MemorySSA utils, for use by MemorySSA classes and walkers
class MemorySSAUtil {
protected:
  friend class GVNHoist;
  friend class MemorySSAWalker;

  // This function should not be used by new passes.
  static bool defClobbersUseOrDef(MemoryDef *MD, const MemoryUseOrDef *MU,
                                  AliasAnalysis &AA);
};

/// An analysis that produces \c MemorySSA for a function.
///
class MemorySSAAnalysis : public AnalysisInfoMixin<MemorySSAAnalysis> {
  friend AnalysisInfoMixin<MemorySSAAnalysis>;

  static AnalysisKey Key;

public:
  // Wrap MemorySSA result to ensure address stability of internal MemorySSA
  // pointers after construction.  Use a wrapper class instead of plain
  // unique_ptr<MemorySSA> to avoid build breakage on MSVC.
  struct Result {
    Result(std::unique_ptr<MemorySSA> &&MSSA) : MSSA(std::move(MSSA)) {}

    MemorySSA &getMSSA() { return *MSSA; }

    std::unique_ptr<MemorySSA> MSSA;

    bool invalidate(Function &F, const PreservedAnalyses &PA,
                    FunctionAnalysisManager::Invalidator &Inv);
  };

  Result run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for \c MemorySSA.
class MemorySSAPrinterPass : public PassInfoMixin<MemorySSAPrinterPass> {
  raw_ostream &OS;
  bool EnsureOptimizedUses;

public:
  explicit MemorySSAPrinterPass(raw_ostream &OS, bool EnsureOptimizedUses)
      : OS(OS), EnsureOptimizedUses(EnsureOptimizedUses) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

/// Printer pass for \c MemorySSA via the walker.
class MemorySSAWalkerPrinterPass
    : public PassInfoMixin<MemorySSAWalkerPrinterPass> {
  raw_ostream &OS;

public:
  explicit MemorySSAWalkerPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

/// Verifier pass for \c MemorySSA.
struct MemorySSAVerifierPass : PassInfoMixin<MemorySSAVerifierPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

/// Legacy analysis pass which computes \c MemorySSA.
class MemorySSAWrapperPass : public FunctionPass {
public:
  MemorySSAWrapperPass();

  static char ID;

  bool runOnFunction(Function &) override;
  void releaseMemory() override;
  MemorySSA &getMSSA() { return *MSSA; }
  const MemorySSA &getMSSA() const { return *MSSA; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  void verifyAnalysis() const override;
  void print(raw_ostream &OS, const Module *M = nullptr) const override;

private:
  std::unique_ptr<MemorySSA> MSSA;
};

/// This is the generic walker interface for walkers of MemorySSA.
/// Walkers are used to be able to further disambiguate the def-use chains
/// MemorySSA gives you, or otherwise produce better info than MemorySSA gives
/// you.
/// In particular, while the def-use chains provide basic information, and are
/// guaranteed to give, for example, the nearest may-aliasing MemoryDef for a
/// MemoryUse as AliasAnalysis considers it, a user mant want better or other
/// information. In particular, they may want to use SCEV info to further
/// disambiguate memory accesses, or they may want the nearest dominating
/// may-aliasing MemoryDef for a call or a store. This API enables a
/// standardized interface to getting and using that info.
class MemorySSAWalker {
public:
  MemorySSAWalker(MemorySSA *);
  virtual ~MemorySSAWalker() = default;

  using MemoryAccessSet = SmallVector<MemoryAccess *, 8>;

  /// Given a memory Mod/Ref/ModRef'ing instruction, calling this
  /// will give you the nearest dominating MemoryAccess that Mod's the location
  /// the instruction accesses (by skipping any def which AA can prove does not
  /// alias the location(s) accessed by the instruction given).
  ///
  /// Note that this will return a single access, and it must dominate the
  /// Instruction, so if an operand of a MemoryPhi node Mod's the instruction,
  /// this will return the MemoryPhi, not the operand. This means that
  /// given:
  /// if (a) {
  ///   1 = MemoryDef(liveOnEntry)
  ///   store %a
  /// } else {
  ///   2 = MemoryDef(liveOnEntry)
  ///   store %b
  /// }
  /// 3 = MemoryPhi(2, 1)
  /// MemoryUse(3)
  /// load %a
  ///
  /// calling this API on load(%a) will return the MemoryPhi, not the MemoryDef
  /// in the if (a) branch.
  MemoryAccess *getClobberingMemoryAccess(const Instruction *I,
                                          BatchAAResults &AA) {
    MemoryAccess *MA = MSSA->getMemoryAccess(I);
    assert(MA && "Handed an instruction that MemorySSA doesn't recognize?");
    return getClobberingMemoryAccess(MA, AA);
  }

  /// Does the same thing as getClobberingMemoryAccess(const Instruction *I),
  /// but takes a MemoryAccess instead of an Instruction.
  virtual MemoryAccess *getClobberingMemoryAccess(MemoryAccess *,
                                                  BatchAAResults &AA) = 0;

  /// Given a potentially clobbering memory access and a new location,
  /// calling this will give you the nearest dominating clobbering MemoryAccess
  /// (by skipping non-aliasing def links).
  ///
  /// This version of the function is mainly used to disambiguate phi translated
  /// pointers, where the value of a pointer may have changed from the initial
  /// memory access. Note that this expects to be handed either a MemoryUse,
  /// or an already potentially clobbering access. Unlike the above API, if
  /// given a MemoryDef that clobbers the pointer as the starting access, it
  /// will return that MemoryDef, whereas the above would return the clobber
  /// starting from the use side of  the memory def.
  virtual MemoryAccess *getClobberingMemoryAccess(MemoryAccess *,
                                                  const MemoryLocation &,
                                                  BatchAAResults &AA) = 0;

  MemoryAccess *getClobberingMemoryAccess(const Instruction *I) {
    BatchAAResults BAA(MSSA->getAA());
    return getClobberingMemoryAccess(I, BAA);
  }

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA) {
    BatchAAResults BAA(MSSA->getAA());
    return getClobberingMemoryAccess(MA, BAA);
  }

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc) {
    BatchAAResults BAA(MSSA->getAA());
    return getClobberingMemoryAccess(MA, Loc, BAA);
  }

  /// Given a memory access, invalidate anything this walker knows about
  /// that access.
  /// This API is used by walkers that store information to perform basic cache
  /// invalidation.  This will be called by MemorySSA at appropriate times for
  /// the walker it uses or returns.
  virtual void invalidateInfo(MemoryAccess *) {}

protected:
  friend class MemorySSA; // For updating MSSA pointer in MemorySSA move
                          // constructor.
  MemorySSA *MSSA;
};

/// A MemorySSAWalker that does no alias queries, or anything else. It
/// simply returns the links as they were constructed by the builder.
class DoNothingMemorySSAWalker final : public MemorySSAWalker {
public:
  // Keep the overrides below from hiding the Instruction overload of
  // getClobberingMemoryAccess.
  using MemorySSAWalker::getClobberingMemoryAccess;

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *,
                                          BatchAAResults &) override;
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *,
                                          const MemoryLocation &,
                                          BatchAAResults &) override;
};

using MemoryAccessPair = std::pair<MemoryAccess *, MemoryLocation>;
using ConstMemoryAccessPair = std::pair<const MemoryAccess *, MemoryLocation>;

/// Iterator base class used to implement const and non-const iterators
/// over the defining accesses of a MemoryAccess.
template <class T>
class memoryaccess_def_iterator_base
    : public iterator_facade_base<memoryaccess_def_iterator_base<T>,
                                  std::forward_iterator_tag, T, ptrdiff_t, T *,
                                  T *> {
  using BaseT = typename memoryaccess_def_iterator_base::iterator_facade_base;

public:
  memoryaccess_def_iterator_base(T *Start) : Access(Start) {}
  memoryaccess_def_iterator_base() = default;

  bool operator==(const memoryaccess_def_iterator_base &Other) const {
    return Access == Other.Access && (!Access || ArgNo == Other.ArgNo);
  }

  // This is a bit ugly, but for MemoryPHI's, unlike PHINodes, you can't get the
  // block from the operand in constant time (In a PHINode, the uselist has
  // both, so it's just subtraction). We provide it as part of the
  // iterator to avoid callers having to linear walk to get the block.
  // If the operation becomes constant time on MemoryPHI's, this bit of
  // abstraction breaking should be removed.
  BasicBlock *getPhiArgBlock() const {
    MemoryPhi *MP = dyn_cast<MemoryPhi>(Access);
    assert(MP && "Tried to get phi arg block when not iterating over a PHI");
    return MP->getIncomingBlock(ArgNo);
  }

  typename std::iterator_traits<BaseT>::pointer operator*() const {
    assert(Access && "Tried to access past the end of our iterator");
    // Go to the first argument for phis, and the defining access for everything
    // else.
    if (const MemoryPhi *MP = dyn_cast<MemoryPhi>(Access))
      return MP->getIncomingValue(ArgNo);
    return cast<MemoryUseOrDef>(Access)->getDefiningAccess();
  }

  using BaseT::operator++;
  memoryaccess_def_iterator_base &operator++() {
    assert(Access && "Hit end of iterator");
    if (const MemoryPhi *MP = dyn_cast<MemoryPhi>(Access)) {
      if (++ArgNo >= MP->getNumIncomingValues()) {
        ArgNo = 0;
        Access = nullptr;
      }
    } else {
      Access = nullptr;
    }
    return *this;
  }

private:
  T *Access = nullptr;
  unsigned ArgNo = 0;
};

inline memoryaccess_def_iterator MemoryAccess::defs_begin() {
  return memoryaccess_def_iterator(this);
}

inline const_memoryaccess_def_iterator MemoryAccess::defs_begin() const {
  return const_memoryaccess_def_iterator(this);
}

inline memoryaccess_def_iterator MemoryAccess::defs_end() {
  return memoryaccess_def_iterator();
}

inline const_memoryaccess_def_iterator MemoryAccess::defs_end() const {
  return const_memoryaccess_def_iterator();
}

/// GraphTraits for a MemoryAccess, which walks defs in the normal case,
/// and uses in the inverse case.
template <> struct GraphTraits<MemoryAccess *> {
  using NodeRef = MemoryAccess *;
  using ChildIteratorType = memoryaccess_def_iterator;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) { return N->defs_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->defs_end(); }
};

template <> struct GraphTraits<Inverse<MemoryAccess *>> {
  using NodeRef = MemoryAccess *;
  using ChildIteratorType = MemoryAccess::iterator;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) { return N->user_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->user_end(); }
};

/// Provide an iterator that walks defs, giving both the memory access,
/// and the current pointer location, updating the pointer location as it
/// changes due to phi node translation.
///
/// This iterator, while somewhat specialized, is what most clients actually
/// want when walking upwards through MemorySSA def chains. It takes a pair of
/// <MemoryAccess,MemoryLocation>, and walks defs, properly translating the
/// memory location through phi nodes for the user.
class upward_defs_iterator
    : public iterator_facade_base<upward_defs_iterator,
                                  std::forward_iterator_tag,
                                  const MemoryAccessPair> {
  using BaseT = upward_defs_iterator::iterator_facade_base;

public:
  upward_defs_iterator(const MemoryAccessPair &Info, DominatorTree *DT)
      : DefIterator(Info.first), Location(Info.second),
        OriginalAccess(Info.first), DT(DT) {
    CurrentPair.first = nullptr;

    WalkingPhi = Info.first && isa<MemoryPhi>(Info.first);
    fillInCurrentPair();
  }

  upward_defs_iterator() { CurrentPair.first = nullptr; }

  bool operator==(const upward_defs_iterator &Other) const {
    return DefIterator == Other.DefIterator;
  }

  typename std::iterator_traits<BaseT>::reference operator*() const {
    assert(DefIterator != OriginalAccess->defs_end() &&
           "Tried to access past the end of our iterator");
    return CurrentPair;
  }

  using BaseT::operator++;
  upward_defs_iterator &operator++() {
    assert(DefIterator != OriginalAccess->defs_end() &&
           "Tried to access past the end of the iterator");
    ++DefIterator;
    if (DefIterator != OriginalAccess->defs_end())
      fillInCurrentPair();
    return *this;
  }

  BasicBlock *getPhiArgBlock() const { return DefIterator.getPhiArgBlock(); }

private:
  /// Returns true if \p Ptr is guaranteed to be loop invariant for any possible
  /// loop. In particular, this guarantees that it only references a single
  /// MemoryLocation during execution of the containing function.
  bool IsGuaranteedLoopInvariant(const Value *Ptr) const;

  void fillInCurrentPair() {
    CurrentPair.first = *DefIterator;
    CurrentPair.second = Location;
    if (WalkingPhi && Location.Ptr) {
      PHITransAddr Translator(
          const_cast<Value *>(Location.Ptr),
          OriginalAccess->getBlock()->getDataLayout(), nullptr);

      if (Value *Addr =
              Translator.translateValue(OriginalAccess->getBlock(),
                                        DefIterator.getPhiArgBlock(), DT, true))
        if (Addr != CurrentPair.second.Ptr)
          CurrentPair.second = CurrentPair.second.getWithNewPtr(Addr);

      // Mark size as unknown, if the location is not guaranteed to be
      // loop-invariant for any possible loop in the function. Setting the size
      // to unknown guarantees that any memory accesses that access locations
      // after the pointer are considered as clobbers, which is important to
      // catch loop carried dependences.
      if (!IsGuaranteedLoopInvariant(CurrentPair.second.Ptr))
        CurrentPair.second = CurrentPair.second.getWithNewSize(
            LocationSize::beforeOrAfterPointer());
    }
  }

  MemoryAccessPair CurrentPair;
  memoryaccess_def_iterator DefIterator;
  MemoryLocation Location;
  MemoryAccess *OriginalAccess = nullptr;
  DominatorTree *DT = nullptr;
  bool WalkingPhi = false;
};

inline upward_defs_iterator
upward_defs_begin(const MemoryAccessPair &Pair, DominatorTree &DT) {
  return upward_defs_iterator(Pair, &DT);
}

inline upward_defs_iterator upward_defs_end() { return upward_defs_iterator(); }

inline iterator_range<upward_defs_iterator>
upward_defs(const MemoryAccessPair &Pair, DominatorTree &DT) {
  return make_range(upward_defs_begin(Pair, DT), upward_defs_end());
}

/// Walks the defining accesses of MemoryDefs. Stops after we hit something that
/// has no defining use (e.g. a MemoryPhi or liveOnEntry). Note that, when
/// comparing against a null def_chain_iterator, this will compare equal only
/// after walking said Phi/liveOnEntry.
///
/// The UseOptimizedChain flag specifies whether to walk the clobbering
/// access chain, or all the accesses.
///
/// Normally, MemoryDef are all just def/use linked together, so a def_chain on
/// a MemoryDef will walk all MemoryDefs above it in the program until it hits
/// a phi node.  The optimized chain walks the clobbering access of a store.
/// So if you are just trying to find, given a store, what the next
/// thing that would clobber the same memory is, you want the optimized chain.
template <class T, bool UseOptimizedChain = false>
struct def_chain_iterator
    : public iterator_facade_base<def_chain_iterator<T, UseOptimizedChain>,
                                  std::forward_iterator_tag, MemoryAccess *> {
  def_chain_iterator() : MA(nullptr) {}
  def_chain_iterator(T MA) : MA(MA) {}

  T operator*() const { return MA; }

  def_chain_iterator &operator++() {
    // N.B. liveOnEntry has a null defining access.
    if (auto *MUD = dyn_cast<MemoryUseOrDef>(MA)) {
      if (UseOptimizedChain && MUD->isOptimized())
        MA = MUD->getOptimized();
      else
        MA = MUD->getDefiningAccess();
    } else {
      MA = nullptr;
    }

    return *this;
  }

  bool operator==(const def_chain_iterator &O) const { return MA == O.MA; }

private:
  T MA;
};

template <class T>
inline iterator_range<def_chain_iterator<T>>
def_chain(T MA, MemoryAccess *UpTo = nullptr) {
#ifdef EXPENSIVE_CHECKS
  assert((!UpTo || find(def_chain(MA), UpTo) != def_chain_iterator<T>()) &&
         "UpTo isn't in the def chain!");
#endif
  return make_range(def_chain_iterator<T>(MA), def_chain_iterator<T>(UpTo));
}

template <class T>
inline iterator_range<def_chain_iterator<T, true>> optimized_def_chain(T MA) {
  return make_range(def_chain_iterator<T, true>(MA),
                    def_chain_iterator<T, true>(nullptr));
}

} // end namespace llvm

#endif // LLVM_ANALYSIS_MEMORYSSA_H
