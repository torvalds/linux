//===-- msan.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// Private MSan header.
//===----------------------------------------------------------------------===//

#ifndef MSAN_H
#define MSAN_H

#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "msan_interface_internal.h"
#include "msan_flags.h"
#include "ubsan/ubsan_platform.h"

#ifndef MSAN_REPLACE_OPERATORS_NEW_AND_DELETE
# define MSAN_REPLACE_OPERATORS_NEW_AND_DELETE 1
#endif

#ifndef MSAN_CONTAINS_UBSAN
# define MSAN_CONTAINS_UBSAN CAN_SANITIZE_UB
#endif

struct MappingDesc {
  uptr start;
  uptr end;
  enum Type {
    INVALID = 1,
    ALLOCATOR = 2,
    APP = 4,
    SHADOW = 8,
    ORIGIN = 16,
  } type;
  const char *name;
};

// Note: MappingDesc::ALLOCATOR entries are only used to check for memory
// layout compatibility. The actual allocation settings are in
// msan_allocator.cpp, which need to be kept in sync.
#if SANITIZER_LINUX && defined(__mips64)

// MIPS64 maps:
// - 0x0000000000-0x0200000000: Program own segments
// - 0xa200000000-0xc000000000: PIE program segments
// - 0xe200000000-0xffffffffff: libraries segments.
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x000200000000ULL, MappingDesc::APP, "app-1"},
    {0x000200000000ULL, 0x002200000000ULL, MappingDesc::INVALID, "invalid"},
    {0x002200000000ULL, 0x004000000000ULL, MappingDesc::SHADOW, "shadow-2"},
    {0x004000000000ULL, 0x004200000000ULL, MappingDesc::INVALID, "invalid"},
    {0x004200000000ULL, 0x006000000000ULL, MappingDesc::ORIGIN, "origin-2"},
    {0x006000000000ULL, 0x006200000000ULL, MappingDesc::INVALID, "invalid"},
    {0x006200000000ULL, 0x008000000000ULL, MappingDesc::SHADOW, "shadow-3"},
    {0x008000000000ULL, 0x008200000000ULL, MappingDesc::SHADOW, "shadow-1"},
    {0x008200000000ULL, 0x00a000000000ULL, MappingDesc::ORIGIN, "origin-3"},
    {0x00a000000000ULL, 0x00a200000000ULL, MappingDesc::ORIGIN, "origin-1"},
    {0x00a200000000ULL, 0x00c000000000ULL, MappingDesc::APP, "app-2"},
    {0x00c000000000ULL, 0x00e200000000ULL, MappingDesc::INVALID, "invalid"},
    {0x00e200000000ULL, 0x00ffffffffffULL, MappingDesc::APP, "app-3"}};

#define MEM_TO_SHADOW(mem) (((uptr)(mem)) ^ 0x8000000000ULL)
#define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x2000000000ULL)

#elif SANITIZER_LINUX && defined(__aarch64__)

// The mapping assumes 48-bit VMA. AArch64 maps:
// - 0x0000000000000-0x0100000000000: 39/42/48-bits program own segments
// - 0x0a00000000000-0x0b00000000000: 48-bits PIE program segments
//   Ideally, this would extend to 0x0c00000000000 (2^45 bytes - the
//   maximum ASLR region for 48-bit VMA) but it is too hard to fit in
//   the larger app/shadow/origin regions.
// - 0x0e00000000000-0x1000000000000: 48-bits libraries segments
const MappingDesc kMemoryLayout[] = {
    {0X0000000000000, 0X0100000000000, MappingDesc::APP, "app-10-13"},
    {0X0100000000000, 0X0200000000000, MappingDesc::SHADOW, "shadow-14"},
    {0X0200000000000, 0X0300000000000, MappingDesc::INVALID, "invalid"},
    {0X0300000000000, 0X0400000000000, MappingDesc::ORIGIN, "origin-14"},
    {0X0400000000000, 0X0600000000000, MappingDesc::SHADOW, "shadow-15"},
    {0X0600000000000, 0X0800000000000, MappingDesc::ORIGIN, "origin-15"},
    {0X0800000000000, 0X0A00000000000, MappingDesc::INVALID, "invalid"},
    {0X0A00000000000, 0X0B00000000000, MappingDesc::APP, "app-14"},
    {0X0B00000000000, 0X0C00000000000, MappingDesc::SHADOW, "shadow-10-13"},
    {0X0C00000000000, 0X0D00000000000, MappingDesc::INVALID, "invalid"},
    {0X0D00000000000, 0X0E00000000000, MappingDesc::ORIGIN, "origin-10-13"},
    {0x0E00000000000, 0x0E40000000000, MappingDesc::ALLOCATOR, "allocator"},
    {0X0E40000000000, 0X1000000000000, MappingDesc::APP, "app-15"},
};
# define MEM_TO_SHADOW(mem) ((uptr)mem ^ 0xB00000000000ULL)
# define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x200000000000ULL)

