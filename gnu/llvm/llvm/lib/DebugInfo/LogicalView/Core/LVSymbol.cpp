//===-- LVSymbol.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVSymbol class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include "llvm/DebugInfo/LogicalView/Core/LVCompare.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLocation.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Symbol"

namespace {
const char *const KindCallSiteParameter = "CallSiteParameter";
const char *const KindConstant = "Constant";
const char *const KindInherits = "Inherits";
const char *const KindMember = "Member";
const char *const KindParameter = "Parameter";
const char *const KindUndefined = "Undefined";
const char *const KindUnspecified = "Unspecified";
const char *const KindVariable = "Variable";
} // end anonymous namespace

// Return a string representation for the symbol kind.
const char *LVSymbol::kind() const {
  const char *Kind = KindUndefined;
  if (getIsCallSiteParameter())
    Kind = KindCallSiteParameter;
  else if (getIsConstant())
    Kind = KindConstant;
  else if (getIsInheritance())
    Kind = KindInherits;
  else if (getIsMember())
    Kind = KindMember;
  else if (getIsParameter())
    Kind = KindParameter;
  else if (getIsUnspecified())
    Kind = KindUnspecified;
  else if (getIsVariable())
    Kind = KindVariable;
  return Kind;
}

LVSymbolDispatch LVSymbol::Dispatch = {
    {LVSymbolKind::IsCallSiteParameter, &LVSymbol::getIsCallSiteParameter},
    {LVSymbolKind::IsConstant, &LVSymbol::getIsConstant},
    {LVSymbolKind::IsInheritance, &LVSymbol::getIsInheritance},
    {LVSymbolKind::IsMember, &LVSymbol::getIsMember},
    {LVSymbolKind::IsParameter, &LVSymbol::getIsParameter},
    {LVSymbolKind::IsUnspecified, &LVSymbol::getIsUnspecified},
    {LVSymbolKind::IsVariable, &LVSymbol::getIsVariable}};

// Add a Location Entry.
void LVSymbol::addLocation(dwarf::Attribute Attr, LVAddress LowPC,
                           LVAddress HighPC, LVUnsigned SectionOffset,
                           uint64_t LocDescOffset, bool CallSiteLocation) {
  if (!Locations)
    Locations = std::make_unique<LVLocations>();

  // Create the location entry.
  CurrentLocation = getReader().createLocationSymbol();
  CurrentLocation->setParent(this);
  CurrentLocation->setAttr(Attr);
  if (CallSiteLocation)
    CurrentLocation->setIsCallSite();
  CurrentLocation->addObject(LowPC, HighPC, SectionOffset, LocDescOffset);
  Locations->push_back(CurrentLocation);

  // Mark the symbol as having location information.
  setHasLocation();
}

// Add a Location Record.
void LVSymbol::addLocationOperands(LVSmall Opcode,
                                   ArrayRef<uint64_t> Operands) {
  if (CurrentLocation)
    CurrentLocation->addObject(Opcode, Operands);
}

// Add a Location Entry.
void LVSymbol::addLocationConstant(dwarf::Attribute Attr, LVUnsigned Constant,
                                   uint64_t LocDescOffset) {
  // Create a Location Entry, with the global information.
  addLocation(Attr,
              /*LowPC=*/0, /*HighPC=*/-1,
              /*SectionOffset=*/0, LocDescOffset);

  // Add records to Location Entry.
  addLocationOperands(/*Opcode=*/LVLocationMemberOffset, {Constant});
}

LVLocations::iterator LVSymbol::addLocationGap(LVLocations::iterator Pos,
                                               LVAddress LowPC,
                                               LVAddress HighPC) {
  // Create a location entry for the gap.
  LVLocation *Gap = getReader().createLocationSymbol();
  Gap->setParent(this);
  Gap->setAttr(dwarf::DW_AT_location);
  Gap->addObject(LowPC, HighPC,
                 /*section_offset=*/0,
                 /*locdesc_offset=*/0);

  LVLocations::iterator Iter = Locations->insert(Pos, Gap);

  // Add gap to Location Entry.
  Gap->addObject(dwarf::DW_OP_hi_user, {});

  // Mark the entry as a gap.
  Gap->setIsGapEntry();

  return Iter;
}

void LVSymbol::fillLocationGaps() {
  // The symbol has locations records. Fill gaps in the location list.
  if (!getHasLocation() || !getFillGaps())
    return;

  // Get the parent range information and add dummy location entries.
  const LVLocations *Ranges = getParentScope()->getRanges();
  if (!Ranges)
    return;

  for (const LVLocation *Entry : *Ranges) {
    LVAddress ParentLowPC = Entry->getLowerAddress();
    LVAddress ParentHighPC = Entry->getUpperAddress();

    // Traverse the symbol locations and for each location contained in
    // the current parent range, insert locations for any existing gap.
    LVLocation *Location;
    LVAddress LowPC = 0;
    LVAddress Marker = ParentLowPC;
    for (LVLocations::iterator Iter = Locations->begin();
         Iter != Locations->end(); ++Iter) {
      Location = *Iter;
      LowPC = Location->getLowerAddress();
      if (LowPC != Marker) {
        // We have a gap at [Marker,LowPC - 1].
        Iter = addLocationGap(Iter, Marker, LowPC - 1);
        ++Iter;
      }

      // Move to the next item in the location list.
      Marker = Location->getUpperAddress() + 1;
    }

    // Check any gap at the end.
    if (Marker < ParentHighPC)
      // We have a gap at [Marker,ParentHighPC].
      addLocationGap(Locations->end(), Marker, ParentHighPC);
  }
}

