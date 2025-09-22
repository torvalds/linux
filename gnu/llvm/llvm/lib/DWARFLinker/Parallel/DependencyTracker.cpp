//=== DependencyTracker.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DependencyTracker.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace dwarf_linker;
using namespace dwarf_linker::parallel;

/// A broken link in the keep chain. By recording both the parent and the child
/// we can show only broken links for DIEs with multiple children.
struct BrokenLink {
  BrokenLink(DWARFDie Parent, DWARFDie Child, const char *Message)
      : Parent(Parent), Child(Child), Message(Message) {}
  DWARFDie Parent;
  DWARFDie Child;
  std::string Message;
};

/// Verify the keep chain by looking for DIEs that are kept but who's parent
/// isn't.
void DependencyTracker::verifyKeepChain() {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  SmallVector<DWARFDie> Worklist;
  Worklist.push_back(CU.getOrigUnit().getUnitDIE());

  // List of broken links.
  SmallVector<BrokenLink> BrokenLinks;

  while (!Worklist.empty()) {
    const DWARFDie Current = Worklist.back();
    Worklist.pop_back();

    if (!Current.isValid())
      continue;

    CompileUnit::DIEInfo &CurrentInfo =
        CU.getDIEInfo(Current.getDebugInfoEntry());
    const bool ParentPlainDieIsKept = CurrentInfo.needToKeepInPlainDwarf();
    const bool ParentTypeDieIsKept = CurrentInfo.needToPlaceInTypeTable();

    for (DWARFDie Child : reverse(Current.children())) {
      Worklist.push_back(Child);

      CompileUnit::DIEInfo &ChildInfo =
          CU.getDIEInfo(Child.getDebugInfoEntry());
      const bool ChildPlainDieIsKept = ChildInfo.needToKeepInPlainDwarf();
      const bool ChildTypeDieIsKept = ChildInfo.needToPlaceInTypeTable();

      if (!ParentPlainDieIsKept && ChildPlainDieIsKept)
        BrokenLinks.emplace_back(Current, Child,
                                 "Found invalid link in keep chain");

      if (Child.getTag() == dwarf::DW_TAG_subprogram) {
        if (!ChildInfo.getKeep() && isLiveSubprogramEntry(UnitEntryPairTy(
                                        &CU, Child.getDebugInfoEntry()))) {
          BrokenLinks.emplace_back(Current, Child,
                                   "Live subprogram is not marked as kept");
        }
      }

      if (!ChildInfo.getODRAvailable()) {
        assert(!ChildTypeDieIsKept);
        continue;
      }

      if (!ParentTypeDieIsKept && ChildTypeDieIsKept)
        BrokenLinks.emplace_back(Current, Child,
                                 "Found invalid link in keep chain");

      if (CurrentInfo.getIsInAnonNamespaceScope() &&
          ChildInfo.needToPlaceInTypeTable()) {
        BrokenLinks.emplace_back(Current, Child,
                                 "Found invalid placement marking for member "
                                 "of anonymous namespace");
      }
    }
  }

  if (!BrokenLinks.empty()) {
    for (BrokenLink Link : BrokenLinks) {
      errs() << "\n=================================\n";
      WithColor::error() << formatv("{0} between {1:x} and {2:x}", Link.Message,
                                    Link.Parent.getOffset(),
                                    Link.Child.getOffset());

      errs() << "\nParent:";
      Link.Parent.dump(errs(), 0, {});
      errs() << "\n";
      CU.getDIEInfo(Link.Parent).dump();

      errs() << "\nChild:";
      Link.Child.dump(errs(), 2, {});
      errs() << "\n";
      CU.getDIEInfo(Link.Child).dump();
    }
    report_fatal_error("invalid keep chain");
  }
#endif
}

bool DependencyTracker::resolveDependenciesAndMarkLiveness(
    bool InterCUProcessingStarted, std::atomic<bool> &HasNewInterconnectedCUs) {
  RootEntriesWorkList.clear();

  // Search for live root DIEs.
  CompileUnit::DIEInfo &CUInfo = CU.getDIEInfo(CU.getDebugInfoEntry(0));
  CUInfo.setPlacement(CompileUnit::PlainDwarf);
  collectRootsToKeep(UnitEntryPairTy{&CU, CU.getDebugInfoEntry(0)},
                     std::nullopt, false);

  // Mark live DIEs as kept.
  return markCollectedLiveRootsAsKept(InterCUProcessingStarted,
                                      HasNewInterconnectedCUs);
}