#elif SANITIZER_LINUX && SANITIZER_LOONGARCH64
// LoongArch64 maps:
// - 0x000000000000-0x010000000000: Program own segments
// - 0x555500000000-0x555600000000: PIE program segments
// - 0x7fff00000000-0x7fffffffffff: libraries segments.
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x010000000000ULL, MappingDesc::APP, "app-1"},
    {0x010000000000ULL, 0x100000000000ULL, MappingDesc::SHADOW, "shadow-2"},
    {0x100000000000ULL, 0x110000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x110000000000ULL, 0x200000000000ULL, MappingDesc::ORIGIN, "origin-2"},
    {0x200000000000ULL, 0x300000000000ULL, MappingDesc::SHADOW, "shadow-3"},
    {0x300000000000ULL, 0x400000000000ULL, MappingDesc::ORIGIN, "origin-3"},
    {0x400000000000ULL, 0x500000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x500000000000ULL, 0x510000000000ULL, MappingDesc::SHADOW, "shadow-1"},
    {0x510000000000ULL, 0x600000000000ULL, MappingDesc::APP, "app-2"},
    {0x600000000000ULL, 0x610000000000ULL, MappingDesc::ORIGIN, "origin-1"},
    {0x610000000000ULL, 0x700000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x700000000000ULL, 0x740000000000ULL, MappingDesc::ALLOCATOR, "allocator"},
    {0x740000000000ULL, 0x800000000000ULL, MappingDesc::APP, "app-3"}};
#  define MEM_TO_SHADOW(mem) (((uptr)(mem)) ^ 0x500000000000ULL)
#  define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x100000000000ULL)

#elif SANITIZER_LINUX && SANITIZER_PPC64
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x000200000000ULL, MappingDesc::APP, "low memory"},
    {0x000200000000ULL, 0x080000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x080000000000ULL, 0x180200000000ULL, MappingDesc::SHADOW, "shadow"},
    {0x180200000000ULL, 0x1C0000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x1C0000000000ULL, 0x2C0200000000ULL, MappingDesc::ORIGIN, "origin"},
    {0x2C0200000000ULL, 0x300000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x300000000000ULL, 0x320000000000ULL, MappingDesc::ALLOCATOR, "allocator"},
    {0x320000000000ULL, 0x800000000000ULL, MappingDesc::APP, "high memory"}};

// Various kernels use different low end ranges but we can combine them into one
// big range. They also use different high end ranges but we can map them all to
// one range.
// Maps low and high app ranges to contiguous space with zero base:
//   Low:  0000 0000 0000 - 0001 ffff ffff  ->  1000 0000 0000 - 1001 ffff ffff
//   High: 3000 0000 0000 - 3fff ffff ffff  ->  0000 0000 0000 - 0fff ffff ffff
//   High: 4000 0000 0000 - 4fff ffff ffff  ->  0000 0000 0000 - 0fff ffff ffff
//   High: 7000 0000 0000 - 7fff ffff ffff  ->  0000 0000 0000 - 0fff ffff ffff
#define LINEARIZE_MEM(mem) \
  (((uptr)(mem) & ~0xE00000000000ULL) ^ 0x100000000000ULL)
