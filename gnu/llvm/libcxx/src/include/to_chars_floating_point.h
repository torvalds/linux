//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// This implementation is dedicated to the memory of Mary and Thavatchai.

#ifndef _LIBCPP_SRC_INCLUDE_TO_CHARS_FLOATING_POINT_H
#define _LIBCPP_SRC_INCLUDE_TO_CHARS_FLOATING_POINT_H

// Avoid formatting to keep the changes with the original code minimal.
// clang-format off

#include <__algorithm/find.h>
#include <__algorithm/find_if.h>
#include <__algorithm/lower_bound.h>
#include <__algorithm/min.h>
#include <__assert>
#include <__config>
#include <__functional/operations.h>
#include <__iterator/access.h>
#include <__iterator/size.h>
#include <bit>
#include <cfloat>
#include <climits>

#include "include/ryu/ryu.h"

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __itoa {
inline constexpr char _Charconv_digits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e',
    'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z'};
static_assert(std::size(_Charconv_digits) == 36);
} // __itoa

// vvvvvvvvvv DERIVED FROM corecrt_internal_fltintrn.h vvvvvvvvvv

template <class _FloatingType>
struct _Floating_type_traits;

template <>
struct _Floating_type_traits<float> {
    static constexpr int32_t _Mantissa_bits = FLT_MANT_DIG;
    static constexpr int32_t _Exponent_bits = sizeof(float) * CHAR_BIT - FLT_MANT_DIG;

    static constexpr int32_t _Maximum_binary_exponent = FLT_MAX_EXP - 1;
    static constexpr int32_t _Minimum_binary_exponent = FLT_MIN_EXP - 1;

    static constexpr int32_t _Exponent_bias = 127;

    static constexpr int32_t _Sign_shift     = _Exponent_bits + _Mantissa_bits - 1;
    static constexpr int32_t _Exponent_shift = _Mantissa_bits - 1;

    using _Uint_type = uint32_t;

    static constexpr uint32_t _Exponent_mask             = (1u << _Exponent_bits) - 1;
    static constexpr uint32_t _Normal_mantissa_mask      = (1u << _Mantissa_bits) - 1;
    static constexpr uint32_t _Denormal_mantissa_mask    = (1u << (_Mantissa_bits - 1)) - 1;
    static constexpr uint32_t _Special_nan_mantissa_mask = 1u << (_Mantissa_bits - 2);
    static constexpr uint32_t _Shifted_sign_mask         = 1u << _Sign_shift;
    static constexpr uint32_t _Shifted_exponent_mask     = _Exponent_mask << _Exponent_shift;
};

template <>
struct _Floating_type_traits<double> {
    static constexpr int32_t _Mantissa_bits = DBL_MANT_DIG;
    static constexpr int32_t _Exponent_bits = sizeof(double) * CHAR_BIT - DBL_MANT_DIG;

    static constexpr int32_t _Maximum_binary_exponent = DBL_MAX_EXP - 1;
    static constexpr int32_t _Minimum_binary_exponent = DBL_MIN_EXP - 1;

    static constexpr int32_t _Exponent_bias = 1023;

    static constexpr int32_t _Sign_shift     = _Exponent_bits + _Mantissa_bits - 1;
    static constexpr int32_t _Exponent_shift = _Mantissa_bits - 1;

    using _Uint_type = uint64_t;

    static constexpr uint64_t _Exponent_mask             = (1ULL << _Exponent_bits) - 1;
    static constexpr uint64_t _Normal_mantissa_mask      = (1ULL << _Mantissa_bits) - 1;
    static constexpr uint64_t _Denormal_mantissa_mask    = (1ULL << (_Mantissa_bits - 1)) - 1;
    static constexpr uint64_t _Special_nan_mantissa_mask = 1ULL << (_Mantissa_bits - 2);
    static constexpr uint64_t _Shifted_sign_mask         = 1ULL << _Sign_shift;
    static constexpr uint64_t _Shifted_exponent_mask     = _Exponent_mask << _Exponent_shift;
};

// ^^^^^^^^^^ DERIVED FROM corecrt_internal_fltintrn.h ^^^^^^^^^^