void DependencyTracker::addActionToRootEntriesWorkList(
    LiveRootWorklistActionTy Action, const UnitEntryPairTy &Entry,
    std::optional<UnitEntryPairTy> ReferencedBy) {
  if (ReferencedBy) {
    RootEntriesWorkList.emplace_back(Action, Entry, *ReferencedBy);
    return;
  }

  RootEntriesWorkList.emplace_back(Action, Entry);
}

void DependencyTracker::collectRootsToKeep(
    const UnitEntryPairTy &Entry, std::optional<UnitEntryPairTy> ReferencedBy,
    bool IsLiveParent) {
  for (const DWARFDebugInfoEntry *CurChild =
           Entry.CU->getFirstChildEntry(Entry.DieEntry);
       CurChild && CurChild->getAbbreviationDeclarationPtr();
       CurChild = Entry.CU->getSiblingEntry(CurChild)) {
    UnitEntryPairTy ChildEntry(Entry.CU, CurChild);
    CompileUnit::DIEInfo &ChildInfo = Entry.CU->getDIEInfo(CurChild);

    bool IsLiveChild = false;

    switch (CurChild->getTag()) {
    case dwarf::DW_TAG_label: {
      IsLiveChild = isLiveSubprogramEntry(ChildEntry);

      // Keep label referencing live address.
      // Keep label which is child of live parent entry.
      if (IsLiveChild || (IsLiveParent && ChildInfo.getHasAnAddress())) {
        addActionToRootEntriesWorkList(
            LiveRootWorklistActionTy::MarkLiveEntryRec, ChildEntry,
            ReferencedBy);
      }
    } break;
    case dwarf::DW_TAG_subprogram: {
      IsLiveChild = isLiveSubprogramEntry(ChildEntry);

      // Keep subprogram referencing live address.
      if (IsLiveChild) {
        // If subprogram is in module scope and this module allows ODR
        // deduplication set "TypeTable" placement, otherwise set "" placement
        LiveRootWorklistActionTy Action =
            (ChildInfo.getIsInMouduleScope() && ChildInfo.getODRAvailable())
                ? LiveRootWorklistActionTy::MarkTypeEntryRec
                : LiveRootWorklistActionTy::MarkLiveEntryRec;

        addActionToRootEntriesWorkList(Action, ChildEntry, ReferencedBy);
      }
    } break;
    case dwarf::DW_TAG_constant:
    case dwarf::DW_TAG_variable: {
      IsLiveChild = isLiveVariableEntry(ChildEntry, IsLiveParent);

      // Keep variable referencing live address.
      if (IsLiveChild) {
        // If variable is in module scope and this module allows ODR
        // deduplication set "TypeTable" placement, otherwise set "" placement

        LiveRootWorklistActionTy Action =
            (ChildInfo.getIsInMouduleScope() && ChildInfo.getODRAvailable())
                ? LiveRootWorklistActionTy::MarkTypeEntryRec
                : LiveRootWorklistActionTy::MarkLiveEntryRec;

        addActionToRootEntriesWorkList(Action, ChildEntry, ReferencedBy);
      }
    } break;
    case dwarf::DW_TAG_base_type: {
      // Always keep base types.
      addActionToRootEntriesWorkList(
          LiveRootWorklistActionTy::MarkSingleLiveEntry, ChildEntry,
          ReferencedBy);
    } break;
    case dwarf::DW_TAG_imported_module:
    case dwarf::DW_TAG_imported_declaration:
    case dwarf::DW_TAG_imported_unit: {
      // Always keep DIEs having DW_AT_import attribute.
      if (Entry.DieEntry->getTag() == dwarf::DW_TAG_compile_unit) {
        addActionToRootEntriesWorkList(
            LiveRootWorklistActionTy::MarkSingleLiveEntry, ChildEntry,
            ReferencedBy);
        break;
      }

      addActionToRootEntriesWorkList(
          LiveRootWorklistActionTy::MarkSingleTypeEntry, ChildEntry,
          ReferencedBy);
    } break;
    case dwarf::DW_TAG_type_unit:
    case dwarf::DW_TAG_partial_unit:
    case dwarf::DW_TAG_compile_unit: {
      llvm_unreachable("Called for incorrect DIE");
    } break;
    default:
      // Nothing to do.
      break;
    }

    collectRootsToKeep(ChildEntry, ReferencedBy, IsLiveChild || IsLiveParent);
  }
}

