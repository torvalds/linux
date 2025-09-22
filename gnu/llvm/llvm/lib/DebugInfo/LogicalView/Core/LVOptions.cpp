//===-- LVOptions.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVOptions class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/Support/Errc.h"

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Options"

//===----------------------------------------------------------------------===//
// Options extracted from the command line.
//===----------------------------------------------------------------------===//
static LVOptions Options;
LVOptions *LVOptions::getOptions() { return &Options; }
void LVOptions::setOptions(LVOptions *CmdOptions) { Options = *CmdOptions; }

void LVOptions::resolveDependencies() {
  // Attributes that are classified as standard options.
  auto StandardAttributes = [&]() {
    // Set the 'standard' attribute to indicate its associated attributes.
    setAttributeStandard();

    setAttributeBase();
    setAttributeCoverage();
    setAttributeDirectories();
    setAttributeDiscriminator();
    setAttributeFilename();
    setAttributeFiles();
    setAttributeFormat();
    setAttributeLevel();
    setAttributeProducer();
    setAttributePublics();
    setAttributeRange();
    setAttributeReference();
    setAttributeZero();
  };

  // Attributes that are classified as extended options.
  auto ExtendedAttributes = [&]() {
    // Set the 'extended' attribute to indicate its associated attributes.
    setAttributeExtended();

    setAttributeArgument();
    setAttributeDiscarded();
    setAttributeEncoded();
    setAttributeGaps();
    setAttributeGenerated();
    setAttributeGlobal();
    setAttributeInserted();
    setAttributeLinkage();
    setAttributeLocal();
    setAttributeLocation();
    setAttributeOffset();
    setAttributePathname();
    setAttributeQualified();
    setAttributeQualifier();
    setAttributeRegister();
    setAttributeSubrange();
    setAttributeSystem();
    setAttributeTypename();
  };

  // '--Attribute=standard' settings.
  if (getAttributeStandard())
    StandardAttributes();

  // '--Attribute=extended' settings.
  if (getAttributeExtended())
    ExtendedAttributes();

  // '--Attribute=all' settings.
  if (getAttributeAll()) {
    StandardAttributes();
    ExtendedAttributes();
  }

  // '--attribute=pathname' supersedes '--attribute=filename'.
  if (getAttributePathname())
    resetAttributeFilename();

  // Assume '--output=text' as default
  if (!getOutputText() && !getOutputJson())
    setOutputText();

  // '--output=all' settings.
  if (getOutputAll()) {
    setOutputJson();
    setOutputSplit();
    setOutputText();
  }

  // A view split folder was specified.
  if (getOutputFolder().length())
    setOutputSplit();

  // Always use the full pathname with splitted output.
  if (getOutputSplit())
    setAttributePathname();

  // '--print=elements' settings.
  if (getPrintElements()) {
    setPrintInstructions();
    setPrintLines();
    setPrintScopes();
    setPrintSymbols();
    setPrintTypes();
  }

  // '--print=all' settings.
  if (getPrintAll()) {
    setPrintInstructions();
    setPrintLines();
    setPrintScopes();
    setPrintSizes();
    setPrintSymbols();
    setPrintSummary();
    setPrintTypes();
    setPrintWarnings();
  }

  // '--warning=all' settings.
  if (getWarningAll()) {
    setWarningCoverages();
    setWarningLines();
    setWarningLocations();
    setWarningRanges();
  }

  // '--internal=all' settings.
  if (getInternalAll()) {
    setInternalCmdline();
    setInternalID();
    setInternalIntegrity();
    setInternalNone();
    setInternalTag();
  }

  // '--compare=all' settings.
  if (getCompareAll()) {
    setCompareLines();
    setCompareScopes();
    setCompareSymbols();
    setCompareTypes();
  }

  // Compare the scopes if a request for compare symbols, types, lines.
  if (getCompareLines() || getCompareSymbols() || getCompareTypes())
    setCompareScopes();

  // Generic request for comparison.
  if (getCompareScopes())
    setCompareExecute();

  // Print any logical line (debug or instruction).
  if (getPrintInstructions() || getPrintLines())
    setPrintAnyLine();

  // Print any logical element (line, scope, symbol or type).
  if (getPrintAnyLine() || getPrintScopes() || getPrintSymbols() ||
      getPrintTypes())
    setPrintAnyElement();

  // Print 'sizes' or 'summary'.
  if (getPrintSizes() && getPrintSummary())
    setPrintSizesSummary();

  // Generic request for printing.
  if (getPrintAll() || getPrintAnyElement() || getPrintSizesSummary() ||
      getPrintWarnings())
    setPrintExecute();

  // '--reports=all' settings.
  if (getReportAll()) {
    setReportChildren();
    setReportList();
    setReportParents();
    setReportView();
  }

  // '--report=view' is a shortcut for '--report=parents,children'.
  if (getReportView()) {
    setReportChildren();
    setReportParents();
  }

  // The report will include: Parents or Children.
  if (getReportParents() || getReportChildren() || getReportView())
    setReportAnyView();

  // The report will include: List or Parents or Children.
  if (getReportList() || getReportAnyView())
    setReportExecute();

  // If a view or element comparison has been requested, the following options
  // must be set, in order to get a correct compare:
  // 1) Sort the CUs, to get a fast compare.
  // 2) Encode template instantiations, so the names include template
  //    parameter information.
  // 3) Include qualified types.
  // 4) Include any inserted abstract references.
  // 5) For added/missing elements add the '+' or '-' tags.
  if (getCompareExecute()) {
    resetPrintExecute();
    setComparePrint();
    setSortMode(LVSortMode::Line);
    setAttributeAdded();
    setAttributeArgument();
    setAttributeEncoded();
    setAttributeInserted();
    setAttributeMissing();
    setAttributeQualified();
  }

  // Enable formatting for printing (indentation, print children).
  setPrintFormatting();

  // These attributes are dependent on the capture of location information.
  if (getAttributeCoverage() || getAttributeGaps() || getAttributeRegister())
    setAttributeLocation();

  // Location information is only relevant when printing symbols.
  if (!getPrintSymbols()) {
    resetAttributeCoverage();
    resetAttributeGaps();
    resetAttributeLocation();
    resetAttributeRegister();
  }

  // Quick check for printing any element source information.
  if (getAttributeFilename() || getAttributePathname())
    setAttributeAnySource();

  // Quick check for printing any location information.
  if (getAttributeLocation() || getAttributeRange())
    setAttributeAnyLocation();

  if (getAttributeRange() || getPrintAnyLine())
    setGeneralCollectRanges();

  calculateIndentationSize();

  // Print collected command line options.
  LLVM_DEBUG({ dump(); });
}

