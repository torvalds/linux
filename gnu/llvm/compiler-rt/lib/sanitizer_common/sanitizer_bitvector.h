//===-- sanitizer_bitvector.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Specializer BitVector implementation.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_BITVECTOR_H
#define SANITIZER_BITVECTOR_H

#include "sanitizer_common.h"

namespace __sanitizer {

// Fixed size bit vector based on a single basic integer.
template <class basic_int_t = uptr>
class BasicBitVector {
 public:
  enum SizeEnum : uptr { kSize = sizeof(basic_int_t) * 8 };

  uptr size() const { return kSize; }
  // No CTOR.
  void clear() { bits_ = 0; }
  void setAll() { bits_ = ~(basic_int_t)0; }
  bool empty() const { return bits_ == 0; }

  // Returns true if the bit has changed from 0 to 1.
  bool setBit(uptr idx) {
    basic_int_t old = bits_;
    bits_ |= mask(idx);
    return bits_ != old;
  }

  // Returns true if the bit has changed from 1 to 0.
  bool clearBit(uptr idx) {
    basic_int_t old = bits_;
    bits_ &= ~mask(idx);
    return bits_ != old;
  }

  bool getBit(uptr idx) const { return (bits_ & mask(idx)) != 0; }

  uptr getAndClearFirstOne() {
    CHECK(!empty());
    uptr idx = LeastSignificantSetBitIndex(bits_);
    clearBit(idx);
    return idx;
  }

  // Do "this |= v" and return whether new bits have been added.
  bool setUnion(const BasicBitVector &v) {
    basic_int_t old = bits_;
    bits_ |= v.bits_;
    return bits_ != old;
  }

  // Do "this &= v" and return whether any bits have been removed.
  bool setIntersection(const BasicBitVector &v) {
    basic_int_t old = bits_;
    bits_ &= v.bits_;
    return bits_ != old;
  }

  // Do "this &= ~v" and return whether any bits have been removed.
  bool setDifference(const BasicBitVector &v) {
    basic_int_t old = bits_;
    bits_ &= ~v.bits_;
    return bits_ != old;
  }

  void copyFrom(const BasicBitVector &v) { bits_ = v.bits_; }

  // Returns true if 'this' intersects with 'v'.
  bool intersectsWith(const BasicBitVector &v) const {
    return (bits_ & v.bits_) != 0;
  }

  // for (BasicBitVector<>::Iterator it(bv); it.hasNext();) {
  //   uptr idx = it.next();
  //   use(idx);
  // }
  class Iterator {
   public:
    Iterator() { }
    explicit Iterator(const BasicBitVector &bv) : bv_(bv) {}
    bool hasNext() const { return !bv_.empty(); }
    uptr next() { return bv_.getAndClearFirstOne(); }
    void clear() { bv_.clear(); }
   private:
    BasicBitVector bv_;
  };

 private:
  basic_int_t mask(uptr idx) const {
    CHECK_LT(idx, size());
    return (basic_int_t)1UL << idx;
  }
  basic_int_t bits_;
};

// Fixed size bit vector of (kLevel1Size*BV::kSize**2) bits.
// The implementation is optimized for better performance on
// sparse bit vectors, i.e. the those with few set bits.
template <uptr kLevel1Size = 1, class BV = BasicBitVector<> >
class TwoLevelBitVector {
  // This is essentially a 2-level bit vector.
  // Set bit in the first level BV indicates that there are set bits
  // in the corresponding BV of the second level.
  // This structure allows O(kLevel1Size) time for clear() and empty(),
  // as well fast handling of sparse BVs.
 public:
  enum SizeEnum : uptr { kSize = BV::kSize * BV::kSize * kLevel1Size };
  // No CTOR.

  uptr size() const { return kSize; }

  void clear() {
    for (uptr i = 0; i < kLevel1Size; i++)
      l1_[i].clear();
  }