bool DependencyTracker::markCollectedLiveRootsAsKept(
    bool InterCUProcessingStarted, std::atomic<bool> &HasNewInterconnectedCUs) {
  bool Res = true;

  // Mark roots as kept.
  while (!RootEntriesWorkList.empty()) {
    LiveRootWorklistItemTy Root = RootEntriesWorkList.pop_back_val();

    if (markDIEEntryAsKeptRec(Root.getAction(), Root.getRootEntry(),
                              Root.getRootEntry(), InterCUProcessingStarted,
                              HasNewInterconnectedCUs)) {
      if (Root.hasReferencedByOtherEntry())
        Dependencies.push_back(Root);
    } else
      Res = false;
  }

  return Res;
}

bool DependencyTracker::updateDependenciesCompleteness() {
  bool HasNewDependency = false;
  for (LiveRootWorklistItemTy &Root : Dependencies) {
    assert(Root.hasReferencedByOtherEntry() &&
           "Root entry without dependency inside the dependencies list");

    UnitEntryPairTy RootEntry = Root.getRootEntry();
    CompileUnit::DIEInfo &RootInfo =
        RootEntry.CU->getDIEInfo(RootEntry.DieEntry);

    UnitEntryPairTy ReferencedByEntry = Root.getReferencedByEntry();
    CompileUnit::DIEInfo &ReferencedByInfo =
        ReferencedByEntry.CU->getDIEInfo(ReferencedByEntry.DieEntry);

    if (!RootInfo.needToPlaceInTypeTable() &&
        ReferencedByInfo.needToPlaceInTypeTable()) {
      HasNewDependency = true;
      setPlainDwarfPlacementRec(ReferencedByEntry);

      // FIXME: we probably need to update getKeepTypeChildren status for
      // parents of *Root.ReferencedBy.
    }
  }

  return HasNewDependency;
}

void DependencyTracker::setPlainDwarfPlacementRec(
    const UnitEntryPairTy &Entry) {
  CompileUnit::DIEInfo &Info = Entry.CU->getDIEInfo(Entry.DieEntry);
  if (Info.getPlacement() == CompileUnit::PlainDwarf &&
      !Info.getKeepTypeChildren())
    return;

  Info.setPlacement(CompileUnit::PlainDwarf);
  Info.unsetKeepTypeChildren();
  markParentsAsKeepingChildren(Entry);

  for (const DWARFDebugInfoEntry *CurChild =
           Entry.CU->getFirstChildEntry(Entry.DieEntry);
       CurChild && CurChild->getAbbreviationDeclarationPtr();
       CurChild = Entry.CU->getSiblingEntry(CurChild))
    setPlainDwarfPlacementRec(UnitEntryPairTy{Entry.CU, CurChild});
}

static bool isNamespaceLikeEntry(const DWARFDebugInfoEntry *Entry) {
  switch (Entry->getTag()) {
  case dwarf::DW_TAG_compile_unit:
  case dwarf::DW_TAG_module:
  case dwarf::DW_TAG_namespace:
    return true;

  default:
    return false;
  }
}

bool isAlreadyMarked(const CompileUnit::DIEInfo &Info,
                     CompileUnit::DieOutputPlacement NewPlacement) {
  if (!Info.getKeep())
    return false;

  switch (NewPlacement) {
  case CompileUnit::TypeTable:
    return Info.needToPlaceInTypeTable();

  case CompileUnit::PlainDwarf:
    return Info.needToKeepInPlainDwarf();

  case CompileUnit::Both:
    return Info.needToPlaceInTypeTable() && Info.needToKeepInPlainDwarf();

  case CompileUnit::NotSet:
    llvm_unreachable("Unset placement type is specified.");
  };

  llvm_unreachable("Unknown CompileUnit::DieOutputPlacement enum");
}

