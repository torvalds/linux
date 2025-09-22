//===-- LVScope.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVScope class, which is used to describe a debug
// information scope.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSCOPE_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSCOPE_H

#include "llvm/DebugInfo/LogicalView/Core/LVElement.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLocation.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSort.h"
#include "llvm/Object/ObjectFile.h"
#include <list>
#include <map>
#include <set>

namespace llvm {
namespace logicalview {

// Name address, Code size.
using LVNameInfo = std::pair<LVAddress, uint64_t>;
using LVPublicNames = std::map<LVScope *, LVNameInfo>;
using LVPublicAddresses = std::map<LVAddress, LVNameInfo>;

class LVRange;

enum class LVScopeKind {
  IsAggregate,
  IsArray,
  IsBlock,
  IsCallSite,
  IsCatchBlock,
  IsClass,
  IsCompileUnit,
  IsEntryPoint,
  IsEnumeration,
  IsFunction,
  IsFunctionType,
  IsInlinedFunction,
  IsLabel,
  IsLexicalBlock,
  IsMember,
  IsNamespace,
  IsRoot,
  IsStructure,
  IsSubprogram,
  IsTemplate,
  IsTemplateAlias,
  IsTemplatePack,
  IsTryBlock,
  IsUnion,
  LastEntry
};
using LVScopeKindSet = std::set<LVScopeKind>;
using LVScopeDispatch = std::map<LVScopeKind, LVScopeGetFunction>;
using LVScopeRequest = std::vector<LVScopeGetFunction>;

using LVOffsetElementMap = std::map<LVOffset, LVElement *>;
using LVOffsetLinesMap = std::map<LVOffset, LVLines>;
using LVOffsetLocationsMap = std::map<LVOffset, LVLocations>;
using LVOffsetSymbolMap = std::map<LVOffset, LVSymbol *>;
using LVTagOffsetsMap = std::map<dwarf::Tag, LVOffsets>;

// Class to represent a DWARF Scope.
class LVScope : public LVElement {
  enum class Property {
    HasDiscriminator,
    CanHaveRanges,
    CanHaveLines,
    HasGlobals,
    HasLocals,
    HasLines,
    HasScopes,
    HasSymbols,
    HasTypes,
    IsComdat,
    HasComdatScopes, // Compile Unit has comdat functions.
    HasRanges,
    AddedMissing, // Added missing referenced symbols.
    LastEntry
  };

  // Typed bitvector with kinds and properties for this scope.
  LVProperties<LVScopeKind> Kinds;
  LVProperties<Property> Properties;
  static LVScopeDispatch Dispatch;

  // Coverage factor in units (bytes).
  unsigned CoverageFactor = 0;

  // Calculate coverage factor.
  void calculateCoverage() {
    float CoveragePercentage = 0;
    LVLocation::calculateCoverage(Ranges.get(), CoverageFactor,
                                  CoveragePercentage);
  }

  // Decide if the scope will be printed, using some conditions given by:
  // only-globals, only-locals, a-pattern.
  bool resolvePrinting() const;

  // Find the current scope in the given 'Targets'.
  LVScope *findIn(const LVScopes *Targets) const;

  // Traverse the scope parent tree, executing the given callback function
  // on each scope.
  void traverseParents(LVScopeGetFunction GetFunction,
                       LVScopeSetFunction SetFunction);

protected:
  // Types, Symbols, Scopes, Lines, Locations in this scope.
  std::unique_ptr<LVTypes> Types;
  std::unique_ptr<LVSymbols> Symbols;
  std::unique_ptr<LVScopes> Scopes;
  std::unique_ptr<LVLines> Lines;
  std::unique_ptr<LVLocations> Ranges;

  // Vector of elements (types, scopes and symbols).
  // It is the union of (*Types, *Symbols and *Scopes) to be used for
  // the following reasons:
  // - Preserve the order the logical elements are read in.
  // - To have a single container with all the logical elements, when
  //   the traversal does not require any specific element kind.
  std::unique_ptr<LVElements> Children;

  // Resolve the template parameters/arguments relationship.
  void resolveTemplate();
  void printEncodedArgs(raw_ostream &OS, bool Full) const;

  void printActiveRanges(raw_ostream &OS, bool Full = true) const;
  virtual void printSizes(raw_ostream &OS) const {}
  virtual void printSummary(raw_ostream &OS) const {}

