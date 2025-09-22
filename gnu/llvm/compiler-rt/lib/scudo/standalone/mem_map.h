//===-- mem_map.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_MEM_MAP_H_
#define SCUDO_MEM_MAP_H_

#include "mem_map_base.h"

#include "common.h"
#include "internal_defs.h"

// TODO: This is only used for `MapPlatformData`. Remove these includes when we
// have all three platform specific `MemMap` and `ReservedMemory`
// implementations.
#include "fuchsia.h"
#include "linux.h"
#include "trusty.h"

#include "mem_map_fuchsia.h"
#include "mem_map_linux.h"

namespace scudo {

// This will be deprecated when every allocator has been supported by each
// platform's `MemMap` implementation.
class MemMapDefault final : public MemMapBase<MemMapDefault> {
public:
  constexpr MemMapDefault() = default;
  MemMapDefault(uptr Base, uptr Capacity) : Base(Base), Capacity(Capacity) {}

  // Impls for base functions.
  bool mapImpl(uptr Addr, uptr Size, const char *Name, uptr Flags);
  void unmapImpl(uptr Addr, uptr Size);
  bool remapImpl(uptr Addr, uptr Size, const char *Name, uptr Flags);
  void setMemoryPermissionImpl(uptr Addr, uptr Size, uptr Flags);
  void releasePagesToOSImpl(uptr From, uptr Size) {
    return releaseAndZeroPagesToOSImpl(From, Size);
  }
  void releaseAndZeroPagesToOSImpl(uptr From, uptr Size);
  uptr getBaseImpl() { return Base; }
  uptr getCapacityImpl() { return Capacity; }

  void setMapPlatformData(MapPlatformData &NewData) { Data = NewData; }

private:
  uptr Base = 0;
  uptr Capacity = 0;
  uptr MappedBase = 0;
  MapPlatformData Data = {};
};

// This will be deprecated when every allocator has been supported by each
// platform's `MemMap` implementation.
class ReservedMemoryDefault final
    : public ReservedMemory<ReservedMemoryDefault, MemMapDefault> {
public:
  constexpr ReservedMemoryDefault() = default;

  bool createImpl(uptr Addr, uptr Size, const char *Name, uptr Flags);
  void releaseImpl();
  MemMapT dispatchImpl(uptr Addr, uptr Size);
  uptr getBaseImpl() { return Base; }
  uptr getCapacityImpl() { return Capacity; }

private:
  uptr Base = 0;
  uptr Capacity = 0;
  MapPlatformData Data = {};
};

#if SCUDO_LINUX
using ReservedMemoryT = ReservedMemoryLinux;
using MemMapT = ReservedMemoryT::MemMapT;
#elif SCUDO_FUCHSIA
using ReservedMemoryT = ReservedMemoryFuchsia;
using MemMapT = ReservedMemoryT::MemMapT;
#elif SCUDO_TRUSTY
using ReservedMemoryT = ReservedMemoryDefault;
using MemMapT = ReservedMemoryT::MemMapT;
#else
#error                                                                         \
    "Unsupported platform, please implement the ReservedMemory for your platform!"
#endif

} // namespace scudo

#endif // SCUDO_MEM_MAP_H_
