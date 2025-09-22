// 1999-10-11 bkoz

// Copyright (C) 1999, 2000, 2001, 2002, 2003 Free Software Foundation, Inc.
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

// 27.5.2 template class basic_streambuf

#include <cstring> // for memset, memcmp
#include <streambuf>
#include <sstream>
#include <ostream>
#include <testsuite_hooks.h>

class testbuf : public std::streambuf
{
public:

  // Typedefs:
  typedef std::streambuf::traits_type traits_type;
  typedef std::streambuf::char_type char_type;

  testbuf(): std::streambuf() 
  { _M_mode = (std::ios_base::in | std::ios_base::out); }

  bool
  check_pointers()
  { 
    bool test = true;
    VERIFY( this->eback() == NULL );
    VERIFY( this->gptr() == NULL );
    VERIFY( this->egptr() == NULL );
    VERIFY( this->pbase() == NULL );
    VERIFY( this->pptr() == NULL );
    VERIFY( this->epptr() == NULL );
    return test;
  }

  int_type 
  pub_uflow() 
  { return (this->uflow()); }

  int_type 
  pub_overflow(int_type __c = traits_type::eof()) 
  { return (this->overflow(__c)); }

  int_type 
  pub_pbackfail(int_type __c) 
  { return (this->pbackfail(__c)); }

  void 
  pub_setg(char* beg, char* cur, char *end) 
  { this->setg(beg, cur, end); }

  void 
  pub_setp(char* beg, char* end) 
  { this->setp(beg, end); }

protected:
  int_type 
  underflow() 
  { 
    int_type __retval = traits_type::eof();
    if (this->gptr() < this->egptr())
      __retval = traits_type::not_eof(0); 
    return __retval;
  }
};

void test01()
{
  typedef testbuf::traits_type traits_type;
  typedef testbuf::int_type int_type;

  bool test = true;
  char* lit01 = "chicago underground trio/possible cube on delmark";
  testbuf buf01;

  // 27.5.2.1 basic_streambuf ctors
  // default ctor initializes 
  // - all pointer members to null pointers
  // - locale to current global locale
  VERIFY( buf01.check_pointers() );
  VERIFY( buf01.getloc() == std::locale() );

  // 27.5.2.3.1 get area
  // 27.5.2.2.3 get area
  // 27.5.2.4.3 get area
  int i01 = 3;
  buf01.pub_setg(lit01, lit01, (lit01 + i01));
  VERIFY( i01 == buf01.in_avail() );

  VERIFY( buf01.pub_uflow() == lit01[0] );
  VERIFY( buf01.sgetc() == traits_type::to_int_type(lit01[1]) );
  VERIFY( buf01.pub_uflow() == lit01[1] );
  VERIFY( buf01.sgetc() == traits_type::to_int_type(lit01[2]) );
  VERIFY( buf01.pub_uflow() == lit01[2] );
  VERIFY( buf01.sgetc() == traits_type::eof() );

  // pbackfail
  buf01.pub_setg(lit01, lit01, (lit01 + i01));
  VERIFY( i01 == buf01.in_avail() );
  int_type intt01 = traits_type::to_int_type('b');
  VERIFY( traits_type::eof() == buf01.pub_pbackfail(intt01) );

  // overflow
  VERIFY( traits_type::eof() == buf01.pub_overflow(intt01) );
  VERIFY( traits_type::eof() == buf01.pub_overflow() );
  VERIFY( buf01.sgetc() == traits_type::to_int_type(lit01[0]) );

  // sputn/xsputn
  char* lit02 = "isotope 217: the unstable molecule on thrill jockey";
  int i02 = std::strlen(lit02);
  char carray[i02 + 1];
  std::memset(carray, 0, i02 + 1);

  buf01.pub_setp(carray, (carray + i02));
  buf01.sputn(lit02, 0);
  VERIFY( carray[0] == 0 );
  VERIFY( lit02[0] == 'i' );
  buf01.sputn(lit02, 1);
  VERIFY( lit02[0] == carray[0] );
  VERIFY( lit02[1] == 's' );
  VERIFY( carray[1] == 0 );
  buf01.sputn(lit02 + 1, 10);
  VERIFY( std::memcmp(lit02, carray, 10) == 0 );
  buf01.sputn(lit02 + 11, 20);
  VERIFY( std::memcmp(lit02, carray, 30) == 0 );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}

