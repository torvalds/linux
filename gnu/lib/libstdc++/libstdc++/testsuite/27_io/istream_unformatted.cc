// 1999-08-11 bkoz

// Copyright (C) 1999, 2000, 2001, 2002 Free Software Foundation
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
// @require@ %-*.tst %-*.txt
// @diff@ %-*.tst %-*.txt

#include <cstring> // for strncmp,...
#include <istream>
#include <sstream>
#include <fstream>
#include <testsuite_hooks.h>

int
test01()
{
  typedef std::ios::traits_type traits_type;

  bool test = true;
  const std::string str_01;
  const std::string str_02("soul eyes: john coltrane quartet");
  std::string strtmp;

  std::stringbuf isbuf_03(str_02, std::ios_base::in);
  std::stringbuf isbuf_04(str_02, std::ios_base::in);

  std::istream is_00(NULL);
  std::istream is_03(&isbuf_03);
  std::istream is_04(&isbuf_04);
  std::ios_base::iostate state1, state2, statefail, stateeof;
  statefail = std::ios_base::failbit;
  stateeof = std::ios_base::eofbit;

  // istream& read(char_type* s, streamsize n)
  char carray[60] = "";
  state1 = is_04.rdstate();
  is_04.read(carray, 0);
  state2 = is_04.rdstate();
  VERIFY( state1 == state2 );

  state1 = is_04.rdstate();
  is_04.read(carray, 9);
  state2 = is_04.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( !std::strncmp(carray, "soul eyes", 9) );
  VERIFY( is_04.peek() == ':' );

  state1 = is_03.rdstate();
  is_03.read(carray, 60);
  state2 = is_03.rdstate();
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & stateeof) ); 
  VERIFY( static_cast<bool>(state2 & statefail) ); 
  VERIFY( !std::strncmp(carray, "soul eyes: john coltrane quartet", 35) );


  // istream& ignore(streamsize n = 1, int_type delim = traits::eof())
  state1 = is_04.rdstate();
  is_04.ignore();
  VERIFY( is_04.gcount() == 1 );
  state2 = is_04.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( is_04.peek() == ' ' );

  state1 = is_04.rdstate();
  is_04.ignore(0);
  VERIFY( is_04.gcount() == 0 );
  state2 = is_04.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( is_04.peek() == ' ' );

  state1 = is_04.rdstate();
  is_04.ignore(5, traits_type::to_int_type(' '));
  VERIFY( is_04.gcount() == 1 );
  state2 = is_04.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( is_04.peek() == 'j' );

  // int_type peek()
  state1 = is_04.rdstate();
  VERIFY( is_04.peek() == 'j' );
  VERIFY( is_04.gcount() == 0 );
  state2 = is_04.rdstate();
  VERIFY( state1 == state2 );

  is_04.ignore(30);
  state1 = is_04.rdstate();
  VERIFY( is_04.peek() == traits_type::eof() );
  VERIFY( is_04.gcount() == 0 );
  state2 = is_04.rdstate();
  VERIFY( state1 != state2 );


  // istream& putback(char c)
  is_04.clear();
  state1 = is_04.rdstate();
  is_04.putback('|');
  VERIFY( is_04.gcount() == 0 );
  state2 = is_04.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( is_04.peek() == '|' );

  // istream& unget()
  is_04.clear();
  state1 = is_04.rdstate();
  is_04.unget();
  VERIFY( is_04.gcount() == 0 );
  state2 = is_04.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( is_04.peek() == 'e' );
  
  // int sync()
  int i = is_00.sync();

#ifdef DEBUG_ASSERT
  assert(test);
#endif
 
  return 0;
}

