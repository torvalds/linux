//===- llvm/ADT/BitVector.h - Bit vectors -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the BitVector class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_BITVECTOR_H
#define LLVM_ADT_BITVECTOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>

namespace llvm {

/// ForwardIterator for the bits that are set.
/// Iterators get invalidated when resize / reserve is called.
template <typename BitVectorT> class const_set_bits_iterator_impl {
  const BitVectorT &Parent;
  int Current = 0;

  void advance() {
    assert(Current != -1 && "Trying to advance past end.");
    Current = Parent.find_next(Current);
  }

public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type   = std::ptrdiff_t;
  using value_type        = int;
  using pointer           = value_type*;
  using reference         = value_type&;

  const_set_bits_iterator_impl(const BitVectorT &Parent, int Current)
      : Parent(Parent), Current(Current) {}
  explicit const_set_bits_iterator_impl(const BitVectorT &Parent)
      : const_set_bits_iterator_impl(Parent, Parent.find_first()) {}
  const_set_bits_iterator_impl(const const_set_bits_iterator_impl &) = default;

  const_set_bits_iterator_impl operator++(int) {
    auto Prev = *this;
    advance();
    return Prev;
  }

  const_set_bits_iterator_impl &operator++() {
    advance();
    return *this;
  }

  unsigned operator*() const { return Current; }

  bool operator==(const const_set_bits_iterator_impl &Other) const {
    assert(&Parent == &Other.Parent &&
           "Comparing iterators from different BitVectors");
    return Current == Other.Current;
  }

  bool operator!=(const const_set_bits_iterator_impl &Other) const {
    assert(&Parent == &Other.Parent &&
           "Comparing iterators from different BitVectors");
    return Current != Other.Current;
  }
};

class BitVector {
  typedef uintptr_t BitWord;

  enum { BITWORD_SIZE = (unsigned)sizeof(BitWord) * CHAR_BIT };

  static_assert(BITWORD_SIZE == 64 || BITWORD_SIZE == 32,
                "Unsupported word size");

  using Storage = SmallVector<BitWord>;

  Storage Bits;  // Actual bits.
  unsigned Size = 0; // Size of bitvector in bits.

public:
  using size_type = unsigned;

  // Encapsulation of a single bit.
  class reference {

    BitWord *WordRef;
    unsigned BitPos;

  public:
    reference(BitVector &b, unsigned Idx) {
      WordRef = &b.Bits[Idx / BITWORD_SIZE];
      BitPos = Idx % BITWORD_SIZE;
    }

    reference() = delete;
    reference(const reference&) = default;

    reference &operator=(reference t) {
      *this = bool(t);
      return *this;
    }

    reference& operator=(bool t) {
      if (t)
        *WordRef |= BitWord(1) << BitPos;
      else
        *WordRef &= ~(BitWord(1) << BitPos);
      return *this;
    }

    operator bool() const {
      return ((*WordRef) & (BitWord(1) << BitPos)) != 0;
    }
  };

  typedef const_set_bits_iterator_impl<BitVector> const_set_bits_iterator;
  typedef const_set_bits_iterator set_iterator;

  const_set_bits_iterator set_bits_begin() const {
    return const_set_bits_iterator(*this);
  }
  const_set_bits_iterator set_bits_end() const {
    return const_set_bits_iterator(*this, -1);
  }
  iterator_range<const_set_bits_iterator> set_bits() const {
    return make_range(set_bits_begin(), set_bits_end());
  }

  /// BitVector default ctor - Creates an empty bitvector.
  BitVector() = default;

  /// BitVector ctor - Creates a bitvector of specified number of bits. All
  /// bits are initialized to the specified value.
  explicit BitVector(unsigned s, bool t = false)
      : Bits(NumBitWords(s), 0 - (BitWord)t), Size(s) {
    if (t)
      clear_unused_bits();
  }

  /// empty - Tests whether there are no bits in this bitvector.
  bool empty() const { return Size == 0; }

  /// size - Returns the number of bits in this bitvector.
  size_type size() const { return Size; }

  /// count - Returns the number of bits which are set.
  size_type count() const {
    unsigned NumBits = 0;
    for (auto Bit : Bits)
      NumBits += llvm::popcount(Bit);
    return NumBits;
  }

  /// any - Returns true if any bit is set.
  bool any() const {
    return any_of(Bits, [](BitWord Bit) { return Bit != 0; });
  }

