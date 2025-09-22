//===-- LVOptions.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVOptions class, which is used to record the command
// line options.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOPTIONS_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOPTIONS_H

#include "llvm/ADT/StringSet.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include "llvm/DebugInfo/LogicalView/Core/LVType.h"
#include "llvm/Support/Regex.h"
#include <set>
#include <string>

namespace llvm {
namespace logicalview {

// Generate get and set 'bool' functions.
#define BOOL_FUNCTION(FAMILY, FIELD)                                           \
  bool get##FAMILY##FIELD() const { return FAMILY.FIELD; }                     \
  void set##FAMILY##FIELD() { FAMILY.FIELD = true; }                           \
  void reset##FAMILY##FIELD() { FAMILY.FIELD = false; }

// Generate get and set 'unsigned' functions.
#define UNSIGNED_FUNCTION(FAMILY, FIELD)                                       \
  unsigned get##FAMILY##FIELD() const { return FAMILY.FIELD; }                 \
  void set##FAMILY##FIELD(unsigned Value) { FAMILY.FIELD = Value; }            \
  void reset##FAMILY##FIELD() { FAMILY.FIELD = -1U; }

// Generate get and set 'std::string' functions.
#define STD_STRING_FUNCTION(FAMILY, FIELD)                                     \
  std::string get##FAMILY##FIELD() const { return FAMILY.FIELD; }              \
  void set##FAMILY##FIELD(std::string FIELD) { FAMILY.FIELD = FIELD; }         \
  void reset##FAMILY##FIELD() { FAMILY.FIELD = ""; }

// Generate get and set 'std::set' functions.
#define STDSET_FUNCTION_4(FAMILY, FIELD, TYPE, SET)                            \
  bool get##FAMILY##FIELD() const {                                            \
    return FAMILY.SET.find(TYPE::FIELD) != FAMILY.SET.end();                   \
  }                                                                            \
  void set##FAMILY##FIELD() { FAMILY.SET.insert(TYPE::FIELD); }                \
  void reset##FAMILY##FIELD() {                                                \
    std::set<TYPE>::iterator Iter = FAMILY.SET.find(TYPE::FIELD);              \
    if (Iter != FAMILY.SET.end())                                              \
      FAMILY.SET.erase(Iter);                                                  \
  }

#define STDSET_FUNCTION_5(FAMILY, FIELD, ENTRY, TYPE, SET)                     \
  bool get##FAMILY##FIELD##ENTRY() const {                                     \
    return FAMILY.SET.find(TYPE::ENTRY) != FAMILY.SET.end();                   \
  }                                                                            \
  void set##FAMILY##FIELD##ENTRY() { FAMILY.SET.insert(TYPE::ENTRY); }

// Generate get and set functions for '--attribute'
#define ATTRIBUTE_OPTION(FIELD)                                                \
  STDSET_FUNCTION_4(Attribute, FIELD, LVAttributeKind, Kinds)

// Generate get and set functions for '--output'
#define OUTPUT_OPTION(FIELD)                                                   \
  STDSET_FUNCTION_4(Output, FIELD, LVOutputKind, Kinds)

// Generate get and set functions for '--print'
#define PRINT_OPTION(FIELD) STDSET_FUNCTION_4(Print, FIELD, LVPrintKind, Kinds)

// Generate get and set functions for '--warning'
#define WARNING_OPTION(FIELD)                                                  \
  STDSET_FUNCTION_4(Warning, FIELD, LVWarningKind, Kinds)

// Generate get and set functions for '--compare'
#define COMPARE_OPTION(FIELD)                                                  \
  STDSET_FUNCTION_4(Compare, FIELD, LVCompareKind, Elements)

// Generate get and set functions for '--report'
#define REPORT_OPTION(FIELD)                                                   \
  STDSET_FUNCTION_4(Report, FIELD, LVReportKind, Kinds)

// Generate get and set functions for '--internal'
#define INTERNAL_OPTION(FIELD)                                                 \
  STDSET_FUNCTION_4(Internal, FIELD, LVInternalKind, Kinds)

