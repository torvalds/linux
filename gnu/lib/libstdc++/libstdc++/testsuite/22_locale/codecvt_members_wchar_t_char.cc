// 2000-08-18 Benjamin Kosnik <bkoz@cygnus.com>

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

#include <cwchar> // for mbstate_t
#include <locale>
#include <testsuite_hooks.h>


// Need to explicitly set the state(mbstate_t) to zero.
// How to do this is not specified by the ISO C99 standard, so we
// might need to add some operators to make the intuiative case
// work:
//   w_codecvt::state_type state00;
//   state00 = 0;  
// or, can use this explicit "C" initialization:
//   w_codecvt::state_type state01 = {0, 0};
// .. except Ulrich says: Use memset. Always use memset. Feel the force...
#ifdef _GLIBCPP_USE_WCHAR_T
void
zero_state(std::mbstate_t& state)
{ std::memset(&state, 0, sizeof(std::mbstate_t)); }

// Required instantiation
// codecvt<wchar_t, char, mbstate_t>
void test01()
{
  using namespace std;
  typedef codecvt<wchar_t, char, mbstate_t> 	w_codecvt;
  typedef codecvt_base::result			result;
  typedef wchar_t				int_type;
  typedef char					ext_type;
  typedef char_traits<wchar_t>			int_traits;
  typedef char_traits<char>			ext_traits;

  bool 			test = true;
  const ext_type* 	e_lit = "black pearl jasmine tea";
  const ext_type*       efrom_next;
  const int_type* 	i_lit = L"black pearl jasmine tea";
  const int_type*       ifrom_next;
  int 			size = strlen(e_lit);
  ext_type* 		e_arr = new ext_type[size + 1];
  ext_type*		eto_next;
  int_type* 		i_arr = new int_type[size + 1];
  int_type*		ito_next;

  locale 		loc;
  const w_codecvt* 	cvt = &use_facet<w_codecvt>(loc); 

  // in
  w_codecvt::state_type state01;
  zero_state(state01);
  result r1 = cvt->in(state01, e_lit, e_lit + size, efrom_next, 
		      i_arr, i_arr + size, ito_next);
  VERIFY( r1 == codecvt_base::ok );
  VERIFY( !int_traits::compare(i_arr, i_lit, size) ); 
  VERIFY( efrom_next == e_lit + size );
  VERIFY( ito_next == i_arr + size );

  // out
  w_codecvt::state_type state02;
  zero_state(state02);  
  result r2 = cvt->out(state02, i_lit, i_lit + size, ifrom_next, 
		       e_arr, e_arr + size, eto_next);
  VERIFY( r2 == codecvt_base::ok );
  VERIFY( !ext_traits::compare(e_arr, e_lit, size) ); 
  VERIFY( ifrom_next == i_lit + size );
  VERIFY( eto_next == e_arr + size );

  // unshift
  strcpy(e_arr, e_lit);
  w_codecvt::state_type state03;
  zero_state(state03);
  result r3 = cvt->unshift(state03, e_arr, e_arr + size, eto_next);
  VERIFY( r3 == codecvt_base::noconv );
  VERIFY( !strcmp(e_arr, e_lit) ); 
  VERIFY( eto_next == e_arr );

  int i = cvt->encoding();
  VERIFY( i == 4 ); // Target-dependent.

  VERIFY( !cvt->always_noconv() );

  w_codecvt::state_type state04;
  zero_state(state04);
  int j = cvt->length(state03, e_lit, e_lit + size, 5);
  VERIFY( j == 5 );

  int k = cvt->max_length();
  VERIFY( k == 1 );

  delete [] e_arr;
  delete [] i_arr;
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
#endif /* !defined(_GLIBCPP_USE_WCHAR_T) */


int main ()
{
#if defined(_GLIBCPP_USE_WCHAR_T)
  test01();
  test02();
  test03();
#endif 

  return 0;
}
