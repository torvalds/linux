// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMATTER_FLOATING_POINT_H
#define _LIBCPP___FORMAT_FORMATTER_FLOATING_POINT_H

#include <__algorithm/copy_n.h>
#include <__algorithm/find.h>
#include <__algorithm/max.h>
#include <__algorithm/min.h>
#include <__algorithm/rotate.h>
#include <__algorithm/transform.h>
#include <__assert>
#include <__charconv/chars_format.h>
#include <__charconv/to_chars_floating_point.h>
#include <__charconv/to_chars_result.h>
#include <__concepts/arithmetic.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__format/concepts.h>
#include <__format/format_parse_context.h>
#include <__format/formatter.h>
#include <__format/formatter_integral.h>
#include <__format/formatter_output.h>
#include <__format/parser_std_format_spec.h>
#include <__iterator/concepts.h>
#include <__memory/allocator.h>
#include <__system_error/errc.h>
#include <__type_traits/conditional.h>
#include <__utility/move.h>
#include <__utility/unreachable.h>
#include <cmath>
#include <cstddef>

#ifndef _LIBCPP_HAS_NO_LOCALIZATION
#  include <__locale>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace __formatter {

template <floating_point _Tp>
_LIBCPP_HIDE_FROM_ABI char* __to_buffer(char* __first, char* __last, _Tp __value) {
  to_chars_result __r = std::to_chars(__first, __last, __value);
  _LIBCPP_ASSERT_INTERNAL(__r.ec == errc(0), "Internal buffer too small");
  return __r.ptr;
}

template <floating_point _Tp>
_LIBCPP_HIDE_FROM_ABI char* __to_buffer(char* __first, char* __last, _Tp __value, chars_format __fmt) {
  to_chars_result __r = std::to_chars(__first, __last, __value, __fmt);
  _LIBCPP_ASSERT_INTERNAL(__r.ec == errc(0), "Internal buffer too small");
  return __r.ptr;
}

template <floating_point _Tp>
_LIBCPP_HIDE_FROM_ABI char* __to_buffer(char* __first, char* __last, _Tp __value, chars_format __fmt, int __precision) {
  to_chars_result __r = std::to_chars(__first, __last, __value, __fmt, __precision);
  _LIBCPP_ASSERT_INTERNAL(__r.ec == errc(0), "Internal buffer too small");
  return __r.ptr;
}

// https://en.cppreference.com/w/cpp/language/types#cite_note-1
// float             min subnormal: +/-0x1p-149   max: +/- 3.402,823,4 10^38
// double            min subnormal: +/-0x1p-1074  max  +/- 1.797,693,134,862,315,7 10^308
// long double (x86) min subnormal: +/-0x1p-16446 max: +/- 1.189,731,495,357,231,765,021 10^4932
//
// The maximum number of digits required for the integral part is based on the
// maximum's value power of 10. Every power of 10 requires one additional
// decimal digit.
// The maximum number of digits required for the fractional part is based on
// the minimal subnormal hexadecimal output's power of 10. Every division of a
// fraction's binary 1 by 2, requires one additional decimal digit.
//
// The maximum size of a formatted value depends on the selected output format.
// Ignoring the fact the format string can request a precision larger than the
// values maximum required, these values are:
//
// sign                    1 code unit
// __max_integral
// radix point             1 code unit
// __max_fractional
// exponent character      1 code unit
// sign                    1 code unit
// __max_fractional_value
// -----------------------------------
// total                   4 code units extra required.
//
// TODO FMT Optimize the storage to avoid storing digits that are known to be zero.
// https://www.exploringbinary.com/maximum-number-of-decimal-digits-in-binary-floating-point-numbers/

// TODO FMT Add long double specialization when to_chars has proper long double support.
template <class _Tp>
struct __traits;

template <floating_point _Fp>
_LIBCPP_HIDE_FROM_ABI constexpr size_t __float_buffer_size(int __precision) {
  using _Traits = __traits<_Fp>;
  return 4 + _Traits::__max_integral + __precision + _Traits::__max_fractional_value;
}

