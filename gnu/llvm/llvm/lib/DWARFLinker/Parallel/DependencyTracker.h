//===- "DependencyTracker.h" ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_DWARFLINKER_PARALLEL_DEPENDENCYTRACKER_H
#define LLVM_LIB_DWARFLINKER_PARALLEL_DEPENDENCYTRACKER_H

#include "DWARFLinkerCompileUnit.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
class DWARFDebugInfoEntry;
class DWARFDie;

namespace dwarf_linker {
namespace parallel {

/// This class discovers DIEs dependencies: marks "live" DIEs, marks DIE
/// locations (whether DIE should be cloned as regular DIE or it should be put
/// into the artificial type unit).
class DependencyTracker {
public:
  DependencyTracker(CompileUnit &CU) : CU(CU) {}

  /// Recursively walk the \p DIE tree and look for DIEs to keep. Store that
  /// information in \p CU's DIEInfo.
  ///
  /// This function is the entry point of the DIE selection algorithm. It is
  /// expected to walk the DIE tree and(through the mediation of
  /// Context.File.Addresses) ask for relocation adjustment value on each
  /// DIE that might be a 'root DIE'(f.e. subprograms, variables).
  ///
  /// Returns true if all dependencies are correctly discovered. Inter-CU
  /// dependencies cannot be discovered if referenced CU is not analyzed yet.
  /// If that is the case this method returns false.
  bool resolveDependenciesAndMarkLiveness(
      bool InterCUProcessingStarted,
      std::atomic<bool> &HasNewInterconnectedCUs);

  /// Check if dependencies have incompatible placement.
  /// If that is the case modify placement to be compatible.
  /// \returns true if any placement was updated, otherwise returns false.
  /// This method should be called as a followup processing after
  /// resolveDependenciesAndMarkLiveness().
  bool updateDependenciesCompleteness();

  /// Recursively walk the \p DIE tree and check "keepness" and "placement"
  /// information. It is an error if parent node does not have "keep" flag,
  /// while child has one. It is an error if parent node has "TypeTable"
  /// placement while child has "PlainDwarf" placement. This function dump error
  /// at stderr in that case.
  void verifyKeepChain();

protected:
  enum class LiveRootWorklistActionTy : uint8_t {
    /// Mark current item as live entry.
    MarkSingleLiveEntry = 0,

    /// Mark current item as type entry.
    MarkSingleTypeEntry,

    /// Mark current item and all its children as live entry.
    MarkLiveEntryRec,

    /// Mark current item and all its children as type entry.
    MarkTypeEntryRec,

    /// Mark all children of current item as live entry.
    MarkLiveChildrenRec,

    /// Mark all children of current item as type entry.
    MarkTypeChildrenRec,
  };

  /// \returns true if the specified action is for the "PlainDwarf".
  bool isLiveAction(LiveRootWorklistActionTy Action) {
    switch (Action) {
    default:
      return false;

    case LiveRootWorklistActionTy::MarkSingleLiveEntry:
    case LiveRootWorklistActionTy::MarkLiveEntryRec:
    case LiveRootWorklistActionTy::MarkLiveChildrenRec:
      return true;
    }
  }

  /// \returns true if the specified action is for the "TypeTable".
  bool isTypeAction(LiveRootWorklistActionTy Action) {
    switch (Action) {
    default:
      return false;

    case LiveRootWorklistActionTy::MarkSingleTypeEntry:
    case LiveRootWorklistActionTy::MarkTypeEntryRec:
    case LiveRootWorklistActionTy::MarkTypeChildrenRec:
      return true;
    }
  }

  /// \returns true if the specified action affects only Root entry
  /// itself and does not affect it`s children.
  bool isSingleAction(LiveRootWorklistActionTy Action) {
    switch (Action) {
    default:
      return false;

    case LiveRootWorklistActionTy::MarkSingleLiveEntry:
    case LiveRootWorklistActionTy::MarkSingleTypeEntry:
      return true;
    }
  }

  /// \returns true if the specified action affects only Root entry
  /// itself and does not affect it`s children.
  bool isChildrenAction(LiveRootWorklistActionTy Action) {
    switch (Action) {
    default:
      return false;

    case LiveRootWorklistActionTy::MarkLiveChildrenRec:
    case LiveRootWorklistActionTy::MarkTypeChildrenRec:
      return true;
    }
  }

  /// Class keeping live worklist item data.
  class LiveRootWorklistItemTy {
  public:
    LiveRootWorklistItemTy() = default;
    LiveRootWorklistItemTy(const LiveRootWorklistItemTy &) = default;
    LiveRootWorklistItemTy(LiveRootWorklistActionTy Action,
                           UnitEntryPairTy RootEntry) {
      RootCU.setInt(Action);
      RootCU.setPointer(RootEntry.CU);

      RootDieEntry = RootEntry.DieEntry;
    }
    LiveRootWorklistItemTy(LiveRootWorklistActionTy Action,
                           UnitEntryPairTy RootEntry,
                           UnitEntryPairTy ReferencedBy) {
      RootCU.setPointer(RootEntry.CU);
      RootCU.setInt(Action);
      RootDieEntry = RootEntry.DieEntry;

      ReferencedByCU = ReferencedBy.CU;
      ReferencedByDieEntry = ReferencedBy.DieEntry;
    }

