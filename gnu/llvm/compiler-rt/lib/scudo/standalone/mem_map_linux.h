//===-- mem_map_linux.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_MEM_MAP_LINUX_H_
#define SCUDO_MEM_MAP_LINUX_H_

#include "platform.h"

#if SCUDO_LINUX

#include "common.h"
#include "mem_map_base.h"

namespace scudo {

class MemMapLinux final : public MemMapBase<MemMapLinux> {
public:
  constexpr MemMapLinux() = default;
  MemMapLinux(uptr Base, uptr Capacity)
      : MapBase(Base), MapCapacity(Capacity) {}

  // Impls for base functions.
  bool mapImpl(uptr Addr, uptr Size, const char *Name, uptr Flags = 0);
  void unmapImpl(uptr Addr, uptr Size);
  bool remapImpl(uptr Addr, uptr Size, const char *Name, uptr Flags = 0);
  void setMemoryPermissionImpl(uptr Addr, uptr Size, uptr Flags);
  void releasePagesToOSImpl(uptr From, uptr Size) {
    return releaseAndZeroPagesToOSImpl(From, Size);
  }
  void releaseAndZeroPagesToOSImpl(uptr From, uptr Size);
  uptr getBaseImpl() { return MapBase; }
  uptr getCapacityImpl() { return MapCapacity; }

private:
  uptr MapBase = 0;
  uptr MapCapacity = 0;
};

// This will be deprecated when every allocator has been supported by each
// platform's `MemMap` implementation.
class ReservedMemoryLinux final
    : public ReservedMemory<ReservedMemoryLinux, MemMapLinux> {
public:
  // The following two are the Impls for function in `MemMapBase`.
  uptr getBaseImpl() { return MapBase; }
  uptr getCapacityImpl() { return MapCapacity; }

  // These threes are specific to `ReservedMemory`.
  bool createImpl(uptr Addr, uptr Size, const char *Name, uptr Flags);
  void releaseImpl();
  MemMapT dispatchImpl(uptr Addr, uptr Size);

private:
  uptr MapBase = 0;
  uptr MapCapacity = 0;
};

} // namespace scudo

#endif // SCUDO_LINUX

#endif // SCUDO_MEM_MAP_LINUX_H_