template <>
struct __traits<float> {
  static constexpr int __max_integral         = 38;
  static constexpr int __max_fractional       = 149;
  static constexpr int __max_fractional_value = 3;
  static constexpr size_t __stack_buffer_size = 256;

  static constexpr int __hex_precision_digits = 3;
};

template <>
struct __traits<double> {
  static constexpr int __max_integral         = 308;
  static constexpr int __max_fractional       = 1074;
  static constexpr int __max_fractional_value = 4;
  static constexpr size_t __stack_buffer_size = 1024;

  static constexpr int __hex_precision_digits = 4;
};

/// Helper class to store the conversion buffer.
///
/// Depending on the maximum size required for a value, the buffer is allocated
/// on the stack or the heap.
template <floating_point _Fp>
class _LIBCPP_TEMPLATE_VIS __float_buffer {
  using _Traits = __traits<_Fp>;

public:
  // TODO FMT Improve this constructor to do a better estimate.
  // When using a scientific formatting with a precision of 6 a stack buffer
  // will always suffice. At the moment that isn't important since floats and
  // doubles use a stack buffer, unless the precision used in the format string
  // is large.
  // When supporting long doubles the __max_integral part becomes 4932 which
  // may be too much for some platforms. For these cases a better estimate is
  // required.
  explicit _LIBCPP_HIDE_FROM_ABI __float_buffer(int __precision)
      : __precision_(__precision != -1 ? __precision : _Traits::__max_fractional) {
    // When the precision is larger than _Traits::__max_fractional the digits in
    // the range (_Traits::__max_fractional, precision] will contain the value
    // zero. There's no need to request to_chars to write these zeros:
    // - When the value is large a temporary heap buffer needs to be allocated.
    // - When to_chars writes the values they need to be "copied" to the output:
    //   - char: std::fill on the output iterator is faster than std::copy.
    //   - wchar_t: same argument as char, but additional std::copy won't work.
    //     The input is always a char buffer, so every char in the buffer needs
    //     to be converted from a char to a wchar_t.
    if (__precision_ > _Traits::__max_fractional) {
      __num_trailing_zeros_ = __precision_ - _Traits::__max_fractional;
      __precision_          = _Traits::__max_fractional;
    }

    __size_ = __formatter::__float_buffer_size<_Fp>(__precision_);
    if (__size_ > _Traits::__stack_buffer_size)
      // The allocated buffer's contents don't need initialization.
      __begin_ = allocator<char>{}.allocate(__size_);
    else
      __begin_ = __buffer_;
  }

  _LIBCPP_HIDE_FROM_ABI ~__float_buffer() {
    if (__size_ > _Traits::__stack_buffer_size)
      allocator<char>{}.deallocate(__begin_, __size_);
  }
  _LIBCPP_HIDE_FROM_ABI __float_buffer(const __float_buffer&)            = delete;
  _LIBCPP_HIDE_FROM_ABI __float_buffer& operator=(const __float_buffer&) = delete;

  _LIBCPP_HIDE_FROM_ABI char* begin() const { return __begin_; }
  _LIBCPP_HIDE_FROM_ABI char* end() const { return __begin_ + __size_; }

  _LIBCPP_HIDE_FROM_ABI int __precision() const { return __precision_; }
  _LIBCPP_HIDE_FROM_ABI int __num_trailing_zeros() const { return __num_trailing_zeros_; }
  _LIBCPP_HIDE_FROM_ABI void __remove_trailing_zeros() { __num_trailing_zeros_ = 0; }
  _LIBCPP_HIDE_FROM_ABI void __add_trailing_zeros(int __zeros) { __num_trailing_zeros_ += __zeros; }

private:
  int __precision_;
  int __num_trailing_zeros_{0};
  size_t __size_;
  char* __begin_;
  char __buffer_[_Traits::__stack_buffer_size];
};

struct __float_result {
  /// Points at the beginning of the integral part in the buffer.
  ///
  /// When there's no sign character this points at the start of the buffer.
  char* __integral;

  /// Points at the radix point, when not present it's the same as \ref __last.
  char* __radix_point;

