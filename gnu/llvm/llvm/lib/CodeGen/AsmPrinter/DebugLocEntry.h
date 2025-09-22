//===-- llvm/CodeGen/DebugLocEntry.h - Entry in debug_loc list -*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DEBUGLOCENTRY_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DEBUGLOCENTRY_H

#include "DebugLocStream.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/Debug.h"

namespace llvm {
class AsmPrinter;

/// This struct describes target specific location.
struct TargetIndexLocation {
  int Index;
  int Offset;

  TargetIndexLocation() = default;
  TargetIndexLocation(unsigned Idx, int64_t Offset)
      : Index(Idx), Offset(Offset) {}

  bool operator==(const TargetIndexLocation &Other) const {
    return Index == Other.Index && Offset == Other.Offset;
  }
};

/// A single location or constant within a variable location description, with
/// either a single entry (with an optional DIExpression) used for a DBG_VALUE,
/// or a list of entries used for a DBG_VALUE_LIST.
class DbgValueLocEntry {

  /// Type of entry that this represents.
  enum EntryType {
    E_Location,
    E_Integer,
    E_ConstantFP,
    E_ConstantInt,
    E_TargetIndexLocation
  };
  enum EntryType EntryKind;

  /// Either a constant,
  union {
    int64_t Int;
    const ConstantFP *CFP;
    const ConstantInt *CIP;
  } Constant;

  union {
    /// Or a location in the machine frame.
    MachineLocation Loc;
    /// Or a location from target specific location.
    TargetIndexLocation TIL;
  };

public:
  DbgValueLocEntry(int64_t i) : EntryKind(E_Integer) { Constant.Int = i; }
  DbgValueLocEntry(const ConstantFP *CFP) : EntryKind(E_ConstantFP) {
    Constant.CFP = CFP;
  }
  DbgValueLocEntry(const ConstantInt *CIP) : EntryKind(E_ConstantInt) {
    Constant.CIP = CIP;
  }
  DbgValueLocEntry(MachineLocation Loc) : EntryKind(E_Location), Loc(Loc) {}
  DbgValueLocEntry(TargetIndexLocation Loc)
      : EntryKind(E_TargetIndexLocation), TIL(Loc) {}

  bool isLocation() const { return EntryKind == E_Location; }
  bool isIndirectLocation() const {
    return EntryKind == E_Location && Loc.isIndirect();
  }
  bool isTargetIndexLocation() const {
    return EntryKind == E_TargetIndexLocation;
  }
  bool isInt() const { return EntryKind == E_Integer; }
  bool isConstantFP() const { return EntryKind == E_ConstantFP; }
  bool isConstantInt() const { return EntryKind == E_ConstantInt; }
  int64_t getInt() const { return Constant.Int; }
  const ConstantFP *getConstantFP() const { return Constant.CFP; }
  const ConstantInt *getConstantInt() const { return Constant.CIP; }
  MachineLocation getLoc() const { return Loc; }
  TargetIndexLocation getTargetIndexLocation() const { return TIL; }
  friend bool operator==(const DbgValueLocEntry &, const DbgValueLocEntry &);
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const {
    if (isLocation()) {
      llvm::dbgs() << "Loc = { reg=" << Loc.getReg() << " ";
      if (Loc.isIndirect())
        llvm::dbgs() << "+0";
      llvm::dbgs() << "} ";
    } else if (isConstantInt())
      Constant.CIP->dump();
    else if (isConstantFP())
      Constant.CFP->dump();
  }
#endif
};

/// The location of a single variable, composed of an expression and 0 or more
/// DbgValueLocEntries.
class DbgValueLoc {
  /// Any complex address location expression for this DbgValueLoc.
  const DIExpression *Expression;

  SmallVector<DbgValueLocEntry, 2> ValueLocEntries;

  bool IsVariadic;

public:
  DbgValueLoc(const DIExpression *Expr, ArrayRef<DbgValueLocEntry> Locs)
      : Expression(Expr), ValueLocEntries(Locs.begin(), Locs.end()),
        IsVariadic(true) {}

  DbgValueLoc(const DIExpression *Expr, ArrayRef<DbgValueLocEntry> Locs,
              bool IsVariadic)
      : Expression(Expr), ValueLocEntries(Locs.begin(), Locs.end()),
        IsVariadic(IsVariadic) {
#ifndef NDEBUG
    assert(Expr->isValid() ||
           !any_of(Locs, [](auto LE) { return LE.isLocation(); }));
    if (!IsVariadic) {
      assert(ValueLocEntries.size() == 1);
    }
#endif
  }

  DbgValueLoc(const DIExpression *Expr, DbgValueLocEntry Loc)
      : Expression(Expr), ValueLocEntries(1, Loc), IsVariadic(false) {
    assert(((Expr && Expr->isValid()) || !Loc.isLocation()) &&
           "DBG_VALUE with a machine location must have a valid expression.");
  }

