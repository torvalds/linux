// 2000-06-29 bkoz

// Copyright (C) 2000, 2001, 2002 Free Software Foundation
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

// 27.6.1.3 unformatted input functions
// NB: ostream has a particular "seeks" category. Adopt this for istreams too.
// @require@ %-*.tst %-*.txt
// @diff@ %-*.tst %-*.txt

#include <istream>
#include <sstream>
#include <fstream>
#include <testsuite_hooks.h>

bool test01()
{
  using namespace std;
  typedef ios::pos_type pos_type;

  bool test = true;
  const char str_lit01[] = "istream_seeks-1.tst";

  // in
  // test default ctors leave things in the same positions...
  istringstream ist1;
  pos_type p3 = ist1.tellg();

  ifstream ifs1;
  pos_type p4 = ifs1.tellg();

  VERIFY( p3 == p4 );

  // in
  // test ctors leave things in the same positions...
  istringstream ist2("bob_marley:kaya");
  p3 = ist2.tellg();

  ifstream ifs2(str_lit01);
  p4 = ifs2.tellg();

  VERIFY( p3 == p4 );
  return test;
}

const char* s = " lootpack, peanut butter wolf, rob swift, madlib, quasimoto";
const int times = 10;

void write_rewind(std::iostream& stream)
{
  for (int j = 0; j < times; j++) 
    {
      bool test = true;
      std::streampos begin = stream.tellg();
      
      for (int i = 0; i < times; ++i)
	stream << j << '-' << i << s << '\n';
      
      stream.seekg(begin);
      std::streampos end = stream.tellg(); 
      std::streampos badpos = std::streampos(std::streambuf::off_type(-1));
    }
}

void check_contents(std::iostream& stream)
{
  bool test = true;

  stream.clear();
  stream.seekg(0, std::ios::beg);
  int i = 0;
  int loop = times * times + 2;
  while (i < loop)
    {
      stream.ignore(80, '\n');
      if (stream.good())
	++i;
      else
	break;
    }
  VERIFY( i == times );
}

// fstream
// libstdc++/2346
void test02()
{	 
  std::fstream ofstrm;
  ofstrm.open("istream_seeks-3.txt", std::ios::out);
  if (!ofstrm)
    std::abort();
  write_rewind(ofstrm);
  ofstrm.close();

  std::fstream ifstrm;
  ifstrm.open("istream_seeks-3.txt", std::ios::in);
  check_contents(ifstrm);
  ifstrm.close();
}

// stringstream
// libstdc++/2346
void test03()
{	 
  std::stringstream sstrm;

  write_rewind(sstrm);
  check_contents(sstrm);
}

