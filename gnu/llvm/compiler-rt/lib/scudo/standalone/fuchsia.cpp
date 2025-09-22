//===-- fuchsia.cpp ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

#if SCUDO_FUCHSIA

#include "common.h"
#include "mutex.h"
#include "string_utils.h"

#include <lib/sync/mutex.h> // for sync_mutex_t
#include <stdlib.h>         // for getenv()
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

namespace scudo {

uptr getPageSize() { return _zx_system_get_page_size(); }

void NORETURN die() { __builtin_trap(); }

// We zero-initialize the Extra parameter of map(), make sure this is consistent
// with ZX_HANDLE_INVALID.
static_assert(ZX_HANDLE_INVALID == 0, "");

static void NORETURN dieOnError(zx_status_t Status, const char *FnName,
                                uptr Size) {
  ScopedString Error;
  Error.append("SCUDO ERROR: %s failed with size %zuKB (%s)", FnName,
               Size >> 10, zx_status_get_string(Status));
  outputRaw(Error.data());
  die();
}

static void *allocateVmar(uptr Size, MapPlatformData *Data, bool AllowNoMem) {
  // Only scenario so far.
  DCHECK(Data);
  DCHECK_EQ(Data->Vmar, ZX_HANDLE_INVALID);

  const zx_status_t Status = _zx_vmar_allocate(
      _zx_vmar_root_self(),
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
      Size, &Data->Vmar, &Data->VmarBase);
  if (UNLIKELY(Status != ZX_OK)) {
    if (Status != ZX_ERR_NO_MEMORY || !AllowNoMem)
      dieOnError(Status, "zx_vmar_allocate", Size);
    return nullptr;
  }
  return reinterpret_cast<void *>(Data->VmarBase);
}

void *map(void *Addr, uptr Size, const char *Name, uptr Flags,
          MapPlatformData *Data) {
  DCHECK_EQ(Size % getPageSizeCached(), 0);
  const bool AllowNoMem = !!(Flags & MAP_ALLOWNOMEM);

  // For MAP_NOACCESS, just allocate a Vmar and return.
  if (Flags & MAP_NOACCESS)
    return allocateVmar(Size, Data, AllowNoMem);

  const zx_handle_t Vmar = (Data && Data->Vmar != ZX_HANDLE_INVALID)
                               ? Data->Vmar
                               : _zx_vmar_root_self();

  zx_status_t Status;
  zx_handle_t Vmo;
  uint64_t VmoSize = 0;
  if (Data && Data->Vmo != ZX_HANDLE_INVALID) {
    // If a Vmo was specified, it's a resize operation.
    CHECK(Addr);
    DCHECK(Flags & MAP_RESIZABLE);
    Vmo = Data->Vmo;
    VmoSize = Data->VmoSize;
    Status = _zx_vmo_set_size(Vmo, VmoSize + Size);
    if (Status != ZX_OK) {
      if (Status != ZX_ERR_NO_MEMORY || !AllowNoMem)
        dieOnError(Status, "zx_vmo_set_size", VmoSize + Size);
      return nullptr;
    }
  } else {
    // Otherwise, create a Vmo and set its name.
    Status = _zx_vmo_create(Size, ZX_VMO_RESIZABLE, &Vmo);
    if (UNLIKELY(Status != ZX_OK)) {
      if (Status != ZX_ERR_NO_MEMORY || !AllowNoMem)
        dieOnError(Status, "zx_vmo_create", Size);
      return nullptr;
    }
    _zx_object_set_property(Vmo, ZX_PROP_NAME, Name, strlen(Name));
  }

  uintptr_t P;
  zx_vm_option_t MapFlags =
      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_ALLOW_FAULTS;
  if (Addr)
    DCHECK(Data);
  const uint64_t Offset =
      Addr ? reinterpret_cast<uintptr_t>(Addr) - Data->VmarBase : 0;
  if (Offset)
    MapFlags |= ZX_VM_SPECIFIC;
  Status = _zx_vmar_map(Vmar, MapFlags, Offset, Vmo, VmoSize, Size, &P);
  if (UNLIKELY(Status != ZX_OK)) {
    if (Status != ZX_ERR_NO_MEMORY || !AllowNoMem)
      dieOnError(Status, "zx_vmar_map", Size);
    return nullptr;
  }

  if (Flags & MAP_PRECOMMIT) {
    Status = _zx_vmar_op_range(Vmar, ZX_VMAR_OP_COMMIT, P, Size,
                               /*buffer=*/nullptr, /*buffer_size=*/0);
  }

  // No need to track the Vmo if we don't intend on resizing it. Close it.
  if (Flags & MAP_RESIZABLE) {
    DCHECK(Data);
    if (Data->Vmo == ZX_HANDLE_INVALID)
      Data->Vmo = Vmo;
    else
      DCHECK_EQ(Data->Vmo, Vmo);
  } else {
    CHECK_EQ(_zx_handle_close(Vmo), ZX_OK);
  }
  if (UNLIKELY(Status != ZX_OK)) {
    if (Status != ZX_ERR_NO_MEMORY || !AllowNoMem)
      dieOnError(Status, "zx_vmar_op_range", Size);
    return nullptr;
  }

  if (Data)
    Data->VmoSize += Size;

  return reinterpret_cast<void *>(P);
}

void unmap(void *Addr, uptr Size, uptr Flags, MapPlatformData *Data) {
  if (Flags & UNMAP_ALL) {
    DCHECK_NE(Data, nullptr);
    const zx_handle_t Vmar = Data->Vmar;
    DCHECK_NE(Vmar, _zx_vmar_root_self());
    // Destroying the vmar effectively unmaps the whole mapping.
    CHECK_EQ(_zx_vmar_destroy(Vmar), ZX_OK);
    CHECK_EQ(_zx_handle_close(Vmar), ZX_OK);
  } else {
    const zx_handle_t Vmar = (Data && Data->Vmar != ZX_HANDLE_INVALID)
                                 ? Data->Vmar
                                 : _zx_vmar_root_self();
    const zx_status_t Status =
        _zx_vmar_unmap(Vmar, reinterpret_cast<uintptr_t>(Addr), Size);
    if (UNLIKELY(Status != ZX_OK))
      dieOnError(Status, "zx_vmar_unmap", Size);
  }
  if (Data) {
    if (Data->Vmo != ZX_HANDLE_INVALID)
      CHECK_EQ(_zx_handle_close(Data->Vmo), ZX_OK);
    memset(Data, 0, sizeof(*Data));
  }
}

void setMemoryPermission(UNUSED uptr Addr, UNUSED uptr Size, UNUSED uptr Flags,
                         UNUSED MapPlatformData *Data) {
  const zx_vm_option_t Prot =
      (Flags & MAP_NOACCESS) ? 0 : (ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  DCHECK(Data);
  DCHECK_NE(Data->Vmar, ZX_HANDLE_INVALID);
  const zx_status_t Status = _zx_vmar_protect(Data->Vmar, Prot, Addr, Size);
  if (Status != ZX_OK)
    dieOnError(Status, "zx_vmar_protect", Size);
}

void releasePagesToOS(UNUSED uptr BaseAddress, uptr Offset, uptr Size,
                      MapPlatformData *Data) {
  // TODO: DCHECK the BaseAddress is consistent with the data in
  // MapPlatformData.
  DCHECK(Data);
  DCHECK_NE(Data->Vmar, ZX_HANDLE_INVALID);
  DCHECK_NE(Data->Vmo, ZX_HANDLE_INVALID);
  const zx_status_t Status =
      _zx_vmo_op_range(Data->Vmo, ZX_VMO_OP_DECOMMIT, Offset, Size, NULL, 0);
  CHECK_EQ(Status, ZX_OK);
}

const char *getEnv(const char *Name) { return getenv(Name); }

// Note: we need to flag these methods with __TA_NO_THREAD_SAFETY_ANALYSIS
// because the Fuchsia implementation of sync_mutex_t has clang thread safety
// annotations. Were we to apply proper capability annotations to the top level
// HybridMutex class itself, they would not be needed. As it stands, the
// thread analysis thinks that we are locking the mutex and accidentally leaving
// it locked on the way out.
bool HybridMutex::tryLock() __TA_NO_THREAD_SAFETY_ANALYSIS {
  // Size and alignment must be compatible between both types.
  return sync_mutex_trylock(&M) == ZX_OK;
}

void HybridMutex::lockSlow() __TA_NO_THREAD_SAFETY_ANALYSIS {
  sync_mutex_lock(&M);
}

void HybridMutex::unlock() __TA_NO_THREAD_SAFETY_ANALYSIS {
  sync_mutex_unlock(&M);
}

void HybridMutex::assertHeldImpl() __TA_NO_THREAD_SAFETY_ANALYSIS {}

u64 getMonotonicTime() { return _zx_clock_get_monotonic(); }
u64 getMonotonicTimeFast() { return _zx_clock_get_monotonic(); }

u32 getNumberOfCPUs() { return _zx_system_get_num_cpus(); }

u32 getThreadID() { return 0; }

bool getRandom(void *Buffer, uptr Length, UNUSED bool Blocking) {
  static_assert(MaxRandomLength <= ZX_CPRNG_DRAW_MAX_LEN, "");
  if (UNLIKELY(!Buffer || !Length || Length > MaxRandomLength))
    return false;
  _zx_cprng_draw(Buffer, Length);
  return true;
}

void outputRaw(const char *Buffer) {
  __sanitizer_log_write(Buffer, strlen(Buffer));
}

void setAbortMessage(const char *Message) {}

} // namespace scudo

#endif // SCUDO_FUCHSIA
