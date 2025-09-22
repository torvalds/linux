//===-- xray_interface.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Implementation of the API functions.
//
//===----------------------------------------------------------------------===//

#include "xray_interface_internal.h"

#include <cinttypes>
#include <cstdio>
#include <errno.h>
#include <limits>
#include <string.h>
#include <sys/mman.h>

#if SANITIZER_FUCHSIA
#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#endif

#include "sanitizer_common/sanitizer_addrhashmap.h"
#include "sanitizer_common/sanitizer_common.h"

#include "xray_defs.h"
#include "xray_flags.h"

extern __sanitizer::SpinMutex XRayInstrMapMutex;
extern __sanitizer::atomic_uint8_t XRayInitialized;
extern __xray::XRaySledMap XRayInstrMap;

namespace __xray {

#if defined(__x86_64__)
static const int16_t cSledLength = 12;
#elif defined(__aarch64__)
static const int16_t cSledLength = 32;
#elif defined(__arm__)
static const int16_t cSledLength = 28;
#elif SANITIZER_LOONGARCH64
static const int16_t cSledLength = 48;
#elif SANITIZER_MIPS32
static const int16_t cSledLength = 48;
#elif SANITIZER_MIPS64
static const int16_t cSledLength = 64;
#elif defined(__powerpc64__)
static const int16_t cSledLength = 8;
#elif defined(__hexagon__)
static const int16_t cSledLength = 20;
#else
#error "Unsupported CPU Architecture"
#endif /* CPU architecture */

// This is the function to call when we encounter the entry or exit sleds.
atomic_uintptr_t XRayPatchedFunction{0};

// This is the function to call from the arg1-enabled sleds/trampolines.
atomic_uintptr_t XRayArgLogger{0};

// This is the function to call when we encounter a custom event log call.
atomic_uintptr_t XRayPatchedCustomEvent{0};

// This is the function to call when we encounter a typed event log call.
atomic_uintptr_t XRayPatchedTypedEvent{0};

// This is the global status to determine whether we are currently
// patching/unpatching.
atomic_uint8_t XRayPatching{0};

struct TypeDescription {
  uint32_t type_id;
  std::size_t description_string_length;
};

using TypeDescriptorMapType = AddrHashMap<TypeDescription, 11>;
// An address map from immutable descriptors to type ids.
TypeDescriptorMapType TypeDescriptorAddressMap{};

atomic_uint32_t TypeEventDescriptorCounter{0};

// MProtectHelper is an RAII wrapper for calls to mprotect(...) that will
// undo any successful mprotect(...) changes. This is used to make a page
// writeable and executable, and upon destruction if it was successful in
// doing so returns the page into a read-only and executable page.
//
// This is only used specifically for runtime-patching of the XRay
// instrumentation points. This assumes that the executable pages are
// originally read-and-execute only.
class MProtectHelper {
  void *PageAlignedAddr;
  std::size_t MProtectLen;
  bool MustCleanup;

public:
  explicit MProtectHelper(void *PageAlignedAddr,
                          std::size_t MProtectLen,
                          std::size_t PageSize) XRAY_NEVER_INSTRUMENT
      : PageAlignedAddr(PageAlignedAddr),
        MProtectLen(MProtectLen),
        MustCleanup(false) {
#if SANITIZER_FUCHSIA
    MProtectLen = RoundUpTo(MProtectLen, PageSize);
#endif
  }

  int MakeWriteable() XRAY_NEVER_INSTRUMENT {
#if SANITIZER_FUCHSIA
    auto R = __sanitizer_change_code_protection(
        reinterpret_cast<uintptr_t>(PageAlignedAddr), MProtectLen, true);
    if (R != ZX_OK) {
      Report("XRay: cannot change code protection: %s\n",
             _zx_status_get_string(R));
      return -1;
    }
    MustCleanup = true;
    return 0;
#else
    auto R = mprotect(PageAlignedAddr, MProtectLen,
                      PROT_READ | PROT_WRITE | PROT_EXEC);
    if (R != -1)
      MustCleanup = true;
    return R;
#endif
  }

