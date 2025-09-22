//===- llvm/ADT/EnumeratedArray.h - Enumerated Array-------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines an array type that can be indexed using scoped enum
/// values.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ENUMERATEDARRAY_H
#define LLVM_ADT_ENUMERATEDARRAY_H

#include <cassert>
#include <iterator>

namespace llvm {

template <typename ValueType, typename Enumeration,
          Enumeration LargestEnum = Enumeration::Last, typename IndexType = int,
          IndexType Size = 1 + static_cast<IndexType>(LargestEnum)>
class EnumeratedArray {
public:
  using iterator = ValueType *;
  using const_iterator = const ValueType *;

  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator = std::reverse_iterator<iterator>;

  using value_type = ValueType;
  using reference = ValueType &;
  using const_reference = const ValueType &;
  using pointer = ValueType *;
  using const_pointer = const ValueType *;

  EnumeratedArray() = default;
  EnumeratedArray(ValueType V) {
    for (IndexType IX = 0; IX < Size; ++IX) {
      Underlying[IX] = V;
    }
  }
  EnumeratedArray(std::initializer_list<ValueType> Init) {
    assert(Init.size() == Size && "Incorrect initializer size");
    for (IndexType IX = 0; IX < Size; ++IX) {
      Underlying[IX] = *(Init.begin() + IX);
    }
  }

  const ValueType &operator[](Enumeration Index) const {
    auto IX = static_cast<IndexType>(Index);
    assert(IX >= 0 && IX < Size && "Index is out of bounds.");
    return Underlying[IX];
  }
  ValueType &operator[](Enumeration Index) {
    return const_cast<ValueType &>(
        static_cast<const EnumeratedArray<ValueType, Enumeration, LargestEnum,
                                          IndexType, Size> &>(*this)[Index]);
  }
  IndexType size() const { return Size; }
  bool empty() const { return size() == 0; }

  iterator begin() { return Underlying; }
  const_iterator begin() const { return Underlying; }

  iterator end() { return begin() + size(); }
  const_iterator end() const { return begin() + size(); }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

private:
  ValueType Underlying[Size];
};

} // namespace llvm

#endif // LLVM_ADT_ENUMERATEDARRAY_H