// fstreams
void test04(void)
{
  typedef std::istream::off_type off_type;

  bool test = true;
  std::istream::pos_type pos01, pos02, pos03, pos04, pos05, pos06;
  std::ios_base::iostate state01, state02;
  const char str_lit01[] = "istream_seeks-1.txt";
  const char str_lit02[] = "istream_seeks-2.txt";
  std::ifstream if01(str_lit01, std::ios_base::in | std::ios_base::out);
  std::ifstream if02(str_lit01, std::ios_base::in);
  std::ifstream if03(str_lit02, std::ios_base::out | std::ios_base::trunc); 
  VERIFY( if01.good() );
  VERIFY( if02.good() );
  VERIFY( if03.good() );

  std::istream is01(if01.rdbuf());
  std::istream is02(if02.rdbuf());
  std::istream is03(if03.rdbuf());

  // pos_type tellg()
  // in | out
  pos01 = is01.tellg();
  pos02 = is01.tellg();
  VERIFY( pos01 == pos02 );
  //  VERIFY( istream::pos_type(0) != pos01 ); //deprecated

  // in
  pos03 = is02.tellg();
  pos04 = is02.tellg();
  VERIFY( pos03 == pos04 );
  //  VERIFY( istream::pos_type(0) != pos03 ); //deprecated

  // out
  pos05 = is03.tellg();
  pos06 = is03.tellg();
  VERIFY( pos05 == pos06 );
  //  VERIFY( istream::pos_type(0) != pos01 ); //deprecated

  // istream& seekg(pos_type)
  // istream& seekg(off_type, ios_base::seekdir)

  // cur 
  // NB: see library issues list 136. It's the v-3 interp that seekg
  // only sets the input buffer, or else istreams with buffers that
  // have _M_mode == ios_base::out will fail to have consistency
  // between seekg and tellg.
  state01 = is01.rdstate();
  is01.seekg(10, std::ios_base::cur);
  state02 = is01.rdstate();
  pos01 = is01.tellg(); 
  VERIFY( pos01 == pos02 + off_type(10) ); 
  VERIFY( state01 == state02 );
  pos02 = is01.tellg(); 
  VERIFY( pos02 == pos01 ); 

  state01 = is02.rdstate();
  is02.seekg(10, std::ios_base::cur);
  state02 = is02.rdstate();
  pos03 = is02.tellg(); 
  VERIFY( pos03 == pos04 + off_type(10) ); 
  VERIFY( state01 == state02 );
  pos04 = is02.tellg(); 
  VERIFY( pos03 == pos04 ); 

  state01 = is03.rdstate();
  is03.seekg(10, std::ios_base::cur);
  state02 = is03.rdstate();
  pos05 = is03.tellg(); 
  VERIFY( pos05 == pos06 + off_type(10) ); 
  VERIFY( state01 == state02 );
  pos06 = is03.tellg(); 
  VERIFY( pos05 == pos06 ); 

  // beg
  state01 = is01.rdstate();
  is01.seekg(20, std::ios_base::beg);
  state02 = is01.rdstate();
  pos01 = is01.tellg(); 
  VERIFY( pos01 == pos02 + off_type(10) ); 
  VERIFY( state01 == state02 );
  pos02 = is01.tellg(); 
  VERIFY( pos02 == pos01 ); 

  state01 = is02.rdstate();
  is02.seekg(20, std::ios_base::beg);
  state02 = is02.rdstate();
  pos03 = is02.tellg(); 
  VERIFY( pos03 == pos04 + off_type(10) ); 
  VERIFY( state01 == state02 );
  pos04 = is02.tellg(); 
  VERIFY( pos03 == pos04 ); 

  state01 = is03.rdstate();
  is03.seekg(20, std::ios_base::beg);
  state02 = is03.rdstate();
  pos05 = is03.tellg(); 
  VERIFY( pos05 == pos06 + off_type(10) );
  VERIFY( state01 == state02 );
  pos06 = is03.tellg(); 
  VERIFY( pos05 == pos06 );

  // libstdc++/6414
  if01.seekg(0, std::ios_base::beg);
  pos01 = if01.tellg();
  if01.peek();
  pos02 = if01.tellg();
  VERIFY( pos02 == pos01 );
}

