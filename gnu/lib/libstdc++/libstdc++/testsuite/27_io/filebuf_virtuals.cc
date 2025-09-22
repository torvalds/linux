// 2001-05-21 Benjamin Kosnik  <bkoz@redhat.com>

// Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
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

// 27.8.1.4 Overridden virtual functions

#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <locale>
#include <testsuite_hooks.h>

// @require@ %-*.tst %-*.txt
// @diff@ %-*.tst %*.txt

void test01()
{
  using namespace std;

  bool test = true;
  char buf[512];
  const char* strlit = "how to tell a story and other essays: mark twain";
  const size_t strlitsize = std::strlen(strlit);
  filebuf fbuf01;

  fbuf01.pubsetbuf(buf, 512);
  fbuf01.sputn(strlit, strlitsize);
  VERIFY( std::strncmp(strlit, buf, strlitsize) != 0 );
}

void test02()
{
  using namespace std;

  bool test = true;
  char buf[512];
  const char* strlit = "how to tell a story and other essays: mark twain";
  const size_t strlitsize = std::strlen(strlit);
  filebuf fbuf01;
  fbuf01.open("tmp", ios_base::out);

  fbuf01.pubsetbuf(buf, strlitsize);
  fbuf01.sputn(strlit, strlitsize);
  VERIFY( std::strncmp(strlit, buf, strlitsize) == 0 );
}


// NB: This test assumes that _M_buf_size == 40, and not the usual
// buffer_size length of BUFSIZ (8192), so that overflow/underflow can be
// simulated a bit more readily.
// NRB (Nota Really Bene): setting it to 40 breaks the test, as intended.
const int buffer_size = 8192;
//const int buffer_size = 40;

const char carray_01[] = "santa cruz or sandiego?";
const char carray_02[] = "memphis, new orleans, and savanah";
const char name_01[] = "filebuf_virtuals-1.txt"; // file with data in it
const char name_02[] = "filebuf_virtuals-2.txt"; // empty file, need to create
const char name_03[] = "filebuf_virtuals-3.txt"; // empty file, need to create
const char name_04[] = "filebuf_virtuals-4.txt"; // empty file, need to create
const char name_05[] = "filebuf_virtuals-5.txt"; // empty file, need to create
const char name_06[] = "filebuf_virtuals-6.txt"; // empty file, need to create
const char name_07[] = "filebuf_virtuals-7.txt"; // empty file, need to create

class derived_filebuf: public std::filebuf
{
 public:
  void
  set_size(int_type __size) { _M_buf_size_opt = __size; }
};

derived_filebuf fb_01; // in 
derived_filebuf fb_02; // out
derived_filebuf fb_03; // in | out


// Initialize filebufs to be the same size regardless of platform.
void test03()
{
  fb_01.set_size(buffer_size);
  fb_02.set_size(buffer_size);
  fb_03.set_size(buffer_size);
}


// Test the filebuf/stringbuf locale settings.
void test04() 
{
  std::locale loc_tmp;
  loc_tmp = fb_01.getloc();
  fb_01.pubimbue(loc_tmp); //This should initialize _M_init to true
  fb_01.getloc(); //This should just return _M_locale
}

