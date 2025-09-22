// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [ratio.ratio], class template ratio
  using std::ratio;

  // [ratio.arithmetic], ratio arithmetic
  using std::ratio_add;
  using std::ratio_divide;
  using std::ratio_multiply;
  using std::ratio_subtract;

  // [ratio.comparison], ratio comparison
  using std::ratio_equal;
  using std::ratio_greater;
  using std::ratio_greater_equal;
  using std::ratio_less;
  using std::ratio_less_equal;
  using std::ratio_not_equal;

  using std::ratio_equal_v;
  using std::ratio_greater_equal_v;
  using std::ratio_greater_v;
  using std::ratio_less_equal_v;
  using std::ratio_less_v;
  using std::ratio_not_equal_v;

  // [ratio.si], convenience SI typedefs
  using std::atto;
  using std::centi;
  using std::deca;
  using std::deci;
  using std::exa;
  using std::femto;
  using std::giga;
  using std::hecto;
  using std::kilo;
  using std::mega;
  using std::micro;
  using std::milli;
  using std::nano;
  using std::peta;
  using std::pico;
  using std::tera;

  // These are not supported by libc++, due to the range of intmax_t
  // using std::yocto;
  // using std::yotta;
  // using std::zepto;
  // using std::zetta
} // namespace std
