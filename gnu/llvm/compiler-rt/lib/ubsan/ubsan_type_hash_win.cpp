//===-- ubsan_type_hash_win.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of type hashing/lookup for Microsoft C++ ABI.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#include "ubsan_platform.h"
#if CAN_SANITIZE_UB && defined(_MSC_VER)
#include "ubsan_type_hash.h"

#include "sanitizer_common/sanitizer_common.h"

#include <typeinfo>

struct CompleteObjectLocator {
  int is_image_relative;
  int offset_to_top;
  int vfptr_offset;
  int rtti_addr;
  int chd_addr;
  int obj_locator_addr;
};

struct CompleteObjectLocatorAbs {
  int is_image_relative;
  int offset_to_top;
  int vfptr_offset;
  std::type_info *rtti_addr;
  void *chd_addr;
  CompleteObjectLocator *obj_locator_addr;
};

bool __ubsan::checkDynamicType(void *Object, void *Type, HashValue Hash) {
  // FIXME: Implement.
  return false;
}

__ubsan::DynamicTypeInfo
__ubsan::getDynamicTypeInfoFromVtable(void *VtablePtr) {
  // The virtual table may not have a complete object locator if the object
  // was compiled without RTTI (i.e. we might be reading from some other global
  // laid out before the virtual table), so we need to carefully validate each
  // pointer dereference and perform sanity checks.
  CompleteObjectLocator **obj_locator_ptr =
    ((CompleteObjectLocator**)VtablePtr)-1;
  if (!IsAccessibleMemoryRange((uptr)obj_locator_ptr, sizeof(void*)))
    return DynamicTypeInfo(0, 0, 0);

  CompleteObjectLocator *obj_locator = *obj_locator_ptr;
  if (!IsAccessibleMemoryRange((uptr)obj_locator,
                               sizeof(CompleteObjectLocator)))
    return DynamicTypeInfo(0, 0, 0);

  std::type_info *tinfo;
  if (obj_locator->is_image_relative == 1) {
    char *image_base = ((char *)obj_locator) - obj_locator->obj_locator_addr;
    tinfo = (std::type_info *)(image_base + obj_locator->rtti_addr);
  } else if (obj_locator->is_image_relative == 0)
    tinfo = ((CompleteObjectLocatorAbs *)obj_locator)->rtti_addr;
  else
    // Probably not a complete object locator.
    return DynamicTypeInfo(0, 0, 0);

  if (!IsAccessibleMemoryRange((uptr)tinfo, sizeof(std::type_info)))
    return DynamicTypeInfo(0, 0, 0);

  // Okay, this is probably a std::type_info. Request its name.
  // FIXME: Implement a base class search like we do for Itanium.
  return DynamicTypeInfo(tinfo->name(), obj_locator->offset_to_top,
                         "<unknown>");
}

bool __ubsan::checkTypeInfoEquality(const void *, const void *) {
  return false;
}

#endif  // CAN_SANITIZE_UB && SANITIZER_WINDOWS