using LVOffsetSet = std::set<uint64_t>;

enum class LVAttributeKind {
  All,           // --attribute=all
  Argument,      // --attribute=argument
  Base,          // --attribute=base
  Coverage,      // --attribute=coverage
  Directories,   // --attribute=directories
  Discarded,     // --attribute=discarded
  Discriminator, // --attribute=discriminator
  Encoded,       // --attribute=encoded
  Extended,      // --attribute=extended
  Filename,      // --attribute=filename
  Files,         // --attribute=files
  Format,        // --attribute=format
  Gaps,          // --attribute=gaps
  Generated,     // --attribute=generated
  Global,        // --attribute=global
  Inserted,      // --attribute=inserted
  Level,         // --attribute=level
  Linkage,       // --attribute=linkage
  Local,         // --attribute=local
  Location,      // --attribute=location
  Offset,        // --attribute=offset
  Pathname,      // --attribute=pathname
  Producer,      // --attribute=producer
  Publics,       // --attribute=publics
  Qualified,     // --attribute=qualified
  Qualifier,     // --attribute=qualifier
  Range,         // --attribute=range
  Reference,     // --attribute=reference
  Register,      // --attribute=register
  Standard,      // --attribute=standard
  Subrange,      // --attribute=subrange
  System,        // --attribute=system
  Typename,      // --attribute=typename
  Underlying,    // --attribute=underlying
  Zero           // --attribute=zero
};
using LVAttributeKindSet = std::set<LVAttributeKind>;

enum class LVCompareKind {
  All,     // --compare=all
  Lines,   // --compare=lines
  Scopes,  // --compare=scopes
  Symbols, // --compare=symbols
  Types    // --compare=types
};
using LVCompareKindSet = std::set<LVCompareKind>;

enum class LVOutputKind {
  All,   // --output=all
  Split, // --output=split
  Json,  // --output=json
  Text   // --output=text
};
using LVOutputKindSet = std::set<LVOutputKind>;

enum class LVPrintKind {
  All,          // --print=all
  Elements,     // --print=elements
  Instructions, // --print=instructions
  Lines,        // --print=lines
  Scopes,       // --print=scopes
  Sizes,        // --print=sizes
  Symbols,      // --print=symbols
  Summary,      // --print=summary
  Types,        // --print=types
  Warnings      // --print=warnings
};
using LVPrintKindSet = std::set<LVPrintKind>;

enum class LVReportKind {
  All,      // --report=all
  Children, // --report=children
  List,     // --report=list
  Parents,  // --report=parents
  View      // --report=view
};
using LVReportKindSet = std::set<LVReportKind>;

enum class LVWarningKind {
  All,       // --warning=all
  Coverages, // --warning=coverages
  Lines,     // --warning=lines
  Locations, // --warning=locations
  Ranges     // --warning=ranges
};
using LVWarningKindSet = std::set<LVWarningKind>;

enum class LVInternalKind {
  All,       // --internal=all
  Cmdline,   // --internal=cmdline
  ID,        // --internal=id
  Integrity, // --internal=integrity
  None,      // --internal=none
  Tag        // --internal=tag
};
using LVInternalKindSet = std::set<LVInternalKind>;

// The 'Kinds' members are a one-to-one mapping to the associated command
// options that supports comma separated values. There are other 'bool'
// members that in very few cases point to a command option (see associated
// comment). Other cases for 'bool' refers to internal values derivated from
// the command options.
class LVOptions {
  class LVAttribute {
  public:
    LVAttributeKindSet Kinds; // --attribute=<Kind>
    bool Added = false;       // Added elements found during comparison.
    bool AnyLocation = false; // Any kind of location information.
    bool AnySource = false;   // Any kind of source information.
    bool Missing = false;     // Missing elements found during comparison.
  };

  class LVCompare {
  public:
    LVCompareKindSet Elements; // --compare=<kind>
    bool Context = false;      // --compare-context
    bool Execute = false;      // Compare requested.
    bool Print = false;        // Enable any printing.
  };

