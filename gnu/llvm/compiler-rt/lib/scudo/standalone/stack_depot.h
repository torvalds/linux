//===-- stack_depot.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_STACK_DEPOT_H_
#define SCUDO_STACK_DEPOT_H_

#include "atomic_helpers.h"
#include "common.h"
#include "mutex.h"

namespace scudo {

class MurMur2HashBuilder {
  static const u32 M = 0x5bd1e995;
  static const u32 Seed = 0x9747b28c;
  static const u32 R = 24;
  u32 H;

public:
  explicit MurMur2HashBuilder(u32 Init = 0) { H = Seed ^ Init; }
  void add(u32 K) {
    K *= M;
    K ^= K >> R;
    K *= M;
    H *= M;
    H ^= K;
  }
  u32 get() {
    u32 X = H;
    X ^= X >> 13;
    X *= M;
    X ^= X >> 15;
    return X;
  }
};

class alignas(atomic_u64) StackDepot {
  HybridMutex RingEndMu;
  u32 RingEnd = 0;

  // This data structure stores a stack trace for each allocation and
  // deallocation when stack trace recording is enabled, that may be looked up
  // using a hash of the stack trace. The lower bits of the hash are an index
  // into the Tab array, which stores an index into the Ring array where the
  // stack traces are stored. As the name implies, Ring is a ring buffer, so a
  // stack trace may wrap around to the start of the array.
  //
  // Each stack trace in Ring is prefixed by a stack trace marker consisting of
  // a fixed 1 bit in bit 0 (this allows disambiguation between stack frames
  // and stack trace markers in the case where instruction pointers are 4-byte
  // aligned, as they are on arm64), the stack trace hash in bits 1-32, and the
  // size of the stack trace in bits 33-63.
  //
  // The insert() function is potentially racy in its accesses to the Tab and
  // Ring arrays, but find() is resilient to races in the sense that, barring
  // hash collisions, it will either return the correct stack trace or no stack
  // trace at all, even if two instances of insert() raced with one another.
  // This is achieved by re-checking the hash of the stack trace before
  // returning the trace.

  u32 RingSize = 0;
  u32 RingMask = 0;
  u32 TabMask = 0;
  // This is immediately followed by RingSize atomic_u64 and
  // (TabMask + 1) atomic_u32.

  atomic_u64 *getRing() {
    return reinterpret_cast<atomic_u64 *>(reinterpret_cast<char *>(this) +
                                          sizeof(StackDepot));
  }

  atomic_u32 *getTab() {
    return reinterpret_cast<atomic_u32 *>(reinterpret_cast<char *>(this) +
                                          sizeof(StackDepot) +
                                          sizeof(atomic_u64) * RingSize);
  }

  const atomic_u64 *getRing() const {
    return reinterpret_cast<const atomic_u64 *>(
        reinterpret_cast<const char *>(this) + sizeof(StackDepot));
  }

  const atomic_u32 *getTab() const {
    return reinterpret_cast<const atomic_u32 *>(
        reinterpret_cast<const char *>(this) + sizeof(StackDepot) +
        sizeof(atomic_u64) * RingSize);
  }

public:
  void init(u32 RingSz, u32 TabSz) {
    DCHECK(isPowerOfTwo(RingSz));
    DCHECK(isPowerOfTwo(TabSz));
    RingSize = RingSz;
    RingMask = RingSz - 1;
    TabMask = TabSz - 1;
  }

  // Ensure that RingSize, RingMask and TabMask are set up in a way that
  // all accesses are within range of BufSize.
  bool isValid(uptr BufSize) const {
    if (!isPowerOfTwo(RingSize))
      return false;
    uptr RingBytes = sizeof(atomic_u64) * RingSize;
    if (RingMask + 1 != RingSize)
      return false;

    if (TabMask == 0)
      return false;
    uptr TabSize = TabMask + 1;
    if (!isPowerOfTwo(TabSize))
      return false;
    uptr TabBytes = sizeof(atomic_u32) * TabSize;

    // Subtract and detect underflow.
    if (BufSize < sizeof(StackDepot))
      return false;
    BufSize -= sizeof(StackDepot);
    if (BufSize < TabBytes)
      return false;
    BufSize -= TabBytes;
    if (BufSize < RingBytes)
      return false;
    return BufSize == RingBytes;
  }

  // Insert hash of the stack trace [Begin, End) into the stack depot, and
  // return the hash.
  u32 insert(uptr *Begin, uptr *End) {
    auto *Tab = getTab();
    auto *Ring = getRing();

    MurMur2HashBuilder B;
    for (uptr *I = Begin; I != End; ++I)
      B.add(u32(*I) >> 2);
    u32 Hash = B.get();

    u32 Pos = Hash & TabMask;
    u32 RingPos = atomic_load_relaxed(&Tab[Pos]);
    u64 Entry = atomic_load_relaxed(&Ring[RingPos]);
    u64 Id = (u64(End - Begin) << 33) | (u64(Hash) << 1) | 1;
    if (Entry == Id)
      return Hash;

    ScopedLock Lock(RingEndMu);
    RingPos = RingEnd;
    atomic_store_relaxed(&Tab[Pos], RingPos);
    atomic_store_relaxed(&Ring[RingPos], Id);
    for (uptr *I = Begin; I != End; ++I) {
      RingPos = (RingPos + 1) & RingMask;
      atomic_store_relaxed(&Ring[RingPos], *I);
    }
    RingEnd = (RingPos + 1) & RingMask;
    return Hash;
  }

  // Look up a stack trace by hash. Returns true if successful. The trace may be
  // accessed via operator[] passing indexes between *RingPosPtr and
  // *RingPosPtr + *SizePtr.
  bool find(u32 Hash, uptr *RingPosPtr, uptr *SizePtr) const {
    auto *Tab = getTab();
    auto *Ring = getRing();

    u32 Pos = Hash & TabMask;
    u32 RingPos = atomic_load_relaxed(&Tab[Pos]);
    if (RingPos >= RingSize)
      return false;
    u64 Entry = atomic_load_relaxed(&Ring[RingPos]);
    u64 HashWithTagBit = (u64(Hash) << 1) | 1;
    if ((Entry & 0x1ffffffff) != HashWithTagBit)
      return false;
    u32 Size = u32(Entry >> 33);
    if (Size >= RingSize)
      return false;
    *RingPosPtr = (RingPos + 1) & RingMask;
    *SizePtr = Size;
    MurMur2HashBuilder B;
    for (uptr I = 0; I != Size; ++I) {
      RingPos = (RingPos + 1) & RingMask;
      B.add(u32(atomic_load_relaxed(&Ring[RingPos])) >> 2);
    }
    return B.get() == Hash;
  }

  u64 at(uptr RingPos) const {
    auto *Ring = getRing();
    return atomic_load_relaxed(&Ring[RingPos & RingMask]);
  }

  // This is done for the purpose of fork safety in multithreaded programs and
  // does not fully disable StackDepot. In particular, find() still works and
  // only insert() is blocked.
  void disable() NO_THREAD_SAFETY_ANALYSIS { RingEndMu.lock(); }

  void enable() NO_THREAD_SAFETY_ANALYSIS { RingEndMu.unlock(); }
};

// We need StackDepot to be aligned to 8-bytes so the ring we store after
// is correctly assigned.
static_assert(sizeof(StackDepot) % alignof(atomic_u64) == 0);

} // namespace scudo

#endif // SCUDO_STACK_DEPOT_H_