bool isAlreadyMarked(const UnitEntryPairTy &Entry,
                     CompileUnit::DieOutputPlacement NewPlacement) {
  return isAlreadyMarked(Entry.CU->getDIEInfo(Entry.DieEntry), NewPlacement);
}

void DependencyTracker::markParentsAsKeepingChildren(
    const UnitEntryPairTy &Entry) {
  if (Entry.DieEntry->getAbbreviationDeclarationPtr() == nullptr)
    return;

  CompileUnit::DIEInfo &Info = Entry.CU->getDIEInfo(Entry.DieEntry);
  bool NeedKeepTypeChildren = Info.needToPlaceInTypeTable();
  bool NeedKeepPlainChildren = Info.needToKeepInPlainDwarf();

  bool AreTypeParentsDone = !NeedKeepTypeChildren;
  bool ArePlainParentsDone = !NeedKeepPlainChildren;

  // Mark parents as 'Keep*Children'.
  std::optional<uint32_t> ParentIdx = Entry.DieEntry->getParentIdx();
  while (ParentIdx) {
    const DWARFDebugInfoEntry *ParentEntry =
        Entry.CU->getDebugInfoEntry(*ParentIdx);
    CompileUnit::DIEInfo &ParentInfo = Entry.CU->getDIEInfo(*ParentIdx);

    if (!AreTypeParentsDone && NeedKeepTypeChildren) {
      if (ParentInfo.getKeepTypeChildren())
        AreTypeParentsDone = true;
      else {
        bool AddToWorklist = !isAlreadyMarked(
            ParentInfo, CompileUnit::DieOutputPlacement::TypeTable);
        ParentInfo.setKeepTypeChildren();
        if (AddToWorklist && !isNamespaceLikeEntry(ParentEntry)) {
          addActionToRootEntriesWorkList(
              LiveRootWorklistActionTy::MarkTypeChildrenRec,
              UnitEntryPairTy{Entry.CU, ParentEntry}, std::nullopt);
        }
      }
    }

    if (!ArePlainParentsDone && NeedKeepPlainChildren) {
      if (ParentInfo.getKeepPlainChildren())
        ArePlainParentsDone = true;
      else {
        bool AddToWorklist = !isAlreadyMarked(
            ParentInfo, CompileUnit::DieOutputPlacement::PlainDwarf);
        ParentInfo.setKeepPlainChildren();
        if (AddToWorklist && !isNamespaceLikeEntry(ParentEntry)) {
          addActionToRootEntriesWorkList(
              LiveRootWorklistActionTy::MarkLiveChildrenRec,
              UnitEntryPairTy{Entry.CU, ParentEntry}, std::nullopt);
        }
      }
    }

    if (AreTypeParentsDone && ArePlainParentsDone)
      break;

    ParentIdx = ParentEntry->getParentIdx();
  }
}

// This function tries to set specified \p Placement for the \p Entry.
// Depending on the concrete entry, the placement could be:
//  a) changed to another.
//  b) joined with current entry placement.
//  c) set as requested.
static CompileUnit::DieOutputPlacement
getFinalPlacementForEntry(const UnitEntryPairTy &Entry,
                          CompileUnit::DieOutputPlacement Placement) {
  assert((Placement != CompileUnit::NotSet) && "Placement is not set");
  CompileUnit::DIEInfo &EntryInfo = Entry.CU->getDIEInfo(Entry.DieEntry);

  if (!EntryInfo.getODRAvailable())
    return CompileUnit::PlainDwarf;

  if (Entry.DieEntry->getTag() == dwarf::DW_TAG_variable) {
    // Do not put variable into the "TypeTable" and "PlainDwarf" at the same
    // time.
    if (EntryInfo.getPlacement() == CompileUnit::PlainDwarf ||
        EntryInfo.getPlacement() == CompileUnit::Both)
      return CompileUnit::PlainDwarf;

    if (Placement == CompileUnit::PlainDwarf || Placement == CompileUnit::Both)
      return CompileUnit::PlainDwarf;
  }

  switch (EntryInfo.getPlacement()) {
  case CompileUnit::NotSet:
    return Placement;

  case CompileUnit::TypeTable:
    return Placement == CompileUnit::PlainDwarf ? CompileUnit::Both : Placement;

  case CompileUnit::PlainDwarf:
    return Placement == CompileUnit::TypeTable ? CompileUnit::Both : Placement;

  case CompileUnit::Both:
    return CompileUnit::Both;
  };

  llvm_unreachable("Unknown placement type.");
  return Placement;
}