  ~MProtectHelper() XRAY_NEVER_INSTRUMENT {
    if (MustCleanup) {
#if SANITIZER_FUCHSIA
      auto R = __sanitizer_change_code_protection(
          reinterpret_cast<uintptr_t>(PageAlignedAddr), MProtectLen, false);
      if (R != ZX_OK) {
        Report("XRay: cannot change code protection: %s\n",
               _zx_status_get_string(R));
      }
#else
      mprotect(PageAlignedAddr, MProtectLen, PROT_READ | PROT_EXEC);
#endif
    }
  }
};

namespace {

bool patchSled(const XRaySledEntry &Sled, bool Enable,
               int32_t FuncId) XRAY_NEVER_INSTRUMENT {
  bool Success = false;
  switch (Sled.Kind) {
  case XRayEntryType::ENTRY:
    Success = patchFunctionEntry(Enable, FuncId, Sled, __xray_FunctionEntry);
    break;
  case XRayEntryType::EXIT:
    Success = patchFunctionExit(Enable, FuncId, Sled);
    break;
  case XRayEntryType::TAIL:
    Success = patchFunctionTailExit(Enable, FuncId, Sled);
    break;
  case XRayEntryType::LOG_ARGS_ENTRY:
    Success = patchFunctionEntry(Enable, FuncId, Sled, __xray_ArgLoggerEntry);
    break;
  case XRayEntryType::CUSTOM_EVENT:
    Success = patchCustomEvent(Enable, FuncId, Sled);
    break;
  case XRayEntryType::TYPED_EVENT:
    Success = patchTypedEvent(Enable, FuncId, Sled);
    break;
  default:
    Report("Unsupported sled kind '%" PRIu64 "' @%04x\n", Sled.Address,
           int(Sled.Kind));
    return false;
  }
  return Success;
}

const XRayFunctionSledIndex
findFunctionSleds(int32_t FuncId,
                  const XRaySledMap &InstrMap) XRAY_NEVER_INSTRUMENT {
  int32_t CurFn = 0;
  uint64_t LastFnAddr = 0;
  XRayFunctionSledIndex Index = {nullptr, 0};

  for (std::size_t I = 0; I < InstrMap.Entries && CurFn <= FuncId; I++) {
    const auto &Sled = InstrMap.Sleds[I];
    const auto Function = Sled.function();
    if (Function != LastFnAddr) {
      CurFn++;
      LastFnAddr = Function;
    }

    if (CurFn == FuncId) {
      if (Index.Begin == nullptr)
        Index.Begin = &Sled;
      Index.Size = &Sled - Index.Begin + 1;
    }
  }

  return Index;
}

XRayPatchingStatus patchFunction(int32_t FuncId,
                                 bool Enable) XRAY_NEVER_INSTRUMENT {
  if (!atomic_load(&XRayInitialized,
                                memory_order_acquire))
    return XRayPatchingStatus::NOT_INITIALIZED; // Not initialized.

  uint8_t NotPatching = false;
  if (!atomic_compare_exchange_strong(
          &XRayPatching, &NotPatching, true, memory_order_acq_rel))
    return XRayPatchingStatus::ONGOING; // Already patching.

  // Next, we look for the function index.
  XRaySledMap InstrMap;
  {
    SpinMutexLock Guard(&XRayInstrMapMutex);
    InstrMap = XRayInstrMap;
  }

  // If we don't have an index, we can't patch individual functions.
  if (InstrMap.Functions == 0)
    return XRayPatchingStatus::NOT_INITIALIZED;

  // FuncId must be a positive number, less than the number of functions
  // instrumented.
  if (FuncId <= 0 || static_cast<size_t>(FuncId) > InstrMap.Functions) {
    Report("Invalid function id provided: %d\n", FuncId);
    return XRayPatchingStatus::FAILED;
  }

  // Now we patch ths sleds for this specific function.
  XRayFunctionSledIndex SledRange;
  if (InstrMap.SledsIndex) {
    SledRange = {InstrMap.SledsIndex[FuncId - 1].fromPCRelative(),
                 InstrMap.SledsIndex[FuncId - 1].Size};
  } else {
    SledRange = findFunctionSleds(FuncId, InstrMap);
  }
  auto *f = SledRange.Begin;
  bool SucceedOnce = false;
  for (size_t i = 0; i != SledRange.Size; ++i)
    SucceedOnce |= patchSled(f[i], Enable, FuncId);

  atomic_store(&XRayPatching, false,
                            memory_order_release);

  if (!SucceedOnce) {
    Report("Failed patching any sled for function '%d'.", FuncId);
    return XRayPatchingStatus::FAILED;
  }

  return XRayPatchingStatus::SUCCESS;
}

// controlPatching implements the common internals of the patching/unpatching
// implementation. |Enable| defines whether we're enabling or disabling the
// runtime XRay instrumentation.
XRayPatchingStatus controlPatching(bool Enable) XRAY_NEVER_INSTRUMENT {
  if (!atomic_load(&XRayInitialized,
                                memory_order_acquire))
    return XRayPatchingStatus::NOT_INITIALIZED; // Not initialized.

  uint8_t NotPatching = false;
  if (!atomic_compare_exchange_strong(
          &XRayPatching, &NotPatching, true, memory_order_acq_rel))
    return XRayPatchingStatus::ONGOING; // Already patching.

  uint8_t PatchingSuccess = false;
  auto XRayPatchingStatusResetter =
      at_scope_exit([&PatchingSuccess] {
        if (!PatchingSuccess)
          atomic_store(&XRayPatching, false,
                                    memory_order_release);
      });

  XRaySledMap InstrMap;
  {
    SpinMutexLock Guard(&XRayInstrMapMutex);
    InstrMap = XRayInstrMap;
  }
  if (InstrMap.Entries == 0)
    return XRayPatchingStatus::NOT_INITIALIZED;

  uint32_t FuncId = 1;
  uint64_t CurFun = 0;

  // First we want to find the bounds for which we have instrumentation points,
  // and try to get as few calls to mprotect(...) as possible. We're assuming
  // that all the sleds for the instrumentation map are contiguous as a single
  // set of pages. When we do support dynamic shared object instrumentation,
  // we'll need to do this for each set of page load offsets per DSO loaded. For
  // now we're assuming we can mprotect the whole section of text between the
  // minimum sled address and the maximum sled address (+ the largest sled
  // size).
  auto *MinSled = &InstrMap.Sleds[0];
  auto *MaxSled = &InstrMap.Sleds[InstrMap.Entries - 1];
  for (std::size_t I = 0; I < InstrMap.Entries; I++) {
    const auto &Sled = InstrMap.Sleds[I];
    if (Sled.address() < MinSled->address())
      MinSled = &Sled;
    if (Sled.address() > MaxSled->address())
      MaxSled = &Sled;
  }

  const size_t PageSize = flags()->xray_page_size_override > 0
                              ? flags()->xray_page_size_override
                              : GetPageSizeCached();
  if ((PageSize == 0) || ((PageSize & (PageSize - 1)) != 0)) {
    Report("System page size is not a power of two: %zu\n", PageSize);
    return XRayPatchingStatus::FAILED;
  }

  void *PageAlignedAddr =
      reinterpret_cast<void *>(MinSled->address() & ~(PageSize - 1));
  size_t MProtectLen =
      (MaxSled->address() - reinterpret_cast<uptr>(PageAlignedAddr)) +
      cSledLength;
  MProtectHelper Protector(PageAlignedAddr, MProtectLen, PageSize);
  if (Protector.MakeWriteable() == -1) {
    Report("Failed mprotect: %d\n", errno);
    return XRayPatchingStatus::FAILED;
  }

  for (std::size_t I = 0; I < InstrMap.Entries; ++I) {
    auto &Sled = InstrMap.Sleds[I];
    auto F = Sled.function();
    if (CurFun == 0)
      CurFun = F;
    if (F != CurFun) {
      ++FuncId;
      CurFun = F;
    }
    patchSled(Sled, Enable, FuncId);
  }
  atomic_store(&XRayPatching, false,
                            memory_order_release);
  PatchingSuccess = true;
  return XRayPatchingStatus::SUCCESS;
}

XRayPatchingStatus mprotectAndPatchFunction(int32_t FuncId,
                                            bool Enable) XRAY_NEVER_INSTRUMENT {
  XRaySledMap InstrMap;
  {
    SpinMutexLock Guard(&XRayInstrMapMutex);
    InstrMap = XRayInstrMap;
  }

  // FuncId must be a positive number, less than the number of functions
  // instrumented.
  if (FuncId <= 0 || static_cast<size_t>(FuncId) > InstrMap.Functions) {
    Report("Invalid function id provided: %d\n", FuncId);
    return XRayPatchingStatus::FAILED;
  }

  const size_t PageSize = flags()->xray_page_size_override > 0
                              ? flags()->xray_page_size_override
                              : GetPageSizeCached();
  if ((PageSize == 0) || ((PageSize & (PageSize - 1)) != 0)) {
    Report("Provided page size is not a power of two: %zu\n", PageSize);
    return XRayPatchingStatus::FAILED;
  }

  // Here we compute the minimum sled and maximum sled associated with a
  // particular function ID.
  XRayFunctionSledIndex SledRange;
  if (InstrMap.SledsIndex) {
    SledRange = {InstrMap.SledsIndex[FuncId - 1].fromPCRelative(),
                 InstrMap.SledsIndex[FuncId - 1].Size};
  } else {
    SledRange = findFunctionSleds(FuncId, InstrMap);
  }
  auto *f = SledRange.Begin;
  auto *e = SledRange.Begin + SledRange.Size;
  auto *MinSled = f;
  auto *MaxSled = e - 1;
  while (f != e) {
    if (f->address() < MinSled->address())
      MinSled = f;
    if (f->address() > MaxSled->address())
      MaxSled = f;
    ++f;
  }

  void *PageAlignedAddr =
      reinterpret_cast<void *>(MinSled->address() & ~(PageSize - 1));
  size_t MProtectLen =
      (MaxSled->address() - reinterpret_cast<uptr>(PageAlignedAddr)) +
      cSledLength;
  MProtectHelper Protector(PageAlignedAddr, MProtectLen, PageSize);
  if (Protector.MakeWriteable() == -1) {
    Report("Failed mprotect: %d\n", errno);
    return XRayPatchingStatus::FAILED;
  }
  return patchFunction(FuncId, Enable);
}

} // namespace

} // namespace __xray

