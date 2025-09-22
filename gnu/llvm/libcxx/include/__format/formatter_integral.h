// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMATTER_INTEGRAL_H
#define _LIBCPP___FORMAT_FORMATTER_INTEGRAL_H

#include <__charconv/to_chars_integral.h>
#include <__charconv/to_chars_result.h>
#include <__charconv/traits.h>
#include <__concepts/arithmetic.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__format/concepts.h>
#include <__format/format_error.h>
#include <__format/formatter_output.h>
#include <__format/parser_std_format_spec.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__memory/pointer_traits.h>
#include <__system_error/errc.h>
#include <__type_traits/make_unsigned.h>
#include <__utility/unreachable.h>
#include <array>
#include <limits>
#include <string>
#include <string_view>

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

//
// Generic
//

template <contiguous_iterator _Iterator>
  requires same_as<char, iter_value_t<_Iterator>>
_LIBCPP_HIDE_FROM_ABI inline _Iterator __insert_sign(_Iterator __buf, bool __negative, __format_spec::__sign __sign) {
  if (__negative)
    *__buf++ = '-';
  else
    switch (__sign) {
    case __format_spec::__sign::__default:
    case __format_spec::__sign::__minus:
      // No sign added.
      break;
    case __format_spec::__sign::__plus:
      *__buf++ = '+';
      break;
    case __format_spec::__sign::__space:
      *__buf++ = ' ';
      break;
    }

  return __buf;
}

/**
 * Determines the required grouping based on the size of the input.
 *
 * The grouping's last element will be repeated. For simplicity this repeating
 * is unwrapped based on the length of the input. (When the input is short some
 * groups are not processed.)
 *
 * @returns The size of the groups to write. This means the number of
 * separator characters written is size() - 1.
 *
 * @note Since zero-sized groups cause issues they are silently ignored.
 *
 * @note The grouping field of the locale is always a @c std::string,
 * regardless whether the @c std::numpunct's type is @c char or @c wchar_t.
 */
_LIBCPP_HIDE_FROM_ABI inline string __determine_grouping(ptrdiff_t __size, const string& __grouping) {
  _LIBCPP_ASSERT_INTERNAL(!__grouping.empty() && __size > __grouping[0],
                          "The slow grouping formatting is used while there will be no separators written");
  string __r;
  auto __end = __grouping.end() - 1;
  auto __ptr = __grouping.begin();

  while (true) {
    __size -= *__ptr;
    if (__size > 0)
      __r.push_back(*__ptr);
    else {
      // __size <= 0 so the value pushed will be <= *__ptr.
      __r.push_back(*__ptr + __size);
      return __r;
    }

    // Proceed to the next group.
    if (__ptr != __end) {
      do {
        ++__ptr;
        // Skip grouping with a width of 0.
      } while (*__ptr == 0 && __ptr != __end);
    }
  }

  __libcpp_unreachable();
}

//
// Char
//

template <__fmt_char_type _CharT>
_LIBCPP_HIDE_FROM_ABI auto
__format_char(integral auto __value,
              output_iterator<const _CharT&> auto __out_it,
              __format_spec::__parsed_specifications<_CharT> __specs) -> decltype(__out_it) {
  using _Tp = decltype(__value);
  if constexpr (!same_as<_CharT, _Tp>) {
    // cmp_less and cmp_greater can't be used for character types.
    if constexpr (signed_integral<_CharT> == signed_integral<_Tp>) {
      if (__value < numeric_limits<_CharT>::min() || __value > numeric_limits<_CharT>::max())
        std::__throw_format_error("Integral value outside the range of the char type");
    } else if constexpr (signed_integral<_CharT>) {
      // _CharT is signed _Tp is unsigned
      if (__value > static_cast<make_unsigned_t<_CharT>>(numeric_limits<_CharT>::max()))
        std::__throw_format_error("Integral value outside the range of the char type");
    } else {
      // _CharT is unsigned _Tp is signed
      if (__value < 0 || static_cast<make_unsigned_t<_Tp>>(__value) > numeric_limits<_CharT>::max())
        std::__throw_format_error("Integral value outside the range of the char type");
    }
  }

  const auto __c = static_cast<_CharT>(__value);
  return __formatter::__write(std::addressof(__c), std::addressof(__c) + 1, std::move(__out_it), __specs);
}