  // Encoded template arguments.
  virtual StringRef getEncodedArgs() const { return StringRef(); }
  virtual void setEncodedArgs(StringRef EncodedArgs) {}

public:
  LVScope() : LVElement(LVSubclassID::LV_SCOPE) {
    setIsScope();
    setIncludeInPrint();
  }
  LVScope(const LVScope &) = delete;
  LVScope &operator=(const LVScope &) = delete;
  virtual ~LVScope() = default;

  static bool classof(const LVElement *Element) {
    return Element->getSubclassID() == LVSubclassID::LV_SCOPE;
  }

  KIND(LVScopeKind, IsAggregate);
  KIND(LVScopeKind, IsArray);
  KIND_2(LVScopeKind, IsBlock, CanHaveRanges, CanHaveLines);
  KIND_1(LVScopeKind, IsCallSite, IsFunction);
  KIND_1(LVScopeKind, IsCatchBlock, IsBlock);
  KIND_1(LVScopeKind, IsClass, IsAggregate);
  KIND_3(LVScopeKind, IsCompileUnit, CanHaveRanges, CanHaveLines,
         TransformName);
  KIND_1(LVScopeKind, IsEntryPoint, IsFunction);
  KIND(LVScopeKind, IsEnumeration);
  KIND_2(LVScopeKind, IsFunction, CanHaveRanges, CanHaveLines);
  KIND_1(LVScopeKind, IsFunctionType, IsFunction);
  KIND_2(LVScopeKind, IsInlinedFunction, IsFunction, IsInlined);
  KIND_1(LVScopeKind, IsLabel, IsFunction);
  KIND_1(LVScopeKind, IsLexicalBlock, IsBlock);
  KIND(LVScopeKind, IsMember);
  KIND(LVScopeKind, IsNamespace);
  KIND_1(LVScopeKind, IsRoot, TransformName);
  KIND_1(LVScopeKind, IsStructure, IsAggregate);
  KIND_1(LVScopeKind, IsSubprogram, IsFunction);
  KIND(LVScopeKind, IsTemplate);
  KIND(LVScopeKind, IsTemplateAlias);
  KIND(LVScopeKind, IsTemplatePack);
  KIND_1(LVScopeKind, IsTryBlock, IsBlock);
  KIND_1(LVScopeKind, IsUnion, IsAggregate);

  PROPERTY(Property, HasDiscriminator);
  PROPERTY(Property, CanHaveRanges);
  PROPERTY(Property, CanHaveLines);
  PROPERTY(Property, HasGlobals);
  PROPERTY(Property, HasLocals);
  PROPERTY(Property, HasLines);
  PROPERTY(Property, HasScopes);
  PROPERTY(Property, HasSymbols);
  PROPERTY(Property, HasTypes);
  PROPERTY(Property, IsComdat);
  PROPERTY(Property, HasComdatScopes);
  PROPERTY(Property, HasRanges);
  PROPERTY(Property, AddedMissing);

  bool isCompileUnit() const override { return getIsCompileUnit(); }
  bool isRoot() const override { return getIsRoot(); }

  const char *kind() const override;

  // Get the specific children.
  const LVLines *getLines() const { return Lines.get(); }
  const LVLocations *getRanges() const { return Ranges.get(); }
  const LVScopes *getScopes() const { return Scopes.get(); }
  const LVSymbols *getSymbols() const { return Symbols.get(); }
  const LVTypes *getTypes() const { return Types.get(); }
  const LVElements *getChildren() const { return Children.get(); }

  void addElement(LVElement *Element);
  void addElement(LVLine *Line);
  void addElement(LVScope *Scope);
  void addElement(LVSymbol *Symbol);
  void addElement(LVType *Type);
  void addObject(LVLocation *Location);
  void addObject(LVAddress LowerAddress, LVAddress UpperAddress);
  void addToChildren(LVElement *Element);

  // Add the missing elements from the given 'Reference', which is the
  // scope associated with any DW_AT_specification, DW_AT_abstract_origin.
  void addMissingElements(LVScope *Reference);

  // Traverse the scope parent tree and the children, executing the given
  // callback function on each element.
  void traverseParentsAndChildren(LVObjectGetFunction GetFunction,
                                  LVObjectSetFunction SetFunction);

