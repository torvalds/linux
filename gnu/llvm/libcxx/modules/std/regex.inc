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
  // [re.const], regex constants
  namespace regex_constants {
    using std::regex_constants::error_type;
    using std::regex_constants::match_flag_type;
    using std::regex_constants::syntax_option_type;

    // regex_constants is a bitmask type.
    // [bitmask.types] specified operators
    using std::regex_constants::operator&;
    using std::regex_constants::operator&=;
    using std::regex_constants::operator^;
    using std::regex_constants::operator^=;
    using std::regex_constants::operator|;
    using std::regex_constants::operator|=;
    using std::regex_constants::operator~;

  } // namespace regex_constants

  // [re.badexp], class regex_error
  using std::regex_error;

  // [re.traits], class template regex_traits
  using std::regex_traits;

  // [re.regex], class template basic_regex
  using std::basic_regex;

  using std::regex;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wregex;
#  endif

  // [re.regex.swap], basic_regex swap
  using std::swap;

  // [re.submatch], class template sub_match
  using std::sub_match;

  using std::csub_match;
  using std::ssub_match;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wcsub_match;
  using std::wssub_match;
#  endif

  // [re.submatch.op], sub_match non-member operators
  using std::operator==;
  using std::operator<=>;

  using std::operator<<;

  // [re.results], class template match_results
  using std::match_results;

  using std::cmatch;
  using std::smatch;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wcmatch;
  using std::wsmatch;
#  endif

  // match_results comparisons

  // [re.results.swap], match_results swap

  // [re.alg.match], function template regex_match
  using std::regex_match;

  // [re.alg.search], function template regex_search
  using std::regex_search;

  // [re.alg.replace], function template regex_replace
  using std::regex_replace;

  // [re.regiter], class template regex_iterator
  using std::regex_iterator;

  using std::cregex_iterator;
  using std::sregex_iterator;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wcregex_iterator;
  using std::wsregex_iterator;
#  endif

  // [re.tokiter], class template regex_token_iterator
  using std::regex_token_iterator;

  using std::cregex_token_iterator;
  using std::sregex_token_iterator;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::wcregex_token_iterator;
  using std::wsregex_token_iterator;
#  endif

  namespace pmr {
    using std::pmr::match_results;

    using std::pmr::cmatch;
    using std::pmr::smatch;
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    using std::pmr::wcmatch;
    using std::pmr::wsmatch;
#  endif
  }    // namespace pmr
#endif // _LIBCPP_HAS_NO_LOCALIZATION
} // namespace std
