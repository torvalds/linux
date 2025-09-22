// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMATTER_BOOL_H
#define _LIBCPP___FORMAT_FORMATTER_BOOL_H

#include <__algorithm/copy.h>
#include <__assert>
#include <__config>
#include <__format/concepts.h>
#include <__format/format_parse_context.h>
#include <__format/formatter.h>
#include <__format/formatter_integral.h>
#include <__format/parser_std_format_spec.h>
#include <__utility/unreachable.h>

#ifndef _LIBCPP_HAS_NO_LOCALIZATION
#  include <__locale>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<bool, _CharT> {
public:
  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    typename _ParseContext::iterator __result = __parser_.__parse(__ctx, __format_spec::__fields_integral);
    __format_spec::__process_parsed_bool(__parser_, "a bool");
    return __result;
  }

  template <class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator format(bool __value, _FormatContext& __ctx) const {
    switch (__parser_.__type_) {
    case __format_spec::__type::__default:
    case __format_spec::__type::__string:
      return __formatter::__format_bool(__value, __ctx, __parser_.__get_parsed_std_specifications(__ctx));

    case __format_spec::__type::__binary_lower_case:
    case __format_spec::__type::__binary_upper_case:
    case __format_spec::__type::__octal:
    case __format_spec::__type::__decimal:
    case __format_spec::__type::__hexadecimal_lower_case:
    case __format_spec::__type::__hexadecimal_upper_case:
      // Promotes bool to an integral type. This reduces the number of
      // instantiations of __format_integer reducing code size.
      return __formatter::__format_integer(
          static_cast<unsigned>(__value), __ctx, __parser_.__get_parsed_std_specifications(__ctx));

    default:
      _LIBCPP_ASSERT_INTERNAL(false, "The parse function should have validated the type");
      __libcpp_unreachable();
    }
  }

  __format_spec::__parser<_CharT> __parser_;
};

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_FORMATTER_BOOL_H
