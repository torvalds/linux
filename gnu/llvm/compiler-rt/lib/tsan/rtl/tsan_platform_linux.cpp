//===-- tsan_platform_linux.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Linux- and BSD-specific code.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_NETBSD

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_linux.h"
#include "sanitizer_common/sanitizer_platform_limits_netbsd.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"
#include "sanitizer_common/sanitizer_posix.h"
#include "sanitizer_common/sanitizer_procmaps.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stoptheworld.h"
#include "tsan_flags.h"
#include "tsan_platform.h"
#include "tsan_rtl.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#if SANITIZER_LINUX
#include <sys/personality.h>
#include <setjmp.h>
#endif
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
#include <dlfcn.h>
#if SANITIZER_LINUX
#define __need_res_state
#include <resolv.h>
#endif

#ifdef sa_handler
# undef sa_handler
#endif

#ifdef sa_sigaction
# undef sa_sigaction
#endif

#if SANITIZER_FREEBSD
extern "C" void *__libc_stack_end;
void *__libc_stack_end = 0;
#endif

#if SANITIZER_LINUX && (defined(__aarch64__) || defined(__loongarch_lp64)) && \
    !SANITIZER_GO
# define INIT_LONGJMP_XOR_KEY 1
#else
# define INIT_LONGJMP_XOR_KEY 0
#endif

#if INIT_LONGJMP_XOR_KEY
#include "interception/interception.h"
// Must be declared outside of other namespaces.
DECLARE_REAL(int, _setjmp, void *env)
#endif

