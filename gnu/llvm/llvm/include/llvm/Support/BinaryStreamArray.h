//===- BinaryStreamArray.h - Array backed by an arbitrary stream *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Lightweight arrays that are backed by an arbitrary BinaryStream.  This file
/// provides two different array implementations.
///
///     VarStreamArray - Arrays of variable length records.  The user specifies
///       an Extractor type that can extract a record from a given offset and
///       return the number of bytes consumed by the record.
///
///     FixedStreamArray - Arrays of fixed length records.  This is similar in
///       spirit to ArrayRef<T>, but since it is backed by a BinaryStream, the
///       elements of the array need not be laid out in contiguous memory.
///

#ifndef LLVM_SUPPORT_BINARYSTREAMARRAY_H
#define LLVM_SUPPORT_BINARYSTREAMARRAY_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstdint>

namespace llvm {

/// VarStreamArrayExtractor is intended to be specialized to provide customized
/// extraction logic.  On input it receives a BinaryStreamRef pointing to the
/// beginning of the next record, but where the length of the record is not yet
/// known.  Upon completion, it should return an appropriate Error instance if
/// a record could not be extracted, or if one could be extracted it should
/// return success and set Len to the number of bytes this record occupied in
/// the underlying stream, and it should fill out the fields of the value type
/// Item appropriately to represent the current record.
///
/// You can specialize this template for your own custom value types to avoid
/// having to specify a second template argument to VarStreamArray (documented
/// below).
template <typename T> struct VarStreamArrayExtractor {
  // Method intentionally deleted.  You must provide an explicit specialization
  // with the following method implemented.
  Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                   T &Item) const = delete;
};

/// VarStreamArray represents an array of variable length records backed by a
/// stream.  This could be a contiguous sequence of bytes in memory, it could
/// be a file on disk, or it could be a PDB stream where bytes are stored as
/// discontiguous blocks in a file.  Usually it is desirable to treat arrays
/// as contiguous blocks of memory, but doing so with large PDB files, for
/// example, could mean allocating huge amounts of memory just to allow
/// re-ordering of stream data to be contiguous before iterating over it.  By
/// abstracting this out, we need not duplicate this memory, and we can
/// iterate over arrays in arbitrarily formatted streams.  Elements are parsed
/// lazily on iteration, so there is no upfront cost associated with building
/// or copying a VarStreamArray, no matter how large it may be.
///
/// You create a VarStreamArray by specifying a ValueType and an Extractor type.
/// If you do not specify an Extractor type, you are expected to specialize
/// VarStreamArrayExtractor<T> for your ValueType.
///
/// By default an Extractor is default constructed in the class, but in some
/// cases you might find it useful for an Extractor to maintain state across
/// extractions.  In this case you can provide your own Extractor through a
/// secondary constructor.  The following examples show various ways of
/// creating a VarStreamArray.
///
///       // Will use VarStreamArrayExtractor<MyType> as the extractor.
///       VarStreamArray<MyType> MyTypeArray;
///
///       // Will use a default-constructed MyExtractor as the extractor.
///       VarStreamArray<MyType, MyExtractor> MyTypeArray2;
///
///       // Will use the specific instance of MyExtractor provided.
///       // MyExtractor need not be default-constructible in this case.
///       MyExtractor E(SomeContext);
///       VarStreamArray<MyType, MyExtractor> MyTypeArray3(E);
///

template <typename ValueType, typename Extractor> class VarStreamArrayIterator;

template <typename ValueType,
          typename Extractor = VarStreamArrayExtractor<ValueType>>
class VarStreamArray {
  friend class VarStreamArrayIterator<ValueType, Extractor>;

public:
  typedef VarStreamArrayIterator<ValueType, Extractor> Iterator;

  VarStreamArray() = default;

  explicit VarStreamArray(const Extractor &E) : E(E) {}

  explicit VarStreamArray(BinaryStreamRef Stream, uint32_t Skew = 0)
      : Stream(Stream), Skew(Skew) {}

  VarStreamArray(BinaryStreamRef Stream, const Extractor &E, uint32_t Skew = 0)
      : Stream(Stream), E(E), Skew(Skew) {}

