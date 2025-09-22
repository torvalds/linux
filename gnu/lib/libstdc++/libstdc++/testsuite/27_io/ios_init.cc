// 2001-06-05 Benjamin Kosnik  <bkoz@redhat.com>

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

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// 27.4.2.1.6 class ios_base::init

#include <fstream>
#include <sstream>
#include <iostream>
#include <testsuite_hooks.h>

// char_traits specialization
namespace std
{
  template<>
    struct char_traits<unsigned short>
    {
      typedef unsigned short 	char_type;
      // Unsigned as wint_t in unsigned.
      typedef unsigned long  	int_type;
      typedef streampos 	pos_type;
      typedef streamoff 	off_type;
      typedef mbstate_t 	state_type;
      
      static void 
      assign(char_type& __c1, const char_type& __c2)
      { __c1 = __c2; }

      static bool 
      eq(const char_type& __c1, const char_type& __c2)
      { return __c1 == __c2; }

      static bool 
      lt(const char_type& __c1, const char_type& __c2)
      { return __c1 < __c2; }

      static int 
      compare(const char_type* __s1, const char_type* __s2, size_t __n)
      { 
	for (size_t __i = 0; __i < __n; ++__i)
	  if (!eq(__s1[__i], __s2[__i]))
	    return lt(__s1[__i], __s2[__i]) ? -1 : 1;
	return 0; 
      }

      static size_t
      length(const char_type* __s)
      { 
	const char_type* __p = __s; 
	while (__p) 
	  ++__p; 
	return (__p - __s); 
      }

      static const char_type* 
      find(const char_type* __s, size_t __n, const char_type& __a)
      { 
	for (const char_type* __p = __s; size_t(__p - __s) < __n; ++__p)
	  if (*__p == __a) return __p;
	return 0;
      }

      static char_type* 
      move(char_type* __s1, const char_type* __s2, size_t __n)
      { return (char_type*) memmove(__s1, __s2, __n * sizeof(char_type)); }

      static char_type* 
      copy(char_type* __s1, const char_type* __s2, size_t __n)
      { return (char_type*) memcpy(__s1, __s2, __n * sizeof(char_type)); }

      static char_type* 
      assign(char_type* __s, size_t __n, char_type __a)
      { 
	for (char_type* __p = __s; __p < __s + __n; ++__p) 
	  assign(*__p, __a);
        return __s; 
      }

      static char_type 
      to_char_type(const int_type& __c)
      { return char_type(); }

      static int_type 
      to_int_type(const char_type& __c) { return int_type(); }

      static bool 
      eq_int_type(const int_type& __c1, const int_type& __c2)
      { return __c1 == __c2; }

      static int_type 
      eof() { return static_cast<int_type>(-1); }

      static int_type 
      not_eof(const int_type& __c)
      { return eq_int_type(__c, eof()) ? int_type(0) : __c; }
    };

  template<>
    struct char_traits<unsigned char>
    {
      typedef unsigned char 	char_type;
      // Unsigned as wint_t in unsigned.
      typedef unsigned long  	int_type;
      typedef streampos 	pos_type;
      typedef streamoff 	off_type;
      typedef mbstate_t 	state_type;
      
      static void 
      assign(char_type& __c1, const char_type& __c2)
      { __c1 = __c2; }

      static bool 
      eq(const char_type& __c1, const char_type& __c2)
      { return __c1 == __c2; }

      static bool 
      lt(const char_type& __c1, const char_type& __c2)
      { return __c1 < __c2; }

      static int 
      compare(const char_type* __s1, const char_type* __s2, size_t __n)
      { 
	for (size_t __i = 0; __i < __n; ++__i)
	  if (!eq(__s1[__i], __s2[__i]))
	    return lt(__s1[__i], __s2[__i]) ? -1 : 1;
	return 0; 
      }

      static size_t
      length(const char_type* __s)
      { 
	const char_type* __p = __s; 
	while (__p && *__p) 
	  ++__p; 
	return (__p - __s); 
      }

      static const char_type* 
      find(const char_type* __s, size_t __n, const char_type& __a)
      { 
	for (const char_type* __p = __s; size_t(__p - __s) < __n; ++__p)
	  if (*__p == __a) return __p;
	return 0;
      }

      static char_type* 
      move(char_type* __s1, const char_type* __s2, size_t __n)
      { return (char_type*) memmove(__s1, __s2, __n * sizeof(char_type)); }

      static char_type* 
      copy(char_type* __s1, const char_type* __s2, size_t __n)
      { return (char_type*) memcpy(__s1, __s2, __n * sizeof(char_type)); }

      static char_type* 
      assign(char_type* __s, size_t __n, char_type __a)
      { 
	for (char_type* __p = __s; __p < __s + __n; ++__p) 
	  assign(*__p, __a);
        return __s; 
      }

