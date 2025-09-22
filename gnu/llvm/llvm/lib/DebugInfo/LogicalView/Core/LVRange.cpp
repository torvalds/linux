//===-- LVRange.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVRange class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVRange.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLocation.h"
#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Range"

void LVRange::startSearch() {
  RangesTree.clear();

  LLVM_DEBUG({ dbgs() << "\nRanges Tree entries:\n"; });

  // Traverse the ranges and store them into the interval tree.
  for (LVRangeEntry &RangeEntry : RangeEntries) {
    LLVM_DEBUG({
      LVScope *Scope = RangeEntry.scope();
      dbgs() << "Scope: " << format_decimal(Scope->getLevel(), 5) << " "
             << "Range: [" << hexValue(RangeEntry.lower()) << ":"
             << hexValue(RangeEntry.upper()) << "]\n";
    });

    RangesTree.insert(RangeEntry.lower(), RangeEntry.upper(),
                      RangeEntry.scope());
  }

  // Create the interval tree.
  RangesTree.create();

  LLVM_DEBUG({
    dbgs() << "\nRanges Tree:\n";
    RangesTree.print(dbgs());
  });
}

// Add the pair in an ascending order, with the smallest ranges at the
// start; in that way, enclosing scopes ranges are at the end of the
// list; we assume that low <= high.
void LVRange::addEntry(LVScope *Scope, LVAddress LowerAddress,
                       LVAddress UpperAddress) {
  // We assume the low <= high.
  if (LowerAddress > UpperAddress)
    std::swap(LowerAddress, UpperAddress);

  // Record the lowest and highest seen addresses.
  if (LowerAddress < Lower)
    Lower = LowerAddress;
  if (UpperAddress > Upper)
    Upper = UpperAddress;

  // Just add the scope and range pair, in no particular order.
  RangeEntries.emplace_back(LowerAddress, UpperAddress, Scope);
}

void LVRange::addEntry(LVScope *Scope) {
  assert(Scope && "Scope must not be nullptr");
  // Traverse the ranges and update the ranges set only if the ranges
  // values are not already recorded.
  if (const LVLocations *Locations = Scope->getRanges())
    for (const LVLocation *Location : *Locations) {
      LVAddress LowPC = Location->getLowerAddress();
      LVAddress HighPC = Location->getUpperAddress();
      if (!hasEntry(LowPC, HighPC))
        // Add the pair of addresses.
        addEntry(Scope, LowPC, HighPC);
    }
}

// Get the scope associated with the input address.
LVScope *LVRange::getEntry(LVAddress Address) const {
  LLVM_DEBUG({ dbgs() << format("Searching: 0x%08x\nFound: ", Address); });

  LVScope *Target = nullptr;
  LVLevel TargetLevel = 0;
  LVLevel Level = 0;
  LVScope *Scope = nullptr;
  for (LVRangesTree::find_iterator Iter = RangesTree.find(Address),
                                   End = RangesTree.find_end();
       Iter != End; ++Iter) {
    LLVM_DEBUG(
        { dbgs() << format("[0x%08x,0x%08x] ", Iter->left(), Iter->right()); });
    Scope = Iter->value();
    Level = Scope->getLevel();
    if (Level > TargetLevel) {
      TargetLevel = Level;
      Target = Scope;
    }
  }

  LLVM_DEBUG({ dbgs() << (Scope ? "\n" : "None\n"); });

  return Target;
}

// Find the associated Scope for the given ranges values.
LVScope *LVRange::getEntry(LVAddress LowerAddress,
                           LVAddress UpperAddress) const {
  for (const LVRangeEntry &RangeEntry : RangeEntries)
    if (LowerAddress >= RangeEntry.lower() && UpperAddress < RangeEntry.upper())
      return RangeEntry.scope();
  return nullptr;
}

// True if the range addresses contain the pair [LowerAddress, UpperAddress].
bool LVRange::hasEntry(LVAddress LowerAddress, LVAddress UpperAddress) const {
  for (const LVRangeEntry &RangeEntry : RangeEntries)
    if (LowerAddress == RangeEntry.lower() &&
        UpperAddress == RangeEntry.upper())
      return true;
  return false;
}

// Sort the range elements for the whole Compile Unit.
void LVRange::sort() {
  auto CompareRangeEntry = [](const LVRangeEntry &lhs,
                              const LVRangeEntry &rhs) -> bool {
    if (lhs.lower() < rhs.lower())
      return true;

    // If the lower address is the same, use the upper address value in
    // order to put first the smallest interval.
    if (lhs.lower() == rhs.lower())
      return lhs.upper() < rhs.upper();

    return false;
  };

  // Sort the ranges using low address and range size.
  std::stable_sort(RangeEntries.begin(), RangeEntries.end(), CompareRangeEntry);
}

void LVRange::print(raw_ostream &OS, bool Full) const {
  size_t Indentation = 0;
  for (const LVRangeEntry &RangeEntry : RangeEntries) {
    LVScope *Scope = RangeEntry.scope();
    Scope->printAttributes(OS, Full);
    Indentation = options().indentationSize();
    if (Indentation)
      OS << " ";
    OS << format("[0x%08x,0x%08x] ", RangeEntry.lower(), RangeEntry.upper())
       << formattedKind(Scope->kind()) << " " << formattedName(Scope->getName())
       << "\n";
  }
  printExtra(OS, Full);
}
