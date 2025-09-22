//=-- lsan_common_linux.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Implementation of common leak checking functionality. Linux/NetBSD-specific
// code.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#include "lsan_common.h"

#if CAN_SANITIZE_LEAKS && (SANITIZER_LINUX || SANITIZER_NETBSD)
#include <link.h>

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_getauxval.h"
#include "sanitizer_common/sanitizer_linux.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

namespace __lsan {

static const char kLinkerName[] = "ld";

alignas(64) static char linker_placeholder[sizeof(LoadedModule)];
static LoadedModule *linker = nullptr;

static bool IsLinker(const LoadedModule& module) {
#if SANITIZER_USE_GETAUXVAL
  return module.base_address() == getauxval(AT_BASE);
#else
  return LibraryNameIs(module.full_name(), kLinkerName);
#endif  // SANITIZER_USE_GETAUXVAL
}

__attribute__((tls_model("initial-exec")))
THREADLOCAL int disable_counter;
bool DisabledInThisThread() { return disable_counter > 0; }
void DisableInThisThread() { disable_counter++; }
void EnableInThisThread() {
  if (disable_counter == 0) {
    DisableCounterUnderflow();
  }
  disable_counter--;
}

void InitializePlatformSpecificModules() {
  ListOfModules modules;
  modules.init();
  for (LoadedModule &module : modules) {
    if (!IsLinker(module))
      continue;
    if (linker == nullptr) {
      linker = reinterpret_cast<LoadedModule *>(linker_placeholder);
      *linker = module;
      module = LoadedModule();
    } else {
      VReport(1, "LeakSanitizer: Multiple modules match \"%s\". "
              "TLS and other allocations originating from linker might be "
              "falsely reported as leaks.\n", kLinkerName);
      linker->clear();
      linker = nullptr;
      return;
    }
  }
  if (linker == nullptr) {
    VReport(1, "LeakSanitizer: Dynamic linker not found. TLS and other "
               "allocations originating from linker might be falsely reported "
                "as leaks.\n");
  }
}

static int ProcessGlobalRegionsCallback(struct dl_phdr_info *info, size_t size,
                                        void *data) {
  Frontier *frontier = reinterpret_cast<Frontier *>(data);
  for (uptr j = 0; j < info->dlpi_phnum; j++) {
    const ElfW(Phdr) *phdr = &(info->dlpi_phdr[j]);
    // We're looking for .data and .bss sections, which reside in writeable,
    // loadable segments.
    if (!(phdr->p_flags & PF_W) || (phdr->p_type != PT_LOAD) ||
        (phdr->p_memsz == 0))
      continue;
    uptr begin = info->dlpi_addr + phdr->p_vaddr;
    uptr end = begin + phdr->p_memsz;
    ScanGlobalRange(begin, end, frontier);
  }
  return 0;
}

#if SANITIZER_ANDROID && __ANDROID_API__ < 21
extern "C" __attribute__((weak)) int dl_iterate_phdr(
    int (*)(struct dl_phdr_info *, size_t, void *), void *);
#endif

// Scans global variables for heap pointers.
void ProcessGlobalRegions(Frontier *frontier) {
  if (!flags()->use_globals) return;
  dl_iterate_phdr(ProcessGlobalRegionsCallback, frontier);
}

LoadedModule *GetLinker() { return linker; }

void ProcessPlatformSpecificAllocations(Frontier *frontier) {}

struct DoStopTheWorldParam {
  StopTheWorldCallback callback;
  void *argument;
};

// While calling Die() here is undefined behavior and can potentially
// cause race conditions, it isn't possible to intercept exit on linux,
// so we have no choice but to call Die() from the atexit handler.
void HandleLeaks() {
  if (common_flags()->exitcode) Die();
}

static int LockStuffAndStopTheWorldCallback(struct dl_phdr_info *info,
                                            size_t size, void *data) {
  ScopedStopTheWorldLock lock;
  DoStopTheWorldParam *param = reinterpret_cast<DoStopTheWorldParam *>(data);
  StopTheWorld(param->callback, param->argument);
  return 1;
}

// LSan calls dl_iterate_phdr() from the tracer task. This may deadlock: if one
// of the threads is frozen while holding the libdl lock, the tracer will hang
// in dl_iterate_phdr() forever.
// Luckily, (a) the lock is reentrant and (b) libc can't distinguish between the
// tracer task and the thread that spawned it. Thus, if we run the tracer task
// while holding the libdl lock in the parent thread, we can safely reenter it
// in the tracer. The solution is to run stoptheworld from a dl_iterate_phdr()
// callback in the parent thread.
void LockStuffAndStopTheWorld(StopTheWorldCallback callback,
                              CheckForLeaksParam *argument) {
  DoStopTheWorldParam param = {callback, argument};
  dl_iterate_phdr(LockStuffAndStopTheWorldCallback, &param);
}

} // namespace __lsan

#endif
