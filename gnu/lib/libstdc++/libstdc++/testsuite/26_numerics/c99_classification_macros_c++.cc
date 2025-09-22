// 2001-04-06 gdr

// Copyright (C) 2001 Free Software Foundation, Inc.
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

// { dg-do compile }

#include <cmath>

void fpclassify() { }

void isfinite() { }

void isinf() { }

void isnan() { }

void isnormal() { }

void signbit() { }

void isgreater() { }

void isgreaterequal() { }

void isless() { }

void islessequal() { }

void islessgreater() { }

void isunordered() { }

#if defined(_GLIBCPP_USE_C99)
template <typename _Tp>
  void test_c99_classify()
  {
    bool test = true;

    typedef _Tp fp_type;
    fp_type f1 = 1.0;
    fp_type f2 = 3.0;
    int res = 0;
    
    res = std::fpclassify(f1);
    res = std::isfinite(f2);
    res = std::isinf(f1);
    res = std::isnan(f2);
    res = std::isnormal(f1);
    res = std::signbit(f2);
    res = std::isgreater(f1, f2);
    res = std::isgreaterequal(f1, f2);
    res = std::isless(f1, f2);
    res = std::islessequal(f1,f2);
    res = std::islessgreater(f1, f2);
    res = std::isunordered(f1, f2);
  }
#endif

int main()
{
#if defined(_GLIBCPP_USE_C99)
  test_c99_classify<float>();
  //test_c99_classify<double>();
#endif
  return 0;
}
