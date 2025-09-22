// 2001-10-30 Benjamin Kosnik  <bkoz@redhat.com>

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

// 21.3.5 string modifiers

#include <string>
#include <cstdio>
#include <testsuite_hooks.h>

void
test01()
{
  bool test = true;

  using namespace std;

  const char* strlit = "../the long pier/Hanalei Bay/Kauai/Hawaii";
  string aux = strlit;
  string::size_type i = aux.rfind("/");
  if (i != string::npos)
    aux.assign(aux, i + 1, string::npos);
  VERIFY(aux == "Hawaii");

  aux = strlit;
  i = aux.rfind("r/");
  if (i != string::npos)
    aux.assign(aux, i + 1, string::npos);
  VERIFY(aux.c_str()[9] == 'B');
  VERIFY(aux == "/Hanalei Bay/Kauai/Hawaii");
}

// assign(const basic_string& __str, size_type __pos, size_type __n)
void
test02()
{
  bool test = true;

  using namespace std;
  
  string one = "Selling England by the pound";
  string two = one;
  string three = "Brilliant trees";

  one.assign(one, 8, 100);
  VERIFY( one == "England by the pound" );

  one.assign(one, 8, 0);
  VERIFY( one == "" );
 
  one.assign(two, 8, 7);
  VERIFY( one == "England" );

  one.assign(three, 10, 100);
  VERIFY( one == "trees" );

  three.assign(one, 0, 3);
  VERIFY( three == "tre" );
}

// assign(const _CharT* __s, size_type __n)
// assign(const _CharT* __s)
void
test03()
{
  bool test = true;

  using namespace std;
 
  string one; 
  string two;
  string three = two;
  const char * source = "Selling England by the pound";

  one.assign(source);
  VERIFY( one == "Selling England by the pound" );

  one.assign(source, 28);
  VERIFY( one == "Selling England by the pound" );

  two.assign(source, 7);
  VERIFY( two == "Selling" );
  
  one.assign(one.c_str() + 8, 20);
  VERIFY( one == "England by the pound" );

  one.assign(one.c_str() + 8, 6);
  VERIFY( one == "by the" );
}



int main()
{ 
  test01();
  test02();
  test03();

  return 0;
}