  Iterator begin(bool *HadError = nullptr) const {
    return Iterator(*this, E, Skew, nullptr);
  }

  bool valid() const { return Stream.valid(); }

  bool isOffsetValid(uint32_t Offset) const { return at(Offset) != end(); }

  uint32_t skew() const { return Skew; }
  Iterator end() const { return Iterator(E); }

  bool empty() const { return Stream.getLength() == 0; }

  VarStreamArray<ValueType, Extractor> substream(uint32_t Begin,
                                                 uint32_t End) const {
    assert(Begin >= Skew);
    // We should never cut off the beginning of the stream since it might be
    // skewed, meaning the initial bytes are important.
    BinaryStreamRef NewStream = Stream.slice(0, End);
    return {NewStream, E, Begin};
  }

  /// given an offset into the array's underlying stream, return an
  /// iterator to the record at that offset.  This is considered unsafe
  /// since the behavior is undefined if \p Offset does not refer to the
  /// beginning of a valid record.
  Iterator at(uint32_t Offset) const {
    return Iterator(*this, E, Offset, nullptr);
  }

  const Extractor &getExtractor() const { return E; }
  Extractor &getExtractor() { return E; }

  BinaryStreamRef getUnderlyingStream() const { return Stream; }
  void setUnderlyingStream(BinaryStreamRef NewStream, uint32_t NewSkew = 0) {
    Stream = NewStream;
    Skew = NewSkew;
  }

  void drop_front() { Skew += begin()->length(); }

private:
  BinaryStreamRef Stream;
  Extractor E;
  uint32_t Skew = 0;
};

template <typename ValueType, typename Extractor>
class VarStreamArrayIterator
    : public iterator_facade_base<VarStreamArrayIterator<ValueType, Extractor>,
                                  std::forward_iterator_tag, const ValueType> {
  typedef VarStreamArrayIterator<ValueType, Extractor> IterType;
  typedef VarStreamArray<ValueType, Extractor> ArrayType;

public:
  VarStreamArrayIterator(const ArrayType &Array, const Extractor &E,
                         uint32_t Offset, bool *HadError)
      : IterRef(Array.Stream.drop_front(Offset)), Extract(E),
        Array(&Array), AbsOffset(Offset), HadError(HadError) {
    if (IterRef.getLength() == 0)
      moveToEnd();
    else {
      auto EC = Extract(IterRef, ThisLen, ThisValue);
      if (EC) {
        consumeError(std::move(EC));
        markError();
      }
    }
  }

  VarStreamArrayIterator() = default;
  explicit VarStreamArrayIterator(const Extractor &E) : Extract(E) {}
  ~VarStreamArrayIterator() = default;

  bool operator==(const IterType &R) const {
    if (Array && R.Array) {
      // Both have a valid array, make sure they're same.
      assert(Array == R.Array);
      return IterRef == R.IterRef;
    }

    // Both iterators are at the end.
    if (!Array && !R.Array)
      return true;

    // One is not at the end and one is.
    return false;
  }

  const ValueType &operator*() const {
    assert(Array && !HasError);
    return ThisValue;
  }

  IterType &operator+=(unsigned N) {
    for (unsigned I = 0; I < N; ++I) {
      // We are done with the current record, discard it so that we are
      // positioned at the next record.
      AbsOffset += ThisLen;
      IterRef = IterRef.drop_front(ThisLen);
      if (IterRef.getLength() == 0) {
        // There is nothing after the current record, we must make this an end
        // iterator.
        moveToEnd();
      } else {
        // There is some data after the current record.
        auto EC = Extract(IterRef, ThisLen, ThisValue);
        if (EC) {
          consumeError(std::move(EC));
          markError();
        } else if (ThisLen == 0) {
          // An empty record? Make this an end iterator.
          moveToEnd();
        }
      }
    }
    return *this;
  }

  uint32_t offset() const { return AbsOffset; }
  uint32_t getRecordLength() const { return ThisLen; }

private:
  void moveToEnd() {
    Array = nullptr;
    ThisLen = 0;
  }
  void markError() {
    moveToEnd();
    HasError = true;
    if (HadError != nullptr)
      *HadError = true;
  }

  ValueType ThisValue;
  BinaryStreamRef IterRef;
  Extractor Extract;
  const ArrayType *Array{nullptr};
  uint32_t ThisLen{0};
  uint32_t AbsOffset{0};
  bool HasError{false};
  bool *HadError{nullptr};
};

