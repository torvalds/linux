//===-- llvm/ADT/BitmaskEnum.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_BITMASKENUM_H
#define LLVM_ADT_BITMASKENUM_H

#include <cassert>
#include <type_traits>
#include <utility>

#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/Support/MathExtras.h"

/// LLVM_MARK_AS_BITMASK_ENUM lets you opt in an individual enum type so you can
/// perform bitwise operations on it without putting static_cast everywhere.
///
/// \code
///   enum MyEnum {
///     E1 = 1, E2 = 2, E3 = 4, E4 = 8,
///     LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ E4)
///   };
///
///   void Foo() {
///     MyEnum A = (E1 | E2) & E3 ^ ~E4; // Look, ma: No static_cast!
///   }
/// \endcode
///
/// Normally when you do a bitwise operation on an enum value, you get back an
/// instance of the underlying type (e.g. int).  But using this macro, bitwise
/// ops on your enum will return you back instances of the enum.  This is
/// particularly useful for enums which represent a combination of flags.
///
/// The parameter to LLVM_MARK_AS_BITMASK_ENUM should be the largest individual
/// value in your enum.
///
/// All of the enum's values must be non-negative.
#define LLVM_MARK_AS_BITMASK_ENUM(LargestValue)                                \
  LLVM_BITMASK_LARGEST_ENUMERATOR = LargestValue

/// LLVM_DECLARE_ENUM_AS_BITMASK can be used to declare an enum type as a bit
/// set, so that bitwise operation on such enum does not require static_cast.
///
/// \code
///   enum MyEnum { E1 = 1, E2 = 2, E3 = 4, E4 = 8 };
///   LLVM_DECLARE_ENUM_AS_BITMASK(MyEnum, E4);
///
///   void Foo() {
///     MyEnum A = (E1 | E2) & E3 ^ ~E4; // No static_cast
///   }
/// \endcode
///
/// The second parameter to LLVM_DECLARE_ENUM_AS_BITMASK specifies the largest
/// bit value of the enum type.
///
/// LLVM_DECLARE_ENUM_AS_BITMASK should be used in llvm namespace.
///
/// This a non-intrusive alternative for LLVM_MARK_AS_BITMASK_ENUM. It allows
/// declaring more than one non-scoped enumerations as bitmask types in the same
/// scope. Otherwise it provides the same functionality as
/// LLVM_MARK_AS_BITMASK_ENUM.
#define LLVM_DECLARE_ENUM_AS_BITMASK(Enum, LargestValue)                       \
  template <> struct is_bitmask_enum<Enum> : std::true_type {};                \
  template <> struct largest_bitmask_enum_bit<Enum> {                          \
    static constexpr std::underlying_type_t<Enum> value = LargestValue;        \
  }

/// LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE() pulls the operator overloads used
/// by LLVM_MARK_AS_BITMASK_ENUM into the current namespace.
///
/// Suppose you have an enum foo::bar::MyEnum.  Before using
/// LLVM_MARK_AS_BITMASK_ENUM on MyEnum, you must put
/// LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE() somewhere inside namespace foo or
/// namespace foo::bar.  This allows the relevant operator overloads to be found
/// by ADL.
///
/// You don't need to use this macro in namespace llvm; it's done at the bottom
/// of this file.
#define LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE()                               \
  using ::llvm::BitmaskEnumDetail::operator~;                                  \
  using ::llvm::BitmaskEnumDetail::operator|;                                  \
  using ::llvm::BitmaskEnumDetail::operator&;                                  \
  using ::llvm::BitmaskEnumDetail::operator^;                                  \
  using ::llvm::BitmaskEnumDetail::operator|=;                                 \
  using ::llvm::BitmaskEnumDetail::operator&=;                                 \
  using ::llvm::BitmaskEnumDetail::operator^=;                                 \
  /* Force a semicolon at the end of this macro. */                            \
  using ::llvm::BitmaskEnumDetail::any

namespace llvm {

/// Traits class to determine whether an enum has a
/// LLVM_BITMASK_LARGEST_ENUMERATOR enumerator.
template <typename E, typename Enable = void>
struct is_bitmask_enum : std::false_type {};

template <typename E>
struct is_bitmask_enum<
    E, std::enable_if_t<sizeof(E::LLVM_BITMASK_LARGEST_ENUMERATOR) >= 0>>
    : std::true_type {};

/// Trait class to determine bitmask enumeration largest bit.
template <typename E, typename Enable = void> struct largest_bitmask_enum_bit;

template <typename E>
struct largest_bitmask_enum_bit<
    E, std::enable_if_t<sizeof(E::LLVM_BITMASK_LARGEST_ENUMERATOR) >= 0>> {
  using UnderlyingTy = std::underlying_type_t<E>;
  static constexpr UnderlyingTy value =
      static_cast<UnderlyingTy>(E::LLVM_BITMASK_LARGEST_ENUMERATOR);
};

namespace BitmaskEnumDetail {

/// Get a bitmask with 1s in all places up to the high-order bit of E's largest
/// value.
template <typename E> constexpr std::underlying_type_t<E> Mask() {
  // On overflow, NextPowerOf2 returns zero with the type uint64_t, so
  // subtracting 1 gives us the mask with all bits set, like we want.
  return NextPowerOf2(largest_bitmask_enum_bit<E>::value) - 1;
}

/// Check that Val is in range for E, and return Val cast to E's underlying
/// type.
template <typename E> constexpr std::underlying_type_t<E> Underlying(E Val) {
  auto U = llvm::to_underlying(Val);
  assert(U >= 0 && "Negative enum values are not allowed.");
  assert(U <= Mask<E>() && "Enum value too large (or largest val too small?)");
  return U;
}

constexpr unsigned bitWidth(uint64_t Value) {
  return Value ? 1 + bitWidth(Value >> 1) : 0;
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum<E>::value>>
constexpr bool any(E Val) {
  return Val != static_cast<E>(0);
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum<E>::value>>
constexpr E operator~(E Val) {
  return static_cast<E>(~Underlying(Val) & Mask<E>());
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum<E>::value>>
constexpr E operator|(E LHS, E RHS) {
  return static_cast<E>(Underlying(LHS) | Underlying(RHS));
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum<E>::value>>
constexpr E operator&(E LHS, E RHS) {
  return static_cast<E>(Underlying(LHS) & Underlying(RHS));
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum<E>::value>>
constexpr E operator^(E LHS, E RHS) {
  return static_cast<E>(Underlying(LHS) ^ Underlying(RHS));
}

// |=, &=, and ^= return a reference to LHS, to match the behavior of the
// operators on builtin types.

template <typename E, typename = std::enable_if_t<is_bitmask_enum<E>::value>>
E &operator|=(E &LHS, E RHS) {
  LHS = LHS | RHS;
  return LHS;
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum<E>::value>>
E &operator&=(E &LHS, E RHS) {
  LHS = LHS & RHS;
  return LHS;
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum<E>::value>>
E &operator^=(E &LHS, E RHS) {
  LHS = LHS ^ RHS;
  return LHS;
}

} // namespace BitmaskEnumDetail

// Enable bitmask enums in namespace ::llvm and all nested namespaces.
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();
template <typename E, typename = std::enable_if_t<is_bitmask_enum<E>::value>>
constexpr unsigned BitWidth = BitmaskEnumDetail::bitWidth(
    uint64_t{llvm::to_underlying(E::LLVM_BITMASK_LARGEST_ENUMERATOR)});

} // namespace llvm

#endif