  /// all - Returns true if all bits are set.
  bool all() const {
    for (unsigned i = 0; i < Size / BITWORD_SIZE; ++i)
      if (Bits[i] != ~BitWord(0))
        return false;

    // If bits remain check that they are ones. The unused bits are always zero.
    if (unsigned Remainder = Size % BITWORD_SIZE)
      return Bits[Size / BITWORD_SIZE] == (BitWord(1) << Remainder) - 1;

    return true;
  }

  /// none - Returns true if none of the bits are set.
  bool none() const {
    return !any();
  }

  /// find_first_in - Returns the index of the first set / unset bit,
  /// depending on \p Set, in the range [Begin, End).
  /// Returns -1 if all bits in the range are unset / set.
  int find_first_in(unsigned Begin, unsigned End, bool Set = true) const {
    assert(Begin <= End && End <= Size);
    if (Begin == End)
      return -1;

    unsigned FirstWord = Begin / BITWORD_SIZE;
    unsigned LastWord = (End - 1) / BITWORD_SIZE;

    // Check subsequent words.
    // The code below is based on search for the first _set_ bit. If
    // we're searching for the first _unset_, we just take the
    // complement of each word before we use it and apply
    // the same method.
    for (unsigned i = FirstWord; i <= LastWord; ++i) {
      BitWord Copy = Bits[i];
      if (!Set)
        Copy = ~Copy;

      if (i == FirstWord) {
        unsigned FirstBit = Begin % BITWORD_SIZE;
        Copy &= maskTrailingZeros<BitWord>(FirstBit);
      }

      if (i == LastWord) {
        unsigned LastBit = (End - 1) % BITWORD_SIZE;
        Copy &= maskTrailingOnes<BitWord>(LastBit + 1);
      }
      if (Copy != 0)
        return i * BITWORD_SIZE + llvm::countr_zero(Copy);
    }
    return -1;
  }

  /// find_last_in - Returns the index of the last set bit in the range
  /// [Begin, End).  Returns -1 if all bits in the range are unset.
  int find_last_in(unsigned Begin, unsigned End) const {
    assert(Begin <= End && End <= Size);
    if (Begin == End)
      return -1;

    unsigned LastWord = (End - 1) / BITWORD_SIZE;
    unsigned FirstWord = Begin / BITWORD_SIZE;

    for (unsigned i = LastWord + 1; i >= FirstWord + 1; --i) {
      unsigned CurrentWord = i - 1;

      BitWord Copy = Bits[CurrentWord];
      if (CurrentWord == LastWord) {
        unsigned LastBit = (End - 1) % BITWORD_SIZE;
        Copy &= maskTrailingOnes<BitWord>(LastBit + 1);
      }

      if (CurrentWord == FirstWord) {
        unsigned FirstBit = Begin % BITWORD_SIZE;
        Copy &= maskTrailingZeros<BitWord>(FirstBit);
      }

      if (Copy != 0)
        return (CurrentWord + 1) * BITWORD_SIZE - llvm::countl_zero(Copy) - 1;
    }

    return -1;
  }

  /// find_first_unset_in - Returns the index of the first unset bit in the
  /// range [Begin, End).  Returns -1 if all bits in the range are set.
  int find_first_unset_in(unsigned Begin, unsigned End) const {
    return find_first_in(Begin, End, /* Set = */ false);
  }

  /// find_last_unset_in - Returns the index of the last unset bit in the
  /// range [Begin, End).  Returns -1 if all bits in the range are set.
  int find_last_unset_in(unsigned Begin, unsigned End) const {
    assert(Begin <= End && End <= Size);
    if (Begin == End)
      return -1;

    unsigned LastWord = (End - 1) / BITWORD_SIZE;
    unsigned FirstWord = Begin / BITWORD_SIZE;

    for (unsigned i = LastWord + 1; i >= FirstWord + 1; --i) {
      unsigned CurrentWord = i - 1;

      BitWord Copy = Bits[CurrentWord];
      if (CurrentWord == LastWord) {
        unsigned LastBit = (End - 1) % BITWORD_SIZE;
        Copy |= maskTrailingZeros<BitWord>(LastBit + 1);
      }

      if (CurrentWord == FirstWord) {
        unsigned FirstBit = Begin % BITWORD_SIZE;
        Copy |= maskTrailingOnes<BitWord>(FirstBit);
      }

      if (Copy != ~BitWord(0)) {
        unsigned Result =
            (CurrentWord + 1) * BITWORD_SIZE - llvm::countl_one(Copy) - 1;
        return Result < Size ? Result : -1;
      }
    }
    return -1;
  }

