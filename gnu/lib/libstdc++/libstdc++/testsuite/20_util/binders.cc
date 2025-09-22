// 2001-06-11  Benjamin Kosnik  <bkoz@redhat.com>

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

// 20.3.6 Binders

#include <vector>
#include <algorithm> // for_each
#include <functional>

class Elem 
{ 
public: 
  void print(int i) const { } 
  void modify(int i) { } 
}; 

// libstdc++/3113
void test01()
{ 
  std::vector<Elem> coll(2); 
  // OK 
  std::for_each(coll.begin(), coll.end(), 
	   std::bind2nd(std::mem_fun_ref(&Elem::print), 42));
  // OK
  std::for_each(coll.begin(), coll.end(), 
	   std::bind2nd(std::mem_fun_ref(&Elem::modify), 42));
}


int main()
{
  test01();
  return 0;
}