  class LVPrint {
  public:
    LVPrintKindSet Kinds;      // --print=<Kind>
    bool AnyElement = false;   // Request to print any element.
    bool AnyLine = false;      // Print 'lines' or 'instructions'.
    bool Execute = false;      // Print requested.
    bool Formatting = true;    // Disable formatting during printing.
    bool Offset = false;       // Print offsets while formatting is disabled.
    bool SizesSummary = false; // Print 'sizes' or 'summary'.
  };

  class LVReport {
  public:
    LVReportKindSet Kinds; // --report=<kind>
    bool AnyView = false;  // View, Parents or Children.
    bool Execute = false;  // Report requested.
  };

  class LVSelect {
  public:
    bool IgnoreCase = false;     // --select-ignore-case
    bool UseRegex = false;       // --select-use-regex
    bool Execute = false;        // Select requested.
    bool GenericKind = false;    // We have collected generic kinds.
    bool GenericPattern = false; // We have collected generic patterns.
    bool OffsetPattern = false;  // We have collected offset patterns.
    StringSet<> Generic;         // --select=<Pattern>
    LVOffsetSet Offsets;         // --select-offset=<Offset>
    LVElementKindSet Elements;   // --select-elements=<Kind>
    LVLineKindSet Lines;         // --select-lines=<Kind>
    LVScopeKindSet Scopes;       // --select-scopes=<Kind>
    LVSymbolKindSet Symbols;     // --select-symbols=<Kind>
    LVTypeKindSelection Types;   // --select-types=<Kind>
  };

  class LVOutput {
  public:
    LVOutputKindSet Kinds;                  // --output=<kind>
    LVSortMode SortMode = LVSortMode::None; // --output-sort=<SortMode>
    std::string Folder;                     // --output-folder=<Folder>
    unsigned Level = -1U;                   // --output-level=<level>
  };

  class LVWarning {
  public:
    LVWarningKindSet Kinds; // --warning=<Kind>
  };

  class LVInternal {
  public:
    LVInternalKindSet Kinds; // --internal=<Kind>
  };

  class LVGeneral {
  public:
    bool CollectRanges = false; // Collect ranges information.
  };

  // Filters the output of the filename associated with the element being
  // printed in order to see clearly which logical elements belongs to
  // a particular filename. It is value is reset after the element
  // that represents the Compile Unit is printed.
  size_t LastFilenameIndex = 0;

  // Controls the amount of additional spaces to insert when printing
  // object attributes, in order to get a consistent printing layout.
  size_t IndentationSize = 0;

  // Calculate the indentation size, so we can use that value when printing
  // additional attributes to objects, such as location.
  void calculateIndentationSize();

public:
  void resetFilenameIndex() { LastFilenameIndex = 0; }
  bool changeFilenameIndex(size_t Index) {
    bool IndexChanged = (Index != LastFilenameIndex);
    if (IndexChanged)
      LastFilenameIndex = Index;
    return IndexChanged;
  }

  // Access to command line options, pattern and printing information.
  static LVOptions *getOptions();
  static void setOptions(LVOptions *Options);

  LVOptions() = default;
  LVOptions(const LVOptions &) = default;
  LVOptions &operator=(const LVOptions &) = default;
  ~LVOptions() = default;

  // Some command line options support shortcuts. For example:
  // The command line option '--print=elements' is a shortcut for:
  // '--print=instructions,lines,scopes,symbols,types'.
  // In the case of logical view comparison, some options related to
  // attributes must be set or reset for a proper comparison.
  // Resolve any dependencies between command line options.
  void resolveDependencies();
  size_t indentationSize() const { return IndentationSize; }

  LVAttribute Attribute;
  LVCompare Compare;
  LVOutput Output;
  LVPrint Print;
  LVReport Report;
  LVSelect Select;
  LVWarning Warning;
  LVInternal Internal;
  LVGeneral General;