  /// Points at the exponent character, when not present it's the same as \ref __last.
  char* __exponent;

  /// Points beyond the last written element in the buffer.
  char* __last;
};

/// Finds the position of the exponent character 'e' at the end of the buffer.
///
/// Assuming there is an exponent the input will terminate with
/// eSdd and eSdddd (S = sign, d = digit)
///
/// \returns a pointer to the exponent or __last when not found.
constexpr inline _LIBCPP_HIDE_FROM_ABI char* __find_exponent(char* __first, char* __last) {
  ptrdiff_t __size = __last - __first;
  if (__size >= 4) {
    __first = __last - std::min(__size, ptrdiff_t(6));
    for (; __first != __last - 3; ++__first) {
      if (*__first == 'e')
        return __first;
    }
  }
  return __last;
}

template <class _Fp, class _Tp>
_LIBCPP_HIDE_FROM_ABI __float_result
__format_buffer_default(const __float_buffer<_Fp>& __buffer, _Tp __value, char* __integral) {
  __float_result __result;
  __result.__integral = __integral;
  __result.__last     = __formatter::__to_buffer(__integral, __buffer.end(), __value);

  __result.__exponent = __formatter::__find_exponent(__result.__integral, __result.__last);

  // Constrains:
  // - There's at least one decimal digit before the radix point.
  // - The radix point, when present, is placed before the exponent.
  __result.__radix_point = std::find(__result.__integral + 1, __result.__exponent, '.');

  // When the radix point isn't found its position is the exponent instead of
  // __result.__last.
  if (__result.__radix_point == __result.__exponent)
    __result.__radix_point = __result.__last;

  // clang-format off
  _LIBCPP_ASSERT_INTERNAL((__result.__integral != __result.__last) &&
                          (__result.__radix_point == __result.__last || *__result.__radix_point == '.') &&
                          (__result.__exponent == __result.__last || *__result.__exponent == 'e'),
                          "Post-condition failure.");
  // clang-format on

  return __result;
}

template <class _Fp, class _Tp>
_LIBCPP_HIDE_FROM_ABI __float_result __format_buffer_hexadecimal_lower_case(
    const __float_buffer<_Fp>& __buffer, _Tp __value, int __precision, char* __integral) {
  __float_result __result;
  __result.__integral = __integral;
  if (__precision == -1)
    __result.__last = __formatter::__to_buffer(__integral, __buffer.end(), __value, chars_format::hex);
  else
    __result.__last = __formatter::__to_buffer(__integral, __buffer.end(), __value, chars_format::hex, __precision);

  // H = one or more hex-digits
  // S = sign
  // D = one or more decimal-digits
  // When the fractional part is zero and no precision the output is 0p+0
  // else the output is                                              0.HpSD
  // So testing the second position can differentiate between these two cases.
  char* __first = __integral + 1;
  if (*__first == '.') {
    __result.__radix_point = __first;
    // One digit is the minimum
    // 0.hpSd
    //       ^-- last
    //     ^---- integral = end of search
    // ^-------- start of search
    // 0123456
    //
    // Four digits is the maximum
    // 0.hpSdddd
    //          ^-- last
    //        ^---- integral = end of search
    //    ^-------- start of search
    // 0123456789
    static_assert(__traits<_Fp>::__hex_precision_digits <= 4, "Guard against possible underflow.");

    char* __last        = __result.__last - 2;
    __first             = __last - __traits<_Fp>::__hex_precision_digits;
    __result.__exponent = std::find(__first, __last, 'p');
  } else {
    __result.__radix_point = __result.__last;
    __result.__exponent    = __first;
  }

  // clang-format off
  _LIBCPP_ASSERT_INTERNAL((__result.__integral != __result.__last) &&
                          (__result.__radix_point == __result.__last || *__result.__radix_point == '.') &&
                          (__result.__exponent != __result.__last && *__result.__exponent == 'p'),
                          "Post-condition failure.");
  // clang-format on

  return __result;
}