namespace __tsan {

#if INIT_LONGJMP_XOR_KEY
static void InitializeLongjmpXorKey();
static uptr longjmp_xor_key;
#endif

// Runtime detected VMA size.
uptr vmaSize;

enum {
  MemTotal,
  MemShadow,
  MemMeta,
  MemFile,
  MemMmap,
  MemHeap,
  MemOther,
  MemCount,
};

void FillProfileCallback(uptr p, uptr rss, bool file, uptr *mem) {
  mem[MemTotal] += rss;
  if (p >= ShadowBeg() && p < ShadowEnd())
    mem[MemShadow] += rss;
  else if (p >= MetaShadowBeg() && p < MetaShadowEnd())
    mem[MemMeta] += rss;
  else if ((p >= LoAppMemBeg() && p < LoAppMemEnd()) ||
           (p >= MidAppMemBeg() && p < MidAppMemEnd()) ||
           (p >= HiAppMemBeg() && p < HiAppMemEnd()))
    mem[file ? MemFile : MemMmap] += rss;
  else if (p >= HeapMemBeg() && p < HeapMemEnd())
    mem[MemHeap] += rss;
  else
    mem[MemOther] += rss;
}

void WriteMemoryProfile(char *buf, uptr buf_size, u64 uptime_ns) {
  uptr mem[MemCount];
  internal_memset(mem, 0, sizeof(mem));
  GetMemoryProfile(FillProfileCallback, mem);
  auto meta = ctx->metamap.GetMemoryStats();
  StackDepotStats stacks = StackDepotGetStats();
  uptr nthread, nlive;
  ctx->thread_registry.GetNumberOfThreads(&nthread, &nlive);
  uptr trace_mem;
  {
    Lock l(&ctx->slot_mtx);
    trace_mem = ctx->trace_part_total_allocated * sizeof(TracePart);
  }
  uptr internal_stats[AllocatorStatCount];
  internal_allocator()->GetStats(internal_stats);
  // All these are allocated from the common mmap region.
  mem[MemMmap] -= meta.mem_block + meta.sync_obj + trace_mem +
                  stacks.allocated + internal_stats[AllocatorStatMapped];
  if (s64(mem[MemMmap]) < 0)
    mem[MemMmap] = 0;
  internal_snprintf(
      buf, buf_size,
      "==%zu== %llus [%zu]: RSS %zd MB: shadow:%zd meta:%zd file:%zd"
      " mmap:%zd heap:%zd other:%zd intalloc:%zd memblocks:%zd syncobj:%zu"
      " trace:%zu stacks=%zd threads=%zu/%zu\n",
      internal_getpid(), uptime_ns / (1000 * 1000 * 1000), ctx->global_epoch,
      mem[MemTotal] >> 20, mem[MemShadow] >> 20, mem[MemMeta] >> 20,
      mem[MemFile] >> 20, mem[MemMmap] >> 20, mem[MemHeap] >> 20,
      mem[MemOther] >> 20, internal_stats[AllocatorStatMapped] >> 20,
      meta.mem_block >> 20, meta.sync_obj >> 20, trace_mem >> 20,
      stacks.allocated >> 20, nlive, nthread);
}

#if !SANITIZER_GO
// Mark shadow for .rodata sections with the special Shadow::kRodata marker.
// Accesses to .rodata can't race, so this saves time, memory and trace space.
static NOINLINE void MapRodata(char* buffer, uptr size) {
  // First create temp file.
  const char *tmpdir = GetEnv("TMPDIR");
  if (tmpdir == 0)
    tmpdir = GetEnv("TEST_TMPDIR");
#ifdef P_tmpdir
  if (tmpdir == 0)
    tmpdir = P_tmpdir;
#endif
  if (tmpdir == 0)
    return;
  internal_snprintf(buffer, size, "%s/tsan.rodata.%d",
                    tmpdir, (int)internal_getpid());
  uptr openrv = internal_open(buffer, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (internal_iserror(openrv))
    return;
  internal_unlink(buffer);  // Unlink it now, so that we can reuse the buffer.
  fd_t fd = openrv;
  // Fill the file with Shadow::kRodata.
  const uptr kMarkerSize = 512 * 1024 / sizeof(RawShadow);
  InternalMmapVector<RawShadow> marker(kMarkerSize);
  // volatile to prevent insertion of memset
  for (volatile RawShadow *p = marker.data(); p < marker.data() + kMarkerSize;
       p++)
    *p = Shadow::kRodata;
  internal_write(fd, marker.data(), marker.size() * sizeof(RawShadow));
  // Map the file into memory.
  uptr page = internal_mmap(0, GetPageSizeCached(), PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);
  if (internal_iserror(page)) {
    internal_close(fd);
    return;
  }
  // Map the file into shadow of .rodata sections.
  MemoryMappingLayout proc_maps(/*cache_enabled*/true);
  // Reusing the buffer 'buffer'.
  MemoryMappedSegment segment(buffer, size);
  while (proc_maps.Next(&segment)) {
    if (segment.filename[0] != 0 && segment.filename[0] != '[' &&
        segment.IsReadable() && segment.IsExecutable() &&
        !segment.IsWritable() && IsAppMem(segment.start)) {
      // Assume it's .rodata
      char *shadow_start = (char *)MemToShadow(segment.start);
      char *shadow_end = (char *)MemToShadow(segment.end);
      for (char *p = shadow_start; p < shadow_end;
           p += marker.size() * sizeof(RawShadow)) {
        internal_mmap(
            p, Min<uptr>(marker.size() * sizeof(RawShadow), shadow_end - p),
            PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0);
      }
    }
  }
  internal_close(fd);
}

void InitializeShadowMemoryPlatform() {
  char buffer[256];  // Keep in a different frame.
  MapRodata(buffer, sizeof(buffer));
}

#endif  // #if !SANITIZER_GO

#  if !SANITIZER_GO
static void ReExecIfNeeded(bool ignore_heap) {
  // Go maps shadow memory lazily and works fine with limited address space.
  // Unlimited stack is not a problem as well, because the executable
  // is not compiled with -pie.
  bool reexec = false;
  // TSan doesn't play well with unlimited stack size (as stack
  // overlaps with shadow memory). If we detect unlimited stack size,
  // we re-exec the program with limited stack size as a best effort.
  if (StackSizeIsUnlimited()) {
    const uptr kMaxStackSize = 32 * 1024 * 1024;
    VReport(1,
            "Program is run with unlimited stack size, which wouldn't "
            "work with ThreadSanitizer.\n"
            "Re-execing with stack size limited to %zd bytes.\n",
            kMaxStackSize);
    SetStackSizeLimitInBytes(kMaxStackSize);
    reexec = true;
  }

  if (!AddressSpaceIsUnlimited()) {
    Report(
        "WARNING: Program is run with limited virtual address space,"
        " which wouldn't work with ThreadSanitizer.\n");
    Report("Re-execing with unlimited virtual address space.\n");
    SetAddressSpaceUnlimited();
    reexec = true;
  }

#    if SANITIZER_LINUX
#      if SANITIZER_ANDROID && (defined(__aarch64__) || defined(__x86_64__))
  // ASLR personality check.
  int old_personality = personality(0xffffffff);
  bool aslr_on =
      (old_personality != -1) && ((old_personality & ADDR_NO_RANDOMIZE) == 0);

  // After patch "arm64: mm: support ARCH_MMAP_RND_BITS." is introduced in
  // linux kernel, the random gap between stack and mapped area is increased
  // from 128M to 36G on 39-bit aarch64. As it is almost impossible to cover
  // this big range, we should disable randomized virtual space on aarch64.
  if (aslr_on) {
    VReport(1,
            "WARNING: Program is run with randomized virtual address "
            "space, which wouldn't work with ThreadSanitizer on Android.\n"
            "Re-execing with fixed virtual address space.\n");
    CHECK_NE(personality(old_personality | ADDR_NO_RANDOMIZE), -1);
    reexec = true;
  }
#      endif

  if (reexec) {
    // Don't check the address space since we're going to re-exec anyway.
  } else if (!CheckAndProtect(false, ignore_heap, false)) {
    // ASLR personality check.
    // N.B. 'personality' is sometimes forbidden by sandboxes, so we only call
    // this as a last resort (when the memory mapping is incompatible and TSan
    // would fail anyway).
    int old_personality = personality(0xffffffff);
    bool aslr_on =
        (old_personality != -1) && ((old_personality & ADDR_NO_RANDOMIZE) == 0);

    if (aslr_on) {
      // Disable ASLR if the memory layout was incompatible.
      // Alternatively, we could just keep re-execing until we get lucky
      // with a compatible randomized layout, but the risk is that if it's
      // not an ASLR-related issue, we will be stuck in an infinite loop of
      // re-execing (unless we change ReExec to pass a parameter of the
      // number of retries allowed.)
      VReport(1,
              "WARNING: ThreadSanitizer: memory layout is incompatible, "
              "possibly due to high-entropy ASLR.\n"
              "Re-execing with fixed virtual address space.\n"
              "N.B. reducing ASLR entropy is preferable.\n");
      CHECK_NE(personality(old_personality | ADDR_NO_RANDOMIZE), -1);
      reexec = true;
    } else {
      Printf(
          "FATAL: ThreadSanitizer: memory layout is incompatible, "
          "even though ASLR is disabled.\n"
          "Please file a bug.\n");
      DumpProcessMap();
      Die();
    }
  }
#    endif  // SANITIZER_LINUX

  if (reexec)
    ReExec();
}
#  endif

void InitializePlatformEarly() {
  vmaSize =
    (MostSignificantSetBitIndex(GET_CURRENT_FRAME()) + 1);
#if defined(__aarch64__)
# if !SANITIZER_GO
  if (vmaSize != 39 && vmaSize != 42 && vmaSize != 48) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 39, 42 and 48\n", vmaSize);
    Die();
  }
#else
  if (vmaSize != 48) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 48\n", vmaSize);
    Die();
  }
#endif
#elif SANITIZER_LOONGARCH64
# if !SANITIZER_GO
  if (vmaSize != 47) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 47\n", vmaSize);
    Die();
  }
