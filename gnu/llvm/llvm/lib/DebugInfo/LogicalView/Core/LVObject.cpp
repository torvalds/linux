//===-- LVObject.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVObject class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Core/LVObject.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include <iomanip>

using namespace llvm;
using namespace llvm::logicalview;

#define DEBUG_TYPE "Object"

#ifndef NDEBUG
uint64_t LVObject::GID = 0;
#endif

StringRef llvm::logicalview::typeNone() { return StringRef(); }
StringRef llvm::logicalview::typeVoid() { return "void"; }
StringRef llvm::logicalview::typeInt() { return "int"; }
StringRef llvm::logicalview::typeUnknown() { return "?"; }
StringRef llvm::logicalview::emptyString() { return StringRef(); }

// Get a string representing the indentation level.
std::string LVObject::indentAsString(LVLevel Level) const {
  return std::string(Level * 2, ' ');
}

// Get a string representing the indentation level.
std::string LVObject::indentAsString() const {
  return (options().getPrintFormatting() || options().getPrintOffset())
             ? indentAsString(ScopeLevel)
             : "";
}

// String used as padding for printing objects with no line number.
std::string LVObject::noLineAsString(bool ShowZero) const {
  return std::string(8, ' ');
}

// Get a string representation for the given number and discriminator.
std::string LVObject::lineAsString(uint32_t LineNumber, LVHalf Discriminator,
                                   bool ShowZero) const {
  // The representation is formatted as:
  // a) line number (xxxxx) and discriminator (yy): 'xxxxx,yy'
  // b) Only line number (xxxxx):                   'xxxxx   '
  // c) No line number:                             '        '
  std::stringstream Stream;
  if (LineNumber) {
    if (Discriminator && options().getAttributeDiscriminator())
      Stream << std::setw(5) << LineNumber << "," << std::left << std::setw(2)
             << Discriminator;
    else
      Stream << std::setw(5) << LineNumber << "   ";
  } else
    Stream << noLineAsString(ShowZero);

  if (options().getInternalNone())
    Stream.str(noLineAsString(ShowZero));

  return Stream.str();
}

// Same as 'LineString' but with stripped whitespaces.
std::string LVObject::lineNumberAsStringStripped(bool ShowZero) const {
  return std::string(StringRef(lineNumberAsString(ShowZero)).trim());
}

std::string LVObject::referenceAsString(uint32_t LineNumber,
                                        bool Spaces) const {
  std::string String;
  raw_string_ostream Stream(String);
  if (LineNumber)
    Stream << "@" << LineNumber << (Spaces ? " " : "");

  return String;
}

void LVObject::setParent(LVScope *Scope) {
  Parent.Scope = Scope;
  setLevel(Scope->getLevel() + 1);
}
void LVObject::setParent(LVSymbol *Symbol) {
  Parent.Symbol = Symbol;
  setLevel(Symbol->getLevel() + 1);
}

void LVObject::markBranchAsMissing() {
  // Mark the current object as 'missing'; then traverse the parents chain
  // marking them as 'special missing' to indicate a missing branch. They
  // can not be marked as missing, because will generate incorrect reports.
  LVObject *Parent = this;
  Parent->setIsMissing();
  while (Parent) {
    Parent->setIsMissingLink();
    Parent = Parent->getParent();
  }
}

Error LVObject::doPrint(bool Split, bool Match, bool Print, raw_ostream &OS,
                        bool Full) const {
  print(OS, Full);
  return Error::success();
}

void LVObject::printAttributes(raw_ostream &OS, bool Full, StringRef Name,
                               LVObject *Parent, StringRef Value,
                               bool UseQuotes, bool PrintRef) const {
  // The current object will be the enclosing scope, use its offset and level.
  LVObject Object(*Parent);
  Object.setLevel(Parent->getLevel() + 1);
  Object.setLineNumber(0);
  Object.printAttributes(OS, Full);

  // Print the line.
  std::string TheLineNumber(Object.lineNumberAsString());
  std::string TheIndentation(Object.indentAsString());
  OS << format(" %5s %s ", TheLineNumber.c_str(), TheIndentation.c_str());

  OS << Name;
  if (PrintRef && options().getAttributeOffset())
    OS << hexSquareString(getOffset());
  if (UseQuotes)
    OS << formattedName(Value) << "\n";
  else
    OS << Value << "\n";
}

void LVObject::printAttributes(raw_ostream &OS, bool Full) const {
#ifndef NDEBUG
  if (options().getInternalID())
    OS << hexSquareString(getID());
#endif
  if (options().getCompareExecute() &&
      (options().getAttributeAdded() || options().getAttributeMissing()))
    OS << (getIsAdded() ? '+' : getIsMissing() ? '-' : ' ');
  if (options().getAttributeOffset())
    OS << hexSquareString(getOffset());
  if (options().getAttributeLevel()) {
    std::stringstream Stream;
    Stream.str(std::string());
    Stream << "[" << std::setfill('0') << std::setw(3) << getLevel() << "]";
    std::string TheLevel(Stream.str());
    OS << TheLevel;
  }
  if (options().getAttributeGlobal())
    OS << (getIsGlobalReference() ? 'X' : ' ');
}

void LVObject::print(raw_ostream &OS, bool Full) const {
  printFileIndex(OS, Full);
  printAttributes(OS, Full);

  // Print the line and any discriminator.
  std::stringstream Stream;
  Stream << " " << std::setw(5) << lineNumberAsString() << " "
         << indentAsString() << " ";
  OS << Stream.str();
}