  // Get the size of specific children.
  size_t lineCount() const { return Lines ? Lines->size() : 0; }
  size_t rangeCount() const { return Ranges ? Ranges->size() : 0; }
  size_t scopeCount() const { return Scopes ? Scopes->size() : 0; }
  size_t symbolCount() const { return Symbols ? Symbols->size() : 0; }
  size_t typeCount() const { return Types ? Types->size() : 0; }

  // Find containing parent for the given address.
  LVScope *outermostParent(LVAddress Address);

  // Get all the locations associated with symbols.
  void getLocations(LVLocations &LocationList, LVValidLocation ValidLocation,
                    bool RecordInvalid = false);
  void getRanges(LVLocations &LocationList, LVValidLocation ValidLocation,
                 bool RecordInvalid = false);
  void getRanges(LVRange &RangeList);

  unsigned getCoverageFactor() const { return CoverageFactor; }

  Error doPrint(bool Split, bool Match, bool Print, raw_ostream &OS,
                bool Full = true) const override;
  // Sort the logical elements using the criteria specified by the
  // command line option '--output-sort'.
  void sort();

  // Get template parameter types.
  bool getTemplateParameterTypes(LVTypes &Params);

  // DW_AT_specification, DW_AT_abstract_origin, DW_AT_extension.
  virtual LVScope *getReference() const { return nullptr; }

  LVScope *getCompileUnitParent() const override {
    return LVElement::getCompileUnitParent();
  }

  // Follow a chain of references given by DW_AT_abstract_origin and/or
  // DW_AT_specification and update the scope name.
  StringRef resolveReferencesChain();

  bool removeElement(LVElement *Element) override;
  void updateLevel(LVScope *Parent, bool Moved) override;

  void resolve() override;
  void resolveName() override;
  void resolveReferences() override;

  // Return the chain of parents as a string.
  void getQualifiedName(std::string &QualifiedName) const;
  // Encode the template arguments.
  void encodeTemplateArguments(std::string &Name) const;
  void encodeTemplateArguments(std::string &Name, const LVTypes *Types) const;

  void resolveElements();

  // Iterate through the 'References' set and check that all its elements
  // are present in the 'Targets' set. For a missing element, mark its
  // parents as missing.
  static void markMissingParents(const LVScopes *References,
                                 const LVScopes *Targets,
                                 bool TraverseChildren);

  // Checks if the current scope is contained within the target scope.
  // Depending on the result, the callback may be performed.
  virtual void markMissingParents(const LVScope *Target, bool TraverseChildren);

  // Returns true if the current scope and the given 'Scope' have the
  // same number of children.
  virtual bool equalNumberOfChildren(const LVScope *Scope) const;

  // Returns true if current scope is logically equal to the given 'Scope'.
  virtual bool equals(const LVScope *Scope) const;

  // Returns true if the given 'References' are logically equal to the
  // given 'Targets'.
  static bool equals(const LVScopes *References, const LVScopes *Targets);

  // For the given 'Scopes' returns a scope that is logically equal
  // to the current scope; otherwise 'nullptr'.
  virtual LVScope *findEqualScope(const LVScopes *Scopes) const;

  // Report the current scope as missing or added during comparison.
  void report(LVComparePass Pass) override;

  static LVScopeDispatch &getDispatch() { return Dispatch; }

  void print(raw_ostream &OS, bool Full = true) const override;
  void printExtra(raw_ostream &OS, bool Full = true) const override;
  virtual void printWarnings(raw_ostream &OS, bool Full = true) const {}
  virtual void printMatchedElements(raw_ostream &OS, bool UseMatchedElements) {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const override { print(dbgs()); }
#endif
};

// Class to represent a DWARF Union/Structure/Class.
class LVScopeAggregate final : public LVScope {
  LVScope *Reference = nullptr; // DW_AT_specification, DW_AT_abstract_origin.
  size_t EncodedArgsIndex = 0;  // Template encoded arguments.

public:
  LVScopeAggregate() : LVScope() {}
  LVScopeAggregate(const LVScopeAggregate &) = delete;
  LVScopeAggregate &operator=(const LVScopeAggregate &) = delete;
  ~LVScopeAggregate() = default;

  // DW_AT_specification, DW_AT_abstract_origin.
  LVScope *getReference() const override { return Reference; }
  void setReference(LVScope *Scope) override {
    Reference = Scope;
    setHasReference();
  }
  void setReference(LVElement *Element) override {
    setReference(static_cast<LVScope *>(Element));
  }