int
test02()
{
  typedef std::char_traits<char>	traits_type;

  bool test = true;
  const char str_lit01[] = "\t\t\t    sun*ra \n"
  "                            "
  "and his myth science arkestra present\n"
  "                            "
  "angles and demons @ play\n"
  "                            "
  "the nubians of plutonia";
  std::string str01(str_lit01);
  std::string strtmp;

  std::stringbuf sbuf_04(str01, std::ios_base::in);

  std::istream is_00(NULL);
  std::istream is_04(&sbuf_04);
  std::ios_base::iostate state1, state2, statefail, stateeof;
  statefail = std::ios_base::failbit;
  stateeof = std::ios_base::eofbit;
  std::streamsize count1, count2;
  char carray1[400] = "";

  // istream& getline(char* s, streamsize n, char delim)
  // istream& getline(char* s, streamsize n)
  state1 = is_00.rdstate();
  is_00.getline(carray1, 20, '*');
  state2 = is_00.rdstate();
  // make sure failbit was set, since we couldn't extract
  // from the NULL streambuf...
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & statefail) );
  
  VERIFY( is_04.gcount() == 0 );
  state1 = is_04.rdstate();
  is_04.getline(carray1, 1, '\t'); // extracts, throws away
  state2 = is_04.rdstate();  
  VERIFY( is_04.gcount() == 1 );
  VERIFY( state1 == state2 );
  VERIFY( state1 == 0 );
  VERIFY( !traits_type::compare("", carray1, 1) );

  state1 = is_04.rdstate();
  is_04.getline(carray1, 20, '*');
  state2 = is_04.rdstate();  
  VERIFY( is_04.gcount() == 10 );
  VERIFY( state1 == state2 );
  VERIFY( state1 == 0 );
  VERIFY( !traits_type::compare("\t\t    sun", carray1, 10) );

  state1 = is_04.rdstate();
  is_04.getline(carray1, 20);
  state2 = is_04.rdstate();  
  VERIFY( is_04.gcount() == 4 );
  VERIFY( state1 == state2 );
  VERIFY( state1 == 0 );
  VERIFY( !traits_type::compare("ra ", carray1, 4) );

  state1 = is_04.rdstate();
  is_04.getline(carray1, 65);
  state2 = is_04.rdstate();  
  VERIFY( is_04.gcount() == 64 );
  VERIFY( state1 != state2 );
  VERIFY( state2 == statefail );
  VERIFY( !traits_type::compare(
  "                            and his myth science arkestra presen",
                               carray1, 65) );

  is_04.clear();
  state1 = is_04.rdstate();
  is_04.getline(carray1, 120, '|');
  state2 = is_04.rdstate();  
  VERIFY( is_04.gcount() == 106 );
  VERIFY( state1 != state2 );
  VERIFY( state2 == stateeof );

  is_04.clear();
  state1 = is_04.rdstate();
  is_04.getline(carray1, 100, '|');
  state2 = is_04.rdstate();  
  VERIFY( is_04.gcount() == 0 ); 
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & stateeof) );
  VERIFY( static_cast<bool>(state2 & statefail) );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
 
  return 0;
}

int
test03()
{
  typedef std::char_traits<char>	traits_type;

  bool test = true;
  const char str_lit01[] = 
  "   sun*ra \n\t\t\t   & his arkestra, featuring john gilmore: \n"
  "                         "
    "jazz in silhouette: images and forecasts of tomorrow";

  std::string str01(str_lit01);
  std::string strtmp;

  std::stringbuf sbuf_03;
  std::stringbuf sbuf_04(str01, std::ios_base::in);
  std::stringbuf sbuf_05(str01, std::ios_base::in);

  std::istream is_00(NULL);
  std::istream is_04(&sbuf_04);
  std::istream is_05(&sbuf_05);
  std::ios_base::iostate state1, state2, statefail, stateeof;
  statefail = std::ios_base::failbit;
  stateeof = std::ios_base::eofbit;
  std::streamsize count1, count2;
  char carray1[400] = "";

  // int_type get()
  // istream& get(char*, streamsize, char delim)
  // istream& get(char*, streamsize)
  // istream& get(streambuf&, char delim)
  // istream& get(streambuf&)
  is_00.get(carray1, 2);
  VERIFY( static_cast<bool>(is_00.rdstate() & statefail) ); 
  VERIFY( is_00.gcount() == 0 );

  is_04.get(carray1, 4);
  VERIFY( !(is_04.rdstate() & statefail) );
  VERIFY( !traits_type::compare(carray1, "   ", 4) );
  VERIFY( is_04.gcount() == 3 );

  is_04.clear();
  is_04.get(carray1 + 3, 200);
  VERIFY( !(is_04.rdstate() & statefail) );
  VERIFY( !(is_04.rdstate() & stateeof) );
  VERIFY( !traits_type::compare(carray1, str_lit01, 10) );
  VERIFY( is_04.gcount() == 7 );

  is_04.clear();
  is_04.get(carray1, 200);
  VERIFY( !(is_04.rdstate() & stateeof) );
  VERIFY( static_cast<bool>(is_04.rdstate() & statefail) ); // delimiter
  VERIFY( is_04.gcount() == 0 );
  is_04.clear();
  is_04.get(carray1, 200, '[');
  VERIFY( static_cast<bool>(is_04.rdstate() & stateeof) );
  VERIFY( !(is_04.rdstate() & statefail) );
  VERIFY( is_04.gcount() == 125 );
  is_04.clear();  
  is_04.get(carray1, 200);
  VERIFY( static_cast<bool>(is_04.rdstate() & stateeof) );
  VERIFY( static_cast<bool>(is_04.rdstate() & statefail) ); 
  VERIFY( is_04.gcount() == 0 );

  std::stringbuf sbuf_02(std::ios_base::in);
  is_05.clear();
  is_05.get(sbuf_02);
  VERIFY( is_05.gcount() == 0 );
  VERIFY( static_cast<bool>(is_05.rdstate() & statefail) ); 
  VERIFY( !(is_05.rdstate() & stateeof) ); 

  is_05.clear();
  is_05.get(sbuf_03);
  VERIFY( is_05.gcount() == 10 );
  VERIFY( sbuf_03.str() == "   sun*ra " );
  VERIFY( !(is_05.rdstate() & statefail) ); 
  VERIFY( !(is_05.rdstate() & stateeof) ); 

  is_05.clear();
  is_05.get(sbuf_03, '|');
  VERIFY( is_05.gcount() == 125 );
  VERIFY( sbuf_03.str() == str_lit01 );
  VERIFY( !(is_05.rdstate() & statefail) ); 
  VERIFY( static_cast<bool>(is_05.rdstate() & stateeof) ); 

  is_05.clear();
  is_05.get(sbuf_03, '|');
  VERIFY( is_05.gcount() == 0 );
  VERIFY( static_cast<bool>(is_05.rdstate() & stateeof) ); 
  VERIFY( static_cast<bool>(is_05.rdstate() & statefail) ); 

#ifdef DEBUG_ASSERT
  assert(test);
#endif
 
  return 0;
}

