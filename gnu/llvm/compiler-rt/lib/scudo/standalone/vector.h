//===-- vector.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_VECTOR_H_
#define SCUDO_VECTOR_H_

#include "mem_map.h"

#include <string.h>

namespace scudo {

// A low-level vector based on map. It stores the contents inline up to a fixed
// capacity, or in an external memory buffer if it grows bigger than that. May
// incur a significant memory overhead for small vectors. The current
// implementation supports only POD types.
//
// NOTE: This class is not meant to be used directly, use Vector<T> instead.
template <typename T, size_t StaticNumEntries> class VectorNoCtor {
public:
  T &operator[](uptr I) {
    DCHECK_LT(I, Size);
    return Data[I];
  }
  const T &operator[](uptr I) const {
    DCHECK_LT(I, Size);
    return Data[I];
  }
  void push_back(const T &Element) {
    DCHECK_LE(Size, capacity());
    if (Size == capacity()) {
      const uptr NewCapacity = roundUpPowerOfTwo(Size + 1);
      if (!reallocate(NewCapacity)) {
        return;
      }
    }
    memcpy(&Data[Size++], &Element, sizeof(T));
  }
  T &back() {
    DCHECK_GT(Size, 0);
    return Data[Size - 1];
  }
  void pop_back() {
    DCHECK_GT(Size, 0);
    Size--;
  }
  uptr size() const { return Size; }
  const T *data() const { return Data; }
  T *data() { return Data; }
  constexpr uptr capacity() const { return CapacityBytes / sizeof(T); }
  bool reserve(uptr NewSize) {
    // Never downsize internal buffer.
    if (NewSize > capacity())
      return reallocate(NewSize);
    return true;
  }
  void resize(uptr NewSize) {
    if (NewSize > Size) {
      if (!reserve(NewSize)) {
        return;
      }
      memset(&Data[Size], 0, sizeof(T) * (NewSize - Size));
    }
    Size = NewSize;
  }

  void clear() { Size = 0; }
  bool empty() const { return size() == 0; }

  const T *begin() const { return data(); }
  T *begin() { return data(); }
  const T *end() const { return data() + size(); }
  T *end() { return data() + size(); }

protected:
  constexpr void init(uptr InitialCapacity = 0) {
    Data = &LocalData[0];
    CapacityBytes = sizeof(LocalData);
    if (InitialCapacity > capacity())
      reserve(InitialCapacity);
  }
  void destroy() {
    if (Data != &LocalData[0])
      ExternalBuffer.unmap(ExternalBuffer.getBase(),
                           ExternalBuffer.getCapacity());
  }

private:
  bool reallocate(uptr NewCapacity) {
    DCHECK_GT(NewCapacity, 0);
    DCHECK_LE(Size, NewCapacity);

    MemMapT NewExternalBuffer;
    NewCapacity = roundUp(NewCapacity * sizeof(T), getPageSizeCached());
    if (!NewExternalBuffer.map(/*Addr=*/0U, NewCapacity, "scudo:vector",
                               MAP_ALLOWNOMEM)) {
      return false;
    }
    T *NewExternalData = reinterpret_cast<T *>(NewExternalBuffer.getBase());

    memcpy(NewExternalData, Data, Size * sizeof(T));
    destroy();

    Data = NewExternalData;
    CapacityBytes = NewCapacity;
    ExternalBuffer = NewExternalBuffer;
    return true;
  }

  T *Data = nullptr;
  uptr CapacityBytes = 0;
  uptr Size = 0;

  T LocalData[StaticNumEntries] = {};
  MemMapT ExternalBuffer;
};

template <typename T, size_t StaticNumEntries>
class Vector : public VectorNoCtor<T, StaticNumEntries> {
public:
  static_assert(StaticNumEntries > 0U,
                "Vector must have a non-zero number of static entries.");
  constexpr Vector() { VectorNoCtor<T, StaticNumEntries>::init(); }
  explicit Vector(uptr Count) {
    VectorNoCtor<T, StaticNumEntries>::init(Count);
    this->resize(Count);
  }
  ~Vector() { VectorNoCtor<T, StaticNumEntries>::destroy(); }
  // Disallow copies and moves.
  Vector(const Vector &) = delete;
  Vector &operator=(const Vector &) = delete;
  Vector(Vector &&) = delete;
  Vector &operator=(Vector &&) = delete;
};

} // namespace scudo

#endif // SCUDO_VECTOR_H_
