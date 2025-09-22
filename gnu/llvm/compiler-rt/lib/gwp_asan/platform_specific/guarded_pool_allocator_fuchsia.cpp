//===-- guarded_pool_allocator_fuchsia.cpp ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/guarded_pool_allocator.h"
#include "gwp_asan/utilities.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace gwp_asan {
void GuardedPoolAllocator::initPRNG() {
  _zx_cprng_draw(&getThreadLocals()->RandomState, sizeof(uint32_t));
}

void *GuardedPoolAllocator::map(size_t Size, const char *Name) const {
  assert((Size % State.PageSize) == 0);
  zx_handle_t Vmo;
  zx_status_t Status = _zx_vmo_create(Size, 0, &Vmo);
  check(Status == ZX_OK, "Failed to create Vmo");
  _zx_object_set_property(Vmo, ZX_PROP_NAME, Name, strlen(Name));
  zx_vaddr_t Addr;
  Status = _zx_vmar_map(_zx_vmar_root_self(),
                        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_ALLOW_FAULTS,
                        0, Vmo, 0, Size, &Addr);
  check(Status == ZX_OK, "Vmo mapping failed");
  _zx_handle_close(Vmo);
  return reinterpret_cast<void *>(Addr);
}

void GuardedPoolAllocator::unmap(void *Ptr, size_t Size) const {
  assert((reinterpret_cast<uintptr_t>(Ptr) % State.PageSize) == 0);
  assert((Size % State.PageSize) == 0);
  zx_status_t Status = _zx_vmar_unmap(_zx_vmar_root_self(),
                                      reinterpret_cast<zx_vaddr_t>(Ptr), Size);
  check(Status == ZX_OK, "Vmo unmapping failed");
}

void *GuardedPoolAllocator::reserveGuardedPool(size_t Size) {
  assert((Size % State.PageSize) == 0);
  zx_vaddr_t Addr;
  const zx_status_t Status = _zx_vmar_allocate(
      _zx_vmar_root_self(),
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
      Size, &GuardedPagePoolPlatformData.Vmar, &Addr);
  check(Status == ZX_OK, "Failed to reserve guarded pool allocator memory");
  _zx_object_set_property(GuardedPagePoolPlatformData.Vmar, ZX_PROP_NAME,
                          kGwpAsanGuardPageName, strlen(kGwpAsanGuardPageName));
  return reinterpret_cast<void *>(Addr);
}

void GuardedPoolAllocator::unreserveGuardedPool() {
  const zx_handle_t Vmar = GuardedPagePoolPlatformData.Vmar;
  assert(Vmar != ZX_HANDLE_INVALID && Vmar != _zx_vmar_root_self());
  check(_zx_vmar_destroy(Vmar) == ZX_OK, "Failed to destroy a vmar");
  check(_zx_handle_close(Vmar) == ZX_OK, "Failed to close a vmar");
  GuardedPagePoolPlatformData.Vmar = ZX_HANDLE_INVALID;
}

void GuardedPoolAllocator::allocateInGuardedPool(void *Ptr, size_t Size) const {
  assert((reinterpret_cast<uintptr_t>(Ptr) % State.PageSize) == 0);
  assert((Size % State.PageSize) == 0);
  zx_handle_t Vmo;
  zx_status_t Status = _zx_vmo_create(Size, 0, &Vmo);
  check(Status == ZX_OK, "Failed to create vmo");
  _zx_object_set_property(Vmo, ZX_PROP_NAME, kGwpAsanAliveSlotName,
                          strlen(kGwpAsanAliveSlotName));
  const zx_handle_t Vmar = GuardedPagePoolPlatformData.Vmar;
  assert(Vmar != ZX_HANDLE_INVALID && Vmar != _zx_vmar_root_self());
  const size_t Offset =
      reinterpret_cast<uintptr_t>(Ptr) - State.GuardedPagePool;
  zx_vaddr_t P;
  Status = _zx_vmar_map(Vmar,
                        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE |
                            ZX_VM_ALLOW_FAULTS | ZX_VM_SPECIFIC,
                        Offset, Vmo, 0, Size, &P);
  check(Status == ZX_OK, "Vmo mapping failed");
  _zx_handle_close(Vmo);
}

void GuardedPoolAllocator::deallocateInGuardedPool(void *Ptr,
                                                   size_t Size) const {
  assert((reinterpret_cast<uintptr_t>(Ptr) % State.PageSize) == 0);
  assert((Size % State.PageSize) == 0);
  const zx_handle_t Vmar = GuardedPagePoolPlatformData.Vmar;
  assert(Vmar != ZX_HANDLE_INVALID && Vmar != _zx_vmar_root_self());
  const zx_status_t Status =
      _zx_vmar_unmap(Vmar, reinterpret_cast<zx_vaddr_t>(Ptr), Size);
  check(Status == ZX_OK, "Vmar unmapping failed");
}

size_t GuardedPoolAllocator::getPlatformPageSize() {
  return _zx_system_get_page_size();
}

void GuardedPoolAllocator::installAtFork() {}
} // namespace gwp_asan
