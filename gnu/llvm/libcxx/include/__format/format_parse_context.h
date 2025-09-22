// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_FORMAT_PARSE_CONTEXT_H
#define _LIBCPP___FORMAT_FORMAT_PARSE_CONTEXT_H

#include <__config>
#include <__format/format_error.h>
#include <__type_traits/is_constant_evaluated.h>
#include <string_view>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class _CharT>
class _LIBCPP_TEMPLATE_VIS basic_format_parse_context {
public:
  using char_type      = _CharT;
  using const_iterator = typename basic_string_view<_CharT>::const_iterator;
  using iterator       = const_iterator;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit basic_format_parse_context(
      basic_string_view<_CharT> __fmt, size_t __num_args = 0) noexcept
      : __begin_(__fmt.begin()),
        __end_(__fmt.end()),
        __indexing_(__unknown),
        __next_arg_id_(0),
        __num_args_(__num_args) {}

  basic_format_parse_context(const basic_format_parse_context&)            = delete;
  basic_format_parse_context& operator=(const basic_format_parse_context&) = delete;

  _LIBCPP_HIDE_FROM_ABI constexpr const_iterator begin() const noexcept { return __begin_; }
  _LIBCPP_HIDE_FROM_ABI constexpr const_iterator end() const noexcept { return __end_; }
  _LIBCPP_HIDE_FROM_ABI constexpr void advance_to(const_iterator __it) { __begin_ = __it; }

  _LIBCPP_HIDE_FROM_ABI constexpr size_t next_arg_id() {
    if (__indexing_ == __manual)
      std::__throw_format_error("Using automatic argument numbering in manual argument numbering mode");

    if (__indexing_ == __unknown)
      __indexing_ = __automatic;

    // Throws an exception to make the expression a non core constant
    // expression as required by:
    // [format.parse.ctx]/8
    //   Remarks: Let cur-arg-id be the value of next_arg_id_ prior to this
    //   call. Call expressions where cur-arg-id >= num_args_ is true are not
    //   core constant expressions (7.7 [expr.const]).
    // Note: the Throws clause [format.parse.ctx]/9 doesn't specify the
    // behavior when id >= num_args_.
    if (is_constant_evaluated() && __next_arg_id_ >= __num_args_)
      std::__throw_format_error("Argument index outside the valid range");

    return __next_arg_id_++;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr void check_arg_id(size_t __id) {
    if (__indexing_ == __automatic)
      std::__throw_format_error("Using manual argument numbering in automatic argument numbering mode");

    if (__indexing_ == __unknown)
      __indexing_ = __manual;

    // Throws an exception to make the expression a non core constant
    // expression as required by:
    // [format.parse.ctx]/11
    //   Remarks: Call expressions where id >= num_args_ are not core constant
    //   expressions ([expr.const]).
    // Note: the Throws clause [format.parse.ctx]/10 doesn't specify the
    // behavior when id >= num_args_.
    if (is_constant_evaluated() && __id >= __num_args_)
      std::__throw_format_error("Argument index outside the valid range");
  }

private:
  iterator __begin_;
  iterator __end_;
  enum _Indexing { __unknown, __manual, __automatic };
  _Indexing __indexing_;
  size_t __next_arg_id_;
  size_t __num_args_;
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(basic_format_parse_context);

using format_parse_context = basic_format_parse_context<char>;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
using wformat_parse_context = basic_format_parse_context<wchar_t>;
#  endif

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FORMAT_FORMAT_PARSE_CONTEXT_H