  /// find_first - Returns the index of the first set bit, -1 if none
  /// of the bits are set.
  int find_first() const { return find_first_in(0, Size); }

  /// find_last - Returns the index of the last set bit, -1 if none of the bits
  /// are set.
  int find_last() const { return find_last_in(0, Size); }

  /// find_next - Returns the index of the next set bit following the
  /// "Prev" bit. Returns -1 if the next set bit is not found.
  int find_next(unsigned Prev) const { return find_first_in(Prev + 1, Size); }

  /// find_prev - Returns the index of the first set bit that precedes the
  /// the bit at \p PriorTo.  Returns -1 if all previous bits are unset.
  int find_prev(unsigned PriorTo) const { return find_last_in(0, PriorTo); }

  /// find_first_unset - Returns the index of the first unset bit, -1 if all
  /// of the bits are set.
  int find_first_unset() const { return find_first_unset_in(0, Size); }

  /// find_next_unset - Returns the index of the next unset bit following the
  /// "Prev" bit.  Returns -1 if all remaining bits are set.
  int find_next_unset(unsigned Prev) const {
    return find_first_unset_in(Prev + 1, Size);
  }

  /// find_last_unset - Returns the index of the last unset bit, -1 if all of
  /// the bits are set.
  int find_last_unset() const { return find_last_unset_in(0, Size); }

  /// find_prev_unset - Returns the index of the first unset bit that precedes
  /// the bit at \p PriorTo.  Returns -1 if all previous bits are set.
  int find_prev_unset(unsigned PriorTo) {
    return find_last_unset_in(0, PriorTo);
  }

  /// clear - Removes all bits from the bitvector.
  void clear() {
    Size = 0;
    Bits.clear();
  }

  /// resize - Grow or shrink the bitvector.
  void resize(unsigned N, bool t = false) {
    set_unused_bits(t);
    Size = N;
    Bits.resize(NumBitWords(N), 0 - BitWord(t));
    clear_unused_bits();
  }

  void reserve(unsigned N) { Bits.reserve(NumBitWords(N)); }

  // Set, reset, flip
  BitVector &set() {
    init_words(true);
    clear_unused_bits();
    return *this;
  }

  BitVector &set(unsigned Idx) {
    assert(Idx < Size && "access in bound");
    Bits[Idx / BITWORD_SIZE] |= BitWord(1) << (Idx % BITWORD_SIZE);
    return *this;
  }

  /// set - Efficiently set a range of bits in [I, E)
  BitVector &set(unsigned I, unsigned E) {
    assert(I <= E && "Attempted to set backwards range!");
    assert(E <= size() && "Attempted to set out-of-bounds range!");

    if (I == E) return *this;

    if (I / BITWORD_SIZE == E / BITWORD_SIZE) {
      BitWord EMask = BitWord(1) << (E % BITWORD_SIZE);
      BitWord IMask = BitWord(1) << (I % BITWORD_SIZE);
      BitWord Mask = EMask - IMask;
      Bits[I / BITWORD_SIZE] |= Mask;
      return *this;
    }

    BitWord PrefixMask = ~BitWord(0) << (I % BITWORD_SIZE);
    Bits[I / BITWORD_SIZE] |= PrefixMask;
    I = alignTo(I, BITWORD_SIZE);

    for (; I + BITWORD_SIZE <= E; I += BITWORD_SIZE)
      Bits[I / BITWORD_SIZE] = ~BitWord(0);

    BitWord PostfixMask = (BitWord(1) << (E % BITWORD_SIZE)) - 1;
    if (I < E)
      Bits[I / BITWORD_SIZE] |= PostfixMask;

    return *this;
  }

  BitVector &reset() {
    init_words(false);
    return *this;
  }

  BitVector &reset(unsigned Idx) {
    Bits[Idx / BITWORD_SIZE] &= ~(BitWord(1) << (Idx % BITWORD_SIZE));
    return *this;
  }