//
// Integer
//

/** Wrapper around @ref to_chars, returning the output iterator. */
template <contiguous_iterator _Iterator, integral _Tp>
  requires same_as<char, iter_value_t<_Iterator>>
_LIBCPP_HIDE_FROM_ABI _Iterator __to_buffer(_Iterator __first, _Iterator __last, _Tp __value, int __base) {
  // TODO FMT Evaluate code overhead due to not calling the internal function
  // directly. (Should be zero overhead.)
  to_chars_result __r = std::to_chars(std::to_address(__first), std::to_address(__last), __value, __base);
  _LIBCPP_ASSERT_INTERNAL(__r.ec == errc(0), "Internal buffer too small");
  auto __diff = __r.ptr - std::to_address(__first);
  return __first + __diff;
}

/**
 * Helper to determine the buffer size to output a integer in Base @em x.
 *
 * There are several overloads for the supported bases. The function uses the
 * base as template argument so it can be used in a constant expression.
 */
template <unsigned_integral _Tp, size_t _Base>
consteval size_t __buffer_size() noexcept
  requires(_Base == 2)
{
  return numeric_limits<_Tp>::digits // The number of binary digits.
       + 2                           // Reserve space for the '0[Bb]' prefix.
       + 1;                          // Reserve space for the sign.
}

template <unsigned_integral _Tp, size_t _Base>
consteval size_t __buffer_size() noexcept
  requires(_Base == 8)
{
  return numeric_limits<_Tp>::digits // The number of binary digits.
           / 3                       // Adjust to octal.
       + 1                           // Turn floor to ceil.
       + 1                           // Reserve space for the '0' prefix.
       + 1;                          // Reserve space for the sign.
}

template <unsigned_integral _Tp, size_t _Base>
consteval size_t __buffer_size() noexcept
  requires(_Base == 10)
{
  return numeric_limits<_Tp>::digits10 // The floored value.
       + 1                             // Turn floor to ceil.
       + 1;                            // Reserve space for the sign.
}

template <unsigned_integral _Tp, size_t _Base>
consteval size_t __buffer_size() noexcept
  requires(_Base == 16)
{
  return numeric_limits<_Tp>::digits // The number of binary digits.
           / 4                       // Adjust to hexadecimal.
       + 2                           // Reserve space for the '0[Xx]' prefix.
       + 1;                          // Reserve space for the sign.
}

template <class _OutIt, contiguous_iterator _Iterator, class _CharT>
  requires same_as<char, iter_value_t<_Iterator>>
_LIBCPP_HIDE_FROM_ABI _OutIt __write_using_decimal_separators(
    _OutIt __out_it,
    _Iterator __begin,
    _Iterator __first,
    _Iterator __last,
    string&& __grouping,
    _CharT __sep,
    __format_spec::__parsed_specifications<_CharT> __specs) {
  int __size = (__first - __begin) +    // [sign][prefix]
               (__last - __first) +     // data
               (__grouping.size() - 1); // number of separator characters

  __padding_size_result __padding = {0, 0};
  if (__specs.__alignment_ == __format_spec::__alignment::__zero_padding) {
    // Write [sign][prefix].
    __out_it = __formatter::__copy(__begin, __first, std::move(__out_it));

    if (__specs.__width_ > __size) {
      // Write zero padding.
      __padding.__before_ = __specs.__width_ - __size;
      __out_it            = __formatter::__fill(std::move(__out_it), __specs.__width_ - __size, _CharT('0'));
    }
  } else {
    if (__specs.__width_ > __size) {
      // Determine padding and write padding.
      __padding = __formatter::__padding_size(__size, __specs.__width_, __specs.__alignment_);

      __out_it = __formatter::__fill(std::move(__out_it), __padding.__before_, __specs.__fill_);
    }
    // Write [sign][prefix].
    __out_it = __formatter::__copy(__begin, __first, std::move(__out_it));
  }

  auto __r = __grouping.rbegin();
  auto __e = __grouping.rend() - 1;
  _LIBCPP_ASSERT_INTERNAL(
      __r != __e, "The slow grouping formatting is used while there will be no separators written.");
  // The output is divided in small groups of numbers to write:
  // - A group before the first separator.
  // - A separator and a group, repeated for the number of separators.
  // - A group after the last separator.
  // This loop achieves that process by testing the termination condition
  // midway in the loop.
  //
  // TODO FMT This loop evaluates the loop invariant `__parser.__type !=
  // _Flags::_Type::__hexadecimal_upper_case` for every iteration. (This test
  // happens in the __write call.) Benchmark whether making two loops and
  // hoisting the invariant is worth the effort.
  while (true) {
    if (__specs.__std_.__type_ == __format_spec::__type::__hexadecimal_upper_case) {
      __last   = __first + *__r;
      __out_it = __formatter::__transform(__first, __last, std::move(__out_it), __hex_to_upper);
      __first  = __last;
    } else {
      __out_it = __formatter::__copy(__first, *__r, std::move(__out_it));
      __first += *__r;
    }

    if (__r == __e)
      break;

    ++__r;
    *__out_it++ = __sep;
  }

  return __formatter::__fill(std::move(__out_it), __padding.__after_, __specs.__fill_);
}