// FUNCTION to_chars (FLOATING-POINT TO STRING)
template <class _Floating>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI
to_chars_result _Floating_to_chars_hex_precision(
    char* _First, char* const _Last, const _Floating _Value, int _Precision) noexcept {

    // * Determine the effective _Precision.
    // * Later, we'll decrement _Precision when printing each hexit after the decimal point.

    // The hexits after the decimal point correspond to the explicitly stored fraction bits.
    // float explicitly stores 23 fraction bits. 23 / 4 == 5.75, which is 6 hexits.
    // double explicitly stores 52 fraction bits. 52 / 4 == 13, which is 13 hexits.
    constexpr int _Full_precision         = _IsSame<_Floating, float>::value ? 6 : 13;
    constexpr int _Adjusted_explicit_bits = _Full_precision * 4;

    if (_Precision < 0) {
        // C11 7.21.6.1 "The fprintf function"/5: "A negative precision argument is taken as if the precision were
        // omitted." /8: "if the precision is missing and FLT_RADIX is a power of 2, then the precision is sufficient
        // for an exact representation of the value"
        _Precision = _Full_precision;
    }

    // * Extract the _Ieee_mantissa and _Ieee_exponent.
    using _Traits    = _Floating_type_traits<_Floating>;
    using _Uint_type = typename _Traits::_Uint_type;

    const _Uint_type _Uint_value    = std::bit_cast<_Uint_type>(_Value);
    const _Uint_type _Ieee_mantissa = _Uint_value & _Traits::_Denormal_mantissa_mask;
    const int32_t _Ieee_exponent    = static_cast<int32_t>(_Uint_value >> _Traits::_Exponent_shift);

    // * Prepare the _Adjusted_mantissa. This is aligned to hexit boundaries,
    // * with the implicit bit restored (0 for zero values and subnormal values, 1 for normal values).
    // * Also calculate the _Unbiased_exponent. This unifies the processing of zero, subnormal, and normal values.
    _Uint_type _Adjusted_mantissa;

    if constexpr (_IsSame<_Floating, float>::value) {
        _Adjusted_mantissa = _Ieee_mantissa << 1; // align to hexit boundary (23 isn't divisible by 4)
    } else {
        _Adjusted_mantissa = _Ieee_mantissa; // already aligned (52 is divisible by 4)
    }

    int32_t _Unbiased_exponent;

    if (_Ieee_exponent == 0) { // zero or subnormal
        // implicit bit is 0

        if (_Ieee_mantissa == 0) { // zero
            // C11 7.21.6.1 "The fprintf function"/8: "If the value is zero, the exponent is zero."
            _Unbiased_exponent = 0;
        } else { // subnormal
            _Unbiased_exponent = 1 - _Traits::_Exponent_bias;
        }
    } else { // normal
        _Adjusted_mantissa |= _Uint_type{1} << _Adjusted_explicit_bits; // implicit bit is 1
        _Unbiased_exponent = _Ieee_exponent - _Traits::_Exponent_bias;
    }

    // _Unbiased_exponent is within [-126, 127] for float, [-1022, 1023] for double.

    // * Decompose _Unbiased_exponent into _Sign_character and _Absolute_exponent.
    char _Sign_character;
    uint32_t _Absolute_exponent;

    if (_Unbiased_exponent < 0) {
        _Sign_character    = '-';
        _Absolute_exponent = static_cast<uint32_t>(-_Unbiased_exponent);
    } else {
        _Sign_character    = '+';
        _Absolute_exponent = static_cast<uint32_t>(_Unbiased_exponent);
    }

    // _Absolute_exponent is within [0, 127] for float, [0, 1023] for double.

    // * Perform a single bounds check.
    {
        int32_t _Exponent_length;

        if (_Absolute_exponent < 10) {
            _Exponent_length = 1;
        } else if (_Absolute_exponent < 100) {
            _Exponent_length = 2;
        } else if constexpr (_IsSame<_Floating, float>::value) {
            _Exponent_length = 3;
        } else if (_Absolute_exponent < 1000) {
            _Exponent_length = 3;
        } else {
            _Exponent_length = 4;
        }

        // _Precision might be enormous; avoid integer overflow by testing it separately.
        ptrdiff_t _Buffer_size = _Last - _First;

        if (_Buffer_size < _Precision) {
            return {_Last, errc::value_too_large};
        }

        _Buffer_size -= _Precision;

        const int32_t _Length_excluding_precision = 1 // leading hexit
                                                    + static_cast<int32_t>(_Precision > 0) // possible decimal point
                                                    // excluding `+ _Precision`, hexits after decimal point
                                                    + 2 // "p+" or "p-"
                                                    + _Exponent_length; // exponent

        if (_Buffer_size < _Length_excluding_precision) {
            return {_Last, errc::value_too_large};
        }
    }

    // * Perform rounding when we've been asked to omit hexits.
    if (_Precision < _Full_precision) {
        // _Precision is within [0, 5] for float, [0, 12] for double.

        // _Dropped_bits is within [4, 24] for float, [4, 52] for double.
        const int _Dropped_bits = (_Full_precision - _Precision) * 4;

        // Perform rounding by adding an appropriately-shifted bit.

        // This can propagate carries all the way into the leading hexit. Examples:
        // "0.ff9" rounded to a precision of 2 is "1.00".
        // "1.ff9" rounded to a precision of 2 is "2.00".

        // Note that the leading hexit participates in the rounding decision. Examples:
        // "0.8" rounded to a precision of 0 is "0".
        // "1.8" rounded to a precision of 0 is "2".

        // Reference implementation with suboptimal codegen:
        // bool _Should_round_up(bool _Lsb_bit, bool _Round_bit, bool _Has_tail_bits) {
        //    // If there are no insignificant set bits, the value is exactly-representable and should not be rounded.
        //    //
        //    // If there are insignificant set bits, we need to round according to round_to_nearest.
        //    // We need to handle two cases: we round up if either [1] the value is slightly greater
        //    // than the midpoint between two exactly-representable values or [2] the value is exactly the midpoint
        //    // between two exactly-representable values and the greater of the two is even (this is "round-to-even").
        //    return _Round_bit && (_Has_tail_bits || _Lsb_bit);
        //}
        // const bool _Lsb_bit       = (_Adjusted_mantissa & (_Uint_type{1} << _Dropped_bits)) != 0;
        // const bool _Round_bit     = (_Adjusted_mantissa & (_Uint_type{1} << (_Dropped_bits - 1))) != 0;
        // const bool _Has_tail_bits = (_Adjusted_mantissa & ((_Uint_type{1} << (_Dropped_bits - 1)) - 1)) != 0;
        // const bool _Should_round = _Should_round_up(_Lsb_bit, _Round_bit, _Has_tail_bits);
        // _Adjusted_mantissa += _Uint_type{_Should_round} << _Dropped_bits;

        // Example for optimized implementation: Let _Dropped_bits be 8.
        //          Bit index: ...[8]76543210
        // _Adjusted_mantissa: ...[L]RTTTTTTT (not depicting known details, like hexit alignment)
        // By focusing on the bit at index _Dropped_bits, we can avoid unnecessary branching and shifting.

        // Bit index: ...[8]76543210
        //  _Lsb_bit: ...[L]RTTTTTTT
        const _Uint_type _Lsb_bit = _Adjusted_mantissa;

        //  Bit index: ...9[8]76543210
        // _Round_bit: ...L[R]TTTTTTT0
        const _Uint_type _Round_bit = _Adjusted_mantissa << 1;

        // We can detect (without branching) whether any of the trailing bits are set.
        // Due to _Should_round below, this computation will be used if and only if R is 1, so we can assume that here.
        //      Bit index: ...9[8]76543210
        //     _Round_bit: ...L[1]TTTTTTT0
        // _Has_tail_bits: ....[H]........

        // If all of the trailing bits T are 0, then `_Round_bit - 1` will produce 0 for H (due to R being 1).
        // If any of the trailing bits T are 1, then `_Round_bit - 1` will produce 1 for H (due to R being 1).
        const _Uint_type _Has_tail_bits = _Round_bit - 1;

        // Finally, we can use _Should_round_up() logic with bitwise-AND and bitwise-OR,
        // selecting just the bit at index _Dropped_bits. This is the appropriately-shifted bit that we want.
        const _Uint_type _Should_round = _Round_bit & (_Has_tail_bits | _Lsb_bit) & (_Uint_type{1} << _Dropped_bits);

        // This rounding technique is dedicated to the memory of Peppermint. =^..^=
        _Adjusted_mantissa += _Should_round;
    }

    // * Print the leading hexit, then mask it away.
    {
        const uint32_t _Nibble = static_cast<uint32_t>(_Adjusted_mantissa >> _Adjusted_explicit_bits);
        _LIBCPP_ASSERT_INTERNAL(_Nibble < 3, "");
        const char _Leading_hexit = static_cast<char>('0' + _Nibble);

        *_First++ = _Leading_hexit;

        constexpr _Uint_type _Mask = (_Uint_type{1} << _Adjusted_explicit_bits) - 1;
        _Adjusted_mantissa &= _Mask;
    }

    // * Print the decimal point and trailing hexits.

    // C11 7.21.6.1 "The fprintf function"/8:
    // "if the precision is zero and the # flag is not specified, no decimal-point character appears."
    if (_Precision > 0) {
        *_First++ = '.';

        int32_t _Number_of_bits_remaining = _Adjusted_explicit_bits; // 24 for float, 52 for double

        for (;;) {
            _LIBCPP_ASSERT_INTERNAL(_Number_of_bits_remaining >= 4, "");
            _LIBCPP_ASSERT_INTERNAL(_Number_of_bits_remaining % 4 == 0, "");
            _Number_of_bits_remaining -= 4;

            const uint32_t _Nibble = static_cast<uint32_t>(_Adjusted_mantissa >> _Number_of_bits_remaining);
            _LIBCPP_ASSERT_INTERNAL(_Nibble < 16, "");
            const char _Hexit = __itoa::_Charconv_digits[_Nibble];

            *_First++ = _Hexit;

            // _Precision is the number of hexits that still need to be printed.
            --_Precision;
            if (_Precision == 0) {
                break; // We're completely done with this phase.
            }
            // Otherwise, we need to keep printing hexits.

            if (_Number_of_bits_remaining == 0) {
                // We've finished printing _Adjusted_mantissa, so all remaining hexits are '0'.
                std::memset(_First, '0', static_cast<size_t>(_Precision));
                _First += _Precision;
                break;
            }

            // Mask away the hexit that we just printed, then keep looping.
            // (We skip this when breaking out of the loop above, because _Adjusted_mantissa isn't used later.)
            const _Uint_type _Mask = (_Uint_type{1} << _Number_of_bits_remaining) - 1;
            _Adjusted_mantissa &= _Mask;
        }
    }

    // * Print the exponent.

    // C11 7.21.6.1 "The fprintf function"/8: "The exponent always contains at least one digit, and only as many more
    // digits as necessary to represent the decimal exponent of 2."

    // Performance note: We should take advantage of the known ranges of possible exponents.

    *_First++ = 'p';
    *_First++ = _Sign_character;

    // We've already printed '-' if necessary, so uint32_t _Absolute_exponent avoids testing that again.
    return std::to_chars(_First, _Last, _Absolute_exponent);
}