// Test overloaded virtual functions.
void test05() 
{
  typedef std::filebuf::int_type 	int_type;
  typedef std::filebuf::traits_type 	traits_type;
  typedef std::filebuf::pos_type 	pos_type;
  typedef std::filebuf::off_type 	off_type;
  typedef size_t 			size_type;

  bool 					test = true;
  std::filebuf 				f_tmp;
  std::streamsize 			strmsz_1, strmsz_2;
  std::streamoff  			strmof_1, strmof_2;
  int 					i = 0, j = 0, k = 0;

  // GET
  // int showmanyc()
  // returns an estimate of the numbers of chars in the seq, or -1.
  // if __retval > 0, then calls to underflow won't return
  // traits_type::eof() till at least __retval chars. 
  // if __retval == -1, then calls to underflow or uflow will fail.
  // NB overriding def if it can determine more chars can be read from 
  // the input sequence.

  // int in_avail()
  // if a read position is available, return _M_in_end - _M_in_cur.
  // else return showmanyc.
  strmof_1 = fb_01.in_avail();
  strmof_2 = fb_02.in_avail();
  VERIFY( strmof_1 == -1 );
  VERIFY( strmof_1 == strmof_2 ); //fail because not open
  strmof_1 = fb_03.in_avail();
  VERIFY( strmof_1 == strmof_2 );
  fb_01.open(name_01, std::ios_base::in);
  fb_02.open(name_02, std::ios_base::out | std::ios_base::trunc);
  fb_03.open(name_03, std::ios_base::out | std::ios_base::in | std::ios_base::trunc); 
  strmof_1 = fb_01.in_avail();
  strmof_2 = fb_02.in_avail();
  VERIFY( strmof_1 != strmof_2 );
  VERIFY( strmof_1 >= 0 );
  VERIFY( strmof_2 == -1 );  // empty file
  strmof_1 = fb_03.in_avail(); 
  VERIFY( strmof_1  == 0 ); // empty file

  // int_type sbumpc()
  // if read_cur not avail returns uflow(), else return *read_cur & increment
  int_type c1 = fb_01.sbumpc();
  int_type c2 = fb_02.sbumpc();
  VERIFY( c1 != c2 );
  VERIFY( c1 == '/' );
  VERIFY( c2 == -1 );
  int_type c3 = fb_01.sbumpc();
  int_type c4 = fb_02.sbumpc();
  VERIFY( c3 != c4 );
  VERIFY( c1 == c3 ); // fluke, both happen to be '/'
  VERIFY( c2 == c4 );
  int_type c5 = fb_03.sbumpc();
  VERIFY( c5 == traits_type::eof() );
  // XXX should do some kind of test to make sure that internal
  // buffers point ot the same thing, to check consistancy.

  // int_type sgetc()
  // if read_cur not avail, return uflow(), else return *read_cur  
  int_type c6 = fb_01.sgetc();
  int_type c7 = fb_02.sgetc();
  VERIFY( c6 != c3 );
  VERIFY( c7 == c4 ); // both -1
  int_type c8 = fb_01.sgetc();
  int_type c9 = fb_02.sgetc();
  VERIFY( c6 == c8 );
  VERIFY( c7 == c9 );
  c5 = fb_03.sgetc();
  VERIFY( c5 == traits_type::eof() );

  // int_type snextc()
  // calls sbumpc and if sbumpc != eof, return sgetc
  c6 = fb_01.snextc();
  c7 = fb_02.snextc();
  VERIFY( c6 != c8 );
  VERIFY( c7 == c9 ); // -1
  VERIFY( c6 == '9' );
  c6 = fb_01.snextc();
  c7 = fb_02.snextc();
  VERIFY( c6 != c8 );
  VERIFY( c7 == c9 ); // -1
  VERIFY( c6 == '9' );
  c5 = fb_03.snextc();
  VERIFY( c5 == traits_type::eof() );

  // streamsize sgetn(char_type *s, streamsize n)
  // streamsize xsgetn(char_type *s, streamsize n)
  // assign up to n chars to s from input sequence, indexing in_cur as
  // approp and returning the number of chars assigned
  strmsz_1 = fb_01.in_avail();
  strmsz_2 = fb_02.in_avail();
  test = strmsz_1 != strmsz_2;
  char carray1[13] = "";
  strmsz_1 = fb_01.sgetn(carray1, 10);
  char carray2[buffer_size] = "";
  strmsz_2 = fb_02.sgetn(carray2, 10);
  VERIFY( strmsz_1 != strmsz_2 );
  VERIFY( strmsz_1 == 10 );
  VERIFY( strmsz_2 == 0 );
  c1 = fb_01.sgetc();
  c2 = fb_02.sgetc();
  VERIFY( c1 == '\n' );  
  VERIFY( c7 == c2 ); // n != i
  strmsz_1 = fb_03.sgetn(carray1, 10);
  VERIFY( !strmsz_1 ); //zero
  strmsz_1 = fb_01.in_avail();
  strmsz_2 = fb_01.sgetn(carray2, strmsz_1 + 5);
  VERIFY( strmsz_1 == strmsz_2 - 5 ); 
  c4 = fb_01.sgetc(); // buffer should have underflowed from above.
  VERIFY( c4 == 'i' );
  strmsz_1 = fb_01.in_avail();
  VERIFY( strmsz_1 > 0 );
  strmsz_2 = fb_01.sgetn(carray2, strmsz_1 + 5);
  VERIFY( strmsz_1 == strmsz_2 ); //at the end of the actual file 
  strmsz_1 = fb_02.in_avail();
  strmsz_2 = fb_02.sgetn(carray2, strmsz_1 + 5);
  VERIFY( strmsz_1 == -1 );
  VERIFY( strmsz_2 == 0 );
  c4 = fb_02.sgetc(); // should be EOF
  VERIFY( c4 == traits_type::eof() );

  // PUT
  // int_type sputc(char_type c)
  // if out_cur not avail, return overflow(traits_type::to_int_type(c)) 
  // else, stores c at out_cur,
  // increments out_cur, and returns c as int_type
  // strmsz_1 = fb_03.in_avail();  // XXX valid for in|out??
  c1 = fb_02.sputc('a'); 
  c2 = fb_03.sputc('b'); 
  VERIFY( c1 != c2 );
  c1 = fb_02.sputc('c'); 
  c2 = fb_03.sputc('d'); 
  VERIFY( c1 != c2 );
  // strmsz_2 = fb_03.in_avail();
  // VERIFY( strmsz_1 != strmsz_2 );
  for (int i = 50; i <= 90; ++i) 
    c2 = fb_02.sputc(char(i));
  // 27filebuf-2.txt == ac23456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX
  // fb_02._M_out_cur = '2'
  strmsz_1 = fb_03.in_avail();
  for (int i = 50; i <= 90; ++i) 
    c2 = fb_03.sputc(char(i));
  strmsz_2 = fb_03.in_avail();
  // VERIFY( strmsz_1 != strmsz_2 );
  // VERIFY( strmsz_1 > 0 );
  // VERIFY( strmsz_2 > 0 );
  // 27filebuf-2.txt == bd23456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX
  // fb_02._M_out_cur = '2'
  c3 = fb_01.sputc('a'); // should be EOF because this is read-only
  VERIFY( c3 == traits_type::eof() );

  // streamsize sputn(const char_typs* s, streamsize n)
  // write up to n chars to out_cur from s, returning number assigned
  // NB *sputn will happily put '\0' into your stream if you give it a chance*
  strmsz_1 = fb_03.sputn("racadabras", 10);//"abracadabras or what?"
  VERIFY( strmsz_1 == 10 );
  strmsz_2 = fb_03.sputn(", i wanna reach out and", 10);
  VERIFY( strmsz_2 == 10 );
  VERIFY( strmsz_1 == strmsz_2 ); 
  // fb_03._M_out_beg = "YZracadabras, i wanna FGHIJKLMNOPQRSTUVW"
  // fb_03._M_out_cur = "FGHIJKLMNOPQRSTUVW"
  strmsz_1 = fb_02.sputn("racadabras", 10);
  VERIFY( strmsz_1 == 10 );  
  // fb_02._M_out_beg = "YZracadabras<=>?@ABCDEFGHIJKLMNOPQRSTUVW"
  // fb_02._M_out_cur = "<=>?@ABCDEFGHIJKLMNOPQRSTUVW"
  strmsz_1 = fb_01.sputn("racadabra", 10);
  VERIFY( strmsz_1 == 0 );  

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
  c1 = fb_03.sgetc(); // -1
  c2 = fb_03.sputbackc('z');
  strmsz_2 = fb_03.in_avail();
  c3 = fb_03.sgetc();
  VERIFY( c3 == c2 );
  VERIFY( c1 != c3 );
  VERIFY( 1 == strmsz_2 );
  //test for _in_cur == _in_beg
  // fb_03._M_out_beg = "bd23456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZracada" etc
  fb_03.pubseekoff(10, std::ios_base::beg, 
		   std::ios_base::in | std::ios_base::out);
  fb_03.sputc('m');
  strmsz_1 = fb_03.in_avail(); 
  c1 = fb_03.sgetc(); 
  fb_03.snextc();
  c2 = fb_03.sputbackc('z');  
  strmsz_2 = fb_03.in_avail(); 
  c3 = fb_03.sgetc();  
  VERIFY( c1 != c2 );
  VERIFY( c3 == c2 );
  VERIFY( c1 != c3 );
  VERIFY( c2 == 'z' );
  //  VERIFY( strmsz_1 == strmsz_2 );
  // test for replacing char with identical one
  fb_03.snextc();
  fb_03.sputc('u');
  fb_03.sputc('v');
  fb_03.sputc('a');
  strmsz_1 = fb_03.in_avail();
  c2 = fb_03.sputbackc('a');
  strmsz_2 = fb_03.in_avail();
  c3 = fb_03.sgetc();
  VERIFY( c3 == c2 );
  VERIFY( strmsz_1 + 1 == strmsz_2 );
  //test for ios_base::out
  c1 = fb_02.sgetc(); // undefined
  c2 = fb_02.sputbackc('a');
  VERIFY( c1 == c2 );
  VERIFY( c1 == -1 );

  // int_type sungetc()
  // if in_cur not avail, return pbackfail(), else decrement and
  // return to_int_type(*gptr())
  // fb_03._M_out_beg = "uvaacadabras, i wannaZ[\\]^_`abcdefghijkl"
  // fb_03._M_out_cur = "aacadabras, i wannaZ[\\]^_`abcdefghijkl"
  strmsz_1 = fb_03.in_avail();
  c2 = fb_03.sungetc(); // delete the 'a'
  strmsz_2 = fb_03.in_avail();
  VERIFY( c2 == 'v' ); //  VERIFY( c2 != traits_type::eof() );
  VERIFY( strmsz_1 + 1 == strmsz_2 );
  //test for _in_cur == _in_beg
  for (int i = 50; i < 32 + 29; ++i)
    fb_02.sputc(char(i));
  fb_02.pubseekoff(0, std::ios_base::beg, std::ios_base::out);
  c1 = fb_02.sgetc(); 
  strmsz_1 = fb_02.in_avail();
  c2 = fb_02.sungetc();
  c3 = fb_02.sgetc();
  strmsz_2 = fb_02.in_avail();
  VERIFY( c1 == c2 );
  VERIFY( c2 == c3 );
  VERIFY( c1 == traits_type::eof() );
  VERIFY( strmsz_1 == strmsz_2 );
  //test for _in_cur == _in_end
  fb_03.pubseekoff(0, std::ios_base::end);
  strmsz_1 = fb_03.in_avail(); // -1 cuz at the end
  c1 = fb_03.sgetc(); 
  c2 = fb_03.sungetc();
  strmsz_2 = fb_03.in_avail(); // 1
  c3 = fb_03.sgetc();
  VERIFY( c1 != c2 );
  // VERIFY( c2 == c3 || c2 == traits_type::not_eof(int(c3)) );
  VERIFY( strmsz_2 != strmsz_1 );
  VERIFY( strmsz_2 == 1 );
  //test for ios_base::out

  // BUFFER MANAGEMENT & POSITIONING
  // int sync()
  // if a put area exists, overflow. 
  // if a get area exists, do something undefined. (like, nothing)
  strmsz_1 = fb_01.in_avail(); 
  fb_01.pubsync();
  strmsz_2 = fb_01.in_avail();
  VERIFY( strmsz_2 == strmsz_1 );
  strmsz_1 = fb_02.in_avail(); 
  fb_02.pubsync();		
  // 27filebuf-2.txt == 53 bytes after this.
  strmsz_2 = fb_02.in_avail();
  VERIFY( strmsz_2 == -1 );
  VERIFY( strmsz_2 == strmsz_1 );
  strmsz_1 = fb_03.in_avail(); 
  fb_03.pubsync();
  // 27filebuf-3.txt 
  // bd23456789mzuva?@ABCDEFGHIJKLMNOPQRSTUVWXYZracadabras, i wannaz 
  // 63 bytes.
  strmsz_2 = fb_03.in_avail();
  VERIFY( strmsz_1 == 1 );
  // VERIFY( strmsz_2 == 1 );

  // setbuf
  // pubsetbuf(char_type* s, streamsize n)
  fb_01.pubsetbuf(0,0);
  fb_02.pubsetbuf(0,0);
  fb_03.pubsetbuf(0,0);
  // Need to test unbuffered output, which means calling this on some
  // things that have just been opened.


  // seekoff
  // pubseekoff(off_type off, ios_base::seekdir way, ios_base::openmode which)
  // alters the stream position to off
  pos_type pt_1(off_type(-1));
  pos_type pt_2(off_type(0));
  off_type off_1 = 0;
  off_type off_2 = 0;
  //IN|OUT
  // 27filebuf-3.txt = bd23456789:;<=>?...
  //beg
  strmsz_1 = fb_03.in_avail(); 
  pt_1 = fb_03.pubseekoff(2, std::ios_base::beg);
  strmsz_2 = fb_03.in_avail(); 
  off_1 = pt_1;
  VERIFY( off_1 > 0 );
  c1 = fb_03.snextc(); //current in pointer +1
  VERIFY( c1 == '3' );
  c2 = fb_03.sputc('\n');  //current in pointer +1
  c3 = fb_03.sgetc();
  VERIFY( c2 != c3 ); 
  VERIFY( c3 == '4' );
  fb_03.pubsync(); 
  c1 = fb_03.sgetc();
  VERIFY( c1 == c3 );
  //cur
  // 27filebuf-3.txt = bd2\n456789:;<=>?...
  pt_2 = fb_03.pubseekoff(2, std::ios_base::cur);
  off_2 = pt_2;
  VERIFY( (off_2 == (off_1 + 2 + 1 + 1)) );
  c1 = fb_03.snextc(); //current in pointer +1
  VERIFY( c1 == '7' );
  c2 = fb_03.sputc('x');  //test current out pointer
  c3 = fb_03.sputc('\n');
  c1 = fb_03.sgetc();
  fb_03.pubsync(); 
  c3 = fb_03.sgetc();
  VERIFY( c1 == c3 );
  //end
  // 27filebuf-3.txt = "bd2\n456x\n9" 
  pt_2 = fb_03.pubseekoff(0, std::ios_base::end, 
			  std::ios_base::in|std::ios_base::out);
  off_1 = pt_2;
  VERIFY( off_1 > off_2 ); //weak, but don't know exactly where it ends
  c3 = fb_03.sputc('\n');
  strmsz_1 = fb_03.sputn("because because because. . .", 28);  
  VERIFY( strmsz_1 == 28 );
  c1 = fb_03.sungetc();
  // Defect?  retval of sungetc is not necessarily the character ungotten.
  // So re-get it.
  c1 = fb_03.sgetc();
  fb_03.pubsync(); 
  c3 = fb_03.sgetc();
  VERIFY( c1 == c3 );
  // IN
  // OUT


  // seekpos
  // pubseekpos(pos_type sp, ios_base::openmode)
  // alters the stream position to sp
  //IN|OUT
  //beg
  pt_1 = fb_03.pubseekoff(78, std::ios_base::beg);
  off_1 = pt_1;
  VERIFY( off_1 > 0 );
  c1 = fb_03.snextc(); 		//current in pointer +1
  VERIFY( c1 == ' ' );
  c2 = fb_03.sputc('\n');  	//test current out pointer
  c3 = fb_03.sgetc();
  fb_03.pubsync(); 		//resets pointers
  pt_2 = fb_03.pubseekpos(pt_1);
  off_2 = pt_2;
  VERIFY( off_1 == off_2 );
  c3 = fb_03.snextc(); 		//current in pointer +1
  VERIFY( c2 == c3 );
  pt_1 = fb_03.pubseekoff(0, std::ios_base::end);
  off_1 = pt_1;
  VERIFY( off_1 > off_2 );
  fb_03.sputn("\nof the wonderful things he does!!\nok", 37);
  fb_03.pubsync();

  // IN
  // OUT

  // VIRTUALS (indirectly tested)
  // underflow
  // if read position avail, returns *gptr()

  // pbackfail(int_type c)
  // put c back into input sequence

  // overflow
  // appends c to output seq

  // NB Have to close these suckers. . .
  // filebuf_type* close()
  fb_01.close();
  fb_02.close();
  fb_03.close();
  VERIFY( !fb_01.is_open() );
  VERIFY( !fb_02.is_open() );
  VERIFY( !fb_03.is_open() );
}