bool DependencyTracker::markDIEEntryAsKeptRec(
    LiveRootWorklistActionTy Action, const UnitEntryPairTy &RootEntry,
    const UnitEntryPairTy &Entry, bool InterCUProcessingStarted,
    std::atomic<bool> &HasNewInterconnectedCUs) {
  if (Entry.DieEntry->getAbbreviationDeclarationPtr() == nullptr)
    return true;

  CompileUnit::DIEInfo &Info = Entry.CU->getDIEInfo(Entry.DieEntry);

  // Calculate final placement placement.
  CompileUnit::DieOutputPlacement Placement = getFinalPlacementForEntry(
      Entry,
      isLiveAction(Action) ? CompileUnit::PlainDwarf : CompileUnit::TypeTable);
  assert((Info.getODRAvailable() || isLiveAction(Action) ||
          Placement == CompileUnit::PlainDwarf) &&
         "Wrong kind of placement for ODR unavailable entry");

  if (!isChildrenAction(Action))
    if (isAlreadyMarked(Entry, Placement))
      return true;

  // Mark current DIE as kept.
  Info.setKeep();
  Info.setPlacement(Placement);

  // Set keep children property for parents.
  markParentsAsKeepingChildren(Entry);

  UnitEntryPairTy FinalRootEntry =
      Entry.DieEntry->getTag() == dwarf::DW_TAG_subprogram ? Entry : RootEntry;

  // Analyse referenced DIEs.
  bool Res = true;
  if (!maybeAddReferencedRoots(Action, FinalRootEntry, Entry,
                               InterCUProcessingStarted,
                               HasNewInterconnectedCUs))
    Res = false;

  // Return if we do not need to process children.
  if (isSingleAction(Action))
    return Res;

  // Process children.
  // Check for subprograms special case.
  if (Entry.DieEntry->getTag() == dwarf::DW_TAG_subprogram &&
      Info.getODRAvailable()) {
    // Subprograms is a special case. As it can be root for type DIEs
    // and itself may be subject to move into the artificial type unit.
    //  a) Non removable children(like DW_TAG_formal_parameter) should always
    //     be cloned. They are placed into the "PlainDwarf" and into the
    //     "TypeTable".
    //  b) ODR deduplication candidates(type DIEs) children should not be put
    //  into the "PlainDwarf".
    //  c) Children keeping addresses and locations(like DW_TAG_call_site)
    //  should not be put into the "TypeTable".
    for (const DWARFDebugInfoEntry *CurChild =
             Entry.CU->getFirstChildEntry(Entry.DieEntry);
         CurChild && CurChild->getAbbreviationDeclarationPtr();
         CurChild = Entry.CU->getSiblingEntry(CurChild)) {
      CompileUnit::DIEInfo ChildInfo = Entry.CU->getDIEInfo(CurChild);

      switch (CurChild->getTag()) {
      case dwarf::DW_TAG_variable:
      case dwarf::DW_TAG_constant:
      case dwarf::DW_TAG_subprogram:
      case dwarf::DW_TAG_label: {
        if (ChildInfo.getHasAnAddress())
          continue;
      } break;

      // Entries having following tags could not be removed from the subprogram.
      case dwarf::DW_TAG_lexical_block:
      case dwarf::DW_TAG_friend:
      case dwarf::DW_TAG_inheritance:
      case dwarf::DW_TAG_formal_parameter:
      case dwarf::DW_TAG_unspecified_parameters:
      case dwarf::DW_TAG_template_type_parameter:
      case dwarf::DW_TAG_template_value_parameter:
      case dwarf::DW_TAG_GNU_template_parameter_pack:
      case dwarf::DW_TAG_GNU_formal_parameter_pack:
      case dwarf::DW_TAG_GNU_template_template_param:
      case dwarf::DW_TAG_thrown_type: {
        // Go to the default child handling.
      } break;

      default: {
        bool ChildIsTypeTableCandidate = isTypeTableCandidate(CurChild);

        // Skip child marked to be copied into the artificial type unit.
        if (isLiveAction(Action) && ChildIsTypeTableCandidate)
          continue;

        // Skip child marked to be copied into the plain unit.
        if (isTypeAction(Action) && !ChildIsTypeTableCandidate)
          continue;

        // Go to the default child handling.
      } break;
      }

      if (!markDIEEntryAsKeptRec(
              Action, FinalRootEntry, UnitEntryPairTy{Entry.CU, CurChild},
              InterCUProcessingStarted, HasNewInterconnectedCUs))
        Res = false;
    }

    return Res;
  }

  // Recursively process children.
  for (const DWARFDebugInfoEntry *CurChild =
           Entry.CU->getFirstChildEntry(Entry.DieEntry);
       CurChild && CurChild->getAbbreviationDeclarationPtr();
       CurChild = Entry.CU->getSiblingEntry(CurChild)) {
    CompileUnit::DIEInfo ChildInfo = Entry.CU->getDIEInfo(CurChild);
    switch (CurChild->getTag()) {
    case dwarf::DW_TAG_variable:
    case dwarf::DW_TAG_constant:
    case dwarf::DW_TAG_subprogram:
    case dwarf::DW_TAG_label: {
      if (ChildInfo.getHasAnAddress())
        continue;
    } break;
    default:
      break; // Nothing to do.
    };

    if (!markDIEEntryAsKeptRec(
            Action, FinalRootEntry, UnitEntryPairTy{Entry.CU, CurChild},
            InterCUProcessingStarted, HasNewInterconnectedCUs))
      Res = false;
  }

  return Res;
}

