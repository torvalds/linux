//===-- linux.cpp -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

#if SCUDO_LINUX

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
#include <sys/prctl.h>
// Definitions of prctl arguments to set a vma name in Android kernels.
#define ANDROID_PR_SET_VMA 0x53564d41
#define ANDROID_PR_SET_VMA_ANON_NAME 0
#endif

namespace scudo {

uptr getPageSize() { return static_cast<uptr>(sysconf(_SC_PAGESIZE)); }

void NORETURN die() { abort(); }

// TODO: Will be deprecated. Use the interfaces in MemMapLinux instead.
void *map(void *Addr, uptr Size, UNUSED const char *Name, uptr Flags,
          UNUSED MapPlatformData *Data) {
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
  void *P = mmap(Addr, Size, MmapProt, MmapFlags, -1, 0);
  if (P == MAP_FAILED) {
    if (!(Flags & MAP_ALLOWNOMEM) || errno != ENOMEM)
      reportMapError(errno == ENOMEM ? Size : 0);
    return nullptr;
  }
#if SCUDO_ANDROID
  if (Name)
    prctl(ANDROID_PR_SET_VMA, ANDROID_PR_SET_VMA_ANON_NAME, P, Size, Name);
#endif
  return P;
}

// TODO: Will be deprecated. Use the interfaces in MemMapLinux instead.
void unmap(void *Addr, uptr Size, UNUSED uptr Flags,
           UNUSED MapPlatformData *Data) {
  if (munmap(Addr, Size) != 0)
    reportUnmapError(reinterpret_cast<uptr>(Addr), Size);
}

// TODO: Will be deprecated. Use the interfaces in MemMapLinux instead.
void setMemoryPermission(uptr Addr, uptr Size, uptr Flags,
                         UNUSED MapPlatformData *Data) {
  int Prot = (Flags & MAP_NOACCESS) ? PROT_NONE : (PROT_READ | PROT_WRITE);
  if (mprotect(reinterpret_cast<void *>(Addr), Size, Prot) != 0)
    reportProtectError(Addr, Size, Prot);
}

// TODO: Will be deprecated. Use the interfaces in MemMapLinux instead.
void releasePagesToOS(uptr BaseAddress, uptr Offset, uptr Size,
                      UNUSED MapPlatformData *Data) {
  void *Addr = reinterpret_cast<void *>(BaseAddress + Offset);

  while (madvise(Addr, Size, MADV_DONTNEED) == -1 && errno == EAGAIN) {
  }
}

// Calling getenv should be fine (c)(tm) at any time.
const char *getEnv(const char *Name) { return getenv(Name); }

namespace {
enum State : u32 { Unlocked = 0, Locked = 1, Sleeping = 2 };
}

bool HybridMutex::tryLock() {
  return atomic_compare_exchange_strong(&M, Unlocked, Locked,
                                        memory_order_acquire) == Unlocked;
}

// The following is based on https://akkadia.org/drepper/futex.pdf.
void HybridMutex::lockSlow() {
  u32 V = atomic_compare_exchange_strong(&M, Unlocked, Locked,
                                         memory_order_acquire);
  if (V == Unlocked)
    return;
  if (V != Sleeping)
    V = atomic_exchange(&M, Sleeping, memory_order_acquire);
  while (V != Unlocked) {
    syscall(SYS_futex, reinterpret_cast<uptr>(&M), FUTEX_WAIT_PRIVATE, Sleeping,
            nullptr, nullptr, 0);
    V = atomic_exchange(&M, Sleeping, memory_order_acquire);
  }
}

void HybridMutex::unlock() {
  if (atomic_fetch_sub(&M, 1U, memory_order_release) != Locked) {
    atomic_store(&M, Unlocked, memory_order_release);
    syscall(SYS_futex, reinterpret_cast<uptr>(&M), FUTEX_WAKE_PRIVATE, 1,
            nullptr, nullptr, 0);
  }
}

void HybridMutex::assertHeldImpl() {
  CHECK(atomic_load(&M, memory_order_acquire) != Unlocked);
}

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

u32 getNumberOfCPUs() {
  cpu_set_t CPUs;
  // sched_getaffinity can fail for a variety of legitimate reasons (lack of
  // CAP_SYS_NICE, syscall filtering, etc), in which case we shall return 0.
  if (sched_getaffinity(0, sizeof(cpu_set_t), &CPUs) != 0)
    return 0;
  return static_cast<u32>(CPU_COUNT(&CPUs));
}

u32 getThreadID() {
#if SCUDO_ANDROID
  return static_cast<u32>(gettid());
#else
  return static_cast<u32>(syscall(SYS_gettid));
#endif
}

// Blocking is possibly unused if the getrandom block is not compiled in.
bool getRandom(void *Buffer, uptr Length, UNUSED bool Blocking) {
  if (!Buffer || !Length || Length > MaxRandomLength)
    return false;
  ssize_t ReadBytes;
#if defined(SYS_getrandom)
#if !defined(GRND_NONBLOCK)
#define GRND_NONBLOCK 1
#endif
  // Up to 256 bytes, getrandom will not be interrupted.
  ReadBytes =
      syscall(SYS_getrandom, Buffer, Length, Blocking ? 0 : GRND_NONBLOCK);
  if (ReadBytes == static_cast<ssize_t>(Length))
    return true;
#endif // defined(SYS_getrandom)
  // Up to 256 bytes, a read off /dev/urandom will not be interrupted.
  // Blocking is moot here, O_NONBLOCK has no effect when opening /dev/urandom.
  const int FileDesc = open("/dev/urandom", O_RDONLY);
  if (FileDesc == -1)
    return false;
  ReadBytes = read(FileDesc, Buffer, Length);
  close(FileDesc);
  return (ReadBytes == static_cast<ssize_t>(Length));
}

// Allocation free syslog-like API.
extern "C" WEAK int async_safe_write_log(int pri, const char *tag,
                                         const char *msg);

void outputRaw(const char *Buffer) {
  if (&async_safe_write_log) {
    constexpr s32 AndroidLogInfo = 4;
    constexpr uptr MaxLength = 1024U;
    char LocalBuffer[MaxLength];
    while (strlen(Buffer) > MaxLength) {
      uptr P;
      for (P = MaxLength - 1; P > 0; P--) {
        if (Buffer[P] == '\n') {
          memcpy(LocalBuffer, Buffer, P);
          LocalBuffer[P] = '\0';
          async_safe_write_log(AndroidLogInfo, "scudo", LocalBuffer);
          Buffer = &Buffer[P + 1];
          break;
        }
      }
      // If no newline was found, just log the buffer.
      if (P == 0)
        break;
    }
    async_safe_write_log(AndroidLogInfo, "scudo", Buffer);
  } else {
    (void)write(2, Buffer, strlen(Buffer));
  }
}

extern "C" WEAK void android_set_abort_message(const char *);

void setAbortMessage(const char *Message) {
  if (&android_set_abort_message)
    android_set_abort_message(Message);
}

} // namespace scudo

#endif // SCUDO_LINUX
