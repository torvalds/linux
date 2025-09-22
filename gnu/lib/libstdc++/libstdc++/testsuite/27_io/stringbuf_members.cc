// 981208 bkoz test functionality of basic_stringbuf for char_type == char

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003
// Free Software Foundation, Inc.
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

#include <sstream>
#include <testsuite_hooks.h>

std::string str_01("mykonos. . . or what?");
std::string str_02("paris, or sainte-maxime?");
std::string str_03;
std::stringbuf strb_01(str_01);
std::stringbuf strb_02(str_02, std::ios_base::in);
std::stringbuf strb_03(str_03, std::ios_base::out);


// test the underlying allocator
bool test01() {
  bool test = false;
  std::allocator<char> alloc_01;
  std::allocator<char>::size_type size_01 = alloc_01.max_size();
  std::allocator<char>::pointer p_01 = alloc_01.allocate(32);

  return true;
}


// test the streambuf/stringbuf locale settings
bool test02() {
  std::locale loc_tmp;
  loc_tmp = strb_01.getloc();
  strb_01.pubimbue(loc_tmp); //This should initialize _M_init to true
  strb_01.getloc(); //This should just return _M_locale

  return true;
}


// test member functions
bool test03() {
  bool test = true;

  //stringbuf::str()
  VERIFY( strb_01.str() == str_01 );
  VERIFY( strb_02.str() == str_02 );
  VERIFY( strb_03.str() == str_03 );
 
  //stringbuf::str(string&)
  strb_03.str("none of the above, go to the oberoi in cairo, egypt.");
  strb_03.str(str_01);
  std::streamsize d1 = strb_01.in_avail();
  std::streamsize d2 = strb_03.in_avail();
  VERIFY( d1 ); // non-zero
  VERIFY( !d2 ); // zero, cuz ios_base::out
  VERIFY( d1 != d2 ); //these should be the same
  VERIFY( str_01.length() == d1 );  
  VERIFY( strb_01.str() == strb_03.str() ); //ditto

  // stringbuf::str(string&) and stringbuf::stringbuf(string&), where the
  // string in question contains embedded NUL characters.  Note that in this
  // embedded-NUL situation, the size must be passed to the string ctor.
  std::string str_nulls ("eschew \0 obfuscation", 20);  // tested in 21_strings
  std::stringbuf strb_normal (str_01);
  std::stringbuf strb_nulls (str_nulls);
  strb_normal.str(str_nulls);  // tried using 'strb_01' rather than declaring
                               // another variable, but then test04 broke!
  VERIFY( strb_nulls.in_avail() == str_nulls.size()  );
  VERIFY( strb_nulls.str().size() == 20              );
  VERIFY( strb_normal.in_avail() == str_nulls.size() );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
 
  return test;
}


