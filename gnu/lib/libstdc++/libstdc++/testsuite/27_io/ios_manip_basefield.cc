// 981027 ncm work with libstdc++v3

// Copyright (C) 1997, 1998, 1999, 2002 Free Software Foundation, Inc.
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

#include <sstream>
#include <locale>
#include <iomanip>
#include <testsuite_hooks.h>
			
struct MyNP : std::numpunct<char>
{
  std::string do_grouping() const;
  char   do_thousands_sep() const;
};

std::string MyNP::do_grouping() const { std::string s("\3"); return s; }
char   MyNP::do_thousands_sep() const { return ' '; }

int
test01()
{
  bool test = true;

  const char lit[] = "0123 456\n: 01 234 567:\n:0123 456   :\n"
                     ":    012 345:\n:     01 234:\n:0726 746 425:\n"
                     ":04 553 207 :\n:   0361 100:\n:       0173:\n"
                     "0x12 345 678\n|0x000012 345 678|\n|0x12 345 6780000|\n"
                     "|00000x12 345 678|\n|0x000012 345 678|\n";

  std::ostringstream oss;
  oss.imbue(std::locale(std::locale(), new MyNP));

  // Octals
  oss << std::oct << std::showbase;
  oss << 0123456l << std::endl;

  oss << ":" << std::setw(11);
  oss << 01234567l << ":" << std::endl;

  oss << ":" << std::setw(11) << std::left;
  oss << 0123456l << ":" << std::endl;

  oss << ":" << std::setw(11) << std::right;
  oss << 012345l << ":" << std::endl;

  oss << ":" << std::setw(11) << std::internal;
  oss << 01234l << ":" << std::endl;

  oss << ":" << std::setw(11);
  oss << 123456789l << ":" << std::endl;

  oss << ":" << std::setw(11) << std::left;
  oss << 1234567l << ":" << std::endl;

  oss << ":" << std::setw(11) << std::right;
  oss << 123456l << ":" << std::endl;

  oss << ":" << std::setw(11) << std::internal;
  oss << 123l << ":" << std::endl;

  // Hexadecimals
  oss << std::hex << std::setfill('0');
  oss << 0x12345678l << std::endl;

  oss << "|" << std::setw(16);
  oss << 0x12345678l << "|" << std::endl;

  oss << "|" << std::setw(16) << std::left;
  oss << 0x12345678l << "|" << std::endl;

  oss << "|" << std::setw(16) << std::right;
  oss << 0x12345678l << "|" << std::endl;

  oss << "|" << std::setw(16) << std::internal;
  oss << 0x12345678l << "|" << std::endl;

  VERIFY( oss.good() );
  VERIFY( oss.str() == lit );

  return 0;
}

int
test02()
{
  bool 		test = true;
  const std::string 	str_blank;
  std::string 	str_tmp;
  std::stringbuf 	strbuf;
  std::ostream 	o(&strbuf);

  o <<  std::setw(6) <<  std::right << "san";
  VERIFY( strbuf.str() == "   san" ); 
  strbuf.str(str_blank);

  o <<  std::setw(6) <<  std::internal << "fran";
  VERIFY( strbuf.str() == "  fran" ); 
  strbuf.str(str_blank);

  o << std::setw(6) <<  std::left << "cisco";
  VERIFY( strbuf.str() == "cisco " ); 
  strbuf.str(str_blank);

  VERIFY( test );
  return 0;
}

int 
main() 
{
  test01();
  test02();
  return 0;
}

// Projected output:
/*
0123 456
: 01 234 567:
:0123 456   :
:    012 345:
:     01 234:
:0726 746 425:
:04 553 207 :
:   0361 100:
:       0173:
0x12 345 678
|0x000012 345 678|
|0x12 345 6780000|
|00000x12 345 678|
|0x000012 345 678|
*/
