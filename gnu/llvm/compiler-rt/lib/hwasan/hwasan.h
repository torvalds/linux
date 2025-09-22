//===-- hwasan.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
// Private Hwasan header.
//===----------------------------------------------------------------------===//

#ifndef HWASAN_H
#define HWASAN_H

#include "hwasan_flags.h"
#include "hwasan_interface_internal.h"
#include "hwasan_mapping.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "ubsan/ubsan_platform.h"

#ifndef HWASAN_CONTAINS_UBSAN
# define HWASAN_CONTAINS_UBSAN CAN_SANITIZE_UB
#endif

#ifndef HWASAN_WITH_INTERCEPTORS
#define HWASAN_WITH_INTERCEPTORS 0
#endif

#ifndef HWASAN_REPLACE_OPERATORS_NEW_AND_DELETE
#define HWASAN_REPLACE_OPERATORS_NEW_AND_DELETE HWASAN_WITH_INTERCEPTORS
#endif

typedef u8 tag_t;

#if defined(HWASAN_ALIASING_MODE)
#  if !defined(__x86_64__)
#    error Aliasing mode is only supported on x86_64
#  endif
// Tags are done in middle bits using userspace aliasing.
constexpr unsigned kAddressTagShift = 39;
constexpr unsigned kTagBits = 3;

// The alias region is placed next to the shadow so the upper bits of all
// taggable addresses matches the upper bits of the shadow base.  This shift
// value determines which upper bits must match.  It has a floor of 44 since the
// shadow is always 8TB.
// TODO(morehouse): In alias mode we can shrink the shadow and use a
// simpler/faster shadow calculation.
constexpr unsigned kTaggableRegionCheckShift =
    __sanitizer::Max(kAddressTagShift + kTagBits + 1U, 44U);
#elif defined(__x86_64__)
// Tags are done in upper bits using Intel LAM.
constexpr unsigned kAddressTagShift = 57;
constexpr unsigned kTagBits = 6;
#else
// TBI (Top Byte Ignore) feature of AArch64: bits [63:56] are ignored in address
// translation and can be used to store a tag.
constexpr unsigned kAddressTagShift = 56;
constexpr unsigned kTagBits = 8;
#endif  // defined(HWASAN_ALIASING_MODE)

// Mask for extracting tag bits from the lower 8 bits.
constexpr uptr kTagMask = (1UL << kTagBits) - 1;

// Mask for extracting tag bits from full pointers.
constexpr uptr kAddressTagMask = kTagMask << kAddressTagShift;

// Minimal alignment of the shadow base address. Determines the space available
// for threads and stack histories. This is an ABI constant.
const unsigned kShadowBaseAlignment = 32;

const unsigned kRecordAddrBaseTagShift = 3;
const unsigned kRecordFPShift = 48;
const unsigned kRecordFPLShift = 4;
const unsigned kRecordFPModulus = 1 << (64 - kRecordFPShift + kRecordFPLShift);

static inline bool InTaggableRegion(uptr addr) {
#if defined(HWASAN_ALIASING_MODE)
  // Aliases are mapped next to shadow so that the upper bits match the shadow
  // base.
  return (addr >> kTaggableRegionCheckShift) ==
         (__hwasan::GetShadowOffset() >> kTaggableRegionCheckShift);
#endif
  return true;
}

static inline tag_t GetTagFromPointer(uptr p) {
  return InTaggableRegion(p) ? ((p >> kAddressTagShift) & kTagMask) : 0;
}

static inline uptr UntagAddr(uptr tagged_addr) {
  return InTaggableRegion(tagged_addr) ? (tagged_addr & ~kAddressTagMask)
                                       : tagged_addr;
}

static inline void *UntagPtr(const void *tagged_ptr) {
  return reinterpret_cast<void *>(
      UntagAddr(reinterpret_cast<uptr>(tagged_ptr)));
}

static inline uptr AddTagToPointer(uptr p, tag_t tag) {
  return InTaggableRegion(p) ? ((p & ~kAddressTagMask) |
                                ((uptr)(tag & kTagMask) << kAddressTagShift))
                             : p;
}