void LVOptions::calculateIndentationSize() {
#ifndef NDEBUG
  if (getInternalID()) {
    std::string String = hexSquareString(0);
    IndentationSize += String.length();
  }
#endif
  if (getCompareExecute() && (getAttributeAdded() || getAttributeMissing()))
    ++IndentationSize;
  if (getAttributeOffset()) {
    std::string String = hexSquareString(0);
    IndentationSize += String.length();
  }
  if (getAttributeLevel()) {
    std::stringstream Stream;
    Stream.str(std::string());
    Stream << "[" << std::setfill('0') << std::setw(3) << 0 << "]";
    IndentationSize += Stream.tellp();
  }
  if (getAttributeGlobal())
    ++IndentationSize;
}

// Print the current values for all the options, after the dependencies
// has been resolved.
void LVOptions::print(raw_ostream &OS) const {
  // --attribute
  OS << "** Attributes **\n"
     << "All:           " << getAttributeAll() << ", "
     << "Argument:      " << getAttributeArgument() << ", "
     << "Base:          " << getAttributeBase() << ", "
     << "Coverage:      " << getAttributeCoverage() << "\n"
     << "Directories:   " << getAttributeDirectories() << ", "
     << "Discarded:     " << getAttributeDiscarded() << ", "
     << "Discriminator: " << getAttributeDiscriminator() << ", "
     << "Encoded:       " << getAttributeEncoded() << "\n"
     << "Extended:      " << getAttributeExtended() << ", "
     << "Filename:      " << getAttributeFilename() << ", "
     << "Files:         " << getAttributeFiles() << ", "
     << "Format:        " << getAttributeFormat() << "\n"
     << "Gaps:          " << getAttributeGaps() << ", "
     << "Generated:     " << getAttributeGenerated() << ", "
     << "Global:        " << getAttributeGlobal() << ", "
     << "Inserted:      " << getAttributeInserted() << "\n"
     << "Level:         " << getAttributeLevel() << ", "
     << "Linkage:       " << getAttributeLinkage() << ", "
     << "Local:         " << getAttributeLocal() << ", "
     << "Location:      " << getAttributeLocation() << "\n"
     << "Offset:        " << getAttributeOffset() << ", "
     << "Pathname:      " << getAttributePathname() << ", "
     << "Producer:      " << getAttributeProducer() << ", "
     << "Publics:       " << getAttributePublics() << "\n"
     << "Qualified:     " << getAttributeQualified() << ", "
     << "Qualifier:     " << getAttributeQualifier() << ", "
     << "Range:         " << getAttributeRange() << ", "
     << "Reference:     " << getAttributeReference() << "\n"
     << "Register:      " << getAttributeRegister() << ", "
     << "Standard:      " << getAttributeStandard() << ", "
     << "Subrange:      " << getAttributeSubrange() << ", "
     << "System:        " << getAttributeSystem() << "\n"
     << "Typename:      " << getAttributeTypename() << ", "
     << "Underlying:    " << getAttributeUnderlying() << ", "
     << "Zero:          " << getAttributeZero() << "\n";
  OS << "Added:         " << getAttributeAdded() << ", "
     << "AnyLocation:   " << getAttributeAnyLocation() << ", "
     << "AnySource:     " << getAttributeAnySource() << ", "
     << "Missing:       " << getAttributeMissing() << "\n"
     << "\n";

  // --compare
  OS << "** Compare **\n"
     << "All:     " << getCompareAll() << ", "
     << "Lines:   " << getCompareLines() << ", "
     << "Scopes:  " << getCompareScopes() << ", "
     << "Symbols: " << getCompareSymbols() << ", "
     << "Types:   " << getCompareTypes() << "\n";
  OS << "Context: " << getCompareContext() << ", "
     << "Execute: " << getCompareExecute() << ", "
     << "Print:   " << getComparePrint() << "\n"
     << "\n";

  // --print
  OS << "** Print **\n"
     << "All:          " << getPrintAll() << ", "
     << "Elements:     " << getPrintElements() << ", "
     << "Instructions: " << getPrintInstructions() << ", "
     << "Lines:        " << getPrintLines() << "\n"
     << "Scopes:       " << getPrintScopes() << ", "
     << "Sizes:        " << getPrintSizes() << ", "
     << "Summary:      " << getPrintSummary() << ", "
     << "Symbols:      " << getPrintSymbols() << "\n"
     << "Types:        " << getPrintTypes() << ", "
     << "Warnings:     " << getPrintWarnings() << "\n";
  OS << "AnyElemeny:   " << getPrintAnyElement() << ", "
     << "AnyLine:      " << getPrintAnyLine() << ", "
     << "Execute:      " << getPrintExecute() << ", "
     << "Formatting:   " << getPrintFormatting() << "\n"
     << "Offset:       " << getPrintOffset() << ", "
     << "SizesSummary: " << getPrintSizesSummary() << "\n"
     << "\n";

  // --report
  OS << "** Report **\n"
     << "All:      " << getReportAll() << ", "
     << "Children: " << getReportChildren() << ", "
     << "List:     " << getReportList() << ", "
     << "Parents:  " << getReportParents() << ", "
     << "View:     " << getReportView() << "\n";
  OS << "AnyView:  " << getReportAnyView() << ", "
     << "Execute:  " << getReportExecute() << "\n"
     << "\n";

  // --select
  OS << "** Select **\n"
     << "IgnoreCase:     " << getSelectIgnoreCase() << ", "
     << "UseRegex:       " << getSelectUseRegex() << ", "
     << "Execute:        " << getSelectExecute() << ", "
     << "GenericKind:    " << getSelectGenericKind() << "\n"
     << "GenericPattern: " << getSelectGenericPattern() << ", "
     << "OffsetPattern:  " << getSelectOffsetPattern() << "\n"
     << "\n";

  // --warning
  OS << "** Warning **\n"
     << "All:       " << getWarningAll() << ", "
     << "Coverage:  " << getWarningCoverages() << ", "
     << "Lines:     " << getWarningLines() << ", "
     << "Locations: " << getWarningLocations() << ", "
     << "Ranges:    " << getWarningRanges() << "\n"
     << "\n";

  // --internal
  OS << "** Internal **\n"
     << "All:       " << Options.getInternalAll() << ", "
     << "Cmdline:   " << Options.getInternalCmdline() << ", "
     << "ID:        " << Options.getInternalID() << ", "
     << "Integrity: " << Options.getInternalIntegrity() << ", "
     << "None:      " << Options.getInternalNone() << "\n"
     << "Tag:       " << Options.getInternalTag() << "\n"
     << "\n";
}