template <typename T> class FixedStreamArrayIterator;

/// FixedStreamArray is similar to VarStreamArray, except with each record
/// having a fixed-length.  As with VarStreamArray, there is no upfront
/// cost associated with building or copying a FixedStreamArray, as the
/// memory for each element is not read from the backing stream until that
/// element is iterated.
template <typename T> class FixedStreamArray {
  friend class FixedStreamArrayIterator<T>;

public:
  typedef FixedStreamArrayIterator<T> Iterator;

  FixedStreamArray() = default;
  explicit FixedStreamArray(BinaryStreamRef Stream) : Stream(Stream) {
    assert(Stream.getLength() % sizeof(T) == 0);
  }

  bool operator==(const FixedStreamArray<T> &Other) const {
    return Stream == Other.Stream;
  }

  bool operator!=(const FixedStreamArray<T> &Other) const {
    return !(*this == Other);
  }

  FixedStreamArray(const FixedStreamArray &) = default;
  FixedStreamArray &operator=(const FixedStreamArray &) = default;

  const T &operator[](uint32_t Index) const {
    assert(Index < size());
    uint32_t Off = Index * sizeof(T);
    ArrayRef<uint8_t> Data;
    if (auto EC = Stream.readBytes(Off, sizeof(T), Data)) {
      assert(false && "Unexpected failure reading from stream");
      // This should never happen since we asserted that the stream length was
      // an exact multiple of the element size.
      consumeError(std::move(EC));
    }
    assert(isAddrAligned(Align::Of<T>(), Data.data()));
    return *reinterpret_cast<const T *>(Data.data());
  }

  uint32_t size() const { return Stream.getLength() / sizeof(T); }

  bool empty() const { return size() == 0; }

  FixedStreamArrayIterator<T> begin() const {
    return FixedStreamArrayIterator<T>(*this, 0);
  }

  FixedStreamArrayIterator<T> end() const {
    return FixedStreamArrayIterator<T>(*this, size());
  }

  const T &front() const { return *begin(); }
  const T &back() const {
    FixedStreamArrayIterator<T> I = end();
    return *(--I);
  }

  BinaryStreamRef getUnderlyingStream() const { return Stream; }

private:
  BinaryStreamRef Stream;
};

template <typename T>
class FixedStreamArrayIterator
    : public iterator_facade_base<FixedStreamArrayIterator<T>,
                                  std::random_access_iterator_tag, const T> {

public:
  FixedStreamArrayIterator(const FixedStreamArray<T> &Array, uint32_t Index)
      : Array(Array), Index(Index) {}

  FixedStreamArrayIterator(const FixedStreamArrayIterator<T> &Other)
      : Array(Other.Array), Index(Other.Index) {}
  FixedStreamArrayIterator<T> &
  operator=(const FixedStreamArrayIterator<T> &Other) {
    Array = Other.Array;
    Index = Other.Index;
    return *this;
  }

  const T &operator*() const { return Array[Index]; }
  const T &operator*() { return Array[Index]; }

  bool operator==(const FixedStreamArrayIterator<T> &R) const {
    assert(Array == R.Array);
    return (Index == R.Index) && (Array == R.Array);
  }

  FixedStreamArrayIterator<T> &operator+=(std::ptrdiff_t N) {
    Index += N;
    return *this;
  }

  FixedStreamArrayIterator<T> &operator-=(std::ptrdiff_t N) {
    assert(std::ptrdiff_t(Index) >= N);
    Index -= N;
    return *this;
  }

  std::ptrdiff_t operator-(const FixedStreamArrayIterator<T> &R) const {
    assert(Array == R.Array);
    assert(Index >= R.Index);
    return Index - R.Index;
  }

  bool operator<(const FixedStreamArrayIterator<T> &RHS) const {
    assert(Array == RHS.Array);
    return Index < RHS.Index;
  }

private:
  FixedStreamArray<T> Array;
  uint32_t Index;
};

} // namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMARRAY_H
