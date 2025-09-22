//=-- lsan_common_mac.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Implementation of common leak checking functionality. Darwin-specific code.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "lsan_common.h"

#if CAN_SANITIZE_LEAKS && SANITIZER_APPLE

#  include <mach/mach.h>
#  include <mach/vm_statistics.h>
#  include <pthread.h>

#  include "lsan_allocator.h"
#  include "sanitizer_common/sanitizer_allocator_internal.h"
namespace __lsan {

class ThreadContextLsanBase;

enum class SeenRegion {
  None = 0,
  AllocOnce = 1 << 0,
  LibDispatch = 1 << 1,
  Foundation = 1 << 2,
  All = AllocOnce | LibDispatch | Foundation
};

inline SeenRegion operator|(SeenRegion left, SeenRegion right) {
  return static_cast<SeenRegion>(static_cast<int>(left) |
                                 static_cast<int>(right));
}

inline SeenRegion &operator|=(SeenRegion &left, const SeenRegion &right) {
  left = left | right;
  return left;
}

struct RegionScanState {
  SeenRegion seen_regions = SeenRegion::None;
  bool in_libdispatch = false;
};

typedef struct {
  int disable_counter;
  ThreadContextLsanBase *current_thread;
  AllocatorCache cache;
} thread_local_data_t;

static pthread_key_t key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

// The main thread destructor requires the current thread,
// so we can't destroy it until it's been used and reset.
void restore_tid_data(void *ptr) {
  thread_local_data_t *data = (thread_local_data_t *)ptr;
  if (data->current_thread)
    pthread_setspecific(key, data);
}

static void make_tls_key() {
  CHECK_EQ(pthread_key_create(&key, restore_tid_data), 0);
}

static thread_local_data_t *get_tls_val(bool alloc) {
  pthread_once(&key_once, make_tls_key);

  thread_local_data_t *ptr = (thread_local_data_t *)pthread_getspecific(key);
  if (ptr == NULL && alloc) {
    ptr = (thread_local_data_t *)InternalAlloc(sizeof(*ptr));
    ptr->disable_counter = 0;
    ptr->current_thread = nullptr;
    ptr->cache = AllocatorCache();
    pthread_setspecific(key, ptr);
  }

  return ptr;
}

bool DisabledInThisThread() {
  thread_local_data_t *data = get_tls_val(false);
  return data ? data->disable_counter > 0 : false;
}

void DisableInThisThread() { ++get_tls_val(true)->disable_counter; }

void EnableInThisThread() {
  int *disable_counter = &get_tls_val(true)->disable_counter;
  if (*disable_counter == 0) {
    DisableCounterUnderflow();
  }
  --*disable_counter;
}

ThreadContextLsanBase *GetCurrentThread() {
  thread_local_data_t *data = get_tls_val(false);
  return data ? data->current_thread : nullptr;
}

void SetCurrentThread(ThreadContextLsanBase *tctx) {
  get_tls_val(true)->current_thread = tctx;
}

AllocatorCache *GetAllocatorCache() { return &get_tls_val(true)->cache; }

LoadedModule *GetLinker() { return nullptr; }

// Required on Linux for initialization of TLS behavior, but should not be
// required on Darwin.
void InitializePlatformSpecificModules() {}

// Sections which can't contain contain global pointers. This list errs on the
// side of caution to avoid false positives, at the expense of performance.
//
// Other potentially safe sections include:
// __all_image_info, __crash_info, __const, __got, __interpose, __objc_msg_break
//
// Sections which definitely cannot be included here are:
// __objc_data, __objc_const, __data, __bss, __common, __thread_data,
// __thread_bss, __thread_vars, __objc_opt_rw, __objc_opt_ptrs
static const char *kSkippedSecNames[] = {
    "__cfstring",       "__la_symbol_ptr",  "__mod_init_func",
    "__mod_term_func",  "__nl_symbol_ptr",  "__objc_classlist",
    "__objc_classrefs", "__objc_imageinfo", "__objc_nlclslist",
    "__objc_protolist", "__objc_selrefs",   "__objc_superrefs"};

// Scans global variables for heap pointers.
void ProcessGlobalRegions(Frontier *frontier) {
  for (auto name : kSkippedSecNames)
    CHECK(internal_strnlen(name, kMaxSegName + 1) <= kMaxSegName);

  MemoryMappingLayout memory_mapping(false);
  InternalMmapVector<LoadedModule> modules;
  modules.reserve(128);
  memory_mapping.DumpListOfModules(&modules);
  for (uptr i = 0; i < modules.size(); ++i) {
    // Even when global scanning is disabled, we still need to scan
    // system libraries for stashed pointers
    if (!flags()->use_globals && modules[i].instrumented()) continue;

    for (const __sanitizer::LoadedModule::AddressRange &range :
         modules[i].ranges()) {
      // Sections storing global variables are writable and non-executable
      if (range.executable || !range.writable) continue;

      for (auto name : kSkippedSecNames) {
        if (!internal_strcmp(range.name, name)) continue;
      }

      ScanGlobalRange(range.beg, range.end, frontier);
    }
  }
}

void ProcessPlatformSpecificAllocations(Frontier *frontier) {
  vm_address_t address = 0;
  kern_return_t err = KERN_SUCCESS;

  InternalMmapVector<Region> mapped_regions;
  bool use_root_regions = flags()->use_root_regions && HasRootRegions();

  RegionScanState scan_state;
  while (err == KERN_SUCCESS) {
    vm_size_t size = 0;
    unsigned depth = 1;
    struct vm_region_submap_info_64 info;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
    err = vm_region_recurse_64(mach_task_self(), &address, &size, &depth,
                               (vm_region_info_t)&info, &count);

    uptr end_address = address + size;
    if (info.user_tag == VM_MEMORY_OS_ALLOC_ONCE) {
      // libxpc stashes some pointers in the Kernel Alloc Once page,
      // make sure not to report those as leaks.
      scan_state.seen_regions |= SeenRegion::AllocOnce;
      ScanRangeForPointers(address, end_address, frontier, "GLOBAL",
                           kReachable);
    } else if (info.user_tag == VM_MEMORY_FOUNDATION) {
      // Objective-C block trampolines use the Foundation region.
      scan_state.seen_regions |= SeenRegion::Foundation;
      ScanRangeForPointers(address, end_address, frontier, "GLOBAL",
                           kReachable);
    } else if (info.user_tag == VM_MEMORY_LIBDISPATCH) {
      // Dispatch continuations use the libdispatch region. Empirically, there
      // can be more than one region with this tag, so we'll optimistically
      // assume that they're continguous. Otherwise, we would need to scan every
      // region to ensure we find them all.
      scan_state.in_libdispatch = true;
      ScanRangeForPointers(address, end_address, frontier, "GLOBAL",
                           kReachable);
    } else if (scan_state.in_libdispatch) {
      scan_state.seen_regions |= SeenRegion::LibDispatch;
      scan_state.in_libdispatch = false;
    }

    // Recursing over the full memory map is very slow, break out
    // early if we don't need the full iteration.
    if (scan_state.seen_regions == SeenRegion::All && !use_root_regions) {
      break;
    }

    // This additional root region scan is required on Darwin in order to
    // detect root regions contained within mmap'd memory regions, because
    // the Darwin implementation of sanitizer_procmaps traverses images
    // as loaded by dyld, and not the complete set of all memory regions.
    //
    // TODO(fjricci) - remove this once sanitizer_procmaps_mac has the same
    // behavior as sanitizer_procmaps_linux and traverses all memory regions
    if (use_root_regions && (info.protection & kProtectionRead))
      mapped_regions.push_back({address, end_address});

    address = end_address;
  }
  ScanRootRegions(frontier, mapped_regions);
}

// On darwin, we can intercept _exit gracefully, and return a failing exit code
// if required at that point. Calling Die() here is undefined behavior and
// causes rare race conditions.
void HandleLeaks() {}

void LockStuffAndStopTheWorld(StopTheWorldCallback callback,
                              CheckForLeaksParam *argument) {
  ScopedStopTheWorldLock lock;
  StopTheWorld(callback, argument);
}

}  // namespace __lsan

#endif // CAN_SANITIZE_LEAKS && SANITIZER_APPLE