  // --attribute.
  ATTRIBUTE_OPTION(All);
  ATTRIBUTE_OPTION(Argument);
  ATTRIBUTE_OPTION(Base);
  ATTRIBUTE_OPTION(Coverage);
  ATTRIBUTE_OPTION(Directories);
  ATTRIBUTE_OPTION(Discarded);
  ATTRIBUTE_OPTION(Discriminator);
  ATTRIBUTE_OPTION(Encoded);
  ATTRIBUTE_OPTION(Extended);
  ATTRIBUTE_OPTION(Filename);
  ATTRIBUTE_OPTION(Files);
  ATTRIBUTE_OPTION(Format);
  ATTRIBUTE_OPTION(Gaps);
  ATTRIBUTE_OPTION(Generated);
  ATTRIBUTE_OPTION(Global);
  ATTRIBUTE_OPTION(Inserted);
  ATTRIBUTE_OPTION(Level);
  ATTRIBUTE_OPTION(Linkage);
  ATTRIBUTE_OPTION(Location);
  ATTRIBUTE_OPTION(Local);
  ATTRIBUTE_OPTION(Offset);
  ATTRIBUTE_OPTION(Pathname);
  ATTRIBUTE_OPTION(Producer);
  ATTRIBUTE_OPTION(Publics);
  ATTRIBUTE_OPTION(Qualified);
  ATTRIBUTE_OPTION(Qualifier);
  ATTRIBUTE_OPTION(Range);
  ATTRIBUTE_OPTION(Reference);
  ATTRIBUTE_OPTION(Register);
  ATTRIBUTE_OPTION(Standard);
  ATTRIBUTE_OPTION(Subrange);
  ATTRIBUTE_OPTION(System);
  ATTRIBUTE_OPTION(Typename);
  ATTRIBUTE_OPTION(Underlying);
  ATTRIBUTE_OPTION(Zero);
  BOOL_FUNCTION(Attribute, Added);
  BOOL_FUNCTION(Attribute, AnyLocation);
  BOOL_FUNCTION(Attribute, AnySource);
  BOOL_FUNCTION(Attribute, Missing);

  // --compare.
  COMPARE_OPTION(All);
  COMPARE_OPTION(Lines);
  COMPARE_OPTION(Scopes);
  COMPARE_OPTION(Symbols);
  COMPARE_OPTION(Types);
  BOOL_FUNCTION(Compare, Context);
  BOOL_FUNCTION(Compare, Execute);
  BOOL_FUNCTION(Compare, Print);

  // --output.
  OUTPUT_OPTION(All);
  OUTPUT_OPTION(Split);
  OUTPUT_OPTION(Text);
  OUTPUT_OPTION(Json);
  STD_STRING_FUNCTION(Output, Folder);
  UNSIGNED_FUNCTION(Output, Level);
  LVSortMode getSortMode() const { return Output.SortMode; }
  void setSortMode(LVSortMode SortMode) { Output.SortMode = SortMode; }

  // --print.
  PRINT_OPTION(All);
  PRINT_OPTION(Elements);
  PRINT_OPTION(Instructions);
  PRINT_OPTION(Lines);
  PRINT_OPTION(Scopes);
  PRINT_OPTION(Sizes);
  PRINT_OPTION(Symbols);
  PRINT_OPTION(Summary);
  PRINT_OPTION(Types);
  PRINT_OPTION(Warnings);
  BOOL_FUNCTION(Print, AnyElement);
  BOOL_FUNCTION(Print, AnyLine);
  BOOL_FUNCTION(Print, Execute);
  BOOL_FUNCTION(Print, Formatting);
  BOOL_FUNCTION(Print, Offset);
  BOOL_FUNCTION(Print, SizesSummary);

  // --report.
  REPORT_OPTION(All);
  REPORT_OPTION(Children);
  REPORT_OPTION(List);
  REPORT_OPTION(Parents);
  REPORT_OPTION(View);
  BOOL_FUNCTION(Report, AnyView);
  BOOL_FUNCTION(Report, Execute);