void test06()
{
  using namespace std;
  typedef istream::int_type	int_type;

  bool test = true;
  ifstream ifs(name_02);
  char buffer[] = "xxxxxxxxxx";
  int_type len1 = ifs.rdbuf()->sgetn(buffer, sizeof(buffer));
  VERIFY( len1 == sizeof(buffer) );
  VERIFY( buffer[0] == 'a' );
}

// libstdc++/9322
void test07()
{
  using std::locale;
  bool test = true;

  locale loc;
  std::filebuf ob;
  VERIFY( ob.getloc() == loc );

  locale::global(locale("en_US"));
  VERIFY( ob.getloc() == loc );

  locale loc_de ("de_DE");
  locale ret = ob.pubimbue(loc_de);
  VERIFY( ob.getloc() == loc_de );
  VERIFY( ret == loc );

  locale::global(loc);
  VERIFY( ob.getloc() == loc_de );
}

class MyTraits : public std::char_traits<char>
{
public:
  static bool eq(char c1, char c2)
  {
    VERIFY( c1 != 'X' );
    VERIFY( c2 != 'X' );
    return std::char_traits<char>::eq(c1, c2);
  }
};

class MyBuf : public std::basic_streambuf<char, MyTraits>
{
  char buffer[8];

public:
  MyBuf()
  {
    std::memset(buffer, 'X', sizeof(buffer));
    std::memset(buffer + 2, 'f', 4);
    setg(buffer + 2, buffer + 2, buffer + 6);
  }
};

