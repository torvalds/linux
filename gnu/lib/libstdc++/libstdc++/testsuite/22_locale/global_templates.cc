// 2000-09-11 Benjamin Kosnik <bkoz@redhat.com>

// Copyright (C) 2000 Free Software Foundation
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

// 22.1.2 locale globals [lib.locale.global.templates]

#include <cwchar> // for mbstate_t
#include <locale>
#include <testsuite_hooks.h>

typedef std::codecvt<char, char, std::mbstate_t> ccodecvt;

class gnu_codecvt: public ccodecvt { }; 

void test01()
{
  using namespace std;

  bool test = true;

  // construct a locale object with the C facet
  const locale& 	cloc = locale::classic();
  // sanity check the constructed locale has the normal facet
  VERIFY( has_facet<ccodecvt>(cloc) );

  // construct a locale object with the specialized facet.
  locale                loc(locale::classic(), new gnu_codecvt);
  // sanity check the constructed locale has the specialized facet.
  VERIFY( has_facet<gnu_codecvt>(loc) );

  try 
    { const ccodecvt& cvt01 = use_facet<ccodecvt>(cloc); }
  catch(...)
    { VERIFY( false ); }

  try
    { const gnu_codecvt& cvt02 = use_facet<gnu_codecvt>(loc); } 
  catch(...)
    { VERIFY( false ); }

  try 
    { const ccodecvt& cvt03 = use_facet<gnu_codecvt>(cloc); }
  catch(bad_cast& obj)
    { VERIFY( true ); }
  catch(...)
    { VERIFY( false ); }
}

int main ()
{
  test01();

  return 0;
}