namespace __hwasan {

extern int hwasan_inited;
extern bool hwasan_init_is_running;
extern int hwasan_report_count;

bool InitShadow();
void InitializeOsSupport();
void InitThreads();
void InitializeInterceptors();

void HwasanAllocatorInit();
void HwasanAllocatorLock();
void HwasanAllocatorUnlock();

void *hwasan_malloc(uptr size, StackTrace *stack);
void *hwasan_calloc(uptr nmemb, uptr size, StackTrace *stack);
void *hwasan_realloc(void *ptr, uptr size, StackTrace *stack);
void *hwasan_reallocarray(void *ptr, uptr nmemb, uptr size, StackTrace *stack);
void *hwasan_valloc(uptr size, StackTrace *stack);
void *hwasan_pvalloc(uptr size, StackTrace *stack);
void *hwasan_aligned_alloc(uptr alignment, uptr size, StackTrace *stack);
void *hwasan_memalign(uptr alignment, uptr size, StackTrace *stack);
int hwasan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        StackTrace *stack);
void hwasan_free(void *ptr, StackTrace *stack);

void InstallAtExitHandler();

#define GET_MALLOC_STACK_TRACE                                            \
  BufferedStackTrace stack;                                               \
  if (hwasan_inited)                                                      \
    stack.Unwind(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME(),         \
                 nullptr, common_flags()->fast_unwind_on_malloc,          \
                 common_flags()->malloc_context_size)

#define GET_FATAL_STACK_TRACE_PC_BP(pc, bp)              \
  BufferedStackTrace stack;                              \
  if (hwasan_inited)                                     \
    stack.Unwind(pc, bp, nullptr, common_flags()->fast_unwind_on_fatal)

void HwasanTSDInit();
void HwasanTSDThreadInit();
void HwasanAtExit();

void HwasanOnDeadlySignal(int signo, void *info, void *context);

void HwasanInstallAtForkHandler();

void InstallAtExitCheckLeaks();

void UpdateMemoryUsage();

void AppendToErrorMessageBuffer(const char *buffer);

void AndroidTestTlsSlot();

// This is a compiler-generated struct that can be shared between hwasan
// implementations.
struct AccessInfo {
  uptr addr;
  uptr size;
  bool is_store;
  bool is_load;
  bool recover;
};

// Given access info and frame information, unwind the stack and report the tag
// mismatch.
void HandleTagMismatch(AccessInfo ai, uptr pc, uptr frame, void *uc,
                       uptr *registers_frame = nullptr);

// This dispatches to HandleTagMismatch but sets up the AccessInfo, program
// counter, and frame pointer.
void HwasanTagMismatch(uptr addr, uptr pc, uptr frame, uptr access_info,
                       uptr *registers_frame, size_t outsize);

}  // namespace __hwasan

#if HWASAN_WITH_INTERCEPTORS
// For both bionic and glibc __sigset_t is an unsigned long.
typedef unsigned long __hw_sigset_t;
// Setjmp and longjmp implementations are platform specific, and hence the
// interception code is platform specific too.
#  if defined(__aarch64__)
constexpr size_t kHwRegisterBufSize = 22;
#  elif defined(__x86_64__)
constexpr size_t kHwRegisterBufSize = 8;
#  elif SANITIZER_RISCV64
// saving PC, 12 int regs, sp, 12 fp regs
#    ifndef __riscv_float_abi_soft
constexpr size_t kHwRegisterBufSize = 1 + 12 + 1 + 12;
#    else
constexpr size_t kHwRegisterBufSize = 1 + 12 + 1;
#    endif
#  endif
typedef unsigned long long __hw_register_buf[kHwRegisterBufSize];
struct __hw_jmp_buf_struct {
  // NOTE: The machine-dependent definition of `__sigsetjmp'
  // assume that a `__hw_jmp_buf' begins with a `__hw_register_buf' and that
  // `__mask_was_saved' follows it.  Do not move these members or add others
  // before it.
  //
  // We add a __magic field to our struct to catch cases where libc's setjmp
  // populated the jmp_buf instead of our interceptor.
  __hw_register_buf __jmpbuf; // Calling environment.
  unsigned __mask_was_saved : 1;  // Saved the signal mask?
  unsigned __magic : 31;      // Used to distinguish __hw_jmp_buf from jmp_buf.
  __hw_sigset_t __saved_mask; // Saved signal mask.
};
typedef struct __hw_jmp_buf_struct __hw_jmp_buf[1];
typedef struct __hw_jmp_buf_struct __hw_sigjmp_buf[1];
constexpr unsigned kHwJmpBufMagic = 0x248ACE77;
#endif  // HWASAN_WITH_INTERCEPTORS

#define ENSURE_HWASAN_INITED()      \
  do {                              \
    CHECK(!hwasan_init_is_running); \
    if (!hwasan_inited) {           \
      __hwasan_init();              \
    }                               \
  } while (0)

#endif  // HWASAN_H
