// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [char.traits], character traits
  using std::char_traits;

  // [basic.string], basic_string
  using std::basic_string;

  using std::operator+;
  using std::operator==;
  using std::operator<=>;

  // [string.special], swap
  using std::swap;

  // [string.io], inserters and extractors
  using std::operator>>;
  using std::operator<<;
  using std::getline;

  // [string.erasure], erasure
  using std::erase;
  using std::erase_if;

  // basic_string typedef-names
  using std::string;
  using std::u16string;
  using std::u32string;
#ifndef _LIBCPP_HAS_NO_CHAR8_T
  using std::u8string;
#endif
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wstring;
#endif

  // [string.conversions], numeric conversions
  using std::stod;
  using std::stof;
  using std::stoi;
  using std::stol;
  using std::stold;
  using std::stoll;
  using std::stoul;
  using std::stoull;
  using std::to_string;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::to_wstring;
#endif

  namespace pmr {
    using std::pmr::basic_string;
    using std::pmr::string;
    using std::pmr::u16string;
    using std::pmr::u32string;
#ifndef _LIBCPP_HAS_NO_CHAR8_T
    using std::pmr::u8string;
#endif
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    using std::pmr::wstring;
#endif
  } // namespace pmr

  // [basic.string.hash], hash support
  using std::hash;

  inline namespace literals {
    inline namespace string_literals {
      // [basic.string.literals], suffix for basic_string literals
      using std::literals::string_literals::operator""s;
    } // namespace string_literals
  } // namespace literals
} // namespace std
