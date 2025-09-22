// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {

  // [complex], class template complex
  using std::complex;

  // [complex.ops], operators
  using std::operator+;
  using std::operator-;
  using std::operator*;
  using std::operator/;

  using std::operator==;
#ifndef _LIBCPP_HAS_NO_LOCALIZATION
  using std::operator>>;
  using std::operator<<;
#endif // _LIBCPP_HAS_NO_LOCALIZATION

  // [complex.value.ops], values
  using std::imag;
  using std::real;

  using std::abs;
  using std::arg;
  using std::norm;

  using std::conj;
  using std::polar;
  using std::proj;

  // [complex.transcendentals], transcendentals
  using std::acos;
  using std::asin;
  using std::atan;

  using std::acosh;
  using std::asinh;
  using std::atanh;

  using std::cos;
  using std::cosh;
  using std::exp;
  using std::log;
  using std::log10;

  using std::pow;

  using std::sin;
  using std::sinh;
  using std::sqrt;
  using std::tan;
  using std::tanh;

#if _LIBCPP_STD_VER >= 26
  // [complex.tuple], tuple interface
  using std::get;
  using std::tuple_element;
  using std::tuple_size;
#endif

  // [complex.literals], complex literals
  inline namespace literals {
    inline namespace complex_literals {
      using std::operator""il;
      using std::operator""i;
      using std::operator""if;
    } // namespace complex_literals
  }   // namespace literals

} // namespace std
