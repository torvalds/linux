// 2001-12-28  Phil Edwards  <pme@gcc.gnu.org>
//
// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// Concept checking must remain sane.

// { dg-options "-D_GLIBCPP_CONCEPT_CHECKS" }

#include <vector>
#include <string>
#include <algorithm>
#include <testsuite_hooks.h>

using namespace std;


// PR libstdc++/2054 and follow-up discussion
struct indirectCompare
{
  indirectCompare(const vector<string>& v) : V(v) {}

  bool operator()( int x,  int y) const
  {
       return V[x] < V[y];
  }

  bool operator()( int x, const string& a) const
  {
       return V[x] < a;
  }

  bool operator()( const string& a, int x) const
  {
       return V[x] < a;
  }

  const vector<string>& V;
};

void
test2054( )
{
  const int Maxi = 1022;

  vector<string> Words(Maxi);
  vector<int> Index(Maxi);

  for(size_t i = 0; i < Index.size(); i++)
     Index[i] = i;

  indirectCompare aComparison(Words);

  sort(Index.begin(), Index.end(), aComparison);

  string SearchTerm;

  lower_bound(Index.begin(), Index.end(), SearchTerm, aComparison);
  upper_bound(Index.begin(), Index.end(), SearchTerm, aComparison);
  equal_range(Index.begin(), Index.end(), SearchTerm, aComparison);
  binary_search(Index.begin(), Index.end(), SearchTerm, aComparison);
}

int main()
{
  test2054();

  return 0;
}
