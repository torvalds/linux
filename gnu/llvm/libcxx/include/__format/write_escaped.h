// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_WRITE_ESCAPED_H
#define _LIBCPP___FORMAT_WRITE_ESCAPED_H

#include <__algorithm/ranges_copy.h>
#include <__algorithm/ranges_for_each.h>
#include <__charconv/to_chars_integral.h>
#include <__charconv/to_chars_result.h>
#include <__chrono/statically_widen.h>
#include <__format/escaped_output_table.h>
#include <__format/formatter_output.h>
#include <__format/parser_std_format_spec.h>
#include <__format/unicode.h>
#include <__iterator/back_insert_iterator.h>
#include <__memory/addressof.h>
#include <__system_error/errc.h>
#include <__type_traits/make_unsigned.h>
#include <__utility/move.h>
#include <string_view>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

namespace __formatter {

#if _LIBCPP_STD_VER >= 20

/// Writes a string using format's width estimation algorithm.
///
/// \note When \c _LIBCPP_HAS_NO_UNICODE is defined the function assumes the
/// input is ASCII.
template <class _CharT>
_LIBCPP_HIDE_FROM_ABI auto
__write_string(basic_string_view<_CharT> __str,
               output_iterator<const _CharT&> auto __out_it,
               __format_spec::__parsed_specifications<_CharT> __specs) -> decltype(__out_it) {
  if (!__specs.__has_precision())
    return __formatter::__write_string_no_precision(__str, std::move(__out_it), __specs);

  int __size = __formatter::__truncate(__str, __specs.__precision_);

  return __formatter::__write(__str.begin(), __str.end(), std::move(__out_it), __specs, __size);
}

#endif // _LIBCPP_STD_VER >= 20
#if _LIBCPP_STD_VER >= 23

struct __nul_terminator {};

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI bool operator==(const _CharT* __cstr, __nul_terminator) {
  return *__cstr == _CharT('\0');
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI void
__write_escaped_code_unit(basic_string<_CharT>& __str, char32_t __value, const _CharT* __prefix) {
  back_insert_iterator __out_it{__str};
  std::ranges::copy(__prefix, __nul_terminator{}, __out_it);

  char __buffer[8];
  to_chars_result __r = std::to_chars(std::begin(__buffer), std::end(__buffer), __value, 16);
  _LIBCPP_ASSERT_INTERNAL(__r.ec == errc(0), "Internal buffer too small");
  std::ranges::copy(std::begin(__buffer), __r.ptr, __out_it);

  __str += _CharT('}');
}

// [format.string.escaped]/2.2.1.2
// ...
// then the sequence \u{hex-digit-sequence} is appended to E, where
// hex-digit-sequence is the shortest hexadecimal representation of C using
// lower-case hexadecimal digits.
template <class _CharT>
_LIBCPP_HIDE_FROM_ABI void __write_well_formed_escaped_code_unit(basic_string<_CharT>& __str, char32_t __value) {
  __formatter::__write_escaped_code_unit(__str, __value, _LIBCPP_STATICALLY_WIDEN(_CharT, "\\u{"));
}

// [format.string.escaped]/2.2.3
// Otherwise (X is a sequence of ill-formed code units), each code unit U is
// appended to E in order as the sequence \x{hex-digit-sequence}, where
// hex-digit-sequence is the shortest hexadecimal representation of U using
// lower-case hexadecimal digits.
template <class _CharT>
_LIBCPP_HIDE_FROM_ABI void __write_escape_ill_formed_code_unit(basic_string<_CharT>& __str, char32_t __value) {
  __formatter::__write_escaped_code_unit(__str, __value, _LIBCPP_STATICALLY_WIDEN(_CharT, "\\x{"));
}

template <class _CharT>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI bool
__is_escaped_sequence_written(basic_string<_CharT>& __str, bool __last_escaped, char32_t __value) {
#  ifdef _LIBCPP_HAS_NO_UNICODE
  // For ASCII assume everything above 127 is printable.
  if (__value > 127)
    return false;
#  endif

  // [format.string.escaped]/2.2.1.2.1
  //   CE is UTF-8, UTF-16, or UTF-32 and C corresponds to a Unicode scalar
  //   value whose Unicode property General_Category has a value in the groups
  //   Separator (Z) or Other (C), as described by UAX #44 of the Unicode Standard,
  if (!__escaped_output_table::__needs_escape(__value))
    // [format.string.escaped]/2.2.1.2.2
    //   CE is UTF-8, UTF-16, or UTF-32 and C corresponds to a Unicode scalar
    //   value with the Unicode property Grapheme_Extend=Yes as described by UAX
    //   #44 of the Unicode Standard and C is not immediately preceded in S by a
    //   character P appended to E without translation to an escape sequence,
    if (!__last_escaped || __extended_grapheme_custer_property_boundary::__get_property(__value) !=
                               __extended_grapheme_custer_property_boundary::__property::__Extend)
      return false;

  __formatter::__write_well_formed_escaped_code_unit(__str, __value);
  return true;
}

template <class _CharT>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr char32_t __to_char32(_CharT __value) {
  return static_cast<make_unsigned_t<_CharT>>(__value);
}

enum class __escape_quotation_mark { __apostrophe, __double_quote };

// [format.string.escaped]/2
template <class _CharT>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI bool __is_escaped_sequence_written(
    basic_string<_CharT>& __str, char32_t __value, bool __last_escaped, __escape_quotation_mark __mark) {
  // 2.2.1.1 - Mapped character in [tab:format.escape.sequences]
  switch (__value) {
  case _CharT('\t'):
    __str += _LIBCPP_STATICALLY_WIDEN(_CharT, "\\t");
    return true;
  case _CharT('\n'):
    __str += _LIBCPP_STATICALLY_WIDEN(_CharT, "\\n");
    return true;
  case _CharT('\r'):
    __str += _LIBCPP_STATICALLY_WIDEN(_CharT, "\\r");
    return true;
  case _CharT('\''):
    if (__mark == __escape_quotation_mark::__apostrophe)
      __str += _LIBCPP_STATICALLY_WIDEN(_CharT, R"(\')");
    else
      __str += __value;
    return true;
  case _CharT('"'):
    if (__mark == __escape_quotation_mark::__double_quote)
      __str += _LIBCPP_STATICALLY_WIDEN(_CharT, R"(\")");
    else
      __str += __value;
    return true;
  case _CharT('\\'):
    __str += _LIBCPP_STATICALLY_WIDEN(_CharT, R"(\\)");
    return true;

  // 2.2.1.2 - Space
  case _CharT(' '):
    __str += __value;
    return true;
  }

  // 2.2.2
  //   Otherwise, if X is a shift sequence, the effect on E and further
  //   decoding of S is unspecified.
  // For now shift sequences are ignored and treated as Unicode. Other parts
  // of the format library do the same. It's unknown how ostream treats them.
  // TODO FMT determine what to do with shift sequences.

  // 2.2.1.2.1 and 2.2.1.2.2 - Escape
  return __formatter::__is_escaped_sequence_written(__str, __last_escaped, __formatter::__to_char32(__value));
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI void
__escape(basic_string<_CharT>& __str, basic_string_view<_CharT> __values, __escape_quotation_mark __mark) {
  __unicode::__code_point_view<_CharT> __view{__values.begin(), __values.end()};

  // When the first code unit has the property Grapheme_Extend=Yes it needs to
  // be escaped. This happens when the previous code unit was also escaped.
  bool __escape = true;
  while (!__view.__at_end()) {
    auto __first                                  = __view.__position();
    typename __unicode::__consume_result __result = __view.__consume();
    if (__result.__status == __unicode::__consume_result::__ok) {
      __escape = __formatter::__is_escaped_sequence_written(__str, __result.__code_point, __escape, __mark);
      if (!__escape)
        // 2.2.1.3 - Add the character
        ranges::copy(__first, __view.__position(), std::back_insert_iterator(__str));
    } else {
      // 2.2.3 sequence of ill-formed code units
      ranges::for_each(__first, __view.__position(), [&](_CharT __value) {
        __formatter::__write_escape_ill_formed_code_unit(__str, __formatter::__to_char32(__value));
      });
    }
  }
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI auto
__format_escaped_char(_CharT __value,
                      output_iterator<const _CharT&> auto __out_it,
                      __format_spec::__parsed_specifications<_CharT> __specs) -> decltype(__out_it) {
  basic_string<_CharT> __str;
  __str += _CharT('\'');
  __formatter::__escape(__str, basic_string_view{std::addressof(__value), 1}, __escape_quotation_mark::__apostrophe);
  __str += _CharT('\'');
  return __formatter::__write(__str.data(), __str.data() + __str.size(), std::move(__out_it), __specs, __str.size());
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI auto
__format_escaped_string(basic_string_view<_CharT> __values,
                        output_iterator<const _CharT&> auto __out_it,
                        __format_spec::__parsed_specifications<_CharT> __specs) -> decltype(__out_it) {
  basic_string<_CharT> __str;
  __str += _CharT('"');
  __formatter::__escape(__str, __values, __escape_quotation_mark::__double_quote);
  __str += _CharT('"');
  return __formatter::__write_string(basic_string_view{__str}, std::move(__out_it), __specs);
}

#endif // _LIBCPP_STD_VER >= 23

} // namespace __formatter

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FORMAT_WRITE_ESCAPED_H
