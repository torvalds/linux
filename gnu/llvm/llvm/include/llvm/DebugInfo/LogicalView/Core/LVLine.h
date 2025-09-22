//===-- LVLine.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVLine class, which is used to describe a debug
// information line.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLINE_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLINE_H

#include "llvm/DebugInfo/LogicalView/Core/LVElement.h"

namespace llvm {
namespace logicalview {

enum class LVLineKind {
  IsBasicBlock,
  IsDiscriminator,
  IsEndSequence,
  IsEpilogueBegin,
  IsLineDebug,
  IsLineAssembler,
  IsNewStatement, // Shared with CodeView 'IsStatement' flag.
  IsPrologueEnd,
  IsAlwaysStepInto, // CodeView
  IsNeverStepInto,  // CodeView
  LastEntry
};
using LVLineKindSet = std::set<LVLineKind>;
using LVLineDispatch = std::map<LVLineKind, LVLineGetFunction>;
using LVLineRequest = std::vector<LVLineGetFunction>;

// Class to represent a logical line.
class LVLine : public LVElement {
  // Typed bitvector with kinds for this line.
  LVProperties<LVLineKind> Kinds;
  static LVLineDispatch Dispatch;

  // Find the current line in the given 'Targets'.
  LVLine *findIn(const LVLines *Targets) const;

public:
  LVLine() : LVElement(LVSubclassID::LV_LINE) {
    setIsLine();
    setIncludeInPrint();
  }
  LVLine(const LVLine &) = delete;
  LVLine &operator=(const LVLine &) = delete;
  virtual ~LVLine() = default;

  static bool classof(const LVElement *Element) {
    return Element->getSubclassID() == LVSubclassID::LV_LINE;
  }

  KIND(LVLineKind, IsBasicBlock);
  KIND(LVLineKind, IsDiscriminator);
  KIND(LVLineKind, IsEndSequence);
  KIND(LVLineKind, IsEpilogueBegin);
  KIND(LVLineKind, IsLineDebug);
  KIND(LVLineKind, IsLineAssembler);
  KIND(LVLineKind, IsNewStatement);
  KIND(LVLineKind, IsPrologueEnd);
  KIND(LVLineKind, IsAlwaysStepInto);
  KIND(LVLineKind, IsNeverStepInto);

  const char *kind() const override;

  // Use the offset to store the line address.
  uint64_t getAddress() const { return getOffset(); }
  void setAddress(uint64_t address) { setOffset(address); }

  // String used for printing objects with no line number.
  std::string noLineAsString(bool ShowZero = false) const override;

  // Line number for display; in the case of Inlined Functions, we use the
  // DW_AT_call_line attribute; otherwise use DW_AT_decl_line attribute.
  std::string lineNumberAsString(bool ShowZero = false) const override {
    return lineAsString(getLineNumber(), getDiscriminator(), ShowZero);
  }

  static LVLineDispatch &getDispatch() { return Dispatch; }

  // Iterate through the 'References' set and check that all its elements
  // are present in the 'Targets' set. For a missing element, mark its
  // parents as missing.
  static void markMissingParents(const LVLines *References,
                                 const LVLines *Targets);

  // Returns true if current line is logically equal to the given 'Line'.
  virtual bool equals(const LVLine *Line) const;

  // Returns true if the given 'References' are logically equal to the
  // given 'Targets'.
  static bool equals(const LVLines *References, const LVLines *Targets);

  // Report the current line as missing or added during comparison.
  void report(LVComparePass Pass) override;

  void print(raw_ostream &OS, bool Full = true) const override;
  void printExtra(raw_ostream &OS, bool Full = true) const override {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const override { print(dbgs()); }
#endif
};

// Class to represent a DWARF line record object.
class LVLineDebug final : public LVLine {
  // Discriminator value (DW_LNE_set_discriminator). The DWARF standard
  // defines the discriminator as an unsigned LEB128 integer.
  uint32_t Discriminator = 0;

public:
  LVLineDebug() : LVLine() { setIsLineDebug(); }
  LVLineDebug(const LVLineDebug &) = delete;
  LVLineDebug &operator=(const LVLineDebug &) = delete;
  ~LVLineDebug() = default;

  // Additional line information. It includes attributes that describes
  // states in the machine instructions (basic block, end prologue, etc).
  std::string statesInfo(bool Formatted) const;

  // Access DW_LNE_set_discriminator attribute.
  uint32_t getDiscriminator() const override { return Discriminator; }
  void setDiscriminator(uint32_t Value) override {
    Discriminator = Value;
    setIsDiscriminator();
  }

  // Returns true if current line is logically equal to the given 'Line'.
  bool equals(const LVLine *Line) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

// Class to represent an assembler line extracted from the text section.
class LVLineAssembler final : public LVLine {
public:
  LVLineAssembler() : LVLine() { setIsLineAssembler(); }
  LVLineAssembler(const LVLineAssembler &) = delete;
  LVLineAssembler &operator=(const LVLineAssembler &) = delete;
  ~LVLineAssembler() = default;

  // Print blanks as the line number.
  std::string noLineAsString(bool ShowZero) const override {
    return std::string(8, ' ');
  };

  // Returns true if current line is logically equal to the given 'Line'.
  bool equals(const LVLine *Line) const override;

  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLINE_H