#define MEM_TO_SHADOW(mem) (LINEARIZE_MEM((mem)) + 0x080000000000ULL)
#define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x140000000000ULL)

#elif SANITIZER_LINUX && SANITIZER_S390_64
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x040000000000ULL, MappingDesc::APP, "low memory"},
    {0x040000000000ULL, 0x080000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x080000000000ULL, 0x180000000000ULL, MappingDesc::SHADOW, "shadow"},
    {0x180000000000ULL, 0x1C0000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x1C0000000000ULL, 0x2C0000000000ULL, MappingDesc::ORIGIN, "origin"},
    {0x2C0000000000ULL, 0x440000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x440000000000ULL, 0x460000000000ULL, MappingDesc::ALLOCATOR, "allocator"},
    {0x460000000000ULL, 0x500000000000ULL, MappingDesc::APP, "high memory"}};

#define MEM_TO_SHADOW(mem) \
  ((((uptr)(mem)) & ~0xC00000000000ULL) + 0x080000000000ULL)
#define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x140000000000ULL)

#elif SANITIZER_FREEBSD && defined(__aarch64__)

// Low memory: main binary, MAP_32BIT mappings and modules
// High memory: heap, modules and main thread stack
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x020000000000ULL, MappingDesc::APP, "low memory"},
    {0x020000000000ULL, 0x200000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x200000000000ULL, 0x620000000000ULL, MappingDesc::SHADOW, "shadow"},
    {0x620000000000ULL, 0x700000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x700000000000ULL, 0xb20000000000ULL, MappingDesc::ORIGIN, "origin"},
    {0xb20000000000ULL, 0xc00000000000ULL, MappingDesc::INVALID, "invalid"},
    {0xc00000000000ULL, 0x1000000000000ULL, MappingDesc::APP, "high memory"}};

// Maps low and high app ranges to contiguous space with zero base:
//   Low:  0000 0000 0000 - 01ff ffff ffff -> 4000 0000 0000 - 41ff ffff ffff
//   High: c000 0000 0000 - ffff ffff ffff -> 0000 0000 0000 - 3fff ffff ffff
#define LINEARIZE_MEM(mem) \
  (((uptr)(mem) & ~0x1800000000000ULL) ^ 0x400000000000ULL)
#define MEM_TO_SHADOW(mem) (LINEARIZE_MEM((mem)) + 0x200000000000ULL)
#define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x500000000000)

#elif SANITIZER_FREEBSD && SANITIZER_WORDSIZE == 64

// Low memory: main binary, MAP_32BIT mappings and modules
// High memory: heap, modules and main thread stack
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x010000000000ULL, MappingDesc::APP, "low memory"},
    {0x010000000000ULL, 0x100000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x100000000000ULL, 0x310000000000ULL, MappingDesc::SHADOW, "shadow"},
    {0x310000000000ULL, 0x380000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x380000000000ULL, 0x590000000000ULL, MappingDesc::ORIGIN, "origin"},
    {0x590000000000ULL, 0x600000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x600000000000ULL, 0x800000000000ULL, MappingDesc::APP, "high memory"}};

// Maps low and high app ranges to contiguous space with zero base:
//   Low:  0000 0000 0000 - 00ff ffff ffff  ->  2000 0000 0000 - 20ff ffff ffff
//   High: 6000 0000 0000 - 7fff ffff ffff  ->  0000 0000 0000 - 1fff ffff ffff
#define LINEARIZE_MEM(mem) \
  (((uptr)(mem) & ~0xc00000000000ULL) ^ 0x200000000000ULL)
#define MEM_TO_SHADOW(mem) (LINEARIZE_MEM((mem)) + 0x100000000000ULL)
#define SHADOW_TO_ORIGIN(shadow) (((uptr)(shadow)) + 0x280000000000)