template <class _Floating>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI
to_chars_result _Floating_to_chars_hex_shortest(
    char* _First, char* const _Last, const _Floating _Value) noexcept {

    // This prints "1.728p+0" instead of "2.e5p-1".
    // This prints "0.000002p-126" instead of "1p-149" for float.
    // This prints "0.0000000000001p-1022" instead of "1p-1074" for double.
    // This prioritizes being consistent with printf's de facto behavior (and hex-precision's behavior)
    // over minimizing the number of characters printed.

    using _Traits    = _Floating_type_traits<_Floating>;
    using _Uint_type = typename _Traits::_Uint_type;

    const _Uint_type _Uint_value = std::bit_cast<_Uint_type>(_Value);

    if (_Uint_value == 0) { // zero detected; write "0p+0" and return
        // C11 7.21.6.1 "The fprintf function"/8: "If the value is zero, the exponent is zero."
        // Special-casing zero is necessary because of the exponent.
        const char* const _Str = "0p+0";
        const size_t _Len      = 4;

        if (_Last - _First < static_cast<ptrdiff_t>(_Len)) {
            return {_Last, errc::value_too_large};
        }

        std::memcpy(_First, _Str, _Len);

        return {_First + _Len, errc{}};
    }

    const _Uint_type _Ieee_mantissa = _Uint_value & _Traits::_Denormal_mantissa_mask;
    const int32_t _Ieee_exponent    = static_cast<int32_t>(_Uint_value >> _Traits::_Exponent_shift);

    char _Leading_hexit; // implicit bit
    int32_t _Unbiased_exponent;

    if (_Ieee_exponent == 0) { // subnormal
        _Leading_hexit     = '0';
        _Unbiased_exponent = 1 - _Traits::_Exponent_bias;
    } else { // normal
        _Leading_hexit     = '1';
        _Unbiased_exponent = _Ieee_exponent - _Traits::_Exponent_bias;
    }

    // Performance note: Consider avoiding per-character bounds checking when there's plenty of space.

    if (_First == _Last) {
        return {_Last, errc::value_too_large};
    }

    *_First++ = _Leading_hexit;

    if (_Ieee_mantissa == 0) {
        // The fraction bits are all 0. Trim them away, including the decimal point.
    } else {
        if (_First == _Last) {
            return {_Last, errc::value_too_large};
        }

        *_First++ = '.';

        // The hexits after the decimal point correspond to the explicitly stored fraction bits.
        // float explicitly stores 23 fraction bits. 23 / 4 == 5.75, so we'll print at most 6 hexits.
        // double explicitly stores 52 fraction bits. 52 / 4 == 13, so we'll print at most 13 hexits.
        _Uint_type _Adjusted_mantissa;
        int32_t _Number_of_bits_remaining;

        if constexpr (_IsSame<_Floating, float>::value) {
            _Adjusted_mantissa        = _Ieee_mantissa << 1; // align to hexit boundary (23 isn't divisible by 4)
            _Number_of_bits_remaining = 24; // 23 fraction bits + 1 alignment bit
        } else {
            _Adjusted_mantissa        = _Ieee_mantissa; // already aligned (52 is divisible by 4)
            _Number_of_bits_remaining = 52; // 52 fraction bits
        }

        // do-while: The condition _Adjusted_mantissa != 0 is initially true - we have nonzero fraction bits and we've
        // printed the decimal point. Each iteration, we print a hexit, mask it away, and keep looping if we still have
        // nonzero fraction bits. If there would be trailing '0' hexits, this trims them. If there wouldn't be trailing
        // '0' hexits, the same condition works (as we print the final hexit and mask it away); we don't need to test
        // _Number_of_bits_remaining.
        do {
            _LIBCPP_ASSERT_INTERNAL(_Number_of_bits_remaining >= 4, "");
            _LIBCPP_ASSERT_INTERNAL(_Number_of_bits_remaining % 4 == 0, "");
            _Number_of_bits_remaining -= 4;

            const uint32_t _Nibble = static_cast<uint32_t>(_Adjusted_mantissa >> _Number_of_bits_remaining);
            _LIBCPP_ASSERT_INTERNAL(_Nibble < 16, "");
            const char _Hexit = __itoa::_Charconv_digits[_Nibble];

            if (_First == _Last) {
                return {_Last, errc::value_too_large};
            }

            *_First++ = _Hexit;

            const _Uint_type _Mask = (_Uint_type{1} << _Number_of_bits_remaining) - 1;
            _Adjusted_mantissa &= _Mask;

        } while (_Adjusted_mantissa != 0);
    }

    // C11 7.21.6.1 "The fprintf function"/8: "The exponent always contains at least one digit, and only as many more
    // digits as necessary to represent the decimal exponent of 2."

    // Performance note: We should take advantage of the known ranges of possible exponents.

    // float: _Unbiased_exponent is within [-126, 127].
    // double: _Unbiased_exponent is within [-1022, 1023].

    if (_Last - _First < 2) {
        return {_Last, errc::value_too_large};
    }

    *_First++ = 'p';

    if (_Unbiased_exponent < 0) {
        *_First++          = '-';
        _Unbiased_exponent = -_Unbiased_exponent;
    } else {
        *_First++ = '+';
    }

    // We've already printed '-' if necessary, so static_cast<uint32_t> avoids testing that again.
    return std::to_chars(_First, _Last, static_cast<uint32_t>(_Unbiased_exponent));
}

// For general precision, we can use lookup tables to avoid performing trial formatting.

// For a simple example, imagine counting the number of digits D in an integer, and needing to know
// whether D is less than 3, equal to 3/4/5/6, or greater than 6. We could use a lookup table:
// D | Largest integer with D digits
// 2 |      99
// 3 |     999
// 4 |   9'999
// 5 |  99'999
// 6 | 999'999
// 7 | table end
// Looking up an integer in this table with lower_bound() will work:
// * Too-small integers, like 7, 70, and 99, will cause lower_bound() to return the D == 2 row. (If all we care
//   about is whether D is less than 3, then it's okay to smash the D == 1 and D == 2 cases together.)
// * Integers in [100, 999] will cause lower_bound() to return the D == 3 row, and so forth.
// * Too-large integers, like 1'000'000 and above, will cause lower_bound() to return the end of the table. If we
//   compute D from that index, this will be considered D == 7, which will activate any "greater than 6" logic.

// Floating-point is slightly more complicated.

// The ordinary lookup tables are for X within [-5, 38] for float, and [-5, 308] for double.
// (-5 absorbs too-negative exponents, outside the P > X >= -4 criterion. 38 and 308 are the maximum exponents.)
// Due to the P > X condition, we can use a subset of the table for X within [-5, P - 1], suitably clamped.

// When P is small, rounding can affect X. For example:
// For P == 1, the largest double with X == 0 is: 9.4999999999999982236431605997495353221893310546875
// For P == 2, the largest double with X == 0 is: 9.949999999999999289457264239899814128875732421875
// For P == 3, the largest double with X == 0 is: 9.9949999999999992184029906638897955417633056640625

// Exponent adjustment is a concern for P within [1, 7] for float, and [1, 15] for double (determined via
// brute force). While larger values of P still perform rounding, they can't trigger exponent adjustment.
// This is because only values with repeated '9' digits can undergo exponent adjustment during rounding,
// and floating-point granularity limits the number of consecutive '9' digits that can appear.

// So, we need special lookup tables for small values of P.
// These tables have varying lengths due to the P > X >= -4 criterion. For example:
// For P == 1, need table entries for X: -5, -4, -3, -2, -1, 0
// For P == 2, need table entries for X: -5, -4, -3, -2, -1, 0, 1
// For P == 3, need table entries for X: -5, -4, -3, -2, -1, 0, 1, 2
// For P == 4, need table entries for X: -5, -4, -3, -2, -1, 0, 1, 2, 3

// We can concatenate these tables for compact storage, using triangular numbers to access them.
// The table for P begins at index (P - 1) * (P + 10) / 2 with length P + 5.

// For both the ordinary and special lookup tables, after an index I is returned by lower_bound(), X is I - 5.

// We need to special-case the floating-point value 0.0, which is considered to have X == 0.
// Otherwise, the lookup tables would consider it to have a highly negative X.

// Finally, because we're working with positive floating-point values,
// representation comparisons behave identically to floating-point comparisons.

// The following code generated the lookup tables for the scientific exponent X. Don't remove this code.
#if 0
// cl /EHsc /nologo /W4 /MT /O2 /std:c++17 generate_tables.cpp && generate_tables

#include <algorithm>
#include <assert.h>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <stdint.h>
#include <stdio.h>
#include <system_error>
#include <type_traits>
#include <vector>
using namespace std;

template <typename UInt, typename Pred>
[[nodiscard]] UInt uint_partition_point(UInt first, const UInt last, Pred pred) {
    // Find the beginning of the false partition in [first, last).
    // [first, last) is partitioned when all of the true values occur before all of the false values.

    static_assert(is_unsigned_v<UInt>);
    assert(first <= last);

    for (UInt n = last - first; n > 0;) {
        const UInt n2  = n / 2;
        const UInt mid = first + n2;

        if (pred(mid)) {
            first = mid + 1;
            n     = n - n2 - 1;
        } else {
            n = n2;
        }
    }

    return first;
}

