// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [string.view.template], class template basic_string_view
  using std::basic_string_view;

  namespace ranges {
    using std::ranges::enable_borrowed_range;
    using std::ranges::enable_view;
  } // namespace ranges

  // [string.view.comparison], non-member comparison functions
  using std::operator==;
  using std::operator<=>;

  // [string.view.io], inserters and extractors
  using std::operator<<;

  // basic_string_view typedef-names
  using std::string_view;
  using std::u16string_view;
  using std::u32string_view;
#ifndef _LIBCPP_HAS_NO_CHAR8_T
  using std::u8string_view;
#endif
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wstring_view;
#endif

  // [string.view.hash], hash support
  using std::hash;

  inline namespace literals {
    inline namespace string_view_literals {
      // [string.view.literals], suffix for basic_string_view literals
      using std::literals::string_view_literals::operator""sv;
    } // namespace string_view_literals
  }   // namespace literals
} // namespace std
