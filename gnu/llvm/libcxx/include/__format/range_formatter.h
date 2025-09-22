// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_RANGE_FORMATTER_H
#define _LIBCPP___FORMAT_RANGE_FORMATTER_H

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#include <__algorithm/ranges_copy.h>
#include <__chrono/statically_widen.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__format/buffer.h>
#include <__format/concepts.h>
#include <__format/format_context.h>
#include <__format/format_error.h>
#include <__format/formatter.h>
#include <__format/formatter_output.h>
#include <__format/parser_std_format_spec.h>
#include <__iterator/back_insert_iterator.h>
#include <__ranges/concepts.h>
#include <__ranges/data.h>
#include <__ranges/from_range.h>
#include <__ranges/size.h>
#include <__type_traits/remove_cvref.h>
#include <string_view>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

template <class _Tp, class _CharT = char>
  requires same_as<remove_cvref_t<_Tp>, _Tp> && formattable<_Tp, _CharT>
struct _LIBCPP_TEMPLATE_VIS range_formatter {
  _LIBCPP_HIDE_FROM_ABI constexpr void set_separator(basic_string_view<_CharT> __separator) noexcept {
    __separator_ = __separator;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr void
  set_brackets(basic_string_view<_CharT> __opening_bracket, basic_string_view<_CharT> __closing_bracket) noexcept {
    __opening_bracket_ = __opening_bracket;
    __closing_bracket_ = __closing_bracket;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr formatter<_Tp, _CharT>& underlying() noexcept { return __underlying_; }
  _LIBCPP_HIDE_FROM_ABI constexpr const formatter<_Tp, _CharT>& underlying() const noexcept { return __underlying_; }

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    auto __begin = __parser_.__parse(__ctx, __format_spec::__fields_range);
    auto __end   = __ctx.end();
    // Note the cases where __begin == __end in this code only happens when the
    // replacement-field has no terminating }, or when the parse is manually
    // called with a format-spec. The former is an error and the latter means
    // using a formatter without the format functions or print.
    if (__begin == __end) [[unlikely]]
      return __parse_empty_range_underlying_spec(__ctx, __begin);

    // The n field overrides a possible m type, therefore delay applying the
    // effect of n until the type has been procesed.
    __parse_type(__begin, __end);
    if (__parser_.__clear_brackets_)
      set_brackets({}, {});
    if (__begin == __end) [[unlikely]]
      return __parse_empty_range_underlying_spec(__ctx, __begin);

    bool __has_range_underlying_spec = *__begin == _CharT(':');
    if (__has_range_underlying_spec) {
      // range-underlying-spec:
      //   :  format-spec
      ++__begin;
    } else if (__begin != __end && *__begin != _CharT('}'))
      // When there is no underlaying range the current parse should have
      // consumed the format-spec. If not, the not consumed input will be
      // processed by the underlying. For example {:-} for a range in invalid,
      // the sign field is not present. Without this check the underlying_ will
      // get -} as input which my be valid.
      std::__throw_format_error("The format specifier should consume the input or end with a '}'");

    __ctx.advance_to(__begin);
    __begin = __underlying_.parse(__ctx);

    // This test should not be required if __has_range_underlying_spec is false.
    // However this test makes sure the underlying formatter left the parser in
    // a valid state. (Note this is not a full protection against evil parsers.
    // For example
    //   } this is test for the next argument {}
    //   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^
    // could consume more than it should.
    if (__begin != __end && *__begin != _CharT('}'))
      std::__throw_format_error("The format specifier should consume the input or end with a '}'");

    if (__parser_.__type_ != __format_spec::__type::__default) {
      // [format.range.formatter]/6
      //   If the range-type is s or ?s, then there shall be no n option and no
      //   range-underlying-spec.
      if (__parser_.__clear_brackets_) {
        if (__parser_.__type_ == __format_spec::__type::__string)
          std::__throw_format_error("The n option and type s can't be used together");
        std::__throw_format_error("The n option and type ?s can't be used together");
      }
      if (__has_range_underlying_spec) {
        if (__parser_.__type_ == __format_spec::__type::__string)
          std::__throw_format_error("Type s and an underlying format specification can't be used together");
        std::__throw_format_error("Type ?s and an underlying format specification can't be used together");
      }
    } else if (!__has_range_underlying_spec)
      std::__set_debug_format(__underlying_);

    return __begin;
  }

  template <ranges::input_range _Rp, class _FormatContext>
    requires formattable<ranges::range_reference_t<_Rp>, _CharT> &&
             same_as<remove_cvref_t<ranges::range_reference_t<_Rp>>, _Tp>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator format(_Rp&& __range, _FormatContext& __ctx) const {
    __format_spec::__parsed_specifications<_CharT> __specs = __parser_.__get_parsed_std_specifications(__ctx);

    if (!__specs.__has_width())
      return __format_range(__range, __ctx, __specs);

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
    std::size_t __capacity_hint = 0;
    if constexpr (std::ranges::sized_range<_Rp>)
      __capacity_hint = 8 * ranges::size(__range);
    __format::__retarget_buffer<_CharT> __buffer{__capacity_hint};
    basic_format_context<typename __format::__retarget_buffer<_CharT>::__iterator, _CharT> __c{
        __buffer.__make_output_iterator(), __ctx};

    __format_range(__range, __c, __specs);

    return __formatter::__write_string_no_precision(__buffer.__view(), __ctx.out(), __specs);
  }

  template <ranges::input_range _Rp, class _FormatContext>
  typename _FormatContext::iterator _LIBCPP_HIDE_FROM_ABI
  __format_range(_Rp&& __range, _FormatContext& __ctx, __format_spec::__parsed_specifications<_CharT> __specs) const {
    if constexpr (same_as<_Tp, _CharT>) {
      switch (__specs.__std_.__type_) {
      case __format_spec::__type::__string:
      case __format_spec::__type::__debug:
        return __format_as_string(__range, __ctx, __specs.__std_.__type_ == __format_spec::__type::__debug);
      default:
        return __format_as_sequence(__range, __ctx);
      }
    } else
      return __format_as_sequence(__range, __ctx);
  }

  template <ranges::input_range _Rp, class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator
  __format_as_string(_Rp&& __range, _FormatContext& __ctx, bool __debug_format) const {
    // When the range is contiguous use a basic_string_view instead to avoid a
    // copy of the underlying data. The basic_string_view formatter
    // specialization is the "basic" string formatter in libc++.
    if constexpr (ranges::contiguous_range<_Rp> && std::ranges::sized_range<_Rp>) {
      std::formatter<basic_string_view<_CharT>, _CharT> __formatter;
      if (__debug_format)
        __formatter.set_debug_format();
      return __formatter.format(
          basic_string_view<_CharT>{
              ranges::data(__range),
              ranges::size(__range),
          },
          __ctx);
    } else {
      std::formatter<basic_string<_CharT>, _CharT> __formatter;
      if (__debug_format)
        __formatter.set_debug_format();
      return __formatter.format(basic_string<_CharT>{from_range, __range}, __ctx);
    }
  }

  template <ranges::input_range _Rp, class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator
  __format_as_sequence(_Rp&& __range, _FormatContext& __ctx) const {
    __ctx.advance_to(ranges::copy(__opening_bracket_, __ctx.out()).out);
    bool __use_separator = false;
    for (auto&& __e : __range) {
      if (__use_separator)
        __ctx.advance_to(ranges::copy(__separator_, __ctx.out()).out);
      else
        __use_separator = true;

      __ctx.advance_to(__underlying_.format(__e, __ctx));
    }

    return ranges::copy(__closing_bracket_, __ctx.out()).out;
  }

  __format_spec::__parser<_CharT> __parser_{.__alignment_ = __format_spec::__alignment::__left};

private:
  template <contiguous_iterator _Iterator>
  _LIBCPP_HIDE_FROM_ABI constexpr void __parse_type(_Iterator& __begin, _Iterator __end) {
    switch (*__begin) {
    case _CharT('m'):
      if constexpr (__fmt_pair_like<_Tp>) {
        set_brackets(_LIBCPP_STATICALLY_WIDEN(_CharT, "{"), _LIBCPP_STATICALLY_WIDEN(_CharT, "}"));
        set_separator(_LIBCPP_STATICALLY_WIDEN(_CharT, ", "));
        ++__begin;
      } else
        std::__throw_format_error("Type m requires a pair or a tuple with two elements");
      break;

    case _CharT('s'):
      if constexpr (same_as<_Tp, _CharT>) {
        __parser_.__type_ = __format_spec::__type::__string;
        ++__begin;
      } else
        std::__throw_format_error("Type s requires character type as formatting argument");
      break;

    case _CharT('?'):
      ++__begin;
      if (__begin == __end || *__begin != _CharT('s'))
        std::__throw_format_error("The format specifier should consume the input or end with a '}'");
      if constexpr (same_as<_Tp, _CharT>) {
        __parser_.__type_ = __format_spec::__type::__debug;
        ++__begin;
      } else
        std::__throw_format_error("Type ?s requires character type as formatting argument");
    }
  }

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator
  __parse_empty_range_underlying_spec(_ParseContext& __ctx, typename _ParseContext::iterator __begin) {
    __ctx.advance_to(__begin);
    [[maybe_unused]] typename _ParseContext::iterator __result = __underlying_.parse(__ctx);
    _LIBCPP_ASSERT_INTERNAL(__result == __begin,
                            "the underlying's parse function should not advance the input beyond the end of the input");
    return __begin;
  }

  formatter<_Tp, _CharT> __underlying_;
  basic_string_view<_CharT> __separator_       = _LIBCPP_STATICALLY_WIDEN(_CharT, ", ");
  basic_string_view<_CharT> __opening_bracket_ = _LIBCPP_STATICALLY_WIDEN(_CharT, "[");
  basic_string_view<_CharT> __closing_bracket_ = _LIBCPP_STATICALLY_WIDEN(_CharT, "]");
};

#endif //_LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_RANGE_FORMATTER_H
