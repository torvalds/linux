// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMAT_STRING_H
#define _LIBCPP___FORMAT_FORMAT_STRING_H

#include <__assert>
#include <__config>
#include <__format/format_error.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h> // iter_value_t
#include <cstddef>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace __format {

template <contiguous_iterator _Iterator>
struct _LIBCPP_TEMPLATE_VIS __parse_number_result {
  _Iterator __last;
  uint32_t __value;
};

template <contiguous_iterator _Iterator>
__parse_number_result(_Iterator, uint32_t) -> __parse_number_result<_Iterator>;

template <contiguous_iterator _Iterator>
_LIBCPP_HIDE_FROM_ABI constexpr __parse_number_result<_Iterator> __parse_number(_Iterator __begin, _Iterator __end);

/**
 * The maximum value of a numeric argument.
 *
 * This is used for:
 * * arg-id
 * * width as value or arg-id.
 * * precision as value or arg-id.
 *
 * The value is compatible with the maximum formatting width and precision
 * using the `%*` syntax on a 32-bit system.
 */
inline constexpr uint32_t __number_max = INT32_MAX;

namespace __detail {
template <contiguous_iterator _Iterator>
_LIBCPP_HIDE_FROM_ABI constexpr __parse_number_result<_Iterator>
__parse_zero(_Iterator __begin, _Iterator, auto& __parse_ctx) {
  __parse_ctx.check_arg_id(0);
  return {++__begin, 0}; // can never be larger than the maximum.
}

template <contiguous_iterator _Iterator>
_LIBCPP_HIDE_FROM_ABI constexpr __parse_number_result<_Iterator>
__parse_automatic(_Iterator __begin, _Iterator, auto& __parse_ctx) {
  size_t __value = __parse_ctx.next_arg_id();
  _LIBCPP_ASSERT_UNCATEGORIZED(__value <= __number_max, "Compilers don't support this number of arguments");

  return {__begin, uint32_t(__value)};
}

template <contiguous_iterator _Iterator>
_LIBCPP_HIDE_FROM_ABI constexpr __parse_number_result<_Iterator>
__parse_manual(_Iterator __begin, _Iterator __end, auto& __parse_ctx) {
  __parse_number_result<_Iterator> __r = __format::__parse_number(__begin, __end);
  __parse_ctx.check_arg_id(__r.__value);
  return __r;
}

} // namespace __detail

/**
 * Parses a number.
 *
 * The number is used for the 31-bit values @em width and @em precision. This
 * allows a maximum value of 2147483647.
 */
template <contiguous_iterator _Iterator>
_LIBCPP_HIDE_FROM_ABI constexpr __parse_number_result<_Iterator>
__parse_number(_Iterator __begin, _Iterator __end_input) {
  using _CharT = iter_value_t<_Iterator>;
  static_assert(__format::__number_max == INT32_MAX, "The algorithm is implemented based on this value.");
  /*
   * Limit the input to 9 digits, otherwise we need two checks during every
   * iteration:
   * - Are we at the end of the input?
   * - Does the value exceed width of an uint32_t? (Switching to uint64_t would
   *   have the same issue, but with a higher maximum.)
   */
  _Iterator __end  = __end_input - __begin > 9 ? __begin + 9 : __end_input;
  uint32_t __value = *__begin - _CharT('0');
  while (++__begin != __end) {
    if (*__begin < _CharT('0') || *__begin > _CharT('9'))
      return {__begin, __value};

    __value = __value * 10 + *__begin - _CharT('0');
  }

  if (__begin != __end_input && *__begin >= _CharT('0') && *__begin <= _CharT('9')) {
    /*
     * There are more than 9 digits, do additional validations:
     * - Does the 10th digit exceed the maximum allowed value?
     * - Are there more than 10 digits?
     * (More than 10 digits always overflows the maximum.)
     */
    uint64_t __v = uint64_t(__value) * 10 + *__begin++ - _CharT('0');
    if (__v > __number_max || (__begin != __end_input && *__begin >= _CharT('0') && *__begin <= _CharT('9')))
      std::__throw_format_error("The numeric value of the format specifier is too large");

    __value = __v;
  }

  return {__begin, __value};
}

/**
 * Multiplexer for all parse functions.
 *
 * The parser will return a pointer beyond the last consumed character. This
 * should be the closing '}' of the arg-id.
 */
template <contiguous_iterator _Iterator>
_LIBCPP_HIDE_FROM_ABI constexpr __parse_number_result<_Iterator>
__parse_arg_id(_Iterator __begin, _Iterator __end, auto& __parse_ctx) {
  using _CharT = iter_value_t<_Iterator>;
  switch (*__begin) {
  case _CharT('0'):
    return __detail::__parse_zero(__begin, __end, __parse_ctx);

  case _CharT(':'):
    // This case is conditionally valid. It's allowed in an arg-id in the
    // replacement-field, but not in the std-format-spec. The caller can
    // provide a better diagnostic, so accept it here unconditionally.
  case _CharT('}'):
    return __detail::__parse_automatic(__begin, __end, __parse_ctx);
  }
  if (*__begin < _CharT('0') || *__begin > _CharT('9'))
    std::__throw_format_error("The argument index starts with an invalid character");

  return __detail::__parse_manual(__begin, __end, __parse_ctx);
}

} // namespace __format

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_FORMAT_STRING_H