bool DependencyTracker::isTypeTableCandidate(
    const DWARFDebugInfoEntry *DIEEntry) {
  switch (DIEEntry->getTag()) {
  default:
    return false;

  case dwarf::DW_TAG_imported_module:
  case dwarf::DW_TAG_imported_declaration:
  case dwarf::DW_TAG_imported_unit:
  case dwarf::DW_TAG_array_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_enumeration_type:
  case dwarf::DW_TAG_pointer_type:
  case dwarf::DW_TAG_reference_type:
  case dwarf::DW_TAG_string_type:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_subroutine_type:
  case dwarf::DW_TAG_typedef:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_variant:
  case dwarf::DW_TAG_module:
  case dwarf::DW_TAG_ptr_to_member_type:
  case dwarf::DW_TAG_set_type:
  case dwarf::DW_TAG_subrange_type:
  case dwarf::DW_TAG_base_type:
  case dwarf::DW_TAG_const_type:
  case dwarf::DW_TAG_enumerator:
  case dwarf::DW_TAG_file_type:
  case dwarf::DW_TAG_packed_type:
  case dwarf::DW_TAG_thrown_type:
  case dwarf::DW_TAG_volatile_type:
  case dwarf::DW_TAG_dwarf_procedure:
  case dwarf::DW_TAG_restrict_type:
  case dwarf::DW_TAG_interface_type:
  case dwarf::DW_TAG_namespace:
  case dwarf::DW_TAG_unspecified_type:
  case dwarf::DW_TAG_shared_type:
  case dwarf::DW_TAG_rvalue_reference_type:
  case dwarf::DW_TAG_coarray_type:
  case dwarf::DW_TAG_dynamic_type:
  case dwarf::DW_TAG_atomic_type:
  case dwarf::DW_TAG_immutable_type:
  case dwarf::DW_TAG_function_template:
  case dwarf::DW_TAG_class_template:
    return true;
  }
}