void test02()
{
  typedef testbuf::traits_type traits_type;
  typedef testbuf::int_type int_type;

  bool test = true;
  char* lit01 = "chicago underground trio/possible cube on delmark";
  testbuf buf01;

  // 27.5.2.1 basic_streambuf ctors
  // default ctor initializes 
  // - all pointer members to null pointers
  // - locale to current global locale
  VERIFY( buf01.check_pointers() );
  VERIFY( buf01.getloc() == std::locale() );

  // 27.5.2.2.5 Put area
  size_t i01 = traits_type::length(lit01);
  char carray01[i01];
  std::memset(carray01, 0, i01);
  
  buf01.pub_setg(lit01, lit01, lit01 + i01);
  buf01.sgetn(carray01, 0);
  VERIFY( carray01[0] == 0 );
  buf01.sgetn(carray01, 1);
  VERIFY( carray01[0] == 'c' );
  buf01.sgetn(carray01 + 1, i01 - 1);
  VERIFY( carray01[0] == 'c' );
  VERIFY( carray01[1] == 'h' );
  VERIFY( carray01[i01 - 1] == 'k' );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}
 
// test03
// http://gcc.gnu.org/ml/libstdc++/2000-q1/msg00151.html
template<typename charT, typename traits = std::char_traits<charT> >
  class basic_nullbuf : public std::basic_streambuf<charT, traits>
  {
  protected:
    typedef typename
      std::basic_streambuf<charT, traits>::int_type int_type;
    virtual int_type 
    overflow(int_type c) 
    {  return traits::not_eof(c); }
  };

typedef basic_nullbuf<char> nullbuf;
typedef basic_nullbuf<wchar_t> wnullbuf;

template<typename T>
  char
  print(const T& x) 
  {
   nullbuf ob;
   std::ostream out(&ob); 
   out << x << std::endl;
   return (!out ? '0' : '1');
 }

void test03() 
{
  bool test = true;
  const std::string control01("11111");
  std::string test01;

  test01 += print(true);
  test01 += print(3.14159);
  test01 += print(10);
  test01 += print('x');
  test01 += print("pipo");

  VERIFY( test01 == control01 );
#ifdef DEBUG_ASSERT
  assert(test);
#endif
}

class setpbuf : public std::streambuf
{
  char 		buffer[4];
  std::string 	result;

public:

  std::string&
  get_result()
  { return result; }

  setpbuf()
  {
    char foo [32];
    setp(foo, foo + 32);
    setp(buffer, buffer + 4);
  }

  ~setpbuf()
  { sync(); }

  virtual int_type 
  overflow(int_type n)
  {
    if (sync() != 0)
      return traits_type::eof();
    
    result += traits_type::to_char_type(n);
    
    return n;
  }
  
  virtual int 
  sync()
  {
    result.append(pbase(), pptr());
    setp(buffer, buffer + 4);
    return 0;
  }
};

// libstdc++/1057
void test04()
{
  bool test = true;
  std::string text = "abcdefghijklmn";
  
  // 01
  setpbuf sp1;
  // Here xsputn writes over sp1.result
  sp1.sputn(text.c_str(), text.length());

  // This crashes when result is accessed
  sp1.pubsync();
  VERIFY( sp1.get_result() == text );
  

  // 02
  setpbuf sp2;
  for (std::string::size_type i = 0; i < text.length(); ++i)
    {
      // sputc also writes over result
      sp2.sputc(text[i]);
    }
  
  // Crash here
  sp2.pubsync();
  VERIFY( sp2.get_result() == text );
}

