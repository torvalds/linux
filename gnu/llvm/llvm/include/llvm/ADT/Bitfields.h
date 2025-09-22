//===-- llvm/ADT/Bitfield.h - Get and Set bits in an integer ---*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements methods to test, set and extract typed bits from packed
/// unsigned integers.
///
/// Why not C++ bitfields?
/// ----------------------
/// C++ bitfields do not offer control over the bit layout nor consistent
/// behavior when it comes to out of range values.
/// For instance, the layout is implementation defined and adjacent bits may be
/// packed together but are not required to. This is problematic when storage is
/// sparse and data must be stored in a particular integer type.
///
/// The methods provided in this file ensure precise control over the
/// layout/storage as well as protection against out of range values.
///
/// Usage example
/// -------------
/// \code{.cpp}
///  uint8_t Storage = 0;
///
///  // Store and retrieve a single bit as bool.
///  using Bool = Bitfield::Element<bool, 0, 1>;
///  Bitfield::set<Bool>(Storage, true);
///  EXPECT_EQ(Storage, 0b00000001);
///  //                          ^
///  EXPECT_EQ(Bitfield::get<Bool>(Storage), true);
///
///  // Store and retrieve a 2 bit typed enum.
///  // Note: enum underlying type must be unsigned.
///  enum class SuitEnum : uint8_t { CLUBS, DIAMONDS, HEARTS, SPADES };
///  // Note: enum maximum value needs to be passed in as last parameter.
///  using Suit = Bitfield::Element<SuitEnum, 1, 2, SuitEnum::SPADES>;
///  Bitfield::set<Suit>(Storage, SuitEnum::HEARTS);
///  EXPECT_EQ(Storage, 0b00000101);
///  //                        ^^
///  EXPECT_EQ(Bitfield::get<Suit>(Storage), SuitEnum::HEARTS);
///
///  // Store and retrieve a 5 bit value as unsigned.
///  using Value = Bitfield::Element<unsigned, 3, 5>;
///  Bitfield::set<Value>(Storage, 10);
///  EXPECT_EQ(Storage, 0b01010101);
///  //                   ^^^^^
///  EXPECT_EQ(Bitfield::get<Value>(Storage), 10U);
///
///  // Interpret the same 5 bit value as signed.
///  using SignedValue = Bitfield::Element<int, 3, 5>;
///  Bitfield::set<SignedValue>(Storage, -2);
///  EXPECT_EQ(Storage, 0b11110101);
///  //                   ^^^^^
///  EXPECT_EQ(Bitfield::get<SignedValue>(Storage), -2);
///
///  // Ability to efficiently test if a field is non zero.
///  EXPECT_TRUE(Bitfield::test<Value>(Storage));
///
///  // Alter Storage changes value.
///  Storage = 0;
///  EXPECT_EQ(Bitfield::get<Bool>(Storage), false);
///  EXPECT_EQ(Bitfield::get<Suit>(Storage), SuitEnum::CLUBS);
///  EXPECT_EQ(Bitfield::get<Value>(Storage), 0U);
///  EXPECT_EQ(Bitfield::get<SignedValue>(Storage), 0);
///
///  Storage = 255;
///  EXPECT_EQ(Bitfield::get<Bool>(Storage), true);
///  EXPECT_EQ(Bitfield::get<Suit>(Storage), SuitEnum::SPADES);
///  EXPECT_EQ(Bitfield::get<Value>(Storage), 31U);
///  EXPECT_EQ(Bitfield::get<SignedValue>(Storage), -1);
/// \endcode
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_BITFIELDS_H
#define LLVM_ADT_BITFIELDS_H

#include <cassert>
#include <climits> // CHAR_BIT
#include <cstddef> // size_t
#include <cstdint> // uintXX_t
#include <limits>  // numeric_limits
#include <type_traits>