// libstdc++/9538
void test08()
{
  bool test = true;

  MyBuf mb;
  mb.sputbackc('a');  
}

// libstdc++/9439, libstdc++/9425
void test09()
{
  using namespace std;
  bool test = true;

  filebuf fbuf;
  fbuf.open(name_01, ios_base::in);
  filebuf::int_type r = fbuf.sputbackc('a');
  fbuf.close();

  VERIFY( r == filebuf::traits_type::eof() );
}

class Cvt_to_upper : public std::codecvt<char, char, mbstate_t>
{
  bool do_always_noconv() const throw()
  {
    return false;
  }
};

// libstdc++/9169
void test10()
{
  using namespace std;
  bool test = true;

  locale c_loc;
  locale loc(c_loc, new Cvt_to_upper);

  string str("abcdefghijklmnopqrstuvwxyz");
  string tmp;

  {
    ofstream out;
    out.imbue(loc);
    out.open(name_04);
    copy(str.begin(), str.end(),
	 ostreambuf_iterator<char>(out));
  }

  {
    ifstream in;
    in.open(name_04);
    copy(istreambuf_iterator<char>(in),
	 istreambuf_iterator<char>(),
	 back_inserter(tmp));
  }

  VERIFY( tmp.size() == str.size() );
  VERIFY( tmp == str );
}