template <typename Floating>
[[nodiscard]] int scientific_exponent_X(const int P, const Floating flt) {
    char buf[400]; // more than enough

    // C11 7.21.6.1 "The fprintf function"/8 performs trial formatting with scientific precision P - 1.
    const auto to_result = to_chars(buf, end(buf), flt, chars_format::scientific, P - 1);
    assert(to_result.ec == errc{});

    const char* exp_ptr = find(buf, to_result.ptr, 'e');
    assert(exp_ptr != to_result.ptr);

    ++exp_ptr; // advance past 'e'

    if (*exp_ptr == '+') {
        ++exp_ptr; // advance past '+' which from_chars() won't parse
    }

    int X;
    const auto from_result = from_chars(exp_ptr, to_result.ptr, X);
    assert(from_result.ec == errc{});
    return X;
}

template <typename UInt>
void print_table(const vector<UInt>& v, const char* const name) {
    constexpr const char* UIntName = _IsSame<UInt, uint32_t>::value ? "uint32_t" : "uint64_t";

    printf("static constexpr %s %s[%zu] = {\n", UIntName, name, v.size());

    for (const auto& val : v) {
        if constexpr (_IsSame<UInt, uint32_t>::value) {
            printf("0x%08Xu,\n", val);
        } else {
            printf("0x%016llXu,\n", val);
        }
    }

    printf("};\n");
}

enum class Mode { Tables, Tests };

template <typename Floating>
void generate_tables(const Mode mode) {
    using Limits = numeric_limits<Floating>;
    using UInt   = conditional_t<_IsSame<Floating, float>::value, uint32_t, uint64_t>;

    map<int, map<int, UInt>> P_X_LargestValWithX;

    constexpr int MaxP = Limits::max_exponent10 + 1; // MaxP performs no rounding during trial formatting

    for (int P = 1; P <= MaxP; ++P) {
        for (int X = -5; X < P; ++X) {
            constexpr Floating first = static_cast<Floating>(9e-5); // well below 9.5e-5, otherwise arbitrary
            constexpr Floating last  = Limits::infinity(); // one bit above Limits::max()

            const UInt val_beyond_X = uint_partition_point(reinterpret_cast<const UInt&>(first),
                reinterpret_cast<const UInt&>(last),
                [P, X](const UInt u) { return scientific_exponent_X(P, reinterpret_cast<const Floating&>(u)) <= X; });

            P_X_LargestValWithX[P][X] = val_beyond_X - 1;
        }
    }

    constexpr const char* FloatingName = _IsSame<Floating, float>::value ? "float" : "double";

    constexpr int MaxSpecialP = _IsSame<Floating, float>::value ? 7 : 15; // MaxSpecialP is affected by exponent adjustment

    if (mode == Mode::Tables) {
        printf("template <>\n");
        printf("struct _General_precision_tables<%s> {\n", FloatingName);

        printf("static constexpr int _Max_special_P = %d;\n", MaxSpecialP);

        vector<UInt> special;

        for (int P = 1; P <= MaxSpecialP; ++P) {
            for (int X = -5; X < P; ++X) {
                const UInt val = P_X_LargestValWithX[P][X];
                special.push_back(val);
            }
        }

        print_table(special, "_Special_X_table");

        for (int P = MaxSpecialP + 1; P < MaxP; ++P) {
            for (int X = -5; X < P; ++X) {
                const UInt val = P_X_LargestValWithX[P][X];
                assert(val == P_X_LargestValWithX[MaxP][X]);
            }
        }

        printf("static constexpr int _Max_P = %d;\n", MaxP);

        vector<UInt> ordinary;

        for (int X = -5; X < MaxP; ++X) {
            const UInt val = P_X_LargestValWithX[MaxP][X];
            ordinary.push_back(val);
        }

        print_table(ordinary, "_Ordinary_X_table");

        printf("};\n");
    } else {
        printf("==========\n");
        printf("Test cases for %s:\n", FloatingName);

        constexpr int Hexits         = _IsSame<Floating, float>::value ? 6 : 13;
        constexpr const char* Suffix = _IsSame<Floating, float>::value ? "f" : "";

        for (int P = 1; P <= MaxP; ++P) {
            for (int X = -5; X < P; ++X) {
                if (P <= MaxSpecialP || P == 25 || P == MaxP || X == P - 1) {
                    const UInt val1   = P_X_LargestValWithX[P][X];
                    const Floating f1 = reinterpret_cast<const Floating&>(val1);
                    const UInt val2   = val1 + 1;
                    const Floating f2 = reinterpret_cast<const Floating&>(val2);

                    printf("{%.*a%s, chars_format::general, %d, \"%.*g\"},\n", Hexits, f1, Suffix, P, P, f1);
                    if (isfinite(f2)) {
                        printf("{%.*a%s, chars_format::general, %d, \"%.*g\"},\n", Hexits, f2, Suffix, P, P, f2);
                    }
                }
            }
        }
    }
}

int main() {
    printf("template <class _Floating>\n");
    printf("struct _General_precision_tables;\n");
    generate_tables<float>(Mode::Tables);
    generate_tables<double>(Mode::Tables);
    generate_tables<float>(Mode::Tests);
    generate_tables<double>(Mode::Tests);
}
#endif // 0

template <class _Floating>
struct _General_precision_tables;

template <>
struct _General_precision_tables<float> {
    static constexpr int _Max_special_P = 7;

    static constexpr uint32_t _Special_X_table[63] = {0x38C73ABCu, 0x3A79096Bu, 0x3C1BA5E3u, 0x3DC28F5Cu, 0x3F733333u,
        0x4117FFFFu, 0x38D0AAA7u, 0x3A826AA8u, 0x3C230553u, 0x3DCBC6A7u, 0x3F7EB851u, 0x411F3333u, 0x42C6FFFFu,
        0x38D19C3Fu, 0x3A8301A7u, 0x3C23C211u, 0x3DCCB295u, 0x3F7FDF3Bu, 0x411FEB85u, 0x42C7E666u, 0x4479DFFFu,
        0x38D1B468u, 0x3A8310C1u, 0x3C23D4F1u, 0x3DCCCA2Du, 0x3F7FFCB9u, 0x411FFDF3u, 0x42C7FD70u, 0x4479FCCCu,
        0x461C3DFFu, 0x38D1B6D2u, 0x3A831243u, 0x3C23D6D4u, 0x3DCCCC89u, 0x3F7FFFACu, 0x411FFFCBu, 0x42C7FFBEu,
        0x4479FFAEu, 0x461C3FCCu, 0x47C34FBFu, 0x38D1B710u, 0x3A83126Au, 0x3C23D704u, 0x3DCCCCC6u, 0x3F7FFFF7u,
        0x411FFFFAu, 0x42C7FFF9u, 0x4479FFF7u, 0x461C3FFAu, 0x47C34FF9u, 0x497423F7u, 0x38D1B716u, 0x3A83126Eu,
        0x3C23D709u, 0x3DCCCCCCu, 0x3F7FFFFFu, 0x411FFFFFu, 0x42C7FFFFu, 0x4479FFFFu, 0x461C3FFFu, 0x47C34FFFu,
        0x497423FFu, 0x4B18967Fu};

    static constexpr int _Max_P = 39;

    static constexpr uint32_t _Ordinary_X_table[44] = {0x38D1B717u, 0x3A83126Eu, 0x3C23D70Au, 0x3DCCCCCCu, 0x3F7FFFFFu,
        0x411FFFFFu, 0x42C7FFFFu, 0x4479FFFFu, 0x461C3FFFu, 0x47C34FFFu, 0x497423FFu, 0x4B18967Fu, 0x4CBEBC1Fu,
        0x4E6E6B27u, 0x501502F8u, 0x51BA43B7u, 0x5368D4A5u, 0x551184E7u, 0x56B5E620u, 0x58635FA9u, 0x5A0E1BC9u,
        0x5BB1A2BCu, 0x5D5E0B6Bu, 0x5F0AC723u, 0x60AD78EBu, 0x6258D726u, 0x64078678u, 0x65A96816u, 0x6753C21Bu,
        0x69045951u, 0x6AA56FA5u, 0x6C4ECB8Fu, 0x6E013F39u, 0x6FA18F07u, 0x7149F2C9u, 0x72FC6F7Cu, 0x749DC5ADu,
        0x76453719u, 0x77F684DFu, 0x799A130Bu, 0x7B4097CEu, 0x7CF0BDC2u, 0x7E967699u, 0x7F7FFFFFu};
};