#    else
  if (vmaSize != 47) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 47\n", vmaSize);
    Die();
  }
#    endif
#elif defined(__powerpc64__)
# if !SANITIZER_GO
  if (vmaSize != 44 && vmaSize != 46 && vmaSize != 47) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 44, 46, and 47\n", vmaSize);
    Die();
  }
# else
  if (vmaSize != 46 && vmaSize != 47) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 46, and 47\n", vmaSize);
    Die();
  }
# endif
#elif defined(__mips64)
# if !SANITIZER_GO
  if (vmaSize != 40) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 40\n", vmaSize);
    Die();
  }
# else
  if (vmaSize != 47) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 47\n", vmaSize);
    Die();
  }
# endif
#  elif SANITIZER_RISCV64
  // the bottom half of vma is allocated for userspace
  vmaSize = vmaSize + 1;
#    if !SANITIZER_GO
  if (vmaSize != 39 && vmaSize != 48) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 39 and 48\n", vmaSize);
    Die();
  }
#    endif
#  endif

#  if !SANITIZER_GO
  // Heap has not been allocated yet
  ReExecIfNeeded(false);
#  endif
}

void InitializePlatform() {
  DisableCoreDumperIfNecessary();

  // Go maps shadow memory lazily and works fine with limited address space.
  // Unlimited stack is not a problem as well, because the executable
  // is not compiled with -pie.
#if !SANITIZER_GO
  {
#    if SANITIZER_LINUX && (defined(__aarch64__) || defined(__loongarch_lp64))
    // Initialize the xor key used in {sig}{set,long}jump.
    InitializeLongjmpXorKey();
#    endif
  }

  // We called ReExecIfNeeded() in InitializePlatformEarly(), but there are
  // intervening allocations that result in an edge case:
  // 1) InitializePlatformEarly(): memory layout is compatible
  // 2) Intervening allocations happen
  // 3) InitializePlatform(): memory layout is incompatible and fails
  //    CheckAndProtect()
#    if !SANITIZER_GO
  // Heap has already been allocated
  ReExecIfNeeded(true);
#    endif

  // Earlier initialization steps already re-exec'ed until we got a compatible
  // memory layout, so we don't expect any more issues here.
  if (!CheckAndProtect(true, true, true)) {
    Printf(
        "FATAL: ThreadSanitizer: unexpectedly found incompatible memory "
        "layout.\n");
    Printf("FATAL: Please file a bug.\n");
    DumpProcessMap();
    Die();
  }

  InitTlsSize();
#endif  // !SANITIZER_GO
}

