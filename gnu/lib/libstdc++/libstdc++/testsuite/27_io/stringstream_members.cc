// 2001-05-24 Benjamin Kosnik  <bkoz@redhat.com>

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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

// 27.7.6 member functions (stringstream_members)

#include <sstream>
#include <testsuite_hooks.h>

void test01()
{
  bool test = true;
  std::stringstream is01;
  const std::string str00; 
  const std::string str01 = "123";
  std::string str02;
  const int i01 = 123;
  int a,b;

  std::ios_base::iostate state1, state2, statefail, stateeof;
  statefail = std::ios_base::failbit;
  stateeof = std::ios_base::eofbit;

  // string str() const
  str02 = is01.str();
  VERIFY( str00 == str02 );

  // void str(const basic_string&)
  is01.str(str01);
  str02 = is01.str();
  VERIFY( str01 == str02 );
  state1 = is01.rdstate();
  is01 >> a;
  state2 = is01.rdstate();
  VERIFY( a = i01 );
  // 22.2.2.1.2 num_get virtual functions
  // p 13
  // in any case, if stage 2 processing was terminated by the test for
  // in == end then err != ios_base::eofbit is performed.
  VERIFY( state1 != state2 );
  VERIFY( state2 == stateeof ); 

  is01.str(str01);
  is01 >> b;
  VERIFY( b != a ); 
  // as is01.good() is false, istream::sentry blocks extraction.

  is01.clear();
  state1 = is01.rdstate();
  is01 >> b;
  state2 = is01.rdstate();
  VERIFY( b == a ); 
  VERIFY( state1 != state2 );
  VERIFY( state2 == stateeof ); 

 #ifdef DEBUG_ASSERT
  assert(test);
#endif
}

void 
redirect_buffer(std::ios& stream, std::streambuf* new_buf) 
{ stream.rdbuf(new_buf); }

std::streambuf*
active_buffer(std::ios& stream)
{ return stream.rdbuf(); }

// libstdc++/2832
void test02()
{
  bool test = true;
  const char* strlit01 = "fuck war";
  const char* strlit02 = "two less cars abstract riot crew, critical mass/SF";
  const std::string str00;
  const std::string str01(strlit01);
  std::string str02;
  std::stringbuf sbuf(str01);
  std::streambuf* pbasebuf0 = &sbuf;

  std::stringstream sstrm1;
  VERIFY( sstrm1.str() == str00 );
  // derived rdbuf() always returns original streambuf, even though
  // it's no longer associated with the stream.
  std::stringbuf* const buf1 = sstrm1.rdbuf();
  // base rdbuf() returns the currently associated streambuf
  std::streambuf* pbasebuf1 = active_buffer(sstrm1);
  redirect_buffer(sstrm1, &sbuf);
  std::stringbuf* const buf2 = sstrm1.rdbuf();
  std::streambuf* pbasebuf2 = active_buffer(sstrm1);
  VERIFY( buf1 == buf2 ); 
  VERIFY( pbasebuf1 != pbasebuf2 );
  VERIFY( pbasebuf2 == pbasebuf0 );

  // derived rdbuf() returns the original buf, so str() doesn't change.
  VERIFY( sstrm1.str() != str01 );
  VERIFY( sstrm1.str() == str00 );
  // however, casting the active streambuf to a stringbuf shows what's up:
  std::stringbuf* psbuf = dynamic_cast<std::stringbuf*>(pbasebuf2);
  str02 = psbuf->str();
  VERIFY( str02 == str01 );

  // How confusing and non-intuitive is this?
  // These semantics are a joke, a serious defect, and incredibly lame.
}

void
test03()
{
  bool test = true;

  //
  // 1: Automatic formatting of a compound string
  //
  int i = 1024;
  int *pi = &i;
  double d = 3.14159;
  double *pd = &d;
  std::string blank;
  std::ostringstream ostrst01; 
  std::ostringstream ostrst02(blank); 
  
  // No buffer, so should be created.
  ostrst01 << "i: " << i << " i's address:  " << pi << "\n"
	     << "d: " << d << " d's address: " << pd << std::endl;
  // Buffer, so existing buffer should be overwritten.
  ostrst02 << "i: " << i << " i's address:  " << pi << "\n"
	     << "d: " << d << " d's address: " << pd << std::endl;

  std::string msg01 = ostrst01.str();
  std::string msg02 = ostrst02.str();
  VERIFY( msg01 == msg02 );
  VERIFY( msg02 != blank );

  //
  // 2: istringstream
  //
  // extracts the stored ascii values, placing them in turn in the four vars
#if 0
  int i2 = 0;
  //int* pi2 = &i2;
  void* pi2 = &i2;
  double d2 = 0.0;
  //  double* pd2 = &d2;
  void* pd2 = &d2;
  std::istringstream istrst01(ostrst02.str());

  istrst01 >> i2 >> pi2 >> d2 >> pd2;
  //istrst01 >> i2;
  //istrst01 >> pi2;
  VERIFY( i2 == i );
  VERIFY( d2 == d );
  VERIFY( pd2 == pd );
  VERIFY( pi2 == pi );
#endif

  // stringstream
  std::string str1("");
  std::string str3("this is a somewhat  string");
  std::stringstream ss1(str1, std::ios_base::in|std::ios_base::out);
  std::stringstream ss2(str3, std::ios_base::in|std::ios_base::out);
}

// libstdc++/8466
void test04()
{
  bool test = true;

  const char* strlit00 = "orvieto";
  const std::string str00 = strlit00;

  std::ostringstream oss;

  oss.str(str00);
  oss << "cortona";
  VERIFY( str00 == strlit00 );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  return 0;
}
