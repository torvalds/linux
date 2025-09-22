// 1999-06-24 bkoz

// Copyright (C) 1999 Free Software Foundation, Inc.
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

#include <iostream>
#include <iterator>
#include <set>
#include <algorithm>

namespace std 
{
  std::ostream& 
  operator<<(std::ostream& os, std::pair<int, int> const& p) 
  { return os << p.first << ' ' << p.second; }
}

bool 
operator<(std::pair<int, int> const& lhs, std::pair<int, int> const& rhs) 
{ return lhs.first < rhs.first; }

int main () 
{
  typedef std::multiset<std::pair<int, int> >::iterator iterator;
  std::pair<int, int> p(69, 0);
  std::multiset<std::pair<int, int> > s;

  for (p.second = 0; p.second < 5; ++p.second)
    s.insert(p);
  for (iterator it = s.begin(); it != s.end(); ++it)
    if (it->second < 5) 
      {
	s.insert(it, p);
	++p.second;
      }

  // XXX need to use debug-assert here and get this working with an
  // ostrinsrtream, that way we can just check the strings for
  // equivalance.
  std::copy(s.begin(), s.end(), 
            std::ostream_iterator<std::pair<int, int> >(std::cout, "\n"));

  return 0;
}

/* output:
69 5
69 0
69 6
69 1
69 7
69 2
69 8
69 3
69 9
69 4
*/