#if !SANITIZER_GO
// Extract file descriptors passed to glibc internal __res_iclose function.
// This is required to properly "close" the fds, because we do not see internal
// closes within glibc. The code is a pure hack.
int ExtractResolvFDs(void *state, int *fds, int nfd) {
#if SANITIZER_LINUX && !SANITIZER_ANDROID
  int cnt = 0;
  struct __res_state *statp = (struct __res_state*)state;
  for (int i = 0; i < MAXNS && cnt < nfd; i++) {
    if (statp->_u._ext.nsaddrs[i] && statp->_u._ext.nssocks[i] != -1)
      fds[cnt++] = statp->_u._ext.nssocks[i];
  }
  return cnt;
#else
  return 0;
#endif
}

// Extract file descriptors passed via UNIX domain sockets.
// This is required to properly handle "open" of these fds.
// see 'man recvmsg' and 'man 3 cmsg'.
int ExtractRecvmsgFDs(void *msgp, int *fds, int nfd) {
  int res = 0;
  msghdr *msg = (msghdr*)msgp;
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg);
  for (; cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
      continue;
    int n = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(fds[0]);
    for (int i = 0; i < n; i++) {
      fds[res++] = ((int*)CMSG_DATA(cmsg))[i];
      if (res == nfd)
        return res;
    }
  }
  return res;
}

