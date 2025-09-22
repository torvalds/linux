// 2001-08-23 pme & Sylvain.Pion@sophia.inria.fr

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

// 23.3.1.2, table 69 -- map::insert(p,t)

#include <map>
#include <testsuite_hooks.h>

// { dg-do run }

// libstdc++/3349 and
// http://gcc.gnu.org/ml/gcc-patches/2001-08/msg01375.html
void test01()
{
  typedef std::map<int, int>   Map;
  Map             M;
  Map::iterator   hint;

  hint = M.insert(Map::value_type(7, 0)).first;

  M.insert(hint, Map::value_type(8, 1));
  M.insert(M.begin(), Map::value_type(9, 2));

#if 0
  // The tree's __rb_verify() member must be exposed in map<> before this
  // will even compile.  It's good test to see that "missing" entries are
  // in fact present in the {map,tree}, but in the wrong place.
  if (0)
  {
      Map::iterator  i = M.begin();
      while (i != M.end()) {
          std::cerr << '(' << i->first << ',' << i->second << ")\n";
          ++i;
      }
      std::cerr << "tree internal verify: "
                << std::boolalpha << M.__rb_verify() << "\n";
  }
#endif

  VERIFY ( M.find(7) != M.end() );
  VERIFY ( M.find(8) != M.end() );
  VERIFY ( M.find(9) != M.end() );
}


int main()
{
  test01();

  return 0;
}

