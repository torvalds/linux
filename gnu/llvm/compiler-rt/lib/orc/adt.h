//===----------------------- adt.h - Handy ADTs -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime support library.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_ADT_H
#define ORC_RT_ADT_H

#include <cstring>
#include <limits>
#include <ostream>
#include <string>

namespace __orc_rt {

constexpr std::size_t dynamic_extent = std::numeric_limits<std::size_t>::max();

/// A substitute for std::span (and llvm::ArrayRef).
/// FIXME: Remove in favor of std::span once we can use c++20.
template <typename T, std::size_t Extent = dynamic_extent> class span {
public:
  typedef T element_type;
  typedef std::remove_cv<T> value_type;
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;
  typedef T *pointer;
  typedef const T *const_pointer;
  typedef T &reference;
  typedef const T &const_reference;

  typedef pointer iterator;

  static constexpr std::size_t extent = Extent;

  constexpr span() noexcept = default;
  constexpr span(T *first, size_type count) noexcept
      : Data(first), Size(count) {}

  template <std::size_t N>
  constexpr span(T (&arr)[N]) noexcept : Data(&arr[0]), Size(N) {}

  constexpr iterator begin() const noexcept { return Data; }
  constexpr iterator end() const noexcept { return Data + Size; }
  constexpr pointer data() const noexcept { return Data; }
  constexpr reference operator[](size_type idx) const { return Data[idx]; }
  constexpr size_type size() const noexcept { return Size; }
  constexpr bool empty() const noexcept { return Size == 0; }

private:
  T *Data = nullptr;
  size_type Size = 0;
};

} // end namespace __orc_rt

#endif // ORC_RT_ADT_H
