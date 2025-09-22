// 2000-09-07 bgarcia@laurelnetworks.com

// Copyright (C) 2000, 2001, 2003 Free Software Foundation, Inc.
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

// 23.3.4 template class multiset

#include <map>
#include <string>
#include <iostream>

// libstdc++/737
// http://gcc.gnu.org/ml/libstdc++/2000-11/msg00093.html
void test02()
{
  typedef std::map<int, int> MapInt;
  
  MapInt m;
  
  for (unsigned i = 0; i < 10; ++i)
    m.insert(MapInt::value_type(i,i));
  
  for (MapInt::const_iterator i = m.begin(); i != m.end(); ++i)
    std::cerr << i->second << ' ';
  
  for (MapInt::const_iterator i = m.begin(); m.end() != i; ++i)
    std::cerr << i->second << ' ';
}

int main()
{
  test02();
  return 0;
}