  /// reset - Efficiently reset a range of bits in [I, E)
  BitVector &reset(unsigned I, unsigned E) {
    assert(I <= E && "Attempted to reset backwards range!");
    assert(E <= size() && "Attempted to reset out-of-bounds range!");

    if (I == E) return *this;

    if (I / BITWORD_SIZE == E / BITWORD_SIZE) {
      BitWord EMask = BitWord(1) << (E % BITWORD_SIZE);
      BitWord IMask = BitWord(1) << (I % BITWORD_SIZE);
      BitWord Mask = EMask - IMask;
      Bits[I / BITWORD_SIZE] &= ~Mask;
      return *this;
    }

    BitWord PrefixMask = ~BitWord(0) << (I % BITWORD_SIZE);
    Bits[I / BITWORD_SIZE] &= ~PrefixMask;
    I = alignTo(I, BITWORD_SIZE);

    for (; I + BITWORD_SIZE <= E; I += BITWORD_SIZE)
      Bits[I / BITWORD_SIZE] = BitWord(0);

    BitWord PostfixMask = (BitWord(1) << (E % BITWORD_SIZE)) - 1;
    if (I < E)
      Bits[I / BITWORD_SIZE] &= ~PostfixMask;

    return *this;
  }

  BitVector &flip() {
    for (auto &Bit : Bits)
      Bit = ~Bit;
    clear_unused_bits();
    return *this;
  }

  BitVector &flip(unsigned Idx) {
    Bits[Idx / BITWORD_SIZE] ^= BitWord(1) << (Idx % BITWORD_SIZE);
    return *this;
  }

  // Indexing.
  reference operator[](unsigned Idx) {
    assert (Idx < Size && "Out-of-bounds Bit access.");
    return reference(*this, Idx);
  }

  bool operator[](unsigned Idx) const {
    assert (Idx < Size && "Out-of-bounds Bit access.");
    BitWord Mask = BitWord(1) << (Idx % BITWORD_SIZE);
    return (Bits[Idx / BITWORD_SIZE] & Mask) != 0;
  }

  /// Return the last element in the vector.
  bool back() const {
    assert(!empty() && "Getting last element of empty vector.");
    return (*this)[size() - 1];
  }

  bool test(unsigned Idx) const {
    return (*this)[Idx];
  }

  // Push single bit to end of vector.
  void push_back(bool Val) {
    unsigned OldSize = Size;
    unsigned NewSize = Size + 1;

    // Resize, which will insert zeros.
    // If we already fit then the unused bits will be already zero.
    if (NewSize > getBitCapacity())
      resize(NewSize, false);
    else
      Size = NewSize;

    // If true, set single bit.
    if (Val)
      set(OldSize);
  }

  /// Pop one bit from the end of the vector.
  void pop_back() {
    assert(!empty() && "Empty vector has no element to pop.");
    resize(size() - 1);
  }

  /// Test if any common bits are set.
  bool anyCommon(const BitVector &RHS) const {
    unsigned ThisWords = Bits.size();
    unsigned RHSWords = RHS.Bits.size();
    for (unsigned i = 0, e = std::min(ThisWords, RHSWords); i != e; ++i)
      if (Bits[i] & RHS.Bits[i])
        return true;
    return false;
  }

  // Comparison operators.
  bool operator==(const BitVector &RHS) const {
    if (size() != RHS.size())
      return false;
    unsigned NumWords = Bits.size();
    return std::equal(Bits.begin(), Bits.begin() + NumWords, RHS.Bits.begin());
  }

  bool operator!=(const BitVector &RHS) const { return !(*this == RHS); }

  /// Intersection, union, disjoint union.
  BitVector &operator&=(const BitVector &RHS) {
    unsigned ThisWords = Bits.size();
    unsigned RHSWords = RHS.Bits.size();
    unsigned i;
    for (i = 0; i != std::min(ThisWords, RHSWords); ++i)
      Bits[i] &= RHS.Bits[i];

    // Any bits that are just in this bitvector become zero, because they aren't
    // in the RHS bit vector.  Any words only in RHS are ignored because they
    // are already zero in the LHS.
    for (; i != ThisWords; ++i)
      Bits[i] = 0;

    return *this;
  }

  /// reset - Reset bits that are set in RHS. Same as *this &= ~RHS.
  BitVector &reset(const BitVector &RHS) {
    unsigned ThisWords = Bits.size();
    unsigned RHSWords = RHS.Bits.size();
    for (unsigned i = 0; i != std::min(ThisWords, RHSWords); ++i)
      Bits[i] &= ~RHS.Bits[i];
    return *this;
  }

