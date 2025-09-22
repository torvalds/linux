//===-- mem_map_fuchsia.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_MEM_MAP_FUCHSIA_H_
#define SCUDO_MEM_MAP_FUCHSIA_H_

#include "mem_map_base.h"

#if SCUDO_FUCHSIA

#include <stdint.h>
#include <zircon/types.h>

namespace scudo {

class MemMapFuchsia final : public MemMapBase<MemMapFuchsia> {
public:
  constexpr MemMapFuchsia() = default;

  // Impls for base functions.
  bool mapImpl(uptr Addr, uptr Size, const char *Name, uptr Flags);
  void unmapImpl(uptr Addr, uptr Size);
  bool remapImpl(uptr Addr, uptr Size, const char *Name, uptr Flags);
  void setMemoryPermissionImpl(uptr Addr, uptr Size, uptr Flags);
  void releasePagesToOSImpl(uptr From, uptr Size) {
    return releaseAndZeroPagesToOSImpl(From, Size);
  }
  void releaseAndZeroPagesToOSImpl(uptr From, uptr Size);
  uptr getBaseImpl() { return WindowBase; }
  uptr getCapacityImpl() { return WindowSize; }

private:
  friend class ReservedMemoryFuchsia;

  // Used by ReservedMemoryFuchsia::dispatch.
  MemMapFuchsia(uptr Base, uptr Capacity);

  // Virtual memory address corresponding to VMO offset 0.
  uptr MapAddr = 0;

  // Virtual memory base address and size of the VMO subrange that is still in
  // use. unmapImpl() can shrink this range, either at the beginning or at the
  // end.
  uptr WindowBase = 0;
  uptr WindowSize = 0;

  zx_handle_t Vmo = ZX_HANDLE_INVALID;
};

class ReservedMemoryFuchsia final
    : public ReservedMemory<ReservedMemoryFuchsia, MemMapFuchsia> {
public:
  constexpr ReservedMemoryFuchsia() = default;

  bool createImpl(uptr Addr, uptr Size, const char *Name, uptr Flags);
  void releaseImpl();
  MemMapT dispatchImpl(uptr Addr, uptr Size);
  uptr getBaseImpl() { return Base; }
  uptr getCapacityImpl() { return Capacity; }

private:
  uptr Base = 0;
  uptr Capacity = 0;
};

} // namespace scudo

#endif // SCUDO_FUCHSIA

#endif // SCUDO_MEM_MAP_FUCHSIA_H_
