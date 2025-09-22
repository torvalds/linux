// 2001-05-23 Benjamin Kosnik  <bkoz@redhat.com>

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

// 27.7.3.2 member functions (ostringstream_members)

#include <sstream>
#include <testsuite_hooks.h>

void test01()
{
  bool test = true;
  std::ostringstream os01;
  const std::string str00; 
  const std::string str01 = "123";
  std::string str02;
  const int i01 = 123;
  int a,b;

  std::ios_base::iostate state1, state2, statefail, stateeof;
  statefail = std::ios_base::failbit;
  stateeof = std::ios_base::eofbit;

  // string str() const
  str02 = os01.str();
  VERIFY( str00 == str02 );

  // void str(const basic_string&)
  os01.str(str01);
  str02 = os01.str();
  VERIFY( str01 == str02 );

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

  std::ostringstream sstrm1;
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

// 03: sanity checks for strings, stringbufs
void
test03()
{
  bool test = false;

  // Empty string sanity check.
  std::string str01;
  std::string::iterator __i_start = str01.begin();
  std::string::iterator __i_end = str01.end();
  std::string::size_type len = str01.size();
  test = __i_start == __i_end;
  VERIFY( len == 0 );

  // Full string sanity check.
  std::string str02("these golden days, i spend waiting for you:\n"
		    "Betty Carter on Verve with I'm Yours and You're Mine.");
  __i_start = str02.begin();
  __i_end = str02.end();
  len = str02.size();
  VERIFY( __i_start != __i_end );
  VERIFY( len != 0 );
 
  // Test an empty ostringstream for sanity.
  std::ostringstream ostrstream0;
  std::string str03 = ostrstream0.str();
  __i_start = str03.begin();
  __i_end = str03.end();
  len = str03.size();
  VERIFY( __i_start == __i_end );
  VERIFY( len == 0 );
  VERIFY( str01 == str03 );
}

// user-reported error
class derived_oss: public std::ostringstream 
{
public:
  derived_oss() : std::ostringstream() { }
};

void
test04()
{
  bool test = true;
  derived_oss yy;
  yy << "buena vista social club\n";
  VERIFY( yy.str() == std::string("buena vista social club\n") );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  return 0;
}
