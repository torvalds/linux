//===- DWARFAttribute.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFATTRIBUTE_H
#define LLVM_DEBUGINFO_DWARF_DWARFATTRIBUTE_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include <cstdint>

namespace llvm {

//===----------------------------------------------------------------------===//
/// Encapsulates a DWARF attribute value and all of the data required to
/// describe the attribute value.
///
/// This class is designed to be used by clients that want to iterate across all
/// attributes in a DWARFDie.
struct DWARFAttribute {
  /// The debug info/types offset for this attribute.
  uint64_t Offset = 0;
  /// The debug info/types section byte size of the data for this attribute.
  uint32_t ByteSize = 0;
  /// The attribute enumeration of this attribute.
  dwarf::Attribute Attr = dwarf::Attribute(0);
  /// The form and value for this attribute.
  DWARFFormValue Value;

  bool isValid() const {
    return Offset != 0 && Attr != dwarf::Attribute(0);
  }

  explicit operator bool() const {
    return isValid();
  }

  /// Identify DWARF attributes that may contain a pointer to a location list.
  static bool mayHaveLocationList(dwarf::Attribute Attr);

  /// Identifies DWARF attributes that may contain a reference to a
  /// DWARF expression.
  static bool mayHaveLocationExpr(dwarf::Attribute Attr);
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFATTRIBUTE_H