// libstdc++/9825
void test11()
{
  using namespace std;
  bool test = true;

  filebuf fbuf;

  fbuf.open(name_05, ios_base::in|ios_base::out|ios_base::trunc);
  fbuf.sputn("crazy bees!", 11);
  fbuf.pubseekoff(0, ios_base::beg);
  fbuf.sbumpc();
  fbuf.sputbackc('x');
  filebuf::int_type c = fbuf.sbumpc();
  VERIFY( c == 'x' );
  c = fbuf.sbumpc();
  VERIFY( c == 'r' );
  c = fbuf.sbumpc();
  VERIFY( c == 'a' );
  fbuf.close();  
}

class errorcvt : public std::codecvt<char, char, mbstate_t>
{
protected:
  std::codecvt_base::result
  do_out(mbstate_t&, const char* from, const char*,
	 const char*& from_next, char* to, char*,
	 char*& to_next) const
  {
    from_next = from;
    to_next = to;
    return std::codecvt<char, char, mbstate_t>::error;
  }
  
  virtual bool do_always_noconv() const throw()
  {
    return false;
  }
};

// libstdc++/9182
void test12()
{
  using namespace std;
  bool test = true;

  locale loc;
  loc = locale(loc, new errorcvt);
  
  filebuf fbuf1;
  fbuf1.pubimbue(loc);
  fbuf1.open(name_06, ios_base::out | ios_base::trunc);
  fbuf1.sputn("ison", 4); 
  int r = fbuf1.pubsync();
  VERIFY( r == -1 );
  fbuf1.close();
}

