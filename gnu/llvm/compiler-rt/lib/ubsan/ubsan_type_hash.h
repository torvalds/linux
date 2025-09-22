//===-- ubsan_type_hash.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Hashing of types for Clang's undefined behavior checker.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_TYPE_HASH_H
#define UBSAN_TYPE_HASH_H

#include "sanitizer_common/sanitizer_common.h"

namespace __ubsan {

typedef uptr HashValue;

/// \brief Information about the dynamic type of an object (extracted from its
/// vptr).
class DynamicTypeInfo {
  const char *MostDerivedTypeName;
  sptr Offset;
  const char *SubobjectTypeName;

public:
  DynamicTypeInfo(const char *MDTN, sptr Offset, const char *STN)
    : MostDerivedTypeName(MDTN), Offset(Offset), SubobjectTypeName(STN) {}

  /// Determine whether the object had a valid dynamic type.
  bool isValid() const { return MostDerivedTypeName; }
  /// Get the name of the most-derived type of the object.
  const char *getMostDerivedTypeName() const { return MostDerivedTypeName; }
  /// Get the offset from the most-derived type to this base class.
  sptr getOffset() const { return Offset; }
  /// Get the name of the most-derived type at the specified offset.
  const char *getSubobjectTypeName() const { return SubobjectTypeName; }
};

/// \brief Get information about the dynamic type of an object.
DynamicTypeInfo getDynamicTypeInfoFromObject(void *Object);

/// \brief Get information about the dynamic type of an object from its vtable.
DynamicTypeInfo getDynamicTypeInfoFromVtable(void *Vtable);

/// \brief Check whether the dynamic type of \p Object has a \p Type subobject
/// at offset 0.
/// \return \c true if the type matches, \c false if not.
bool checkDynamicType(void *Object, void *Type, HashValue Hash);

const unsigned VptrTypeCacheSize = 128;

/// A sanity check for Vtable. Offsets to top must be reasonably small
/// numbers (by absolute value). It's a weak check for Vtable corruption.
const int VptrMaxOffsetToTop = 1<<20;

/// \brief A cache of the results of checkDynamicType. \c checkDynamicType would
/// return \c true (modulo hash collisions) if
/// \code
///   __ubsan_vptr_type_cache[Hash % VptrTypeCacheSize] == Hash
/// \endcode
extern "C" SANITIZER_INTERFACE_ATTRIBUTE
HashValue __ubsan_vptr_type_cache[VptrTypeCacheSize];

/// \brief Do whatever is required by the ABI to check for std::type_info
/// equivalence beyond simple pointer comparison.
bool checkTypeInfoEquality(const void *TypeInfo1, const void *TypeInfo2);

} // namespace __ubsan

#endif // UBSAN_TYPE_HASH_H
