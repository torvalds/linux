//===- TypeRecordHelpers.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPERECORDHELPERS_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPERECORDHELPERS_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"

namespace llvm {
namespace codeview {

/// Given an arbitrary codeview type, determine if it is an LF_STRUCTURE,
/// LF_CLASS, LF_INTERFACE, LF_UNION, or LF_ENUM with the forward ref class
/// option.
bool isUdtForwardRef(CVType CVT);

/// Given a CVType which is assumed to be an LF_MODIFIER, return the
/// TypeIndex of the type that the LF_MODIFIER modifies.
TypeIndex getModifiedType(const CVType &CVT);

/// Return true if this record should be in the IPI stream of a PDB. In an
/// object file, these record kinds will appear mixed into the .debug$T section.
inline bool isIdRecord(TypeLeafKind K) {
  switch (K) {
  case TypeLeafKind::LF_FUNC_ID:
  case TypeLeafKind::LF_MFUNC_ID:
  case TypeLeafKind::LF_STRING_ID:
  case TypeLeafKind::LF_SUBSTR_LIST:
  case TypeLeafKind::LF_BUILDINFO:
  case TypeLeafKind::LF_UDT_SRC_LINE:
  case TypeLeafKind::LF_UDT_MOD_SRC_LINE:
    return true;
  default:
    return false;
  }
}

/// Given an arbitrary codeview type, determine if it is an LF_STRUCTURE,
/// LF_CLASS, LF_INTERFACE, LF_UNION.
inline bool isAggregate(CVType CVT) {
  switch (CVT.kind()) {
  case LF_STRUCTURE:
  case LF_CLASS:
  case LF_INTERFACE:
  case LF_UNION:
    return true;
  default:
    return false;
  }
}

/// Given an arbitrary codeview type index, determine its size.
uint64_t getSizeInBytesForTypeIndex(TypeIndex TI);

/// Given an arbitrary codeview type, return the type's size in the case
/// of aggregate (LF_STRUCTURE, LF_CLASS, LF_INTERFACE, LF_UNION).
uint64_t getSizeInBytesForTypeRecord(CVType CVT);

} // namespace codeview
} // namespace llvm

#endif