  // --select.
  BOOL_FUNCTION(Select, IgnoreCase);
  BOOL_FUNCTION(Select, UseRegex);
  BOOL_FUNCTION(Select, Execute);
  BOOL_FUNCTION(Select, GenericKind);
  BOOL_FUNCTION(Select, GenericPattern);
  BOOL_FUNCTION(Select, OffsetPattern);

  // --warning.
  WARNING_OPTION(All);
  WARNING_OPTION(Coverages);
  WARNING_OPTION(Lines);
  WARNING_OPTION(Locations);
  WARNING_OPTION(Ranges);

  // --internal.
  INTERNAL_OPTION(All);
  INTERNAL_OPTION(Cmdline);
  INTERNAL_OPTION(ID);
  INTERNAL_OPTION(Integrity);
  INTERNAL_OPTION(None);
  INTERNAL_OPTION(Tag);

  // General shortcuts to some combinations.
  BOOL_FUNCTION(General, CollectRanges);

  void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const { print(dbgs()); }
#endif
};

inline LVOptions &options() { return (*LVOptions::getOptions()); }
inline void setOptions(LVOptions *Options) { LVOptions::setOptions(Options); }

class LVPatterns final {
  // Pattern Mode.
  enum class LVMatchMode {
    None = 0, // No given pattern.
    Match,    // Perfect match.
    NoCase,   // Ignore case.
    Regex     // Regular expression.
  };

  // Keep the search pattern information.
  struct LVMatch {
    std::string Pattern;                  // Normal pattern.
    std::shared_ptr<Regex> RE;            // Regular Expression Pattern.
    LVMatchMode Mode = LVMatchMode::None; // Match mode.
  };

  using LVMatchInfo = std::vector<LVMatch>;
  LVMatchInfo GenericMatchInfo;
  using LVMatchOffsets = std::vector<uint64_t>;
  LVMatchOffsets OffsetMatchInfo;

  // Element selection.
  LVElementDispatch ElementDispatch;
  LVLineDispatch LineDispatch;
  LVScopeDispatch ScopeDispatch;
  LVSymbolDispatch SymbolDispatch;
  LVTypeDispatch TypeDispatch;

  // Element selection request.
  LVElementRequest ElementRequest;
  LVLineRequest LineRequest;
  LVScopeRequest ScopeRequest;
  LVSymbolRequest SymbolRequest;
  LVTypeRequest TypeRequest;

  // Check an element printing Request.
  template <typename T, typename U>
  bool checkElementRequest(const T *Element, const U &Requests) const {
    assert(Element && "Element must not be nullptr");
    for (const auto &Request : Requests)
      if ((Element->*Request)())
        return true;
    // Check generic element requests.
    for (const LVElementGetFunction &Request : ElementRequest)
      if ((Element->*Request)())
        return true;
    return false;
  }

  // Add an element printing request based on its kind.
  template <typename T, typename U, typename V>
  void addRequest(const T &Selection, const U &Dispatch, V &Request) const {
    for (const auto &Entry : Selection) {
      // Find target function to fullfit request.
      typename U::const_iterator Iter = Dispatch.find(Entry);
      if (Iter != Dispatch.end())
        Request.push_back(Iter->second);
    }
  }

  void addElement(LVElement *Element);

  template <typename T, typename U>
  void resolveGenericPatternMatch(T *Element, const U &Requests) {
    assert(Element && "Element must not be nullptr");
    auto CheckPattern = [this, Element]() -> bool {
      return (Element->isNamed() &&
              (matchGenericPattern(Element->getName()) ||
               matchGenericPattern(Element->getLinkageName()))) ||
             (Element->isTyped() &&
              matchGenericPattern(Element->getTypeName()));
    };
    auto CheckOffset = [this, Element]() -> bool {
      return matchOffsetPattern(Element->getOffset());
    };
    if ((options().getSelectGenericPattern() && CheckPattern()) ||
        (options().getSelectOffsetPattern() && CheckOffset()) ||
        ((Requests.size() || ElementRequest.size()) &&
         checkElementRequest(Element, Requests)))
      addElement(Element);
  }

