// 1999-07-01 bkoz

// Copyright (C) 1999, 2000, 2001, 2002 Free Software Foundation, Inc.
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

// 21.3.7.9 inserters and extractors

// NB: This file is predicated on sstreams, istreams, and ostreams
// working, not to mention other major details like char_traits, and
// all of the string class.

#include <string>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <testsuite_hooks.h>

bool test01(void)
{
  bool test = true;
  typedef std::string::size_type csize_type;
  typedef std::string::const_reference cref;
  typedef std::string::reference ref;
  csize_type npos = std::string::npos;
  csize_type csz01, csz02;

  const std::string str01("sailing grand traverse bay\n"
	       "\t\t\t    from Elk Rapids to the point reminds me of miles");
  const std::string str02("sailing");
  const std::string str03("grand");
  const std::string str04("traverse");
  const std::string str05;
  std::string str10;
  
  // istream& operator>>(istream&, string&)
  std::istringstream istrs01(str01);
  istrs01 >> str10;
  VERIFY( str10 == str02 );
  try 
    {
      std::istringstream::int_type i01 = istrs01.peek(); //a-boo
      VERIFY( std::istringstream::traits_type::to_char_type(i01) == ' ' );
    }
  catch(std::exception& fail) 
    {
      VERIFY( false ); // shouldn't throw
    }

  istrs01.clear();
  istrs01 >> str10; 
  VERIFY( str10 == str03 ); 
  istrs01.clear();
  istrs01 >> str10; 
  VERIFY( str10 == str04 ); // sentry picks out the white spaces. . 

  std::istringstream istrs02(str05); // empty
  istrs02 >> str10;
  VERIFY( str10 == str04 );
 
  // istream& getline(istream&, string&, char)
  // istream& getline(istream&, string&)
  try 
    {
      istrs01.clear();
      getline(istrs01, str10);
      VERIFY( !istrs01.fail() );
      VERIFY( !istrs01.eof() );
      VERIFY( istrs01.good() );
      VERIFY( str10 == " bay" );
    }
  catch(std::exception& fail) 
    {
      VERIFY( false ); // shouldn't throw
    }

  try 
    {
      istrs01.clear();
      getline(istrs01, str10,'\t');
      VERIFY( !istrs01.fail() );
      VERIFY( !istrs01.eof() );
      VERIFY( istrs01.good() );
      VERIFY( str10 == str05 );
    }
  catch(std::exception& fail) 
    {
      VERIFY( false ); // shouldn't throw
    }
  
  try 
    {
      istrs01.clear();
      getline(istrs01, str10,'\t');
      VERIFY( !istrs01.fail() );
      VERIFY( !istrs01.eof() );
      VERIFY( istrs01.good() );
      VERIFY( str10 == str05 );
    }
  catch(std::exception& fail) 
    {
      VERIFY( false ); // shouldn't throw
    }
  
  try 
    {
      istrs01.clear();
      getline(istrs01, str10, '.');
      VERIFY( !istrs01.fail() );
      VERIFY( istrs01.eof() );
      VERIFY( !istrs01.good() );
      VERIFY( str10 == "\t    from Elk Rapids to the point reminds me of miles" );
    }
  catch(std::exception& fail) 
    {
      VERIFY( false ); // shouldn't throw
    }

  try 
    {
      getline(istrs02, str10);
      VERIFY( istrs02.fail() );
      VERIFY( istrs02.eof() );
      VERIFY( str10 =="\t    from Elk Rapids to the point reminds me of miles" );
    }
  catch(std::exception& fail) 
    {
      VERIFY( false ); // shouldn't throw
    }
  
  // ostream& operator<<(ostream&, const basic_string&)
  std::ostringstream ostrs01;
  try 
    {
      ostrs01 << str01;
      VERIFY( ostrs01.str() == str01 );
    }
  catch(std::exception& fail) 
    {
      VERIFY( false );
    }
  
  std::string hello_world;
  std::cout << hello_world;
  
#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return test;
}


