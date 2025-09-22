//===-- mem_map_linux.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

#if SCUDO_LINUX

#include "mem_map_linux.h"

#include "common.h"
#include "internal_defs.h"
#include "linux.h"
#include "mutex.h"
#include "report_linux.h"
#include "string_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#if SCUDO_ANDROID
// TODO(chiahungduan): Review if we still need the followings macros.
#include <sys/prctl.h>
// Definitions of prctl arguments to set a vma name in Android kernels.
#define ANDROID_PR_SET_VMA 0x53564d41
#define ANDROID_PR_SET_VMA_ANON_NAME 0
#endif

namespace scudo {

static void *mmapWrapper(uptr Addr, uptr Size, const char *Name, uptr Flags) {
  int MmapFlags = MAP_PRIVATE | MAP_ANONYMOUS;
  int MmapProt;
  if (Flags & MAP_NOACCESS) {
    MmapFlags |= MAP_NORESERVE;
    MmapProt = PROT_NONE;
  } else {
    MmapProt = PROT_READ | PROT_WRITE;
  }
#if defined(__aarch64__)
#ifndef PROT_MTE
#define PROT_MTE 0x20
#endif
  if (Flags & MAP_MEMTAG)
    MmapProt |= PROT_MTE;
#endif
  if (Addr)
    MmapFlags |= MAP_FIXED;
  void *P =
      mmap(reinterpret_cast<void *>(Addr), Size, MmapProt, MmapFlags, -1, 0);
  if (P == MAP_FAILED) {
    if (!(Flags & MAP_ALLOWNOMEM) || errno != ENOMEM)
      reportMapError(errno == ENOMEM ? Size : 0);
    return nullptr;
  }
#if SCUDO_ANDROID
  if (Name)
    prctl(ANDROID_PR_SET_VMA, ANDROID_PR_SET_VMA_ANON_NAME, P, Size, Name);
#else
  (void)Name;
#endif

  return P;
}

bool MemMapLinux::mapImpl(uptr Addr, uptr Size, const char *Name, uptr Flags) {
  void *P = mmapWrapper(Addr, Size, Name, Flags);
  if (P == nullptr)
    return false;

  MapBase = reinterpret_cast<uptr>(P);
  MapCapacity = Size;
  return true;
}

void MemMapLinux::unmapImpl(uptr Addr, uptr Size) {
  // If we unmap all the pages, also mark `MapBase` to 0 to indicate invalid
  // status.
  if (Size == MapCapacity) {
    MapBase = MapCapacity = 0;
  } else {
    // This is partial unmap and is unmapping the pages from the beginning,
    // shift `MapBase` to the new base.
    if (MapBase == Addr)
      MapBase = Addr + Size;
    MapCapacity -= Size;
  }

  if (munmap(reinterpret_cast<void *>(Addr), Size) != 0)
    reportUnmapError(Addr, Size);
}

bool MemMapLinux::remapImpl(uptr Addr, uptr Size, const char *Name,
                            uptr Flags) {
  void *P = mmapWrapper(Addr, Size, Name, Flags);
  if (reinterpret_cast<uptr>(P) != Addr)
    reportMapError();
  return true;
}

void MemMapLinux::setMemoryPermissionImpl(uptr Addr, uptr Size, uptr Flags) {
  int Prot = (Flags & MAP_NOACCESS) ? PROT_NONE : (PROT_READ | PROT_WRITE);
  if (mprotect(reinterpret_cast<void *>(Addr), Size, Prot) != 0)
    reportProtectError(Addr, Size, Prot);
}

void MemMapLinux::releaseAndZeroPagesToOSImpl(uptr From, uptr Size) {
  void *Addr = reinterpret_cast<void *>(From);

  while (madvise(Addr, Size, MADV_DONTNEED) == -1 && errno == EAGAIN) {
  }
}

bool ReservedMemoryLinux::createImpl(uptr Addr, uptr Size, const char *Name,
                                     uptr Flags) {
  ReservedMemoryLinux::MemMapT MemMap;
  if (!MemMap.map(Addr, Size, Name, Flags | MAP_NOACCESS))
    return false;

  MapBase = MemMap.getBase();
  MapCapacity = MemMap.getCapacity();

  return true;
}

void ReservedMemoryLinux::releaseImpl() {
  if (munmap(reinterpret_cast<void *>(getBase()), getCapacity()) != 0)
    reportUnmapError(getBase(), getCapacity());
}

ReservedMemoryLinux::MemMapT ReservedMemoryLinux::dispatchImpl(uptr Addr,
                                                               uptr Size) {
  return ReservedMemoryLinux::MemMapT(Addr, Size);
}

} // namespace scudo

#endif // SCUDO_LINUX
