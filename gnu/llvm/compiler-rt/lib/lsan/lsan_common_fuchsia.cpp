//=-- lsan_common_fuchsia.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Implementation of common leak checking functionality. Fuchsia-specific code.
//
//===---------------------------------------------------------------------===//

#include "lsan_common.h"
#include "lsan_thread.h"
#include "sanitizer_common/sanitizer_platform.h"

#if CAN_SANITIZE_LEAKS && SANITIZER_FUCHSIA
#include <zircon/sanitizer.h>

#include "lsan_allocator.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_stoptheworld_fuchsia.h"
#include "sanitizer_common/sanitizer_thread_registry.h"

// Ensure that the Zircon system ABI is linked in.
#pragma comment(lib, "zircon")

namespace __lsan {

void InitializePlatformSpecificModules() {}

LoadedModule *GetLinker() { return nullptr; }

__attribute__((tls_model("initial-exec"))) THREADLOCAL int disable_counter;
bool DisabledInThisThread() { return disable_counter > 0; }
void DisableInThisThread() { disable_counter++; }
void EnableInThisThread() {
  if (disable_counter == 0) {
    DisableCounterUnderflow();
  }
  disable_counter--;
}

// There is nothing left to do after the globals callbacks.
void ProcessGlobalRegions(Frontier *frontier) {}

// Nothing to do here.
void ProcessPlatformSpecificAllocations(Frontier *frontier) {}

// On Fuchsia, we can intercept _Exit gracefully, and return a failing exit
// code if required at that point.  Calling Die() here is undefined
// behavior and causes rare race conditions.
void HandleLeaks() {}

// This is defined differently in asan_fuchsia.cpp and lsan_fuchsia.cpp.
bool UseExitcodeOnLeak();

int ExitHook(int status) {
  if (common_flags()->detect_leaks && common_flags()->leak_check_at_exit) {
    if (UseExitcodeOnLeak())
      DoLeakCheck();
    else
      DoRecoverableLeakCheckVoid();
  }
  return status == 0 && HasReportedLeaks() ? common_flags()->exitcode : status;
}

void LockStuffAndStopTheWorld(StopTheWorldCallback callback,
                              CheckForLeaksParam *argument) {
  ScopedStopTheWorldLock lock;

  struct Params {
    InternalMmapVector<uptr> allocator_caches;
    StopTheWorldCallback callback;
    CheckForLeaksParam *argument;
  } params = {{}, callback, argument};

  // Callback from libc for globals (data/bss modulo relro), when enabled.
  auto globals = +[](void *chunk, size_t size, void *data) {
    auto params = static_cast<const Params *>(data);
    uptr begin = reinterpret_cast<uptr>(chunk);
    uptr end = begin + size;
    ScanGlobalRange(begin, end, &params->argument->frontier);
  };

  // Callback from libc for thread stacks.
  auto stacks = +[](void *chunk, size_t size, void *data) {
    auto params = static_cast<const Params *>(data);
    uptr begin = reinterpret_cast<uptr>(chunk);
    uptr end = begin + size;
    ScanRangeForPointers(begin, end, &params->argument->frontier, "STACK",
                         kReachable);
  };

  // Callback from libc for thread registers.
  auto registers = +[](void *chunk, size_t size, void *data) {
    auto params = static_cast<const Params *>(data);
    uptr begin = reinterpret_cast<uptr>(chunk);
    uptr end = begin + size;
    ScanRangeForPointers(begin, end, &params->argument->frontier, "REGISTERS",
                         kReachable);
  };

  if (flags()->use_tls) {
    // Collect the allocator cache range from each thread so these
    // can all be excluded from the reported TLS ranges.
    GetAllThreadAllocatorCachesLocked(&params.allocator_caches);
    __sanitizer::Sort(params.allocator_caches.data(),
                      params.allocator_caches.size());
  }

  // Callback from libc for TLS regions.  This includes thread_local
  // variables as well as C11 tss_set and POSIX pthread_setspecific.
  auto tls = +[](void *chunk, size_t size, void *data) {
    auto params = static_cast<const Params *>(data);
    uptr begin = reinterpret_cast<uptr>(chunk);
    uptr end = begin + size;
    auto i = __sanitizer::InternalLowerBound(params->allocator_caches, begin);
    if (i < params->allocator_caches.size() &&
        params->allocator_caches[i] >= begin &&
        params->allocator_caches[i] <= end &&
        end - params->allocator_caches[i] >= sizeof(AllocatorCache)) {
      // Split the range in two and omit the allocator cache within.
      ScanRangeForPointers(begin, params->allocator_caches[i],
                           &params->argument->frontier, "TLS", kReachable);
      uptr begin2 = params->allocator_caches[i] + sizeof(AllocatorCache);
      ScanRangeForPointers(begin2, end, &params->argument->frontier, "TLS",
                           kReachable);
    } else {
      ScanRangeForPointers(begin, end, &params->argument->frontier, "TLS",
                           kReachable);
    }
  };

  // This stops the world and then makes callbacks for various memory regions.
  // The final callback is the last thing before the world starts up again.
  __sanitizer_memory_snapshot(
      flags()->use_globals ? globals : nullptr,
      flags()->use_stacks ? stacks : nullptr,
      flags()->use_registers ? registers : nullptr,
      flags()->use_tls ? tls : nullptr,
      [](zx_status_t, void *data) {
        auto params = static_cast<const Params *>(data);

        // We don't use the thread registry at all for enumerating the threads
        // and their stacks, registers, and TLS regions.  So use it separately
        // just for the allocator cache, and to call ScanExtraStackRanges,
        // which ASan needs.
        if (flags()->use_stacks) {
          InternalMmapVector<Range> ranges;
          GetThreadExtraStackRangesLocked(&ranges);
          ScanExtraStackRanges(ranges, &params->argument->frontier);
        }
        params->callback(SuspendedThreadsListFuchsia(), params->argument);
      },
      &params);
}

}  // namespace __lsan

// This is declared (in extern "C") by <zircon/sanitizer.h>.
// _Exit calls this directly to intercept and change the status value.
int __sanitizer_process_exit_hook(int status) {
  return __lsan::ExitHook(status);
}

#endif
