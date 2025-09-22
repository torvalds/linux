//===-- asan_poisoning.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Shadow memory poisoning by ASan RTL and by user application.
//===----------------------------------------------------------------------===//

#include "asan_poisoning.h"

#include "asan_report.h"
#include "asan_stack.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_interface_internal.h"
#include "sanitizer_common/sanitizer_libc.h"

namespace __asan {

static atomic_uint8_t can_poison_memory;

void SetCanPoisonMemory(bool value) {
  atomic_store(&can_poison_memory, value, memory_order_release);
}

bool CanPoisonMemory() {
  return atomic_load(&can_poison_memory, memory_order_acquire);
}

void PoisonShadow(uptr addr, uptr size, u8 value) {
  if (value && !CanPoisonMemory()) return;
  CHECK(AddrIsAlignedByGranularity(addr));
  CHECK(AddrIsInMem(addr));
  CHECK(AddrIsAlignedByGranularity(addr + size));
  CHECK(AddrIsInMem(addr + size - ASAN_SHADOW_GRANULARITY));
  CHECK(REAL(memset));
  FastPoisonShadow(addr, size, value);
}

void PoisonShadowPartialRightRedzone(uptr addr,
                                     uptr size,
                                     uptr redzone_size,
                                     u8 value) {
  if (!CanPoisonMemory()) return;
  CHECK(AddrIsAlignedByGranularity(addr));
  CHECK(AddrIsInMem(addr));
  FastPoisonShadowPartialRightRedzone(addr, size, redzone_size, value);
}

struct ShadowSegmentEndpoint {
  u8 *chunk;
  s8 offset;  // in [0, ASAN_SHADOW_GRANULARITY)
  s8 value;  // = *chunk;

  explicit ShadowSegmentEndpoint(uptr address) {
    chunk = (u8*)MemToShadow(address);
    offset = address & (ASAN_SHADOW_GRANULARITY - 1);
    value = *chunk;
  }
};

void AsanPoisonOrUnpoisonIntraObjectRedzone(uptr ptr, uptr size, bool poison) {
  uptr end = ptr + size;
  if (Verbosity()) {
    Printf("__asan_%spoison_intra_object_redzone [%p,%p) %zd\n",
           poison ? "" : "un", (void *)ptr, (void *)end, size);
    if (Verbosity() >= 2)
      PRINT_CURRENT_STACK();
  }
  CHECK(size);
  CHECK_LE(size, 4096);
  CHECK(IsAligned(end, ASAN_SHADOW_GRANULARITY));
  if (!IsAligned(ptr, ASAN_SHADOW_GRANULARITY)) {
    *(u8 *)MemToShadow(ptr) =
        poison ? static_cast<u8>(ptr % ASAN_SHADOW_GRANULARITY) : 0;
    ptr |= ASAN_SHADOW_GRANULARITY - 1;
    ptr++;
  }
  for (; ptr < end; ptr += ASAN_SHADOW_GRANULARITY)
    *(u8*)MemToShadow(ptr) = poison ? kAsanIntraObjectRedzone : 0;
}

}  // namespace __asan

// ---------------------- Interface ---------------- {{{1
using namespace __asan;