template <unsigned_integral _Tp, contiguous_iterator _Iterator, class _CharT, class _FormatContext>
  requires same_as<char, iter_value_t<_Iterator>>
_LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator __format_integer(
    _Tp __value,
    _FormatContext& __ctx,
    __format_spec::__parsed_specifications<_CharT> __specs,
    bool __negative,
    _Iterator __begin,
    _Iterator __end,
    const char* __prefix,
    int __base) {
  _Iterator __first = __formatter::__insert_sign(__begin, __negative, __specs.__std_.__sign_);
  if (__specs.__std_.__alternate_form_ && __prefix)
    while (*__prefix)
      *__first++ = *__prefix++;

  _Iterator __last = __formatter::__to_buffer(__first, __end, __value, __base);

#  ifndef _LIBCPP_HAS_NO_LOCALIZATION
  if (__specs.__std_.__locale_specific_form_) {
    const auto& __np  = std::use_facet<numpunct<_CharT>>(__ctx.locale());
    string __grouping = __np.grouping();
    ptrdiff_t __size  = __last - __first;
    // Writing the grouped form has more overhead than the normal output
    // routines. If there will be no separators written the locale-specific
    // form is identical to the normal routine. Test whether to grouped form
    // is required.
    if (!__grouping.empty() && __size > __grouping[0])
      return __formatter::__write_using_decimal_separators(
          __ctx.out(),
          __begin,
          __first,
          __last,
          __formatter::__determine_grouping(__size, __grouping),
          __np.thousands_sep(),
          __specs);
  }
#  endif
  auto __out_it = __ctx.out();
  if (__specs.__alignment_ != __format_spec::__alignment::__zero_padding)
    __first = __begin;
  else {
    // __buf contains [sign][prefix]data
    //                              ^ location of __first
    // The zero padding is done like:
    // - Write [sign][prefix]
    // - Write data right aligned with '0' as fill character.
    __out_it                  = __formatter::__copy(__begin, __first, std::move(__out_it));
    __specs.__alignment_      = __format_spec::__alignment::__right;
    __specs.__fill_.__data[0] = _CharT('0');
    int32_t __size            = __first - __begin;

    __specs.__width_ -= std::min(__size, __specs.__width_);
  }

  if (__specs.__std_.__type_ != __format_spec::__type::__hexadecimal_upper_case) [[likely]]
    return __formatter::__write(__first, __last, __ctx.out(), __specs);

  return __formatter::__write_transformed(__first, __last, __ctx.out(), __specs, __formatter::__hex_to_upper);
}