template <>
struct _General_precision_tables<double> {
    static constexpr int _Max_special_P = 15;

    static constexpr uint64_t _Special_X_table[195] = {0x3F18E757928E0C9Du, 0x3F4F212D77318FC5u, 0x3F8374BC6A7EF9DBu,
        0x3FB851EB851EB851u, 0x3FEE666666666666u, 0x4022FFFFFFFFFFFFu, 0x3F1A1554FBDAD751u, 0x3F504D551D68C692u,
        0x3F8460AA64C2F837u, 0x3FB978D4FDF3B645u, 0x3FEFD70A3D70A3D7u, 0x4023E66666666666u, 0x4058DFFFFFFFFFFFu,
        0x3F1A3387ECC8EB96u, 0x3F506034F3FD933Eu, 0x3F84784230FCF80Du, 0x3FB99652BD3C3611u, 0x3FEFFBE76C8B4395u,
        0x4023FD70A3D70A3Du, 0x4058FCCCCCCCCCCCu, 0x408F3BFFFFFFFFFFu, 0x3F1A368D04E0BA6Au, 0x3F506218230C7482u,
        0x3F847A9E2BCF91A3u, 0x3FB99945B6C3760Bu, 0x3FEFFF972474538Eu, 0x4023FFBE76C8B439u, 0x4058FFAE147AE147u,
        0x408F3F9999999999u, 0x40C387BFFFFFFFFFu, 0x3F1A36DA54164F19u, 0x3F506248748DF16Fu, 0x3F847ADA91B16DCBu,
        0x3FB99991361DC93Eu, 0x3FEFFFF583A53B8Eu, 0x4023FFF972474538u, 0x4058FFF7CED91687u, 0x408F3FF5C28F5C28u,
        0x40C387F999999999u, 0x40F869F7FFFFFFFFu, 0x3F1A36E20F35445Du, 0x3F50624D49814ABAu, 0x3F847AE09BE19D69u,
        0x3FB99998C2DA04C3u, 0x3FEFFFFEF39085F4u, 0x4023FFFF583A53B8u, 0x4058FFFF2E48E8A7u, 0x408F3FFEF9DB22D0u,
        0x40C387FF5C28F5C2u, 0x40F869FF33333333u, 0x412E847EFFFFFFFFu, 0x3F1A36E2D51EC34Bu, 0x3F50624DC5333A0Eu,
        0x3F847AE136800892u, 0x3FB9999984200AB7u, 0x3FEFFFFFE5280D65u, 0x4023FFFFEF39085Fu, 0x4058FFFFEB074A77u,
        0x408F3FFFE5C91D14u, 0x40C387FFEF9DB22Du, 0x40F869FFEB851EB8u, 0x412E847FE6666666u, 0x416312CFEFFFFFFFu,
        0x3F1A36E2E8E94FFCu, 0x3F50624DD191D1FDu, 0x3F847AE145F6467Du, 0x3FB999999773D81Cu, 0x3FEFFFFFFD50CE23u,
        0x4023FFFFFE5280D6u, 0x4058FFFFFDE7210Bu, 0x408F3FFFFD60E94Eu, 0x40C387FFFE5C91D1u, 0x40F869FFFDF3B645u,
        0x412E847FFD70A3D7u, 0x416312CFFE666666u, 0x4197D783FDFFFFFFu, 0x3F1A36E2EAE3F7A7u, 0x3F50624DD2CE7AC8u,
        0x3F847AE14782197Bu, 0x3FB9999999629FD9u, 0x3FEFFFFFFFBB47D0u, 0x4023FFFFFFD50CE2u, 0x4058FFFFFFCA501Au,
        0x408F3FFFFFBCE421u, 0x40C387FFFFD60E94u, 0x40F869FFFFCB923Au, 0x412E847FFFBE76C8u, 0x416312CFFFD70A3Du,
        0x4197D783FFCCCCCCu, 0x41CDCD64FFBFFFFFu, 0x3F1A36E2EB16A205u, 0x3F50624DD2EE2543u, 0x3F847AE147A9AE94u,
        0x3FB9999999941A39u, 0x3FEFFFFFFFF920C8u, 0x4023FFFFFFFBB47Du, 0x4058FFFFFFFAA19Cu, 0x408F3FFFFFF94A03u,
        0x40C387FFFFFBCE42u, 0x40F869FFFFFAC1D2u, 0x412E847FFFF97247u, 0x416312CFFFFBE76Cu, 0x4197D783FFFAE147u,
        0x41CDCD64FFF99999u, 0x4202A05F1FFBFFFFu, 0x3F1A36E2EB1BB30Fu, 0x3F50624DD2F14FE9u, 0x3F847AE147ADA3E3u,
        0x3FB9999999990CDCu, 0x3FEFFFFFFFFF5014u, 0x4023FFFFFFFF920Cu, 0x4058FFFFFFFF768Fu, 0x408F3FFFFFFF5433u,
        0x40C387FFFFFF94A0u, 0x40F869FFFFFF79C8u, 0x412E847FFFFF583Au, 0x416312CFFFFF9724u, 0x4197D783FFFF7CEDu,
        0x41CDCD64FFFF5C28u, 0x4202A05F1FFF9999u, 0x42374876E7FF7FFFu, 0x3F1A36E2EB1C34C3u, 0x3F50624DD2F1A0FAu,
        0x3F847AE147AE0938u, 0x3FB9999999998B86u, 0x3FEFFFFFFFFFEE68u, 0x4023FFFFFFFFF501u, 0x4058FFFFFFFFF241u,
        0x408F3FFFFFFFEED1u, 0x40C387FFFFFFF543u, 0x40F869FFFFFFF294u, 0x412E847FFFFFEF39u, 0x416312CFFFFFF583u,
        0x4197D783FFFFF2E4u, 0x41CDCD64FFFFEF9Du, 0x4202A05F1FFFF5C2u, 0x42374876E7FFF333u, 0x426D1A94A1FFEFFFu,
        0x3F1A36E2EB1C41BBu, 0x3F50624DD2F1A915u, 0x3F847AE147AE135Au, 0x3FB9999999999831u, 0x3FEFFFFFFFFFFE3Du,
        0x4023FFFFFFFFFEE6u, 0x4058FFFFFFFFFEA0u, 0x408F3FFFFFFFFE48u, 0x40C387FFFFFFFEEDu, 0x40F869FFFFFFFEA8u,
        0x412E847FFFFFFE52u, 0x416312CFFFFFFEF3u, 0x4197D783FFFFFEB0u, 0x41CDCD64FFFFFE5Cu, 0x4202A05F1FFFFEF9u,
        0x42374876E7FFFEB8u, 0x426D1A94A1FFFE66u, 0x42A2309CE53FFEFFu, 0x3F1A36E2EB1C4307u, 0x3F50624DD2F1A9E4u,
        0x3F847AE147AE145Eu, 0x3FB9999999999975u, 0x3FEFFFFFFFFFFFD2u, 0x4023FFFFFFFFFFE3u, 0x4058FFFFFFFFFFDCu,
        0x408F3FFFFFFFFFD4u, 0x40C387FFFFFFFFE4u, 0x40F869FFFFFFFFDDu, 0x412E847FFFFFFFD5u, 0x416312CFFFFFFFE5u,
        0x4197D783FFFFFFDEu, 0x41CDCD64FFFFFFD6u, 0x4202A05F1FFFFFE5u, 0x42374876E7FFFFDFu, 0x426D1A94A1FFFFD7u,
        0x42A2309CE53FFFE6u, 0x42D6BCC41E8FFFDFu, 0x3F1A36E2EB1C4328u, 0x3F50624DD2F1A9F9u, 0x3F847AE147AE1477u,
        0x3FB9999999999995u, 0x3FEFFFFFFFFFFFFBu, 0x4023FFFFFFFFFFFDu, 0x4058FFFFFFFFFFFCu, 0x408F3FFFFFFFFFFBu,
        0x40C387FFFFFFFFFDu, 0x40F869FFFFFFFFFCu, 0x412E847FFFFFFFFBu, 0x416312CFFFFFFFFDu, 0x4197D783FFFFFFFCu,
        0x41CDCD64FFFFFFFBu, 0x4202A05F1FFFFFFDu, 0x42374876E7FFFFFCu, 0x426D1A94A1FFFFFBu, 0x42A2309CE53FFFFDu,
        0x42D6BCC41E8FFFFCu, 0x430C6BF52633FFFBu};