// Current implementation of __asan_(un)poison_memory_region doesn't check
// that user program (un)poisons the memory it owns. It poisons memory
// conservatively, and unpoisons progressively to make sure asan shadow
// mapping invariant is preserved (see detailed mapping description here:
// https://github.com/google/sanitizers/wiki/AddressSanitizerAlgorithm).
//
// * if user asks to poison region [left, right), the program poisons
// at least [left, AlignDown(right)).
// * if user asks to unpoison region [left, right), the program unpoisons
// at most [AlignDown(left), right).
void __asan_poison_memory_region(void const volatile *addr, uptr size) {
  if (!flags()->allow_user_poisoning || size == 0) return;
  uptr beg_addr = (uptr)addr;
  uptr end_addr = beg_addr + size;
  VPrintf(3, "Trying to poison memory region [%p, %p)\n", (void *)beg_addr,
          (void *)end_addr);
  ShadowSegmentEndpoint beg(beg_addr);
  ShadowSegmentEndpoint end(end_addr);
  if (beg.chunk == end.chunk) {
    CHECK_LT(beg.offset, end.offset);
    s8 value = beg.value;
    CHECK_EQ(value, end.value);
    // We can only poison memory if the byte in end.offset is unaddressable.
    // No need to re-poison memory if it is poisoned already.
    if (value > 0 && value <= end.offset) {
      if (beg.offset > 0) {
        *beg.chunk = Min(value, beg.offset);
      } else {
        *beg.chunk = kAsanUserPoisonedMemoryMagic;
      }
    }
    return;
  }
  CHECK_LT(beg.chunk, end.chunk);
  if (beg.offset > 0) {
    // Mark bytes from beg.offset as unaddressable.
    if (beg.value == 0) {
      *beg.chunk = beg.offset;
    } else {
      *beg.chunk = Min(beg.value, beg.offset);
    }
    beg.chunk++;
  }
  REAL(memset)(beg.chunk, kAsanUserPoisonedMemoryMagic, end.chunk - beg.chunk);
  // Poison if byte in end.offset is unaddressable.
  if (end.value > 0 && end.value <= end.offset) {
    *end.chunk = kAsanUserPoisonedMemoryMagic;
  }
}

void __asan_unpoison_memory_region(void const volatile *addr, uptr size) {
  if (!flags()->allow_user_poisoning || size == 0) return;
  uptr beg_addr = (uptr)addr;
  uptr end_addr = beg_addr + size;
  VPrintf(3, "Trying to unpoison memory region [%p, %p)\n", (void *)beg_addr,
          (void *)end_addr);
  ShadowSegmentEndpoint beg(beg_addr);
  ShadowSegmentEndpoint end(end_addr);
  if (beg.chunk == end.chunk) {
    CHECK_LT(beg.offset, end.offset);
    s8 value = beg.value;
    CHECK_EQ(value, end.value);
    // We unpoison memory bytes up to enbytes up to end.offset if it is not
    // unpoisoned already.
    if (value != 0) {
      *beg.chunk = Max(value, end.offset);
    }
    return;
  }
  CHECK_LT(beg.chunk, end.chunk);
  REAL(memset)(beg.chunk, 0, end.chunk - beg.chunk);
  if (end.offset > 0 && end.value != 0) {
    *end.chunk = Max(end.value, end.offset);
  }
}

int __asan_address_is_poisoned(void const volatile *addr) {
  return __asan::AddressIsPoisoned((uptr)addr);
}

uptr __asan_region_is_poisoned(uptr beg, uptr size) {
  if (!size)
    return 0;
  uptr end = beg + size;
  if (!AddrIsInMem(beg))
    return beg;
  if (!AddrIsInMem(end))
    return end;
  CHECK_LT(beg, end);
  uptr aligned_b = RoundUpTo(beg, ASAN_SHADOW_GRANULARITY);
  uptr aligned_e = RoundDownTo(end, ASAN_SHADOW_GRANULARITY);
  uptr shadow_beg = MemToShadow(aligned_b);
  uptr shadow_end = MemToShadow(aligned_e);
  // First check the first and the last application bytes,
  // then check the ASAN_SHADOW_GRANULARITY-aligned region by calling
  // mem_is_zero on the corresponding shadow.
  if (!__asan::AddressIsPoisoned(beg) && !__asan::AddressIsPoisoned(end - 1) &&
      (shadow_end <= shadow_beg ||
       __sanitizer::mem_is_zero((const char *)shadow_beg,
                                shadow_end - shadow_beg)))
    return 0;
  // The fast check failed, so we have a poisoned byte somewhere.
  // Find it slowly.
  for (; beg < end; beg++)
    if (__asan::AddressIsPoisoned(beg))
      return beg;
  UNREACHABLE("mem_is_zero returned false, but poisoned byte was not found");
  return 0;
}