  StringRef getEncodedArgs() const override {
    return getStringPool().getString(EncodedArgsIndex);
  }
  void setEncodedArgs(StringRef EncodedArgs) override {
    EncodedArgsIndex = getStringPool().getIndex(EncodedArgs);
  }

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  // For the given 'Scopes' returns a scope that is logically equal
  // to the current scope; otherwise 'nullptr'.
  LVScope *findEqualScope(const LVScopes *Scopes) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

// Class to represent a DWARF Template alias.
class LVScopeAlias final : public LVScope {
public:
  LVScopeAlias() : LVScope() {
    setIsTemplateAlias();
    setIsTemplate();
  }
  LVScopeAlias(const LVScopeAlias &) = delete;
  LVScopeAlias &operator=(const LVScopeAlias &) = delete;
  ~LVScopeAlias() = default;

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

// Class to represent a DWARF array (DW_TAG_array_type).
class LVScopeArray final : public LVScope {
public:
  LVScopeArray() : LVScope() { setIsArray(); }
  LVScopeArray(const LVScopeArray &) = delete;
  LVScopeArray &operator=(const LVScopeArray &) = delete;
  ~LVScopeArray() = default;

  void resolveExtra() override;

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

// Class to represent a DWARF Compilation Unit (CU).
class LVScopeCompileUnit final : public LVScope {
  // Names (files and directories) used by the Compile Unit.
  std::vector<size_t> Filenames;

  // As the .debug_pubnames section has been removed in DWARF5, we have a
  // similar functionality, which is used by the decoded functions. We use
  // the low-pc and high-pc for those scopes that are marked as public, in
  // order to support DWARF and CodeView.
  LVPublicNames PublicNames;

  // Toolchain producer.
  size_t ProducerIndex = 0;

  // Compilation directory name.
  size_t CompilationDirectoryIndex = 0;

  // Used by the CodeView Reader.
  codeview::CPUType CompilationCPUType = codeview::CPUType::X64;

  // Keep record of elements. They are needed at the compilation unit level
  // to print the summary at the end of the printing.
  LVCounter Allocated;
  LVCounter Found;
  LVCounter Printed;

  // Elements that match a given command line pattern.
  LVElements MatchedElements;
  LVScopes MatchedScopes;

  // It records the mapping between logical lines representing a debug line
  // entry and its address in the text section. It is used to find a line
  // giving its exact or closest address. To support comdat functions, all
  // addresses for the same section are recorded in the same map.
  using LVAddressToLine = std::map<LVAddress, LVLine *>;
  LVDoubleMap<LVSectionIndex, LVAddress, LVLine *> SectionMappings;

  // DWARF Tags (Tag, Element list).
  LVTagOffsetsMap DebugTags;

  // Offsets associated with objects being flagged as having invalid data
  // (ranges, locations, lines zero or coverages).
  LVOffsetElementMap WarningOffsets;

  // Symbols with invalid locations. (Symbol, Location List).
  LVOffsetLocationsMap InvalidLocations;

  // Symbols with invalid coverage values.
  LVOffsetSymbolMap InvalidCoverages;

  // Scopes with invalid ranges (Scope, Range list).
  LVOffsetLocationsMap InvalidRanges;

  // Scopes with lines zero (Scope, Line list).
  LVOffsetLinesMap LinesZero;

  // Record scopes contribution in bytes to the debug information.
  using LVSizesMap = std::map<const LVScope *, LVOffset>;
  LVSizesMap Sizes;
  LVOffset CUContributionSize = 0;

  // Helper function to add an invalid location/range.
  void addInvalidLocationOrRange(LVLocation *Location, LVElement *Element,
                                 LVOffsetLocationsMap *Map) {
    LVOffset Offset = Element->getOffset();
    addInvalidOffset(Offset, Element);
    addItem<LVOffsetLocationsMap, LVOffset, LVLocation *>(Map, Offset,
                                                          Location);
  }

  // Record scope sizes indexed by lexical level.
  // Setting an initial size that will cover a very deep nested scopes.
  const size_t TotalInitialSize = 8;
  using LVTotalsEntry = std::pair<unsigned, float>;
  SmallVector<LVTotalsEntry> Totals;
  // Maximum seen lexical level. It is used to control how many entries
  // in the 'Totals' vector are valid values.
  LVLevel MaxSeenLevel = 0;

  // Get the line located at the given address.
  LVLine *lineLowerBound(LVAddress Address, LVScope *Scope) const;
  LVLine *lineUpperBound(LVAddress Address, LVScope *Scope) const;

