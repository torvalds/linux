// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMATTER_INTEGER_H
#define _LIBCPP___FORMAT_FORMATTER_INTEGER_H

#include <__concepts/arithmetic.h>
#include <__config>
#include <__format/concepts.h>
#include <__format/format_parse_context.h>
#include <__format/formatter.h>
#include <__format/formatter_integral.h>
#include <__format/formatter_output.h>
#include <__format/parser_std_format_spec.h>
#include <__type_traits/is_void.h>
#include <__type_traits/make_32_64_or_128_bit.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS __formatter_integer {
public:
  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    typename _ParseContext::iterator __result = __parser_.__parse(__ctx, __format_spec::__fields_integral);
    __format_spec::__process_parsed_integer(__parser_, "an integer");
    return __result;
  }

  template <integral _Tp, class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator format(_Tp __value, _FormatContext& __ctx) const {
    __format_spec::__parsed_specifications<_CharT> __specs = __parser_.__get_parsed_std_specifications(__ctx);

    if (__specs.__std_.__type_ == __format_spec::__type::__char)
      return __formatter::__format_char(__value, __ctx.out(), __specs);

    using _Type = __make_32_64_or_128_bit_t<_Tp>;
    static_assert(!is_void<_Type>::value, "unsupported integral type used in __formatter_integer::__format");

    // Reduce the number of instantiation of the integer formatter
    return __formatter::__format_integer(static_cast<_Type>(__value), __ctx, __specs);
  }

  __format_spec::__parser<_CharT> __parser_;
};

// Signed integral types.
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<signed char, _CharT> : public __formatter_integer<_CharT> {};
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<short, _CharT> : public __formatter_integer<_CharT> {};
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<int, _CharT> : public __formatter_integer<_CharT> {};
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<long, _CharT> : public __formatter_integer<_CharT> {};
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<long long, _CharT> : public __formatter_integer<_CharT> {};
#  ifndef _LIBCPP_HAS_NO_INT128
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<__int128_t, _CharT> : public __formatter_integer<_CharT> {};
#  endif

// Unsigned integral types.
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<unsigned char, _CharT> : public __formatter_integer<_CharT> {};
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<unsigned short, _CharT> : public __formatter_integer<_CharT> {};
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<unsigned, _CharT> : public __formatter_integer<_CharT> {};
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<unsigned long, _CharT> : public __formatter_integer<_CharT> {};
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<unsigned long long, _CharT> : public __formatter_integer<_CharT> {};
#  ifndef _LIBCPP_HAS_NO_INT128
template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<__uint128_t, _CharT> : public __formatter_integer<_CharT> {};
#  endif

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_FORMATTER_INTEGER_H