// Get all the locations based on the valid function.
void LVSymbol::getLocations(LVLocations &LocationList,
                            LVValidLocation ValidLocation, bool RecordInvalid) {
  if (!Locations)
    return;

  for (LVLocation *Location : *Locations) {
    // Add the invalid location object.
    if (!(Location->*ValidLocation)() && RecordInvalid)
      LocationList.push_back(Location);
  }

  // Calculate coverage factor.
  calculateCoverage();
}

void LVSymbol::getLocations(LVLocations &LocationList) const {
  if (!Locations)
    return;

  for (LVLocation *Location : *Locations)
    LocationList.push_back(Location);
}

// Calculate coverage factor.
void LVSymbol::calculateCoverage() {
  if (!LVLocation::calculateCoverage(Locations.get(), CoverageFactor,
                                     CoveragePercentage)) {
    LVScope *Parent = getParentScope();
    if (Parent->getIsInlinedFunction()) {
      // For symbols representing the inlined function parameters and its
      // variables, get the outer most parent that contains their location
      // lower address.
      // The symbol can have a set of non-contiguous locations. We are using
      // only the first location entry to get the outermost parent.
      // If no scope contains the location, assume its enclosing parent.
      LVScope *Scope =
          Parent->outermostParent(Locations->front()->getLowerAddress());
      if (Scope)
        Parent = Scope;
    }
    unsigned CoverageParent = Parent->getCoverageFactor();
    // Get a percentage rounded to two decimal digits. This avoids
    // implementation-defined rounding inside printing functions.
    CoveragePercentage =
        CoverageParent
            ? rint((double(CoverageFactor) / CoverageParent) * 100.0 * 100.0) /
                  100.0
            : 0;
    // Record invalid coverage entry.
    if (options().getWarningCoverages() && CoveragePercentage > 100)
      getReaderCompileUnit()->addInvalidCoverage(this);
  }
}

void LVSymbol::resolveName() {
  if (getIsResolvedName())
    return;
  setIsResolvedName();

  LVElement::resolveName();

  // Resolve any given pattern.
  patterns().resolvePatternMatch(this);
}

void LVSymbol::resolveReferences() {
  // The symbols can have the following references to other elements:
  //   A Type:
  //     DW_AT_type             ->  Type or Scope
  //     DW_AT_import           ->  Type
  //   A Reference:
  //     DW_AT_specification    ->  Symbol
  //     DW_AT_abstract_origin  ->  Symbol
  //     DW_AT_extension        ->  Symbol

  // Resolve any referenced symbol.
  LVSymbol *Reference = getReference();
  if (Reference) {
    Reference->resolve();
    // Recursively resolve the symbol names.
    resolveReferencesChain();
  }

  // Set the file/line information using the Debug Information entry.
  setFile(Reference);

  // Resolve symbol type.
  if (LVElement *Element = getType()) {
    Element->resolve();

    // In the case of demoted typedefs, use the underlying type.
    if (Element->getIsTypedefReduced()) {
      Element = Element->getType();
      Element->resolve();
    }

    // If the type is a template parameter, get its type, which can
    // point to a type or scope, depending on the argument instance.
    setGenericType(Element);
  }

  // Resolve the variable associated type.
  if (!getType() && Reference)
    setType(Reference->getType());
}

StringRef LVSymbol::resolveReferencesChain() {
  // If the symbol have a DW_AT_specification or DW_AT_abstract_origin,
  // follow the chain to resolve the name from those references.
  if (getHasReference() && !isNamed())
    setName(getReference()->resolveReferencesChain());

  return getName();
}

void LVSymbol::markMissingParents(const LVSymbols *References,
                                  const LVSymbols *Targets) {
  if (!(References && Targets))
    return;

  LLVM_DEBUG({
    dbgs() << "\n[LVSymbol::markMissingParents]\n";
    for (const LVSymbol *Reference : *References)
      dbgs() << "References: "
             << "Kind = " << formattedKind(Reference->kind()) << ", "
             << "Name = " << formattedName(Reference->getName()) << "\n";
    for (const LVSymbol *Target : *Targets)
      dbgs() << "Targets   : "
             << "Kind = " << formattedKind(Target->kind()) << ", "
             << "Name = " << formattedName(Target->getName()) << "\n";
  });

  for (LVSymbol *Reference : *References) {
    LLVM_DEBUG({
      dbgs() << "Search Reference: Name = "
             << formattedName(Reference->getName()) << "\n";
    });
    if (!Reference->findIn(Targets))
      Reference->markBranchAsMissing();
  }
}

