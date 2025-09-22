//===- llvm/ADT/PointerIntPair.h - Pair for pointer and int -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the PointerIntPair class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_POINTERINTPAIR_H
#define LLVM_ADT_POINTERINTPAIR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>

namespace llvm {

namespace detail {
template <typename Ptr> struct PunnedPointer {
  static_assert(sizeof(Ptr) == sizeof(intptr_t), "");

  // Asserts that allow us to let the compiler implement the destructor and
  // copy/move constructors
  static_assert(std::is_trivially_destructible<Ptr>::value, "");
  static_assert(std::is_trivially_copy_constructible<Ptr>::value, "");
  static_assert(std::is_trivially_move_constructible<Ptr>::value, "");

  explicit constexpr PunnedPointer(intptr_t i = 0) { *this = i; }

  constexpr intptr_t asInt() const {
    intptr_t R = 0;
    std::memcpy(&R, Data, sizeof(R));
    return R;
  }

  constexpr operator intptr_t() const { return asInt(); }

  constexpr PunnedPointer &operator=(intptr_t V) {
    std::memcpy(Data, &V, sizeof(Data));
    return *this;
  }

  Ptr *getPointerAddress() { return reinterpret_cast<Ptr *>(Data); }
  const Ptr *getPointerAddress() const { return reinterpret_cast<Ptr *>(Data); }

private:
  alignas(Ptr) unsigned char Data[sizeof(Ptr)];
};
} // namespace detail

template <typename T, typename Enable> struct DenseMapInfo;
template <typename PointerT, unsigned IntBits, typename PtrTraits>
struct PointerIntPairInfo;

/// PointerIntPair - This class implements a pair of a pointer and small
/// integer.  It is designed to represent this in the space required by one
/// pointer by bitmangling the integer into the low part of the pointer.  This
/// can only be done for small integers: typically up to 3 bits, but it depends
/// on the number of bits available according to PointerLikeTypeTraits for the
/// type.
///
/// Note that PointerIntPair always puts the IntVal part in the highest bits
/// possible.  For example, PointerIntPair<void*, 1, bool> will put the bit for
/// the bool into bit #2, not bit #0, which allows the low two bits to be used
/// for something else.  For example, this allows:
///   PointerIntPair<PointerIntPair<void*, 1, bool>, 1, bool>
/// ... and the two bools will land in different bits.
template <typename PointerTy, unsigned IntBits, typename IntType = unsigned,
          typename PtrTraits = PointerLikeTypeTraits<PointerTy>,
          typename Info = PointerIntPairInfo<PointerTy, IntBits, PtrTraits>>
class PointerIntPair {
  // Used by MSVC visualizer and generally helpful for debugging/visualizing.
  using InfoTy = Info;
  detail::PunnedPointer<PointerTy> Value;

public:
  constexpr PointerIntPair() = default;

  PointerIntPair(PointerTy PtrVal, IntType IntVal) {
    setPointerAndInt(PtrVal, IntVal);
  }

  explicit PointerIntPair(PointerTy PtrVal) { initWithPointer(PtrVal); }

  PointerTy getPointer() const { return Info::getPointer(Value); }

  IntType getInt() const { return (IntType)Info::getInt(Value); }

  void setPointer(PointerTy PtrVal) & {
    Value = Info::updatePointer(Value, PtrVal);
  }

  void setInt(IntType IntVal) & {
    Value = Info::updateInt(Value, static_cast<intptr_t>(IntVal));
  }

  void initWithPointer(PointerTy PtrVal) & {
    Value = Info::updatePointer(0, PtrVal);
  }

  void setPointerAndInt(PointerTy PtrVal, IntType IntVal) & {
    Value = Info::updateInt(Info::updatePointer(0, PtrVal),
                            static_cast<intptr_t>(IntVal));
  }

  PointerTy const *getAddrOfPointer() const {
    return const_cast<PointerIntPair *>(this)->getAddrOfPointer();
  }

  PointerTy *getAddrOfPointer() {
    assert(Value == reinterpret_cast<intptr_t>(getPointer()) &&
           "Can only return the address if IntBits is cleared and "
           "PtrTraits doesn't change the pointer");
    return Value.getPointerAddress();
  }

  void *getOpaqueValue() const {
    return reinterpret_cast<void *>(Value.asInt());
  }

  void setFromOpaqueValue(void *Val) & {
    Value = reinterpret_cast<intptr_t>(Val);
  }

  static PointerIntPair getFromOpaqueValue(void *V) {
    PointerIntPair P;
    P.setFromOpaqueValue(V);
    return P;
  }

  // Allow PointerIntPairs to be created from const void * if and only if the
  // pointer type could be created from a const void *.
  static PointerIntPair getFromOpaqueValue(const void *V) {
    (void)PtrTraits::getFromVoidPointer(V);
    return getFromOpaqueValue(const_cast<void *>(V));
  }

  bool operator==(const PointerIntPair &RHS) const {
    return Value == RHS.Value;
  }

  bool operator!=(const PointerIntPair &RHS) const {
    return Value != RHS.Value;
  }

  bool operator<(const PointerIntPair &RHS) const { return Value < RHS.Value; }
  bool operator>(const PointerIntPair &RHS) const { return Value > RHS.Value; }

  bool operator<=(const PointerIntPair &RHS) const {
    return Value <= RHS.Value;
  }