namespace llvm {

namespace bitfields_details {

/// A struct defining useful bit patterns for n-bits integer types.
template <typename T, unsigned Bits> struct BitPatterns {
  /// Bit patterns are forged using the equivalent `Unsigned` type because of
  /// undefined operations over signed types (e.g. Bitwise shift operators).
  /// Moreover same size casting from unsigned to signed is well defined but not
  /// the other way around.
  using Unsigned = std::make_unsigned_t<T>;
  static_assert(sizeof(Unsigned) == sizeof(T), "Types must have same size");

  static constexpr unsigned TypeBits = sizeof(Unsigned) * CHAR_BIT;
  static_assert(TypeBits >= Bits, "n-bit must fit in T");

  /// e.g. with TypeBits == 8 and Bits == 6.
  static constexpr Unsigned AllZeros = Unsigned(0);                  // 00000000
  static constexpr Unsigned AllOnes = ~Unsigned(0);                  // 11111111
  static constexpr Unsigned Umin = AllZeros;                         // 00000000
  static constexpr Unsigned Umax = AllOnes >> (TypeBits - Bits);     // 00111111
  static constexpr Unsigned SignBitMask = Unsigned(1) << (Bits - 1); // 00100000
  static constexpr Unsigned Smax = Umax >> 1U;                       // 00011111
  static constexpr Unsigned Smin = ~Smax;                            // 11100000
  static constexpr Unsigned SignExtend = Unsigned(Smin << 1U);       // 11000000
};

/// `Compressor` is used to manipulate the bits of a (possibly signed) integer
/// type so it can be packed and unpacked into a `bits` sized integer,
/// `Compressor` is specialized on signed-ness so no runtime cost is incurred.
/// The `pack` method also checks that the passed in `UserValue` is valid.
template <typename T, unsigned Bits, bool = std::is_unsigned<T>::value>
struct Compressor {
  static_assert(std::is_unsigned<T>::value, "T must be unsigned");
  using BP = BitPatterns<T, Bits>;

  static T pack(T UserValue, T UserMaxValue) {
    assert(UserValue <= UserMaxValue && "value is too big");
    assert(UserValue <= BP::Umax && "value is too big");
    return UserValue;
  }

  static T unpack(T StorageValue) { return StorageValue; }
};

template <typename T, unsigned Bits> struct Compressor<T, Bits, false> {
  static_assert(std::is_signed<T>::value, "T must be signed");
  using BP = BitPatterns<T, Bits>;

  static T pack(T UserValue, T UserMaxValue) {
    assert(UserValue <= UserMaxValue && "value is too big");
    assert(UserValue <= T(BP::Smax) && "value is too big");
    assert(UserValue >= T(BP::Smin) && "value is too small");
    if (UserValue < 0)
      UserValue &= ~BP::SignExtend;
    return UserValue;
  }

  static T unpack(T StorageValue) {
    if (StorageValue >= T(BP::SignBitMask))
      StorageValue |= BP::SignExtend;
    return StorageValue;
  }
};

/// Impl is where Bifield description and Storage are put together to interact
/// with values.
template <typename Bitfield, typename StorageType> struct Impl {
  static_assert(std::is_unsigned<StorageType>::value,
                "Storage must be unsigned");
  using IntegerType = typename Bitfield::IntegerType;
  using C = Compressor<IntegerType, Bitfield::Bits>;
  using BP = BitPatterns<StorageType, Bitfield::Bits>;

  static constexpr size_t StorageBits = sizeof(StorageType) * CHAR_BIT;
  static_assert(Bitfield::FirstBit <= StorageBits, "Data must fit in mask");
  static_assert(Bitfield::LastBit <= StorageBits, "Data must fit in mask");
  static constexpr StorageType Mask = BP::Umax << Bitfield::Shift;

  /// Checks `UserValue` is within bounds and packs it between `FirstBit` and
  /// `LastBit` of `Packed` leaving the rest unchanged.
  static void update(StorageType &Packed, IntegerType UserValue) {
    const StorageType StorageValue = C::pack(UserValue, Bitfield::UserMaxValue);
    Packed &= ~Mask;
    Packed |= StorageValue << Bitfield::Shift;
  }

  /// Interprets bits between `FirstBit` and `LastBit` of `Packed` as
  /// an`IntegerType`.
  static IntegerType extract(StorageType Packed) {
    const StorageType StorageValue = (Packed & Mask) >> Bitfield::Shift;
    return C::unpack(StorageValue);
  }

  /// Interprets bits between `FirstBit` and `LastBit` of `Packed` as
  /// an`IntegerType`.
  static StorageType test(StorageType Packed) { return Packed & Mask; }
};

/// `Bitfield` deals with the following type:
/// - unsigned enums
/// - signed and unsigned integer
/// - `bool`
/// Internally though we only manipulate integer with well defined and
/// consistent semantics, this excludes typed enums and `bool` that are replaced
/// with their unsigned counterparts. The correct type is restored in the public
/// API.
template <typename T, bool = std::is_enum<T>::value>
struct ResolveUnderlyingType {
  using type = std::underlying_type_t<T>;
};
template <typename T> struct ResolveUnderlyingType<T, false> {
  using type = T;
};
template <> struct ResolveUnderlyingType<bool, false> {
  /// In case sizeof(bool) != 1, replace `void` by an additionnal
  /// std::conditional.
  using type = std::conditional_t<sizeof(bool) == 1, uint8_t, void>;
};

} // namespace bitfields_details

/// Holds functions to get, set or test bitfields.
struct Bitfield {
  /// Describes an element of a Bitfield. This type is then used with the
  /// Bitfield static member functions.
  /// \tparam T         The type of the field once in unpacked form.
  /// \tparam Offset    The position of the first bit.
  /// \tparam Size      The size of the field.
  /// \tparam MaxValue  For enums the maximum enum allowed.
  template <typename T, unsigned Offset, unsigned Size,
            T MaxValue = std::is_enum<T>::value
                             ? T(0) // coupled with static_assert below
                             : std::numeric_limits<T>::max()>
  struct Element {
    using Type = T;
    using IntegerType =
        typename bitfields_details::ResolveUnderlyingType<T>::type;
    static constexpr unsigned Shift = Offset;
    static constexpr unsigned Bits = Size;
    static constexpr unsigned FirstBit = Offset;
    static constexpr unsigned LastBit = Shift + Bits - 1;
    static constexpr unsigned NextBit = Shift + Bits;