LVSymbol *LVSymbol::findIn(const LVSymbols *Targets) const {
  if (!Targets)
    return nullptr;

  LLVM_DEBUG({
    dbgs() << "\n[LVSymbol::findIn]\n"
           << "Reference: "
           << "Level = " << getLevel() << ", "
           << "Kind = " << formattedKind(kind()) << ", "
           << "Name = " << formattedName(getName()) << "\n";
    for (const LVSymbol *Target : *Targets)
      dbgs() << "Target   : "
             << "Level = " << Target->getLevel() << ", "
             << "Kind = " << formattedKind(Target->kind()) << ", "
             << "Name = " << formattedName(Target->getName()) << "\n";
  });

  for (LVSymbol *Target : *Targets)
    if (equals(Target))
      return Target;

  return nullptr;
}

// Check for a match on the arguments of a function.
bool LVSymbol::parametersMatch(const LVSymbols *References,
                               const LVSymbols *Targets) {
  if (!References && !Targets)
    return true;
  if (References && Targets) {
    LVSymbols ReferenceParams;
    getParameters(References, &ReferenceParams);
    LVSymbols TargetParams;
    getParameters(Targets, &TargetParams);
    return LVSymbol::equals(&ReferenceParams, &TargetParams);
  }
  return false;
}

// Return the symbols which are parameters.
void LVSymbol::getParameters(const LVSymbols *Symbols, LVSymbols *Parameters) {
  if (Symbols)
    for (LVSymbol *Symbol : *Symbols)
      if (Symbol->getIsParameter())
        Parameters->push_back(Symbol);
}

bool LVSymbol::equals(const LVSymbol *Symbol) const {
  if (!LVElement::equals(Symbol))
    return false;

  // Check if any reference is the same.
  if (!referenceMatch(Symbol))
    return false;

  if (getReference() && !getReference()->equals(Symbol->getReference()))
    return false;

  return true;
}

bool LVSymbol::equals(const LVSymbols *References, const LVSymbols *Targets) {
  if (!References && !Targets)
    return true;
  if (References && Targets && References->size() == Targets->size()) {
    for (const LVSymbol *Reference : *References)
      if (!Reference->findIn(Targets))
        return false;
    return true;
  }
  return false;
}

void LVSymbol::report(LVComparePass Pass) {
  getComparator().printItem(this, Pass);
}

void LVSymbol::printLocations(raw_ostream &OS, bool Full) const {
  if (Locations)
    for (const LVLocation *Location : *Locations)
      Location->printRaw(OS, Full);
}

void LVSymbol::print(raw_ostream &OS, bool Full) const {
  if (getIncludeInPrint() && getReader().doPrintSymbol(this)) {
    getReaderCompileUnit()->incrementPrintedSymbols();
    LVElement::print(OS, Full);
    printExtra(OS, Full);
  }
}

void LVSymbol::printExtra(raw_ostream &OS, bool Full) const {
  // Accessibility depends on the parent (class, structure).
  uint32_t AccessCode = 0;
  if (getIsMember() || getIsInheritance())
    AccessCode = getParentScope()->getIsClass() ? dwarf::DW_ACCESS_private
                                                : dwarf::DW_ACCESS_public;

  const LVSymbol *Symbol = getIsInlined() ? Reference : this;
  std::string Attributes =
      Symbol->getIsCallSiteParameter()
          ? ""
          : formatAttributes(Symbol->externalString(),
                             Symbol->accessibilityString(AccessCode),
                             virtualityString());

  OS << formattedKind(Symbol->kind()) << " " << Attributes;
  if (Symbol->getIsUnspecified())
    OS << formattedName(Symbol->getName());
  else {
    if (Symbol->getIsInheritance())
      OS << Symbol->typeOffsetAsString()
         << formattedNames(Symbol->getTypeQualifiedName(),
                           Symbol->typeAsString());
    else {
      OS << formattedName(Symbol->getName());
      // Print any bitfield information.
      if (uint32_t Size = getBitSize())
        OS << ":" << Size;
      OS << " -> " << Symbol->typeOffsetAsString()
         << formattedNames(Symbol->getTypeQualifiedName(),
                           Symbol->typeAsString());
    }
  }

  // Print any initial value if any.
  if (ValueIndex)
    OS << " = " << formattedName(getValue());
  OS << "\n";

  if (Full && options().getPrintFormatting()) {
    if (getLinkageNameIndex())
      printLinkageName(OS, Full, const_cast<LVSymbol *>(this));
    if (LVSymbol *Reference = getReference())
      Reference->printReference(OS, Full, const_cast<LVSymbol *>(this));

    // Print location information.
    LVLocation::print(Locations.get(), OS, Full);
  }
}