// stringstreams
void test05(void)
{
  typedef std::istream::off_type off_type;

  bool test = true;
  std::istream::pos_type pos01, pos02, pos03, pos04, pos05, pos06;
  std::ios_base::iostate state01, state02;
  const char str_lit01[] = "istream_seeks-1.tst";
  std::ifstream if01(str_lit01);
  std::ifstream if02(str_lit01);
  std::ifstream if03(str_lit01);
  VERIFY( if01.good() );
  VERIFY( if02.good() );
  VERIFY( if03.good() );

  std::stringbuf strbuf01(std::ios_base::in | std::ios_base::out);
  if01 >> &strbuf01; 
  // initialize stringbufs that are ios_base::out
  std::stringbuf strbuf03(strbuf01.str(), std::ios_base::out);
  // initialize stringbufs that are ios_base::in
  std::stringbuf strbuf02(strbuf01.str(), std::ios_base::in);

  std::istream is01(&strbuf01);
  std::istream is02(&strbuf02);
  std::istream is03(&strbuf03);

  // pos_type tellg()
  // in | out
  pos01 = is01.tellg();
  pos02 = is01.tellg();
  VERIFY( pos01 == pos02 );
  // VERIFY( istream::pos_type(0) != pos01 ); // deprecated

  // in
  pos03 = is02.tellg();
  pos04 = is02.tellg();
  VERIFY( pos03 == pos04 );
  //  VERIFY( istream::pos_type(0) != pos03 ); // deprecated

  // out
  pos05 = is03.tellg();
  pos06 = is03.tellg();
  VERIFY( pos05 == pos06 );
  //  VERIFY( istream::pos_type(0) != pos01 ); //deprecated

  // istream& seekg(pos_type)
  // istream& seekg(off_type, ios_base::seekdir)

  // cur 
  // NB: see library issues list 136. It's the v-3 interp that seekg
  // only sets the input buffer, or else istreams with buffers that
  // have _M_mode == ios_base::out will fail to have consistency
  // between seekg and tellg.
  state01 = is01.rdstate();
  is01.seekg(10, std::ios_base::cur);
  state02 = is01.rdstate();
  pos01 = is01.tellg(); 
  VERIFY( pos01 == pos02 + off_type(10) ); 
  VERIFY( state01 == state02 );
  pos02 = is01.tellg(); 
  VERIFY( pos02 == pos01 ); 

  state01 = is02.rdstate();
  is02.seekg(10, std::ios_base::cur);
  state02 = is02.rdstate();
  pos03 = is02.tellg(); 
  VERIFY( pos03 == pos04 + off_type(10) ); 
  VERIFY( state01 == state02 );
  pos04 = is02.tellg(); 
  VERIFY( pos03 == pos04 ); 

  state01 = is03.rdstate();
  is03.seekg(10, std::ios_base::cur);
  state02 = is03.rdstate();
  pos05 = is03.tellg(); 
  VERIFY( pos05 == pos06 ); // as only out buffer 
  VERIFY( state01 != state02 );
  pos06 = is03.tellg(); 
  VERIFY( pos05 == pos06 ); 

  // beg
  state01 = is01.rdstate();
  is01.seekg(20, std::ios_base::beg);
  state02 = is01.rdstate();
  pos01 = is01.tellg(); 
  VERIFY( pos01 == pos02 + off_type(10) ); 
  VERIFY( state01 == state02 );
  pos02 = is01.tellg(); 
  VERIFY( pos02 == pos01 ); 

  state01 = is02.rdstate();
  is02.seekg(20, std::ios_base::beg);
  state02 = is02.rdstate();
  pos03 = is02.tellg(); 
  VERIFY( pos03 == pos04 + off_type(10) ); 
  VERIFY( state01 == state02 );
  pos04 = is02.tellg(); 
  VERIFY( pos03 == pos04 ); 

  state01 = is03.rdstate();
  is03.seekg(20, std::ios_base::beg);
  state02 = is03.rdstate();
  pos05 = is03.tellg(); 
  VERIFY( pos05 == pos06 ); // as only out buffer 
  VERIFY( state01 == state02 );
  pos06 = is03.tellg(); 
  VERIFY( pos05 == pos06 ); 
}

// libstdc++/8348
void test06(void)
{
  using namespace std;
  bool test = true;
  string num1("555");

  // tellg
  {
    istringstream iss(num1);
    istream::pos_type pos1 = iss.tellg();
    int asNum = 0;
    iss >> asNum;
    VERIFY( test = iss.eof() );
    VERIFY( test = !iss.fail() );
    iss.tellg();
    VERIFY( test = !iss.fail() );
  }

  // seekg
  {
    istringstream iss(num1);
    istream::pos_type pos1 = iss.tellg();
    int asNum = 0;
    iss >> asNum;
    VERIFY( test = iss.eof() );
    VERIFY( test = !iss.fail() );
    iss.seekg(0, ios_base::beg);
    VERIFY( test = !iss.fail() );
  }

  // seekg
  {
    istringstream iss(num1);
    istream::pos_type pos1 = iss.tellg();
    int asNum = 0;
    iss >> asNum;
    VERIFY( test = iss.eof() );
    VERIFY( test = !iss.fail() );
    iss.seekg(pos1);
    VERIFY( test = !iss.fail() );
  }
}

int main()
{
  test01();

  test02();
  test03();

  test04();
  test05();

  test06();
  return 0;
}


