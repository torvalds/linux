//===-- LVSymbol.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVSymbol class, which is used to describe a debug
// information symbol.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSYMBOL_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSYMBOL_H

#include "llvm/DebugInfo/LogicalView/Core/LVElement.h"

namespace llvm {
namespace logicalview {

enum class LVSymbolKind {
  IsCallSiteParameter,
  IsConstant,
  IsInheritance,
  IsMember,
  IsParameter,
  IsUnspecified,
  IsVariable,
  LastEntry
};
using LVSymbolKindSet = std::set<LVSymbolKind>;
using LVSymbolDispatch = std::map<LVSymbolKind, LVSymbolGetFunction>;
using LVSymbolRequest = std::vector<LVSymbolGetFunction>;

class LVSymbol final : public LVElement {
  enum class Property { HasLocation, FillGaps, LastEntry };

  // Typed bitvector with kinds and properties for this symbol.
  LVProperties<LVSymbolKind> Kinds;
  LVProperties<Property> Properties;
  static LVSymbolDispatch Dispatch;

  // CodeView symbol Linkage name.
  size_t LinkageNameIndex = 0;

  // Reference to DW_AT_specification, DW_AT_abstract_origin attribute.
  LVSymbol *Reference = nullptr;
  std::unique_ptr<LVLocations> Locations;
  LVLocation *CurrentLocation = nullptr;

  // Bitfields length.
  uint32_t BitSize = 0;

  // Index in the String pool representing any initial value.
  size_t ValueIndex = 0;

  // Coverage factor in units (bytes).
  unsigned CoverageFactor = 0;
  float CoveragePercentage = 0;

  // Add a location gap into the location list.
  LVLocations::iterator addLocationGap(LVLocations::iterator Pos,
                                       LVAddress LowPC, LVAddress HighPC);

  // Find the current symbol in the given 'Targets'.
  LVSymbol *findIn(const LVSymbols *Targets) const;

public:
  LVSymbol() : LVElement(LVSubclassID::LV_SYMBOL) {
    setIsSymbol();
    setIncludeInPrint();
  }
  LVSymbol(const LVSymbol &) = delete;
  LVSymbol &operator=(const LVSymbol &) = delete;
  ~LVSymbol() = default;

  static bool classof(const LVElement *Element) {
    return Element->getSubclassID() == LVSubclassID::LV_SYMBOL;
  }

  KIND(LVSymbolKind, IsCallSiteParameter);
  KIND(LVSymbolKind, IsConstant);
  KIND(LVSymbolKind, IsInheritance);
  KIND(LVSymbolKind, IsMember);
  KIND(LVSymbolKind, IsParameter);
  KIND(LVSymbolKind, IsUnspecified);
  KIND(LVSymbolKind, IsVariable);

  PROPERTY(Property, HasLocation);
  PROPERTY(Property, FillGaps);

  const char *kind() const override;

  // Access DW_AT_specification, DW_AT_abstract_origin reference.
  LVSymbol *getReference() const { return Reference; }
  void setReference(LVSymbol *Symbol) override {
    Reference = Symbol;
    setHasReference();
  }
  void setReference(LVElement *Element) override {
    assert((!Element || isa<LVSymbol>(Element)) && "Invalid element");
    setReference(static_cast<LVSymbol *>(Element));
  }

  void setLinkageName(StringRef LinkageName) override {
    LinkageNameIndex = getStringPool().getIndex(LinkageName);
  }
  StringRef getLinkageName() const override {
    return getStringPool().getString(LinkageNameIndex);
  }
  size_t getLinkageNameIndex() const override { return LinkageNameIndex; }

  uint32_t getBitSize() const override { return BitSize; }
  void setBitSize(uint32_t Size) override { BitSize = Size; }

  // Process the values for a DW_AT_const_value.
  StringRef getValue() const override {
    return getStringPool().getString(ValueIndex);
  }
  void setValue(StringRef Value) override {
    ValueIndex = getStringPool().getIndex(Value);
  }
  size_t getValueIndex() const override { return ValueIndex; }

  // Add a Location Entry.
  void addLocationConstant(dwarf::Attribute Attr, LVUnsigned Constant,
                           uint64_t LocDescOffset);
  void addLocationOperands(LVSmall Opcode, ArrayRef<uint64_t> Operands);
  void addLocation(dwarf::Attribute Attr, LVAddress LowPC, LVAddress HighPC,
                   LVUnsigned SectionOffset, uint64_t LocDescOffset,
                   bool CallSiteLocation = false);

  // Fill gaps in the location list.
  void fillLocationGaps();

  // Get all the locations associated with symbols.
  void getLocations(LVLocations &LocationList, LVValidLocation ValidLocation,
                    bool RecordInvalid = false);
  void getLocations(LVLocations &LocationList) const;

  // Calculate coverage factor.
  void calculateCoverage();

  unsigned getCoverageFactor() const { return CoverageFactor; }
  void setCoverageFactor(unsigned Value) { CoverageFactor = Value; }
  float getCoveragePercentage() const { return CoveragePercentage; }
  void setCoveragePercentage(float Value) { CoveragePercentage = Value; }

  // Print location in raw format.
  void printLocations(raw_ostream &OS, bool Full = true) const;

  // Follow a chain of references given by DW_AT_abstract_origin and/or
  // DW_AT_specification and update the symbol name.
  StringRef resolveReferencesChain();

  void resolveName() override;
  void resolveReferences() override;

  static LVSymbolDispatch &getDispatch() { return Dispatch; }

  static bool parametersMatch(const LVSymbols *References,
                              const LVSymbols *Targets);

  static void getParameters(const LVSymbols *Symbols, LVSymbols *Parameters);

  // Iterate through the 'References' set and check that all its elements
  // are present in the 'Targets' set. For a missing element, mark its
  // parents as missing.
  static void markMissingParents(const LVSymbols *References,
                                 const LVSymbols *Targets);

  // Returns true if current type is logically equal to the given 'Symbol'.
  bool equals(const LVSymbol *Symbol) const;

  // Returns true if the given 'References' are logically equal to the
  // given 'Targets'.
  static bool equals(const LVSymbols *References, const LVSymbols *Targets);

  // Report the current symbol as missing or added during comparison.
  void report(LVComparePass Pass) override;

  void print(raw_ostream &OS, bool Full = true) const override;
  void printExtra(raw_ostream &OS, bool Full = true) const override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const override { print(dbgs()); }
#endif
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSYMBOL_H
