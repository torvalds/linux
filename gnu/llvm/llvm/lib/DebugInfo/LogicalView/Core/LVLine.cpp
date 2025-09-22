//===-- LVLine.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVLine class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVLine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVCompare.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Line"

namespace {
const char *const KindBasicBlock = "BasicBlock";
const char *const KindDiscriminator = "Discriminator";
const char *const KindEndSequence = "EndSequence";
const char *const KindEpilogueBegin = "EpilogueBegin";
const char *const KindLineDebug = "Line";
const char *const KindLineSource = "Code";
const char *const KindNewStatement = "NewStatement";
const char *const KindPrologueEnd = "PrologueEnd";
const char *const KindUndefined = "Undefined";
const char *const KindAlwaysStepInto = "AlwaysStepInto"; // CodeView
const char *const KindNeverStepInto = "NeverStepInto";   // CodeView
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Logical line.
//===----------------------------------------------------------------------===//
// Return a string representation for the line kind.
const char *LVLine::kind() const {
  const char *Kind = KindUndefined;
  if (getIsLineDebug())
    Kind = KindLineDebug;
  else if (getIsLineAssembler())
    Kind = KindLineSource;
  return Kind;
}

LVLineDispatch LVLine::Dispatch = {
    {LVLineKind::IsBasicBlock, &LVLine::getIsBasicBlock},
    {LVLineKind::IsDiscriminator, &LVLine::getIsDiscriminator},
    {LVLineKind::IsEndSequence, &LVLine::getIsEndSequence},
    {LVLineKind::IsLineDebug, &LVLine::getIsLineDebug},
    {LVLineKind::IsLineAssembler, &LVLine::getIsLineAssembler},
    {LVLineKind::IsNewStatement, &LVLine::getIsNewStatement},
    {LVLineKind::IsEpilogueBegin, &LVLine::getIsEpilogueBegin},
    {LVLineKind::IsPrologueEnd, &LVLine::getIsPrologueEnd},
    {LVLineKind::IsAlwaysStepInto, &LVLine::getIsAlwaysStepInto},
    {LVLineKind::IsNeverStepInto, &LVLine::getIsNeverStepInto}};

// String used as padding for printing elements with no line number.
std::string LVLine::noLineAsString(bool ShowZero) const {
  if (options().getInternalNone())
    return LVObject::noLineAsString(ShowZero);
  return (ShowZero || options().getAttributeZero()) ? ("    0   ")
                                                    : ("    -   ");
}

void LVLine::markMissingParents(const LVLines *References,
                                const LVLines *Targets) {
  if (!(References && Targets))
    return;

  LLVM_DEBUG({
    dbgs() << "\n[LVLine::markMissingParents]\n";
    for (const LVLine *Reference : *References)
      dbgs() << "References: "
             << "Kind = " << formattedKind(Reference->kind()) << ", "
             << "Line = " << Reference->getLineNumber() << "\n";
    for (const LVLine *Target : *Targets)
      dbgs() << "Targets   : "
             << "Kind = " << formattedKind(Target->kind()) << ", "
             << "Line = " << Target->getLineNumber() << "\n";
  });

  for (LVLine *Reference : *References) {
    LLVM_DEBUG({
      dbgs() << "Search Reference: Line = " << Reference->getLineNumber()
             << "\n";
    });
    if (!Reference->findIn(Targets))
      Reference->markBranchAsMissing();
  }
}

LVLine *LVLine::findIn(const LVLines *Targets) const {
  if (!Targets)
    return nullptr;

  LLVM_DEBUG({
    dbgs() << "\n[LVLine::findIn]\n"
           << "Reference: "
           << "Level = " << getLevel() << ", "
           << "Kind = " << formattedKind(kind()) << ", "
           << "Line = " << getLineNumber() << "\n";
    for (const LVLine *Target : *Targets)
      dbgs() << "Target   : "
             << "Level = " << Target->getLevel() << ", "
             << "Kind = " << formattedKind(Target->kind()) << ", "
             << "Line = " << Target->getLineNumber() << "\n";
  });

  for (LVLine *Line : *Targets)
    if (equals(Line))
      return Line;

  return nullptr;
}

bool LVLine::equals(const LVLine *Line) const {
  return LVElement::equals(Line);
}

bool LVLine::equals(const LVLines *References, const LVLines *Targets) {
  if (!References && !Targets)
    return true;
  if (References && Targets && References->size() == Targets->size()) {
    for (const LVLine *Reference : *References)
      if (!Reference->findIn(Targets))
        return false;
    return true;
  }
  return false;
}

void LVLine::report(LVComparePass Pass) {
  getComparator().printItem(this, Pass);
}

void LVLine::print(raw_ostream &OS, bool Full) const {
  if (getReader().doPrintLine(this)) {
    getReaderCompileUnit()->incrementPrintedLines();
    LVElement::print(OS, Full);
    printExtra(OS, Full);
  }
}

//===----------------------------------------------------------------------===//
// DWARF line record.
//===----------------------------------------------------------------------===//
std::string LVLineDebug::statesInfo(bool Formatted) const {
  // Returns the DWARF extra qualifiers.
  std::string String;
  raw_string_ostream Stream(String);

  std::string Separator = Formatted ? " " : "";
  if (getIsNewStatement()) {
    Stream << Separator << "{" << KindNewStatement << "}";
    Separator = " ";
  }
  if (getIsDiscriminator()) {
    Stream << Separator << "{" << KindDiscriminator << "}";
    Separator = " ";
  }
  if (getIsBasicBlock()) {
    Stream << Separator << "{" << KindBasicBlock << "}";
    Separator = " ";
  }
  if (getIsEndSequence()) {
    Stream << Separator << "{" << KindEndSequence << "}";
    Separator = " ";
  }
  if (getIsEpilogueBegin()) {
    Stream << Separator << "{" << KindEpilogueBegin << "}";
    Separator = " ";
  }
  if (getIsPrologueEnd()) {
    Stream << Separator << "{" << KindPrologueEnd << "}";
    Separator = " ";
  }
  if (getIsAlwaysStepInto()) {
    Stream << Separator << "{" << KindAlwaysStepInto << "}";
    Separator = " ";
  }
  if (getIsNeverStepInto()) {
    Stream << Separator << "{" << KindNeverStepInto << "}";
    Separator = " ";
  }

  return String;
}

bool LVLineDebug::equals(const LVLine *Line) const {
  if (!LVLine::equals(Line))
    return false;
  return getFilenameIndex() == Line->getFilenameIndex();
}

void LVLineDebug::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind());

  if (options().getAttributeQualifier()) {
    // The qualifier includes the states information and the source filename
    // that contains the line element.
    OS << statesInfo(/*Formatted=*/true);
    OS << " " << formattedName(getPathname());
  }
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// Assembler line extracted from the ELF .text section.
//===----------------------------------------------------------------------===//
bool LVLineAssembler::equals(const LVLine *Line) const {
  return LVLine::equals(Line);
}

void LVLineAssembler::printExtra(raw_ostream &OS, bool Full) const {
  OS << formattedKind(kind());
  OS << " " << formattedName(getName());
  OS << "\n";
}