    UnitEntryPairTy getRootEntry() const {
      return UnitEntryPairTy{RootCU.getPointer(), RootDieEntry};
    }

    CompileUnit::DieOutputPlacement getPlacement() const {
      return static_cast<CompileUnit::DieOutputPlacement>(RootCU.getInt());
    }

    bool hasReferencedByOtherEntry() const { return ReferencedByCU != nullptr; }

    UnitEntryPairTy getReferencedByEntry() const {
      assert(ReferencedByCU);
      assert(ReferencedByDieEntry);
      return UnitEntryPairTy{ReferencedByCU, ReferencedByDieEntry};
    }

    LiveRootWorklistActionTy getAction() const {
      return static_cast<LiveRootWorklistActionTy>(RootCU.getInt());
    }

  protected:
    /// Root entry.
    /// ASSUMPTION: 3 bits are used to store LiveRootWorklistActionTy value.
    /// Thus LiveRootWorklistActionTy should have no more eight elements.

    /// Pointer traits for CompileUnit.
    struct CompileUnitPointerTraits {
      static inline void *getAsVoidPointer(CompileUnit *P) { return P; }
      static inline CompileUnit *getFromVoidPointer(void *P) {
        return (CompileUnit *)P;
      }
      static constexpr int NumLowBitsAvailable = 3;
      static_assert(
          alignof(CompileUnit) >= (1 << NumLowBitsAvailable),
          "CompileUnit insufficiently aligned to have enough low bits.");
    };

    PointerIntPair<CompileUnit *, 3, LiveRootWorklistActionTy,
                   CompileUnitPointerTraits>
        RootCU;
    const DWARFDebugInfoEntry *RootDieEntry = nullptr;

    /// Another root entry which references this RootDieEntry.
    /// ReferencedByDieEntry is kept to update placement.
    /// if RootDieEntry has placement incompatible with placement
    /// of ReferencedByDieEntry then it should be updated.
    CompileUnit *ReferencedByCU = nullptr;
    const DWARFDebugInfoEntry *ReferencedByDieEntry = nullptr;
  };

  using RootEntriesListTy = SmallVector<LiveRootWorklistItemTy>;

  /// This function navigates DIEs tree starting from specified \p Entry.
  /// It puts found 'root DIE' into the worklist. The \p CollectLiveEntries
  /// instructs to collect either live roots(like subprograms having live
  /// DW_AT_low_pc) or otherwise roots which is not live(they need to be
  /// collected if they are imported f.e. by DW_TAG_imported_module).
  void collectRootsToKeep(const UnitEntryPairTy &Entry,
                          std::optional<UnitEntryPairTy> ReferencedBy,
                          bool IsLiveParent);

  /// Returns true if specified variable references live code section.
  static bool isLiveVariableEntry(const UnitEntryPairTy &Entry,
                                  bool IsLiveParent);

  /// Returns true if specified subprogram references live code section.
  static bool isLiveSubprogramEntry(const UnitEntryPairTy &Entry);

  /// Examine worklist and mark all 'root DIE's as kept and set "Placement"
  /// property.
  bool markCollectedLiveRootsAsKept(bool InterCUProcessingStarted,
                                    std::atomic<bool> &HasNewInterconnectedCUs);

  /// Mark whole DIE tree as kept recursively.
  bool markDIEEntryAsKeptRec(LiveRootWorklistActionTy Action,
                             const UnitEntryPairTy &RootEntry,
                             const UnitEntryPairTy &Entry,
                             bool InterCUProcessingStarted,
                             std::atomic<bool> &HasNewInterconnectedCUs);

  /// Mark parents as keeping children.
  void markParentsAsKeepingChildren(const UnitEntryPairTy &Entry);

  /// Mark whole DIE tree as placed in "PlainDwarf".
  void setPlainDwarfPlacementRec(const UnitEntryPairTy &Entry);

  /// Check referenced DIEs and add them into the worklist.
  bool maybeAddReferencedRoots(LiveRootWorklistActionTy Action,
                               const UnitEntryPairTy &RootEntry,
                               const UnitEntryPairTy &Entry,
                               bool InterCUProcessingStarted,
                               std::atomic<bool> &HasNewInterconnectedCUs);

  /// \returns true if \p DIEEntry can possibly be put into the artificial type
  /// unit.
  bool isTypeTableCandidate(const DWARFDebugInfoEntry *DIEEntry);

  /// \returns root for the specified \p Entry.
  UnitEntryPairTy getRootForSpecifiedEntry(UnitEntryPairTy Entry);

  /// Add action item to the work list.
  void
  addActionToRootEntriesWorkList(LiveRootWorklistActionTy Action,
                                 const UnitEntryPairTy &Entry,
                                 std::optional<UnitEntryPairTy> ReferencedBy);

  CompileUnit &CU;

  /// List of entries which are 'root DIE's.
  RootEntriesListTy RootEntriesWorkList;

  /// List of entries dependencies.
  RootEntriesListTy Dependencies;
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_LIB_DWARFLINKER_PARALLEL_DEPENDENCYTRACKER_H
