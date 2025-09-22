// 2000-08-17 Benjamin Kosnik <bkoz@cygnus.com>

// Copyright (C) 2000, 2002 Free Software Foundation
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

// 22.2.1.5 - Template class codecvt [lib.locale.codecvt]

#include <locale>
#include <testsuite_hooks.h>

// Required instantiation, degenerate conversion.
// codecvt<char, char, mbstate_t>
void test01()
{
  using namespace std;
  typedef codecvt_base::result			result;
  typedef codecvt<char, char, mbstate_t> 	c_codecvt;

  bool 			test = true;
  const char* 		c_lit = "black pearl jasmine tea";
  const char* 	        from_next;
  int 			size = 25;
  char* 		c_arr = new char[size];
  char*                 c_ref = new char[size];
  char*			to_next;

  locale 		loc;
  c_codecvt::state_type state;
  const c_codecvt* 	cvt = &use_facet<c_codecvt>(loc); 

  // According to the resolution of DR19 (see also libstd++/9168), in
  // case of degenerate conversion ('noconv'), "there are no changes to
  // the values in [to, to_limit)."
  memset(c_ref, 'X', size);

  // in
  memset(c_arr, 'X', size);
  result r1 = cvt->in(state, c_lit, c_lit + size, from_next, 
		      c_arr, c_arr + size, to_next);
  VERIFY( r1 == codecvt_base::noconv );
  VERIFY( !memcmp(c_arr, c_ref, size) ); 
  VERIFY( from_next == c_lit );
  VERIFY( to_next == c_arr );

  // out
  memset(c_arr, 'X', size);
  result r2 = cvt->out(state, c_lit, c_lit + size, from_next, 
		       c_arr, c_arr + size, to_next);
  VERIFY( r2 == codecvt_base::noconv );
  VERIFY( !memcmp(c_arr, c_ref, size) ); 
  VERIFY( from_next == c_lit );
  VERIFY( to_next == c_arr );

  // unshift
  ::strlcpy(c_arr, c_lit, size);
  result r3 = cvt->unshift(state, c_arr, c_arr + size, to_next);
  VERIFY( r3 == codecvt_base::noconv );
  VERIFY( !strcmp(c_arr, c_lit) ); 
  VERIFY( to_next == c_arr );

  int i = cvt->encoding();
  VERIFY( i == 1 );

  VERIFY( cvt->always_noconv() );

  int j = cvt->length(state, c_lit, c_lit + size, 5);
  VERIFY( j == 5 );

  int k = cvt->max_length();
  VERIFY( k == 1 );

  delete [] c_arr;
  delete [] c_ref;
}

// libstdc++/5280
void test02()
{
#ifdef _GLIBCPP_HAVE_SETENV 
  // Set the global locale to non-"C".
  std::locale loc_de("de_DE");
  std::locale::global(loc_de);

  // Set LANG environment variable to de_DE.
  const char* oldLANG = getenv("LANG");
  if (!setenv("LANG", "de_DE", 1))
    {
      test01();
      setenv("LANG", oldLANG ? oldLANG : "", 1);
    }
#endif
}

// http://gcc.gnu.org/ml/libstdc++/2002-05/msg00038.html
void test03()
{
  bool test = true;

  const char* tentLANG = std::setlocale(LC_ALL, "ja_JP.eucjp");
  if (tentLANG != NULL)
    {
      std::string preLANG = tentLANG;
      test01();
      std::string postLANG = std::setlocale(LC_ALL, NULL);
      VERIFY( preLANG == postLANG );
    }
}

int main ()
{
  test01();
  test02();
  test03();
  return 0;
}