      static char_type 
      to_char_type(const int_type& __c)
      { return char_type(); }

      static int_type 
      to_int_type(const char_type& __c) { return int_type(); }

      static bool 
      eq_int_type(const int_type& __c1, const int_type& __c2)
      { return __c1 == __c2; }

      static int_type 
      eof() { return static_cast<int_type>(-1); }

      static int_type 
      not_eof(const int_type& __c)
      { return eq_int_type(__c, eof()) ? int_type(0) : __c; }
    };
} // namespace std

class gnu_filebuf: public std::filebuf
{
  int i;
public:
  gnu_filebuf(int j = 1): i(j) { }
  ~gnu_filebuf() { --i; }
  int get_i() { return i;}
};

const int initial = 4;
gnu_filebuf buf(initial);

// libstdc++/3045, in a vague way.
void test01()
{
  bool test = true;
  int k1;

  // 1 normal
  k1 = buf.get_i();
  VERIFY( k1 == initial );
  {
    std::cout.rdbuf(&buf);
  }
  k1 = buf.get_i();
  VERIFY( k1 == initial );

  // 2 syncd off
  k1 = buf.get_i();
  VERIFY( k1 == initial );
  {
    std::cout.rdbuf(&buf);
    std::ios_base::sync_with_stdio(false); // make sure doesn't clobber buf
  }
  k1 = buf.get_i();
  VERIFY( k1 == initial );

  // 3 callling init
  k1 = buf.get_i();
  VERIFY( k1 == initial );
  {
    std::cout.rdbuf(&buf);
    std::ios_base::Init make_sure_initialized;
  }
  k1 = buf.get_i();
  VERIFY( k1 == initial );
}

// Non-required instantiations don't have the required facets inbued,
// by default, into the locale object.
// See 27.4.4.1
class gnu_ios: public std::basic_ios<char> { };

void test02() 
{
  bool test = true;

  // 01: Doesn't call basic_ios::init, which uses ctype<char_type>..
  // This should be unambiguously correct.
  try
    {
      gnu_ios gios;
    }
  catch(...)
    { 
      test = false; 
    }

  // 02: Calls basic_ios::init, which may call ctype<char_type>...
  try
    {
      std::basic_string<unsigned short>        	str;
      std::basic_ostringstream<unsigned short> 	oss(str);
      
      // Try each member functions for unformatted io.
      // put
      oss.put(324);

      // write
      const unsigned short us[4] = {1246, 433, 520, 0};
      oss.write(us, 4);

      // flush
      oss.flush();
    }
  catch(const std::bad_cast& obj)
    {
      // Should be able to do the above without calling fill() and
      // forcing a call to widen...
      test = false;
    }
  catch(...)
    {
      test = false;
    }
  VERIFY( test );
}

// libstdc++/3983
void test03()
{
  using namespace std;
  bool test = true;

  // input streams
  basic_istringstream<unsigned char> iss_uc;
  unsigned char arr[6] = { 'a', 'b', 'c', 'd', 'e' };

  // Sentry uses locale info, so have to try one formatted input.
  try 
    { 
      int i;
      iss_uc >> i;
    }
  catch (bad_cast& obj)
    { }
  catch (exception& obj)
    { test = false; }
  
  try 
    { 
      iss_uc >> arr;
    }
  catch (bad_cast& obj)
    { }
  catch (exception& obj)
    { test = false; }
  
  try 
    { 
      iss_uc >> ws;
    }
  catch (bad_cast& obj)
    { }
  catch (exception& obj)
    { test = false; }
 
  try 
    { 
      basic_string<unsigned char> s_uc(arr);
      iss_uc >> s_uc;
    }
  catch (bad_cast& obj)
    { }
  catch (exception& obj)
    { test = false; }

  // output streams
  basic_ostringstream<unsigned char> oss_uc;

  try 
    { 
      bool b = true;
      oss_uc << b;
    }
  catch (bad_cast& obj)
    { }
  catch (exception& obj)
    { test = false; }
   
  VERIFY( test );
}

// libstdc++/5268
int test04()
{
  std::stringbuf b1;
  std::cout.rdbuf( &b1 );
  std::cout << "hello\n";
  return 0;
}

#if !__GXX_WEAK__
// Explicitly instantiate for systems with no COMDAT or weak support.
template 
  std::basic_string<unsigned short>::size_type 
  std::basic_string<unsigned short>::_Rep::_S_max_size;

template 
  unsigned short
  std::basic_string<unsigned short>::_Rep::_S_terminal;

template 
  std::basic_string<unsigned char>::size_type 
  std::basic_string<unsigned char>::_Rep::_S_max_size;

template 
  unsigned char
  std::basic_string<unsigned char>::_Rep::_S_terminal;
#endif

int main()
{
  test01();
  test02();
  test03();
  test04();
  return 0;
}