#define CHECK_SMALL_REGION(p, size, isWrite)                  \
  do {                                                        \
    uptr __p = reinterpret_cast<uptr>(p);                     \
    uptr __size = size;                                       \
    if (UNLIKELY(__asan::AddressIsPoisoned(__p) ||            \
        __asan::AddressIsPoisoned(__p + __size - 1))) {       \
      GET_CURRENT_PC_BP_SP;                                   \
      uptr __bad = __asan_region_is_poisoned(__p, __size);    \
      __asan_report_error(pc, bp, sp, __bad, isWrite, __size, 0);\
    }                                                         \
  } while (false)


extern "C" SANITIZER_INTERFACE_ATTRIBUTE
u16 __sanitizer_unaligned_load16(const uu16 *p) {
  CHECK_SMALL_REGION(p, sizeof(*p), false);
  return *p;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
u32 __sanitizer_unaligned_load32(const uu32 *p) {
  CHECK_SMALL_REGION(p, sizeof(*p), false);
  return *p;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
u64 __sanitizer_unaligned_load64(const uu64 *p) {
  CHECK_SMALL_REGION(p, sizeof(*p), false);
  return *p;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_unaligned_store16(uu16 *p, u16 x) {
  CHECK_SMALL_REGION(p, sizeof(*p), true);
  *p = x;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_unaligned_store32(uu32 *p, u32 x) {
  CHECK_SMALL_REGION(p, sizeof(*p), true);
  *p = x;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_unaligned_store64(uu64 *p, u64 x) {
  CHECK_SMALL_REGION(p, sizeof(*p), true);
  *p = x;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __asan_poison_cxx_array_cookie(uptr p) {
  if (SANITIZER_WORDSIZE != 64) return;
  if (!flags()->poison_array_cookie) return;
  uptr s = MEM_TO_SHADOW(p);
  *reinterpret_cast<u8*>(s) = kAsanArrayCookieMagic;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
uptr __asan_load_cxx_array_cookie(uptr *p) {
  if (SANITIZER_WORDSIZE != 64) return *p;
  if (!flags()->poison_array_cookie) return *p;
  uptr s = MEM_TO_SHADOW(reinterpret_cast<uptr>(p));
  u8 sval = *reinterpret_cast<u8*>(s);
  if (sval == kAsanArrayCookieMagic) return *p;
  // If sval is not kAsanArrayCookieMagic it can only be freed memory,
  // which means that we are going to get double-free. So, return 0 to avoid
  // infinite loop of destructors. We don't want to report a double-free here
  // though, so print a warning just in case.
  // CHECK_EQ(sval, kAsanHeapFreeMagic);
  if (sval == kAsanHeapFreeMagic) {
    Report("AddressSanitizer: loaded array cookie from free-d memory; "
           "expect a double-free report\n");
    return 0;
  }
  // The cookie may remain unpoisoned if e.g. it comes from a custom
  // operator new defined inside a class.
  return *p;
}

// This is a simplified version of __asan_(un)poison_memory_region, which
// assumes that left border of region to be poisoned is properly aligned.
static void PoisonAlignedStackMemory(uptr addr, uptr size, bool do_poison) {
  if (size == 0) return;
  uptr aligned_size = size & ~(ASAN_SHADOW_GRANULARITY - 1);
  PoisonShadow(addr, aligned_size,
               do_poison ? kAsanStackUseAfterScopeMagic : 0);
  if (size == aligned_size)
    return;
  s8 end_offset = (s8)(size - aligned_size);
  s8* shadow_end = (s8*)MemToShadow(addr + aligned_size);
  s8 end_value = *shadow_end;
  if (do_poison) {
    // If possible, mark all the bytes mapping to last shadow byte as
    // unaddressable.
    if (end_value > 0 && end_value <= end_offset)
      *shadow_end = (s8)kAsanStackUseAfterScopeMagic;
  } else {
    // If necessary, mark few first bytes mapping to last shadow byte
    // as addressable
    if (end_value != 0)
      *shadow_end = Max(end_value, end_offset);
  }
}

void __asan_set_shadow_00(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0, size);
}

void __asan_set_shadow_01(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0x01, size);
}

void __asan_set_shadow_02(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0x02, size);
}

void __asan_set_shadow_03(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0x03, size);
}

void __asan_set_shadow_04(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0x04, size);
}

void __asan_set_shadow_05(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0x05, size);
}

void __asan_set_shadow_06(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0x06, size);
}