template <class _Fp, class _Tp>
_LIBCPP_HIDE_FROM_ABI __float_result __format_buffer_hexadecimal_upper_case(
    const __float_buffer<_Fp>& __buffer, _Tp __value, int __precision, char* __integral) {
  __float_result __result =
      __formatter::__format_buffer_hexadecimal_lower_case(__buffer, __value, __precision, __integral);
  std::transform(__result.__integral, __result.__exponent, __result.__integral, __hex_to_upper);
  *__result.__exponent = 'P';
  return __result;
}

template <class _Fp, class _Tp>
_LIBCPP_HIDE_FROM_ABI __float_result __format_buffer_scientific_lower_case(
    const __float_buffer<_Fp>& __buffer, _Tp __value, int __precision, char* __integral) {
  __float_result __result;
  __result.__integral = __integral;
  __result.__last =
      __formatter::__to_buffer(__integral, __buffer.end(), __value, chars_format::scientific, __precision);

  char* __first = __integral + 1;
  _LIBCPP_ASSERT_INTERNAL(__first != __result.__last, "No exponent present");
  if (*__first == '.') {
    __result.__radix_point = __first;
    __result.__exponent    = __formatter::__find_exponent(__first + 1, __result.__last);
  } else {
    __result.__radix_point = __result.__last;
    __result.__exponent    = __first;
  }

  // clang-format off
  _LIBCPP_ASSERT_INTERNAL((__result.__integral != __result.__last) &&
                          (__result.__radix_point == __result.__last || *__result.__radix_point == '.') &&
                          (__result.__exponent != __result.__last && *__result.__exponent == 'e'),
                          "Post-condition failure.");
  // clang-format on
  return __result;
}

template <class _Fp, class _Tp>
_LIBCPP_HIDE_FROM_ABI __float_result __format_buffer_scientific_upper_case(
    const __float_buffer<_Fp>& __buffer, _Tp __value, int __precision, char* __integral) {
  __float_result __result =
      __formatter::__format_buffer_scientific_lower_case(__buffer, __value, __precision, __integral);
  *__result.__exponent = 'E';
  return __result;
}

template <class _Fp, class _Tp>
_LIBCPP_HIDE_FROM_ABI __float_result
__format_buffer_fixed(const __float_buffer<_Fp>& __buffer, _Tp __value, int __precision, char* __integral) {
  __float_result __result;
  __result.__integral = __integral;
  __result.__last     = __formatter::__to_buffer(__integral, __buffer.end(), __value, chars_format::fixed, __precision);

  // When there's no precision there's no radix point.
  // Else the radix point is placed at __precision + 1 from the end.
  // By converting __precision to a bool the subtraction can be done
  // unconditionally.
  __result.__radix_point = __result.__last - (__precision + bool(__precision));
  __result.__exponent    = __result.__last;

  // clang-format off
  _LIBCPP_ASSERT_INTERNAL((__result.__integral != __result.__last) &&
                          (__result.__radix_point == __result.__last || *__result.__radix_point == '.') &&
                          (__result.__exponent == __result.__last),
                          "Post-condition failure.");
  // clang-format on
  return __result;
}

template <class _Fp, class _Tp>
_LIBCPP_HIDE_FROM_ABI __float_result
__format_buffer_general_lower_case(__float_buffer<_Fp>& __buffer, _Tp __value, int __precision, char* __integral) {
  __buffer.__remove_trailing_zeros();

  __float_result __result;
  __result.__integral = __integral;
  __result.__last = __formatter::__to_buffer(__integral, __buffer.end(), __value, chars_format::general, __precision);

  char* __first = __integral + 1;
  if (__first == __result.__last) {
    __result.__radix_point = __result.__last;
    __result.__exponent    = __result.__last;
  } else {
    __result.__exponent = __formatter::__find_exponent(__first, __result.__last);
    if (__result.__exponent != __result.__last)
      // In scientific mode if there's a radix point it will always be after
      // the first digit. (This is the position __first points at).
      __result.__radix_point = *__first == '.' ? __first : __result.__last;
    else {
      // In fixed mode the algorithm truncates trailing spaces and possibly the
      // radix point. There's no good guess for the position of the radix point
      // therefore scan the output after the first digit.
      __result.__radix_point = std::find(__first, __result.__last, '.');
    }
  }

  // clang-format off
  _LIBCPP_ASSERT_INTERNAL((__result.__integral != __result.__last) &&
                          (__result.__radix_point == __result.__last || *__result.__radix_point == '.') &&
                          (__result.__exponent == __result.__last || *__result.__exponent == 'e'),
                          "Post-condition failure.");
  // clang-format on

  return __result;
}

