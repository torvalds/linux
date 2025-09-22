//===- llvm/Analysis/AliasSetTracker.h - Build Alias Sets -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines two classes: AliasSetTracker and AliasSet. These interfaces
// are used to classify a collection of memory locations into a maximal number
// of disjoint sets. Each AliasSet object constructed by the AliasSetTracker
// object refers to memory disjoint from the other sets.
//
// An AliasSetTracker can only be used on immutable IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ALIASSETTRACKER_H
#define LLVM_ANALYSIS_ALIASSETTRACKER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include <cassert>
#include <vector>

namespace llvm {

class AliasResult;
class AliasSetTracker;
class AnyMemSetInst;
class AnyMemTransferInst;
class BasicBlock;
class BatchAAResults;
class LoadInst;
enum class ModRefInfo : uint8_t;
class raw_ostream;
class StoreInst;
class VAArgInst;
class Value;

class AliasSet : public ilist_node<AliasSet> {
  friend class AliasSetTracker;

  // Forwarding pointer.
  AliasSet *Forward = nullptr;

  /// Memory locations in this alias set.
  SmallVector<MemoryLocation, 0> MemoryLocs;

  /// All instructions without a specific address in this alias set.
  std::vector<AssertingVH<Instruction>> UnknownInsts;

  /// Number of nodes pointing to this AliasSet plus the number of AliasSets
  /// forwarding to it.
  unsigned RefCount : 27;

  // Signifies that this set should be considered to alias any pointer.
  // Use when the tracker holding this set is saturated.
  unsigned AliasAny : 1;

  /// The kinds of access this alias set models.
  ///
  /// We keep track of whether this alias set merely refers to the locations of
  /// memory (and not any particular access), whether it modifies or references
  /// the memory, or whether it does both. The lattice goes from "NoAccess" to
  /// either RefAccess or ModAccess, then to ModRefAccess as necessary.
  enum AccessLattice {
    NoAccess = 0,
    RefAccess = 1,
    ModAccess = 2,
    ModRefAccess = RefAccess | ModAccess
  };
  unsigned Access : 2;

  /// The kind of alias relationship between pointers of the set.
  ///
  /// These represent conservatively correct alias results between any members
  /// of the set. We represent these independently of the values of alias
  /// results in order to pack it into a single bit. Lattice goes from
  /// MustAlias to MayAlias.
  enum AliasLattice {
    SetMustAlias = 0, SetMayAlias = 1
  };
  unsigned Alias : 1;

  void addRef() { ++RefCount; }

  void dropRef(AliasSetTracker &AST) {
    assert(RefCount >= 1 && "Invalid reference count detected!");
    if (--RefCount == 0)
      removeFromTracker(AST);
  }

public:
  AliasSet(const AliasSet &) = delete;
  AliasSet &operator=(const AliasSet &) = delete;

  /// Accessors...
  bool isRef() const { return Access & RefAccess; }
  bool isMod() const { return Access & ModAccess; }
  bool isMustAlias() const { return Alias == SetMustAlias; }
  bool isMayAlias()  const { return Alias == SetMayAlias; }

  /// Return true if this alias set should be ignored as part of the
  /// AliasSetTracker object.
  bool isForwardingAliasSet() const { return Forward; }

  /// Merge the specified alias set into this alias set.
  void mergeSetIn(AliasSet &AS, AliasSetTracker &AST, BatchAAResults &BatchAA);

  // Alias Set iteration - Allow access to all of the memory locations which are
  // part of this alias set.
  using iterator = SmallVectorImpl<MemoryLocation>::const_iterator;
  iterator begin() const { return MemoryLocs.begin(); }
  iterator end() const { return MemoryLocs.end(); }

  unsigned size() const { return MemoryLocs.size(); }

  /// Retrieve the pointer values for the memory locations in this alias set.
  /// The order matches that of the memory locations, but duplicate pointer
  /// values are omitted.
  using PointerVector = SmallVector<const Value *, 8>;
  PointerVector getPointers() const;

  void print(raw_ostream &OS) const;
  void dump() const;

private:
  // Can only be created by AliasSetTracker.
  AliasSet()
      : RefCount(0), AliasAny(false), Access(NoAccess), Alias(SetMustAlias) {}

  void removeFromTracker(AliasSetTracker &AST);

  void addMemoryLocation(AliasSetTracker &AST, const MemoryLocation &MemLoc,
                         bool KnownMustAlias = false);
  void addUnknownInst(Instruction *I, BatchAAResults &AA);

public:
  /// If the specified memory location "may" (or must) alias one of the members
  /// in the set return the appropriate AliasResult. Otherwise return NoAlias.
  AliasResult aliasesMemoryLocation(const MemoryLocation &MemLoc,
                                    BatchAAResults &AA) const;

