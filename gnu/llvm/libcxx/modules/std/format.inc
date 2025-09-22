// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [format.context], class template basic_format_context
  using std::basic_format_context;
  using std::format_context;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wformat_context;
#endif

  // [format.args], class template basic_format_args
  using std::basic_format_args;
  using std::format_args;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wformat_args;
#endif

  // [format.fmt.string], class template basic_format_string
  using std::basic_format_string;
  using std::format_string;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wformat_string;
#endif
#if _LIBCPP_STD_VER >= 26
  using std::runtime_format;
#endif //_LIBCPP_STD_VER >= 26

  // [format.functions], formatting functions
  using std::format;
  using std::format_to;
  using std::vformat;
  using std::vformat_to;

  using std::format_to_n;
  using std::format_to_n_result;
  using std::formatted_size;

  // [format.formatter], formatter
  using std::formatter;

#if _LIBCPP_STD_VER >= 23
  // [format.formattable], concept formattable
  using std::formattable;
#endif

  // [format.parse.ctx], class template basic_format_parse_context
  using std::basic_format_parse_context;
  using std::format_parse_context;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wformat_parse_context;
#endif

#if _LIBCPP_STD_VER >= 23
  // [format.range], formatting of ranges
  // [format.range.fmtkind], variable template format_kind
  using std::format_kind;
  using std::range_format;

  // [format.range.formatter], class template range_formatter
  using std::range_formatter;
#endif // _LIBCPP_STD_VER >= 23

  // [format.arg], class template basic_format_arg
  using std::basic_format_arg;
  using std::visit_format_arg;

  // [format.arg.store], class template format-arg-store
  using std::make_format_args;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::make_wformat_args;
#endif

  // [format.error], class format_error
  using std::format_error;
} // namespace std
