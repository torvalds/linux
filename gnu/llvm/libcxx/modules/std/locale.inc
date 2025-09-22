// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#ifndef _LIBCPP_HAS_NO_LOCALIZATION
  // [locale], locale
  using std::has_facet;
  using std::locale;
  using std::use_facet;

  // [locale.convenience], convenience interfaces
  using std::isalnum;
  using std::isalpha;
  using std::isblank;
  using std::iscntrl;
  using std::isdigit;
  using std::isgraph;
  using std::islower;
  using std::isprint;
  using std::ispunct;
  using std::isspace;
  using std::isupper;
  using std::isxdigit;
  using std::tolower;
  using std::toupper;

  // [category.ctype], ctype
  using std::codecvt;
  using std::codecvt_base;
  using std::codecvt_byname;
  using std::ctype;
  using std::ctype_base;
  using std::ctype_byname;

  // [category.numeric], numeric
  using std::num_get;
  using std::num_put;
  using std::numpunct;
  using std::numpunct_byname;

  // [category.collate], collation
  using std::collate;
  using std::collate_byname;

  // [category.time], date and time
  using std::time_base;
  using std::time_get;
  using std::time_get_byname;
  using std::time_put;
  using std::time_put_byname;

  // [category.monetary], money
  using std::money_base;
  using std::money_get;
  using std::money_put;
  using std::moneypunct;
  using std::moneypunct_byname;

  // [category.messages], message retrieval
  using std::messages;
  using std::messages_base;
  using std::messages_byname;

#  if _LIBCPP_STD_VER < 26 || defined(_LIBCPP_ENABLE_CXX26_REMOVED_WSTRING_CONVERT)

  // [depr.conversions.buffer]
  using std::wbuffer_convert;

  // [depr.conversions.string]
  using std::wstring_convert;

#  endif // _LIBCPP_STD_VER < 26 || defined(_LIBCPP_ENABLE_CXX26_REMOVED_WSTRING_CONVERT)

#endif // _LIBCPP_HAS_NO_LOCALIZATION
} // namespace std