// testing basic_stringbuf::xsputn via stress testing with large strings
// based on a bug report libstdc++ 9
void test04(int size)
{
  bool test = true;
  std::string str(size, 's');
  int expected_size = 2 * (size + 1);
  std::ostringstream oss(str);
  
  // sanity checks
  VERIFY( str.size() == size );
  VERIFY( oss.good() );

  // stress test
  oss << str << std::endl;
  if (!oss.good()) 
    test = false;

  oss << str << std::endl;
  if (!oss.good()) 
    test = false;

  VERIFY( str.size() == size );
  VERIFY( oss.good() );
  std::string str_tmp = oss.str();
  VERIFY( str_tmp.size() == expected_size );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}


// testing basic_filebuf::xsputn via stress testing with large strings
// based on a bug report libstdc++ 9
// mode == out
void test05(int size)
{
  bool test = true;
  const char filename[] = "inserters_extractors-1.txt";
  const char fillc = 'f';
  std::ofstream ofs(filename);
  std::string str(size, fillc);

  // sanity checks
  VERIFY( str.size() == size );
  VERIFY( ofs.good() );

  // stress test
  ofs << str << std::endl;
  if (!ofs.good()) 
    test = false;

  ofs << str << std::endl;
  if (!ofs.good()) 
    test = false;

  VERIFY( str.size() == size );
  VERIFY( ofs.good() );

  ofs.close();

  // sanity check on the written file
  std::ifstream ifs(filename);
  int count = 0;
  char c;
  while (count <= (2 * size) + 4)
    {
      ifs >> c;
      if (ifs.good() && c == fillc)
	{
	  ++count;
	  c = '0';
	}
      else 
	break;
    }

  VERIFY( count == 2 * size );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}


// istringstream/stringbuf extractor properly size buffer based on
// actual, not allocated contents (string.size() vs. string.capacity()).
// http://gcc.gnu.org/ml/libstdc++/1999-q4/msg00049.html
void test06(void)
{
  bool test = true;

  typedef std::string::size_type size_type;
  std::string str01("@silent");
  size_type i01 = str01.size();
  size_type i02 = str01.capacity();
  str01.erase(0, 1);
  size_type i03 = str01.size();
  size_type i04 = str01.capacity();
  VERIFY( i01 - 1 == i03 );
  VERIFY( i02 >= i04 );

  std::istringstream is(str01);
  std::string str02;
  is >> str02 >> std::ws;
  size_type i05 = str02.size();
  size_type i06 = str02.capacity();
  VERIFY( i05 == i03 );
  VERIFY( i06 <= i04 );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}

// http://gcc.gnu.org/ml/libstdc++/2000-q1/msg00085.html
// istream::operator>>(string)
// sets failbit
// NB: this is a defect in the standard.
void test07(void)
{
  bool test = true;
  const std::string name("z6.cc");
  std::istringstream iss (name);
  int i = 0;
  std::string s;
  while (iss >> s) 
    ++i;

  VERIFY( i < 3 );
  VERIFY( static_cast<bool>(iss.rdstate() & std::ios_base::failbit) );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}

// libstdc++/1019
void test08()
{
  using namespace std;

  bool 		test = true;
  istringstream istrm("enero:2001");
  int 		year;
  char 		sep;
  string 	month;
  
  istrm >> setw(5) >> month >> sep >> year;
  VERIFY( month.size() == 5 );
  VERIFY( sep == ':' );
  VERIFY( year == 2001 );
}

// libstdc++/2830
void test09()
{
  bool test = true;
  std::string blanks( 3, '\0');
  std::string foo = "peace";
  foo += blanks;
  foo += "& love";
  
  std::ostringstream oss1;
  oss1 << foo;
  VERIFY( oss1.str() == foo );
  
  std::ostringstream oss2;
  oss2.width(20);
  oss2 << foo;
  VERIFY( oss2.str() != foo );
  VERIFY( oss2.str().size() == 20 );
}

int main()
{ 
  test01();

  test04(1); // expected_size == 4
  test04(1000); // expected_size == 2002
  test04(10000); // expected_size == 20002

  test05(1); 
  test05(1000); 
  test05(10000);

  test06();
  test07();

  test08();
  
  test09();
  return 0;
}
