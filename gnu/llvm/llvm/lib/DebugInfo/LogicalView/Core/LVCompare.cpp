//===-- LVCompare.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVCompare class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVCompare.h"
#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include <tuple>

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Compare"

namespace {

enum class LVCompareItem { Scope, Symbol, Type, Line, Total };
enum class LVCompareIndex { Header, Expected, Missing, Added };
using LVCompareEntry = std::tuple<const char *, unsigned, unsigned, unsigned>;
using LVCompareInfo = std::map<LVCompareItem, LVCompareEntry>;
LVCompareInfo Results = {
    {LVCompareItem::Line, LVCompareEntry("Lines", 0, 0, 0)},
    {LVCompareItem::Scope, LVCompareEntry("Scopes", 0, 0, 0)},
    {LVCompareItem::Symbol, LVCompareEntry("Symbols", 0, 0, 0)},
    {LVCompareItem::Type, LVCompareEntry("Types", 0, 0, 0)},
    {LVCompareItem::Total, LVCompareEntry("Total", 0, 0, 0)}};
static LVCompareInfo::iterator IterTotal = Results.end();

constexpr unsigned getHeader() {
  return static_cast<unsigned>(LVCompareIndex::Header);
}
constexpr unsigned getExpected() {
  return static_cast<unsigned>(LVCompareIndex::Expected);
}
constexpr unsigned getMissing() {
  return static_cast<unsigned>(LVCompareIndex::Missing);
}
constexpr unsigned getAdded() {
  return static_cast<unsigned>(LVCompareIndex::Added);
}

LVCompare *CurrentComparator = nullptr;

void zeroResults() {
  // In case the same reader instance is used.
  for (LVCompareInfo::reference Entry : Results) {
    std::get<getExpected()>(Entry.second) = 0;
    std::get<getMissing()>(Entry.second) = 0;
    std::get<getAdded()>(Entry.second) = 0;
  }
  IterTotal = Results.find(LVCompareItem::Total);
  assert(IterTotal != Results.end());
}

LVCompareInfo::iterator getResultsEntry(LVElement *Element) {
  LVCompareItem Kind;
  if (Element->getIsLine())
    Kind = LVCompareItem::Line;
  else if (Element->getIsScope())
    Kind = LVCompareItem::Scope;
  else if (Element->getIsSymbol())
    Kind = LVCompareItem::Symbol;
  else
    Kind = LVCompareItem::Type;

  // Used to update the expected, missing or added entry for the given kind.
  LVCompareInfo::iterator Iter = Results.find(Kind);
  assert(Iter != Results.end());
  return Iter;
}

void updateExpected(LVElement *Element) {
  LVCompareInfo::iterator Iter = getResultsEntry(Element);
  // Update total for expected.
  ++std::get<getExpected()>(IterTotal->second);
  // Update total for specific element kind.
  ++std::get<getExpected()>(Iter->second);
}

void updateMissingOrAdded(LVElement *Element, LVComparePass Pass) {
  LVCompareInfo::iterator Iter = getResultsEntry(Element);
  if (Pass == LVComparePass::Missing) {
    ++std::get<getMissing()>(IterTotal->second);
    ++std::get<getMissing()>(Iter->second);
  } else {
    ++std::get<getAdded()>(IterTotal->second);
    ++std::get<getAdded()>(Iter->second);
  }
}

} // namespace

LVCompare &LVCompare::getInstance() {
  static LVCompare DefaultComparator(outs());
  return CurrentComparator ? *CurrentComparator : DefaultComparator;
}

void LVCompare::setInstance(LVCompare *Comparator) {
  CurrentComparator = Comparator;
}

LVCompare::LVCompare(raw_ostream &OS) : OS(OS) {
  PrintLines = options().getPrintLines();
  PrintSymbols = options().getPrintSymbols();
  PrintTypes = options().getPrintTypes();
  PrintScopes =
      options().getPrintScopes() || PrintLines || PrintSymbols || PrintTypes;
}