  void printScopeSize(const LVScope *Scope, raw_ostream &OS);
  void printScopeSize(const LVScope *Scope, raw_ostream &OS) const {
    (const_cast<LVScopeCompileUnit *>(this))->printScopeSize(Scope, OS);
  }
  void printTotals(raw_ostream &OS) const;

protected:
  void printSizes(raw_ostream &OS) const override;
  void printSummary(raw_ostream &OS) const override;

public:
  LVScopeCompileUnit() : LVScope(), Totals(TotalInitialSize, {0, 0.0}) {
    setIsCompileUnit();
  }
  LVScopeCompileUnit(const LVScopeCompileUnit &) = delete;
  LVScopeCompileUnit &operator=(const LVScopeCompileUnit &) = delete;
  ~LVScopeCompileUnit() = default;

  LVScope *getCompileUnitParent() const override {
    return static_cast<LVScope *>(const_cast<LVScopeCompileUnit *>(this));
  }

  // Add line to address mapping.
  void addMapping(LVLine *Line, LVSectionIndex SectionIndex);
  LVLineRange lineRange(LVLocation *Location) const;

  LVNameInfo NameNone = {UINT64_MAX, 0};
  void addPublicName(LVScope *Scope, LVAddress LowPC, LVAddress HighPC) {
    PublicNames.emplace(std::piecewise_construct, std::forward_as_tuple(Scope),
                        std::forward_as_tuple(LowPC, HighPC - LowPC));
  }
  const LVNameInfo &findPublicName(LVScope *Scope) {
    LVPublicNames::iterator Iter = PublicNames.find(Scope);
    return (Iter != PublicNames.end()) ? Iter->second : NameNone;
  }
  const LVPublicNames &getPublicNames() const { return PublicNames; }

  // The base address of the scope for any of the debugging information
  // entries listed, is given by either the DW_AT_low_pc attribute or the
  // first address in the first range entry in the list of ranges given by
  // the DW_AT_ranges attribute.
  LVAddress getBaseAddress() const {
    return Ranges ? Ranges->front()->getLowerAddress() : 0;
  }

  StringRef getCompilationDirectory() const {
    return getStringPool().getString(CompilationDirectoryIndex);
  }
  void setCompilationDirectory(StringRef CompilationDirectory) {
    CompilationDirectoryIndex = getStringPool().getIndex(CompilationDirectory);
  }

  StringRef getFilename(size_t Index) const;
  void addFilename(StringRef Name) {
    Filenames.push_back(getStringPool().getIndex(Name));
  }

  StringRef getProducer() const override {
    return getStringPool().getString(ProducerIndex);
  }
  void setProducer(StringRef ProducerName) override {
    ProducerIndex = getStringPool().getIndex(ProducerName);
  }

  void setCPUType(codeview::CPUType Type) { CompilationCPUType = Type; }
  codeview::CPUType getCPUType() { return CompilationCPUType; }

  // Record DWARF tags.
  void addDebugTag(dwarf::Tag Target, LVOffset Offset);
  // Record elements with invalid offsets.
  void addInvalidOffset(LVOffset Offset, LVElement *Element);
  // Record symbols with invalid coverage values.
  void addInvalidCoverage(LVSymbol *Symbol);
  // Record symbols with invalid locations.
  void addInvalidLocation(LVLocation *Location);
  // Record scopes with invalid ranges.
  void addInvalidRange(LVLocation *Location);
  // Record line zero.
  void addLineZero(LVLine *Line);

  const LVTagOffsetsMap &getDebugTags() const { return DebugTags; }
  const LVOffsetElementMap &getWarningOffsets() const { return WarningOffsets; }
  const LVOffsetLocationsMap &getInvalidLocations() const {
    return InvalidLocations;
  }
  const LVOffsetSymbolMap &getInvalidCoverages() const {
    return InvalidCoverages;
  }
  const LVOffsetLocationsMap &getInvalidRanges() const { return InvalidRanges; }
  const LVOffsetLinesMap &getLinesZero() const { return LinesZero; }

  // Process ranges, locations and calculate coverage.
  void processRangeLocationCoverage(
      LVValidLocation ValidLocation = &LVLocation::validateRanges);

  // Add matched element.
  void addMatched(LVElement *Element) { MatchedElements.push_back(Element); }
  void addMatched(LVScope *Scope) { MatchedScopes.push_back(Scope); }
  void propagatePatternMatch();