//===----------------------------------------------------------------------===//
// Logical element selection using patterns.
//===----------------------------------------------------------------------===//
LVPatterns *LVPatterns::getPatterns() {
  static LVPatterns Patterns;
  return &Patterns;
}

Error LVPatterns::createMatchEntry(LVMatchInfo &Filters, StringRef Pattern,
                                   bool IgnoreCase, bool UseRegex) {
  LVMatch Match;
  // Process pattern as regular expression.
  if (UseRegex) {
    Match.Pattern = std::string(Pattern);
    if (Pattern.size()) {
      Match.RE = std::make_shared<Regex>(Pattern, IgnoreCase ? Regex::IgnoreCase
                                                             : Regex::NoFlags);
      std::string Error;
      if (!Match.RE->isValid(Error))
        return createStringError(errc::invalid_argument,
                                 "Error in regular expression: %s",
                                 Error.c_str());

      Match.Mode = LVMatchMode::Regex;
      Filters.push_back(Match);
      return Error::success();
    }
  }

  // Process pattern as an exact string match, depending on the case.
  Match.Pattern = std::string(Pattern);
  if (Match.Pattern.size()) {
    Match.Mode = IgnoreCase ? LVMatchMode::NoCase : LVMatchMode::Match;
    Filters.push_back(Match);
  }

  return Error::success();
}