Error LVCompare::execute(LVReader *ReferenceReader, LVReader *TargetReader) {
  setInstance(this);
  // In the case of added elements, the 'Reference' reader will be modified;
  // those elements will be added to it. Update the current reader instance.
  LVReader::setInstance(ReferenceReader);

  auto PrintHeader = [this](LVScopeRoot *LHS, LVScopeRoot *RHS) {
    LLVM_DEBUG({
      dbgs() << "[Reference] " << LHS->getName() << "\n"
             << "[Target] " << RHS->getName() << "\n";
    });
    OS << "\nReference: " << formattedName(LHS->getName()) << "\n"
       << "Target:    " << formattedName(RHS->getName()) << "\n";
  };

  // We traverse the given scopes tree ('Reference' and 'Target') twice.
  // The first time we look for missing items from the 'Reference' and the
  // second time we look for items added to the 'Target'.
  // The comparison test includes the name, lexical level, type, source
  // location, etc.
  LVScopeRoot *ReferenceRoot = ReferenceReader->getScopesRoot();
  LVScopeRoot *TargetRoot = TargetReader->getScopesRoot();
  ReferenceRoot->setIsInCompare();
  TargetRoot->setIsInCompare();

  // Reset possible previous results.
  zeroResults();

  if (options().getCompareContext()) {
    // Perform a logical view comparison as a whole unit. We start at the
    // root reference; at each scope an equal test is applied to its children.
    // If a difference is found, the current path is marked as missing.
    auto CompareViews = [this](LVScopeRoot *LHS, LVScopeRoot *RHS) -> Error {
      LHS->markMissingParents(RHS, /*TraverseChildren=*/true);
      if (LHS->getIsMissingLink() && options().getReportAnyView()) {
        // As we are printing a missing tree, enable formatting.
        options().setPrintFormatting();
        OS << "\nMissing Tree:\n";
        if (Error Err = LHS->doPrint(/*Split=*/false, /*Match=*/false,
                                     /*Print=*/true, OS))
          return Err;
        options().resetPrintFormatting();
      }

      return Error::success();
    };

    // If the user has requested printing details for the comparison, we
    // disable the indentation and the added/missing tags ('+'/'-'), as the
    // details are just a list of elements.
    options().resetPrintFormatting();

    PrintHeader(ReferenceRoot, TargetRoot);
    Reader = ReferenceReader;
    if (Error Err = CompareViews(ReferenceRoot, TargetRoot))
      return Err;
    FirstMissing = true;
    ReferenceRoot->report(LVComparePass::Missing);

    PrintHeader(TargetRoot, ReferenceRoot);
    Reader = TargetReader;
    if (Error Err = CompareViews(TargetRoot, ReferenceRoot))
      return Err;
    FirstMissing = true;
    TargetRoot->report(LVComparePass::Added);

    options().setPrintFormatting();

    // Display a summary with the elements missing and/or added.
    printSummary();
  } else {
    // Perform logical elements comparison. An equal test is apply to each
    // element. If a difference is found, the reference element is marked as
    // 'missing'.
    // The final comparison result will show the 'Reference' scopes tree,
    // having both missing and added elements.
    using LVScopeLink = std::map<LVScope *, LVScope *>;
    LVScopeLink ScopeLinks;
    auto CompareReaders = [&](LVReader *LHS, LVReader *RHS, LVElements &Set,
                              LVComparePass Pass) -> Error {
      auto FindMatch = [&](auto &References, auto &Targets,
                           const char *Category) -> Error {
        LVElements Elements;
        for (LVElement *Reference : References) {
          // Report elements that can be printed; ignore logical elements that
          // have qualifiers.
          if (Reference->getIncludeInPrint()) {
            if (Pass == LVComparePass::Missing)
              updateExpected(Reference);
            Reference->setIsInCompare();
            LVElement *CurrentTarget = nullptr;
            if (llvm::any_of(Targets, [&](auto Target) -> bool {
                  CurrentTarget = Target;
                  return Reference->equals(Target);
                })) {
              if (Pass == LVComparePass::Missing && Reference->getIsScope()) {
                // If the elements being compared are scopes and are a match,
                // they are recorded, to be used when creating the augmented
                // tree, as insertion points for the "added" items.
                ScopeLinks.emplace(static_cast<LVScope *>(CurrentTarget),
                                   static_cast<LVScope *>(Reference));
              }
            } else {
              // Element is missing or added.
              Pass == LVComparePass::Missing ? Reference->setIsMissing()
                                             : Reference->setIsAdded();
              Elements.push_back(Reference);
              updateMissingOrAdded(Reference, Pass);
              // Record missing/added element.
              addPassEntry(Reader, Reference, Pass);
            }
          }
        }
        if (Pass == LVComparePass::Added)
          // Record all the current missing elements for this category.
          Set.insert(Set.end(), Elements.begin(), Elements.end());
        if (options().getReportList()) {
          if (Elements.size()) {
            OS << "\n(" << Elements.size() << ") "
               << (Pass == LVComparePass::Missing ? "Missing" : "Added") << " "
               << Category << ":\n";
            for (const LVElement *Element : Elements) {
              if (Error Err = Element->doPrint(/*Split=*/false, /*Match=*/false,
                                               /*Print=*/true, OS))
                return Err;
            }
          }
        }

        return Error::success();
      };

      // First compare the scopes, so they will be inserted at the front of
      // the missing elements list. When they are moved, their children are
      // moved as well and no additional work is required.
      if (options().getCompareScopes())
        if (Error Err = FindMatch(LHS->getScopes(), RHS->getScopes(), "Scopes"))
          return Err;
      if (options().getCompareSymbols())
        if (Error Err =
                FindMatch(LHS->getSymbols(), RHS->getSymbols(), "Symbols"))
          return Err;
      if (options().getCompareTypes())
        if (Error Err = FindMatch(LHS->getTypes(), RHS->getTypes(), "Types"))
          return Err;
      if (options().getCompareLines())
        if (Error Err = FindMatch(LHS->getLines(), RHS->getLines(), "Lines"))
          return Err;

      return Error::success();
    };

    // If the user has requested printing details for the comparison, we
    // disable the indentation and the added/missing tags ('+'/'-'), as the
    // details are just a list of elements.
    options().resetPrintFormatting();

    PrintHeader(ReferenceRoot, TargetRoot);
    // Include the root in the expected count.
    updateExpected(ReferenceRoot);

    LVElements ElementsToAdd;
    Reader = ReferenceReader;
    if (Error Err = CompareReaders(ReferenceReader, TargetReader, ElementsToAdd,
                                   LVComparePass::Missing))
      return Err;
    Reader = TargetReader;
    if (Error Err = CompareReaders(TargetReader, ReferenceReader, ElementsToAdd,
                                   LVComparePass::Added))
      return Err;

    LLVM_DEBUG({
      dbgs() << "\nReference/Target Scope links:\n";
      for (LVScopeLink::const_reference Entry : ScopeLinks)
        dbgs() << "Source: " << hexSquareString(Entry.first->getOffset()) << " "
               << "Destination: " << hexSquareString(Entry.second->getOffset())
               << "\n";
      dbgs() << "\n";
    });

    // Add the 'missing' elements from the 'Target' into the 'Reference'.
    // First insert the missing scopes, as they include any missing children.
    LVScope *Parent = nullptr;
    for (LVElement *Element : ElementsToAdd) {
      LLVM_DEBUG({
        dbgs() << "Element to Insert: " << hexSquareString(Element->getOffset())
               << ", Parent: "
               << hexSquareString(Element->getParentScope()->getOffset())
               << "\n";
      });
      // Skip already inserted elements. They were inserted, if their parents
      // were missing. When inserting them, all the children are moved.
      if (Element->getHasMoved())
        continue;

      // We need to find an insertion point in the reference scopes tree.
      Parent = Element->getParentScope();
      if (ScopeLinks.find(Parent) != ScopeLinks.end()) {
        LVScope *InsertionPoint = ScopeLinks[Parent];
        LLVM_DEBUG({
          dbgs() << "Inserted at: "
                 << hexSquareString(InsertionPoint->getOffset()) << "\n";
        });
        if (Parent->removeElement(Element)) {
          // Be sure we have a current compile unit.
          getReader().setCompileUnit(InsertionPoint->getCompileUnitParent());
          InsertionPoint->addElement(Element);
          Element->updateLevel(InsertionPoint, /*Moved=*/true);
        }
      }
    }

    options().setPrintFormatting();

    // Display the augmented reference scopes tree.
    if (options().getReportAnyView())
      if (Error Err = ReferenceReader->doPrint())
        return Err;

    LLVM_DEBUG({
      dbgs() << "\nModified Reference Reader";
      if (Error Err = ReferenceReader->doPrint())
        return Err;
      dbgs() << "\nModified Target Reader";
      if (Error Err = TargetReader->doPrint())
        return Err;
    });

    // Display a summary with the elements missing and/or added.
    printSummary();
  }

  return Error::success();
}