  bool isFragment() const { return getExpression()->isFragment(); }
  bool isEntryVal() const { return getExpression()->isEntryValue(); }
  bool isVariadic() const { return IsVariadic; }
  bool isEquivalent(const DbgValueLoc &Other) const {
    // Cannot be equivalent with different numbers of entries.
    if (ValueLocEntries.size() != Other.ValueLocEntries.size())
      return false;
    bool ThisIsIndirect =
        !IsVariadic && ValueLocEntries[0].isIndirectLocation();
    bool OtherIsIndirect =
        !Other.IsVariadic && Other.ValueLocEntries[0].isIndirectLocation();
    // Check equivalence of DIExpressions + Directness together.
    if (!DIExpression::isEqualExpression(Expression, ThisIsIndirect,
                                         Other.Expression, OtherIsIndirect))
      return false;
    // Indirectness should have been accounted for in the above check, so just
    // compare register values directly here.
    if (ThisIsIndirect || OtherIsIndirect) {
      DbgValueLocEntry ThisOp = ValueLocEntries[0];
      DbgValueLocEntry OtherOp = Other.ValueLocEntries[0];
      return ThisOp.isLocation() && OtherOp.isLocation() &&
             ThisOp.getLoc().getReg() == OtherOp.getLoc().getReg();
    }
    // If neither are indirect, then just compare the loc entries directly.
    return ValueLocEntries == Other.ValueLocEntries;
  }
  const DIExpression *getExpression() const { return Expression; }
  ArrayRef<DbgValueLocEntry> getLocEntries() const { return ValueLocEntries; }
  friend bool operator==(const DbgValueLoc &, const DbgValueLoc &);
  friend bool operator<(const DbgValueLoc &, const DbgValueLoc &);
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const {
    for (const DbgValueLocEntry &DV : ValueLocEntries)
      DV.dump();
    if (Expression)
      Expression->dump();
  }
#endif
};

/// This struct describes location entries emitted in the .debug_loc
/// section.
class DebugLocEntry {
  /// Begin and end symbols for the address range that this location is valid.
  const MCSymbol *Begin;
  const MCSymbol *End;

  /// A nonempty list of locations/constants belonging to this entry,
  /// sorted by offset.
  SmallVector<DbgValueLoc, 1> Values;

public:
  /// Create a location list entry for the range [\p Begin, \p End).
  ///
  /// \param Vals One or more values describing (parts of) the variable.
  DebugLocEntry(const MCSymbol *Begin, const MCSymbol *End,
                ArrayRef<DbgValueLoc> Vals)
      : Begin(Begin), End(End) {
    addValues(Vals);
  }

  /// Attempt to merge this DebugLocEntry with Next and return
  /// true if the merge was successful. Entries can be merged if they
  /// share the same Loc/Constant and if Next immediately follows this
  /// Entry.
  bool MergeRanges(const DebugLocEntry &Next) {
    // If this and Next are describing the same variable, merge them.
    if (End != Next.Begin)
      return false;
    if (Values.size() != Next.Values.size())
      return false;
    for (unsigned EntryIdx = 0; EntryIdx < Values.size(); ++EntryIdx)
      if (!Values[EntryIdx].isEquivalent(Next.Values[EntryIdx]))
        return false;
    End = Next.End;
    return true;
  }

  const MCSymbol *getBeginSym() const { return Begin; }
  const MCSymbol *getEndSym() const { return End; }
  ArrayRef<DbgValueLoc> getValues() const { return Values; }
  void addValues(ArrayRef<DbgValueLoc> Vals) {
    Values.append(Vals.begin(), Vals.end());
    sortUniqueValues();
    assert((Values.size() == 1 || all_of(Values, [](DbgValueLoc V) {
              return V.isFragment();
            })) && "must either have a single value or multiple pieces");
  }

  // Sort the pieces by offset.
  // Remove any duplicate entries by dropping all but the first.
  void sortUniqueValues() {
    // Values is either 1 item that does not have a fragment, or many items
    // that all do. No need to sort if the former and also prevents operator<
    // being called on a non fragment item when _GLIBCXX_DEBUG is defined.
    if (Values.size() == 1)
      return;
    llvm::sort(Values);
    Values.erase(llvm::unique(Values,
                              [](const DbgValueLoc &A, const DbgValueLoc &B) {
                                return A.getExpression() == B.getExpression();
                              }),
                 Values.end());
  }

  /// Lower this entry into a DWARF expression.
  void finalize(const AsmPrinter &AP,
                DebugLocStream::ListBuilder &List,
                const DIBasicType *BT,
                DwarfCompileUnit &TheCU);
};

/// Compare two DbgValueLocEntries for equality.
inline bool operator==(const DbgValueLocEntry &A, const DbgValueLocEntry &B) {
  if (A.EntryKind != B.EntryKind)
    return false;

  switch (A.EntryKind) {
  case DbgValueLocEntry::E_Location:
    return A.Loc == B.Loc;
  case DbgValueLocEntry::E_TargetIndexLocation:
    return A.TIL == B.TIL;
  case DbgValueLocEntry::E_Integer:
    return A.Constant.Int == B.Constant.Int;
  case DbgValueLocEntry::E_ConstantFP:
    return A.Constant.CFP == B.Constant.CFP;
  case DbgValueLocEntry::E_ConstantInt:
    return A.Constant.CIP == B.Constant.CIP;
  }
  llvm_unreachable("unhandled EntryKind");
}

/// Compare two DbgValueLocs for equality.
inline bool operator==(const DbgValueLoc &A, const DbgValueLoc &B) {
  return A.ValueLocEntries == B.ValueLocEntries &&
         A.Expression == B.Expression && A.IsVariadic == B.IsVariadic;
}

/// Compare two fragments based on their offset.
inline bool operator<(const DbgValueLoc &A,
                      const DbgValueLoc &B) {
  return A.getExpression()->getFragmentInfo()->OffsetInBits <
         B.getExpression()->getFragmentInfo()->OffsetInBits;
}

}

#endif