using namespace __xray;

// The following functions are declared `extern "C" {...}` in the header, hence
// they're defined in the global namespace.

int __xray_set_handler(void (*entry)(int32_t,
                                     XRayEntryType)) XRAY_NEVER_INSTRUMENT {
  if (atomic_load(&XRayInitialized,
                               memory_order_acquire)) {

    atomic_store(&__xray::XRayPatchedFunction,
                              reinterpret_cast<uintptr_t>(entry),
                              memory_order_release);
    return 1;
  }
  return 0;
}

int __xray_set_customevent_handler(void (*entry)(void *, size_t))
    XRAY_NEVER_INSTRUMENT {
  if (atomic_load(&XRayInitialized,
                               memory_order_acquire)) {
    atomic_store(&__xray::XRayPatchedCustomEvent,
                              reinterpret_cast<uintptr_t>(entry),
                              memory_order_release);
    return 1;
  }
  return 0;
}

int __xray_set_typedevent_handler(void (*entry)(size_t, const void *,
                                                size_t)) XRAY_NEVER_INSTRUMENT {
  if (atomic_load(&XRayInitialized,
                               memory_order_acquire)) {
    atomic_store(&__xray::XRayPatchedTypedEvent,
                              reinterpret_cast<uintptr_t>(entry),
                              memory_order_release);
    return 1;
  }
  return 0;
}