template <unsigned_integral _Tp, class _CharT, class _FormatContext>
_LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator
__format_integer(_Tp __value,
                 _FormatContext& __ctx,
                 __format_spec::__parsed_specifications<_CharT> __specs,
                 bool __negative = false) {
  switch (__specs.__std_.__type_) {
  case __format_spec::__type::__binary_lower_case: {
    array<char, __formatter::__buffer_size<decltype(__value), 2>()> __array;
    return __formatter::__format_integer(__value, __ctx, __specs, __negative, __array.begin(), __array.end(), "0b", 2);
  }
  case __format_spec::__type::__binary_upper_case: {
    array<char, __formatter::__buffer_size<decltype(__value), 2>()> __array;
    return __formatter::__format_integer(__value, __ctx, __specs, __negative, __array.begin(), __array.end(), "0B", 2);
  }
  case __format_spec::__type::__octal: {
    // Octal is special; if __value == 0 there's no prefix.
    array<char, __formatter::__buffer_size<decltype(__value), 8>()> __array;
    return __formatter::__format_integer(
        __value, __ctx, __specs, __negative, __array.begin(), __array.end(), __value != 0 ? "0" : nullptr, 8);
  }
  case __format_spec::__type::__default:
  case __format_spec::__type::__decimal: {
    array<char, __formatter::__buffer_size<decltype(__value), 10>()> __array;
    return __formatter::__format_integer(
        __value, __ctx, __specs, __negative, __array.begin(), __array.end(), nullptr, 10);
  }
  case __format_spec::__type::__hexadecimal_lower_case: {
    array<char, __formatter::__buffer_size<decltype(__value), 16>()> __array;
    return __formatter::__format_integer(__value, __ctx, __specs, __negative, __array.begin(), __array.end(), "0x", 16);
  }
  case __format_spec::__type::__hexadecimal_upper_case: {
    array<char, __formatter::__buffer_size<decltype(__value), 16>()> __array;
    return __formatter::__format_integer(__value, __ctx, __specs, __negative, __array.begin(), __array.end(), "0X", 16);
  }
  default:
    _LIBCPP_ASSERT_INTERNAL(false, "The parse function should have validated the type");
    __libcpp_unreachable();
  }
}

template <signed_integral _Tp, class _CharT, class _FormatContext>
_LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator
__format_integer(_Tp __value, _FormatContext& __ctx, __format_spec::__parsed_specifications<_CharT> __specs) {
  // Depending on the std-format-spec string the sign and the value
  // might not be outputted together:
  // - alternate form may insert a prefix string.
  // - zero-padding may insert additional '0' characters.
  // Therefore the value is processed as a positive unsigned value.
  // The function @ref __insert_sign will a '-' when the value was negative.
  auto __r        = std::__to_unsigned_like(__value);
  bool __negative = __value < 0;
  if (__negative)
    __r = std::__complement(__r);

  return __formatter::__format_integer(__r, __ctx, __specs, __negative);
}

//
// Formatter arithmetic (bool)
//

template <class _CharT>
struct _LIBCPP_TEMPLATE_VIS __bool_strings;

template <>
struct _LIBCPP_TEMPLATE_VIS __bool_strings<char> {
  static constexpr string_view __true{"true"};
  static constexpr string_view __false{"false"};
};

#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
struct _LIBCPP_TEMPLATE_VIS __bool_strings<wchar_t> {
  static constexpr wstring_view __true{L"true"};
  static constexpr wstring_view __false{L"false"};
};
#  endif

template <class _CharT, class _FormatContext>
_LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator
__format_bool(bool __value, _FormatContext& __ctx, __format_spec::__parsed_specifications<_CharT> __specs) {
#  ifndef _LIBCPP_HAS_NO_LOCALIZATION
  if (__specs.__std_.__locale_specific_form_) {
    const auto& __np           = std::use_facet<numpunct<_CharT>>(__ctx.locale());
    basic_string<_CharT> __str = __value ? __np.truename() : __np.falsename();
    return __formatter::__write_string_no_precision(basic_string_view<_CharT>{__str}, __ctx.out(), __specs);
  }
#  endif
  basic_string_view<_CharT> __str =
      __value ? __formatter::__bool_strings<_CharT>::__true : __formatter::__bool_strings<_CharT>::__false;
  return __formatter::__write(__str.begin(), __str.end(), __ctx.out(), __specs);
}

} // namespace __formatter

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FORMAT_FORMATTER_INTEGRAL_H
