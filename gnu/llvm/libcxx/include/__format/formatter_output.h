// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMATTER_OUTPUT_H
#define _LIBCPP___FORMAT_FORMATTER_OUTPUT_H

#include <__algorithm/ranges_copy.h>
#include <__algorithm/ranges_fill_n.h>
#include <__algorithm/ranges_transform.h>
#include <__bit/countl.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__format/buffer.h>
#include <__format/concepts.h>
#include <__format/formatter.h>
#include <__format/parser_std_format_spec.h>
#include <__format/unicode.h>
#include <__iterator/back_insert_iterator.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__memory/addressof.h>
#include <__memory/pointer_traits.h>
#include <__utility/move.h>
#include <__utility/unreachable.h>
#include <cstddef>
#include <string_view>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace __formatter {

_LIBCPP_HIDE_FROM_ABI constexpr char __hex_to_upper(char __c) {
  switch (__c) {
  case 'a':
    return 'A';
  case 'b':
    return 'B';
  case 'c':
    return 'C';
  case 'd':
    return 'D';
  case 'e':
    return 'E';
  case 'f':
    return 'F';
  }
  return __c;
}

struct _LIBCPP_EXPORTED_FROM_ABI __padding_size_result {
  size_t __before_;
  size_t __after_;
};

_LIBCPP_HIDE_FROM_ABI constexpr __padding_size_result
__padding_size(size_t __size, size_t __width, __format_spec::__alignment __align) {
  _LIBCPP_ASSERT_INTERNAL(__width > __size, "don't call this function when no padding is required");
  _LIBCPP_ASSERT_INTERNAL(
      __align != __format_spec::__alignment::__zero_padding, "the caller should have handled the zero-padding");

  size_t __fill = __width - __size;
  switch (__align) {
  case __format_spec::__alignment::__zero_padding:
    __libcpp_unreachable();

  case __format_spec::__alignment::__left:
    return {0, __fill};

  case __format_spec::__alignment::__center: {
    // The extra padding is divided per [format.string.std]/3
    // __before = floor(__fill, 2);
    // __after = ceil(__fill, 2);
    size_t __before = __fill / 2;
    size_t __after  = __fill - __before;
    return {__before, __after};
  }
  case __format_spec::__alignment::__default:
  case __format_spec::__alignment::__right:
    return {__fill, 0};
  }
  __libcpp_unreachable();
}

/// Copy wrapper.
///
/// This uses a "mass output function" of __format::__output_buffer when possible.
template <__fmt_char_type _CharT, __fmt_char_type _OutCharT = _CharT>
_LIBCPP_HIDE_FROM_ABI auto
__copy(basic_string_view<_CharT> __str, output_iterator<const _OutCharT&> auto __out_it) -> decltype(__out_it) {
  if constexpr (std::same_as<decltype(__out_it), std::back_insert_iterator<__format::__output_buffer<_OutCharT>>>) {
    __out_it.__get_container()->__copy(__str);
    return __out_it;
  } else if constexpr (std::same_as<decltype(__out_it), typename __format::__retarget_buffer<_OutCharT>::__iterator>) {
    __out_it.__buffer_->__copy(__str);
    return __out_it;
  } else {
    return std::ranges::copy(__str, std::move(__out_it)).out;
  }
}

template <contiguous_iterator _Iterator,
          __fmt_char_type _CharT    = typename iterator_traits<_Iterator>::value_type,
          __fmt_char_type _OutCharT = _CharT>
_LIBCPP_HIDE_FROM_ABI auto
__copy(_Iterator __first, _Iterator __last, output_iterator<const _OutCharT&> auto __out_it) -> decltype(__out_it) {
  return __formatter::__copy(basic_string_view{__first, __last}, std::move(__out_it));
}

template <contiguous_iterator _Iterator,
          __fmt_char_type _CharT    = typename iterator_traits<_Iterator>::value_type,
          __fmt_char_type _OutCharT = _CharT>
_LIBCPP_HIDE_FROM_ABI auto
__copy(_Iterator __first, size_t __n, output_iterator<const _OutCharT&> auto __out_it) -> decltype(__out_it) {
  return __formatter::__copy(basic_string_view{std::to_address(__first), __n}, std::move(__out_it));
}

/// Transform wrapper.
///
/// This uses a "mass output function" of __format::__output_buffer when possible.
template <contiguous_iterator _Iterator,
          __fmt_char_type _CharT    = typename iterator_traits<_Iterator>::value_type,
          __fmt_char_type _OutCharT = _CharT,
          class _UnaryOperation>
