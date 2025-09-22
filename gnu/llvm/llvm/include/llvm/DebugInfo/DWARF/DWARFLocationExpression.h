//===- DWARFLocationExpression.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFLOCATIONEXPRESSION_H
#define LLVM_DEBUGINFO_DWARF_DWARFLOCATIONEXPRESSION_H

#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"

namespace llvm {

class raw_ostream;

/// Represents a single DWARF expression, whose value is location-dependent.
/// Typically used in DW_AT_location attributes to describe the location of
/// objects.
struct DWARFLocationExpression {
  /// The address range in which this expression is valid. std::nullopt denotes a
  /// default entry which is valid in addresses not covered by other location
  /// expressions, or everywhere if there are no other expressions.
  std::optional<DWARFAddressRange> Range;

  /// The expression itself.
  SmallVector<uint8_t, 4> Expr;
};

inline bool operator==(const DWARFLocationExpression &L,
                       const DWARFLocationExpression &R) {
  return L.Range == R.Range && L.Expr == R.Expr;
}

inline bool operator!=(const DWARFLocationExpression &L,
                       const DWARFLocationExpression &R) {
  return !(L == R);
}

raw_ostream &operator<<(raw_ostream &OS, const DWARFLocationExpression &Loc);

/// Represents a set of absolute location expressions.
using DWARFLocationExpressionsVector = std::vector<DWARFLocationExpression>;

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFLOCATIONEXPRESSION_H
