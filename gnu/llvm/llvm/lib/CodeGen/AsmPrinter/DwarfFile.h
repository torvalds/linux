//===- llvm/CodeGen/DwarfFile.h - Dwarf Debug Framework ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DWARFFILE_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DWARFFILE_H

#include "DwarfStringPool.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/Support/Allocator.h"
#include <map>
#include <memory>
#include <utility>

namespace llvm {

class AsmPrinter;
class DbgEntity;
class DbgVariable;
class DbgLabel;
class DINode;
class DILocalScope;
class DwarfCompileUnit;
class DwarfUnit;
class LexicalScope;
class MCSection;
class MDNode;

// Data structure to hold a range for range lists.
struct RangeSpan {
  const MCSymbol *Begin;
  const MCSymbol *End;
};

struct RangeSpanList {
  // Index for locating within the debug_range section this particular span.
  MCSymbol *Label;
  const DwarfCompileUnit *CU;
  // List of ranges.
  SmallVector<RangeSpan, 2> Ranges;
};

class DwarfFile {
  // Target of Dwarf emission, used for sizing of abbreviations.
  AsmPrinter *Asm;

  BumpPtrAllocator AbbrevAllocator;

  // Used to uniquely define abbreviations.
  DIEAbbrevSet Abbrevs;

  // A pointer to all units in the section.
  SmallVector<std::unique_ptr<DwarfCompileUnit>, 1> CUs;

  DwarfStringPool StrPool;

  // List of range lists for a given compile unit, separate from the ranges for
  // the CU itself.
  SmallVector<RangeSpanList, 1> CURangeLists;

  /// DWARF v5: The symbol that designates the start of the contribution to
  /// the string offsets table. The contribution is shared by all units.
  MCSymbol *StringOffsetsStartSym = nullptr;

  /// DWARF v5: The symbol that designates the base of the range list table.
  /// The table is shared by all units.
  MCSymbol *RnglistsTableBaseSym = nullptr;

  /// The variables of a lexical scope.
  struct ScopeVars {
    /// We need to sort Args by ArgNo and check for duplicates. This could also
    /// be implemented as a list or vector + std::lower_bound().
    std::map<unsigned, DbgVariable *> Args;
    SmallVector<DbgVariable *, 8> Locals;
  };
  /// Collection of DbgVariables of each lexical scope.
  DenseMap<LexicalScope *, ScopeVars> ScopeVariables;

  /// Collection of DbgLabels of each lexical scope.
  using LabelList = SmallVector<DbgLabel *, 4>;
  DenseMap<LexicalScope *, LabelList> ScopeLabels;

  // Collection of abstract subprogram DIEs.
  DenseMap<const DILocalScope *, DIE *> AbstractLocalScopeDIEs;
  DenseMap<const DINode *, std::unique_ptr<DbgEntity>> AbstractEntities;

  /// Maps MDNodes for type system with the corresponding DIEs. These DIEs can
  /// be shared across CUs, that is why we keep the map here instead
  /// of in DwarfCompileUnit.
  DenseMap<const MDNode *, DIE *> DITypeNodeToDieMap;

public:
  DwarfFile(AsmPrinter *AP, StringRef Pref, BumpPtrAllocator &DA);

  const SmallVectorImpl<std::unique_ptr<DwarfCompileUnit>> &getUnits() {
    return CUs;
  }

  std::pair<uint32_t, RangeSpanList *> addRange(const DwarfCompileUnit &CU,
                                                SmallVector<RangeSpan, 2> R);

  /// getRangeLists - Get the vector of range lists.
  const SmallVectorImpl<RangeSpanList> &getRangeLists() const {
    return CURangeLists;
  }

  /// Compute the size and offset of a DIE given an incoming Offset.
  unsigned computeSizeAndOffset(DIE &Die, unsigned Offset);

  /// Compute the size and offset of all the DIEs.
  void computeSizeAndOffsets();

  /// Compute the size and offset of all the DIEs in the given unit.
  /// \returns The size of the root DIE.
  unsigned computeSizeAndOffsetsForUnit(DwarfUnit *TheU);

  /// Add a unit to the list of CUs.
  void addUnit(std::unique_ptr<DwarfCompileUnit> U);

  /// Emit all of the units to the section listed with the given
  /// abbreviation section.
  void emitUnits(bool UseOffsets);

  /// Emit the given unit to its section.
  void emitUnit(DwarfUnit *TheU, bool UseOffsets);

  /// Emit a set of abbreviations to the specific section.
  void emitAbbrevs(MCSection *);

  /// Emit all of the strings to the section given. If OffsetSection is
  /// non-null, emit a table of string offsets to it. If UseRelativeOffsets
  /// is false, emit absolute offsets to the strings. Otherwise, emit
  /// relocatable references to the strings if they are supported by the target.
  void emitStrings(MCSection *StrSection, MCSection *OffsetSection = nullptr,
                   bool UseRelativeOffsets = false);

  /// Returns the string pool.
  DwarfStringPool &getStringPool() { return StrPool; }

  MCSymbol *getStringOffsetsStartSym() const { return StringOffsetsStartSym; }
  void setStringOffsetsStartSym(MCSymbol *Sym) { StringOffsetsStartSym = Sym; }

  MCSymbol *getRnglistsTableBaseSym() const { return RnglistsTableBaseSym; }
  void setRnglistsTableBaseSym(MCSymbol *Sym) { RnglistsTableBaseSym = Sym; }

  void addScopeVariable(LexicalScope *LS, DbgVariable *Var);

  void addScopeLabel(LexicalScope *LS, DbgLabel *Label);

  DenseMap<LexicalScope *, ScopeVars> &getScopeVariables() {
    return ScopeVariables;
  }

  DenseMap<LexicalScope *, LabelList> &getScopeLabels() {
    return ScopeLabels;
  }

  DenseMap<const DILocalScope *, DIE *> &getAbstractScopeDIEs() {
    return AbstractLocalScopeDIEs;
  }

  DenseMap<const DINode *, std::unique_ptr<DbgEntity>> &getAbstractEntities() {
    return AbstractEntities;
  }

  void insertDIE(const MDNode *TypeMD, DIE *Die) {
    DITypeNodeToDieMap.insert(std::make_pair(TypeMD, Die));
  }

  DIE *getDIE(const MDNode *TypeMD) {
    return DITypeNodeToDieMap.lookup(TypeMD);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_ASMPRINTER_DWARFFILE_H