void __asan_set_shadow_07(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0x07, size);
}

void __asan_set_shadow_f1(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0xf1, size);
}

void __asan_set_shadow_f2(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0xf2, size);
}

void __asan_set_shadow_f3(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0xf3, size);
}

void __asan_set_shadow_f5(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0xf5, size);
}

void __asan_set_shadow_f8(uptr addr, uptr size) {
  REAL(memset)((void *)addr, 0xf8, size);
}

void __asan_poison_stack_memory(uptr addr, uptr size) {
  VReport(1, "poisoning: %p %zx\n", (void *)addr, size);
  PoisonAlignedStackMemory(addr, size, true);
}

void __asan_unpoison_stack_memory(uptr addr, uptr size) {
  VReport(1, "unpoisoning: %p %zx\n", (void *)addr, size);
  PoisonAlignedStackMemory(addr, size, false);
}

static void FixUnalignedStorage(uptr storage_beg, uptr storage_end,
                                uptr &old_beg, uptr &old_end, uptr &new_beg,
                                uptr &new_end) {
  constexpr uptr granularity = ASAN_SHADOW_GRANULARITY;
  if (UNLIKELY(!AddrIsAlignedByGranularity(storage_end))) {
    uptr end_down = RoundDownTo(storage_end, granularity);
    // Ignore the last unaligned granule if the storage is followed by
    // unpoisoned byte, because we can't poison the prefix anyway. Don't call
    // AddressIsPoisoned at all if container changes does not affect the last
    // granule at all.
    if ((((old_end != new_end) && Max(old_end, new_end) > end_down) ||
         ((old_beg != new_beg) && Max(old_beg, new_beg) > end_down)) &&
        !AddressIsPoisoned(storage_end)) {
      old_beg = Min(end_down, old_beg);
      old_end = Min(end_down, old_end);
      new_beg = Min(end_down, new_beg);
      new_end = Min(end_down, new_end);
    }
  }

  // Handle misaligned begin and cut it off.
  if (UNLIKELY(!AddrIsAlignedByGranularity(storage_beg))) {
    uptr beg_up = RoundUpTo(storage_beg, granularity);
    // The first unaligned granule needs special handling only if we had bytes
    // there before and will have none after.
    if ((new_beg == new_end || new_beg >= beg_up) && old_beg != old_end &&
        old_beg < beg_up) {
      // Keep granule prefix outside of the storage unpoisoned.
      uptr beg_down = RoundDownTo(storage_beg, granularity);
      *(u8 *)MemToShadow(beg_down) = storage_beg - beg_down;
      old_beg = Max(beg_up, old_beg);
      old_end = Max(beg_up, old_end);
      new_beg = Max(beg_up, new_beg);
      new_end = Max(beg_up, new_end);
    }
  }
}