int __xray_remove_handler() XRAY_NEVER_INSTRUMENT {
  return __xray_set_handler(nullptr);
}

int __xray_remove_customevent_handler() XRAY_NEVER_INSTRUMENT {
  return __xray_set_customevent_handler(nullptr);
}

int __xray_remove_typedevent_handler() XRAY_NEVER_INSTRUMENT {
  return __xray_set_typedevent_handler(nullptr);
}

uint16_t __xray_register_event_type(
    const char *const event_type) XRAY_NEVER_INSTRUMENT {
  TypeDescriptorMapType::Handle h(&TypeDescriptorAddressMap, (uptr)event_type);
  if (h.created()) {
    h->type_id = atomic_fetch_add(
        &TypeEventDescriptorCounter, 1, memory_order_acq_rel);
    h->description_string_length = strnlen(event_type, 1024);
  }
  return h->type_id;
}

XRayPatchingStatus __xray_patch() XRAY_NEVER_INSTRUMENT {
  return controlPatching(true);
}

XRayPatchingStatus __xray_unpatch() XRAY_NEVER_INSTRUMENT {
  return controlPatching(false);
}

XRayPatchingStatus __xray_patch_function(int32_t FuncId) XRAY_NEVER_INSTRUMENT {
  return mprotectAndPatchFunction(FuncId, true);
}

XRayPatchingStatus
__xray_unpatch_function(int32_t FuncId) XRAY_NEVER_INSTRUMENT {
  return mprotectAndPatchFunction(FuncId, false);
}

int __xray_set_handler_arg1(void (*entry)(int32_t, XRayEntryType, uint64_t)) {
  if (!atomic_load(&XRayInitialized,
                                memory_order_acquire))
    return 0;

  // A relaxed write might not be visible even if the current thread gets
  // scheduled on a different CPU/NUMA node.  We need to wait for everyone to
  // have this handler installed for consistency of collected data across CPUs.
  atomic_store(&XRayArgLogger, reinterpret_cast<uint64_t>(entry),
                            memory_order_release);
  return 1;
}

int __xray_remove_handler_arg1() { return __xray_set_handler_arg1(nullptr); }

uintptr_t __xray_function_address(int32_t FuncId) XRAY_NEVER_INSTRUMENT {
  XRaySledMap InstrMap;
  {
    SpinMutexLock Guard(&XRayInstrMapMutex);
    InstrMap = XRayInstrMap;
  }

  if (FuncId <= 0 || static_cast<size_t>(FuncId) > InstrMap.Functions)
    return 0;
  const XRaySledEntry *Sled =
      InstrMap.SledsIndex ? InstrMap.SledsIndex[FuncId - 1].fromPCRelative()
                          : findFunctionSleds(FuncId, InstrMap).Begin;
  return Sled->function()
// On PPC, function entries are always aligned to 16 bytes. The beginning of a
// sled might be a local entry, which is always +8 based on the global entry.
// Always return the global entry.
#ifdef __PPC__
         & ~0xf
#endif
      ;
}

size_t __xray_max_function_id() XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Guard(&XRayInstrMapMutex);
  return XRayInstrMap.Functions;
}