template <class _Fp, class _Tp>
_LIBCPP_HIDE_FROM_ABI __float_result
__format_buffer_general_upper_case(__float_buffer<_Fp>& __buffer, _Tp __value, int __precision, char* __integral) {
  __float_result __result = __formatter::__format_buffer_general_lower_case(__buffer, __value, __precision, __integral);
  if (__result.__exponent != __result.__last)
    *__result.__exponent = 'E';
  return __result;
}

/// Fills the buffer with the data based on the requested formatting.
///
/// This function, when needed, turns the characters to upper case and
/// determines the "interesting" locations which are returned to the caller.
///
/// This means the caller never has to convert the contents of the buffer to
/// upper case or search for radix points and the location of the exponent.
/// This gives a bit of overhead. The original code didn't do that, but due
/// to the number of possible additional work needed to turn this number to
/// the proper output the code was littered with tests for upper cases and
/// searches for radix points and exponents.
/// - When a precision larger than the type's precision is selected
///   additional zero characters need to be written before the exponent.
/// - alternate form needs to add a radix point when not present.
/// - localization needs to do grouping in the integral part.
template <class _Fp, class _Tp>
// TODO FMT _Fp should just be _Tp when to_chars has proper long double support.
_LIBCPP_HIDE_FROM_ABI __float_result __format_buffer(
    __float_buffer<_Fp>& __buffer,
    _Tp __value,
    bool __negative,
    bool __has_precision,
    __format_spec::__sign __sign,
    __format_spec::__type __type) {
  char* __first = __formatter::__insert_sign(__buffer.begin(), __negative, __sign);
  switch (__type) {
  case __format_spec::__type::__default:
    if (__has_precision)
      return __formatter::__format_buffer_general_lower_case(__buffer, __value, __buffer.__precision(), __first);
    else
      return __formatter::__format_buffer_default(__buffer, __value, __first);

  case __format_spec::__type::__hexfloat_lower_case:
    return __formatter::__format_buffer_hexadecimal_lower_case(
        __buffer, __value, __has_precision ? __buffer.__precision() : -1, __first);

  case __format_spec::__type::__hexfloat_upper_case:
    return __formatter::__format_buffer_hexadecimal_upper_case(
        __buffer, __value, __has_precision ? __buffer.__precision() : -1, __first);

  case __format_spec::__type::__scientific_lower_case:
    return __formatter::__format_buffer_scientific_lower_case(__buffer, __value, __buffer.__precision(), __first);

  case __format_spec::__type::__scientific_upper_case:
    return __formatter::__format_buffer_scientific_upper_case(__buffer, __value, __buffer.__precision(), __first);

  case __format_spec::__type::__fixed_lower_case:
  case __format_spec::__type::__fixed_upper_case:
    return __formatter::__format_buffer_fixed(__buffer, __value, __buffer.__precision(), __first);

  case __format_spec::__type::__general_lower_case:
    return __formatter::__format_buffer_general_lower_case(__buffer, __value, __buffer.__precision(), __first);

  case __format_spec::__type::__general_upper_case:
    return __formatter::__format_buffer_general_upper_case(__buffer, __value, __buffer.__precision(), __first);

  default:
    _LIBCPP_ASSERT_INTERNAL(false, "The parser should have validated the type");
    __libcpp_unreachable();
  }
}

