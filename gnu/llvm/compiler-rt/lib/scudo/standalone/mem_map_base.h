//===-- mem_map_base.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_MEM_MAP_BASE_H_
#define SCUDO_MEM_MAP_BASE_H_

#include "common.h"

namespace scudo {

// In Scudo, every memory operation will be fulfilled through a
// platform-specific `MemMap` instance. The essential APIs are listed in the
// `MemMapBase` below. This is implemented in CRTP, so for each implementation,
// it has to implement all of the 'Impl' named functions.
template <class Derived> class MemMapBase {
public:
  constexpr MemMapBase() = default;

  // This is used to map a new set of contiguous pages. Note that the `Addr` is
  // only a suggestion to the system.
  bool map(uptr Addr, uptr Size, const char *Name, uptr Flags = 0) {
    DCHECK(!isAllocated());
    return invokeImpl(&Derived::mapImpl, Addr, Size, Name, Flags);
  }

  // This is used to unmap partial/full pages from the beginning or the end.
  // I.e., the result pages are expected to be still contiguous.
  void unmap(uptr Addr, uptr Size) {
    DCHECK(isAllocated());
    DCHECK((Addr == getBase()) || (Addr + Size == getBase() + getCapacity()));
    invokeImpl(&Derived::unmapImpl, Addr, Size);
  }

  // This is used to remap a mapped range (either from map() or dispatched from
  // ReservedMemory). For example, we have reserved several pages and then we
  // want to remap them with different accessibility.
  bool remap(uptr Addr, uptr Size, const char *Name, uptr Flags = 0) {
    DCHECK(isAllocated());
    DCHECK((Addr >= getBase()) && (Addr + Size <= getBase() + getCapacity()));
    return invokeImpl(&Derived::remapImpl, Addr, Size, Name, Flags);
  }

  // This is used to update the pages' access permission. For example, mark
  // pages as no read/write permission.
  void setMemoryPermission(uptr Addr, uptr Size, uptr Flags) {
    DCHECK(isAllocated());
    DCHECK((Addr >= getBase()) && (Addr + Size <= getBase() + getCapacity()));
    return invokeImpl(&Derived::setMemoryPermissionImpl, Addr, Size, Flags);
  }

  // Suggest releasing a set of contiguous physical pages back to the OS. Note
  // that only physical pages are supposed to be released. Any release of
  // virtual pages may lead to undefined behavior.
  void releasePagesToOS(uptr From, uptr Size) {
    DCHECK(isAllocated());
    DCHECK((From >= getBase()) && (From + Size <= getBase() + getCapacity()));
    invokeImpl(&Derived::releasePagesToOSImpl, From, Size);
  }
  // This is similar to the above one except that any subsequent access to the
  // released pages will return with zero-filled pages.
  void releaseAndZeroPagesToOS(uptr From, uptr Size) {
    DCHECK(isAllocated());
    DCHECK((From >= getBase()) && (From + Size <= getBase() + getCapacity()));
    invokeImpl(&Derived::releaseAndZeroPagesToOSImpl, From, Size);
  }

  uptr getBase() { return invokeImpl(&Derived::getBaseImpl); }
  uptr getCapacity() { return invokeImpl(&Derived::getCapacityImpl); }

  bool isAllocated() { return getBase() != 0U; }

protected:
  template <typename R, typename... Args>
  R invokeImpl(R (Derived::*MemFn)(Args...), Args... args) {
    return (static_cast<Derived *>(this)->*MemFn)(args...);
  }
};

// `ReservedMemory` is a special memory handle which can be viewed as a page
// allocator. `ReservedMemory` will reserve a contiguous pages and the later
// page request can be fulfilled at the designated address. This is used when
// we want to ensure the virtual address of the MemMap will be in a known range.
// This is implemented in CRTP, so for each
// implementation, it has to implement all of the 'Impl' named functions.
template <class Derived, typename MemMapTy> class ReservedMemory {
public:
  using MemMapT = MemMapTy;
  constexpr ReservedMemory() = default;

  // Reserve a chunk of memory at a suggested address.
  bool create(uptr Addr, uptr Size, const char *Name, uptr Flags = 0) {
    DCHECK(!isCreated());
    return invokeImpl(&Derived::createImpl, Addr, Size, Name, Flags);
  }

  // Release the entire reserved memory.
  void release() {
    DCHECK(isCreated());
    invokeImpl(&Derived::releaseImpl);
  }

  // Dispatch a sub-range of reserved memory. Note that any fragmentation of
  // the reserved pages is managed by each implementation.
  MemMapT dispatch(uptr Addr, uptr Size) {
    DCHECK(isCreated());
    DCHECK((Addr >= getBase()) && (Addr + Size <= getBase() + getCapacity()));
    return invokeImpl(&Derived::dispatchImpl, Addr, Size);
  }

  uptr getBase() { return invokeImpl(&Derived::getBaseImpl); }
  uptr getCapacity() { return invokeImpl(&Derived::getCapacityImpl); }

  bool isCreated() { return getBase() != 0U; }

protected:
  template <typename R, typename... Args>
  R invokeImpl(R (Derived::*MemFn)(Args...), Args... args) {
    return (static_cast<Derived *>(this)->*MemFn)(args...);
  }
};

} // namespace scudo

#endif // SCUDO_MEM_MAP_BASE_H_