// http://gcc.gnu.org/ml/libstdc++/2000-q1/msg00177.html
int
test04()
{
  bool test = true;

  const std::string str_00("Red_Garland_Qunitet-Soul_Junction");
  std::string strtmp;
  char c_array[str_00.size() + 4];

  std::stringbuf isbuf_00(str_00, std::ios_base::in);
  std::istream is_00(&isbuf_00);
  std::ios_base::iostate state1, state2, statefail, stateeof;
  statefail = std::ios_base::failbit;
  stateeof = std::ios_base::eofbit;

  state1 = stateeof | statefail;
  VERIFY( is_00.gcount() == 0 );
  is_00.read(c_array, str_00.size() + 1);
  VERIFY( is_00.gcount() == str_00.size() );
  VERIFY( is_00.rdstate() == state1 );

  is_00.read(c_array, str_00.size());
  VERIFY( is_00.rdstate() == state1 );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return 0;
}

// http://gcc.gnu.org/ml/libstdc++/2000-07/msg00003.html
int
test05()
{
  const char* charray = "\n"
"a\n"
"aa\n"
"aaa\n"
"aaaa\n"
"aaaaa\n"
"aaaaaa\n"
"aaaaaaa\n"
"aaaaaaaa\n"
"aaaaaaaaa\n"
"aaaaaaaaaa\n"
"aaaaaaaaaaa\n"
"aaaaaaaaaaaa\n"
"aaaaaaaaaaaaa\n"
"aaaaaaaaaaaaaa\n";

  bool test = true;
  const std::streamsize it = 5;
  std::streamsize br = 0;
  char tmp[it];
  std::stringbuf sb(charray, std::ios_base::in);
  std::istream ifs(&sb);
  std::streamsize blen = std::strlen(charray);
  VERIFY(!(!ifs));
  while(ifs.getline(tmp, it) || ifs.gcount())
    {
      br += ifs.gcount();
      if(ifs.eof())
        {
          // Just sanity checks to make sure we've extracted the same
          // number of chars that were in the streambuf
          VERIFY(br == blen);
          // Also, we should only set the failbit if we could
          // _extract_ no chars from the stream, i.e. the first read
          // returned EOF. 
          VERIFY(ifs.fail() && ifs.gcount() == 0);
        }
      else if(ifs.fail())
        {
	  // delimiter not read
	  //
	  // either
	  // -> extracted no characters
	  // or
	  // -> n - 1 characters are stored
          ifs.clear(ifs.rdstate() & ~std::ios::failbit);
          VERIFY((ifs.gcount() == 0) || (std::strlen(tmp) == it - 1));
          VERIFY(!(!ifs));
          continue;
        }
      else 
        {
	  // delimiter was read.
	  //
	  // -> strlen(__s) < n - 1 
	  // -> delimiter was seen -> gcount() > strlen(__s)
          VERIFY(ifs.gcount() == std::strlen(tmp) + 1);
          continue;
        }
    }

  return 0;
}