#  ifndef _LIBCPP_HAS_NO_LOCALIZATION
template <class _OutIt, class _Fp, class _CharT>
_LIBCPP_HIDE_FROM_ABI _OutIt __format_locale_specific_form(
    _OutIt __out_it,
    const __float_buffer<_Fp>& __buffer,
    const __float_result& __result,
    std::locale __loc,
    __format_spec::__parsed_specifications<_CharT> __specs) {
  const auto& __np  = std::use_facet<numpunct<_CharT>>(__loc);
  string __grouping = __np.grouping();
  char* __first     = __result.__integral;
  // When no radix point or exponent are present __last will be __result.__last.
  char* __last = std::min(__result.__radix_point, __result.__exponent);

  ptrdiff_t __digits = __last - __first;
  if (!__grouping.empty()) {
    if (__digits <= __grouping[0])
      __grouping.clear();
    else
      __grouping = __formatter::__determine_grouping(__digits, __grouping);
  }

  ptrdiff_t __size =
      __result.__last - __buffer.begin() + // Formatted string
      __buffer.__num_trailing_zeros() +    // Not yet rendered zeros
      __grouping.size() -                  // Grouping contains one
      !__grouping.empty();                 // additional character

  __formatter::__padding_size_result __padding = {0, 0};
  bool __zero_padding                          = __specs.__alignment_ == __format_spec::__alignment::__zero_padding;
  if (__size < __specs.__width_) {
    if (__zero_padding) {
      __specs.__alignment_      = __format_spec::__alignment::__right;
      __specs.__fill_.__data[0] = _CharT('0');
    }

    __padding = __formatter::__padding_size(__size, __specs.__width_, __specs.__alignment_);
  }

  // sign and (zero padding or alignment)
  if (__zero_padding && __first != __buffer.begin())
    *__out_it++ = *__buffer.begin();
  __out_it = __formatter::__fill(std::move(__out_it), __padding.__before_, __specs.__fill_);
  if (!__zero_padding && __first != __buffer.begin())
    *__out_it++ = *__buffer.begin();

  // integral part
  if (__grouping.empty()) {
    __out_it = __formatter::__copy(__first, __digits, std::move(__out_it));
  } else {
    auto __r     = __grouping.rbegin();
    auto __e     = __grouping.rend() - 1;
    _CharT __sep = __np.thousands_sep();
    // The output is divided in small groups of numbers to write:
    // - A group before the first separator.
    // - A separator and a group, repeated for the number of separators.
    // - A group after the last separator.
    // This loop achieves that process by testing the termination condition
    // midway in the loop.
    while (true) {
      __out_it = __formatter::__copy(__first, *__r, std::move(__out_it));
      __first += *__r;

      if (__r == __e)
        break;

      ++__r;
      *__out_it++ = __sep;
    }
  }

  // fractional part
  if (__result.__radix_point != __result.__last) {
    *__out_it++ = __np.decimal_point();
    __out_it    = __formatter::__copy(__result.__radix_point + 1, __result.__exponent, std::move(__out_it));
    __out_it    = __formatter::__fill(std::move(__out_it), __buffer.__num_trailing_zeros(), _CharT('0'));
  }

  // exponent
  if (__result.__exponent != __result.__last)
    __out_it = __formatter::__copy(__result.__exponent, __result.__last, std::move(__out_it));

  // alignment
  return __formatter::__fill(std::move(__out_it), __padding.__after_, __specs.__fill_);
}
#  endif // _LIBCPP_HAS_NO_LOCALIZATION

template <class _OutIt, class _CharT>
_LIBCPP_HIDE_FROM_ABI _OutIt __format_floating_point_non_finite(
    _OutIt __out_it, __format_spec::__parsed_specifications<_CharT> __specs, bool __negative, bool __isnan) {
  char __buffer[4];
  char* __last = __formatter::__insert_sign(__buffer, __negative, __specs.__std_.__sign_);

  // to_chars can return inf, infinity, nan, and nan(n-char-sequence).
  // The format library requires inf and nan.
  // All in one expression to avoid dangling references.
  bool __upper_case =
      __specs.__std_.__type_ == __format_spec::__type::__hexfloat_upper_case ||
      __specs.__std_.__type_ == __format_spec::__type::__scientific_upper_case ||
      __specs.__std_.__type_ == __format_spec::__type::__fixed_upper_case ||
      __specs.__std_.__type_ == __format_spec::__type::__general_upper_case;
  __last = std::copy_n(&("infnanINFNAN"[6 * __upper_case + 3 * __isnan]), 3, __last);

  // [format.string.std]/13
  // A zero (0) character preceding the width field pads the field with
  // leading zeros (following any indication of sign or base) to the field
  // width, except when applied to an infinity or NaN.
  if (__specs.__alignment_ == __format_spec::__alignment::__zero_padding)
    __specs.__alignment_ = __format_spec::__alignment::__right;

  return __formatter::__write(__buffer, __last, std::move(__out_it), __specs);
}