  /// test - Check if (This - RHS) is zero.
  /// This is the same as reset(RHS) and any().
  bool test(const BitVector &RHS) const {
    unsigned ThisWords = Bits.size();
    unsigned RHSWords = RHS.Bits.size();
    unsigned i;
    for (i = 0; i != std::min(ThisWords, RHSWords); ++i)
      if ((Bits[i] & ~RHS.Bits[i]) != 0)
        return true;

    for (; i != ThisWords ; ++i)
      if (Bits[i] != 0)
        return true;

    return false;
  }

  template <class F, class... ArgTys>
  static BitVector &apply(F &&f, BitVector &Out, BitVector const &Arg,
                          ArgTys const &...Args) {
    assert(llvm::all_of(
               std::initializer_list<unsigned>{Args.size()...},
               [&Arg](auto const &BV) { return Arg.size() == BV; }) &&
           "consistent sizes");
    Out.resize(Arg.size());
    for (size_type I = 0, E = Arg.Bits.size(); I != E; ++I)
      Out.Bits[I] = f(Arg.Bits[I], Args.Bits[I]...);
    Out.clear_unused_bits();
    return Out;
  }

  BitVector &operator|=(const BitVector &RHS) {
    if (size() < RHS.size())
      resize(RHS.size());
    for (size_type I = 0, E = RHS.Bits.size(); I != E; ++I)
      Bits[I] |= RHS.Bits[I];
    return *this;
  }

  BitVector &operator^=(const BitVector &RHS) {
    if (size() < RHS.size())
      resize(RHS.size());
    for (size_type I = 0, E = RHS.Bits.size(); I != E; ++I)
      Bits[I] ^= RHS.Bits[I];
    return *this;
  }

  BitVector &operator>>=(unsigned N) {
    assert(N <= Size);
    if (LLVM_UNLIKELY(empty() || N == 0))
      return *this;

    unsigned NumWords = Bits.size();
    assert(NumWords >= 1);

    wordShr(N / BITWORD_SIZE);

    unsigned BitDistance = N % BITWORD_SIZE;
    if (BitDistance == 0)
      return *this;

    // When the shift size is not a multiple of the word size, then we have
    // a tricky situation where each word in succession needs to extract some
    // of the bits from the next word and or them into this word while
    // shifting this word to make room for the new bits.  This has to be done
    // for every word in the array.

    // Since we're shifting each word right, some bits will fall off the end
    // of each word to the right, and empty space will be created on the left.
    // The final word in the array will lose bits permanently, so starting at
    // the beginning, work forwards shifting each word to the right, and
    // OR'ing in the bits from the end of the next word to the beginning of
    // the current word.

    // Example:
    //   Starting with {0xAABBCCDD, 0xEEFF0011, 0x22334455} and shifting right
    //   by 4 bits.
    // Step 1: Word[0] >>= 4           ; 0x0ABBCCDD
    // Step 2: Word[0] |= 0x10000000   ; 0x1ABBCCDD
    // Step 3: Word[1] >>= 4           ; 0x0EEFF001
    // Step 4: Word[1] |= 0x50000000   ; 0x5EEFF001
    // Step 5: Word[2] >>= 4           ; 0x02334455
    // Result: { 0x1ABBCCDD, 0x5EEFF001, 0x02334455 }
    const BitWord Mask = maskTrailingOnes<BitWord>(BitDistance);
    const unsigned LSH = BITWORD_SIZE - BitDistance;

    for (unsigned I = 0; I < NumWords - 1; ++I) {
      Bits[I] >>= BitDistance;
      Bits[I] |= (Bits[I + 1] & Mask) << LSH;
    }

    Bits[NumWords - 1] >>= BitDistance;

    return *this;
  }