// Reverse operation of libc stack pointer mangling
static uptr UnmangleLongJmpSp(uptr mangled_sp) {
#if defined(__x86_64__)
# if SANITIZER_LINUX
  // Reverse of:
  //   xor  %fs:0x30, %rsi
  //   rol  $0x11, %rsi
  uptr sp;
  asm("ror  $0x11,     %0 \n"
      "xor  %%fs:0x30, %0 \n"
      : "=r" (sp)
      : "0" (mangled_sp));
  return sp;
# else
  return mangled_sp;
# endif
#elif defined(__aarch64__)
# if SANITIZER_LINUX
  return mangled_sp ^ longjmp_xor_key;
# else
  return mangled_sp;
# endif
#elif defined(__loongarch_lp64)
  return mangled_sp ^ longjmp_xor_key;
#elif defined(__powerpc64__)
  // Reverse of:
  //   ld   r4, -28696(r13)
  //   xor  r4, r3, r4
  uptr xor_key;
  asm("ld  %0, -28696(%%r13)" : "=r" (xor_key));
  return mangled_sp ^ xor_key;
#elif defined(__mips__)
  return mangled_sp;
#    elif SANITIZER_RISCV64
  return mangled_sp;
#    elif defined(__s390x__)
  // tcbhead_t.stack_guard
  uptr xor_key = ((uptr *)__builtin_thread_pointer())[5];
  return mangled_sp ^ xor_key;
#    else
#      error "Unknown platform"
#    endif
}

#if SANITIZER_NETBSD
# ifdef __x86_64__
#  define LONG_JMP_SP_ENV_SLOT 6
# else
#  error unsupported
# endif
#elif defined(__powerpc__)
# define LONG_JMP_SP_ENV_SLOT 0
#elif SANITIZER_FREEBSD
# ifdef __aarch64__
#  define LONG_JMP_SP_ENV_SLOT 1
# else
#  define LONG_JMP_SP_ENV_SLOT 2
# endif
#elif SANITIZER_LINUX
# ifdef __aarch64__
#  define LONG_JMP_SP_ENV_SLOT 13
# elif defined(__loongarch__)
#  define LONG_JMP_SP_ENV_SLOT 1
# elif defined(__mips64)
#  define LONG_JMP_SP_ENV_SLOT 1
#      elif SANITIZER_RISCV64
#        define LONG_JMP_SP_ENV_SLOT 13
#      elif defined(__s390x__)
#        define LONG_JMP_SP_ENV_SLOT 9
#      else
#        define LONG_JMP_SP_ENV_SLOT 6
#      endif
#endif

uptr ExtractLongJmpSp(uptr *env) {
  uptr mangled_sp = env[LONG_JMP_SP_ENV_SLOT];
  return UnmangleLongJmpSp(mangled_sp);
}

#if INIT_LONGJMP_XOR_KEY
// GLIBC mangles the function pointers in jmp_buf (used in {set,long}*jmp
// functions) by XORing them with a random key.  For AArch64 it is a global
// variable rather than a TCB one (as for x86_64/powerpc).  We obtain the key by
// issuing a setjmp and XORing the SP pointer values to derive the key.
static void InitializeLongjmpXorKey() {
  // 1. Call REAL(setjmp), which stores the mangled SP in env.
  jmp_buf env;
  REAL(_setjmp)(env);

  // 2. Retrieve vanilla/mangled SP.
  uptr sp;
#ifdef __loongarch__
  asm("move  %0, $sp" : "=r" (sp));
#else
  asm("mov  %0, sp" : "=r" (sp));
#endif
  uptr mangled_sp = ((uptr *)&env)[LONG_JMP_SP_ENV_SLOT];

  // 3. xor SPs to obtain key.
  longjmp_xor_key = mangled_sp ^ sp;
}
#endif

extern "C" void __tsan_tls_initialization() {}

