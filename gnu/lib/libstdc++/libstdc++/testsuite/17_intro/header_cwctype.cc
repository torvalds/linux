// 2000-01-03 bkoz

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

// 17.4.1.2 Headers, cwctype

#if defined(_GLIBCPP_USE_WCHAR_T)
#include <cwctype>
// Make sure wint_t is in namespace std
std::wint_t i = 5;

// Make sure table 46 is in namespace std.
using std::wctype_t;
using std::wctrans_t;
using std::iswalpha;
using std::iswupper;
using std::iswlower;
using std::iswdigit;
using std::iswxdigit;
using std::iswalnum;
using std::iswspace;
using std::iswpunct;
using std::iswprint;
using std::iswgraph;
using std::iswcntrl;
using std::iswctype;
using std::towctrans;
using std::towlower;
using std::towupper;
using std::wctrans;
using std::wctype;
#endif

int main(void)
{
  return 0;
}