    static constexpr int _Max_P = 309;

    static constexpr uint64_t _Ordinary_X_table[314] = {0x3F1A36E2EB1C432Cu, 0x3F50624DD2F1A9FBu, 0x3F847AE147AE147Au,
        0x3FB9999999999999u, 0x3FEFFFFFFFFFFFFFu, 0x4023FFFFFFFFFFFFu, 0x4058FFFFFFFFFFFFu, 0x408F3FFFFFFFFFFFu,
        0x40C387FFFFFFFFFFu, 0x40F869FFFFFFFFFFu, 0x412E847FFFFFFFFFu, 0x416312CFFFFFFFFFu, 0x4197D783FFFFFFFFu,
        0x41CDCD64FFFFFFFFu, 0x4202A05F1FFFFFFFu, 0x42374876E7FFFFFFu, 0x426D1A94A1FFFFFFu, 0x42A2309CE53FFFFFu,
        0x42D6BCC41E8FFFFFu, 0x430C6BF52633FFFFu, 0x4341C37937E07FFFu, 0x4376345785D89FFFu, 0x43ABC16D674EC7FFu,
        0x43E158E460913CFFu, 0x4415AF1D78B58C3Fu, 0x444B1AE4D6E2EF4Fu, 0x4480F0CF064DD591u, 0x44B52D02C7E14AF6u,
        0x44EA784379D99DB4u, 0x45208B2A2C280290u, 0x4554ADF4B7320334u, 0x4589D971E4FE8401u, 0x45C027E72F1F1281u,
        0x45F431E0FAE6D721u, 0x46293E5939A08CE9u, 0x465F8DEF8808B024u, 0x4693B8B5B5056E16u, 0x46C8A6E32246C99Cu,
        0x46FED09BEAD87C03u, 0x4733426172C74D82u, 0x476812F9CF7920E2u, 0x479E17B84357691Bu, 0x47D2CED32A16A1B1u,
        0x48078287F49C4A1Du, 0x483D6329F1C35CA4u, 0x48725DFA371A19E6u, 0x48A6F578C4E0A060u, 0x48DCB2D6F618C878u,
        0x4911EFC659CF7D4Bu, 0x49466BB7F0435C9Eu, 0x497C06A5EC5433C6u, 0x49B18427B3B4A05Bu, 0x49E5E531A0A1C872u,
        0x4A1B5E7E08CA3A8Fu, 0x4A511B0EC57E6499u, 0x4A8561D276DDFDC0u, 0x4ABABA4714957D30u, 0x4AF0B46C6CDD6E3Eu,
        0x4B24E1878814C9CDu, 0x4B5A19E96A19FC40u, 0x4B905031E2503DA8u, 0x4BC4643E5AE44D12u, 0x4BF97D4DF19D6057u,
        0x4C2FDCA16E04B86Du, 0x4C63E9E4E4C2F344u, 0x4C98E45E1DF3B015u, 0x4CCF1D75A5709C1Au, 0x4D03726987666190u,
        0x4D384F03E93FF9F4u, 0x4D6E62C4E38FF872u, 0x4DA2FDBB0E39FB47u, 0x4DD7BD29D1C87A19u, 0x4E0DAC74463A989Fu,
        0x4E428BC8ABE49F63u, 0x4E772EBAD6DDC73Cu, 0x4EACFA698C95390Bu, 0x4EE21C81F7DD43A7u, 0x4F16A3A275D49491u,
        0x4F4C4C8B1349B9B5u, 0x4F81AFD6EC0E1411u, 0x4FB61BCCA7119915u, 0x4FEBA2BFD0D5FF5Bu, 0x502145B7E285BF98u,
        0x50559725DB272F7Fu, 0x508AFCEF51F0FB5Eu, 0x50C0DE1593369D1Bu, 0x50F5159AF8044462u, 0x512A5B01B605557Au,
        0x516078E111C3556Cu, 0x5194971956342AC7u, 0x51C9BCDFABC13579u, 0x5200160BCB58C16Cu, 0x52341B8EBE2EF1C7u,
        0x526922726DBAAE39u, 0x529F6B0F092959C7u, 0x52D3A2E965B9D81Cu, 0x53088BA3BF284E23u, 0x533EAE8CAEF261ACu,
        0x53732D17ED577D0Bu, 0x53A7F85DE8AD5C4Eu, 0x53DDF67562D8B362u, 0x5412BA095DC7701Du, 0x5447688BB5394C25u,
        0x547D42AEA2879F2Eu, 0x54B249AD2594C37Cu, 0x54E6DC186EF9F45Cu, 0x551C931E8AB87173u, 0x5551DBF316B346E7u,
        0x558652EFDC6018A1u, 0x55BBE7ABD3781ECAu, 0x55F170CB642B133Eu, 0x5625CCFE3D35D80Eu, 0x565B403DCC834E11u,
        0x569108269FD210CBu, 0x56C54A3047C694FDu, 0x56FA9CBC59B83A3Du, 0x5730A1F5B8132466u, 0x5764CA732617ED7Fu,
        0x5799FD0FEF9DE8DFu, 0x57D03E29F5C2B18Bu, 0x58044DB473335DEEu, 0x583961219000356Au, 0x586FB969F40042C5u,
        0x58A3D3E2388029BBu, 0x58D8C8DAC6A0342Au, 0x590EFB1178484134u, 0x59435CEAEB2D28C0u, 0x59783425A5F872F1u,
        0x59AE412F0F768FADu, 0x59E2E8BD69AA19CCu, 0x5A17A2ECC414A03Fu, 0x5A4D8BA7F519C84Fu, 0x5A827748F9301D31u,
        0x5AB7151B377C247Eu, 0x5AECDA62055B2D9Du, 0x5B22087D4358FC82u, 0x5B568A9C942F3BA3u, 0x5B8C2D43B93B0A8Bu,
        0x5BC19C4A53C4E697u, 0x5BF6035CE8B6203Du, 0x5C2B843422E3A84Cu, 0x5C6132A095CE492Fu, 0x5C957F48BB41DB7Bu,
        0x5CCADF1AEA12525Au, 0x5D00CB70D24B7378u, 0x5D34FE4D06DE5056u, 0x5D6A3DE04895E46Cu, 0x5DA066AC2D5DAEC3u,
        0x5DD4805738B51A74u, 0x5E09A06D06E26112u, 0x5E400444244D7CABu, 0x5E7405552D60DBD6u, 0x5EA906AA78B912CBu,
        0x5EDF485516E7577Eu, 0x5F138D352E5096AFu, 0x5F48708279E4BC5Au, 0x5F7E8CA3185DEB71u, 0x5FB317E5EF3AB327u,
        0x5FE7DDDF6B095FF0u, 0x601DD55745CBB7ECu, 0x6052A5568B9F52F4u, 0x60874EAC2E8727B1u, 0x60BD22573A28F19Du,
        0x60F2357684599702u, 0x6126C2D4256FFCC2u, 0x615C73892ECBFBF3u, 0x6191C835BD3F7D78u, 0x61C63A432C8F5CD6u,
        0x61FBC8D3F7B3340Bu, 0x62315D847AD00087u, 0x6265B4E5998400A9u, 0x629B221EFFE500D3u, 0x62D0F5535FEF2084u,
        0x630532A837EAE8A5u, 0x633A7F5245E5A2CEu, 0x63708F936BAF85C1u, 0x63A4B378469B6731u, 0x63D9E056584240FDu,
        0x64102C35F729689Eu, 0x6444374374F3C2C6u, 0x647945145230B377u, 0x64AF965966BCE055u, 0x64E3BDF7E0360C35u,
        0x6518AD75D8438F43u, 0x654ED8D34E547313u, 0x6583478410F4C7ECu, 0x65B819651531F9E7u, 0x65EE1FBE5A7E7861u,
        0x6622D3D6F88F0B3Cu, 0x665788CCB6B2CE0Cu, 0x668D6AFFE45F818Fu, 0x66C262DFEEBBB0F9u, 0x66F6FB97EA6A9D37u,
        0x672CBA7DE5054485u, 0x6761F48EAF234AD3u, 0x679671B25AEC1D88u, 0x67CC0E1EF1A724EAu, 0x680188D357087712u,
        0x6835EB082CCA94D7u, 0x686B65CA37FD3A0Du, 0x68A11F9E62FE4448u, 0x68D56785FBBDD55Au, 0x690AC1677AAD4AB0u,
        0x6940B8E0ACAC4EAEu, 0x6974E718D7D7625Au, 0x69AA20DF0DCD3AF0u, 0x69E0548B68A044D6u, 0x6A1469AE42C8560Cu,
        0x6A498419D37A6B8Fu, 0x6A7FE52048590672u, 0x6AB3EF342D37A407u, 0x6AE8EB0138858D09u, 0x6B1F25C186A6F04Cu,
        0x6B537798F428562Fu, 0x6B88557F31326BBBu, 0x6BBE6ADEFD7F06AAu, 0x6BF302CB5E6F642Au, 0x6C27C37E360B3D35u,
        0x6C5DB45DC38E0C82u, 0x6C9290BA9A38C7D1u, 0x6CC734E940C6F9C5u, 0x6CFD022390F8B837u, 0x6D3221563A9B7322u,
        0x6D66A9ABC9424FEBu, 0x6D9C5416BB92E3E6u, 0x6DD1B48E353BCE6Fu, 0x6E0621B1C28AC20Bu, 0x6E3BAA1E332D728Eu,
        0x6E714A52DFFC6799u, 0x6EA59CE797FB817Fu, 0x6EDB04217DFA61DFu, 0x6F10E294EEBC7D2Bu, 0x6F451B3A2A6B9C76u,
        0x6F7A6208B5068394u, 0x6FB07D457124123Cu, 0x6FE49C96CD6D16CBu, 0x7019C3BC80C85C7Eu, 0x70501A55D07D39CFu,
        0x708420EB449C8842u, 0x70B9292615C3AA53u, 0x70EF736F9B3494E8u, 0x7123A825C100DD11u, 0x7158922F31411455u,
        0x718EB6BAFD91596Bu, 0x71C33234DE7AD7E2u, 0x71F7FEC216198DDBu, 0x722DFE729B9FF152u, 0x7262BF07A143F6D3u,
        0x72976EC98994F488u, 0x72CD4A7BEBFA31AAu, 0x73024E8D737C5F0Au, 0x7336E230D05B76CDu, 0x736C9ABD04725480u,
        0x73A1E0B622C774D0u, 0x73D658E3AB795204u, 0x740BEF1C9657A685u, 0x74417571DDF6C813u, 0x7475D2CE55747A18u,
        0x74AB4781EAD1989Eu, 0x74E10CB132C2FF63u, 0x75154FDD7F73BF3Bu, 0x754AA3D4DF50AF0Au, 0x7580A6650B926D66u,
        0x75B4CFFE4E7708C0u, 0x75EA03FDE214CAF0u, 0x7620427EAD4CFED6u, 0x7654531E58A03E8Bu, 0x768967E5EEC84E2Eu,
        0x76BFC1DF6A7A61BAu, 0x76F3D92BA28C7D14u, 0x7728CF768B2F9C59u, 0x775F03542DFB8370u, 0x779362149CBD3226u,
        0x77C83A99C3EC7EAFu, 0x77FE494034E79E5Bu, 0x7832EDC82110C2F9u, 0x7867A93A2954F3B7u, 0x789D9388B3AA30A5u,
        0x78D27C35704A5E67u, 0x79071B42CC5CF601u, 0x793CE2137F743381u, 0x79720D4C2FA8A030u, 0x79A6909F3B92C83Du,
        0x79DC34C70A777A4Cu, 0x7A11A0FC668AAC6Fu, 0x7A46093B802D578Bu, 0x7A7B8B8A6038AD6Eu, 0x7AB137367C236C65u,
        0x7AE585041B2C477Eu, 0x7B1AE64521F7595Eu, 0x7B50CFEB353A97DAu, 0x7B8503E602893DD1u, 0x7BBA44DF832B8D45u,
        0x7BF06B0BB1FB384Bu, 0x7C2485CE9E7A065Eu, 0x7C59A742461887F6u, 0x7C9008896BCF54F9u, 0x7CC40AABC6C32A38u,
        0x7CF90D56B873F4C6u, 0x7D2F50AC6690F1F8u, 0x7D63926BC01A973Bu, 0x7D987706B0213D09u, 0x7DCE94C85C298C4Cu,
        0x7E031CFD3999F7AFu, 0x7E37E43C8800759Bu, 0x7E6DDD4BAA009302u, 0x7EA2AA4F4A405BE1u, 0x7ED754E31CD072D9u,
        0x7F0D2A1BE4048F90u, 0x7F423A516E82D9BAu, 0x7F76C8E5CA239028u, 0x7FAC7B1F3CAC7433u, 0x7FE1CCF385EBC89Fu,
        0x7FEFFFFFFFFFFFFFu};
};

