// 1999-10-14 bkoz

// Copyright (C) 1999, 2001 Free Software Foundation, Inc.
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

// 27.6.1.1.2 class basic_istream::sentry

#include <istream>
#include <sstream>
#include <testsuite_hooks.h>

void test01()
{
  bool test = true;
  const char* lit01 = "stereolab on the bolsinga tip";
  const std::string str01(lit01);

  std::stringbuf strbuf01;
  std::stringbuf strbuf02(str01);
  std::istream istr01(&strbuf01);
  std::istream istr02(&strbuf02);
  
  // test negatives
  std::istream::sentry sentry01(istr01);	
  VERIFY( bool(sentry01) == false ); 

  std::istream::sentry sentry02(istr01, true);
  VERIFY( bool(sentry02) == false ); 

  // positive tests
  std::istream::sentry sentry03(istr02);	
  VERIFY( bool(sentry03) == true ); 

  std::istream::sentry sentry04(istr02, true);
  VERIFY( bool(sentry04) == true ); 
}

// libstdc++/944
void 
test02()
{
  using namespace std;
  istringstream in("80.21 56.89 12.3");
  bool  test = true;
  int i = 0;
  double x;

  // ios_base::eof == 2
  while (in >> x)
    {
      ++i;
      if (i > 3) 
	break;
    }
  VERIFY( i == 3 );
}    

int main() 
{
  test01();
  test02();
  return 0;
}