_LIBCPP_HIDE_FROM_ABI auto
__transform(_Iterator __first,
            _Iterator __last,
            output_iterator<const _OutCharT&> auto __out_it,
            _UnaryOperation __operation) -> decltype(__out_it) {
  if constexpr (std::same_as<decltype(__out_it), std::back_insert_iterator<__format::__output_buffer<_OutCharT>>>) {
    __out_it.__get_container()->__transform(__first, __last, std::move(__operation));
    return __out_it;
  } else if constexpr (std::same_as<decltype(__out_it), typename __format::__retarget_buffer<_OutCharT>::__iterator>) {
    __out_it.__buffer_->__transform(__first, __last, std::move(__operation));
    return __out_it;
  } else {
    return std::ranges::transform(__first, __last, std::move(__out_it), __operation).out;
  }
}

/// Fill wrapper.
///
/// This uses a "mass output function" of __format::__output_buffer when possible.
template <__fmt_char_type _CharT, output_iterator<const _CharT&> _OutIt>
_LIBCPP_HIDE_FROM_ABI _OutIt __fill(_OutIt __out_it, size_t __n, _CharT __value) {
  if constexpr (std::same_as<decltype(__out_it), std::back_insert_iterator<__format::__output_buffer<_CharT>>>) {
    __out_it.__get_container()->__fill(__n, __value);
    return __out_it;
  } else if constexpr (std::same_as<decltype(__out_it), typename __format::__retarget_buffer<_CharT>::__iterator>) {
    __out_it.__buffer_->__fill(__n, __value);
    return __out_it;
  } else {
    return std::ranges::fill_n(std::move(__out_it), __n, __value);
  }
}

#  ifndef _LIBCPP_HAS_NO_UNICODE
template <__fmt_char_type _CharT, output_iterator<const _CharT&> _OutIt>
  requires(same_as<_CharT, char>)
_LIBCPP_HIDE_FROM_ABI _OutIt __fill(_OutIt __out_it, size_t __n, __format_spec::__code_point<_CharT> __value) {
  std::size_t __bytes = std::countl_one(static_cast<unsigned char>(__value.__data[0]));
  if (__bytes == 0)
    return __formatter::__fill(std::move(__out_it), __n, __value.__data[0]);

  for (size_t __i = 0; __i < __n; ++__i)
    __out_it = __formatter::__copy(
        std::addressof(__value.__data[0]), std::addressof(__value.__data[0]) + __bytes, std::move(__out_it));
  return __out_it;
}

#    ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <__fmt_char_type _CharT, output_iterator<const _CharT&> _OutIt>
  requires(same_as<_CharT, wchar_t> && sizeof(wchar_t) == 2)
_LIBCPP_HIDE_FROM_ABI _OutIt __fill(_OutIt __out_it, size_t __n, __format_spec::__code_point<_CharT> __value) {
  if (!__unicode::__is_high_surrogate(__value.__data[0]))
    return __formatter::__fill(std::move(__out_it), __n, __value.__data[0]);

  for (size_t __i = 0; __i < __n; ++__i)
    __out_it = __formatter::__copy(
        std::addressof(__value.__data[0]), std::addressof(__value.__data[0]) + 2, std::move(__out_it));
  return __out_it;
}

template <__fmt_char_type _CharT, output_iterator<const _CharT&> _OutIt>
  requires(same_as<_CharT, wchar_t> && sizeof(wchar_t) == 4)
_LIBCPP_HIDE_FROM_ABI _OutIt __fill(_OutIt __out_it, size_t __n, __format_spec::__code_point<_CharT> __value) {
  return __formatter::__fill(std::move(__out_it), __n, __value.__data[0]);
}
#    endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS
#  else    // _LIBCPP_HAS_NO_UNICODE
template <__fmt_char_type _CharT, output_iterator<const _CharT&> _OutIt>
_LIBCPP_HIDE_FROM_ABI _OutIt __fill(_OutIt __out_it, size_t __n, __format_spec::__code_point<_CharT> __value) {
  return __formatter::__fill(std::move(__out_it), __n, __value.__data[0]);
}
#  endif   // _LIBCPP_HAS_NO_UNICODE

/// Writes the input to the output with the required padding.
///
/// Since the output column width is specified the function can be used for
/// ASCII and Unicode output.
///
/// \pre \a __size <= \a __width. Using this function when this pre-condition
///      doesn't hold incurs an unwanted overhead.
///
/// \param __str       The string to write.
/// \param __out_it    The output iterator to write to.
/// \param __specs     The parsed formatting specifications.
/// \param __size      The (estimated) output column width. When the elements
///                    to be written are ASCII the following condition holds
///                    \a __size == \a __last - \a __first.
///
/// \returns           An iterator pointing beyond the last element written.
///
/// \note The type of the elements in range [\a __first, \a __last) can differ
/// from the type of \a __specs. Integer output uses \c std::to_chars for its
/// conversion, which means the [\a __first, \a __last) always contains elements
/// of the type \c char.
template <class _CharT, class _ParserCharT>
_LIBCPP_HIDE_FROM_ABI auto
__write(basic_string_view<_CharT> __str,
        output_iterator<const _CharT&> auto __out_it,
        __format_spec::__parsed_specifications<_ParserCharT> __specs,
        ptrdiff_t __size) -> decltype(__out_it) {
  if (__size >= __specs.__width_)
    return __formatter::__copy(__str, std::move(__out_it));

  __padding_size_result __padding = __formatter::__padding_size(__size, __specs.__width_, __specs.__std_.__alignment_);
  __out_it                        = __formatter::__fill(std::move(__out_it), __padding.__before_, __specs.__fill_);
  __out_it                        = __formatter::__copy(__str, std::move(__out_it));
  return __formatter::__fill(std::move(__out_it), __padding.__after_, __specs.__fill_);
}