  void setAll() {
    for (uptr i0 = 0; i0 < kLevel1Size; i0++) {
      l1_[i0].setAll();
      for (uptr i1 = 0; i1 < BV::kSize; i1++)
        l2_[i0][i1].setAll();
    }
  }

  bool empty() const {
    for (uptr i = 0; i < kLevel1Size; i++)
      if (!l1_[i].empty())
        return false;
    return true;
  }

  // Returns true if the bit has changed from 0 to 1.
  bool setBit(uptr idx) {
    check(idx);
    uptr i0 = idx0(idx);
    uptr i1 = idx1(idx);
    uptr i2 = idx2(idx);
    if (!l1_[i0].getBit(i1)) {
      l1_[i0].setBit(i1);
      l2_[i0][i1].clear();
    }
    bool res = l2_[i0][i1].setBit(i2);
    // Printf("%s: %zd => %zd %zd %zd; %d\n", __func__,
    // idx, i0, i1, i2, res);
    return res;
  }

  bool clearBit(uptr idx) {
    check(idx);
    uptr i0 = idx0(idx);
    uptr i1 = idx1(idx);
    uptr i2 = idx2(idx);
    bool res = false;
    if (l1_[i0].getBit(i1)) {
      res = l2_[i0][i1].clearBit(i2);
      if (l2_[i0][i1].empty())
        l1_[i0].clearBit(i1);
    }
    return res;
  }

  bool getBit(uptr idx) const {
    check(idx);
    uptr i0 = idx0(idx);
    uptr i1 = idx1(idx);
    uptr i2 = idx2(idx);
    // Printf("%s: %zd => %zd %zd %zd\n", __func__, idx, i0, i1, i2);
    return l1_[i0].getBit(i1) && l2_[i0][i1].getBit(i2);
  }

  uptr getAndClearFirstOne() {
    for (uptr i0 = 0; i0 < kLevel1Size; i0++) {
      if (l1_[i0].empty()) continue;
      uptr i1 = l1_[i0].getAndClearFirstOne();
      uptr i2 = l2_[i0][i1].getAndClearFirstOne();
      if (!l2_[i0][i1].empty())
        l1_[i0].setBit(i1);
      uptr res = i0 * BV::kSize * BV::kSize + i1 * BV::kSize + i2;
      // Printf("getAndClearFirstOne: %zd %zd %zd => %zd\n", i0, i1, i2, res);
      return res;
    }
    CHECK(0);
    return 0;
  }

  // Do "this |= v" and return whether new bits have been added.
  bool setUnion(const TwoLevelBitVector &v) {
    bool res = false;
    for (uptr i0 = 0; i0 < kLevel1Size; i0++) {
      BV t = v.l1_[i0];
      while (!t.empty()) {
        uptr i1 = t.getAndClearFirstOne();
        if (l1_[i0].setBit(i1))
          l2_[i0][i1].clear();
        if (l2_[i0][i1].setUnion(v.l2_[i0][i1]))
          res = true;
      }
    }
    return res;
  }

  // Do "this &= v" and return whether any bits have been removed.
  bool setIntersection(const TwoLevelBitVector &v) {
    bool res = false;
    for (uptr i0 = 0; i0 < kLevel1Size; i0++) {
      if (l1_[i0].setIntersection(v.l1_[i0]))
        res = true;
      if (!l1_[i0].empty()) {
        BV t = l1_[i0];
        while (!t.empty()) {
          uptr i1 = t.getAndClearFirstOne();
          if (l2_[i0][i1].setIntersection(v.l2_[i0][i1]))
            res = true;
          if (l2_[i0][i1].empty())
            l1_[i0].clearBit(i1);
        }
      }
    }
    return res;
  }

  // Do "this &= ~v" and return whether any bits have been removed.
  bool setDifference(const TwoLevelBitVector &v) {
    bool res = false;
    for (uptr i0 = 0; i0 < kLevel1Size; i0++) {
      BV t = l1_[i0];
      t.setIntersection(v.l1_[i0]);
      while (!t.empty()) {
        uptr i1 = t.getAndClearFirstOne();
        if (l2_[i0][i1].setDifference(v.l2_[i0][i1]))
          res = true;
        if (l2_[i0][i1].empty())
          l1_[i0].clearBit(i1);
      }
    }
    return res;
  }

