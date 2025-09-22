//===-- LVReader.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVReader class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include <tuple>

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Reader"

// Detect elements that are inserted more than once at different scopes,
// causing a crash on the reader destruction, as the element is already
// deleted from other scope. Helper for CodeView reader.
bool checkIntegrityScopesTree(LVScope *Root) {
  using LVDuplicateEntry = std::tuple<LVElement *, LVScope *, LVScope *>;
  using LVDuplicate = std::vector<LVDuplicateEntry>;
  LVDuplicate Duplicate;

  using LVIntegrity = std::map<LVElement *, LVScope *>;
  LVIntegrity Integrity;

  // Add the given element to the integrity map.
  auto AddElement = [&](LVElement *Element, LVScope *Scope) {
    LVIntegrity::iterator Iter = Integrity.find(Element);
    if (Iter == Integrity.end())
      Integrity.emplace(Element, Scope);
    else
      // We found a duplicate.
      Duplicate.emplace_back(Element, Scope, Iter->second);
  };

  // Recursively add all the elements in the scope.
  std::function<void(LVScope * Parent)> TraverseScope = [&](LVScope *Parent) {
    auto Traverse = [&](const auto *Set) {
      if (Set)
        for (const auto &Entry : *Set)
          AddElement(Entry, Parent);
    };
    if (const LVScopes *Scopes = Parent->getScopes()) {
      for (LVScope *Scope : *Scopes) {
        AddElement(Scope, Parent);
        TraverseScope(Scope);
      }
    }
    Traverse(Parent->getSymbols());
    Traverse(Parent->getTypes());
    Traverse(Parent->getLines());
  };

  // Start traversing the scopes root and print any duplicates.
  TraverseScope(Root);
  bool PassIntegrity = true;
  if (Duplicate.size()) {
    std::stable_sort(begin(Duplicate), end(Duplicate),
                     [](const auto &l, const auto &r) {
                       return std::get<0>(l)->getID() < std::get<0>(r)->getID();
                     });

    auto PrintIndex = [](unsigned Index) {
      if (Index)
        dbgs() << format("%8d: ", Index);
      else
        dbgs() << format("%8c: ", ' ');
    };
    auto PrintElement = [&](LVElement *Element, unsigned Index = 0) {
      PrintIndex(Index);
      std::string ElementName(Element->getName());
      dbgs() << format("%15s ID=0x%08x '%s'\n", Element->kind(),
                       Element->getID(), ElementName.c_str());
    };

    std::string RootName(Root->getName());
    dbgs() << formatv("{0}\n", fmt_repeat('=', 72));
    dbgs() << format("Root: '%s'\nDuplicated elements: %d\n", RootName.c_str(),
                     Duplicate.size());
    dbgs() << formatv("{0}\n", fmt_repeat('=', 72));

    unsigned Index = 0;
    for (const LVDuplicateEntry &Entry : Duplicate) {
      LVElement *Element;
      LVScope *First;
      LVScope *Second;
      std::tie(Element, First, Second) = Entry;
      dbgs() << formatv("\n{0}\n", fmt_repeat('-', 72));
      PrintElement(Element, ++Index);
      PrintElement(First);
      PrintElement(Second);
      dbgs() << formatv("{0}\n", fmt_repeat('-', 72));
    }
    PassIntegrity = false;
  }
  return PassIntegrity;
}

//===----------------------------------------------------------------------===//
// Class to represent a split context.
//===----------------------------------------------------------------------===//
Error LVSplitContext::createSplitFolder(StringRef Where) {
  // The 'location' will represent the root directory for the output created
  // by the context. It will contain the different CUs files, that will be
  // extracted from a single ELF.
  Location = std::string(Where);

  // Add a trailing slash, if there is none.
  size_t Pos = Location.find_last_of('/');
  if (Location.length() != Pos + 1)
    Location.append("/");

  // Make sure the new directory exists, creating it if necessary.
  if (std::error_code EC = llvm::sys::fs::create_directories(Location))
    return createStringError(EC, "Error: could not create directory %s",
                             Location.c_str());

  return Error::success();
}

std::error_code LVSplitContext::open(std::string ContextName,
                                     std::string Extension, raw_ostream &OS) {
  assert(OutputFile == nullptr && "OutputFile already set.");

  // Transforms '/', '\', '.', ':' into '_'.
  std::string Name(flattenedFilePath(ContextName));
  Name.append(Extension);
  // Add the split context location folder name.
  if (!Location.empty())
    Name.insert(0, Location);

  std::error_code EC;
  OutputFile = std::make_unique<ToolOutputFile>(Name, EC, sys::fs::OF_None);
  if (EC)
    return EC;

  // Don't remove output file.
  OutputFile->keep();
  return std::error_code();
}

LVReader *CurrentReader = nullptr;
LVReader &LVReader::getInstance() {
  if (CurrentReader)
    return *CurrentReader;
  outs() << "Invalid instance reader.\n";
  llvm_unreachable("Invalid instance reader.");
}
void LVReader::setInstance(LVReader *Reader) { CurrentReader = Reader; }

