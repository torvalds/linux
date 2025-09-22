//===-- hwasan_linux.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is a part of HWAddressSanitizer and contains Linux-, NetBSD- and
/// FreeBSD-specific code.
///
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD

#  include <dlfcn.h>
#  include <elf.h>
#  include <errno.h>
#  include <link.h>
#  include <pthread.h>
#  include <signal.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <sys/prctl.h>
#  include <sys/resource.h>
#  include <sys/time.h>
#  include <unistd.h>
#  include <unwind.h>

#  include "hwasan.h"
#  include "hwasan_dynamic_shadow.h"
#  include "hwasan_interface_internal.h"
#  include "hwasan_mapping.h"
#  include "hwasan_report.h"
#  include "hwasan_thread.h"
#  include "hwasan_thread_list.h"
#  include "sanitizer_common/sanitizer_common.h"
#  include "sanitizer_common/sanitizer_procmaps.h"
#  include "sanitizer_common/sanitizer_stackdepot.h"

// Configurations of HWASAN_WITH_INTERCEPTORS and SANITIZER_ANDROID.
//
// HWASAN_WITH_INTERCEPTORS=OFF, SANITIZER_ANDROID=OFF
//   Not currently tested.
// HWASAN_WITH_INTERCEPTORS=OFF, SANITIZER_ANDROID=ON
//   Integration tests downstream exist.
// HWASAN_WITH_INTERCEPTORS=ON, SANITIZER_ANDROID=OFF
//    Tested with check-hwasan on x86_64-linux.
// HWASAN_WITH_INTERCEPTORS=ON, SANITIZER_ANDROID=ON
//    Tested with check-hwasan on aarch64-linux-android.
#  if !SANITIZER_ANDROID
SANITIZER_INTERFACE_ATTRIBUTE
THREADLOCAL uptr __hwasan_tls;
#  endif