/// Writes additional zero's for the precision before the exponent.
/// This is used when the precision requested in the format string is larger
/// than the maximum precision of the floating-point type. These precision
/// digits are always 0.
///
/// \param __exponent           The location of the exponent character.
/// \param __num_trailing_zeros The number of 0's to write before the exponent
///                             character.
template <class _CharT, class _ParserCharT>
_LIBCPP_HIDE_FROM_ABI auto __write_using_trailing_zeros(
    const _CharT* __first,
    const _CharT* __last,
    output_iterator<const _CharT&> auto __out_it,
    __format_spec::__parsed_specifications<_ParserCharT> __specs,
    size_t __size,
    const _CharT* __exponent,
    size_t __num_trailing_zeros) -> decltype(__out_it) {
  _LIBCPP_ASSERT_INTERNAL(__first <= __last, "Not a valid range");
  _LIBCPP_ASSERT_INTERNAL(__num_trailing_zeros > 0, "The overload not writing trailing zeros should have been used");

  __padding_size_result __padding =
      __formatter::__padding_size(__size + __num_trailing_zeros, __specs.__width_, __specs.__alignment_);
  __out_it = __formatter::__fill(std::move(__out_it), __padding.__before_, __specs.__fill_);
  __out_it = __formatter::__copy(__first, __exponent, std::move(__out_it));
  __out_it = __formatter::__fill(std::move(__out_it), __num_trailing_zeros, _CharT('0'));
  __out_it = __formatter::__copy(__exponent, __last, std::move(__out_it));
  return __formatter::__fill(std::move(__out_it), __padding.__after_, __specs.__fill_);
}