  private:
    template <typename, typename> friend struct bitfields_details::Impl;

    static_assert(Bits > 0, "Bits must be non zero");
    static constexpr size_t TypeBits = sizeof(IntegerType) * CHAR_BIT;
    static_assert(Bits <= TypeBits, "Bits may not be greater than T size");
    static_assert(!std::is_enum<T>::value || MaxValue != T(0),
                  "Enum Bitfields must provide a MaxValue");
    static_assert(!std::is_enum<T>::value ||
                      std::is_unsigned<IntegerType>::value,
                  "Enum must be unsigned");
    static_assert(std::is_integral<IntegerType>::value &&
                      std::numeric_limits<IntegerType>::is_integer,
                  "IntegerType must be an integer type");

    static constexpr IntegerType UserMaxValue =
        static_cast<IntegerType>(MaxValue);
  };

  /// Unpacks the field from the `Packed` value.
  template <typename Bitfield, typename StorageType>
  static typename Bitfield::Type get(StorageType Packed) {
    using I = bitfields_details::Impl<Bitfield, StorageType>;
    return static_cast<typename Bitfield::Type>(I::extract(Packed));
  }

  /// Return a non-zero value if the field is non-zero.
  /// It is more efficient than `getField`.
  template <typename Bitfield, typename StorageType>
  static StorageType test(StorageType Packed) {
    using I = bitfields_details::Impl<Bitfield, StorageType>;
    return I::test(Packed);
  }

  /// Sets the typed value in the provided `Packed` value.
  /// The method will asserts if the provided value is too big to fit in.
  template <typename Bitfield, typename StorageType>
  static void set(StorageType &Packed, typename Bitfield::Type Value) {
    using I = bitfields_details::Impl<Bitfield, StorageType>;
    I::update(Packed, static_cast<typename Bitfield::IntegerType>(Value));
  }

  /// Returns whether the two bitfields share common bits.
  template <typename A, typename B> static constexpr bool isOverlapping() {
    return A::LastBit >= B::FirstBit && B::LastBit >= A::FirstBit;
  }

  template <typename A> static constexpr bool areContiguous() { return true; }
  template <typename A, typename B, typename... Others>
  static constexpr bool areContiguous() {
    return A::NextBit == B::FirstBit && areContiguous<B, Others...>();
  }
};

} // namespace llvm

#endif // LLVM_ADT_BITFIELDS_H
