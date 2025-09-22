// 2001-04-06 gdr

// Copyright (C) 2000 Free Software Foundation, Inc.
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

#include <vector>
#include <algorithm>

//
// 25.1.8 Make sure std::equal doesn't make any extra assumption
//        about operator== and operator!=
//

struct X { };

bool operator==(X, X) { return true; }

// Not implemented on purpose.  { dg-do link }
bool operator!=(X, X);

int main()
{
  std::vector<X> v, w;

  return !std::equal(v.begin(), v.end(), w.begin());
}