  BitVector &operator<<=(unsigned N) {
    assert(N <= Size);
    if (LLVM_UNLIKELY(empty() || N == 0))
      return *this;

    unsigned NumWords = Bits.size();
    assert(NumWords >= 1);

    wordShl(N / BITWORD_SIZE);

    unsigned BitDistance = N % BITWORD_SIZE;
    if (BitDistance == 0)
      return *this;

    // When the shift size is not a multiple of the word size, then we have
    // a tricky situation where each word in succession needs to extract some
    // of the bits from the previous word and or them into this word while
    // shifting this word to make room for the new bits.  This has to be done
    // for every word in the array.  This is similar to the algorithm outlined
    // in operator>>=, but backwards.

    // Since we're shifting each word left, some bits will fall off the end
    // of each word to the left, and empty space will be created on the right.
    // The first word in the array will lose bits permanently, so starting at
    // the end, work backwards shifting each word to the left, and OR'ing
    // in the bits from the end of the next word to the beginning of the
    // current word.

    // Example:
    //   Starting with {0xAABBCCDD, 0xEEFF0011, 0x22334455} and shifting left
    //   by 4 bits.
    // Step 1: Word[2] <<= 4           ; 0x23344550
    // Step 2: Word[2] |= 0x0000000E   ; 0x2334455E
    // Step 3: Word[1] <<= 4           ; 0xEFF00110
    // Step 4: Word[1] |= 0x0000000A   ; 0xEFF0011A
    // Step 5: Word[0] <<= 4           ; 0xABBCCDD0
    // Result: { 0xABBCCDD0, 0xEFF0011A, 0x2334455E }
    const BitWord Mask = maskLeadingOnes<BitWord>(BitDistance);
    const unsigned RSH = BITWORD_SIZE - BitDistance;

    for (int I = NumWords - 1; I > 0; --I) {
      Bits[I] <<= BitDistance;
      Bits[I] |= (Bits[I - 1] & Mask) >> RSH;
    }
    Bits[0] <<= BitDistance;
    clear_unused_bits();

    return *this;
  }

  void swap(BitVector &RHS) {
    std::swap(Bits, RHS.Bits);
    std::swap(Size, RHS.Size);
  }

  void invalid() {
    assert(!Size && Bits.empty());
    Size = (unsigned)-1;
  }
  bool isInvalid() const { return Size == (unsigned)-1; }

  ArrayRef<BitWord> getData() const { return {Bits.data(), Bits.size()}; }

  //===--------------------------------------------------------------------===//
  // Portable bit mask operations.
  //===--------------------------------------------------------------------===//
  //
  // These methods all operate on arrays of uint32_t, each holding 32 bits. The
  // fixed word size makes it easier to work with literal bit vector constants
  // in portable code.
  //
  // The LSB in each word is the lowest numbered bit.  The size of a portable
  // bit mask is always a whole multiple of 32 bits.  If no bit mask size is
  // given, the bit mask is assumed to cover the entire BitVector.

  /// setBitsInMask - Add '1' bits from Mask to this vector. Don't resize.
  /// This computes "*this |= Mask".
  void setBitsInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    applyMask<true, false>(Mask, MaskWords);
  }

  /// clearBitsInMask - Clear any bits in this vector that are set in Mask.
  /// Don't resize. This computes "*this &= ~Mask".
  void clearBitsInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    applyMask<false, false>(Mask, MaskWords);
  }

  /// setBitsNotInMask - Add a bit to this vector for every '0' bit in Mask.
  /// Don't resize.  This computes "*this |= ~Mask".
  void setBitsNotInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    applyMask<true, true>(Mask, MaskWords);
  }

  /// clearBitsNotInMask - Clear a bit in this vector for every '0' bit in Mask.
  /// Don't resize.  This computes "*this &= Mask".
  void clearBitsNotInMask(const uint32_t *Mask, unsigned MaskWords = ~0u) {
    applyMask<false, true>(Mask, MaskWords);
  }

