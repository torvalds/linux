// 2002-05-24 bkoz

// Copyright (C) 2002 Free Software Foundation, Inc.
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

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// 22.2.1.3.2 ctype<char> members

#include <locale>
#include <vector>
#include <testsuite_hooks.h>

// libstdc++/6701
void test01()
{
  using namespace std;
  typedef char 	wide_type;

  bool test = true;
  const char dfault = '?';
  const locale loc_c = locale::classic();
  const ctype<wide_type>& ctype_c = use_facet<ctype<wide_type> >(loc_c); 

  basic_string<wide_type> 	wide("wibble");
  basic_string<char> 		narrow("wibble");
  vector<char> 			narrow_chars(wide.length() + 1);
  
  // narrow(charT c, char dfault) const
  for (int i = 0; i < wide.length(); ++i)
    {
      char c = ctype_c.narrow(wide[i], dfault);
      VERIFY( c == narrow[i] );
    }

  // narrow(const charT* low, const charT* high, char dfault, char* dest) const
  ctype_c.narrow(&wide[0], &wide[wide.length()], dfault, &narrow_chars[0]);  
  VERIFY( narrow_chars[0] != dfault );
  for (int i = 0; i < wide.length(); ++i)
    VERIFY( narrow_chars[i] == narrow[i] );
}

void test02()
{
  using namespace std;
  typedef char 	wide_type;

  bool test = true;
  const char dfault = '?';
  const locale loc_c = locale::classic();
  const ctype<wide_type>& ctype_c = use_facet<ctype<wide_type> >(loc_c); 

  // Construct non-asci string.
  basic_string<wide_type> 	wide("wibble");
  wide += wide_type(1240);
  wide += "kibble";
  basic_string<char> 		narrow("wibble");
  narrow += char(1240);
  narrow += "kibble";
  vector<char> 			narrow_chars(wide.length() + 1);

  // narrow(charT c, char dfault) const
  for (int i = 0; i < wide.length(); ++i)
    {
      char c = ctype_c.narrow(wide[i], dfault);
      VERIFY( c == narrow[i] );
    }

  // narrow(const charT* low, const charT* high, char dfault, char* dest) const
  ctype_c.narrow(&wide[0], &wide[wide.length()], dfault, &narrow_chars[0]);  
  VERIFY( narrow_chars[0] != dfault );
  for (int i = 0; i < wide.length(); ++i)
    VERIFY( narrow_chars[i] == narrow[i] );
}

int main() 
{
  test01();
  test02();
  return 0;
}