  const LVElements &getMatchedElements() const { return MatchedElements; }
  const LVScopes &getMatchedScopes() const { return MatchedScopes; }

  void printLocalNames(raw_ostream &OS, bool Full = true) const;
  void printSummary(raw_ostream &OS, const LVCounter &Counter,
                    const char *Header) const;

  void incrementPrintedLines();
  void incrementPrintedScopes();
  void incrementPrintedSymbols();
  void incrementPrintedTypes();

  // Values are used by '--summary' option (allocated).
  void increment(LVLine *Line);
  void increment(LVScope *Scope);
  void increment(LVSymbol *Symbol);
  void increment(LVType *Type);

  // A new element has been added to the scopes tree. Take the following steps:
  // Increase the added element counters, for printing summary.
  // During comparison notify the Reader of the new element.
  void addedElement(LVLine *Line);
  void addedElement(LVScope *Scope);
  void addedElement(LVSymbol *Symbol);
  void addedElement(LVType *Type);

  void addSize(LVScope *Scope, LVOffset Lower, LVOffset Upper);

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  void print(raw_ostream &OS, bool Full = true) const override;
  void printExtra(raw_ostream &OS, bool Full = true) const override;
  void printWarnings(raw_ostream &OS, bool Full = true) const override;
  void printMatchedElements(raw_ostream &OS, bool UseMatchedElements) override;
};

// Class to represent a DWARF enumerator (DW_TAG_enumeration_type).
class LVScopeEnumeration final : public LVScope {
public:
  LVScopeEnumeration() : LVScope() { setIsEnumeration(); }
  LVScopeEnumeration(const LVScopeEnumeration &) = delete;
  LVScopeEnumeration &operator=(const LVScopeEnumeration &) = delete;
  ~LVScopeEnumeration() = default;

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

// Class to represent a DWARF formal parameter pack
// (DW_TAG_GNU_formal_parameter_pack).
class LVScopeFormalPack final : public LVScope {
public:
  LVScopeFormalPack() : LVScope() { setIsTemplatePack(); }
  LVScopeFormalPack(const LVScopeFormalPack &) = delete;
  LVScopeFormalPack &operator=(const LVScopeFormalPack &) = delete;
  ~LVScopeFormalPack() = default;

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

// Class to represent a DWARF Function.
class LVScopeFunction : public LVScope {
  LVScope *Reference = nullptr; // DW_AT_specification, DW_AT_abstract_origin.
  size_t LinkageNameIndex = 0;  // Function DW_AT_linkage_name attribute.
  size_t EncodedArgsIndex = 0;  // Template encoded arguments.

public:
  LVScopeFunction() : LVScope() {}
  LVScopeFunction(const LVScopeFunction &) = delete;
  LVScopeFunction &operator=(const LVScopeFunction &) = delete;
  virtual ~LVScopeFunction() = default;

  // DW_AT_specification, DW_AT_abstract_origin.
  LVScope *getReference() const override { return Reference; }
  void setReference(LVScope *Scope) override {
    Reference = Scope;
    setHasReference();
  }
  void setReference(LVElement *Element) override {
    setReference(static_cast<LVScope *>(Element));
  }

  StringRef getEncodedArgs() const override {
    return getStringPool().getString(EncodedArgsIndex);
  }
  void setEncodedArgs(StringRef EncodedArgs) override {
    EncodedArgsIndex = getStringPool().getIndex(EncodedArgs);
  }

  void setLinkageName(StringRef LinkageName) override {
    LinkageNameIndex = getStringPool().getIndex(LinkageName);
  }
  StringRef getLinkageName() const override {
    return getStringPool().getString(LinkageNameIndex);
  }
  size_t getLinkageNameIndex() const override { return LinkageNameIndex; }

  void setName(StringRef ObjectName) override;

  void resolveExtra() override;
  void resolveReferences() override;

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  // For the given 'Scopes' returns a scope that is logically equal
  // to the current scope; otherwise 'nullptr'.
  LVScope *findEqualScope(const LVScopes *Scopes) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

// Class to represent a DWARF inlined function.
class LVScopeFunctionInlined final : public LVScopeFunction {
  size_t CallFilenameIndex = 0;
  uint32_t CallLineNumber = 0;
  uint32_t Discriminator = 0;

public:
  LVScopeFunctionInlined() : LVScopeFunction() { setIsInlinedFunction(); }
  LVScopeFunctionInlined(const LVScopeFunctionInlined &) = delete;
  LVScopeFunctionInlined &operator=(const LVScopeFunctionInlined &) = delete;
  ~LVScopeFunctionInlined() = default;