namespace __hwasan {

// With the zero shadow base we can not actually map pages starting from 0.
// This constant is somewhat arbitrary.
constexpr uptr kZeroBaseShadowStart = 0;
constexpr uptr kZeroBaseMaxShadowStart = 1 << 18;

static void ProtectGap(uptr addr, uptr size) {
  __sanitizer::ProtectGap(addr, size, kZeroBaseShadowStart,
                          kZeroBaseMaxShadowStart);
}

uptr kLowMemStart;
uptr kLowMemEnd;
uptr kHighMemStart;
uptr kHighMemEnd;

static void PrintRange(uptr start, uptr end, const char *name) {
  Printf("|| [%p, %p] || %.*s ||\n", (void *)start, (void *)end, 10, name);
}

static void PrintAddressSpaceLayout() {
  PrintRange(kHighMemStart, kHighMemEnd, "HighMem");
  if (kHighShadowEnd + 1 < kHighMemStart)
    PrintRange(kHighShadowEnd + 1, kHighMemStart - 1, "ShadowGap");
  else
    CHECK_EQ(kHighShadowEnd + 1, kHighMemStart);
  PrintRange(kHighShadowStart, kHighShadowEnd, "HighShadow");
  if (kLowShadowEnd + 1 < kHighShadowStart)
    PrintRange(kLowShadowEnd + 1, kHighShadowStart - 1, "ShadowGap");
  else
    CHECK_EQ(kLowMemEnd + 1, kHighShadowStart);
  PrintRange(kLowShadowStart, kLowShadowEnd, "LowShadow");
  if (kLowMemEnd + 1 < kLowShadowStart)
    PrintRange(kLowMemEnd + 1, kLowShadowStart - 1, "ShadowGap");
  else
    CHECK_EQ(kLowMemEnd + 1, kLowShadowStart);
  PrintRange(kLowMemStart, kLowMemEnd, "LowMem");
  CHECK_EQ(0, kLowMemStart);
}

static uptr GetHighMemEnd() {
  // HighMem covers the upper part of the address space.
  uptr max_address = GetMaxUserVirtualAddress();
  // Adjust max address to make sure that kHighMemEnd and kHighMemStart are
  // properly aligned:
  max_address |= (GetMmapGranularity() << kShadowScale) - 1;
  return max_address;
}

static void InitializeShadowBaseAddress(uptr shadow_size_bytes) {
  // FIXME: Android should init flags before shadow.
  if (!SANITIZER_ANDROID && flags()->fixed_shadow_base != (uptr)-1) {
    __hwasan_shadow_memory_dynamic_address = flags()->fixed_shadow_base;
    uptr beg = __hwasan_shadow_memory_dynamic_address;
    uptr end = beg + shadow_size_bytes;
    if (!MemoryRangeIsAvailable(beg, end)) {
      Report(
          "FATAL: HWAddressSanitizer: Shadow range %p-%p is not available.\n",
          (void *)beg, (void *)end);
      DumpProcessMap();
      CHECK(MemoryRangeIsAvailable(beg, end));
    }
  } else {
    __hwasan_shadow_memory_dynamic_address =
        FindDynamicShadowStart(shadow_size_bytes);
  }
}

static void MaybeDieIfNoTaggingAbi(const char *message) {
  if (!flags()->fail_without_syscall_abi)
    return;
  Printf("FATAL: %s\n", message);
  Die();
}

#  define PR_SET_TAGGED_ADDR_CTRL 55
#  define PR_GET_TAGGED_ADDR_CTRL 56
#  define PR_TAGGED_ADDR_ENABLE (1UL << 0)
#  define ARCH_GET_UNTAG_MASK 0x4001
#  define ARCH_ENABLE_TAGGED_ADDR 0x4002
#  define ARCH_GET_MAX_TAG_BITS 0x4003

static bool CanUseTaggingAbi() {
#  if defined(__x86_64__)
  unsigned long num_bits = 0;
  // Check for x86 LAM support. This API is based on a currently unsubmitted
  // patch to the Linux kernel (as of August 2022) and is thus subject to
  // change. The patch is here:
  // https://lore.kernel.org/all/20220815041803.17954-1-kirill.shutemov@linux.intel.com/
  //
  // arch_prctl(ARCH_GET_MAX_TAG_BITS, &bits) returns the maximum number of tag
  // bits the user can request, or zero if LAM is not supported by the hardware.
  if (internal_iserror(internal_arch_prctl(ARCH_GET_MAX_TAG_BITS,
                                           reinterpret_cast<uptr>(&num_bits))))
    return false;
  // The platform must provide enough bits for HWASan tags.
  if (num_bits < kTagBits)
    return false;
  return true;
#  else
  // Check for ARM TBI support.
  return !internal_iserror(internal_prctl(PR_GET_TAGGED_ADDR_CTRL, 0, 0, 0, 0));
#  endif // __x86_64__
}

static bool EnableTaggingAbi() {
#  if defined(__x86_64__)
  // Enable x86 LAM tagging for the process.
  //
  // arch_prctl(ARCH_ENABLE_TAGGED_ADDR, bits) enables tagging if the number of
  // tag bits requested by the user does not exceed that provided by the system.
  // arch_prctl(ARCH_GET_UNTAG_MASK, &mask) returns the mask of significant
  // address bits. It is ~0ULL if either LAM is disabled for the process or LAM
  // is not supported by the hardware.
  if (internal_iserror(internal_arch_prctl(ARCH_ENABLE_TAGGED_ADDR, kTagBits)))
    return false;
  unsigned long mask = 0;
  // Make sure the tag bits are where we expect them to be.
  if (internal_iserror(internal_arch_prctl(ARCH_GET_UNTAG_MASK,
                                           reinterpret_cast<uptr>(&mask))))
    return false;
  // @mask has ones for non-tag bits, whereas @kAddressTagMask has ones for tag
  // bits. Therefore these masks must not overlap.
  if (mask & kAddressTagMask)
    return false;
  return true;
#  else
  // Enable ARM TBI tagging for the process. If for some reason tagging is not
  // supported, prctl(PR_SET_TAGGED_ADDR_CTRL, PR_TAGGED_ADDR_ENABLE) returns
  // -EINVAL.
  if (internal_iserror(internal_prctl(PR_SET_TAGGED_ADDR_CTRL,
                                      PR_TAGGED_ADDR_ENABLE, 0, 0, 0)))
    return false;
  // Ensure that TBI is enabled.
  if (internal_prctl(PR_GET_TAGGED_ADDR_CTRL, 0, 0, 0, 0) !=
      PR_TAGGED_ADDR_ENABLE)
    return false;
  return true;
#  endif // __x86_64__
}

void InitializeOsSupport() {
  // Check we're running on a kernel that can use the tagged address ABI.
  bool has_abi = CanUseTaggingAbi();

  if (!has_abi) {
#  if SANITIZER_ANDROID || defined(HWASAN_ALIASING_MODE)
    // Some older Android kernels have the tagged pointer ABI on
    // unconditionally, and hence don't have the tagged-addr prctl while still
    // allow the ABI.
    // If targeting Android and the prctl is not around we assume this is the
    // case.
    return;
#  else
    MaybeDieIfNoTaggingAbi(
        "HWAddressSanitizer requires a kernel with tagged address ABI.");
#  endif
  }

  if (EnableTaggingAbi())
    return;

#  if SANITIZER_ANDROID
  MaybeDieIfNoTaggingAbi(
      "HWAddressSanitizer failed to enable tagged address syscall ABI.\n"
      "Check the `sysctl abi.tagged_addr_disabled` configuration.");
#  else
  MaybeDieIfNoTaggingAbi(
      "HWAddressSanitizer failed to enable tagged address syscall ABI.\n");
#  endif
}

bool InitShadow() {
  // Define the entire memory range.
  kHighMemEnd = GetHighMemEnd();

  // Determine shadow memory base offset.
  InitializeShadowBaseAddress(MemToShadowSize(kHighMemEnd));

  // Place the low memory first.
  kLowMemEnd = __hwasan_shadow_memory_dynamic_address - 1;
  kLowMemStart = 0;

  // Define the low shadow based on the already placed low memory.
  kLowShadowEnd = MemToShadow(kLowMemEnd);
  kLowShadowStart = __hwasan_shadow_memory_dynamic_address;

  // High shadow takes whatever memory is left up there (making sure it is not
  // interfering with low memory in the fixed case).
  kHighShadowEnd = MemToShadow(kHighMemEnd);
  kHighShadowStart = Max(kLowMemEnd, MemToShadow(kHighShadowEnd)) + 1;

  // High memory starts where allocated shadow allows.
  kHighMemStart = ShadowToMem(kHighShadowStart);

  // Check the sanity of the defined memory ranges (there might be gaps).
  CHECK_EQ(kHighMemStart % GetMmapGranularity(), 0);
  CHECK_GT(kHighMemStart, kHighShadowEnd);
  CHECK_GT(kHighShadowEnd, kHighShadowStart);
  CHECK_GT(kHighShadowStart, kLowMemEnd);
  CHECK_GT(kLowMemEnd, kLowMemStart);
  CHECK_GT(kLowShadowEnd, kLowShadowStart);
  CHECK_GT(kLowShadowStart, kLowMemEnd);

  // Reserve shadow memory.
  ReserveShadowMemoryRange(kLowShadowStart, kLowShadowEnd, "low shadow");
  ReserveShadowMemoryRange(kHighShadowStart, kHighShadowEnd, "high shadow");

  // Protect all the gaps.
  ProtectGap(0, Min(kLowMemStart, kLowShadowStart));
  if (kLowMemEnd + 1 < kLowShadowStart)
    ProtectGap(kLowMemEnd + 1, kLowShadowStart - kLowMemEnd - 1);
  if (kLowShadowEnd + 1 < kHighShadowStart)
    ProtectGap(kLowShadowEnd + 1, kHighShadowStart - kLowShadowEnd - 1);
  if (kHighShadowEnd + 1 < kHighMemStart)
    ProtectGap(kHighShadowEnd + 1, kHighMemStart - kHighShadowEnd - 1);

  if (Verbosity())
    PrintAddressSpaceLayout();

  return true;
}

void InitThreads() {
  CHECK(__hwasan_shadow_memory_dynamic_address);
  uptr guard_page_size = GetMmapGranularity();
  uptr thread_space_start =
      __hwasan_shadow_memory_dynamic_address - (1ULL << kShadowBaseAlignment);
  uptr thread_space_end =
      __hwasan_shadow_memory_dynamic_address - guard_page_size;
  ReserveShadowMemoryRange(thread_space_start, thread_space_end - 1,
                           "hwasan threads", /*madvise_shadow*/ false);
  ProtectGap(thread_space_end,
             __hwasan_shadow_memory_dynamic_address - thread_space_end);
  InitThreadList(thread_space_start, thread_space_end - thread_space_start);
  hwasanThreadList().CreateCurrentThread();
}

bool MemIsApp(uptr p) {
// Memory outside the alias range has non-zero tags.
#  if !defined(HWASAN_ALIASING_MODE)
  CHECK_EQ(GetTagFromPointer(p), 0);
#  endif

  return (p >= kHighMemStart && p <= kHighMemEnd) ||
         (p >= kLowMemStart && p <= kLowMemEnd);
}

void InstallAtExitHandler() { atexit(HwasanAtExit); }

// ---------------------- TSD ---------------- {{{1

#  if HWASAN_WITH_INTERCEPTORS
static pthread_key_t tsd_key;
static bool tsd_key_inited = false;

void HwasanTSDThreadInit() {
  if (tsd_key_inited)
    CHECK_EQ(0, pthread_setspecific(tsd_key,
                                    (void *)GetPthreadDestructorIterations()));
}

void HwasanTSDDtor(void *tsd) {
  uptr iterations = (uptr)tsd;
  if (iterations > 1) {
    CHECK_EQ(0, pthread_setspecific(tsd_key, (void *)(iterations - 1)));
    return;
  }
  __hwasan_thread_exit();
}

void HwasanTSDInit() {
  CHECK(!tsd_key_inited);
  tsd_key_inited = true;
  CHECK_EQ(0, pthread_key_create(&tsd_key, HwasanTSDDtor));
}
#  else
void HwasanTSDInit() {}
void HwasanTSDThreadInit() {}
#  endif

#  if SANITIZER_ANDROID
uptr *GetCurrentThreadLongPtr() { return (uptr *)get_android_tls_ptr(); }
#  else
uptr *GetCurrentThreadLongPtr() { return &__hwasan_tls; }
#  endif

#  if SANITIZER_ANDROID
void AndroidTestTlsSlot() {
  uptr kMagicValue = 0x010203040A0B0C0D;
  uptr *tls_ptr = GetCurrentThreadLongPtr();
  uptr old_value = *tls_ptr;
  *tls_ptr = kMagicValue;
  dlerror();
  if (*(uptr *)get_android_tls_ptr() != kMagicValue) {
    Printf(
        "ERROR: Incompatible version of Android: TLS_SLOT_SANITIZER(6) is used "
        "for dlerror().\n");
    Die();
  }
  *tls_ptr = old_value;
}
#  else
void AndroidTestTlsSlot() {}
#  endif

static AccessInfo GetAccessInfo(siginfo_t *info, ucontext_t *uc) {
  // Access type is passed in a platform dependent way (see below) and encoded
  // as 0xXY, where X&1 is 1 for store, 0 for load, and X&2 is 1 if the error is
  // recoverable. Valid values of Y are 0 to 4, which are interpreted as
  // log2(access_size), and 0xF, which means that access size is passed via
  // platform dependent register (see below).
#  if defined(__aarch64__)
  // Access type is encoded in BRK immediate as 0x900 + 0xXY. For Y == 0xF,
  // access size is stored in X1 register. Access address is always in X0
  // register.
  uptr pc = (uptr)info->si_addr;
  const unsigned code = ((*(u32 *)pc) >> 5) & 0xffff;
  if ((code & 0xff00) != 0x900)
    return AccessInfo{};  // Not ours.

  const bool is_store = code & 0x10;
  const bool recover = code & 0x20;
  const uptr addr = uc->uc_mcontext.regs[0];
  const unsigned size_log = code & 0xf;
  if (size_log > 4 && size_log != 0xf)
    return AccessInfo{};  // Not ours.
  const uptr size = size_log == 0xf ? uc->uc_mcontext.regs[1] : 1U << size_log;

#  elif defined(__x86_64__)
  // Access type is encoded in the instruction following INT3 as
  // NOP DWORD ptr [EAX + 0x40 + 0xXY]. For Y == 0xF, access size is stored in
  // RSI register. Access address is always in RDI register.
  uptr pc = (uptr)uc->uc_mcontext.gregs[REG_RIP];
  uint8_t *nop = (uint8_t *)pc;
  if (*nop != 0x0f || *(nop + 1) != 0x1f || *(nop + 2) != 0x40 ||
      *(nop + 3) < 0x40)
    return AccessInfo{};  // Not ours.
  const unsigned code = *(nop + 3);

  const bool is_store = code & 0x10;
  const bool recover = code & 0x20;
  const uptr addr = uc->uc_mcontext.gregs[REG_RDI];
  const unsigned size_log = code & 0xf;
  if (size_log > 4 && size_log != 0xf)
    return AccessInfo{};  // Not ours.
  const uptr size =
      size_log == 0xf ? uc->uc_mcontext.gregs[REG_RSI] : 1U << size_log;

#  elif SANITIZER_RISCV64
  // Access type is encoded in the instruction following EBREAK as
  // ADDI x0, x0, [0x40 + 0xXY]. For Y == 0xF, access size is stored in
  // X11 register. Access address is always in X10 register.
  uptr pc = (uptr)uc->uc_mcontext.__gregs[REG_PC];
  uint8_t byte1 = *((u8 *)(pc + 0));
  uint8_t byte2 = *((u8 *)(pc + 1));
  uint8_t byte3 = *((u8 *)(pc + 2));
  uint8_t byte4 = *((u8 *)(pc + 3));
  uint32_t ebreak = (byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24));
  bool isFaultShort = false;
  bool isEbreak = (ebreak == 0x100073);
  bool isShortEbreak = false;
#    if defined(__riscv_compressed)
  isFaultShort = ((ebreak & 0x3) != 0x3);
  isShortEbreak = ((ebreak & 0xffff) == 0x9002);
#    endif
  // faulted insn is not ebreak, not our case
  if (!(isEbreak || isShortEbreak))
    return AccessInfo{};
  // advance pc to point after ebreak and reconstruct addi instruction
  pc += isFaultShort ? 2 : 4;
  byte1 = *((u8 *)(pc + 0));
  byte2 = *((u8 *)(pc + 1));
  byte3 = *((u8 *)(pc + 2));
  byte4 = *((u8 *)(pc + 3));
  // reconstruct instruction
  uint32_t instr = (byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24));
  // check if this is really 32 bit instruction
  // code is encoded in top 12 bits, since instruction is supposed to be with
  // imm
  const unsigned code = (instr >> 20) & 0xffff;
  const uptr addr = uc->uc_mcontext.__gregs[10];
  const bool is_store = code & 0x10;
  const bool recover = code & 0x20;
  const unsigned size_log = code & 0xf;
  if (size_log > 4 && size_log != 0xf)
    return AccessInfo{};  // Not our case
  const uptr size =
      size_log == 0xf ? uc->uc_mcontext.__gregs[11] : 1U << size_log;