template <class _Floating>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI
to_chars_result _Floating_to_chars_general_precision(
    char* _First, char* const _Last, const _Floating _Value, int _Precision) noexcept {

    using _Traits    = _Floating_type_traits<_Floating>;
    using _Uint_type = typename _Traits::_Uint_type;

    const _Uint_type _Uint_value = std::bit_cast<_Uint_type>(_Value);

    if (_Uint_value == 0) { // zero detected; write "0" and return; _Precision is irrelevant due to zero-trimming
        if (_First == _Last) {
            return {_Last, errc::value_too_large};
        }

        *_First++ = '0';

        return {_First, errc{}};
    }

    // C11 7.21.6.1 "The fprintf function"/5:
    // "A negative precision argument is taken as if the precision were omitted."
    // /8: "g,G [...] Let P equal the precision if nonzero, 6 if the precision is omitted,
    // or 1 if the precision is zero."

    // Performance note: It's possible to rewrite this for branchless codegen,
    // but profiling will be necessary to determine whether that's faster.
    if (_Precision < 0) {
        _Precision = 6;
    } else if (_Precision == 0) {
        _Precision = 1;
    } else if (_Precision < 1'000'000) {
        // _Precision is ok.
    } else {
        // Avoid integer overflow.
        // Due to general notation's zero-trimming behavior, we can simply clamp _Precision.
        // This is further clamped below.
        _Precision = 1'000'000;
    }

    // _Precision is now the Standard's P.

    // /8: "Then, if a conversion with style E would have an exponent of X:
    // - if P > X >= -4, the conversion is with style f (or F) and precision P - (X + 1).
    // - otherwise, the conversion is with style e (or E) and precision P - 1."

    // /8: "Finally, [...] any trailing zeros are removed from the fractional portion of the result
    // and the decimal-point character is removed if there is no fractional portion remaining."

    using _Tables = _General_precision_tables<_Floating>;

    const _Uint_type* _Table_begin;
    const _Uint_type* _Table_end;

    if (_Precision <= _Tables::_Max_special_P) {
        _Table_begin = _Tables::_Special_X_table + (_Precision - 1) * (_Precision + 10) / 2;
        _Table_end   = _Table_begin + _Precision + 5;
    } else {
        _Table_begin = _Tables::_Ordinary_X_table;
        _Table_end   = _Table_begin + std::min(_Precision, _Tables::_Max_P) + 5;
    }

    // Profiling indicates that linear search is faster than binary search for small tables.
    // Performance note: lambda captures may have a small performance cost.
    const _Uint_type* const _Table_lower_bound = [=] {
        if constexpr (!_IsSame<_Floating, float>::value) {
            if (_Precision > 155) { // threshold determined via profiling
                return std::lower_bound(_Table_begin, _Table_end, _Uint_value, less{});
            }
        }

        return std::find_if(_Table_begin, _Table_end, [=](const _Uint_type _Elem) { return _Uint_value <= _Elem; });
    }();

    const ptrdiff_t _Table_index     = _Table_lower_bound - _Table_begin;
    const int _Scientific_exponent_X = static_cast<int>(_Table_index - 5);
    const bool _Use_fixed_notation   = _Precision > _Scientific_exponent_X && _Scientific_exponent_X >= -4;

    // Performance note: it might (or might not) be faster to modify Ryu Printf to perform zero-trimming.
    // Such modifications would involve a fairly complicated state machine (notably, both '0' and '9' digits would
    // need to be buffered, due to rounding), and that would have performance costs due to increased branching.
    // Here, we're using a simpler approach: writing into a local buffer, manually zero-trimming, and then copying into
    // the output range. The necessary buffer size is reasonably small, the zero-trimming logic is simple and fast,
    // and the final copying is also fast.

    constexpr int _Max_output_length =
        _IsSame<_Floating, float>::value ? 117 : 773; // cases: 0x1.fffffep-126f and 0x1.fffffffffffffp-1022
    constexpr int _Max_fixed_precision =
        _IsSame<_Floating, float>::value ? 37 : 66; // cases: 0x1.fffffep-14f and 0x1.fffffffffffffp-14
    constexpr int _Max_scientific_precision =
        _IsSame<_Floating, float>::value ? 111 : 766; // cases: 0x1.fffffep-126f and 0x1.fffffffffffffp-1022

    // Note that _Max_output_length is determined by scientific notation and is more than enough for fixed notation.
    // 0x1.fffffep+127f is 39 digits, plus 1 for '.', plus _Max_fixed_precision for '0' digits, equals 77.
    // 0x1.fffffffffffffp+1023 is 309 digits, plus 1 for '.', plus _Max_fixed_precision for '0' digits, equals 376.

    char _Buffer[_Max_output_length];
    const char* const _Significand_first = _Buffer; // e.g. "1.234"
    const char* _Significand_last        = nullptr;
    const char* _Exponent_first          = nullptr; // e.g. "e-05"
    const char* _Exponent_last           = nullptr;
    int _Effective_precision; // number of digits printed after the decimal point, before trimming

    // Write into the local buffer.
    // Clamping _Effective_precision allows _Buffer to be as small as possible, and increases efficiency.
    if (_Use_fixed_notation) {
        _Effective_precision = std::min(_Precision - (_Scientific_exponent_X + 1), _Max_fixed_precision);
        const to_chars_result _Buf_result =
            _Floating_to_chars_fixed_precision(_Buffer, std::end(_Buffer), _Value, _Effective_precision);
        _LIBCPP_ASSERT_INTERNAL(_Buf_result.ec == errc{}, "");
        _Significand_last = _Buf_result.ptr;
    } else {
        _Effective_precision = std::min(_Precision - 1, _Max_scientific_precision);
        const to_chars_result _Buf_result =
            _Floating_to_chars_scientific_precision(_Buffer, std::end(_Buffer), _Value, _Effective_precision);
        _LIBCPP_ASSERT_INTERNAL(_Buf_result.ec == errc{}, "");
        _Significand_last = std::find(_Buffer, _Buf_result.ptr, 'e');
        _Exponent_first   = _Significand_last;
        _Exponent_last    = _Buf_result.ptr;
    }

    // If we printed a decimal point followed by digits, perform zero-trimming.
    if (_Effective_precision > 0) {
        while (_Significand_last[-1] == '0') { // will stop at '.' or a nonzero digit
            --_Significand_last;
        }

        if (_Significand_last[-1] == '.') {
            --_Significand_last;
        }
    }

    // Copy the significand to the output range.
    const ptrdiff_t _Significand_distance = _Significand_last - _Significand_first;
    if (_Last - _First < _Significand_distance) {
        return {_Last, errc::value_too_large};
    }
    std::memcpy(_First, _Significand_first, static_cast<size_t>(_Significand_distance));
    _First += _Significand_distance;

    // Copy the exponent to the output range.
    if (!_Use_fixed_notation) {
        const ptrdiff_t _Exponent_distance = _Exponent_last - _Exponent_first;
        if (_Last - _First < _Exponent_distance) {
            return {_Last, errc::value_too_large};
        }
        std::memcpy(_First, _Exponent_first, static_cast<size_t>(_Exponent_distance));
        _First += _Exponent_distance;
    }

    return {_First, errc{}};
}