  uint32_t getDiscriminator() const override { return Discriminator; }
  void setDiscriminator(uint32_t Value) override {
    Discriminator = Value;
    setHasDiscriminator();
  }

  uint32_t getCallLineNumber() const override { return CallLineNumber; }
  void setCallLineNumber(uint32_t Number) override { CallLineNumber = Number; }
  size_t getCallFilenameIndex() const override { return CallFilenameIndex; }
  void setCallFilenameIndex(size_t Index) override {
    CallFilenameIndex = Index;
  }

  // Line number for display; in the case of Inlined Functions, we use the
  // DW_AT_call_line attribute; otherwise use DW_AT_decl_line attribute.
  std::string lineNumberAsString(bool ShowZero = false) const override {
    return lineAsString(getCallLineNumber(), getDiscriminator(), ShowZero);
  }

  void resolveExtra() override;

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  // For the given 'Scopes' returns a scope that is logically equal
  // to the current scope; otherwise 'nullptr'.
  LVScope *findEqualScope(const LVScopes *Scopes) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

// Class to represent a DWARF subroutine type.
class LVScopeFunctionType final : public LVScopeFunction {
public:
  LVScopeFunctionType() : LVScopeFunction() { setIsFunctionType(); }
  LVScopeFunctionType(const LVScopeFunctionType &) = delete;
  LVScopeFunctionType &operator=(const LVScopeFunctionType &) = delete;
  ~LVScopeFunctionType() = default;

  void resolveExtra() override;
};

// Class to represent a DWARF Namespace.
class LVScopeNamespace final : public LVScope {
  LVScope *Reference = nullptr; // Reference to DW_AT_extension attribute.

public:
  LVScopeNamespace() : LVScope() { setIsNamespace(); }
  LVScopeNamespace(const LVScopeNamespace &) = delete;
  LVScopeNamespace &operator=(const LVScopeNamespace &) = delete;
  ~LVScopeNamespace() = default;

  // Access DW_AT_extension reference.
  LVScope *getReference() const override { return Reference; }
  void setReference(LVScope *Scope) override {
    Reference = Scope;
    setHasReference();
  }
  void setReference(LVElement *Element) override {
    setReference(static_cast<LVScope *>(Element));
  }

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  // For the given 'Scopes' returns a scope that is logically equal
  // to the current scope; otherwise 'nullptr'.
  LVScope *findEqualScope(const LVScopes *Scopes) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

// Class to represent the binary file being analyzed.
class LVScopeRoot final : public LVScope {
  size_t FileFormatNameIndex = 0;

public:
  LVScopeRoot() : LVScope() { setIsRoot(); }
  LVScopeRoot(const LVScopeRoot &) = delete;
  LVScopeRoot &operator=(const LVScopeRoot &) = delete;
  ~LVScopeRoot() = default;

  StringRef getFileFormatName() const {
    return getStringPool().getString(FileFormatNameIndex);
  }
  void setFileFormatName(StringRef FileFormatName) {
    FileFormatNameIndex = getStringPool().getIndex(FileFormatName);
  }

  // The CodeView Reader uses scoped names. Recursively transform the
  // element name to use just the most inner component.
  void transformScopedName();

  // Process the collected location, ranges and calculate coverage.
  void processRangeInformation();

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  void print(raw_ostream &OS, bool Full = true) const override;
  void printExtra(raw_ostream &OS, bool Full = true) const override;
  Error doPrintMatches(bool Split, raw_ostream &OS,
                       bool UseMatchedElements) const;
};

// Class to represent a DWARF template parameter pack
// (DW_TAG_GNU_template_parameter_pack).
class LVScopeTemplatePack final : public LVScope {
public:
  LVScopeTemplatePack() : LVScope() { setIsTemplatePack(); }
  LVScopeTemplatePack(const LVScopeTemplatePack &) = delete;
  LVScopeTemplatePack &operator=(const LVScopeTemplatePack &) = delete;
  ~LVScopeTemplatePack() = default;

  // Returns true if current scope is logically equal to the given 'Scope'.
  bool equals(const LVScope *Scope) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSCOPE_H