void test13()
{
  using namespace std;
  bool test = true;
  
  locale loc;
  loc = locale(loc, new errorcvt);
  
  filebuf fbuf1;
  fbuf1.pubimbue(loc);
  fbuf1.pubsetbuf(0, 0);
  fbuf1.open(name_06, ios_base::out | ios_base::trunc);
  streamsize n = fbuf1.sputn("onne", 4);
  VERIFY( n == 0 );
  fbuf1.close();
}

class OverBuf : public std::filebuf
{
public:
  int_type pub_overflow(int_type c = traits_type::eof())
  { return std::filebuf::overflow(c); }
};

// libstdc++/9988
void test14()
{
  using namespace std;
  bool test = true;
  
  OverBuf fb;
  fb.open(name_07, ios_base::out | ios_base::trunc);
  
  fb.sputc('a');
  fb.pub_overflow('b');
  fb.pub_overflow();
  fb.sputc('c');
  fb.close();

  filebuf fbin;
  fbin.open(name_07, ios_base::in);
  filebuf::int_type c;
  c = fbin.sbumpc();
  VERIFY( c == 'a' );
  c = fbin.sbumpc();
  VERIFY( c == 'b' );
  c = fbin.sbumpc();
  VERIFY( c == 'c' );
  c = fbin.sbumpc();
  VERIFY( c == filebuf::traits_type::eof() );
  fbin.close();
}