  template <typename U>
  void resolveGenericPatternMatch(LVLine *Line, const U &Requests) {
    assert(Line && "Line must not be nullptr");
    auto CheckPattern = [this, Line]() -> bool {
      return matchGenericPattern(Line->lineNumberAsStringStripped()) ||
             matchGenericPattern(Line->getName()) ||
             matchGenericPattern(Line->getPathname());
    };
    auto CheckOffset = [this, Line]() -> bool {
      return matchOffsetPattern(Line->getAddress());
    };
    if ((options().getSelectGenericPattern() && CheckPattern()) ||
        (options().getSelectOffsetPattern() && CheckOffset()) ||
        (Requests.size() && checkElementRequest(Line, Requests)))
      addElement(Line);
  }

  Error createMatchEntry(LVMatchInfo &Filters, StringRef Pattern,
                         bool IgnoreCase, bool UseRegex);

public:
  static LVPatterns *getPatterns();

  LVPatterns() {
    ElementDispatch = LVElement::getDispatch();
    LineDispatch = LVLine::getDispatch();
    ScopeDispatch = LVScope::getDispatch();
    SymbolDispatch = LVSymbol::getDispatch();
    TypeDispatch = LVType::getDispatch();
  }
  LVPatterns(const LVPatterns &) = delete;
  LVPatterns &operator=(const LVPatterns &) = delete;
  ~LVPatterns() = default;

  // Clear any existing patterns.
  void clear() {
    GenericMatchInfo.clear();
    OffsetMatchInfo.clear();
    ElementRequest.clear();
    LineRequest.clear();
    ScopeRequest.clear();
    SymbolRequest.clear();
    TypeRequest.clear();

    options().resetSelectGenericKind();
    options().resetSelectGenericPattern();
    options().resetSelectOffsetPattern();
  }

  void addRequest(LVElementKindSet &Selection) {
    addRequest(Selection, ElementDispatch, ElementRequest);
  }
  void addRequest(LVLineKindSet &Selection) {
    addRequest(Selection, LineDispatch, LineRequest);
  }
  void addRequest(LVScopeKindSet &Selection) {
    addRequest(Selection, ScopeDispatch, ScopeRequest);
  }
  void addRequest(LVSymbolKindSet &Selection) {
    addRequest(Selection, SymbolDispatch, SymbolRequest);
  }
  void addRequest(LVTypeKindSelection &Selection) {
    addRequest(Selection, TypeDispatch, TypeRequest);
  }

  void updateReportOptions();

  bool matchPattern(StringRef Input, const LVMatchInfo &MatchInfo);
  // Match a pattern (--select='pattern').
  bool matchGenericPattern(StringRef Input) {
    return matchPattern(Input, GenericMatchInfo);
  }
  bool matchOffsetPattern(LVOffset Offset) {
    return llvm::is_contained(OffsetMatchInfo, Offset);
  }

  void resolvePatternMatch(LVLine *Line) {
    resolveGenericPatternMatch(Line, LineRequest);
  }

  void resolvePatternMatch(LVScope *Scope) {
    resolveGenericPatternMatch(Scope, ScopeRequest);
  }

  void resolvePatternMatch(LVSymbol *Symbol) {
    resolveGenericPatternMatch(Symbol, SymbolRequest);
  }

  void resolvePatternMatch(LVType *Type) {
    resolveGenericPatternMatch(Type, TypeRequest);
  }

  void addPatterns(StringSet<> &Patterns, LVMatchInfo &Filters);

  // Add generic and offset patterns info.
  void addGenericPatterns(StringSet<> &Patterns);
  void addOffsetPatterns(const LVOffsetSet &Patterns);

  // Conditions to print an object.
  bool printElement(const LVLine *Line) const;
  bool printObject(const LVLocation *Location) const;
  bool printElement(const LVScope *Scope) const;
  bool printElement(const LVSymbol *Symbol) const;
  bool printElement(const LVType *Type) const;

  void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const { print(dbgs()); }
#endif
};

inline LVPatterns &patterns() { return *LVPatterns::getPatterns(); }

} // namespace logicalview
} // namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVOPTIONS_H