#  else
#    error Unsupported architecture
#  endif

  return AccessInfo{addr, size, is_store, !is_store, recover};
}

static bool HwasanOnSIGTRAP(int signo, siginfo_t *info, ucontext_t *uc) {
  AccessInfo ai = GetAccessInfo(info, uc);
  if (!ai.is_store && !ai.is_load)
    return false;

  SignalContext sig{info, uc};
  HandleTagMismatch(ai, StackTrace::GetNextInstructionPc(sig.pc), sig.bp, uc);

#  if defined(__aarch64__)
  uc->uc_mcontext.pc += 4;
#  elif defined(__x86_64__)
#  elif SANITIZER_RISCV64
  // pc points to EBREAK which is 2 bytes long
  uint8_t *exception_source = (uint8_t *)(uc->uc_mcontext.__gregs[REG_PC]);
  uint8_t byte1 = (uint8_t)(*(exception_source + 0));
  uint8_t byte2 = (uint8_t)(*(exception_source + 1));
  uint8_t byte3 = (uint8_t)(*(exception_source + 2));
  uint8_t byte4 = (uint8_t)(*(exception_source + 3));
  uint32_t faulted = (byte1 | (byte2 << 8) | (byte3 << 16) | (byte4 << 24));
  bool isFaultShort = false;
#    if defined(__riscv_compressed)
  isFaultShort = ((faulted & 0x3) != 0x3);
#    endif
  uc->uc_mcontext.__gregs[REG_PC] += isFaultShort ? 2 : 4;
#  else
#    error Unsupported architecture
#  endif
  return true;
}

