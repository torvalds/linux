// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  using std::div_t _LIBCPP_USING_IF_EXISTS;
  using std::ldiv_t _LIBCPP_USING_IF_EXISTS;
  using std::lldiv_t _LIBCPP_USING_IF_EXISTS;
  using std::size_t _LIBCPP_USING_IF_EXISTS;

  // [support.start.term], start and termination
  using std::_Exit _LIBCPP_USING_IF_EXISTS;
  using std::abort _LIBCPP_USING_IF_EXISTS;
  using std::at_quick_exit _LIBCPP_USING_IF_EXISTS;
  using std::atexit _LIBCPP_USING_IF_EXISTS;
  using std::exit _LIBCPP_USING_IF_EXISTS;
  using std::quick_exit _LIBCPP_USING_IF_EXISTS;

  using std::getenv _LIBCPP_USING_IF_EXISTS;
  using std::system _LIBCPP_USING_IF_EXISTS;

  // [c.malloc], C library memory allocation
  using std::aligned_alloc _LIBCPP_USING_IF_EXISTS;
  using std::calloc _LIBCPP_USING_IF_EXISTS;
  using std::free _LIBCPP_USING_IF_EXISTS;
  using std::malloc _LIBCPP_USING_IF_EXISTS;
  using std::realloc _LIBCPP_USING_IF_EXISTS;

  using std::atof _LIBCPP_USING_IF_EXISTS;
  using std::atoi _LIBCPP_USING_IF_EXISTS;
  using std::atol _LIBCPP_USING_IF_EXISTS;
  using std::atoll _LIBCPP_USING_IF_EXISTS;
  using std::strtod _LIBCPP_USING_IF_EXISTS;
  using std::strtof _LIBCPP_USING_IF_EXISTS;
  using std::strtol _LIBCPP_USING_IF_EXISTS;
  using std::strtold _LIBCPP_USING_IF_EXISTS;
  using std::strtoll _LIBCPP_USING_IF_EXISTS;
  using std::strtoul _LIBCPP_USING_IF_EXISTS;
  using std::strtoull _LIBCPP_USING_IF_EXISTS;

  // [c.mb.wcs], multibyte / wide string and character conversion functions
  using std::mblen _LIBCPP_USING_IF_EXISTS;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::mbstowcs _LIBCPP_USING_IF_EXISTS;
  using std::mbtowc _LIBCPP_USING_IF_EXISTS;
  using std::wcstombs _LIBCPP_USING_IF_EXISTS;
  using std::wctomb _LIBCPP_USING_IF_EXISTS;
#endif
  // [alg.c.library], C standard library algorithms
  using std::bsearch _LIBCPP_USING_IF_EXISTS;
  using std::qsort _LIBCPP_USING_IF_EXISTS;

  // [c.math.rand], low-quality random number generation
  using std::rand _LIBCPP_USING_IF_EXISTS;
  using std::srand _LIBCPP_USING_IF_EXISTS;

  // [c.math.abs], absolute values
  using std::abs _LIBCPP_USING_IF_EXISTS;

  using std::labs _LIBCPP_USING_IF_EXISTS;
  using std::llabs _LIBCPP_USING_IF_EXISTS;

  using std::div _LIBCPP_USING_IF_EXISTS;
  using std::ldiv _LIBCPP_USING_IF_EXISTS;
  using std::lldiv _LIBCPP_USING_IF_EXISTS;
} // namespace std