void ImitateTlsWrite(ThreadState *thr, uptr tls_addr, uptr tls_size) {
  // Check that the thr object is in tls;
  const uptr thr_beg = (uptr)thr;
  const uptr thr_end = (uptr)thr + sizeof(*thr);
  CHECK_GE(thr_beg, tls_addr);
  CHECK_LE(thr_beg, tls_addr + tls_size);
  CHECK_GE(thr_end, tls_addr);
  CHECK_LE(thr_end, tls_addr + tls_size);
  // Since the thr object is huge, skip it.
  const uptr pc = StackTrace::GetNextInstructionPc(
      reinterpret_cast<uptr>(__tsan_tls_initialization));
  MemoryRangeImitateWrite(thr, pc, tls_addr, thr_beg - tls_addr);
  MemoryRangeImitateWrite(thr, pc, thr_end, tls_addr + tls_size - thr_end);
}

// Note: this function runs with async signals enabled,
// so it must not touch any tsan state.
int call_pthread_cancel_with_cleanup(int (*fn)(void *arg),
                                     void (*cleanup)(void *arg), void *arg) {
  // pthread_cleanup_push/pop are hardcore macros mess.
  // We can't intercept nor call them w/o including pthread.h.
  int res;
  pthread_cleanup_push(cleanup, arg);
  res = fn(arg);
  pthread_cleanup_pop(0);
  return res;
}
#endif  // !SANITIZER_GO

#if !SANITIZER_GO
void ReplaceSystemMalloc() { }
#endif

#if !SANITIZER_GO
#if SANITIZER_ANDROID
// On Android, one thread can call intercepted functions after
// DestroyThreadState(), so add a fake thread state for "dead" threads.
static ThreadState *dead_thread_state = nullptr;

ThreadState *cur_thread() {
  ThreadState* thr = reinterpret_cast<ThreadState*>(*get_android_tls_ptr());
  if (thr == nullptr) {
    __sanitizer_sigset_t emptyset;
    internal_sigfillset(&emptyset);
    __sanitizer_sigset_t oldset;
    CHECK_EQ(0, internal_sigprocmask(SIG_SETMASK, &emptyset, &oldset));
    thr = reinterpret_cast<ThreadState*>(*get_android_tls_ptr());
    if (thr == nullptr) {
      thr = reinterpret_cast<ThreadState*>(MmapOrDie(sizeof(ThreadState),
                                                     "ThreadState"));
      *get_android_tls_ptr() = reinterpret_cast<uptr>(thr);
      if (dead_thread_state == nullptr) {
        dead_thread_state = reinterpret_cast<ThreadState*>(
            MmapOrDie(sizeof(ThreadState), "ThreadState"));
        dead_thread_state->fast_state.SetIgnoreBit();
        dead_thread_state->ignore_interceptors = 1;
        dead_thread_state->is_dead = true;
        *const_cast<u32*>(&dead_thread_state->tid) = -1;
        CHECK_EQ(0, internal_mprotect(dead_thread_state, sizeof(ThreadState),
                                      PROT_READ));
      }
    }
    CHECK_EQ(0, internal_sigprocmask(SIG_SETMASK, &oldset, nullptr));
  }
  return thr;
}

void set_cur_thread(ThreadState *thr) {
  *get_android_tls_ptr() = reinterpret_cast<uptr>(thr);
}

void cur_thread_finalize() {
  __sanitizer_sigset_t emptyset;
  internal_sigfillset(&emptyset);
  __sanitizer_sigset_t oldset;
  CHECK_EQ(0, internal_sigprocmask(SIG_SETMASK, &emptyset, &oldset));
  ThreadState* thr = reinterpret_cast<ThreadState*>(*get_android_tls_ptr());
  if (thr != dead_thread_state) {
    *get_android_tls_ptr() = reinterpret_cast<uptr>(dead_thread_state);
    UnmapOrDie(thr, sizeof(ThreadState));
  }
  CHECK_EQ(0, internal_sigprocmask(SIG_SETMASK, &oldset, nullptr));
}
#endif  // SANITIZER_ANDROID
#endif  // if !SANITIZER_GO

}  // namespace __tsan

#endif  // SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_NETBSD