  ModRefInfo aliasesUnknownInst(const Instruction *Inst,
                                BatchAAResults &AA) const;
};

inline raw_ostream& operator<<(raw_ostream &OS, const AliasSet &AS) {
  AS.print(OS);
  return OS;
}

class AliasSetTracker {
  BatchAAResults &AA;
  ilist<AliasSet> AliasSets;

  using PointerMapType = DenseMap<AssertingVH<const Value>, AliasSet *>;

  // Map from pointer values to the alias set holding one or more memory
  // locations with that pointer value.
  PointerMapType PointerMap;

public:
  /// Create an empty collection of AliasSets, and use the specified alias
  /// analysis object to disambiguate load and store addresses.
  explicit AliasSetTracker(BatchAAResults &AA) : AA(AA) {}
  ~AliasSetTracker() { clear(); }

  /// These methods are used to add different types of instructions to the alias
  /// sets. Adding a new instruction can result in one of three actions
  /// happening:
  ///
  ///   1. If the instruction doesn't alias any other sets, create a new set.
  ///   2. If the instruction aliases exactly one set, add it to the set
  ///   3. If the instruction aliases multiple sets, merge the sets, and add
  ///      the instruction to the result.
  ///
  void add(const MemoryLocation &Loc);
  void add(LoadInst *LI);
  void add(StoreInst *SI);
  void add(VAArgInst *VAAI);
  void add(AnyMemSetInst *MSI);
  void add(AnyMemTransferInst *MTI);
  void add(Instruction *I);       // Dispatch to one of the other add methods...
  void add(BasicBlock &BB);       // Add all instructions in basic block
  void add(const AliasSetTracker &AST); // Add alias relations from another AST
  void addUnknown(Instruction *I);

  void clear();

  /// Return the alias sets that are active.
  const ilist<AliasSet> &getAliasSets() const { return AliasSets; }

  /// Return the alias set which contains the specified memory location.  If
  /// the memory location aliases two or more existing alias sets, will have
  /// the effect of merging those alias sets before the single resulting alias
  /// set is returned.
  AliasSet &getAliasSetFor(const MemoryLocation &MemLoc);

  /// Return the underlying alias analysis object used by this tracker.
  BatchAAResults &getAliasAnalysis() const { return AA; }

  using iterator = ilist<AliasSet>::iterator;
  using const_iterator = ilist<AliasSet>::const_iterator;

  const_iterator begin() const { return AliasSets.begin(); }
  const_iterator end()   const { return AliasSets.end(); }

  iterator begin() { return AliasSets.begin(); }
  iterator end()   { return AliasSets.end(); }

  void print(raw_ostream &OS) const;
  void dump() const;

private:
  friend class AliasSet;

  // The total number of memory locations contained in all alias sets.
  unsigned TotalAliasSetSize = 0;

  // A non-null value signifies this AST is saturated. A saturated AST lumps
  // all elements into a single "May" set.
  AliasSet *AliasAnyAS = nullptr;

  void removeAliasSet(AliasSet *AS);

  // Update an alias set field to point to its real destination. If the field is
  // pointing to a set that has been merged with another set and is forwarding,
  // the field is updated to point to the set obtained by following the
  // forwarding links. The Forward fields of intermediate alias sets are
  // collapsed as well, and alias set reference counts are updated to reflect
  // the new situation.
  void collapseForwardingIn(AliasSet *&AS) {
    if (AS->Forward) {
      collapseForwardingIn(AS->Forward);
      // Swap out AS for AS->Forward, while updating reference counts.
      AliasSet *NewAS = AS->Forward;
      NewAS->addRef();
      AS->dropRef(*this);
      AS = NewAS;
    }
  }

  AliasSet &addMemoryLocation(MemoryLocation Loc, AliasSet::AccessLattice E);
  AliasSet *mergeAliasSetsForMemoryLocation(const MemoryLocation &MemLoc,
                                            AliasSet *PtrAS,
                                            bool &MustAliasAll);

  /// Merge all alias sets into a single set that is considered to alias
  /// any memory location or instruction.
  AliasSet &mergeAllAliasSets();

  AliasSet *findAliasSetForUnknownInst(Instruction *Inst);
};

inline raw_ostream& operator<<(raw_ostream &OS, const AliasSetTracker &AST) {
  AST.print(OS);
  return OS;
}

class AliasSetsPrinterPass : public PassInfoMixin<AliasSetsPrinterPass> {
  raw_ostream &OS;

public:
  explicit AliasSetsPrinterPass(raw_ostream &OS);
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_ALIASSETTRACKER_H