void LVPatterns::addGenericPatterns(StringSet<> &Patterns) {
  addPatterns(Patterns, GenericMatchInfo);
  if (GenericMatchInfo.size()) {
    options().setSelectGenericPattern();
    options().setSelectExecute();
  }
}

void LVPatterns::addOffsetPatterns(const LVOffsetSet &Patterns) {
  for (const LVOffset &Entry : Patterns)
    OffsetMatchInfo.push_back(Entry);
  if (OffsetMatchInfo.size()) {
    options().setSelectOffsetPattern();
    options().setSelectExecute();
  }
}

void LVPatterns::addPatterns(StringSet<> &Patterns, LVMatchInfo &Filters) {
  bool IgnoreCase = options().getSelectIgnoreCase();
  bool UseRegex = options().getSelectUseRegex();
  for (const StringSet<>::value_type &Entry : Patterns) {
    StringRef Pattern = Entry.first();
    if (Error Err = createMatchEntry(Filters, Pattern, IgnoreCase, UseRegex))
      consumeError(std::move(Err));
  }

  LLVM_DEBUG({
    dbgs() << "\nPattern Information:\n";
    for (LVMatch &Match : Filters)
      dbgs() << "Mode: "
             << (Match.Mode == LVMatchMode::Match ? "Match" : "Regex")
             << " Pattern: '" << Match.Pattern << "'\n";
  });
}

