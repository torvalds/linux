//===-- tsan_vector_clock.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_vector_clock.h"

#include "sanitizer_common/sanitizer_placement_new.h"
#include "tsan_mman.h"

namespace __tsan {

#if TSAN_VECTORIZE
const uptr kVectorClockSize = kThreadSlotCount * sizeof(Epoch) / sizeof(m128);
#endif

VectorClock::VectorClock() { Reset(); }

void VectorClock::Reset() {
#if !TSAN_VECTORIZE
  for (uptr i = 0; i < kThreadSlotCount; i++)
    clk_[i] = kEpochZero;
#else
  m128 z = _mm_setzero_si128();
  m128* vclk = reinterpret_cast<m128*>(clk_);
  for (uptr i = 0; i < kVectorClockSize; i++) _mm_store_si128(&vclk[i], z);
#endif
}

void VectorClock::Acquire(const VectorClock* src) {
  if (!src)
    return;
#if !TSAN_VECTORIZE
  for (uptr i = 0; i < kThreadSlotCount; i++)
    clk_[i] = max(clk_[i], src->clk_[i]);
#else
  m128* __restrict vdst = reinterpret_cast<m128*>(clk_);
  m128 const* __restrict vsrc = reinterpret_cast<m128 const*>(src->clk_);
  for (uptr i = 0; i < kVectorClockSize; i++) {
    m128 s = _mm_load_si128(&vsrc[i]);
    m128 d = _mm_load_si128(&vdst[i]);
    m128 m = _mm_max_epu16(s, d);
    _mm_store_si128(&vdst[i], m);
  }
#endif
}

static VectorClock* AllocClock(VectorClock** dstp) {
  if (UNLIKELY(!*dstp))
    *dstp = New<VectorClock>();
  return *dstp;
}

void VectorClock::Release(VectorClock** dstp) const {
  VectorClock* dst = AllocClock(dstp);
  dst->Acquire(this);
}

void VectorClock::ReleaseStore(VectorClock** dstp) const {
  VectorClock* dst = AllocClock(dstp);
  *dst = *this;
}

VectorClock& VectorClock::operator=(const VectorClock& other) {
#if !TSAN_VECTORIZE
  for (uptr i = 0; i < kThreadSlotCount; i++)
    clk_[i] = other.clk_[i];
#else
  m128* __restrict vdst = reinterpret_cast<m128*>(clk_);
  m128 const* __restrict vsrc = reinterpret_cast<m128 const*>(other.clk_);
  for (uptr i = 0; i < kVectorClockSize; i++) {
    m128 s = _mm_load_si128(&vsrc[i]);
    _mm_store_si128(&vdst[i], s);
  }
#endif
  return *this;
}

void VectorClock::ReleaseStoreAcquire(VectorClock** dstp) {
  VectorClock* dst = AllocClock(dstp);
#if !TSAN_VECTORIZE
  for (uptr i = 0; i < kThreadSlotCount; i++) {
    Epoch tmp = dst->clk_[i];
    dst->clk_[i] = clk_[i];
    clk_[i] = max(clk_[i], tmp);
  }
#else
  m128* __restrict vdst = reinterpret_cast<m128*>(dst->clk_);
  m128* __restrict vclk = reinterpret_cast<m128*>(clk_);
  for (uptr i = 0; i < kVectorClockSize; i++) {
    m128 t = _mm_load_si128(&vdst[i]);
    m128 c = _mm_load_si128(&vclk[i]);
    m128 m = _mm_max_epu16(c, t);
    _mm_store_si128(&vdst[i], c);
    _mm_store_si128(&vclk[i], m);
  }
#endif
}

void VectorClock::ReleaseAcquire(VectorClock** dstp) {
  VectorClock* dst = AllocClock(dstp);
#if !TSAN_VECTORIZE
  for (uptr i = 0; i < kThreadSlotCount; i++) {
    dst->clk_[i] = max(dst->clk_[i], clk_[i]);
    clk_[i] = dst->clk_[i];
  }
#else
  m128* __restrict vdst = reinterpret_cast<m128*>(dst->clk_);
  m128* __restrict vclk = reinterpret_cast<m128*>(clk_);
  for (uptr i = 0; i < kVectorClockSize; i++) {
    m128 c = _mm_load_si128(&vclk[i]);
    m128 d = _mm_load_si128(&vdst[i]);
    m128 m = _mm_max_epu16(c, d);
    _mm_store_si128(&vdst[i], m);
    _mm_store_si128(&vclk[i], m);
  }
#endif
}

}  // namespace __tsan