void __sanitizer_annotate_contiguous_container(const void *beg_p,
                                               const void *end_p,
                                               const void *old_mid_p,
                                               const void *new_mid_p) {
  if (!flags()->detect_container_overflow)
    return;
  VPrintf(2, "contiguous_container: %p %p %p %p\n", beg_p, end_p, old_mid_p,
          new_mid_p);
  uptr storage_beg = reinterpret_cast<uptr>(beg_p);
  uptr storage_end = reinterpret_cast<uptr>(end_p);
  uptr old_end = reinterpret_cast<uptr>(old_mid_p);
  uptr new_end = reinterpret_cast<uptr>(new_mid_p);
  uptr old_beg = storage_beg;
  uptr new_beg = storage_beg;
  uptr granularity = ASAN_SHADOW_GRANULARITY;
  if (!(storage_beg <= old_end && storage_beg <= new_end &&
        old_end <= storage_end && new_end <= storage_end)) {
    GET_STACK_TRACE_FATAL_HERE;
    ReportBadParamsToAnnotateContiguousContainer(storage_beg, storage_end,
                                                 old_end, new_end, &stack);
  }
  CHECK_LE(storage_end - storage_beg,
           FIRST_32_SECOND_64(1UL << 30, 1ULL << 40));  // Sanity check.

  if (old_end == new_end)
    return;  // Nothing to do here.

  FixUnalignedStorage(storage_beg, storage_end, old_beg, old_end, new_beg,
                      new_end);

  uptr a = RoundDownTo(Min(old_end, new_end), granularity);
  uptr c = RoundUpTo(Max(old_end, new_end), granularity);
  uptr d1 = RoundDownTo(old_end, granularity);
  // uptr d2 = RoundUpTo(old_mid, granularity);
  // Currently we should be in this state:
  // [a, d1) is good, [d2, c) is bad, [d1, d2) is partially good.
  // Make a quick sanity check that we are indeed in this state.
  //
  // FIXME: Two of these three checks are disabled until we fix
  // https://github.com/google/sanitizers/issues/258.
  // if (d1 != d2)
  //  DCHECK_EQ(*(u8*)MemToShadow(d1), old_mid - d1);
  //
  // NOTE: curly brackets for the "if" below to silence a MSVC warning.
  if (a + granularity <= d1) {
    DCHECK_EQ(*(u8 *)MemToShadow(a), 0);
  }
  // if (d2 + granularity <= c && c <= end)
  //   DCHECK_EQ(*(u8 *)MemToShadow(c - granularity),
  //            kAsanContiguousContainerOOBMagic);

  uptr b1 = RoundDownTo(new_end, granularity);
  uptr b2 = RoundUpTo(new_end, granularity);
  // New state:
  // [a, b1) is good, [b2, c) is bad, [b1, b2) is partially good.
  if (b1 > a)
    PoisonShadow(a, b1 - a, 0);
  else if (c > b2)
    PoisonShadow(b2, c - b2, kAsanContiguousContainerOOBMagic);
  if (b1 != b2) {
    CHECK_EQ(b2 - b1, granularity);
    *(u8 *)MemToShadow(b1) = static_cast<u8>(new_end - b1);
  }
}

