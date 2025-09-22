//===-- mem_map_fuchsia.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mem_map_fuchsia.h"

#include "atomic_helpers.h"
#include "common.h"
#include "string_utils.h"

#if SCUDO_FUCHSIA

#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace scudo {

static void NORETURN dieOnError(zx_status_t Status, const char *FnName,
                                uptr Size) {
  ScopedString Error;
  Error.append("SCUDO ERROR: %s failed with size %zuKB (%s)", FnName,
               Size >> 10, _zx_status_get_string(Status));
  outputRaw(Error.data());
  die();
}

static void setVmoName(zx_handle_t Vmo, const char *Name) {
  size_t Len = strlen(Name);
  DCHECK_LT(Len, ZX_MAX_NAME_LEN);
  zx_status_t Status = _zx_object_set_property(Vmo, ZX_PROP_NAME, Name, Len);
  CHECK_EQ(Status, ZX_OK);
}

// Returns the (cached) base address of the root VMAR.
static uptr getRootVmarBase() {
  static atomic_uptr CachedResult = {0};

  uptr Result = atomic_load(&CachedResult, memory_order_acquire);
  if (UNLIKELY(!Result)) {
    zx_info_vmar_t VmarInfo;
    zx_status_t Status =
        _zx_object_get_info(_zx_vmar_root_self(), ZX_INFO_VMAR, &VmarInfo,
                            sizeof(VmarInfo), nullptr, nullptr);
    CHECK_EQ(Status, ZX_OK);
    CHECK_NE(VmarInfo.base, 0);

    atomic_store(&CachedResult, VmarInfo.base, memory_order_release);
    Result = VmarInfo.base;
  }

  return Result;
}

// Lazily creates and then always returns the same zero-sized VMO.
static zx_handle_t getPlaceholderVmo() {
  static atomic_u32 StoredVmo = {ZX_HANDLE_INVALID};

  zx_handle_t Vmo = atomic_load(&StoredVmo, memory_order_acquire);
  if (UNLIKELY(Vmo == ZX_HANDLE_INVALID)) {
    // Create a zero-sized placeholder VMO.
    zx_status_t Status = _zx_vmo_create(0, 0, &Vmo);
    if (UNLIKELY(Status != ZX_OK))
      dieOnError(Status, "zx_vmo_create", 0);

    setVmoName(Vmo, "scudo:reserved");

    // Atomically store its handle. If some other thread wins the race, use its
    // handle and discard ours.
    zx_handle_t OldValue = atomic_compare_exchange_strong(
        &StoredVmo, ZX_HANDLE_INVALID, Vmo, memory_order_acq_rel);
    if (UNLIKELY(OldValue != ZX_HANDLE_INVALID)) {
      Status = _zx_handle_close(Vmo);
      CHECK_EQ(Status, ZX_OK);

      Vmo = OldValue;
    }
  }

  return Vmo;
}

// Checks if MAP_ALLOWNOMEM allows the given error code.
static bool IsNoMemError(zx_status_t Status) {
  // Note: _zx_vmar_map returns ZX_ERR_NO_RESOURCES if the VMAR does not contain
  // a suitable free spot.
  return Status == ZX_ERR_NO_MEMORY || Status == ZX_ERR_NO_RESOURCES;
}

// Note: this constructor is only called by ReservedMemoryFuchsia::dispatch.
MemMapFuchsia::MemMapFuchsia(uptr Base, uptr Capacity)
    : MapAddr(Base), WindowBase(Base), WindowSize(Capacity) {
  // Create the VMO.
  zx_status_t Status = _zx_vmo_create(Capacity, 0, &Vmo);
  if (UNLIKELY(Status != ZX_OK))
    dieOnError(Status, "zx_vmo_create", Capacity);

  setVmoName(Vmo, "scudo:dispatched");
}

bool MemMapFuchsia::mapImpl(UNUSED uptr Addr, uptr Size, const char *Name,
                            uptr Flags) {
  const bool AllowNoMem = !!(Flags & MAP_ALLOWNOMEM);
  const bool PreCommit = !!(Flags & MAP_PRECOMMIT);
  const bool NoAccess = !!(Flags & MAP_NOACCESS);

  // Create the VMO.
  zx_status_t Status = _zx_vmo_create(Size, 0, &Vmo);
  if (UNLIKELY(Status != ZX_OK)) {
    if (AllowNoMem && IsNoMemError(Status))
      return false;
    dieOnError(Status, "zx_vmo_create", Size);
  }

  if (Name != nullptr)
    setVmoName(Vmo, Name);

  // Map it.
  zx_vm_option_t MapFlags = ZX_VM_ALLOW_FAULTS;
  if (!NoAccess)
    MapFlags |= ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  Status =
      _zx_vmar_map(_zx_vmar_root_self(), MapFlags, 0, Vmo, 0, Size, &MapAddr);
  if (UNLIKELY(Status != ZX_OK)) {
    if (AllowNoMem && IsNoMemError(Status)) {
      Status = _zx_handle_close(Vmo);
      CHECK_EQ(Status, ZX_OK);

      MapAddr = 0;
      Vmo = ZX_HANDLE_INVALID;
      return false;
    }
    dieOnError(Status, "zx_vmar_map", Size);
  }

  if (PreCommit) {
    Status = _zx_vmar_op_range(_zx_vmar_root_self(), ZX_VMAR_OP_COMMIT, MapAddr,
                               Size, nullptr, 0);
    CHECK_EQ(Status, ZX_OK);
  }

  WindowBase = MapAddr;
  WindowSize = Size;
  return true;
}