#elif SANITIZER_NETBSD || (SANITIZER_LINUX && SANITIZER_WORDSIZE == 64)

// All of the following configurations are supported.
// ASLR disabled: main executable and DSOs at 0x555550000000
// PIE and ASLR: main executable and DSOs at 0x7f0000000000
// non-PIE: main executable below 0x100000000, DSOs at 0x7f0000000000
// Heap at 0x700000000000.
const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x010000000000ULL, MappingDesc::APP, "app-1"},
    {0x010000000000ULL, 0x100000000000ULL, MappingDesc::SHADOW, "shadow-2"},
    {0x100000000000ULL, 0x110000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x110000000000ULL, 0x200000000000ULL, MappingDesc::ORIGIN, "origin-2"},
    {0x200000000000ULL, 0x300000000000ULL, MappingDesc::SHADOW, "shadow-3"},
    {0x300000000000ULL, 0x400000000000ULL, MappingDesc::ORIGIN, "origin-3"},
    {0x400000000000ULL, 0x500000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x500000000000ULL, 0x510000000000ULL, MappingDesc::SHADOW, "shadow-1"},
    {0x510000000000ULL, 0x600000000000ULL, MappingDesc::APP, "app-2"},
    {0x600000000000ULL, 0x610000000000ULL, MappingDesc::ORIGIN, "origin-1"},
    {0x610000000000ULL, 0x700000000000ULL, MappingDesc::INVALID, "invalid"},
    {0x700000000000ULL, 0x740000000000ULL, MappingDesc::ALLOCATOR, "allocator"},
    {0x740000000000ULL, 0x800000000000ULL, MappingDesc::APP, "app-3"}};
#define MEM_TO_SHADOW(mem) (((uptr)(mem)) ^ 0x500000000000ULL)
#define SHADOW_TO_ORIGIN(mem) (((uptr)(mem)) + 0x100000000000ULL)

#else
#error "Unsupported platform"
#endif

const uptr kMemoryLayoutSize = sizeof(kMemoryLayout) / sizeof(kMemoryLayout[0]);

#define MEM_TO_ORIGIN(mem) (SHADOW_TO_ORIGIN(MEM_TO_SHADOW((mem))))

#ifndef __clang__
__attribute__((optimize("unroll-loops")))
#endif
inline bool
addr_is_type(uptr addr, int mapping_types) {
// It is critical for performance that this loop is unrolled (because then it is
// simplified into just a few constant comparisons).
#ifdef __clang__
#pragma unroll
#endif
  for (unsigned i = 0; i < kMemoryLayoutSize; ++i)
    if ((kMemoryLayout[i].type & mapping_types) &&
        addr >= kMemoryLayout[i].start && addr < kMemoryLayout[i].end)
      return true;
  return false;
}

#define MEM_IS_APP(mem) \
  (addr_is_type((uptr)(mem), MappingDesc::APP | MappingDesc::ALLOCATOR))
#define MEM_IS_SHADOW(mem) addr_is_type((uptr)(mem), MappingDesc::SHADOW)
#define MEM_IS_ORIGIN(mem) addr_is_type((uptr)(mem), MappingDesc::ORIGIN)

// These constants must be kept in sync with the ones in MemorySanitizer.cpp.
const int kMsanParamTlsSize = 800;
const int kMsanRetvalTlsSize = 800;

namespace __msan {
extern int msan_inited;
extern bool msan_init_is_running;
extern int msan_report_count;

bool ProtectRange(uptr beg, uptr end);
bool InitShadowWithReExec(bool init_origins);
char *GetProcSelfMaps();
void InitializeInterceptors();

void MsanAllocatorInit();
void MsanDeallocate(BufferedStackTrace *stack, void *ptr);

void *msan_malloc(uptr size, BufferedStackTrace *stack);
void *msan_calloc(uptr nmemb, uptr size, BufferedStackTrace *stack);
void *msan_realloc(void *ptr, uptr size, BufferedStackTrace *stack);
void *msan_reallocarray(void *ptr, uptr nmemb, uptr size,
                        BufferedStackTrace *stack);
void *msan_valloc(uptr size, BufferedStackTrace *stack);
void *msan_pvalloc(uptr size, BufferedStackTrace *stack);
void *msan_aligned_alloc(uptr alignment, uptr size, BufferedStackTrace *stack);
void *msan_memalign(uptr alignment, uptr size, BufferedStackTrace *stack);
int msan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        BufferedStackTrace *stack);