class nullsetpbuf : public std::streambuf
{
  char foo[64];
public:
  nullsetpbuf()
  {
    setp(foo, foo + 64);
    setp(NULL, NULL);
  }
};

// libstdc++/1057
void test05()
{
    std::string text1 = "abcdefghijklmn";

    nullsetpbuf nsp;
    // Immediate crash as xsputn writes to null pointer
    nsp.sputn(text1.c_str(), text1.length());
    // ditto
    nsp.sputc('a');
}

// test06
namespace gnu 
{
  class something_derived;
}

class gnu::something_derived : std::streambuf { };

// libstdc++/3599
class testbuf2 : public std::streambuf
{
public:
  typedef std::streambuf::traits_type traits_type;

  testbuf2() : std::streambuf() { }
 
protected:
  int_type 
  overflow(int_type c = traits_type::eof()) 
  { return traits_type::not_eof(0); }
};

void
test07()
{
  bool test = true;
  testbuf2 ob;
  std::ostream out(&ob); 

  out << "gasp";
  VERIFY(out.good());

  out << std::endl;
  VERIFY(out.good());
}

// libstdc++/9322
void test08()
{
  using std::locale;
  bool test = true;

  locale loc;
  testbuf2 ob;
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

// libstdc++/9318
class Outbuf : public std::streambuf
{
public:
  typedef std::streambuf::traits_type traits_type;

  std::string result() const { return str; }

protected:
  virtual int_type overflow(int_type c = traits_type::eof())
  {
    if (!traits_type::eq_int_type(c, traits_type::eof()))
      str.push_back(traits_type::to_char_type(c));
    return traits_type::not_eof(c);
  }

private:
  std::string str;
};

// <1>
void test09()
{
  bool test = true;
  
  std::istringstream stream("Bad Moon Rising");
  Outbuf buf;
  stream >> &buf;

  VERIFY( buf.result() == "Bad Moon Rising" );
}

// <2>
void test10()
{
  bool test = true;

  std::stringbuf sbuf("Bad Moon Rising", std::ios::in);
  Outbuf buf;
  std::ostream stream(&buf);
  stream << &sbuf;

  VERIFY( buf.result() == "Bad Moon Rising" );
}

// libstdc++/9424
class Outbuf_2 : public std::streambuf
{
  char buf[1];

public:
  Outbuf_2()
  {
    setp(buf, buf + 1);
  }

  int_type overflow(int_type c)
  {
    int_type eof = traits_type::eof();
    
    if (pptr() < epptr())
      {
	if (traits_type::eq_int_type(c, eof))
	  return traits_type::not_eof(c);
	
	*pptr() = traits_type::to_char_type(c);
	pbump(1);
	return c;
      }

    return eof;
  }
};

class Inbuf_2 : public std::streambuf
{
  static const char buf[];
  const char* current;
  int size;

public:
  Inbuf_2()
  {
    current = buf;
    size = std::strlen(buf);
  }
  
  int_type underflow()
  {
    if (current < buf + size)
      return traits_type::to_int_type(*current);
    return traits_type::eof();
  }
  
  int_type uflow()
  {
    if (current < buf + size)
      return traits_type::to_int_type(*current++);
    return traits_type::eof();
  }
};

const char Inbuf_2::buf[] = "Atteivlis";

// <1>
void test11()
{
  bool test = true;

  Inbuf_2 inbuf1;
  std::istream is(&inbuf1);
  Outbuf_2 outbuf1;
  is >> &outbuf1;
  VERIFY( inbuf1.sgetc() == 't' );
  VERIFY( is.good() );
}

// <2>
void test12()
{ 
  bool test = true;
 
  Outbuf_2 outbuf2;
  std::ostream os (&outbuf2);
  Inbuf_2 inbuf2;
  os << &inbuf2;
  VERIFY( inbuf2.sgetc() == 't' );
  VERIFY( os.good() );
}

int main() 
{
  test01();
  test02();
  test03();

  test04();
  test05();

  test07();
  test08();

  test09();
  test10();
  test11();
  test12();
  return 0;
}
