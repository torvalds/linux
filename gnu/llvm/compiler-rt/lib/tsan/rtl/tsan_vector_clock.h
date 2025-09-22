//===-- tsan_vector_clock.h -------------------------------------*- C++ -*-===//
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
#ifndef TSAN_VECTOR_CLOCK_H
#define TSAN_VECTOR_CLOCK_H

#include "tsan_defs.h"

namespace __tsan {

// Fixed-size vector clock, used both for threads and sync objects.
class VectorClock {
 public:
  VectorClock();

  Epoch Get(Sid sid) const;
  void Set(Sid sid, Epoch v);

  void Reset();
  void Acquire(const VectorClock* src);
  void Release(VectorClock** dstp) const;
  void ReleaseStore(VectorClock** dstp) const;
  void ReleaseStoreAcquire(VectorClock** dstp);
  void ReleaseAcquire(VectorClock** dstp);

  VectorClock& operator=(const VectorClock& other);

 private:
  VECTOR_ALIGNED Epoch clk_[kThreadSlotCount];
};

ALWAYS_INLINE Epoch VectorClock::Get(Sid sid) const {
  return clk_[static_cast<u8>(sid)];
}

ALWAYS_INLINE void VectorClock::Set(Sid sid, Epoch v) {
  DCHECK_GE(v, clk_[static_cast<u8>(sid)]);
  clk_[static_cast<u8>(sid)] = v;
}

}  // namespace __tsan

#endif  // TSAN_VECTOR_CLOCK_H
