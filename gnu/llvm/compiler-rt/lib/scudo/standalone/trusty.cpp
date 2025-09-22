//===-- trusty.cpp ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

#if SCUDO_TRUSTY

#include "common.h"
#include "mutex.h"
#include "report_linux.h"
#include "trusty.h"

#include <errno.h>           // for errno
#include <lk/err_ptr.h>      // for PTR_ERR and IS_ERR
#include <stdio.h>           // for printf()
#include <stdlib.h>          // for getenv()
#include <sys/auxv.h>        // for getauxval()
#include <time.h>            // for clock_gettime()
#include <trusty_err.h>      // for lk_err_to_errno()
#include <trusty_syscalls.h> // for _trusty_brk()
#include <uapi/mm.h>         // for MMAP flags

namespace scudo {

uptr getPageSize() { return getauxval(AT_PAGESZ); }

void NORETURN die() { abort(); }

void *map(void *Addr, uptr Size, const char *Name, uptr Flags,
          UNUSED MapPlatformData *Data) {
  uint32_t MmapFlags =
      MMAP_FLAG_ANONYMOUS | MMAP_FLAG_PROT_READ | MMAP_FLAG_PROT_WRITE;

  // If the MAP_NOACCESS flag is set, Scudo tries to reserve
  // a memory region without mapping physical pages. This corresponds
  // to MMAP_FLAG_NO_PHYSICAL in Trusty.
  if (Flags & MAP_NOACCESS)
    MmapFlags |= MMAP_FLAG_NO_PHYSICAL;
  if (Addr)
    MmapFlags |= MMAP_FLAG_FIXED_NOREPLACE;

  if (Flags & MAP_MEMTAG)
    MmapFlags |= MMAP_FLAG_PROT_MTE;

  void *P = (void *)_trusty_mmap(Addr, Size, MmapFlags, 0);

  if (IS_ERR(P)) {
    errno = lk_err_to_errno(PTR_ERR(P));
    if (!(Flags & MAP_ALLOWNOMEM) || errno != ENOMEM)
      reportMapError(Size);
    return nullptr;
  }

  return P;
}

void unmap(UNUSED void *Addr, UNUSED uptr Size, UNUSED uptr Flags,
           UNUSED MapPlatformData *Data) {
  if (_trusty_munmap(Addr, Size) != 0)
    reportUnmapError(reinterpret_cast<uptr>(Addr), Size);
}

void setMemoryPermission(UNUSED uptr Addr, UNUSED uptr Size, UNUSED uptr Flags,
                         UNUSED MapPlatformData *Data) {}

void releasePagesToOS(UNUSED uptr BaseAddress, UNUSED uptr Offset,
                      UNUSED uptr Size, UNUSED MapPlatformData *Data) {}

const char *getEnv(const char *Name) { return getenv(Name); }

// All mutex operations are a no-op since Trusty doesn't currently support
// threads.
bool HybridMutex::tryLock() { return true; }

void HybridMutex::lockSlow() {}

void HybridMutex::unlock() {}

void HybridMutex::assertHeldImpl() {}

u64 getMonotonicTime() {
  timespec TS;
  clock_gettime(CLOCK_MONOTONIC, &TS);
  return static_cast<u64>(TS.tv_sec) * (1000ULL * 1000 * 1000) +
         static_cast<u64>(TS.tv_nsec);
}

u64 getMonotonicTimeFast() {
#if defined(CLOCK_MONOTONIC_COARSE)
  timespec TS;
  clock_gettime(CLOCK_MONOTONIC_COARSE, &TS);
  return static_cast<u64>(TS.tv_sec) * (1000ULL * 1000 * 1000) +
         static_cast<u64>(TS.tv_nsec);
#else
  return getMonotonicTime();
#endif
}

u32 getNumberOfCPUs() { return 0; }

u32 getThreadID() { return 0; }

bool getRandom(UNUSED void *Buffer, UNUSED uptr Length, UNUSED bool Blocking) {
  return false;
}

void outputRaw(const char *Buffer) { printf("%s", Buffer); }

void setAbortMessage(UNUSED const char *Message) {}

} // namespace scudo

#endif // SCUDO_TRUSTY