static void OnStackUnwind(const SignalContext &sig, const void *,
                          BufferedStackTrace *stack) {
  stack->Unwind(StackTrace::GetNextInstructionPc(sig.pc), sig.bp, sig.context,
                common_flags()->fast_unwind_on_fatal);
}

void HwasanOnDeadlySignal(int signo, void *info, void *context) {
  // Probably a tag mismatch.
  if (signo == SIGTRAP)
    if (HwasanOnSIGTRAP(signo, (siginfo_t *)info, (ucontext_t *)context))
      return;

  HandleDeadlySignal(info, context, GetTid(), &OnStackUnwind, nullptr);
}

void Thread::InitStackAndTls(const InitState *) {
  uptr tls_size;
  uptr stack_size;
  GetThreadStackAndTls(IsMainThread(), &stack_bottom_, &stack_size, &tls_begin_,
                       &tls_size);
  stack_top_ = stack_bottom_ + stack_size;
  tls_end_ = tls_begin_ + tls_size;
}

uptr TagMemoryAligned(uptr p, uptr size, tag_t tag) {
  CHECK(IsAligned(p, kShadowAlignment));
  CHECK(IsAligned(size, kShadowAlignment));
  uptr shadow_start = MemToShadow(p);
  uptr shadow_size = MemToShadowSize(size);

  uptr page_size = GetPageSizeCached();
  uptr page_start = RoundUpTo(shadow_start, page_size);
  uptr page_end = RoundDownTo(shadow_start + shadow_size, page_size);
  uptr threshold = common_flags()->clear_shadow_mmap_threshold;
  if (SANITIZER_LINUX &&
      UNLIKELY(page_end >= page_start + threshold && tag == 0)) {
    internal_memset((void *)shadow_start, tag, page_start - shadow_start);
    internal_memset((void *)page_end, tag,
                    shadow_start + shadow_size - page_end);
    // For an anonymous private mapping MADV_DONTNEED will return a zero page on
    // Linux.
    ReleaseMemoryPagesToOSAndZeroFill(page_start, page_end);
  } else {
    internal_memset((void *)shadow_start, tag, shadow_size);
  }
  return AddTagToPointer(p, tag);
}