void LVPatterns::addElement(LVElement *Element) {
  // Mark any element that matches a given pattern.
  Element->setIsMatched();
  options().setSelectExecute();
  if (options().getReportList())
    getReaderCompileUnit()->addMatched(Element);
  if (options().getReportAnyView()) {
    getReaderCompileUnit()->addMatched(Element->getIsScope()
                                           ? static_cast<LVScope *>(Element)
                                           : Element->getParentScope());
    // Mark element as matched.
    if (!Element->getIsScope())
      Element->setHasPattern();
  }
}

void LVPatterns::updateReportOptions() {
  if (ElementRequest.size() || LineRequest.size() || ScopeRequest.size() ||
      SymbolRequest.size() || TypeRequest.size()) {
    options().setSelectGenericKind();
    options().setSelectExecute();
  }

  // If we have selected requests and there are no specified report options,
  // assume the 'details' option.
  if (options().getSelectExecute() && !options().getReportExecute()) {
    options().setReportExecute();
    options().setReportList();
  }
}

// Match a general pattern.
bool LVPatterns::matchPattern(StringRef Input, const LVMatchInfo &MatchInfo) {
  bool Matched = false;
  // Do not match an empty 'Input'.
  if (Input.empty())
    return Matched;
  // Traverse all match specifications.
  for (const LVMatch &Match : MatchInfo) {
    switch (Match.Mode) {
    case LVMatchMode::Match:
      Matched = Input == Match.Pattern;
      break;
    case LVMatchMode::NoCase:
      Matched = Input.equals_insensitive(Match.Pattern);
      break;
    case LVMatchMode::Regex:
      Matched = Match.RE->match(Input);
      break;
    default:
      break;
    }
    // Return if we have a match.
    if (Matched)
      return true;
  }
  return Matched;
}

bool LVPatterns::printElement(const LVLine *Line) const {
  return (options().getPrintLines() && Line->getIsLineDebug()) ||
         (options().getPrintInstructions() && Line->getIsLineAssembler());
}

bool LVPatterns::printObject(const LVLocation *Location) const {
  if (options().getAttributeAll())
    return true;
  bool DoPrint = options().getAttributeAnyLocation();
  // Consider the case of filler locations.
  if (DoPrint && Location && Location->getIsGapEntry())
    DoPrint = options().getAttributeGaps();
  return DoPrint;
}

bool LVPatterns::printElement(const LVScope *Scope) const {
  // A scope will be printed depending on the following rules:
  // - Request to print scopes.
  // - Request to print any of its children.
  // - If the scope is Root or CompileUnit:
  //     Request to print summary, sizes or warnings.
  return options().getPrintScopes() ||
         (options().getPrintSymbols() && Scope->getHasSymbols()) ||
         (options().getPrintAnyLine() && Scope->getHasLines()) ||
         (options().getPrintTypes() && Scope->getHasTypes()) ||
         ((options().getPrintSizesSummary() || options().getPrintWarnings()) &&
          (Scope->getIsRoot() || Scope->getIsCompileUnit()));
}

bool LVPatterns::printElement(const LVSymbol *Symbol) const {
  // Print compiler generated symbols only if command line option.
  if (Symbol->getIsArtificial())
    return options().getAttributeGenerated() && options().getPrintSymbols();
  return options().getPrintSymbols();
}

bool LVPatterns::printElement(const LVType *Type) const {
  // Print array subranges only if print types is requested.
  if (Type->getIsSubrange())
    return options().getAttributeSubrange() && options().getPrintTypes();
  return options().getPrintTypes();
}

void LVPatterns::print(raw_ostream &OS) const {
  OS << "LVPatterns\n";
  LLVM_DEBUG(dbgs() << "Print Patterns\n");
}
