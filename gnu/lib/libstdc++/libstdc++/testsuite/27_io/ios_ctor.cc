// 1999-07-23 bkoz

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

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// 27.4.4.1 basic_ios constructors

#include <ios>
#include <sstream>
#include <testsuite_hooks.h>

void test01()
{
  bool test = true;
  std::string str_01("jade cove, big sur");
  std::string str_05;
  std::stringbuf strb_01;
  std::stringbuf strb_02(str_01, std::ios_base::in);
  std::stringbuf strb_03(str_01, std::ios_base::out);
  const std::ios_base::fmtflags flag01 = std::ios_base::skipws | 
    					 std::ios_base::dec;
  std::ios_base::fmtflags flag02, flag03;
  const std::locale glocale = std::locale();

  // explicit basic_ios(streambuf* sb)
  std::ios ios_00(0);
  std::ios ios_01(&strb_01);
  std::ios ios_02(&strb_02);
  std::ios ios_03(&strb_03);

  // basic_ios()
  // NB: This is protected so need to go through fstream 

  // void init(sreambuf* sb)
  // NB: This is protected so need to go through fstream/stringstream
  // Can double-check the accuracy of the above initializations though.
  VERIFY( ios_00.rdbuf() == 0 );
  VERIFY( ios_00.tie() == 0 );
  VERIFY( ios_00.rdstate() == std::ios_base::badbit );
  VERIFY( ios_00.exceptions() == std::ios_base::goodbit );
  flag02 = ios_00.flags();
  VERIFY( flag02 == flag01 );
  VERIFY( ios_00.width() == 0 );  
  VERIFY( ios_00.precision() == 6 );  
  VERIFY( ios_00.fill() == ios_00.widen(' ') );
  VERIFY( ios_00.getloc() == glocale );    

  VERIFY( ios_01.rdbuf() == &strb_01 );
  VERIFY( ios_01.tie() == 0 );
  VERIFY( ios_01.rdstate() == std::ios_base::goodbit );
  VERIFY( ios_01.exceptions() == std::ios_base::goodbit );
  flag02 = ios_01.flags();
  VERIFY( flag02 == flag01 );
  VERIFY( ios_01.width() == 0 );  
  VERIFY( ios_01.precision() == 6 );  
  VERIFY( ios_01.fill() == ios_01.widen(' ') );
  VERIFY( ios_01.getloc() == glocale );    

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}

int main() {
  test01();
  return 0;
}