  bool operator>=(const PointerIntPair &RHS) const {
    return Value >= RHS.Value;
  }
};

template <typename PointerT, unsigned IntBits, typename PtrTraits>
struct PointerIntPairInfo {
  static_assert(PtrTraits::NumLowBitsAvailable <
                    std::numeric_limits<uintptr_t>::digits,
                "cannot use a pointer type that has all bits free");
  static_assert(IntBits <= PtrTraits::NumLowBitsAvailable,
                "PointerIntPair with integer size too large for pointer");
  enum MaskAndShiftConstants : uintptr_t {
    /// PointerBitMask - The bits that come from the pointer.
    PointerBitMask =
        ~(uintptr_t)(((intptr_t)1 << PtrTraits::NumLowBitsAvailable) - 1),

    /// IntShift - The number of low bits that we reserve for other uses, and
    /// keep zero.
    IntShift = (uintptr_t)PtrTraits::NumLowBitsAvailable - IntBits,

    /// IntMask - This is the unshifted mask for valid bits of the int type.
    IntMask = (uintptr_t)(((intptr_t)1 << IntBits) - 1),

    // ShiftedIntMask - This is the bits for the integer shifted in place.
    ShiftedIntMask = (uintptr_t)(IntMask << IntShift)
  };

  static PointerT getPointer(intptr_t Value) {
    return PtrTraits::getFromVoidPointer(
        reinterpret_cast<void *>(Value & PointerBitMask));
  }

  static intptr_t getInt(intptr_t Value) {
    return (Value >> IntShift) & IntMask;
  }

  static intptr_t updatePointer(intptr_t OrigValue, PointerT Ptr) {
    intptr_t PtrWord =
        reinterpret_cast<intptr_t>(PtrTraits::getAsVoidPointer(Ptr));
    assert((PtrWord & ~PointerBitMask) == 0 &&
           "Pointer is not sufficiently aligned");
    // Preserve all low bits, just update the pointer.
    return PtrWord | (OrigValue & ~PointerBitMask);
  }

  static intptr_t updateInt(intptr_t OrigValue, intptr_t Int) {
    intptr_t IntWord = static_cast<intptr_t>(Int);
    assert((IntWord & ~IntMask) == 0 && "Integer too large for field");

    // Preserve all bits other than the ones we are updating.
    return (OrigValue & ~ShiftedIntMask) | IntWord << IntShift;
  }
};

// Provide specialization of DenseMapInfo for PointerIntPair.
template <typename PointerTy, unsigned IntBits, typename IntType>
struct DenseMapInfo<PointerIntPair<PointerTy, IntBits, IntType>, void> {
  using Ty = PointerIntPair<PointerTy, IntBits, IntType>;

  static Ty getEmptyKey() {
    uintptr_t Val = static_cast<uintptr_t>(-1);
    Val <<= PointerLikeTypeTraits<Ty>::NumLowBitsAvailable;
    return Ty::getFromOpaqueValue(reinterpret_cast<void *>(Val));
  }

  static Ty getTombstoneKey() {
    uintptr_t Val = static_cast<uintptr_t>(-2);
    Val <<= PointerLikeTypeTraits<PointerTy>::NumLowBitsAvailable;
    return Ty::getFromOpaqueValue(reinterpret_cast<void *>(Val));
  }

  static unsigned getHashValue(Ty V) {
    uintptr_t IV = reinterpret_cast<uintptr_t>(V.getOpaqueValue());
    return unsigned(IV) ^ unsigned(IV >> 9);
  }

  static bool isEqual(const Ty &LHS, const Ty &RHS) { return LHS == RHS; }
};

// Teach SmallPtrSet that PointerIntPair is "basically a pointer".
template <typename PointerTy, unsigned IntBits, typename IntType,
          typename PtrTraits>
struct PointerLikeTypeTraits<
    PointerIntPair<PointerTy, IntBits, IntType, PtrTraits>> {
  static inline void *
  getAsVoidPointer(const PointerIntPair<PointerTy, IntBits, IntType> &P) {
    return P.getOpaqueValue();
  }

  static inline PointerIntPair<PointerTy, IntBits, IntType>
  getFromVoidPointer(void *P) {
    return PointerIntPair<PointerTy, IntBits, IntType>::getFromOpaqueValue(P);
  }

  static inline PointerIntPair<PointerTy, IntBits, IntType>
  getFromVoidPointer(const void *P) {
    return PointerIntPair<PointerTy, IntBits, IntType>::getFromOpaqueValue(P);
  }

  static constexpr int NumLowBitsAvailable =
      PtrTraits::NumLowBitsAvailable - IntBits;
};

// Allow structured bindings on PointerIntPair.
template <std::size_t I, typename PointerTy, unsigned IntBits, typename IntType,
          typename PtrTraits, typename Info>
decltype(auto)
get(const PointerIntPair<PointerTy, IntBits, IntType, PtrTraits, Info> &Pair) {
  static_assert(I < 2);
  if constexpr (I == 0)
    return Pair.getPointer();
  else
    return Pair.getInt();
}

} // end namespace llvm

namespace std {
template <typename PointerTy, unsigned IntBits, typename IntType,
          typename PtrTraits, typename Info>
struct tuple_size<
    llvm::PointerIntPair<PointerTy, IntBits, IntType, PtrTraits, Info>>
    : std::integral_constant<std::size_t, 2> {};

template <std::size_t I, typename PointerTy, unsigned IntBits, typename IntType,
          typename PtrTraits, typename Info>
struct tuple_element<
    I, llvm::PointerIntPair<PointerTy, IntBits, IntType, PtrTraits, Info>>
    : std::conditional<I == 0, PointerTy, IntType> {};
} // namespace std

#endif // LLVM_ADT_POINTERINTPAIR_H