template <contiguous_iterator _Iterator, class _ParserCharT>
_LIBCPP_HIDE_FROM_ABI auto
__write(_Iterator __first,
        _Iterator __last,
        output_iterator<const iter_value_t<_Iterator>&> auto __out_it,
        __format_spec::__parsed_specifications<_ParserCharT> __specs,
        ptrdiff_t __size) -> decltype(__out_it) {
  _LIBCPP_ASSERT_VALID_INPUT_RANGE(__first <= __last, "Not a valid range");
  return __formatter::__write(basic_string_view{__first, __last}, std::move(__out_it), __specs, __size);
}

/// \overload
///
/// Calls the function above where \a __size = \a __last - \a __first.
template <contiguous_iterator _Iterator, class _ParserCharT>
_LIBCPP_HIDE_FROM_ABI auto
__write(_Iterator __first,
        _Iterator __last,
        output_iterator<const iter_value_t<_Iterator>&> auto __out_it,
        __format_spec::__parsed_specifications<_ParserCharT> __specs) -> decltype(__out_it) {
  _LIBCPP_ASSERT_VALID_INPUT_RANGE(__first <= __last, "Not a valid range");
  return __formatter::__write(__first, __last, std::move(__out_it), __specs, __last - __first);
}

template <contiguous_iterator _Iterator,
          class _CharT = typename iterator_traits<_Iterator>::value_type,
          class _ParserCharT,
          class _UnaryOperation>
_LIBCPP_HIDE_FROM_ABI auto __write_transformed(
    _Iterator __first,
    _Iterator __last,
    output_iterator<const _CharT&> auto __out_it,
    __format_spec::__parsed_specifications<_ParserCharT> __specs,
    _UnaryOperation __op) -> decltype(__out_it) {
  _LIBCPP_ASSERT_VALID_INPUT_RANGE(__first <= __last, "Not a valid range");

  ptrdiff_t __size = __last - __first;
  if (__size >= __specs.__width_)
    return __formatter::__transform(__first, __last, std::move(__out_it), __op);

  __padding_size_result __padding = __formatter::__padding_size(__size, __specs.__width_, __specs.__alignment_);
  __out_it                        = __formatter::__fill(std::move(__out_it), __padding.__before_, __specs.__fill_);
  __out_it                        = __formatter::__transform(__first, __last, std::move(__out_it), __op);
  return __formatter::__fill(std::move(__out_it), __padding.__after_, __specs.__fill_);
}

/// Writes a string using format's width estimation algorithm.
///
/// \pre !__specs.__has_precision()
///
/// \note When \c _LIBCPP_HAS_NO_UNICODE is defined the function assumes the
/// input is ASCII.
template <class _CharT>
_LIBCPP_HIDE_FROM_ABI auto __write_string_no_precision(
    basic_string_view<_CharT> __str,
    output_iterator<const _CharT&> auto __out_it,
    __format_spec::__parsed_specifications<_CharT> __specs) -> decltype(__out_it) {
  _LIBCPP_ASSERT_INTERNAL(!__specs.__has_precision(), "use __write_string");

  // No padding -> copy the string
  if (!__specs.__has_width())
    return __formatter::__copy(__str, std::move(__out_it));

  // Note when the estimated width is larger than size there's no padding. So
  // there's no reason to get the real size when the estimate is larger than or
  // equal to the minimum field width.
  size_t __size =
      __format_spec::__estimate_column_width(__str, __specs.__width_, __format_spec::__column_width_rounding::__up)
          .__width_;
  return __formatter::__write(__str, std::move(__out_it), __specs, __size);
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI int __truncate(basic_string_view<_CharT>& __str, int __precision) {
  __format_spec::__column_width_result __result =
      __format_spec::__estimate_column_width(__str, __precision, __format_spec::__column_width_rounding::__down);
  __str = basic_string_view<_CharT>{__str.begin(), __result.__last_};
  return __result.__width_;
}

} // namespace __formatter

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FORMAT_FORMATTER_OUTPUT_H