// test overloaded virtual functions
bool test04() {
  bool 			test = true;
  std::string 		str_tmp;
  std::stringbuf 		strb_tmp;
  std::streamsize 		strmsz_1, strmsz_2;
  std::streamoff  		strmof_1(-1), strmof_2;
  typedef std::stringbuf::int_type int_type;
  typedef std::stringbuf::traits_type traits_type;
  typedef std::stringbuf::pos_type pos_type;
  typedef std::stringbuf::off_type off_type;

  // GET
  // int in_avail()
  strmof_1 = strb_01.in_avail();
  strmof_2 = strb_02.in_avail();
  VERIFY( strmof_1 != strmof_2 );
  VERIFY( strmof_1 == str_01.length() );
  VERIFY( strmof_2 == str_02.length() );
  strmof_1 = strb_03.in_avail(); 
  VERIFY( strmof_1 == 0 ); // zero cuz write-only, or eof()? zero, from showmany

  // int_type sbumpc()
  // if read_cur not avail, return uflow(), else return *read_cur & increment
  int_type c1 = strb_01.sbumpc();
  int_type c2 = strb_02.sbumpc();
  VERIFY( c1 != c2 );
  VERIFY( c1 == str_01[0] );
  VERIFY( c2 == str_02[0] ); //should equal first letter at this point
  int_type c3 = strb_01.sbumpc();
  int_type c4 = strb_02.sbumpc();
  VERIFY( c1 != c2 );
  VERIFY( c1 != c3 );
  VERIFY( c2 != c4 );
  int_type c5 = strb_03.sbumpc();
  VERIFY( c5 == traits_type::eof() );

  // int_type sgetc()
  // if read_cur not avail, return uflow(), else return *read_cur  
  int_type c6 = strb_01.sgetc();
  int_type c7 = strb_02.sgetc();
  VERIFY( c6 != c3 );
  VERIFY( c7 != c4 );
  int_type c8 = strb_01.sgetc();
  int_type c9 = strb_02.sgetc();
  VERIFY( c6 == c8 );
  VERIFY( c7 == c9 );
  c5 = strb_03.sgetc();
  VERIFY( c5 == traits_type::eof() );

  // int_type snextc()
  // calls sbumpc and if sbumpc != eof, return sgetc
  c6 = strb_01.snextc();
  c7 = strb_02.snextc();
  VERIFY( c6 != c8 );
  VERIFY( c7 != c9 );
  VERIFY( c6 == str_01[3] );
  VERIFY( c7 == str_02[3] ); //should equal fourth letter at this point
  c5 = strb_03.snextc();
  VERIFY( c5 == traits_type::eof() );

  // int showmanyc
  // streamsize sgetn(char_type *s, streamsize n)
  // streamsize xsgetn(char_type *s, streamsize n)
  // assign up to n chars to s from input sequence, indexing in_cur as
  // approp and returning the number of chars assigned
  strmsz_1 = strb_01.in_avail();
  strmsz_2 = strb_02.in_avail();
  test = strmsz_1 != strmsz_2;
  VERIFY( strmsz_1 != str_01.length() );
  VERIFY( strmsz_2 != str_02.length() ); //because now we've moved into string
  char carray1[11] = "";
  strmsz_1 = strb_01.sgetn(carray1, 10);
  char carray2[20] = "";
  strmsz_2 = strb_02.sgetn(carray2, 10);
  VERIFY( strmsz_1 == strmsz_2 );
  VERIFY( strmsz_1 == 10 );
  c1 = strb_01.sgetc();
  c2 = strb_02.sgetc();
  VERIFY( c6 == c1 ); //just by co-incidence both o's
  VERIFY( c7 != c2 ); // n != i
  VERIFY( c1 == str_01[13] );
  VERIFY( c2 == str_02[13] ); //should equal fourteenth letter at this point
  strmsz_1 = strb_03.sgetn(carray1, 10);
  VERIFY( !strmsz_1 ); //zero
  strmsz_1 = strb_02.in_avail();
  strmsz_2 = strb_02.sgetn(carray2, strmsz_1 + 5);
  VERIFY( strmsz_1 == strmsz_2 ); //write off the end
  c4 = strb_02.sgetc(); // should be EOF
  VERIFY( c4 == traits_type::eof() );

  // PUT
  // int_type sputc(char_type c)
  // if out_cur not avail, return overflow. Else, stores c at out_cur,
  // increments out_cur, and returns c as int_type
  strb_03.str(str_01); //reset
  std::string::size_type sz1 = strb_03.str().length();
  c1 = strb_03.sputc('a'); 
  std::string::size_type sz2 = strb_03.str().length();
  VERIFY( sz1 == sz2 ); //cuz inserting at out_cur, which is at beg to start
  c2 = strb_03.sputc('b'); 
  VERIFY( c1 != c2 );
  VERIFY( strb_03.str() != str_01 );
  c3 = strb_02.sputc('a'); // should be EOF because this is read-only
  VERIFY( c3 == traits_type::eof() );
  
  // streamsize sputn(const char_typs* s, streamsize n)
  // write up to n chars to out_cur from s, returning number assigned
  // NB *sputn will happily put '\0' into your stream if you give it a chance*
  str_tmp = strb_03.str();
  sz1 = str_tmp.length();
  strmsz_1 = strb_03.sputn("racadabras", 10);//"abracadabras or what?"
  sz2 = strb_03.str().length();
  VERIFY( sz1 == sz2 ); //shouldn't have changed length
  VERIFY( strmsz_1 == 10 );
  VERIFY( str_tmp != strb_03.str() );
  strmsz_2 = strb_03.sputn(", i wanna reach out and", 10);
  VERIFY( strmsz_1 == strmsz_2 ); // should re-allocate, copy 10 chars.
  VERIFY( strmsz_1 == 10 );
  VERIFY( strmsz_2 == 10 );
  sz2 = strb_03.str().length();
  VERIFY( sz1 != sz2 ); // need to change length
  VERIFY( str_tmp != strb_03.str() );
  str_tmp = strb_02.str();
  strmsz_1 = strb_02.sputn("racadabra", 10);
  VERIFY( strmsz_1 == 0 );  
  VERIFY( str_tmp == strb_02.str() );

  // PUTBACK
  // int_type pbfail(int_type c)
  // called when gptr() null, gptr() == eback(), or traits::eq(*gptr, c) false
  // "pending sequence" is:
  //	1) everything as defined in underflow
  // 	2) + if (traits::eq_int_type(c, traits::eof()), then input
  // 	sequence is backed up one char before the pending sequence is
  // 	determined.
  //	3) + if (not 2) then c is prepended. Left unspecified is
  //	whether the input sequence is backedup or modified in any way
  // returns traits::eof() for failure, unspecified other value for success

  // int_type sputbackc(char_type c)
  // if in_cur not avail || ! traits::eq(c, gptr() [-1]), return pbfail
  // otherwise decrements in_cur and returns *gptr()
  strmsz_1 = strb_01.in_avail();
  str_tmp = strb_01.str();
  c1 = strb_01.sgetc(); //"mykonos. . . 'o'r what?"
  c2 = strb_01.sputbackc('z');//"mykonos. . .zor what?"
  c3 = strb_01.sgetc();
  VERIFY( c1 != c2 );
  VERIFY( c3 == c2 );
  VERIFY( strb_01.str() == std::string("mykonos. . .zor what?") );
  VERIFY( str_tmp.size() == strb_01.str().size() );
  //test for _in_cur == _in_beg
  strb_01.str(str_tmp);
  strmsz_1 = strb_01.in_avail();
  c1 = strb_01.sgetc(); //"'m'ykonos. . . or what?"
  c2 = strb_01.sputbackc('z');//"mykonos. . . or what?"
  c3 = strb_01.sgetc();
  VERIFY( c1 != c2 );
  VERIFY( c3 != c2 );
  VERIFY( c1 == c3 );
  VERIFY( c2 == traits_type::eof() );
  VERIFY( strb_01.str() == str_tmp );
  VERIFY( str_tmp.size() == strb_01.str().size() );
  // test for replacing char with identical one
  strb_01.str(str_01); //reset
  strmsz_1 = strb_01.in_avail();
  strb_01.sbumpc();
  strb_01.sbumpc();
  c1 = strb_01.sgetc(); //"my'k'onos. . . or what?"
  c2 = strb_01.sputbackc('y');//"mykonos. . . or what?"
  c3 = strb_01.sgetc();
  VERIFY( c1 != c2 );
  VERIFY( c3 == c2 );
  VERIFY( c1 != c3 );
  VERIFY( strb_01.str() == str_01 );
  VERIFY( str_01.size() == strb_01.str().size() );
  //test for ios_base::out
  strmsz_2 = strb_03.in_avail();
  c4 = strb_03.sputbackc('x');
  VERIFY( c4 == traits_type::eof() );

  // int_type sungetc()
  // if in_cur not avail, return pbackfail(), else decrement and
  // return to_int_type(*gptr())
  for (int i = 0; i<12; ++i)
    strb_01.sbumpc();
  strmsz_1 = strb_01.in_avail();
  str_tmp = strb_01.str();
  c1 = strb_01.sgetc(); //"mykonos. . . 'o'r what?"
  c2 = strb_01.sungetc();//"mykonos. . . or what?"
  c3 = strb_01.sgetc();
  VERIFY( c1 != c2 );
  VERIFY( c3 == c2 );
  VERIFY( c1 != c3 );
  VERIFY( c2 == ' ' );
  VERIFY( strb_01.str() == str_01 );
  VERIFY( str_01.size() == strb_01.str().size() );
  //test for _in_cur == _in_beg
  strb_01.str(str_tmp);
  strmsz_1 = strb_01.in_avail();
  c1 = strb_01.sgetc(); //"'m'ykonos. . . or what?"
  c2 = strb_01.sungetc();//"mykonos. . . or what?"
  c3 = strb_01.sgetc();
  VERIFY( c1 != c2 );
  VERIFY( c3 != c2 );
  VERIFY( c1 == c3 );
  VERIFY( c2 == traits_type::eof() );
  VERIFY( strb_01.str() == str_01 );
  VERIFY( str_01.size() == strb_01.str().size() );
  // test for replacing char with identical one
  strb_01.str(str_01); //reset
  strmsz_1 = strb_01.in_avail();
  strb_01.sbumpc();
  strb_01.sbumpc();
  c1 = strb_01.sgetc(); //"my'k'onos. . . or what?"
  c2 = strb_01.sungetc();//"mykonos. . . or what?"
  c3 = strb_01.sgetc();
  VERIFY( c1 != c2 );
  VERIFY( c3 == c2 );
  VERIFY( c1 != c3 );
  VERIFY( strb_01.str() == str_01 );
  VERIFY( str_01.size() == strb_01.str().size() );
  //test for ios_base::out
  strmsz_2 = strb_03.in_avail();
  c4 = strb_03.sungetc();
  VERIFY( c4 == traits_type::eof() );

  // BUFFER MANAGEMENT & POSITIONING
  // sync
  // pubsync
  strb_01.pubsync();
  strb_02.pubsync();
  strb_03.pubsync();

  // setbuf
  // pubsetbuf(char_type* s, streamsize n)
  str_tmp = std::string("naaaah, go to cebu");
  strb_01.pubsetbuf(const_cast<char*> (str_tmp.c_str()), str_tmp.size());
  VERIFY( strb_01.str() == str_tmp );
  strb_01.pubsetbuf(0,0);
  VERIFY( strb_01.str() == str_tmp );

  // seekoff
  // pubseekoff(off_type off, ios_base::seekdir way, ios_base::openmode which)
  // alters the stream position to off
  pos_type pt_1(off_type(-1));
  pos_type pt_2(off_type(0));
  off_type off_1 = 0;
  off_type off_2 = 0;
  strb_01.str(str_01); //in|out ("mykonos. . . or what?");
  strb_02.str(str_02); //in ("paris, or sainte-maxime?");
  strb_03.str(str_03); //out ("")
  //IN|OUT
  //beg
  pt_1 = strb_01.pubseekoff(2, std::ios_base::beg);
  off_1 = pt_1;
  VERIFY( off_1 >= 0 );
  c1 = strb_01.snextc(); //current in pointer +1
  VERIFY( c1 == 'o' );
  c2 = strb_01.sputc('x');  //test current out pointer
  str_tmp = std::string("myxonos. . . or what?");
  VERIFY( strb_01.str() == str_tmp );
  //cur
  pt_1 = strb_01.pubseekoff(2, std::ios_base::cur);
  off_1 = pt_1;
  VERIFY( off_1 == -1 ); // can't seekoff for in and out + cur in sstreams
  pt_1 = strb_01.pubseekoff(2, std::ios_base::cur, std::ios_base::in);
  off_1 = pt_1;
  pt_2 = strb_01.pubseekoff(2, std::ios_base::cur, std::ios_base::in);
  off_2 = pt_2;
  VERIFY( off_2 == off_1 + 2 );
  c1 = strb_01.snextc(); //current in pointer + 1
  VERIFY( c1 == ' ' );
  c2 = strb_01.sputc('x');  //test current out pointer
  str_tmp = std::string("myxxnos. . . or what?");
  VERIFY( strb_01.str() == str_tmp );
  //end
  pt_2 = strb_01.pubseekoff(2, std::ios_base::end);
  off_1 = pt_2;
  VERIFY( off_1 == -1 ); // not a valid position
  VERIFY( strb_01.str() == str_tmp );
  // end part two (from the filebuf tests)
  strb_01.pubseekoff(0, std::ios_base::end);
  strmsz_1 = strb_01.in_avail(); // 0 cuz at the end
  c1 = strb_01.sgetc(); 
  c2 = strb_01.sungetc();
  strmsz_2 = strb_01.in_avail(); // 1
  c3 = strb_01.sgetc();
  VERIFY( c1 != c2 );
  VERIFY( strmsz_2 != strmsz_1 );
  VERIFY( strmsz_2 == 1 );
  // end part three
  strmsz_1 = strb_01.str().size();
  strmsz_2 = strb_01.sputn(" ravi shankar meets carlos santana in LoHa", 90);
  strb_01.pubseekoff(0, std::ios_base::end);
  strb_01.sputc('<');
  str_tmp = strb_01.str();
  VERIFY( str_tmp.size() == strmsz_1 + strmsz_2 + 1 );
  // IN
  // OUT

  // seekpos
  // pubseekpos(pos_type sp, ios_base::openmode)
  // alters the stream position to sp
  strb_01.str(str_01); //in|out ("mykonos. . . or what?");
  strb_02.str(str_02); //in ("paris, or sainte-maxime?");
  strb_03.str(str_03); //out ("")
  //IN|OUT
  //beg
  pt_1 = strb_01.pubseekoff(2, std::ios_base::beg);
  off_1 = pt_1;
  VERIFY( off_1 >= 0 );
  pt_1 = strb_01.pubseekoff(0, std::ios_base::cur, std::ios_base::out);
  off_1 = pt_1;
  c1 = strb_01.snextc(); //current in pointer +1
  VERIFY( c1 == 'o' );
  c2 = strb_01.sputc('x');  //test current out pointer
  str_tmp = std::string("myxonos. . . or what?");
  VERIFY( strb_01.str() == str_tmp );
  strb_01.pubsync(); //resets pointers
  pt_2 = strb_01.pubseekpos(pt_1, std::ios_base::in|std::ios_base::out);
  off_2 = pt_2;
  VERIFY( off_1 == off_2 );
  c3 = strb_01.snextc(); //current in pointer +1
  VERIFY( c1 == c3 );
  c2 = strb_01.sputc('x');  //test current out pointer
  str_tmp = std::string("myxonos. . . or what?");
  VERIFY( strb_01.str() == str_tmp );

  // VIRTUALS (indirectly tested)
  // underflow
  // if read position avail, returns *gptr()

  // pbackfail(int_type c)
  // put c back into input sequence

  // overflow
  // appends c to output seq

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}


// libstdc++/3955 -- ios_base::app overwrites from the beginning
bool test05()
{
  bool test = true;

  std::ostringstream os ("foo");
  os << "bar";

  test = os.str() == "bar";

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}

bool test06()
{
  bool test = true;

  std::ostringstream os ("foo", std::ios_base::app);
  os << "bar";

  test = os.str() == "foobar";

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}

// http://gcc.gnu.org/ml/libstdc++/2003-02/msg00269.html
// Growing and then seeking to ios_base::beg triggered a bug in str(),
// which didn't notice the grow.
bool test07()
{
  bool test = true;

  std::stringbuf strb_01;
  strb_01.sputc('s');
  strb_01.pubseekoff(0, std::ios_base::beg);
  VERIFY( strb_01.str() == "s" );

  std::string str("strivi,");
  std::stringbuf strb_02(str);
  strb_02.pubseekoff(0, std::ios_base::end);
  strb_02.sputn(" no better!", 11);
  strb_02.pubseekoff(0, std::ios_base::beg);
  VERIFY( strb_02.str() == "strivi, no better!" );

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
  test06();
  test07();

  return 0;
}



// more candy!!!
