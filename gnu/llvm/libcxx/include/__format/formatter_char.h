// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMATTER_CHAR_H
#define _LIBCPP___FORMAT_FORMATTER_CHAR_H

#include <__concepts/same_as.h>
#include <__config>
#include <__format/concepts.h>
#include <__format/format_parse_context.h>
#include <__format/formatter.h>
#include <__format/formatter_integral.h>
#include <__format/formatter_output.h>
#include <__format/parser_std_format_spec.h>
#include <__format/write_escaped.h>
#include <__type_traits/conditional.h>
#include <__type_traits/make_unsigned.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS __formatter_char {
public:
  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    typename _ParseContext::iterator __result = __parser_.__parse(__ctx, __format_spec::__fields_integral);
    __format_spec::__process_parsed_char(__parser_, "a character");
    return __result;
  }

  template <class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator format(_CharT __value, _FormatContext& __ctx) const {
    if (__parser_.__type_ == __format_spec::__type::__default || __parser_.__type_ == __format_spec::__type::__char)
      return __formatter::__format_char(__value, __ctx.out(), __parser_.__get_parsed_std_specifications(__ctx));

#  if _LIBCPP_STD_VER >= 23
    if (__parser_.__type_ == __format_spec::__type::__debug)
      return __formatter::__format_escaped_char(__value, __ctx.out(), __parser_.__get_parsed_std_specifications(__ctx));
#  endif

    if constexpr (sizeof(_CharT) <= sizeof(unsigned))
      return __formatter::__format_integer(
          static_cast<unsigned>(static_cast<make_unsigned_t<_CharT>>(__value)),
          __ctx,
          __parser_.__get_parsed_std_specifications(__ctx));
    else
      return __formatter::__format_integer(
          static_cast<make_unsigned_t<_CharT>>(__value), __ctx, __parser_.__get_parsed_std_specifications(__ctx));
  }

  template <class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator format(char __value, _FormatContext& __ctx) const
    requires(same_as<_CharT, wchar_t>)
  {
    return format(static_cast<wchar_t>(static_cast<unsigned char>(__value)), __ctx);
  }

#  if _LIBCPP_STD_VER >= 23
  _LIBCPP_HIDE_FROM_ABI constexpr void set_debug_format() { __parser_.__type_ = __format_spec::__type::__debug; }
#  endif

  __format_spec::__parser<_CharT> __parser_;
};

template <>
struct _LIBCPP_TEMPLATE_VIS formatter<char, char> : public __formatter_char<char> {};

#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
struct _LIBCPP_TEMPLATE_VIS formatter<char, wchar_t> : public __formatter_char<wchar_t> {};

template <>
struct _LIBCPP_TEMPLATE_VIS formatter<wchar_t, wchar_t> : public __formatter_char<wchar_t> {};

#  endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_FORMATTER_CHAR_H
