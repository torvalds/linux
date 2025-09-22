//===-- xray_segmented_array.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Defines the implementation of a segmented array, with fixed-size segments
// backing the segments.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_SEGMENTED_ARRAY_H
#define XRAY_SEGMENTED_ARRAY_H

#include "sanitizer_common/sanitizer_allocator.h"
#include "xray_allocator.h"
#include "xray_utils.h"
#include <cassert>
#include <type_traits>
#include <utility>

namespace __xray {

/// The Array type provides an interface similar to std::vector<...> but does
/// not shrink in size. Once constructed, elements can be appended but cannot be
/// removed. The implementation is heavily dependent on the contract provided by
/// the Allocator type, in that all memory will be released when the Allocator
/// is destroyed. When an Array is destroyed, it will destroy elements in the
/// backing store but will not free the memory.
template <class T> class Array {
  struct Segment {
    Segment *Prev;
    Segment *Next;
    char Data[1];
  };

public:
  // Each segment of the array will be laid out with the following assumptions:
  //
  //   - Each segment will be on a cache-line address boundary (kCacheLineSize
  //     aligned).
  //
  //   - The elements will be accessed through an aligned pointer, dependent on
  //     the alignment of T.
  //
  //   - Each element is at least two-pointers worth from the beginning of the
  //     Segment, aligned properly, and the rest of the elements are accessed
  //     through appropriate alignment.
  //
  // We then compute the size of the segment to follow this logic:
  //
  //   - Compute the number of elements that can fit within
  //     kCacheLineSize-multiple segments, minus the size of two pointers.
  //
  //   - Request cacheline-multiple sized elements from the allocator.
  static constexpr uint64_t AlignedElementStorageSize = sizeof(T);

  static constexpr uint64_t SegmentControlBlockSize = sizeof(Segment *) * 2;

  static constexpr uint64_t SegmentSize = nearest_boundary(
      SegmentControlBlockSize + next_pow2(sizeof(T)), kCacheLineSize);

  using AllocatorType = Allocator<SegmentSize>;

  static constexpr uint64_t ElementsPerSegment =
      (SegmentSize - SegmentControlBlockSize) / next_pow2(sizeof(T));

  static_assert(ElementsPerSegment > 0,
                "Must have at least 1 element per segment.");

  static Segment SentinelSegment;

  using size_type = uint64_t;

private:
  // This Iterator models a BidirectionalIterator.
  template <class U> class Iterator {
    Segment *S = &SentinelSegment;
    uint64_t Offset = 0;
    uint64_t Size = 0;

  public:
    Iterator(Segment *IS, uint64_t Off, uint64_t S) XRAY_NEVER_INSTRUMENT
        : S(IS),
          Offset(Off),
          Size(S) {}
    Iterator(const Iterator &) NOEXCEPT XRAY_NEVER_INSTRUMENT = default;
    Iterator() NOEXCEPT XRAY_NEVER_INSTRUMENT = default;
    Iterator(Iterator &&) NOEXCEPT XRAY_NEVER_INSTRUMENT = default;
    Iterator &operator=(const Iterator &) XRAY_NEVER_INSTRUMENT = default;
    Iterator &operator=(Iterator &&) XRAY_NEVER_INSTRUMENT = default;
    ~Iterator() XRAY_NEVER_INSTRUMENT = default;

    Iterator &operator++() XRAY_NEVER_INSTRUMENT {
      if (++Offset % ElementsPerSegment || Offset == Size)
        return *this;

      // At this point, we know that Offset % N == 0, so we must advance the
      // segment pointer.
      DCHECK_EQ(Offset % ElementsPerSegment, 0);
      DCHECK_NE(Offset, Size);
      DCHECK_NE(S, &SentinelSegment);
      DCHECK_NE(S->Next, &SentinelSegment);
      S = S->Next;
      DCHECK_NE(S, &SentinelSegment);
      return *this;
    }

    Iterator &operator--() XRAY_NEVER_INSTRUMENT {
      DCHECK_NE(S, &SentinelSegment);
      DCHECK_GT(Offset, 0);

      auto PreviousOffset = Offset--;
      if (PreviousOffset != Size && PreviousOffset % ElementsPerSegment == 0) {
        DCHECK_NE(S->Prev, &SentinelSegment);
        S = S->Prev;
      }

      return *this;
    }

    Iterator operator++(int) XRAY_NEVER_INSTRUMENT {
      Iterator Copy(*this);
      ++(*this);
      return Copy;
    }

    Iterator operator--(int) XRAY_NEVER_INSTRUMENT {
      Iterator Copy(*this);
      --(*this);
      return Copy;
    }

    template <class V, class W>
    friend bool operator==(const Iterator<V> &L,
                           const Iterator<W> &R) XRAY_NEVER_INSTRUMENT {
      return L.S == R.S && L.Offset == R.Offset;
    }

    template <class V, class W>
    friend bool operator!=(const Iterator<V> &L,
                           const Iterator<W> &R) XRAY_NEVER_INSTRUMENT {
      return !(L == R);
    }

    U &operator*() const XRAY_NEVER_INSTRUMENT {
      DCHECK_NE(S, &SentinelSegment);
      auto RelOff = Offset % ElementsPerSegment;

      // We need to compute the character-aligned pointer, offset from the
      // segment's Data location to get the element in the position of Offset.
      auto Base = &S->Data;
      auto AlignedOffset = Base + (RelOff * AlignedElementStorageSize);
      return *reinterpret_cast<U *>(AlignedOffset);
    }

    U *operator->() const XRAY_NEVER_INSTRUMENT { return &(**this); }
  };

  AllocatorType *Alloc;
  Segment *Head;
  Segment *Tail;

  // Here we keep track of segments in the freelist, to allow us to re-use
  // segments when elements are trimmed off the end.
  Segment *Freelist;
  uint64_t Size;

  // ===============================
  // In the following implementation, we work through the algorithms and the
  // list operations using the following notation:
  //
  //   - pred(s) is the predecessor (previous node accessor) and succ(s) is
  //     the successor (next node accessor).
  //
  //   - S is a sentinel segment, which has the following property:
  //
  //         pred(S) == succ(S) == S
  //
  //   - @ is a loop operator, which can imply pred(s) == s if it appears on
  //     the left of s, or succ(s) == S if it appears on the right of s.
  //
  //   - sL <-> sR : means a bidirectional relation between sL and sR, which
  //     means:
  //
  //         succ(sL) == sR && pred(SR) == sL
  //
  //   - sL -> sR : implies a unidirectional relation between sL and SR,
  //     with the following properties:
  //
  //         succ(sL) == sR
  //
  //     sL <- sR : implies a unidirectional relation between sR and sL,
  //     with the following properties:
  //
  //         pred(sR) == sL
  //
  // ===============================

  Segment *NewSegment() XRAY_NEVER_INSTRUMENT {
    // We need to handle the case in which enough elements have been trimmed to
    // allow us to re-use segments we've allocated before. For this we look into
    // the Freelist, to see whether we need to actually allocate new blocks or
    // just re-use blocks we've already seen before.
    if (Freelist != &SentinelSegment) {
      // The current state of lists resemble something like this at this point:
      //
      //   Freelist: @S@<-f0->...<->fN->@S@
      //                  ^ Freelist
      //
      // We want to perform a splice of `f0` from Freelist to a temporary list,
      // which looks like:
      //
      //   Templist: @S@<-f0->@S@
      //                  ^ FreeSegment
      //
      // Our algorithm preconditions are:
      DCHECK_EQ(Freelist->Prev, &SentinelSegment);

      // Then the algorithm we implement is:
      //
      //   SFS = Freelist
      //   Freelist = succ(Freelist)
      //   if (Freelist != S)
      //     pred(Freelist) = S
      //   succ(SFS) = S
      //   pred(SFS) = S
      //
      auto *FreeSegment = Freelist;
      Freelist = Freelist->Next;

      // Note that we need to handle the case where Freelist is now pointing to
      // S, which we don't want to be overwriting.
      // TODO: Determine whether the cost of the branch is higher than the cost
      // of the blind assignment.
      if (Freelist != &SentinelSegment)
        Freelist->Prev = &SentinelSegment;

      FreeSegment->Next = &SentinelSegment;
      FreeSegment->Prev = &SentinelSegment;

      // Our postconditions are:
      DCHECK_EQ(Freelist->Prev, &SentinelSegment);
      DCHECK_NE(FreeSegment, &SentinelSegment);
      return FreeSegment;
    }

    auto SegmentBlock = Alloc->Allocate();
    if (SegmentBlock.Data == nullptr)
      return nullptr;

    // Placement-new the Segment element at the beginning of the SegmentBlock.
    new (SegmentBlock.Data) Segment{&SentinelSegment, &SentinelSegment, {0}};
    auto SB = reinterpret_cast<Segment *>(SegmentBlock.Data);
    return SB;
  }

  Segment *InitHeadAndTail() XRAY_NEVER_INSTRUMENT {
    DCHECK_EQ(Head, &SentinelSegment);
    DCHECK_EQ(Tail, &SentinelSegment);
    auto S = NewSegment();
    if (S == nullptr)
      return nullptr;
    DCHECK_EQ(S->Next, &SentinelSegment);
    DCHECK_EQ(S->Prev, &SentinelSegment);
    DCHECK_NE(S, &SentinelSegment);
    Head = S;
    Tail = S;
    DCHECK_EQ(Head, Tail);
    DCHECK_EQ(Tail->Next, &SentinelSegment);
    DCHECK_EQ(Tail->Prev, &SentinelSegment);
    return S;
  }

  Segment *AppendNewSegment() XRAY_NEVER_INSTRUMENT {
    auto S = NewSegment();
    if (S == nullptr)
      return nullptr;
    DCHECK_NE(Tail, &SentinelSegment);
    DCHECK_EQ(Tail->Next, &SentinelSegment);
    DCHECK_EQ(S->Prev, &SentinelSegment);
    DCHECK_EQ(S->Next, &SentinelSegment);
    S->Prev = Tail;
    Tail->Next = S;
    Tail = S;
    DCHECK_EQ(S, S->Prev->Next);
    DCHECK_EQ(Tail->Next, &SentinelSegment);
    return S;
  }

public:
  explicit Array(AllocatorType &A) XRAY_NEVER_INSTRUMENT
      : Alloc(&A),
        Head(&SentinelSegment),
        Tail(&SentinelSegment),
        Freelist(&SentinelSegment),
        Size(0) {}

  Array() XRAY_NEVER_INSTRUMENT : Alloc(nullptr),
                                  Head(&SentinelSegment),
                                  Tail(&SentinelSegment),
                                  Freelist(&SentinelSegment),
                                  Size(0) {}

  Array(const Array &) = delete;
  Array &operator=(const Array &) = delete;

  Array(Array &&O) XRAY_NEVER_INSTRUMENT : Alloc(O.Alloc),
                                           Head(O.Head),
                                           Tail(O.Tail),
                                           Freelist(O.Freelist),
                                           Size(O.Size) {
    O.Alloc = nullptr;
    O.Head = &SentinelSegment;
    O.Tail = &SentinelSegment;
    O.Size = 0;
    O.Freelist = &SentinelSegment;
  }

  Array &operator=(Array &&O) XRAY_NEVER_INSTRUMENT {
    Alloc = O.Alloc;
    O.Alloc = nullptr;
    Head = O.Head;
    O.Head = &SentinelSegment;
    Tail = O.Tail;
    O.Tail = &SentinelSegment;
    Freelist = O.Freelist;
    O.Freelist = &SentinelSegment;
    Size = O.Size;
    O.Size = 0;
    return *this;
  }

  ~Array() XRAY_NEVER_INSTRUMENT {
    for (auto &E : *this)
      (&E)->~T();
  }

  bool empty() const XRAY_NEVER_INSTRUMENT { return Size == 0; }

  AllocatorType &allocator() const XRAY_NEVER_INSTRUMENT {
    DCHECK_NE(Alloc, nullptr);
    return *Alloc;
  }

  uint64_t size() const XRAY_NEVER_INSTRUMENT { return Size; }

  template <class... Args>
  T *AppendEmplace(Args &&... args) XRAY_NEVER_INSTRUMENT {
    DCHECK((Size == 0 && Head == &SentinelSegment && Head == Tail) ||
           (Size != 0 && Head != &SentinelSegment && Tail != &SentinelSegment));
    if (UNLIKELY(Head == &SentinelSegment)) {
      auto R = InitHeadAndTail();
      if (R == nullptr)
        return nullptr;
    }

    DCHECK_NE(Head, &SentinelSegment);
    DCHECK_NE(Tail, &SentinelSegment);

    auto Offset = Size % ElementsPerSegment;
    if (UNLIKELY(Size != 0 && Offset == 0))
      if (AppendNewSegment() == nullptr)
        return nullptr;

    DCHECK_NE(Tail, &SentinelSegment);
    auto Base = &Tail->Data;
    auto AlignedOffset = Base + (Offset * AlignedElementStorageSize);
    DCHECK_LE(AlignedOffset + sizeof(T),
              reinterpret_cast<unsigned char *>(Base) + SegmentSize);

    // In-place construct at Position.
    new (AlignedOffset) T{std::forward<Args>(args)...};
    ++Size;
    return reinterpret_cast<T *>(AlignedOffset);
  }

  T *Append(const T &E) XRAY_NEVER_INSTRUMENT {
    // FIXME: This is a duplication of AppenEmplace with the copy semantics
    // explicitly used, as a work-around to GCC 4.8 not invoking the copy
    // constructor with the placement new with braced-init syntax.
    DCHECK((Size == 0 && Head == &SentinelSegment && Head == Tail) ||
           (Size != 0 && Head != &SentinelSegment && Tail != &SentinelSegment));
    if (UNLIKELY(Head == &SentinelSegment)) {
      auto R = InitHeadAndTail();
      if (R == nullptr)
        return nullptr;
    }

    DCHECK_NE(Head, &SentinelSegment);
    DCHECK_NE(Tail, &SentinelSegment);

    auto Offset = Size % ElementsPerSegment;
    if (UNLIKELY(Size != 0 && Offset == 0))
      if (AppendNewSegment() == nullptr)
        return nullptr;

    DCHECK_NE(Tail, &SentinelSegment);
    auto Base = &Tail->Data;
    auto AlignedOffset = Base + (Offset * AlignedElementStorageSize);
    DCHECK_LE(AlignedOffset + sizeof(T),
              reinterpret_cast<unsigned char *>(Tail) + SegmentSize);

    // In-place construct at Position.
    new (AlignedOffset) T(E);
    ++Size;
    return reinterpret_cast<T *>(AlignedOffset);
  }

  T &operator[](uint64_t Offset) const XRAY_NEVER_INSTRUMENT {
    DCHECK_LE(Offset, Size);
    // We need to traverse the array enough times to find the element at Offset.
    auto S = Head;
    while (Offset >= ElementsPerSegment) {
      S = S->Next;
      Offset -= ElementsPerSegment;
      DCHECK_NE(S, &SentinelSegment);
    }
    auto Base = &S->Data;
    auto AlignedOffset = Base + (Offset * AlignedElementStorageSize);
    auto Position = reinterpret_cast<T *>(AlignedOffset);
    return *reinterpret_cast<T *>(Position);
  }

  T &front() const XRAY_NEVER_INSTRUMENT {
    DCHECK_NE(Head, &SentinelSegment);
    DCHECK_NE(Size, 0u);
    return *begin();
  }

  T &back() const XRAY_NEVER_INSTRUMENT {
    DCHECK_NE(Tail, &SentinelSegment);
    DCHECK_NE(Size, 0u);
    auto It = end();
    --It;
    return *It;
  }

  template <class Predicate>
  T *find_element(Predicate P) const XRAY_NEVER_INSTRUMENT {
    if (empty())
      return nullptr;

    auto E = end();
    for (auto I = begin(); I != E; ++I)
      if (P(*I))
        return &(*I);

    return nullptr;
  }

  /// Remove N Elements from the end. This leaves the blocks behind, and not
  /// require allocation of new blocks for new elements added after trimming.
  void trim(uint64_t Elements) XRAY_NEVER_INSTRUMENT {
    auto OldSize = Size;
    Elements = Elements > Size ? Size : Elements;
    Size -= Elements;

    // We compute the number of segments we're going to return from the tail by
    // counting how many elements have been trimmed. Given the following:
    //
    // - Each segment has N valid positions, where N > 0
    // - The previous size > current size
    //
    // To compute the number of segments to return, we need to perform the
    // following calculations for the number of segments required given 'x'
    // elements:
    //
    //   f(x) = {
    //            x == 0          : 0
    //          , 0 < x <= N      : 1
    //          , N < x <= max    : x / N + (x % N ? 1 : 0)
    //          }
    //
    // We can simplify this down to:
    //
    //   f(x) = {
    //            x == 0          : 0,
    //          , 0 < x <= max    : x / N + (x < N || x % N ? 1 : 0)
    //          }
    //
    // And further down to:
    //
    //   f(x) = x ? x / N + (x < N || x % N ? 1 : 0) : 0
    //
    // We can then perform the following calculation `s` which counts the number
    // of segments we need to remove from the end of the data structure:
    //
    //   s(p, c) = f(p) - f(c)
    //
    // If we treat p = previous size, and c = current size, and given the
    // properties above, the possible range for s(...) is [0..max(typeof(p))/N]
    // given that typeof(p) == typeof(c).
    auto F = [](uint64_t X) {
      return X ? (X / ElementsPerSegment) +
                     (X < ElementsPerSegment || X % ElementsPerSegment ? 1 : 0)
               : 0;
    };
    auto PS = F(OldSize);
    auto CS = F(Size);
    DCHECK_GE(PS, CS);
    auto SegmentsToTrim = PS - CS;
    for (auto I = 0uL; I < SegmentsToTrim; ++I) {
      // Here we place the current tail segment to the freelist. To do this
      // appropriately, we need to perform a splice operation on two
      // bidirectional linked-lists. In particular, we have the current state of
      // the doubly-linked list of segments:
      //
      //   @S@ <- s0 <-> s1 <-> ... <-> sT -> @S@
      //
      DCHECK_NE(Head, &SentinelSegment);
      DCHECK_NE(Tail, &SentinelSegment);
      DCHECK_EQ(Tail->Next, &SentinelSegment);

      if (Freelist == &SentinelSegment) {
        // Our two lists at this point are in this configuration:
        //
        //   Freelist: (potentially) @S@
        //   Mainlist: @S@<-s0<->s1<->...<->sPT<->sT->@S@
        //                  ^ Head                ^ Tail
        //
        // The end state for us will be this configuration:
        //
        //   Freelist: @S@<-sT->@S@
        //   Mainlist: @S@<-s0<->s1<->...<->sPT->@S@
        //                  ^ Head          ^ Tail
        //
        // The first step for us is to hold a reference to the tail of Mainlist,
        // which in our notation is represented by sT. We call this our "free
        // segment" which is the segment we are placing on the Freelist.
        //
        //   sF = sT
        //
        // Then, we also hold a reference to the "pre-tail" element, which we
        // call sPT:
        //
        //   sPT = pred(sT)
        //
        // We want to splice sT into the beginning of the Freelist, which in
        // an empty Freelist means placing a segment whose predecessor and
        // successor is the sentinel segment.
        //
        // The splice operation then can be performed in the following
        // algorithm:
        //
        //   succ(sPT) = S
        //   pred(sT) = S
        //   succ(sT) = Freelist
        //   Freelist = sT
        //   Tail = sPT
        //
        auto SPT = Tail->Prev;
        SPT->Next = &SentinelSegment;
        Tail->Prev = &SentinelSegment;
        Tail->Next = Freelist;
        Freelist = Tail;
        Tail = SPT;

        // Our post-conditions here are:
        DCHECK_EQ(Tail->Next, &SentinelSegment);
        DCHECK_EQ(Freelist->Prev, &SentinelSegment);
      } else {
        // In the other case, where the Freelist is not empty, we perform the
        // following transformation instead:
        //
        // This transforms the current state:
        //
        //   Freelist: @S@<-f0->@S@
        //                  ^ Freelist
        //   Mainlist: @S@<-s0<->s1<->...<->sPT<->sT->@S@
        //                  ^ Head                ^ Tail
        //
        // Into the following:
        //
        //   Freelist: @S@<-sT<->f0->@S@
        //                  ^ Freelist
        //   Mainlist: @S@<-s0<->s1<->...<->sPT->@S@
        //                  ^ Head          ^ Tail
        //
        // The algorithm is:
        //
        //   sFH = Freelist
        //   sPT = pred(sT)
        //   pred(SFH) = sT
        //   succ(sT) = Freelist
        //   pred(sT) = S
        //   succ(sPT) = S
        //   Tail = sPT
        //   Freelist = sT
        //
        auto SFH = Freelist;
        auto SPT = Tail->Prev;
        auto ST = Tail;
        SFH->Prev = ST;
        ST->Next = Freelist;
        ST->Prev = &SentinelSegment;
        SPT->Next = &SentinelSegment;
        Tail = SPT;
        Freelist = ST;

        // Our post-conditions here are:
        DCHECK_EQ(Tail->Next, &SentinelSegment);
        DCHECK_EQ(Freelist->Prev, &SentinelSegment);
        DCHECK_EQ(Freelist->Next->Prev, Freelist);
      }
    }

    // Now in case we've spliced all the segments in the end, we ensure that the
    // main list is "empty", or both the head and tail pointing to the sentinel
    // segment.
    if (Tail == &SentinelSegment)
      Head = Tail;

    DCHECK(
        (Size == 0 && Head == &SentinelSegment && Tail == &SentinelSegment) ||
        (Size != 0 && Head != &SentinelSegment && Tail != &SentinelSegment));
    DCHECK(
        (Freelist != &SentinelSegment && Freelist->Prev == &SentinelSegment) ||
        (Freelist == &SentinelSegment && Tail->Next == &SentinelSegment));
  }

  // Provide iterators.
  Iterator<T> begin() const XRAY_NEVER_INSTRUMENT {
    return Iterator<T>(Head, 0, Size);
  }
  Iterator<T> end() const XRAY_NEVER_INSTRUMENT {
    return Iterator<T>(Tail, Size, Size);
  }
  Iterator<const T> cbegin() const XRAY_NEVER_INSTRUMENT {
    return Iterator<const T>(Head, 0, Size);
  }
  Iterator<const T> cend() const XRAY_NEVER_INSTRUMENT {
    return Iterator<const T>(Tail, Size, Size);
  }
};

// We need to have this storage definition out-of-line so that the compiler can
// ensure that storage for the SentinelSegment is defined and has a single
// address.
template <class T>
typename Array<T>::Segment Array<T>::SentinelSegment{
    &Array<T>::SentinelSegment, &Array<T>::SentinelSegment, {'\0'}};

} // namespace __xray

#endif // XRAY_SEGMENTED_ARRAY_H
