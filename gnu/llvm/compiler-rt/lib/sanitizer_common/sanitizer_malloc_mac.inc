//===-- sanitizer_malloc_mac.inc --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains Mac-specific malloc interceptors and a custom zone
// implementation, which together replace the system allocator.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if !SANITIZER_APPLE
#error "This file should only be compiled on Darwin."
#endif

#include <AvailabilityMacros.h>
#include <CoreFoundation/CFBase.h>
#include <dlfcn.h>
#include <malloc/malloc.h>
#include <sys/mman.h>

#include "interception/interception.h"
#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_mac.h"

// Similar code is used in Google Perftools,
// https://github.com/gperftools/gperftools.

namespace __sanitizer {

extern malloc_zone_t sanitizer_zone;

struct sanitizer_malloc_introspection_t : public malloc_introspection_t {
  // IMPORTANT: Do not change the order, alignment, or types of these fields to
  // maintain binary compatibility. You should only add fields to this struct.

  // Used to track changes to the allocator that will affect
  // zone enumeration.
  u64 allocator_enumeration_version;
  uptr allocator_ptr;
  uptr allocator_size;
};

u64 GetMallocZoneAllocatorEnumerationVersion() {
  // This represents the current allocator ABI version.
  // This field should be incremented every time the Allocator
  // ABI changes in a way that breaks allocator enumeration.
  return 0;
}

}  // namespace __sanitizer

INTERCEPTOR(malloc_zone_t *, malloc_create_zone,
                             vm_size_t start_size, unsigned zone_flags) {
  COMMON_MALLOC_ENTER();
  uptr page_size = GetPageSizeCached();
  uptr allocated_size = RoundUpTo(sizeof(sanitizer_zone), page_size);
  COMMON_MALLOC_MEMALIGN(page_size, allocated_size);
  malloc_zone_t *new_zone = (malloc_zone_t *)p;
  internal_memcpy(new_zone, &sanitizer_zone, sizeof(sanitizer_zone));
  new_zone->zone_name = NULL;  // The name will be changed anyway.
  // Prevent the client app from overwriting the zone contents.
  // Library functions that need to modify the zone will set PROT_WRITE on it.
  // This matches the behavior of malloc_create_zone() on OSX 10.7 and higher.
  mprotect(new_zone, allocated_size, PROT_READ);
  // We're explicitly *NOT* registering the zone.
  return new_zone;
}

INTERCEPTOR(void, malloc_destroy_zone, malloc_zone_t *zone) {
  COMMON_MALLOC_ENTER();
  // We don't need to do anything here.  We're not registering new zones, so we
  // don't to unregister.  Just un-mprotect and free() the zone.
  uptr page_size = GetPageSizeCached();
  uptr allocated_size = RoundUpTo(sizeof(sanitizer_zone), page_size);
  mprotect(zone, allocated_size, PROT_READ | PROT_WRITE);
  if (zone->zone_name) {
    COMMON_MALLOC_FREE((void *)zone->zone_name);
  }
  COMMON_MALLOC_FREE(zone);
}

INTERCEPTOR(malloc_zone_t *, malloc_default_zone, void) {
  COMMON_MALLOC_ENTER();
  return &sanitizer_zone;
}

INTERCEPTOR(malloc_zone_t *, malloc_zone_from_ptr, const void *ptr) {
  COMMON_MALLOC_ENTER();
  size_t size = sanitizer_zone.size(&sanitizer_zone, ptr);
  if (size) { // Claimed by sanitizer zone?
    return &sanitizer_zone;
  }
  return REAL(malloc_zone_from_ptr)(ptr);
}

INTERCEPTOR(malloc_zone_t *, malloc_default_purgeable_zone, void) {
  // FIXME: ASan should support purgeable allocations.
  // https://github.com/google/sanitizers/issues/139
  COMMON_MALLOC_ENTER();
  return &sanitizer_zone;
}

INTERCEPTOR(void, malloc_make_purgeable, void *ptr) {
  // FIXME: ASan should support purgeable allocations. Ignoring them is fine
  // for now.
  COMMON_MALLOC_ENTER();
}

INTERCEPTOR(int, malloc_make_nonpurgeable, void *ptr) {
  // FIXME: ASan should support purgeable allocations. Ignoring them is fine
  // for now.
  COMMON_MALLOC_ENTER();
  // Must return 0 if the contents were not purged since the last call to
  // malloc_make_purgeable().
  return 0;
}

INTERCEPTOR(void, malloc_set_zone_name, malloc_zone_t *zone, const char *name) {
  COMMON_MALLOC_ENTER();
  InternalScopedString new_name;
  if (name && zone->introspect == sanitizer_zone.introspect) {
    new_name.AppendF(COMMON_MALLOC_ZONE_NAME "-%s", name);
    name = new_name.data();
  }

  // Call the system malloc's implementation for both external and our zones,
  // since that appropriately changes VM region protections on the zone.
  REAL(malloc_set_zone_name)(zone, name);
}

