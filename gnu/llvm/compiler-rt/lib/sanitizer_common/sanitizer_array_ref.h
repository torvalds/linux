//===-- sanitizer_array_ref.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ARRAY_REF_H
#define SANITIZER_ARRAY_REF_H

#include "sanitizer_internal_defs.h"

namespace __sanitizer {

/// ArrayRef - Represent a constant reference to an array (0 or more elements
/// consecutively in memory), i.e. a start pointer and a length.  It allows
/// various APIs to take consecutive elements easily and conveniently.
///
/// This class does not own the underlying data, it is expected to be used in
/// situations where the data resides in some other buffer, whose lifetime
/// extends past that of the ArrayRef. For this reason, it is not in general
/// safe to store an ArrayRef.
///
/// This is intended to be trivially copyable, so it should be passed by
/// value.
template <typename T>
class ArrayRef {
 public:
  constexpr ArrayRef() {}
  constexpr ArrayRef(const T *begin, const T *end) : begin_(begin), end_(end) {
    DCHECK(empty() || begin);
  }
  constexpr ArrayRef(const T *data, uptr length)
      : ArrayRef(data, data + length) {}
  template <uptr N>
  constexpr ArrayRef(const T (&src)[N]) : ArrayRef(src, src + N) {}
  template <typename C>
  constexpr ArrayRef(const C &src)
      : ArrayRef(src.data(), src.data() + src.size()) {}
  ArrayRef(const T &one_elt) : ArrayRef(&one_elt, &one_elt + 1) {}

  const T *data() const { return empty() ? nullptr : begin_; }

  const T *begin() const { return begin_; }
  const T *end() const { return end_; }

  bool empty() const { return begin_ == end_; }

  uptr size() const { return end_ - begin_; }

  /// equals - Check for element-wise equality.
  bool equals(ArrayRef rhs) const {
    if (size() != rhs.size())
      return false;
    auto r = rhs.begin();
    for (auto &l : *this) {
      if (!(l == *r))
        return false;
      ++r;
    }
    return true;
  }

  /// slice(n, m) - Chop off the first N elements of the array, and keep M
  /// elements in the array.
  ArrayRef<T> slice(uptr N, uptr M) const {
    DCHECK_LE(N + M, size());
    return ArrayRef<T>(data() + N, M);
  }

  /// slice(n) - Chop off the first N elements of the array.
  ArrayRef<T> slice(uptr N) const { return slice(N, size() - N); }

  /// Drop the first \p N elements of the array.
  ArrayRef<T> drop_front(uptr N = 1) const {
    DCHECK_GE(size(), N);
    return slice(N, size() - N);
  }

  /// Drop the last \p N elements of the array.
  ArrayRef<T> drop_back(uptr N = 1) const {
    DCHECK_GE(size(), N);
    return slice(0, size() - N);
  }

  /// Return a copy of *this with only the first \p N elements.
  ArrayRef<T> take_front(uptr N = 1) const {
    if (N >= size())
      return *this;
    return drop_back(size() - N);
  }

  /// Return a copy of *this with only the last \p N elements.
  ArrayRef<T> take_back(uptr N = 1) const {
    if (N >= size())
      return *this;
    return drop_front(size() - N);
  }

  const T &operator[](uptr index) const {
    DCHECK_LT(index, size());
    return begin_[index];
  }

 private:
  const T *begin_ = nullptr;
  const T *end_ = nullptr;
};

template <typename T>
inline bool operator==(ArrayRef<T> lhs, ArrayRef<T> rhs) {
  return lhs.equals(rhs);
}

template <typename T>
inline bool operator!=(ArrayRef<T> lhs, ArrayRef<T> rhs) {
  return !(lhs == rhs);
}

}  // namespace __sanitizer

#endif  // SANITIZER_ARRAY_REF_H
