//===---- bitmask_enum.h - Enable bitmask operations on enums ---*- C++ -*-===//
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

#ifndef ORC_RT_BITMASK_ENUM_H
#define ORC_RT_BITMASK_ENUM_H

#include "stl_extras.h"

#include <cassert>
#include <type_traits>

namespace __orc_rt {

/// ORC_RT_MARK_AS_BITMASK_ENUM lets you opt in an individual enum type so you
/// can perform bitwise operations on it without putting static_cast everywhere.
///
/// \code
///   enum MyEnum {
///     E1 = 1, E2 = 2, E3 = 4, E4 = 8,
///     ORC_RT_MARK_AS_BITMASK_ENUM(/* LargestValue = */ E4)
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
/// The parameter to ORC_RT_MARK_AS_BITMASK_ENUM should be the largest
/// individual value in your enum.
///
/// All of the enum's values must be non-negative.
#define ORC_RT_MARK_AS_BITMASK_ENUM(LargestValue)                              \
  ORC_RT_BITMASK_LARGEST_ENUMERATOR = LargestValue

/// ORC_RT_DECLARE_ENUM_AS_BITMASK can be used to declare an enum type as a bit
/// set, so that bitwise operation on such enum does not require static_cast.
///
/// \code
///   enum MyEnum { E1 = 1, E2 = 2, E3 = 4, E4 = 8 };
///   ORC_RT_DECLARE_ENUM_AS_BITMASK(MyEnum, E4);
///
///   void Foo() {
///     MyEnum A = (E1 | E2) & E3 ^ ~E4; // No static_cast
///   }
/// \endcode
///
/// The second parameter to ORC_RT_DECLARE_ENUM_AS_BITMASK specifies the largest
/// bit value of the enum type.
///
/// ORC_RT_DECLARE_ENUM_AS_BITMASK should be used in __orc_rt namespace.
///
/// This a non-intrusive alternative for ORC_RT_MARK_AS_BITMASK_ENUM. It allows
/// declaring more than one non-scoped enumerations as bitmask types in the same
/// scope. Otherwise it provides the same functionality as
/// ORC_RT_MARK_AS_BITMASK_ENUM.
#define ORC_RT_DECLARE_ENUM_AS_BITMASK(Enum, LargestValue)                     \
  template <> struct is_bitmask_enum<Enum> : std::true_type {};                \
  template <> struct largest_bitmask_enum_bit<Enum> {                          \
    static constexpr std::underlying_type_t<Enum> value = LargestValue;        \
  }

/// Traits class to determine whether an enum has been declared as a bitwise
/// enum via ORC_RT_DECLARE_ENUM_AS_BITMASK.
template <typename E, typename Enable = void>
struct is_bitmask_enum : std::false_type {};

template <typename E>
struct is_bitmask_enum<
    E, std::enable_if_t<sizeof(E::ORC_RT_BITMASK_LARGEST_ENUMERATOR) >= 0>>
    : std::true_type {};

template <typename E>
inline constexpr bool is_bitmask_enum_v = is_bitmask_enum<E>::value;

/// Traits class to deermine bitmask enum largest bit.
template <typename E, typename Enable = void> struct largest_bitmask_enum_bit;

template <typename E>
struct largest_bitmask_enum_bit<
    E, std::enable_if_t<sizeof(E::ORC_RT_BITMASK_LARGEST_ENUMERATOR) >= 0>> {
  using UnderlyingTy = std::underlying_type_t<E>;
  static constexpr UnderlyingTy value =
      static_cast<UnderlyingTy>(E::ORC_RT_BITMASK_LARGEST_ENUMERATOR);
};

template <typename E> constexpr std::underlying_type_t<E> Mask() {
  return bit_ceil(largest_bitmask_enum_bit<E>::value) - 1;
}

template <typename E> constexpr std::underlying_type_t<E> Underlying(E Val) {
  auto U = static_cast<std::underlying_type_t<E>>(Val);
  assert(U >= 0 && "Negative enum values are not allowed");
  assert(U <= Mask<E>() && "Enum value too large (or langest val too small");
  return U;
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum_v<E>>>
constexpr E operator~(E Val) {
  return static_cast<E>(~Underlying(Val) & Mask<E>());
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum_v<E>>>
constexpr E operator|(E LHS, E RHS) {
  return static_cast<E>(Underlying(LHS) | Underlying(RHS));
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum_v<E>>>
constexpr E operator&(E LHS, E RHS) {
  return static_cast<E>(Underlying(LHS) & Underlying(RHS));
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum_v<E>>>
constexpr E operator^(E LHS, E RHS) {
  return static_cast<E>(Underlying(LHS) ^ Underlying(RHS));
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum_v<E>>>
E &operator|=(E &LHS, E RHS) {
  LHS = LHS | RHS;
  return LHS;
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum_v<E>>>
E &operator&=(E &LHS, E RHS) {
  LHS = LHS & RHS;
  return LHS;
}

template <typename E, typename = std::enable_if_t<is_bitmask_enum_v<E>>>
E &operator^=(E &LHS, E RHS) {
  LHS = LHS ^ RHS;
  return LHS;
}

} // end namespace __orc_rt

#endif // ORC_RT_BITMASK_ENUM_H