bool DependencyTracker::maybeAddReferencedRoots(
    LiveRootWorklistActionTy Action, const UnitEntryPairTy &RootEntry,
    const UnitEntryPairTy &Entry, bool InterCUProcessingStarted,
    std::atomic<bool> &HasNewInterconnectedCUs) {
  const auto *Abbrev = Entry.DieEntry->getAbbreviationDeclarationPtr();
  if (Abbrev == nullptr)
    return true;

  DWARFUnit &Unit = Entry.CU->getOrigUnit();
  DWARFDataExtractor Data = Unit.getDebugInfoExtractor();
  uint64_t Offset =
      Entry.DieEntry->getOffset() + getULEB128Size(Abbrev->getCode());

  // For each DIE attribute...
  for (const auto &AttrSpec : Abbrev->attributes()) {
    DWARFFormValue Val(AttrSpec.Form);
    if (!Val.isFormClass(DWARFFormValue::FC_Reference) ||
        AttrSpec.Attr == dwarf::DW_AT_sibling) {
      DWARFFormValue::skipValue(AttrSpec.Form, Data, &Offset,
                                Unit.getFormParams());
      continue;
    }
    Val.extractValue(Data, &Offset, Unit.getFormParams(), &Unit);

    // Resolve reference.
    std::optional<UnitEntryPairTy> RefDie = Entry.CU->resolveDIEReference(
        Val, InterCUProcessingStarted
                 ? ResolveInterCUReferencesMode::Resolve
                 : ResolveInterCUReferencesMode::AvoidResolving);
    if (!RefDie) {
      Entry.CU->warn("cann't find referenced DIE", Entry.DieEntry);
      continue;
    }

    if (!RefDie->DieEntry) {
      // Delay resolving reference.
      RefDie->CU->setInterconnectedCU();
      Entry.CU->setInterconnectedCU();
      HasNewInterconnectedCUs = true;
      return false;
    }

    assert((Entry.CU->getUniqueID() == RefDie->CU->getUniqueID() ||
            InterCUProcessingStarted) &&
           "Inter-CU reference while inter-CU processing is not started");

    CompileUnit::DIEInfo &RefInfo = RefDie->CU->getDIEInfo(RefDie->DieEntry);
    if (!RefInfo.getODRAvailable())
      Action = LiveRootWorklistActionTy::MarkLiveEntryRec;
    else if (RefInfo.getODRAvailable() &&
             llvm::is_contained(getODRAttributes(), AttrSpec.Attr))
      // Note: getODRAttributes does not include DW_AT_containing_type.
      // It should be OK as we do getRootForSpecifiedEntry(). So any containing
      // type would be found as the root for the entry.
      Action = LiveRootWorklistActionTy::MarkTypeEntryRec;
    else if (isLiveAction(Action))
      Action = LiveRootWorklistActionTy::MarkLiveEntryRec;
    else
      Action = LiveRootWorklistActionTy::MarkTypeEntryRec;

    if (AttrSpec.Attr == dwarf::DW_AT_import) {
      if (isNamespaceLikeEntry(RefDie->DieEntry)) {
        addActionToRootEntriesWorkList(
            isTypeAction(Action)
                ? LiveRootWorklistActionTy::MarkSingleTypeEntry
                : LiveRootWorklistActionTy::MarkSingleLiveEntry,
            *RefDie, RootEntry);
        continue;
      }

      addActionToRootEntriesWorkList(Action, *RefDie, RootEntry);
      continue;
    }

    UnitEntryPairTy RootForReferencedDie = getRootForSpecifiedEntry(*RefDie);
    addActionToRootEntriesWorkList(Action, RootForReferencedDie, RootEntry);
  }

  return true;
}

UnitEntryPairTy
DependencyTracker::getRootForSpecifiedEntry(UnitEntryPairTy Entry) {
  UnitEntryPairTy Result = Entry;

  do {
    switch (Entry.DieEntry->getTag()) {
    case dwarf::DW_TAG_subprogram:
    case dwarf::DW_TAG_label:
    case dwarf::DW_TAG_variable:
    case dwarf::DW_TAG_constant: {
      return Result;
    } break;

    default: {
      // Nothing to do.
    }
    }

    std::optional<uint32_t> ParentIdx = Result.DieEntry->getParentIdx();
    if (!ParentIdx)
      return Result;

    const DWARFDebugInfoEntry *ParentEntry =
        Result.CU->getDebugInfoEntry(*ParentIdx);
    if (isNamespaceLikeEntry(ParentEntry))
      break;
    Result.DieEntry = ParentEntry;
  } while (true);

  return Result;
}