enum class _Floating_to_chars_overload { _Plain, _Format_only, _Format_precision };

template <_Floating_to_chars_overload _Overload, class _Floating>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI
to_chars_result _Floating_to_chars(
    char* _First, char* const _Last, _Floating _Value, const chars_format _Fmt, const int _Precision) noexcept {

    if constexpr (_Overload == _Floating_to_chars_overload::_Plain) {
        _LIBCPP_ASSERT_INTERNAL(_Fmt == chars_format{}, ""); // plain overload must pass chars_format{} internally
    } else {
        _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(_Fmt == chars_format::general || _Fmt == chars_format::scientific
                         || _Fmt == chars_format::fixed || _Fmt == chars_format::hex,
            "invalid format in to_chars()");
    }

    using _Traits    = _Floating_type_traits<_Floating>;
    using _Uint_type = typename _Traits::_Uint_type;

    _Uint_type _Uint_value = std::bit_cast<_Uint_type>(_Value);

    const bool _Was_negative = (_Uint_value & _Traits::_Shifted_sign_mask) != 0;

    if (_Was_negative) { // sign bit detected; write minus sign and clear sign bit
        if (_First == _Last) {
            return {_Last, errc::value_too_large};
        }

        *_First++ = '-';

        _Uint_value &= ~_Traits::_Shifted_sign_mask;
        _Value = std::bit_cast<_Floating>(_Uint_value);
    }

    if ((_Uint_value & _Traits::_Shifted_exponent_mask) == _Traits::_Shifted_exponent_mask) {
        // inf/nan detected; write appropriate string and return
        const char* _Str;
        size_t _Len;

        const _Uint_type _Mantissa = _Uint_value & _Traits::_Denormal_mantissa_mask;

        if (_Mantissa == 0) {
            _Str = "inf";
            _Len = 3;
        } else if (_Was_negative && _Mantissa == _Traits::_Special_nan_mantissa_mask) {
            // When a NaN value has the sign bit set, the quiet bit set, and all other mantissa bits cleared,
            // the UCRT interprets it to mean "indeterminate", and indicates this by printing "-nan(ind)".
            _Str = "nan(ind)";
            _Len = 8;
        } else if ((_Mantissa & _Traits::_Special_nan_mantissa_mask) != 0) {
            _Str = "nan";
            _Len = 3;
        } else {
            _Str = "nan(snan)";
            _Len = 9;
        }

        if (_Last - _First < static_cast<ptrdiff_t>(_Len)) {
            return {_Last, errc::value_too_large};
        }

        std::memcpy(_First, _Str, _Len);

        return {_First + _Len, errc{}};
    }

    if constexpr (_Overload == _Floating_to_chars_overload::_Plain) {
        return _Floating_to_chars_ryu(_First, _Last, _Value, chars_format{});
    } else if constexpr (_Overload == _Floating_to_chars_overload::_Format_only) {
        if (_Fmt == chars_format::hex) {
            return _Floating_to_chars_hex_shortest(_First, _Last, _Value);
        }

        return _Floating_to_chars_ryu(_First, _Last, _Value, _Fmt);
    } else if constexpr (_Overload == _Floating_to_chars_overload::_Format_precision) {
        switch (_Fmt) {
        case chars_format::scientific:
            return _Floating_to_chars_scientific_precision(_First, _Last, _Value, _Precision);
        case chars_format::fixed:
            return _Floating_to_chars_fixed_precision(_First, _Last, _Value, _Precision);
        case chars_format::general:
            return _Floating_to_chars_general_precision(_First, _Last, _Value, _Precision);
        case chars_format::hex:
        default: // avoid MSVC warning C4715: not all control paths return a value
            return _Floating_to_chars_hex_precision(_First, _Last, _Value, _Precision);
        }
    }
}

// clang-format on

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_SRC_INCLUDE_TO_CHARS_FLOATING_POINT_H