// Annotates a double ended contiguous memory area like std::deque's chunk.
// It allows detecting buggy accesses to allocated but not used begining
// or end items of such a container.
void __sanitizer_annotate_double_ended_contiguous_container(
    const void *storage_beg_p, const void *storage_end_p,
    const void *old_container_beg_p, const void *old_container_end_p,
    const void *new_container_beg_p, const void *new_container_end_p) {
  if (!flags()->detect_container_overflow)
    return;

  VPrintf(2, "contiguous_container: %p %p %p %p %p %p\n", storage_beg_p,
          storage_end_p, old_container_beg_p, old_container_end_p,
          new_container_beg_p, new_container_end_p);

  uptr storage_beg = reinterpret_cast<uptr>(storage_beg_p);
  uptr storage_end = reinterpret_cast<uptr>(storage_end_p);
  uptr old_beg = reinterpret_cast<uptr>(old_container_beg_p);
  uptr old_end = reinterpret_cast<uptr>(old_container_end_p);
  uptr new_beg = reinterpret_cast<uptr>(new_container_beg_p);
  uptr new_end = reinterpret_cast<uptr>(new_container_end_p);

  constexpr uptr granularity = ASAN_SHADOW_GRANULARITY;

  if (!(old_beg <= old_end && new_beg <= new_end) ||
      !(storage_beg <= new_beg && new_end <= storage_end) ||
      !(storage_beg <= old_beg && old_end <= storage_end)) {
    GET_STACK_TRACE_FATAL_HERE;
    ReportBadParamsToAnnotateDoubleEndedContiguousContainer(
        storage_beg, storage_end, old_beg, old_end, new_beg, new_end, &stack);
  }
  CHECK_LE(storage_end - storage_beg,
           FIRST_32_SECOND_64(1UL << 30, 1ULL << 40));  // Sanity check.

  if ((old_beg == old_end && new_beg == new_end) ||
      (old_beg == new_beg && old_end == new_end))
    return;  // Nothing to do here.

  FixUnalignedStorage(storage_beg, storage_end, old_beg, old_end, new_beg,
                      new_end);

  // Handle non-intersecting new/old containers separately have simpler
  // intersecting case.
  if (old_beg == old_end || new_beg == new_end || new_end <= old_beg ||
      old_end <= new_beg) {
    if (old_beg != old_end) {
      // Poisoning the old container.
      uptr a = RoundDownTo(old_beg, granularity);
      uptr b = RoundUpTo(old_end, granularity);
      PoisonShadow(a, b - a, kAsanContiguousContainerOOBMagic);
    }

    if (new_beg != new_end) {
      // Unpoisoning the new container.
      uptr a = RoundDownTo(new_beg, granularity);
      uptr b = RoundDownTo(new_end, granularity);
      PoisonShadow(a, b - a, 0);
      if (!AddrIsAlignedByGranularity(new_end))
        *(u8 *)MemToShadow(b) = static_cast<u8>(new_end - b);
    }

    return;
  }

  // Intersection of old and new containers is not empty.
  CHECK_LT(new_beg, old_end);
  CHECK_GT(new_end, old_beg);

  if (new_beg < old_beg) {
    // Round down because we can't poison prefixes.
    uptr a = RoundDownTo(new_beg, granularity);
    // Round down and ignore the [c, old_beg) as its state defined by unchanged
    // [old_beg, old_end).
    uptr c = RoundDownTo(old_beg, granularity);
    PoisonShadow(a, c - a, 0);
  } else if (new_beg > old_beg) {
    // Round down and poison [a, old_beg) because it was unpoisoned only as a
    // prefix.
    uptr a = RoundDownTo(old_beg, granularity);
    // Round down and ignore the [c, new_beg) as its state defined by unchanged
    // [new_beg, old_end).
    uptr c = RoundDownTo(new_beg, granularity);

    PoisonShadow(a, c - a, kAsanContiguousContainerOOBMagic);
  }

  if (new_end > old_end) {
    // Round down to poison the prefix.
    uptr a = RoundDownTo(old_end, granularity);
    // Round down and handle remainder below.
    uptr c = RoundDownTo(new_end, granularity);
    PoisonShadow(a, c - a, 0);
    if (!AddrIsAlignedByGranularity(new_end))
      *(u8 *)MemToShadow(c) = static_cast<u8>(new_end - c);
  } else if (new_end < old_end) {
    // Round up and handle remained below.
    uptr a2 = RoundUpTo(new_end, granularity);
    // Round up to poison entire granule as we had nothing in [old_end, c2).
    uptr c2 = RoundUpTo(old_end, granularity);
    PoisonShadow(a2, c2 - a2, kAsanContiguousContainerOOBMagic);

    if (!AddrIsAlignedByGranularity(new_end)) {
      uptr a = RoundDownTo(new_end, granularity);
      *(u8 *)MemToShadow(a) = static_cast<u8>(new_end - a);
    }
  }
}

static const void *FindBadAddress(uptr begin, uptr end, bool poisoned) {
  CHECK_LE(begin, end);
  constexpr uptr kMaxRangeToCheck = 32;
  if (end - begin > kMaxRangeToCheck * 2) {
    if (auto *bad = FindBadAddress(begin, begin + kMaxRangeToCheck, poisoned))
      return bad;
    if (auto *bad = FindBadAddress(end - kMaxRangeToCheck, end, poisoned))
      return bad;
  }

  for (uptr i = begin; i < end; ++i)
    if (AddressIsPoisoned(i) != poisoned)
      return reinterpret_cast<const void *>(i);
  return nullptr;
}

