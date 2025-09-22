// 2002-08-02 gdr

// Copyright (C) 2002, 2003 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// Test name lookup resolutions for standard functions applied to an
// array expression.
// { dg-do compile }

#include <valarray>

namespace My
{
  struct Number 
  { 
    operator bool() const;
  };
  
  Number operator+(Number);
  Number operator-(Number);
  Number operator~(Number);

  bool operator!(Number);
  
  Number operator+(Number, Number);
  Number operator-(Number, Number);
  Number operator*(Number, Number);
  Number operator/(Number, Number);
  Number operator%(Number, Number);

  Number operator^(Number, Number);
  Number operator&(Number, Number);
  Number operator|(Number, Number);

  Number operator<<(Number, Number);
  Number operator>>(Number, Number);

  bool operator==(Number, Number);
  bool operator!=(Number, Number);
  bool operator<(Number, Number);
  bool operator<=(Number, Number);
  bool operator>(Number, Number);
  bool operator>=(Number, Number);

  Number abs(Number);

  Number cos(Number);
  Number cosh(Number);
  Number acos(Number);

  Number sin(Number);
  Number sinh(Number);
  Number asin(Number);
  
  Number tan(Number);
  Number tanh(Number);
  Number atan(Number);

  Number exp(Number);
  Number log(Number);
  Number log10(Number);
  Number sqrt(Number);

  Number atan2(Number, Number);
  Number pow(Number, Number);
}

int main()
{
  typedef std::valarray<My::Number> Array;
  Array u(10), v(10);
  v = +u;
  v = -u;
  v = ~u;
  std::valarray<bool> z = !u;

  v = abs(u);
  
  v = cos(u);
  v = cosh(u);
  v = acos(u);

  v = sin(u);
  v = sinh(u);
  v = asin(u);

  v = tan(u);
  v = tanh(u);
  v = atan(u);

  v = exp(u);
  v = log(u);
  v = log10(u);
  v = sqrt(u);  

  Array w = u + v;
  w = u - v;
  w = u * v;
  w = u / v;
  w = u % v;

  w = u ^ v;
  w = u & v;
  w = u | v;

  w = u << v;
  w = u >> v;

  z = u == v;
  z = u != v;
  z = u < v;
  z = u <= v;
  z = u > v;
  z = u >= v;

  w = atan2(u, v);
  w = pow(u, v);
}