template <floating_point _Tp, class _CharT, class _FormatContext>
_LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator
__format_floating_point(_Tp __value, _FormatContext& __ctx, __format_spec::__parsed_specifications<_CharT> __specs) {
  bool __negative = std::signbit(__value);

  if (!std::isfinite(__value)) [[unlikely]]
    return __formatter::__format_floating_point_non_finite(__ctx.out(), __specs, __negative, std::isnan(__value));

  // Depending on the std-format-spec string the sign and the value
  // might not be outputted together:
  // - zero-padding may insert additional '0' characters.
  // Therefore the value is processed as a non negative value.
  // The function @ref __insert_sign will insert a '-' when the value was
  // negative.

  if (__negative)
    __value = -__value;

  // TODO FMT _Fp should just be _Tp when to_chars has proper long double support.
  using _Fp = conditional_t<same_as<_Tp, long double>, double, _Tp>;
  // Force the type of the precision to avoid -1 to become an unsigned value.
  __float_buffer<_Fp> __buffer(__specs.__precision_);
  __float_result __result = __formatter::__format_buffer(
      __buffer, __value, __negative, (__specs.__has_precision()), __specs.__std_.__sign_, __specs.__std_.__type_);

  if (__specs.__std_.__alternate_form_) {
    if (__result.__radix_point == __result.__last) {
      *__result.__last++ = '.';

      // When there is an exponent the point needs to be moved before the
      // exponent. When there's no exponent the rotate does nothing. Since
      // rotate tests whether the operation is a nop, call it unconditionally.
      std::rotate(__result.__exponent, __result.__last - 1, __result.__last);
      __result.__radix_point = __result.__exponent;

      // The radix point is always placed before the exponent.
      // - No exponent needs to point to the new last.
      // - An exponent needs to move one position to the right.
      // So it's safe to increment the value unconditionally.
      ++__result.__exponent;
    }

    // [format.string.std]/6
    //   In addition, for g and G conversions, trailing zeros are not removed
    //   from the result.
    //
    // If the type option for a floating-point type is none it may use the
    // general formatting, but it's not a g or G conversion. So in that case
    // the formatting should not append trailing zeros.
    bool __is_general = __specs.__std_.__type_ == __format_spec::__type::__general_lower_case ||
                        __specs.__std_.__type_ == __format_spec::__type::__general_upper_case;

    if (__is_general) {
      // https://en.cppreference.com/w/c/io/fprintf
      // Let P equal the precision if nonzero, 6 if the precision is not
      // specified, or 1 if the precision is 0. Then, if a conversion with
      // style E would have an exponent of X:
      int __p = std::max<int>(1, (__specs.__has_precision() ? __specs.__precision_ : 6));
      if (__result.__exponent == __result.__last)
        // if P > X >= -4, the conversion is with style f or F and precision P - 1 - X.
        // By including the radix point it calculates P - (1 + X)
        __p -= __result.__radix_point - __result.__integral;
      else
        // otherwise, the conversion is with style e or E and precision P - 1.
        --__p;

      ptrdiff_t __precision = (__result.__exponent - __result.__radix_point) - 1;
      if (__precision < __p)
        __buffer.__add_trailing_zeros(__p - __precision);
    }
  }

#  ifndef _LIBCPP_HAS_NO_LOCALIZATION
  if (__specs.__std_.__locale_specific_form_)
    return __formatter::__format_locale_specific_form(__ctx.out(), __buffer, __result, __ctx.locale(), __specs);
#  endif

  ptrdiff_t __size         = __result.__last - __buffer.begin();
  int __num_trailing_zeros = __buffer.__num_trailing_zeros();
  if (__size + __num_trailing_zeros >= __specs.__width_) {
    if (__num_trailing_zeros && __result.__exponent != __result.__last)
      // Insert trailing zeros before exponent character.
      return __formatter::__copy(
          __result.__exponent,
          __result.__last,
          __formatter::__fill(__formatter::__copy(__buffer.begin(), __result.__exponent, __ctx.out()),
                              __num_trailing_zeros,
                              _CharT('0')));

    return __formatter::__fill(
        __formatter::__copy(__buffer.begin(), __result.__last, __ctx.out()), __num_trailing_zeros, _CharT('0'));
  }

  auto __out_it = __ctx.out();
  char* __first = __buffer.begin();
  if (__specs.__alignment_ == __format_spec::__alignment ::__zero_padding) {
    // When there is a sign output it before the padding. Note the __size
    // doesn't need any adjustment, regardless whether the sign is written
    // here or in __formatter::__write.
    if (__first != __result.__integral)
      *__out_it++ = *__first++;
    // After the sign is written, zero padding is the same a right alignment
    // with '0'.
    __specs.__alignment_      = __format_spec::__alignment::__right;
    __specs.__fill_.__data[0] = _CharT('0');
  }

  if (__num_trailing_zeros)
    return __formatter::__write_using_trailing_zeros(
        __first, __result.__last, std::move(__out_it), __specs, __size, __result.__exponent, __num_trailing_zeros);

  return __formatter::__write(__first, __result.__last, std::move(__out_it), __specs, __size);
}

} // namespace __formatter

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS __formatter_floating_point {
public:
  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    typename _ParseContext::iterator __result = __parser_.__parse(__ctx, __format_spec::__fields_floating_point);
    __format_spec::__process_parsed_floating_point(__parser_, "a floating-point");
    return __result;
  }

  template <floating_point _Tp, class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator format(_Tp __value, _FormatContext& __ctx) const {
    return __formatter::__format_floating_point(__value, __ctx, __parser_.__get_parsed_std_specifications(__ctx));
  }

  __format_spec::__parser<_CharT> __parser_;
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<float, _CharT> : public __formatter_floating_point<_CharT> {};
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<double, _CharT> : public __formatter_floating_point<_CharT> {};
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<long double, _CharT> : public __formatter_floating_point<_CharT> {};

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FORMAT_FORMATTER_FLOATING_POINT_H