void LVCompare::printCurrentStack() {
  for (const LVScope *Scope : ScopeStack) {
    Scope->printAttributes(OS);
    OS << Scope->lineNumberAsString(/*ShowZero=*/true) << " " << Scope->kind()
       << " " << formattedName(Scope->getName()) << "\n";
  }
}

void LVCompare::printItem(LVElement *Element, LVComparePass Pass) {
  // Record expected, missing, added.
  updateExpected(Element);
  updateMissingOrAdded(Element, Pass);

  // Record missing/added element.
  if (Element->getIsMissing())
    addPassEntry(Reader, Element, Pass);

  if ((!PrintLines && Element->getIsLine()) ||
      (!PrintScopes && Element->getIsScope()) ||
      (!PrintSymbols && Element->getIsSymbol()) ||
      (!PrintTypes && Element->getIsType()))
    return;

  if (Element->getIsMissing()) {
    if (FirstMissing) {
      OS << "\n";
      FirstMissing = false;
    }

    StringRef Kind = Element->kind();
    StringRef Name =
        Element->getIsLine() ? Element->getPathname() : Element->getName();
    StringRef Status = (Pass == LVComparePass::Missing) ? "Missing" : "Added";
    OS << Status << " " << Kind << " '" << Name << "'";
    if (Element->getLineNumber() > 0)
      OS << " at line " << Element->getLineNumber();
    OS << "\n";

    if (options().getReportList()) {
      printCurrentStack();
      Element->printAttributes(OS);
      OS << Element->lineNumberAsString(/*ShowZero=*/true) << " " << Kind << " "
         << Name << "\n";
    }
  }
}

void LVCompare::printSummary() const {
  if (!options().getPrintSummary())
    return;
  std::string Separator = std::string(40, '-');
  auto PrintSeparator = [&]() { OS << Separator << "\n"; };
  auto PrintHeadingRow = [&](const char *T, const char *U, const char *V,
                             const char *W) {
    OS << format("%-9s%9s  %9s  %9s\n", T, U, V, W);
  };
  auto PrintDataRow = [&](const char *T, unsigned U, unsigned V, unsigned W) {
    OS << format("%-9s%9d  %9d  %9d\n", T, U, V, W);
  };

  OS << "\n";
  PrintSeparator();
  PrintHeadingRow("Element", "Expected", "Missing", "Added");
  PrintSeparator();
  for (LVCompareInfo::reference Entry : Results) {
    if (Entry.first == LVCompareItem::Total)
      PrintSeparator();
    PrintDataRow(std::get<getHeader()>(Entry.second),
                 std::get<getExpected()>(Entry.second),
                 std::get<getMissing()>(Entry.second),
                 std::get<getAdded()>(Entry.second));
  }
}

void LVCompare::print(raw_ostream &OS) const { OS << "LVCompare\n"; }