class UnderBuf : public std::filebuf
{
public:
  int_type
  pub_underflow()
  { return underflow(); }

  std::streamsize
  pub_showmanyc()
  { return showmanyc(); }
};

// libstdc++/10097
void test15()
{
  using namespace std;
  bool test = true;

  const char* name = "tmp_fifo1";
  
  signal(SIGPIPE, SIG_IGN);
  unlink(name);
  
  if (0 != mkfifo(name, S_IRWXU))
    {
      VERIFY( false );
    }
  
  int fval = fork();
  if (fval == -1)
    {
      unlink(name);
      VERIFY( false );
    }
  else if (fval == 0)
    {
      filebuf fbout;
      fbout.open(name, ios_base::out);
      fbout.sputn("0123456789", 10);
      fbout.pubsync();
      sleep(2);
      fbout.close();
      exit(0);
    }

  UnderBuf fb;
  fb.open(name, ios_base::in);
  sleep(1);
  
  fb.sgetc();
  streamsize n = fb.pub_showmanyc();

  while (n > 0)
    {
      --n;
      
      UnderBuf::int_type c = fb.pub_underflow();
      VERIFY( c != UnderBuf::traits_type::eof() );
      
      fb.sbumpc();
    }

  fb.close();
}

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
  test14();
  test15();
  return 0;
}