private:
  /// Perform a logical left shift of \p Count words by moving everything
  /// \p Count words to the right in memory.
  ///
  /// While confusing, words are stored from least significant at Bits[0] to
  /// most significant at Bits[NumWords-1].  A logical shift left, however,
  /// moves the current least significant bit to a higher logical index, and
  /// fills the previous least significant bits with 0.  Thus, we actually
  /// need to move the bytes of the memory to the right, not to the left.
  /// Example:
  ///   Words = [0xBBBBAAAA, 0xDDDDFFFF, 0x00000000, 0xDDDD0000]
  /// represents a BitVector where 0xBBBBAAAA contain the least significant
  /// bits.  So if we want to shift the BitVector left by 2 words, we need
  /// to turn this into 0x00000000 0x00000000 0xBBBBAAAA 0xDDDDFFFF by using a
  /// memmove which moves right, not left.
  void wordShl(uint32_t Count) {
    if (Count == 0)
      return;

    uint32_t NumWords = Bits.size();

    // Since we always move Word-sized chunks of data with src and dest both
    // aligned to a word-boundary, we don't need to worry about endianness
    // here.
    std::copy(Bits.begin(), Bits.begin() + NumWords - Count,
              Bits.begin() + Count);
    std::fill(Bits.begin(), Bits.begin() + Count, 0);
    clear_unused_bits();
  }

  /// Perform a logical right shift of \p Count words by moving those
  /// words to the left in memory.  See wordShl for more information.
  ///
  void wordShr(uint32_t Count) {
    if (Count == 0)
      return;

    uint32_t NumWords = Bits.size();

    std::copy(Bits.begin() + Count, Bits.begin() + NumWords, Bits.begin());
    std::fill(Bits.begin() + NumWords - Count, Bits.begin() + NumWords, 0);
  }

  int next_unset_in_word(int WordIndex, BitWord Word) const {
    unsigned Result = WordIndex * BITWORD_SIZE + llvm::countr_one(Word);
    return Result < size() ? Result : -1;
  }

  unsigned NumBitWords(unsigned S) const {
    return (S + BITWORD_SIZE-1) / BITWORD_SIZE;
  }

  // Set the unused bits in the high words.
  void set_unused_bits(bool t = true) {
    //  Then set any stray high bits of the last used word.
    if (unsigned ExtraBits = Size % BITWORD_SIZE) {
      BitWord ExtraBitMask = ~BitWord(0) << ExtraBits;
      if (t)
        Bits.back() |= ExtraBitMask;
      else
        Bits.back() &= ~ExtraBitMask;
    }
  }

  // Clear the unused bits in the high words.
  void clear_unused_bits() {
    set_unused_bits(false);
  }

  void init_words(bool t) {
    std::fill(Bits.begin(), Bits.end(), 0 - (BitWord)t);
  }

  template<bool AddBits, bool InvertMask>
  void applyMask(const uint32_t *Mask, unsigned MaskWords) {
    static_assert(BITWORD_SIZE % 32 == 0, "Unsupported BitWord size.");
    MaskWords = std::min(MaskWords, (size() + 31) / 32);
    const unsigned Scale = BITWORD_SIZE / 32;
    unsigned i;
    for (i = 0; MaskWords >= Scale; ++i, MaskWords -= Scale) {
      BitWord BW = Bits[i];
      // This inner loop should unroll completely when BITWORD_SIZE > 32.
      for (unsigned b = 0; b != BITWORD_SIZE; b += 32) {
        uint32_t M = *Mask++;
        if (InvertMask) M = ~M;
        if (AddBits) BW |=   BitWord(M) << b;
        else         BW &= ~(BitWord(M) << b);
      }
      Bits[i] = BW;
    }
    for (unsigned b = 0; MaskWords; b += 32, --MaskWords) {
      uint32_t M = *Mask++;
      if (InvertMask) M = ~M;
      if (AddBits) Bits[i] |=   BitWord(M) << b;
      else         Bits[i] &= ~(BitWord(M) << b);
    }
    if (AddBits)
      clear_unused_bits();
  }

public:
  /// Return the size (in bytes) of the bit vector.
  size_type getMemorySize() const { return Bits.size() * sizeof(BitWord); }
  size_type getBitCapacity() const { return Bits.size() * BITWORD_SIZE; }
};

inline BitVector::size_type capacity_in_bytes(const BitVector &X) {
  return X.getMemorySize();
}

template <> struct DenseMapInfo<BitVector> {
  static inline BitVector getEmptyKey() { return {}; }
  static inline BitVector getTombstoneKey() {
    BitVector V;
    V.invalid();
    return V;
  }
  static unsigned getHashValue(const BitVector &V) {
    return DenseMapInfo<std::pair<BitVector::size_type, ArrayRef<uintptr_t>>>::
        getHashValue(std::make_pair(V.size(), V.getData()));
  }
  static bool isEqual(const BitVector &LHS, const BitVector &RHS) {
    if (LHS.isInvalid() || RHS.isInvalid())
      return LHS.isInvalid() == RHS.isInvalid();
    return LHS == RHS;
  }
};
} // end namespace llvm

namespace std {
  /// Implement std::swap in terms of BitVector swap.
inline void swap(llvm::BitVector &LHS, llvm::BitVector &RHS) { LHS.swap(RHS); }
} // end namespace std

#endif // LLVM_ADT_BITVECTOR_H