INTERCEPTOR(void *, malloc, size_t size) {
  COMMON_MALLOC_ENTER();
  COMMON_MALLOC_MALLOC(size);
  return p;
}

INTERCEPTOR(void, free, void *ptr) {
  COMMON_MALLOC_ENTER();
  if (!ptr) return;
  COMMON_MALLOC_FREE(ptr);
}

INTERCEPTOR(void *, realloc, void *ptr, size_t size) {
  COMMON_MALLOC_ENTER();
  COMMON_MALLOC_REALLOC(ptr, size);
  return p;
}

INTERCEPTOR(void *, calloc, size_t nmemb, size_t size) {
  COMMON_MALLOC_ENTER();
  COMMON_MALLOC_CALLOC(nmemb, size);
  return p;
}

INTERCEPTOR(void *, valloc, size_t size) {
  COMMON_MALLOC_ENTER();
  COMMON_MALLOC_VALLOC(size);
  return p;
}

INTERCEPTOR(size_t, malloc_good_size, size_t size) {
  COMMON_MALLOC_ENTER();
  return sanitizer_zone.introspect->good_size(&sanitizer_zone, size);
}

INTERCEPTOR(int, posix_memalign, void **memptr, size_t alignment, size_t size) {
  COMMON_MALLOC_ENTER();
  CHECK(memptr);
  COMMON_MALLOC_POSIX_MEMALIGN(memptr, alignment, size);
  return res;
}

namespace {

// TODO(glider): the __sanitizer_mz_* functions should be united with the Linux
// wrappers, as they are basically copied from there.
extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
size_t __sanitizer_mz_size(malloc_zone_t* zone, const void* ptr) {
  COMMON_MALLOC_SIZE(ptr);
  return size;
}

extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_mz_malloc(malloc_zone_t *zone, uptr size) {
  COMMON_MALLOC_ENTER();
  COMMON_MALLOC_MALLOC(size);
  return p;
}

struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !COMMON_MALLOC_SANITIZER_INITIALIZED; }
};

extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_mz_calloc(malloc_zone_t *zone, size_t nmemb, size_t size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);
  COMMON_MALLOC_CALLOC(nmemb, size);
  return p;
}

extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_mz_valloc(malloc_zone_t *zone, size_t size) {
  COMMON_MALLOC_ENTER();
  COMMON_MALLOC_VALLOC(size);
  return p;
}

// TODO(glider): the allocation callbacks need to be refactored.
extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_mz_free(malloc_zone_t *zone, void *ptr) {
  if (!ptr) return;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  COMMON_MALLOC_FREE(ptr);
}

#define GET_ZONE_FOR_PTR(ptr) \
  malloc_zone_t *zone_ptr = WRAP(malloc_zone_from_ptr)(ptr); \
  const char *zone_name = (zone_ptr == 0) ? 0 : zone_ptr->zone_name

extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_mz_realloc(malloc_zone_t *zone, void *ptr, size_t new_size) {
  if (!ptr) {
    COMMON_MALLOC_MALLOC(new_size);
    return p;
  } else {
    COMMON_MALLOC_SIZE(ptr);
    if (size) {
      COMMON_MALLOC_REALLOC(ptr, new_size);
      return p;
    } else {
      // We can't recover from reallocating an unknown address, because
      // this would require reading at most |new_size| bytes from
      // potentially unaccessible memory.
      GET_ZONE_FOR_PTR(ptr);
      COMMON_MALLOC_REPORT_UNKNOWN_REALLOC(ptr, zone_ptr, zone_name);
      return nullptr;
    }
  }
}

extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_mz_destroy(malloc_zone_t* zone) {
  // A no-op -- we will not be destroyed!
  Report("__sanitizer_mz_destroy() called -- ignoring\n");
}

extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
void *__sanitizer_mz_memalign(malloc_zone_t *zone, size_t align, size_t size) {
  COMMON_MALLOC_ENTER();
  COMMON_MALLOC_MEMALIGN(align, size);
  return p;
}

// This public API exists purely for testing purposes.
extern "C"
SANITIZER_INTERFACE_ATTRIBUTE
malloc_zone_t* __sanitizer_mz_default_zone() {
  return &sanitizer_zone;
}

// This function is currently unused, and we build with -Werror.
#if 0
void __sanitizer_mz_free_definite_size(
    malloc_zone_t* zone, void *ptr, size_t size) {
  // TODO(glider): check that |size| is valid.
  UNIMPLEMENTED();
}
#endif

#ifndef COMMON_MALLOC_HAS_ZONE_ENUMERATOR
#error "COMMON_MALLOC_HAS_ZONE_ENUMERATOR must be defined"
#endif
static_assert((COMMON_MALLOC_HAS_ZONE_ENUMERATOR) == 0 ||
                  (COMMON_MALLOC_HAS_ZONE_ENUMERATOR) == 1,
              "COMMON_MALLOC_HAS_ZONE_ENUMERATOR must be 0 or 1");

