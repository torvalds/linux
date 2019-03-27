/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
 
/**
 * A subset of `folly/Range.h`.
 * All code copied verbatiam modulo formatting
 */
#pragma once

#include "utils/Likely.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace pzstd {

namespace detail {
/*
 *Use IsCharPointer<T>::type to enable const char* or char*.
 *Use IsCharPointer<T>::const_type to enable only const char*.
*/
template <class T>
struct IsCharPointer {};

template <>
struct IsCharPointer<char*> {
  typedef int type;
};

template <>
struct IsCharPointer<const char*> {
  typedef int const_type;
  typedef int type;
};

} // namespace detail

template <typename Iter>
class Range {
  Iter b_;
  Iter e_;

 public:
  using size_type = std::size_t;
  using iterator = Iter;
  using const_iterator = Iter;
  using value_type = typename std::remove_reference<
      typename std::iterator_traits<Iter>::reference>::type;
  using reference = typename std::iterator_traits<Iter>::reference;

  constexpr Range() : b_(), e_() {}
  constexpr Range(Iter begin, Iter end) : b_(begin), e_(end) {}

  constexpr Range(Iter begin, size_type size) : b_(begin), e_(begin + size) {}

  template <class T = Iter, typename detail::IsCharPointer<T>::type = 0>
  /* implicit */ Range(Iter str) : b_(str), e_(str + std::strlen(str)) {}

  template <class T = Iter, typename detail::IsCharPointer<T>::const_type = 0>
  /* implicit */ Range(const std::string& str)
      : b_(str.data()), e_(b_ + str.size()) {}

  // Allow implicit conversion from Range<From> to Range<To> if From is
  // implicitly convertible to To.
  template <
      class OtherIter,
      typename std::enable_if<
          (!std::is_same<Iter, OtherIter>::value &&
           std::is_convertible<OtherIter, Iter>::value),
          int>::type = 0>
  constexpr /* implicit */ Range(const Range<OtherIter>& other)
      : b_(other.begin()), e_(other.end()) {}

  Range(const Range&) = default;
  Range(Range&&) = default;

  Range& operator=(const Range&) & = default;
  Range& operator=(Range&&) & = default;

  constexpr size_type size() const {
    return e_ - b_;
  }
  bool empty() const {
    return b_ == e_;
  }
  Iter data() const {
    return b_;
  }
  Iter begin() const {
    return b_;
  }
  Iter end() const {
    return e_;
  }

  void advance(size_type n) {
    if (UNLIKELY(n > size())) {
      throw std::out_of_range("index out of range");
    }
    b_ += n;
  }

  void subtract(size_type n) {
    if (UNLIKELY(n > size())) {
      throw std::out_of_range("index out of range");
    }
    e_ -= n;
  }

  Range subpiece(size_type first, size_type length = std::string::npos) const {
    if (UNLIKELY(first > size())) {
      throw std::out_of_range("index out of range");
    }

    return Range(b_ + first, std::min(length, size() - first));
  }
};

using ByteRange = Range<const unsigned char*>;
using MutableByteRange = Range<unsigned char*>;
using StringPiece = Range<const char*>;
}