  void copyFrom(const TwoLevelBitVector &v) {
    clear();
    setUnion(v);
  }

  // Returns true if 'this' intersects with 'v'.
  bool intersectsWith(const TwoLevelBitVector &v) const {
    for (uptr i0 = 0; i0 < kLevel1Size; i0++) {
      BV t = l1_[i0];
      t.setIntersection(v.l1_[i0]);
      while (!t.empty()) {
        uptr i1 = t.getAndClearFirstOne();
        if (!v.l1_[i0].getBit(i1)) continue;
        if (l2_[i0][i1].intersectsWith(v.l2_[i0][i1]))
          return true;
      }
    }
    return false;
  }

  // for (TwoLevelBitVector<>::Iterator it(bv); it.hasNext();) {
  //   uptr idx = it.next();
  //   use(idx);
  // }
  class Iterator {
   public:
    Iterator() { }
    explicit Iterator(const TwoLevelBitVector &bv) : bv_(bv), i0_(0), i1_(0) {
      it1_.clear();
      it2_.clear();
    }

    bool hasNext() const {
      if (it1_.hasNext()) return true;
      for (uptr i = i0_; i < kLevel1Size; i++)
        if (!bv_.l1_[i].empty()) return true;
      return false;
    }

    uptr next() {
      // Printf("++++: %zd %zd; %d %d; size %zd\n", i0_, i1_, it1_.hasNext(),
      //       it2_.hasNext(), kSize);
      if (!it1_.hasNext() && !it2_.hasNext()) {
        for (; i0_ < kLevel1Size; i0_++) {
          if (bv_.l1_[i0_].empty()) continue;
          it1_ = typename BV::Iterator(bv_.l1_[i0_]);
          // Printf("+i0: %zd %zd; %d %d; size %zd\n", i0_, i1_, it1_.hasNext(),
          //   it2_.hasNext(), kSize);
          break;
        }
      }
      if (!it2_.hasNext()) {
        CHECK(it1_.hasNext());
        i1_ = it1_.next();
        it2_ = typename BV::Iterator(bv_.l2_[i0_][i1_]);
        // Printf("++i1: %zd %zd; %d %d; size %zd\n", i0_, i1_, it1_.hasNext(),
        //       it2_.hasNext(), kSize);
      }
      CHECK(it2_.hasNext());
      uptr i2 = it2_.next();
      uptr res = i0_ * BV::kSize * BV::kSize + i1_ * BV::kSize + i2;
      // Printf("+ret: %zd %zd; %d %d; size %zd; res: %zd\n", i0_, i1_,
      //       it1_.hasNext(), it2_.hasNext(), kSize, res);
      if (!it1_.hasNext() && !it2_.hasNext())
        i0_++;
      return res;
    }

   private:
    const TwoLevelBitVector &bv_;
    uptr i0_, i1_;
    typename BV::Iterator it1_, it2_;
  };

 private:
  void check(uptr idx) const { CHECK_LT(idx, size()); }

  uptr idx0(uptr idx) const {
    uptr res = idx / (BV::kSize * BV::kSize);
    CHECK_LT(res, kLevel1Size);
    return res;
  }

  uptr idx1(uptr idx) const {
    uptr res = (idx / BV::kSize) % BV::kSize;
    CHECK_LT(res, BV::kSize);
    return res;
  }

  uptr idx2(uptr idx) const {
    uptr res = idx % BV::kSize;
    CHECK_LT(res, BV::kSize);
    return res;
  }

  BV l1_[kLevel1Size];
  BV l2_[kLevel1Size][BV::kSize];
};

} // namespace __sanitizer

#endif // SANITIZER_BITVECTOR_H