void InstallTrapHandler();
void InstallAtExitHandler();

const char *GetStackOriginDescr(u32 id, uptr *pc);

bool IsInSymbolizerOrUnwider();

void PrintWarning(uptr pc, uptr bp);
void PrintWarningWithOrigin(uptr pc, uptr bp, u32 origin);

// Unpoison first n function arguments.
void UnpoisonParam(uptr n);
void UnpoisonThreadLocalState();

// Returns a "chained" origin id, pointing to the given stack trace followed by
// the previous origin id.
u32 ChainOrigin(u32 id, StackTrace *stack);

const int STACK_TRACE_TAG_POISON = StackTrace::TAG_CUSTOM + 1;
const int STACK_TRACE_TAG_FIELDS = STACK_TRACE_TAG_POISON + 1;
const int STACK_TRACE_TAG_VPTR = STACK_TRACE_TAG_FIELDS + 1;

#define GET_MALLOC_STACK_TRACE                                             \
  UNINITIALIZED BufferedStackTrace stack;                                  \
  if (__msan_get_track_origins() && msan_inited) {                         \
    stack.Unwind(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME(), nullptr, \
                 common_flags()->fast_unwind_on_malloc,                    \
                 common_flags()->malloc_context_size);                     \
  }

// For platforms which support slow unwinder only, we restrict the store context
// size to 1, basically only storing the current pc. We do this because the slow
// unwinder which is based on libunwind is not async signal safe and causes
// random freezes in forking applications as well as in signal handlers.
#define GET_STORE_STACK_TRACE_PC_BP(pc, bp)                              \
  UNINITIALIZED BufferedStackTrace stack;                                \
  if (__msan_get_track_origins() > 1 && msan_inited) {                   \
    int size = flags()->store_context_size;                              \
    if (!SANITIZER_CAN_FAST_UNWIND)                                      \
      size = Min(size, 1);                                               \
    stack.Unwind(pc, bp, nullptr, common_flags()->fast_unwind_on_malloc, \
                 size);                                                  \
  }

#define GET_STORE_STACK_TRACE \
  GET_STORE_STACK_TRACE_PC_BP(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME())

#define GET_FATAL_STACK_TRACE_PC_BP(pc, bp)                              \
  UNINITIALIZED BufferedStackTrace stack;                                \
  if (msan_inited) {                                                     \
    stack.Unwind(pc, bp, nullptr, common_flags()->fast_unwind_on_fatal); \
  }

#define GET_FATAL_STACK_TRACE \
  GET_FATAL_STACK_TRACE_PC_BP(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME())

// Unwind the stack for fatal error, as the parameter `stack` is
// empty without origins.
#define GET_FATAL_STACK_TRACE_IF_EMPTY(STACK)                                 \
  if (msan_inited && (STACK)->size == 0) {                                    \
    (STACK)->Unwind(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME(), nullptr, \
                    common_flags()->fast_unwind_on_fatal);                    \
  }

class ScopedThreadLocalStateBackup {
 public:
  ScopedThreadLocalStateBackup() { Backup(); }
  ~ScopedThreadLocalStateBackup() { Restore(); }
  void Backup();
  void Restore();
 private:
  u64 va_arg_overflow_size_tls;
};

void MsanTSDInit(void (*destructor)(void *tsd));
void *MsanTSDGet();
void MsanTSDSet(void *tsd);
void MsanTSDDtor(void *tsd);

void InstallAtForkHandler();

}  // namespace __msan

#endif  // MSAN_H
