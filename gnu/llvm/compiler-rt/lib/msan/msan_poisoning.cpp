//===-- msan_poisoning.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
//===----------------------------------------------------------------------===//

#include "msan_poisoning.h"

#include "interception/interception.h"
#include "msan_origin.h"
#include "msan_thread.h"
#include "sanitizer_common/sanitizer_common.h"

DECLARE_REAL(void *, memset, void *dest, int c, uptr n)
DECLARE_REAL(void *, memcpy, void *dest, const void *src, uptr n)
DECLARE_REAL(void *, memmove, void *dest, const void *src, uptr n)

namespace __msan {

u32 GetOriginIfPoisoned(uptr addr, uptr size) {
  unsigned char *s = (unsigned char *)MEM_TO_SHADOW(addr);
  for (uptr i = 0; i < size; ++i)
    if (s[i]) return *(u32 *)SHADOW_TO_ORIGIN(((uptr)s + i) & ~3UL);
  return 0;
}

void SetOriginIfPoisoned(uptr addr, uptr src_shadow, uptr size,
                         u32 src_origin) {
  uptr dst_s = MEM_TO_SHADOW(addr);
  uptr src_s = src_shadow;
  uptr src_s_end = src_s + size;

  for (; src_s < src_s_end; ++dst_s, ++src_s)
    if (*(u8 *)src_s) *(u32 *)SHADOW_TO_ORIGIN(dst_s & ~3UL) = src_origin;
}

void CopyOrigin(const void *dst, const void *src, uptr size,
                StackTrace *stack) {
  if (!MEM_IS_APP(dst) || !MEM_IS_APP(src)) return;

  uptr d = (uptr)dst;
  uptr beg = d & ~3UL;
  // Copy left unaligned origin if that memory is poisoned.
  if (beg < d) {
    u32 o = GetOriginIfPoisoned((uptr)src, beg + 4 - d);
    if (o) {
      if (__msan_get_track_origins() > 1) o = ChainOrigin(o, stack);
      *(u32 *)MEM_TO_ORIGIN(beg) = o;
    }
    beg += 4;
  }

  uptr end = (d + size) & ~3UL;
  // If both ends fall into the same 4-byte slot, we are done.
  if (end < beg) return;

  // Copy right unaligned origin if that memory is poisoned.
  if (end < d + size) {
    u32 o = GetOriginIfPoisoned((uptr)src + (end - d), (d + size) - end);
    if (o) {
      if (__msan_get_track_origins() > 1) o = ChainOrigin(o, stack);
      *(u32 *)MEM_TO_ORIGIN(end) = o;
    }
  }

  if (beg < end) {
    // Align src up.
    uptr s = ((uptr)src + 3) & ~3UL;
    // FIXME: factor out to msan_copy_origin_aligned
    if (__msan_get_track_origins() > 1) {
      u32 *src = (u32 *)MEM_TO_ORIGIN(s);
      u32 *src_s = (u32 *)MEM_TO_SHADOW(s);
      u32 *src_end = (u32 *)MEM_TO_ORIGIN(s + (end - beg));
      u32 *dst = (u32 *)MEM_TO_ORIGIN(beg);
      u32 src_o = 0;
      u32 dst_o = 0;
      for (; src < src_end; ++src, ++src_s, ++dst) {
        if (!*src_s) continue;
        if (*src != src_o) {
          src_o = *src;
          dst_o = ChainOrigin(src_o, stack);
        }
        *dst = dst_o;
      }
    } else {
      REAL(memcpy)((void *)MEM_TO_ORIGIN(beg), (void *)MEM_TO_ORIGIN(s),
                   end - beg);
    }
  }
}

void ReverseCopyOrigin(const void *dst, const void *src, uptr size,
                       StackTrace *stack) {
  if (!MEM_IS_APP(dst) || !MEM_IS_APP(src))
    return;

  uptr d = (uptr)dst;
  uptr end = (d + size) & ~3UL;

  // Copy right unaligned origin if that memory is poisoned.
  if (end < d + size) {
    u32 o = GetOriginIfPoisoned((uptr)src + (end - d), (d + size) - end);
    if (o) {
      if (__msan_get_track_origins() > 1)
        o = ChainOrigin(o, stack);
      *(u32 *)MEM_TO_ORIGIN(end) = o;
    }
  }

  uptr beg = d & ~3UL;

  if (beg + 4 < end) {
    // Align src up.
    uptr s = ((uptr)src + 3) & ~3UL;
    if (__msan_get_track_origins() > 1) {
      u32 *src = (u32 *)MEM_TO_ORIGIN(s + end - beg - 4);
      u32 *src_s = (u32 *)MEM_TO_SHADOW(s + end - beg - 4);
      u32 *src_begin = (u32 *)MEM_TO_ORIGIN(s);
      u32 *dst = (u32 *)MEM_TO_ORIGIN(end - 4);
      u32 src_o = 0;
      u32 dst_o = 0;
      for (; src >= src_begin; --src, --src_s, --dst) {
        if (!*src_s)
          continue;
        if (*src != src_o) {
          src_o = *src;
          dst_o = ChainOrigin(src_o, stack);
        }
        *dst = dst_o;
      }
    } else {
      REAL(memmove)
      ((void *)MEM_TO_ORIGIN(beg), (void *)MEM_TO_ORIGIN(s), end - beg - 4);
    }
  }

  // Copy left unaligned origin if that memory is poisoned.
  if (beg < d) {
    u32 o = GetOriginIfPoisoned((uptr)src, beg + 4 - d);
    if (o) {
      if (__msan_get_track_origins() > 1)
        o = ChainOrigin(o, stack);
      *(u32 *)MEM_TO_ORIGIN(beg) = o;
    }
  }
}

void MoveOrigin(const void *dst, const void *src, uptr size,
                StackTrace *stack) {
  // If destination origin range overlaps with source origin range, move
  // origins by coping origins in a reverse order; otherwise, copy origins in
  // a normal order.
  uptr src_aligned_beg = reinterpret_cast<uptr>(src) & ~3UL;
  uptr src_aligned_end = (reinterpret_cast<uptr>(src) + size) & ~3UL;
  uptr dst_aligned_beg = reinterpret_cast<uptr>(dst) & ~3UL;
  if (dst_aligned_beg < src_aligned_end && dst_aligned_beg >= src_aligned_beg)
    return ReverseCopyOrigin(dst, src, size, stack);
  return CopyOrigin(dst, src, size, stack);
}

void MoveShadowAndOrigin(const void *dst, const void *src, uptr size,
                         StackTrace *stack) {
  if (!MEM_IS_APP(dst)) return;
  if (!MEM_IS_APP(src)) return;
  if (src == dst) return;
  // MoveOrigin transfers origins by refering to their shadows. So we
  // need to move origins before moving shadows.
  if (__msan_get_track_origins())
    MoveOrigin(dst, src, size, stack);
  REAL(memmove)((void *)MEM_TO_SHADOW((uptr)dst),
                (void *)MEM_TO_SHADOW((uptr)src), size);
}

void CopyShadowAndOrigin(const void *dst, const void *src, uptr size,
                         StackTrace *stack) {
  if (!MEM_IS_APP(dst)) return;
  if (!MEM_IS_APP(src)) return;
  // Because origin's range is slightly larger than app range, memcpy may also
  // cause overlapped origin ranges.
  REAL(memcpy)((void *)MEM_TO_SHADOW((uptr)dst),
               (void *)MEM_TO_SHADOW((uptr)src), size);
  if (__msan_get_track_origins())
    MoveOrigin(dst, src, size, stack);
}

void CopyMemory(void *dst, const void *src, uptr size, StackTrace *stack) {
  REAL(memcpy)(dst, src, size);
  CopyShadowAndOrigin(dst, src, size, stack);
}

void SetShadow(const void *ptr, uptr size, u8 value) {
  uptr PageSize = GetPageSizeCached();
  uptr shadow_beg = MEM_TO_SHADOW(ptr);
  uptr shadow_end = shadow_beg + size;
  if (value ||
      shadow_end - shadow_beg < common_flags()->clear_shadow_mmap_threshold) {
    REAL(memset)((void *)shadow_beg, value, shadow_end - shadow_beg);
  } else {
    uptr page_beg = RoundUpTo(shadow_beg, PageSize);
    uptr page_end = RoundDownTo(shadow_end, PageSize);

    if (page_beg >= page_end) {
      REAL(memset)((void *)shadow_beg, 0, shadow_end - shadow_beg);
    } else {
      if (page_beg != shadow_beg) {
        REAL(memset)((void *)shadow_beg, 0, page_beg - shadow_beg);
      }
      if (page_end != shadow_end) {
        REAL(memset)((void *)page_end, 0, shadow_end - page_end);
      }
      if (!MmapFixedSuperNoReserve(page_beg, page_end - page_beg))
        Die();

      if (__msan_get_track_origins()) {
        // No need to set origin for zero shadow, but we can release pages.
        uptr origin_beg = RoundUpTo(MEM_TO_ORIGIN(ptr), PageSize);
        if (!MmapFixedSuperNoReserve(origin_beg, page_end - page_beg))
          Die();
      }
    }
  }
}

void SetOrigin(const void *dst, uptr size, u32 origin) {
  // Origin mapping is 4 bytes per 4 bytes of application memory.
  // Here we extend the range such that its left and right bounds are both
  // 4 byte aligned.
  uptr x = MEM_TO_ORIGIN((uptr)dst);
  uptr beg = x & ~3UL;               // align down.
  uptr end = (x + size + 3) & ~3UL;  // align up.
  u64 origin64 = ((u64)origin << 32) | origin;
  // This is like memset, but the value is 32-bit. We unroll by 2 to write
  // 64 bits at once. May want to unroll further to get 128-bit stores.
  if (beg & 7ULL) {
    *(u32 *)beg = origin;
    beg += 4;
  }
  for (uptr addr = beg; addr < (end & ~7UL); addr += 8) *(u64 *)addr = origin64;
  if (end & 7ULL) *(u32 *)(end - 4) = origin;
}

void PoisonMemory(const void *dst, uptr size, StackTrace *stack) {
  SetShadow(dst, size, (u8)-1);

  if (__msan_get_track_origins()) {
    MsanThread *t = GetCurrentThread();
    if (t && t->InSignalHandler())
      return;
    Origin o = Origin::CreateHeapOrigin(stack);
    SetOrigin(dst, size, o.raw_id());
  }
}

}  // namespace __msan
