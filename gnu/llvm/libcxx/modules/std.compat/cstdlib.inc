// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export {
  using ::div_t _LIBCPP_USING_IF_EXISTS;
  using ::ldiv_t _LIBCPP_USING_IF_EXISTS;
  using ::lldiv_t _LIBCPP_USING_IF_EXISTS;
  using ::size_t _LIBCPP_USING_IF_EXISTS;

  // [support.start.term], start and termination
  using ::_Exit _LIBCPP_USING_IF_EXISTS;
  using ::abort _LIBCPP_USING_IF_EXISTS;
  using ::at_quick_exit _LIBCPP_USING_IF_EXISTS;
  using ::atexit _LIBCPP_USING_IF_EXISTS;
  using ::exit _LIBCPP_USING_IF_EXISTS;
  using ::quick_exit _LIBCPP_USING_IF_EXISTS;

  using ::getenv _LIBCPP_USING_IF_EXISTS;
  using ::system _LIBCPP_USING_IF_EXISTS;

  // [c.malloc], C library memory allocation
  using ::aligned_alloc _LIBCPP_USING_IF_EXISTS;
  using ::calloc _LIBCPP_USING_IF_EXISTS;
  using ::free _LIBCPP_USING_IF_EXISTS;
  using ::malloc _LIBCPP_USING_IF_EXISTS;
  using ::realloc _LIBCPP_USING_IF_EXISTS;

  using ::atof _LIBCPP_USING_IF_EXISTS;
  using ::atoi _LIBCPP_USING_IF_EXISTS;
  using ::atol _LIBCPP_USING_IF_EXISTS;
  using ::atoll _LIBCPP_USING_IF_EXISTS;
  using ::strtod _LIBCPP_USING_IF_EXISTS;
  using ::strtof _LIBCPP_USING_IF_EXISTS;
  using ::strtol _LIBCPP_USING_IF_EXISTS;
  using ::strtold _LIBCPP_USING_IF_EXISTS;
  using ::strtoll _LIBCPP_USING_IF_EXISTS;
  using ::strtoul _LIBCPP_USING_IF_EXISTS;
  using ::strtoull _LIBCPP_USING_IF_EXISTS;

  // [c.mb.wcs], multibyte / wide string and character conversion functions
  using ::mblen _LIBCPP_USING_IF_EXISTS;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using ::mbstowcs _LIBCPP_USING_IF_EXISTS;
  using ::mbtowc _LIBCPP_USING_IF_EXISTS;
  using ::wcstombs _LIBCPP_USING_IF_EXISTS;
  using ::wctomb _LIBCPP_USING_IF_EXISTS;
#endif
  // [alg.c.library], C standard library algorithms
  using ::bsearch _LIBCPP_USING_IF_EXISTS;
  using ::qsort _LIBCPP_USING_IF_EXISTS;

  // [c.math.rand], low-quality random number generation
  using ::rand _LIBCPP_USING_IF_EXISTS;
  using ::srand _LIBCPP_USING_IF_EXISTS;

  // [c.math.abs], absolute values
  using ::abs _LIBCPP_USING_IF_EXISTS;

  using ::labs _LIBCPP_USING_IF_EXISTS;
  using ::llabs _LIBCPP_USING_IF_EXISTS;

  using ::div _LIBCPP_USING_IF_EXISTS;
  using ::ldiv _LIBCPP_USING_IF_EXISTS;
  using ::lldiv _LIBCPP_USING_IF_EXISTS;

} // export