const void *__sanitizer_contiguous_container_find_bad_address(
    const void *beg_p, const void *mid_p, const void *end_p) {
  if (!flags()->detect_container_overflow)
    return nullptr;
  uptr granularity = ASAN_SHADOW_GRANULARITY;
  uptr beg = reinterpret_cast<uptr>(beg_p);
  uptr end = reinterpret_cast<uptr>(end_p);
  uptr mid = reinterpret_cast<uptr>(mid_p);
  CHECK_LE(beg, mid);
  CHECK_LE(mid, end);
  // If the byte after the storage is unpoisoned, everything in the granule
  // before must stay unpoisoned.
  uptr annotations_end =
      (!AddrIsAlignedByGranularity(end) && !AddressIsPoisoned(end))
          ? RoundDownTo(end, granularity)
          : end;
  beg = Min(beg, annotations_end);
  mid = Min(mid, annotations_end);
  if (auto *bad = FindBadAddress(beg, mid, false))
    return bad;
  if (auto *bad = FindBadAddress(mid, annotations_end, true))
    return bad;
  return FindBadAddress(annotations_end, end, false);
}

int __sanitizer_verify_contiguous_container(const void *beg_p,
                                            const void *mid_p,
                                            const void *end_p) {
  return __sanitizer_contiguous_container_find_bad_address(beg_p, mid_p,
                                                           end_p) == nullptr;
}

const void *__sanitizer_double_ended_contiguous_container_find_bad_address(
    const void *storage_beg_p, const void *container_beg_p,
    const void *container_end_p, const void *storage_end_p) {
  if (!flags()->detect_container_overflow)
    return nullptr;
  uptr granularity = ASAN_SHADOW_GRANULARITY;
  uptr storage_beg = reinterpret_cast<uptr>(storage_beg_p);
  uptr storage_end = reinterpret_cast<uptr>(storage_end_p);
  uptr beg = reinterpret_cast<uptr>(container_beg_p);
  uptr end = reinterpret_cast<uptr>(container_end_p);

  // The prefix of the firs granule of the container is unpoisoned.
  if (beg != end)
    beg = Max(storage_beg, RoundDownTo(beg, granularity));

  // If the byte after the storage is unpoisoned, the prefix of the last granule
  // is unpoisoned.
  uptr annotations_end = (!AddrIsAlignedByGranularity(storage_end) &&
                          !AddressIsPoisoned(storage_end))
                             ? RoundDownTo(storage_end, granularity)
                             : storage_end;
  storage_beg = Min(storage_beg, annotations_end);
  beg = Min(beg, annotations_end);
  end = Min(end, annotations_end);

  if (auto *bad = FindBadAddress(storage_beg, beg, true))
    return bad;
  if (auto *bad = FindBadAddress(beg, end, false))
    return bad;
  if (auto *bad = FindBadAddress(end, annotations_end, true))
    return bad;
  return FindBadAddress(annotations_end, storage_end, false);
}

int __sanitizer_verify_double_ended_contiguous_container(
    const void *storage_beg_p, const void *container_beg_p,
    const void *container_end_p, const void *storage_end_p) {
  return __sanitizer_double_ended_contiguous_container_find_bad_address(
             storage_beg_p, container_beg_p, container_end_p, storage_end_p) ==
         nullptr;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __asan_poison_intra_object_redzone(uptr ptr, uptr size) {
  AsanPoisonOrUnpoisonIntraObjectRedzone(ptr, size, true);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __asan_unpoison_intra_object_redzone(uptr ptr, uptr size) {
  AsanPoisonOrUnpoisonIntraObjectRedzone(ptr, size, false);
}

// --- Implementation of LSan-specific functions --- {{{1
namespace __lsan {
bool WordIsPoisoned(uptr addr) {
  return (__asan_region_is_poisoned(addr, sizeof(uptr)) != 0);
}
}