#if COMMON_MALLOC_HAS_ZONE_ENUMERATOR
// Forward declare and expect the implementation to provided by
// includer.
kern_return_t mi_enumerator(task_t task, void *, unsigned type_mask,
                            vm_address_t zone_address, memory_reader_t reader,
                            vm_range_recorder_t recorder);
#else
// Provide stub implementation that fails.
kern_return_t mi_enumerator(task_t task, void *, unsigned type_mask,
                            vm_address_t zone_address, memory_reader_t reader,
                            vm_range_recorder_t recorder) {
  // Not supported.
  return KERN_FAILURE;
}
#endif

#ifndef COMMON_MALLOC_HAS_EXTRA_INTROSPECTION_INIT
#error "COMMON_MALLOC_HAS_EXTRA_INTROSPECTION_INIT must be defined"
#endif
static_assert((COMMON_MALLOC_HAS_EXTRA_INTROSPECTION_INIT) == 0 ||
                  (COMMON_MALLOC_HAS_EXTRA_INTROSPECTION_INIT) == 1,
              "COMMON_MALLOC_HAS_EXTRA_INTROSPECTION_INIT must be 0 or 1");
#if COMMON_MALLOC_HAS_EXTRA_INTROSPECTION_INIT
// Forward declare and expect the implementation to provided by
// includer.
void mi_extra_init(
    sanitizer_malloc_introspection_t *mi);
#else
void mi_extra_init(
    sanitizer_malloc_introspection_t *mi) {
  // Just zero initialize the fields.
  mi->allocator_ptr = 0;
  mi->allocator_size = 0;
}
#endif

size_t mi_good_size(malloc_zone_t *zone, size_t size) {
  // I think it's always safe to return size, but we maybe could do better.
  return size;
}

boolean_t mi_check(malloc_zone_t *zone) {
  UNIMPLEMENTED();
}

void mi_print(malloc_zone_t *zone, boolean_t verbose) {
  UNIMPLEMENTED();
}

void mi_log(malloc_zone_t *zone, void *address) {
  // I don't think we support anything like this
}

void mi_force_lock(malloc_zone_t *zone) {
  COMMON_MALLOC_FORCE_LOCK();
}

void mi_force_unlock(malloc_zone_t *zone) {
  COMMON_MALLOC_FORCE_UNLOCK();
}

void mi_statistics(malloc_zone_t *zone, malloc_statistics_t *stats) {
  COMMON_MALLOC_FILL_STATS(zone, stats);
}

boolean_t mi_zone_locked(malloc_zone_t *zone) {
  // UNIMPLEMENTED();
  return false;
}

}  // unnamed namespace

namespace COMMON_MALLOC_NAMESPACE {

void InitMallocZoneFields() {
  static sanitizer_malloc_introspection_t sanitizer_zone_introspection;
  // Ok to use internal_memset, these places are not performance-critical.
  internal_memset(&sanitizer_zone_introspection, 0,
                  sizeof(sanitizer_zone_introspection));

  sanitizer_zone_introspection.enumerator = &mi_enumerator;
  sanitizer_zone_introspection.good_size = &mi_good_size;
  sanitizer_zone_introspection.check = &mi_check;
  sanitizer_zone_introspection.print = &mi_print;
  sanitizer_zone_introspection.log = &mi_log;
  sanitizer_zone_introspection.force_lock = &mi_force_lock;
  sanitizer_zone_introspection.force_unlock = &mi_force_unlock;
  sanitizer_zone_introspection.statistics = &mi_statistics;
  sanitizer_zone_introspection.zone_locked = &mi_zone_locked;

  // Set current allocator enumeration version.
  sanitizer_zone_introspection.allocator_enumeration_version =
      GetMallocZoneAllocatorEnumerationVersion();

  // Perform any sanitizer specific initialization.
  mi_extra_init(&sanitizer_zone_introspection);

  internal_memset(&sanitizer_zone, 0, sizeof(malloc_zone_t));

  // Use version 6 for OSX >= 10.6.
  sanitizer_zone.version = 6;
  sanitizer_zone.zone_name = COMMON_MALLOC_ZONE_NAME;
  sanitizer_zone.size = &__sanitizer_mz_size;
  sanitizer_zone.malloc = &__sanitizer_mz_malloc;
  sanitizer_zone.calloc = &__sanitizer_mz_calloc;
  sanitizer_zone.valloc = &__sanitizer_mz_valloc;
  sanitizer_zone.free = &__sanitizer_mz_free;
  sanitizer_zone.realloc = &__sanitizer_mz_realloc;
  sanitizer_zone.destroy = &__sanitizer_mz_destroy;
  sanitizer_zone.batch_malloc = 0;
  sanitizer_zone.batch_free = 0;
  sanitizer_zone.free_definite_size = 0;
  sanitizer_zone.memalign = &__sanitizer_mz_memalign;
  sanitizer_zone.introspect = &sanitizer_zone_introspection;
}

void ReplaceSystemMalloc() {
  InitMallocZoneFields();

  // Register the zone.
  malloc_zone_register(&sanitizer_zone);
}

}  // namespace COMMON_MALLOC_NAMESPACE
