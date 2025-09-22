//===-- tsan_shadow.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef TSAN_SHADOW_H
#define TSAN_SHADOW_H

#include "tsan_defs.h"

namespace __tsan {

class FastState {
 public:
  FastState() { Reset(); }

  void Reset() {
    part_.unused0_ = 0;
    part_.sid_ = static_cast<u8>(kFreeSid);
    part_.epoch_ = static_cast<u16>(kEpochLast);
    part_.unused1_ = 0;
    part_.ignore_accesses_ = false;
  }

  void SetSid(Sid sid) { part_.sid_ = static_cast<u8>(sid); }

  Sid sid() const { return static_cast<Sid>(part_.sid_); }

  Epoch epoch() const { return static_cast<Epoch>(part_.epoch_); }

  void SetEpoch(Epoch epoch) { part_.epoch_ = static_cast<u16>(epoch); }

  void SetIgnoreBit() { part_.ignore_accesses_ = 1; }
  void ClearIgnoreBit() { part_.ignore_accesses_ = 0; }
  bool GetIgnoreBit() const { return part_.ignore_accesses_; }

 private:
  friend class Shadow;
  struct Parts {
    u32 unused0_ : 8;
    u32 sid_ : 8;
    u32 epoch_ : kEpochBits;
    u32 unused1_ : 1;
    u32 ignore_accesses_ : 1;
  };
  union {
    Parts part_;
    u32 raw_;
  };
};

static_assert(sizeof(FastState) == kShadowSize, "bad FastState size");

class Shadow {
 public:
  static constexpr RawShadow kEmpty = static_cast<RawShadow>(0);

  Shadow(FastState state, u32 addr, u32 size, AccessType typ) {
    raw_ = state.raw_;
    DCHECK_GT(size, 0);
    DCHECK_LE(size, 8);
    UNUSED Sid sid0 = part_.sid_;
    UNUSED u16 epoch0 = part_.epoch_;
    raw_ |= (!!(typ & kAccessAtomic) << kIsAtomicShift) |
            (!!(typ & kAccessRead) << kIsReadShift) |
            (((((1u << size) - 1) << (addr & 0x7)) & 0xff) << kAccessShift);
    // Note: we don't check kAccessAtomic because it overlaps with
    // FastState::ignore_accesses_ and it may be set spuriously.
    DCHECK_EQ(part_.is_read_, !!(typ & kAccessRead));
    DCHECK_EQ(sid(), sid0);
    DCHECK_EQ(epoch(), epoch0);
  }

  explicit Shadow(RawShadow x = Shadow::kEmpty) { raw_ = static_cast<u32>(x); }

  RawShadow raw() const { return static_cast<RawShadow>(raw_); }
  Sid sid() const { return part_.sid_; }
  Epoch epoch() const { return static_cast<Epoch>(part_.epoch_); }
  u8 access() const { return part_.access_; }

  void GetAccess(uptr *addr, uptr *size, AccessType *typ) const {
    DCHECK(part_.access_ != 0 || raw_ == static_cast<u32>(Shadow::kRodata));
    if (addr)
      *addr = part_.access_ ? __builtin_ffs(part_.access_) - 1 : 0;
    if (size)
      *size = part_.access_ == kFreeAccess ? kShadowCell
                                           : __builtin_popcount(part_.access_);
    if (typ) {
      *typ = part_.is_read_ ? kAccessRead : kAccessWrite;
      if (part_.is_atomic_)
        *typ |= kAccessAtomic;
      if (part_.access_ == kFreeAccess)
        *typ |= kAccessFree;
    }
  }

  ALWAYS_INLINE
  bool IsBothReadsOrAtomic(AccessType typ) const {
    u32 is_read = !!(typ & kAccessRead);
    u32 is_atomic = !!(typ & kAccessAtomic);
    bool res =
        raw_ & ((is_atomic << kIsAtomicShift) | (is_read << kIsReadShift));
    DCHECK_EQ(res,
              (part_.is_read_ && is_read) || (part_.is_atomic_ && is_atomic));
    return res;
  }

  ALWAYS_INLINE
  bool IsRWWeakerOrEqual(AccessType typ) const {
    u32 is_read = !!(typ & kAccessRead);
    u32 is_atomic = !!(typ & kAccessAtomic);
    UNUSED u32 res0 =
        (part_.is_atomic_ > is_atomic) ||
        (part_.is_atomic_ == is_atomic && part_.is_read_ >= is_read);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    const u32 kAtomicReadMask = (1 << kIsAtomicShift) | (1 << kIsReadShift);
    bool res = (raw_ & kAtomicReadMask) >=
               ((is_atomic << kIsAtomicShift) | (is_read << kIsReadShift));

    DCHECK_EQ(res, res0);
    return res;
#else
    return res0;
#endif
  }

  // The FreedMarker must not pass "the same access check" so that we don't
  // return from the race detection algorithm early.
  static RawShadow FreedMarker() {
    FastState fs;
    fs.SetSid(kFreeSid);
    fs.SetEpoch(kEpochLast);
    Shadow s(fs, 0, 8, kAccessWrite);
    return s.raw();
  }

  static RawShadow FreedInfo(Sid sid, Epoch epoch) {
    Shadow s;
    s.part_.sid_ = sid;
    s.part_.epoch_ = static_cast<u16>(epoch);
    s.part_.access_ = kFreeAccess;
    return s.raw();
  }

 private:
  struct Parts {
    u8 access_;
    Sid sid_;
    u16 epoch_ : kEpochBits;
    u16 is_read_ : 1;
    u16 is_atomic_ : 1;
  };
  union {
    Parts part_;
    u32 raw_;
  };

  static constexpr u8 kFreeAccess = 0x81;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  static constexpr uptr kAccessShift = 0;
  static constexpr uptr kIsReadShift = 30;
  static constexpr uptr kIsAtomicShift = 31;
#else
  static constexpr uptr kAccessShift = 24;
  static constexpr uptr kIsReadShift = 1;
  static constexpr uptr kIsAtomicShift = 0;
#endif

 public:
  // .rodata shadow marker, see MapRodata and ContainsSameAccessFast.
  static constexpr RawShadow kRodata =
      static_cast<RawShadow>(1 << kIsReadShift);
};

static_assert(sizeof(Shadow) == kShadowSize, "bad Shadow size");

ALWAYS_INLINE RawShadow LoadShadow(RawShadow *p) {
  return static_cast<RawShadow>(
      atomic_load((atomic_uint32_t *)p, memory_order_relaxed));
}

ALWAYS_INLINE void StoreShadow(RawShadow *sp, RawShadow s) {
  atomic_store((atomic_uint32_t *)sp, static_cast<u32>(s),
               memory_order_relaxed);
}

}  // namespace __tsan

#endif