Error LVReader::createSplitFolder() {
  if (OutputSplit) {
    // If the '--output=split' was specified, but no '--split-folder'
    // option, use the input file as base for the split location.
    if (options().getOutputFolder().empty())
      options().setOutputFolder(getFilename().str() + "_cus");

    SmallString<128> SplitFolder;
    SplitFolder = options().getOutputFolder();
    sys::fs::make_absolute(SplitFolder);

    // Return error if unable to create a split context location.
    if (Error Err = SplitContext.createSplitFolder(SplitFolder))
      return Err;

    OS << "\nSplit View Location: '" << SplitContext.getLocation() << "'\n";
  }

  return Error::success();
}

// Get the filename for given object.
StringRef LVReader::getFilename(LVObject *Object, size_t Index) const {
  // TODO: The current CodeView Reader implementation does not have support
  // for multiple compile units. Until we have a proper offset calculation,
  // check only in the current compile unit.
  if (CompileUnits.size()) {
    // Get Compile Unit for the given object.
    LVCompileUnits::const_iterator Iter =
        std::prev(CompileUnits.lower_bound(Object->getOffset()));
    if (Iter != CompileUnits.end())
      return Iter->second->getFilename(Index);
  }

  return CompileUnit ? CompileUnit->getFilename(Index) : StringRef();
}

// The Reader is the module that creates the logical view using the debug
// information contained in the binary file specified in the command line.
// This is the main entry point for the Reader and performs the following
// steps:
// - Process any patterns collected from the '--select' options.
// - For each compile unit in the debug information:
//   * Create the logical elements (scopes, symbols, types, lines).
//   * Collect debug ranges and debug locations.
//   * Move the collected logical lines to their associated scopes.
// - Once all the compile units have been processed, traverse the scopes
//   tree in order to:
//   * Calculate symbol coverage.
//   * Detect invalid ranges and locations.
//   * "resolve" the logical elements. During this pass, the names and
//      file information are updated, to reflect any dependency with other
//     logical elements.
Error LVReader::doLoad() {
  // Set current Reader instance.
  setInstance(this);

  // Before any scopes creation, process any pattern specified by the
  // --select and --select-offsets options.
  patterns().addGenericPatterns(options().Select.Generic);
  patterns().addOffsetPatterns(options().Select.Offsets);

  // Add any specific element printing requests based on the element kind.
  patterns().addRequest(options().Select.Elements);
  patterns().addRequest(options().Select.Lines);
  patterns().addRequest(options().Select.Scopes);
  patterns().addRequest(options().Select.Symbols);
  patterns().addRequest(options().Select.Types);

  // Once we have processed the requests for any particular kind of elements,
  // we need to update the report options, in order to have a default value.
  patterns().updateReportOptions();

  // Delegate the scope tree creation to the specific reader.
  if (Error Err = createScopes())
    return Err;

  if (options().getInternalIntegrity() && !checkIntegrityScopesTree(Root))
    return llvm::make_error<StringError>("Duplicated elements in Scopes Tree",
                                         inconvertibleErrorCode());

  // Calculate symbol coverage and detect invalid debug locations and ranges.
  Root->processRangeInformation();

  // As the elements can depend on elements from a different compile unit,
  // information such as name and file/line source information needs to be
  // updated.
  Root->resolveElements();

  sortScopes();
  return Error::success();
}

// Default handler for a generic reader.
Error LVReader::doPrint() {
  // Set current Reader instance.
  setInstance(this);

  // Check for any '--report' request.
  if (options().getReportExecute()) {
    // Requested details.
    if (options().getReportList())
      if (Error Err = printMatchedElements(/*UseMatchedElements=*/true))
        return Err;
    // Requested only children.
    if (options().getReportChildren() && !options().getReportParents())
      if (Error Err = printMatchedElements(/*UseMatchedElements=*/false))
        return Err;
    // Requested (parents) or (parents and children).
    if (options().getReportParents() || options().getReportView())
      if (Error Err = printScopes())
        return Err;

    return Error::success();
  }

  return printScopes();
}

Error LVReader::printScopes() {
  if (bool DoPrint =
          (options().getPrintExecute() || options().getComparePrint())) {
    if (Error Err = createSplitFolder())
      return Err;

    // Start printing from the root.
    bool DoMatch = options().getSelectGenericPattern() ||
                   options().getSelectGenericKind() ||
                   options().getSelectOffsetPattern();
    return Root->doPrint(OutputSplit, DoMatch, DoPrint, OS);
  }

  return Error::success();
}

Error LVReader::printMatchedElements(bool UseMatchedElements) {
  if (Error Err = createSplitFolder())
    return Err;

  return Root->doPrintMatches(OutputSplit, OS, UseMatchedElements);
}

void LVReader::print(raw_ostream &OS) const {
  OS << "LVReader\n";
  LLVM_DEBUG(dbgs() << "PrintReader\n");
}