// http://gcc.gnu.org/ml/libstdc++/2000-07/msg00126.html
int
test06()
{
  using namespace std;

  bool test = true;
  const streamsize it = 5;
  char tmp[it];
  const char* str_lit = "abcd\n";

  stringbuf strbuf(str_lit, std::ios_base::in);
  istream istr(&strbuf);
  
  istr.getline(tmp,it); 
  VERIFY( istr.gcount() == it );  // extracted whole string
  VERIFY( strlen(tmp) == 4 );     // stored all but '\n'
  VERIFY( !istr.eof() );          // extracted up to but not eof
  VERIFY( !istr.fail() );         // failbit not set
  
  char c = 'z';
  istr.get(c);
  VERIFY( c == 'z' );
  VERIFY( istr.eof() );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  
  return 0;
}

// bug reported by bgarcia@laurelnetworks.com
// http://gcc.gnu.org/ml/libstdc++-prs/2000-q3/msg00041.html
void
test07()
{
  bool test = true;
  const char* tfn = "istream_unformatted-1.txt";
  std::ifstream infile;
  infile.open(tfn);
  VERIFY( !(!infile) );
  while (infile)
    {
      std::string line;
      std::ostringstream line_ss;
      while (infile.peek() == '\n')
	infile.get();
      infile.get(*(line_ss.rdbuf()));
      line = line_ss.str();
      VERIFY( line == "1234567890" || line == "" );
    }
}

// 2002-04-19 PR libstdc++ 6360
void
test08()
{
  using namespace std;
  bool test = true;

  stringstream ss("abcd" "\xFF" "1234ina donna coolbrith");  
  char c;
  ss >> c;
  VERIFY( c == 'a' );
  ss.ignore(8);
  ss >> c;
  VERIFY( c == 'i' );
}
    
// Theodore Papadopoulo 
void 
test09()
{
  using namespace std;
  bool test = true;

  istringstream iss("Juana Briones");
  char tab[13];
  iss.read(tab, 13);
  if (!iss)
    test = false;
  VERIFY( test );
}

// libstdc++/70220
void
test10()
{
  using namespace std;
  bool test = true;
  typedef string string_type;
  typedef stringbuf stringbuf_type;
  typedef istream istream_type;

  int res = 0;
  streamsize n;
  string_type  input("abcdefg\n");
  stringbuf_type sbuf(input);
  istream_type  istr(&sbuf);
  
  istr.ignore(0);
  if (istr.gcount() != 0) 
    test = false;
  VERIFY( test );
  
  istr.ignore(0, 'b');
  if (istr.gcount() != 0) 
    test = false;
  VERIFY( test );
  
  istr.ignore();	// Advance to next position.
  istr.ignore(0, 'b');
  if ((n=istr.gcount()) != 0) 
    test = false;
  VERIFY( test );
  
  if (istr.peek() != 'b')
    test = false;
  VERIFY( test );
}


// libstdc++/8258
class mybuf : public std::basic_streambuf<char> 
{ };

void test11()
{
  bool test = true;
  using namespace std;
  char arr[10];
  mybuf sbuf;
  basic_istream<char, char_traits<char> > istr(&sbuf);
  
  VERIFY(istr.rdstate() == ios_base::goodbit);
  VERIFY(istr.readsome(arr, 10) == 0);
  VERIFY(istr.rdstate() == ios_base::goodbit);
}

// libstdc++/6746   
void test12()
{
  using namespace std;
  bool test = true;
  streamsize sum = 0;
  istringstream iss("shamma shamma");
      
  // test01
  size_t i = iss.rdbuf()->in_avail();
  VERIFY( i != 0 );
    
  // test02
  streamsize extracted;
  do
    {
      char buf[1024];
      extracted = iss.readsome(buf, sizeof buf);
      sum += extracted;
    }
  while (iss.good() && extracted);
  VERIFY( sum != 0 );  
}
    
// libstdc++/6746   
void test13()
{
  using namespace std;
  bool test = true;
  streamsize sum = 0;
  ifstream ifs("istream_unformatted-1.tst");
      
  // test01
  size_t i = ifs.rdbuf()->in_avail();
  VERIFY( i != 0 );
    
  // test02
  streamsize extracted;
  do
    {
      char buf[1024];
      extracted = ifs.readsome(buf, sizeof buf);
      sum += extracted;
    }
  while (ifs.good() && extracted);
  VERIFY( sum != 0 );  
}
 
int 
main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
  test06();
  test07();
  test08();
  test09();
  test10();
  test11();

  test12();
  test13();

  return 0;
}