bool DependencyTracker::isLiveVariableEntry(const UnitEntryPairTy &Entry,
                                            bool IsLiveParent) {
  DWARFDie DIE = Entry.CU->getDIE(Entry.DieEntry);
  CompileUnit::DIEInfo &Info = Entry.CU->getDIEInfo(DIE);

  if (Info.getTrackLiveness()) {
    const auto *Abbrev = DIE.getAbbreviationDeclarationPtr();

    if (!Info.getIsInFunctionScope() &&
        Abbrev->findAttributeIndex(dwarf::DW_AT_const_value)) {
      // Global variables with constant value can always be kept.
    } else {
      // See if there is a relocation to a valid debug map entry inside this
      // variable's location. The order is important here. We want to always
      // check if the variable has a location expression address. However, we
      // don't want a static variable in a function to force us to keep the
      // enclosing function, unless requested explicitly.
      std::pair<bool, std::optional<int64_t>> LocExprAddrAndRelocAdjustment =
          Entry.CU->getContaingFile().Addresses->getVariableRelocAdjustment(
              DIE, Entry.CU->getGlobalData().getOptions().Verbose);

      if (LocExprAddrAndRelocAdjustment.first)
        Info.setHasAnAddress();

      if (!LocExprAddrAndRelocAdjustment.second)
        return false;

      if (!IsLiveParent && Info.getIsInFunctionScope() &&
          !Entry.CU->getGlobalData().getOptions().KeepFunctionForStatic)
        return false;
    }
  }
  Info.setHasAnAddress();

  if (Entry.CU->getGlobalData().getOptions().Verbose) {
    outs() << "Keeping variable DIE:";
    DIDumpOptions DumpOpts;
    DumpOpts.ChildRecurseDepth = 0;
    DumpOpts.Verbose = Entry.CU->getGlobalData().getOptions().Verbose;
    DIE.dump(outs(), 8 /* Indent */, DumpOpts);
  }

  return true;
}

bool DependencyTracker::isLiveSubprogramEntry(const UnitEntryPairTy &Entry) {
  DWARFDie DIE = Entry.CU->getDIE(Entry.DieEntry);
  CompileUnit::DIEInfo &Info = Entry.CU->getDIEInfo(Entry.DieEntry);
  std::optional<DWARFFormValue> LowPCVal = DIE.find(dwarf::DW_AT_low_pc);

  std::optional<uint64_t> LowPc;
  std::optional<uint64_t> HighPc;
  std::optional<int64_t> RelocAdjustment;
  if (Info.getTrackLiveness()) {
    LowPc = dwarf::toAddress(LowPCVal);
    if (!LowPc)
      return false;

    Info.setHasAnAddress();

    RelocAdjustment =
        Entry.CU->getContaingFile().Addresses->getSubprogramRelocAdjustment(
            DIE, Entry.CU->getGlobalData().getOptions().Verbose);
    if (!RelocAdjustment)
      return false;

    if (DIE.getTag() == dwarf::DW_TAG_subprogram) {
      // Validate subprogram address range.

      HighPc = DIE.getHighPC(*LowPc);
      if (!HighPc) {
        Entry.CU->warn("function without high_pc. Range will be discarded.",
                       &DIE);
        return false;
      }

      if (*LowPc > *HighPc) {
        Entry.CU->warn("low_pc greater than high_pc. Range will be discarded.",
                       &DIE);
        return false;
      }
    } else if (DIE.getTag() == dwarf::DW_TAG_label) {
      if (Entry.CU->hasLabelAt(*LowPc))
        return false;

      // FIXME: dsymutil-classic compat. dsymutil-classic doesn't consider
      // labels that don't fall into the CU's aranges. This is wrong IMO. Debug
      // info generation bugs aside, this is really wrong in the case of labels,
      // where a label marking the end of a function will have a PC == CU's
      // high_pc.
      if (dwarf::toAddress(Entry.CU->find(Entry.DieEntry, dwarf::DW_AT_high_pc))
              .value_or(UINT64_MAX) <= LowPc)
        return false;

      Entry.CU->addLabelLowPc(*LowPc, *RelocAdjustment);
    }
  } else
    Info.setHasAnAddress();

  if (Entry.CU->getGlobalData().getOptions().Verbose) {
    outs() << "Keeping subprogram DIE:";
    DIDumpOptions DumpOpts;
    DumpOpts.ChildRecurseDepth = 0;
    DumpOpts.Verbose = Entry.CU->getGlobalData().getOptions().Verbose;
    DIE.dump(outs(), 8 /* Indent */, DumpOpts);
  }

  if (!Info.getTrackLiveness() || DIE.getTag() == dwarf::DW_TAG_label)
    return true;

  Entry.CU->addFunctionRange(*LowPc, *HighPc, *RelocAdjustment);
  return true;
}