static void BeforeFork() {
  if (CAN_SANITIZE_LEAKS) {
    __lsan::LockGlobal();
  }
  // `_lsan` functions defined regardless of `CAN_SANITIZE_LEAKS` and lock the
  // stuff we need.
  __lsan::LockThreads();
  __lsan::LockAllocator();
  StackDepotLockBeforeFork();
}

static void AfterFork(bool fork_child) {
  StackDepotUnlockAfterFork(fork_child);
  // `_lsan` functions defined regardless of `CAN_SANITIZE_LEAKS` and unlock
  // the stuff we need.
  __lsan::UnlockAllocator();
  __lsan::UnlockThreads();
  if (CAN_SANITIZE_LEAKS) {
    __lsan::UnlockGlobal();
  }
}

void HwasanInstallAtForkHandler() {
  pthread_atfork(
      &BeforeFork, []() { AfterFork(/* fork_child= */ false); },
      []() { AfterFork(/* fork_child= */ true); });
}

void InstallAtExitCheckLeaks() {
  if (CAN_SANITIZE_LEAKS) {
    if (common_flags()->detect_leaks && common_flags()->leak_check_at_exit) {
      if (flags()->halt_on_error)
        Atexit(__lsan::DoLeakCheck);
      else
        Atexit(__lsan::DoRecoverableLeakCheckVoid);
    }
  }
}

}  // namespace __hwasan

using namespace __hwasan;

extern "C" void __hwasan_thread_enter() {
  hwasanThreadList().CreateCurrentThread()->EnsureRandomStateInited();
}

extern "C" void __hwasan_thread_exit() {
  Thread *t = GetCurrentThread();
  // Make sure that signal handler can not see a stale current thread pointer.
  atomic_signal_fence(memory_order_seq_cst);
  if (t) {
    // Block async signals on the thread as the handler can be instrumented.
    // After this point instrumented code can't access essential data from TLS
    // and will crash.
    // Bionic already calls __hwasan_thread_exit with blocked signals.
    if (SANITIZER_GLIBC)
      BlockSignals();
    hwasanThreadList().ReleaseThread(t);
  }
}

#endif  // SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD
