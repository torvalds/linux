// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMATTER_TUPLE_H
#define _LIBCPP___FORMAT_FORMATTER_TUPLE_H

#include <__algorithm/ranges_copy.h>
#include <__chrono/statically_widen.h>
#include <__config>
#include <__format/buffer.h>
#include <__format/concepts.h>
#include <__format/format_context.h>
#include <__format/format_error.h>
#include <__format/format_parse_context.h>
#include <__format/formatter.h>
#include <__format/formatter_output.h>
#include <__format/parser_std_format_spec.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/integer_sequence.h>
#include <__utility/pair.h>
#include <string_view>
#include <tuple>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

template <__fmt_char_type _CharT, class _Tuple, formattable<_CharT>... _Args>
struct _LIBCPP_TEMPLATE_VIS __formatter_tuple {
  _LIBCPP_HIDE_FROM_ABI constexpr void set_separator(basic_string_view<_CharT> __separator) noexcept {
    __separator_ = __separator;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr void
  set_brackets(basic_string_view<_CharT> __opening_bracket, basic_string_view<_CharT> __closing_bracket) noexcept {
    __opening_bracket_ = __opening_bracket;
    __closing_bracket_ = __closing_bracket;
  }

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    auto __begin = __parser_.__parse(__ctx, __format_spec::__fields_tuple);

    auto __end = __ctx.end();
    // Note 'n' is part of the type here
    if (__parser_.__clear_brackets_)
      set_brackets({}, {});
    else if (__begin != __end && *__begin == _CharT('m')) {
      if constexpr (sizeof...(_Args) == 2) {
        set_separator(_LIBCPP_STATICALLY_WIDEN(_CharT, ": "));
        set_brackets({}, {});
        ++__begin;
      } else
        std::__throw_format_error("Type m requires a pair or a tuple with two elements");
    }

    if (__begin != __end && *__begin != _CharT('}'))
      std::__throw_format_error("The format specifier should consume the input or end with a '}'");

    __ctx.advance_to(__begin);

    // [format.tuple]/7
    //   ... For each element e in underlying_, if e.set_debug_format()
    //   is a valid expression, calls e.set_debug_format().
    std::__for_each_index_sequence(make_index_sequence<sizeof...(_Args)>(), [&]<size_t _Index> {
      auto& __formatter = std::get<_Index>(__underlying_);
      __formatter.parse(__ctx);
      // Unlike the range_formatter we don't guard against evil parsers. Since
      // this format-spec never has a format-spec for the underlying type
      // adding the test would give additional overhead.
      std::__set_debug_format(__formatter);
    });

    return __begin;
  }

  template <class _FormatContext>
  typename _FormatContext::iterator _LIBCPP_HIDE_FROM_ABI
  format(conditional_t<(formattable<const _Args, _CharT> && ...), const _Tuple&, _Tuple&> __tuple,
         _FormatContext& __ctx) const {
    __format_spec::__parsed_specifications<_CharT> __specs = __parser_.__get_parsed_std_specifications(__ctx);

    if (!__specs.__has_width())
      return __format_tuple(__tuple, __ctx);

    // The size of the buffer needed is:
    // - open bracket characters
    // - close bracket character
    // - n elements where every element may have a different size
    // - (n -1) separators
    // The size of the element is hard to predict, knowing the type helps but
    // it depends on the format-spec. As an initial estimate we guess 6
    // characters.
    // Typically both brackets are 1 character and the separator is 2
    // characters. Which means there will be
    //   (n - 1) * 2 + 1 + 1 = n * 2 character
    // So estimate 8 times the range size as buffer.
    __format::__retarget_buffer<_CharT> __buffer{8 * tuple_size_v<_Tuple>};
    basic_format_context<typename __format::__retarget_buffer<_CharT>::__iterator, _CharT> __c{
        __buffer.__make_output_iterator(), __ctx};

    __format_tuple(__tuple, __c);

    return __formatter::__write_string_no_precision(basic_string_view{__buffer.__view()}, __ctx.out(), __specs);
  }

  template <class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator __format_tuple(auto&& __tuple, _FormatContext& __ctx) const {
    __ctx.advance_to(std::ranges::copy(__opening_bracket_, __ctx.out()).out);

    std::__for_each_index_sequence(make_index_sequence<sizeof...(_Args)>(), [&]<size_t _Index> {
      if constexpr (_Index)
        __ctx.advance_to(std::ranges::copy(__separator_, __ctx.out()).out);
      __ctx.advance_to(std::get<_Index>(__underlying_).format(std::get<_Index>(__tuple), __ctx));
    });

    return std::ranges::copy(__closing_bracket_, __ctx.out()).out;
  }

  __format_spec::__parser<_CharT> __parser_{.__alignment_ = __format_spec::__alignment::__left};

private:
  tuple<formatter<remove_cvref_t<_Args>, _CharT>...> __underlying_;
  basic_string_view<_CharT> __separator_       = _LIBCPP_STATICALLY_WIDEN(_CharT, ", ");
  basic_string_view<_CharT> __opening_bracket_ = _LIBCPP_STATICALLY_WIDEN(_CharT, "(");
  basic_string_view<_CharT> __closing_bracket_ = _LIBCPP_STATICALLY_WIDEN(_CharT, ")");
};

template <__fmt_char_type _CharT, formattable<_CharT>... _Args>
struct _LIBCPP_TEMPLATE_VIS formatter<pair<_Args...>, _CharT>
    : public __formatter_tuple<_CharT, pair<_Args...>, _Args...> {};

template <__fmt_char_type _CharT, formattable<_CharT>... _Args>
struct _LIBCPP_TEMPLATE_VIS formatter<tuple<_Args...>, _CharT>
    : public __formatter_tuple<_CharT, tuple<_Args...>, _Args...> {};

#endif //_LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_FORMATTER_TUPLE_H