void MemMapFuchsia::unmapImpl(uptr Addr, uptr Size) {
  zx_status_t Status;

  if (Size == WindowSize) {
    // NOTE: Closing first and then unmapping seems slightly faster than doing
    // the same operations in the opposite order.
    Status = _zx_handle_close(Vmo);
    CHECK_EQ(Status, ZX_OK);
    Status = _zx_vmar_unmap(_zx_vmar_root_self(), Addr, Size);
    CHECK_EQ(Status, ZX_OK);

    MapAddr = WindowBase = WindowSize = 0;
    Vmo = ZX_HANDLE_INVALID;
  } else {
    // Unmap the subrange.
    Status = _zx_vmar_unmap(_zx_vmar_root_self(), Addr, Size);
    CHECK_EQ(Status, ZX_OK);

    // Decommit the pages that we just unmapped.
    Status = _zx_vmo_op_range(Vmo, ZX_VMO_OP_DECOMMIT, Addr - MapAddr, Size,
                              nullptr, 0);
    CHECK_EQ(Status, ZX_OK);

    if (Addr == WindowBase)
      WindowBase += Size;
    WindowSize -= Size;
  }
}

bool MemMapFuchsia::remapImpl(uptr Addr, uptr Size, const char *Name,
                              uptr Flags) {
  const bool AllowNoMem = !!(Flags & MAP_ALLOWNOMEM);
  const bool PreCommit = !!(Flags & MAP_PRECOMMIT);
  const bool NoAccess = !!(Flags & MAP_NOACCESS);

  // NOTE: This will rename the *whole* VMO, not only the requested portion of
  // it. But we cannot do better than this given the MemMap API. In practice,
  // the upper layers of Scudo always pass the same Name for a given MemMap.
  if (Name != nullptr)
    setVmoName(Vmo, Name);

  uptr MappedAddr;
  zx_vm_option_t MapFlags = ZX_VM_ALLOW_FAULTS | ZX_VM_SPECIFIC_OVERWRITE;
  if (!NoAccess)
    MapFlags |= ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  zx_status_t Status =
      _zx_vmar_map(_zx_vmar_root_self(), MapFlags, Addr - getRootVmarBase(),
                   Vmo, Addr - MapAddr, Size, &MappedAddr);
  if (UNLIKELY(Status != ZX_OK)) {
    if (AllowNoMem && IsNoMemError(Status))
      return false;
    dieOnError(Status, "zx_vmar_map", Size);
  }
  DCHECK_EQ(Addr, MappedAddr);

  if (PreCommit) {
    Status = _zx_vmar_op_range(_zx_vmar_root_self(), ZX_VMAR_OP_COMMIT, MapAddr,
                               Size, nullptr, 0);
    CHECK_EQ(Status, ZX_OK);
  }

  return true;
}

void MemMapFuchsia::releaseAndZeroPagesToOSImpl(uptr From, uptr Size) {
  zx_status_t Status = _zx_vmo_op_range(Vmo, ZX_VMO_OP_DECOMMIT, From - MapAddr,
                                        Size, nullptr, 0);
  CHECK_EQ(Status, ZX_OK);
}

void MemMapFuchsia::setMemoryPermissionImpl(uptr Addr, uptr Size, uptr Flags) {
  const bool NoAccess = !!(Flags & MAP_NOACCESS);

  zx_vm_option_t MapFlags = 0;
  if (!NoAccess)
    MapFlags |= ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  zx_status_t Status =
      _zx_vmar_protect(_zx_vmar_root_self(), MapFlags, Addr, Size);
  CHECK_EQ(Status, ZX_OK);
}

bool ReservedMemoryFuchsia::createImpl(UNUSED uptr Addr, uptr Size,
                                       UNUSED const char *Name, uptr Flags) {
  const bool AllowNoMem = !!(Flags & MAP_ALLOWNOMEM);

  // Reserve memory by mapping the placeholder VMO without any permission.
  zx_status_t Status = _zx_vmar_map(_zx_vmar_root_self(), ZX_VM_ALLOW_FAULTS, 0,
                                    getPlaceholderVmo(), 0, Size, &Base);
  if (UNLIKELY(Status != ZX_OK)) {
    if (AllowNoMem && IsNoMemError(Status))
      return false;
    dieOnError(Status, "zx_vmar_map", Size);
  }

  Capacity = Size;
  return true;
}

void ReservedMemoryFuchsia::releaseImpl() {
  zx_status_t Status = _zx_vmar_unmap(_zx_vmar_root_self(), Base, Capacity);
  CHECK_EQ(Status, ZX_OK);
}

ReservedMemoryFuchsia::MemMapT ReservedMemoryFuchsia::dispatchImpl(uptr Addr,
                                                                   uptr Size) {
  return ReservedMemoryFuchsia::MemMapT(Addr, Size);
}

} // namespace scudo

#endif // SCUDO_FUCHSIA
