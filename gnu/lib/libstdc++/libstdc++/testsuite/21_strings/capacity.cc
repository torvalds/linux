// 1999-05-11 bkoz

// Copyright (C) 1999, 2002 Free Software Foundation, Inc.
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

// 21.3.3 string capacity

#include <string>
#include <testsuite_hooks.h>

template<typename T>
  struct A { };

template<typename T>
  bool
  operator==(const A<T>& a, const A<T>& b) { return true; }

template<typename T>
  bool
  operator<(const A<T>& a, const A<T>& b) { return true; }

struct B { };

// char_traits specialization
namespace std
{
  template<>
    struct char_traits<A<B> >
    {
      typedef A<B> 		char_type;
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
} // namespace std

void test01()
{
  // 1 POD types : resize, capacity, reserve
  bool test = true;
  std::string str01;
  typedef std::string::size_type size_type_s;

  size_type_s sz01 = str01.capacity();
  str01.reserve(100);
  size_type_s sz02 = str01.capacity();
  VERIFY( sz02 >= sz01 );
  VERIFY( sz02 >= 100 );
  str01.reserve();
  sz01 = str01.capacity();
  VERIFY( sz01 >= 0 );

  sz01 = str01.size() + 5;
  str01.resize(sz01);
  sz02 = str01.size();
  VERIFY( sz01 == sz02 );

  sz01 = str01.size() - 5;
  str01.resize(sz01);
  sz02 = str01.size();
  VERIFY( sz01 == sz02 );

  std::string str05(30, 'q');
  std::string str06 = str05;
  str05 = str06 + str05;
  VERIFY( str05.capacity() >= str05.size() );
  VERIFY( str06.capacity() >= str06.size() );

  // 2 non POD types : resize, capacity, reserve
  std::basic_string< A<B> > str02;
  typedef std::basic_string< A<B> >::size_type size_type_o;
  size_type_o sz03;
  size_type_o sz04;

  sz03 = str02.capacity();
  str02.reserve(100);
  sz04 = str02.capacity();
  VERIFY( sz04 >= sz03 );
  VERIFY( sz04 >= 100 );
  str02.reserve();
  sz03 = str02.capacity();
  VERIFY( sz03 >= 0 );

  sz03 = str02.size() + 5;
  str02.resize(sz03);
  sz04 = str02.size();
  VERIFY( sz03 == sz04 );

  sz03 = str02.size() - 5;
  str02.resize(sz03);
  sz04 = str02.size();
  VERIFY( sz03 == sz04 );

  A<B> inst_obj;
  std::basic_string<A<B> > str07(30, inst_obj);
  std::basic_string<A<B> > str08 = str07;
  str07 = str08 + str07;
  VERIFY( str07.capacity() >= str07.size() );
  VERIFY( str08.capacity() >= str08.size() );

  // 3 POD types: size, length, max_size, clear(), empty()
  bool b01;
  std::string str011;
  b01 = str01.empty();  
  VERIFY( b01 == true );
  sz01 = str01.size();
  sz02 = str01.length();
  VERIFY( sz01 == sz02 );
  str01.c_str();
  sz01 = str01.size();
  sz02 = str01.length();
  VERIFY( sz01 == sz02 );

  sz01 = str01.length();
  str01.c_str();
  str011 = str01 +  "_addendum_";
  str01.c_str();
  sz02 = str01.length();    
  VERIFY( sz01 == sz02 );
  sz02 = str011.length();
  VERIFY( sz02 > sz01 );
    
  // trickster allocator issues involved with these:
  std::string str3 = "8-chars_8-chars_";
  const char* p3 = str3.c_str();
  std::string str4 = str3 + "7-chars";
  const char* p4 = str3.c_str();
  
  sz01 = str01.size();
  sz02 = str01.max_size();  
  VERIFY( sz02 >= sz01 );

  sz01 = str01.size();
  str01.clear();  
  b01 = str01.empty(); 
  VERIFY( b01 == true );
  sz02 = str01.size();  
  VERIFY( sz01 >= sz02 );


  // 4 non-POD types: size, length, max_size, clear(), empty()
  b01 = str02.empty();  
  VERIFY( b01 == true );
  sz03 = str02.size();
  sz04 = str02.length();
  VERIFY( sz03 == sz04 );
  str02.c_str();
  sz03 = str02.size();
  sz04 = str02.length();
  VERIFY( sz03 == sz04 );

  sz03 = str02.max_size();  
  VERIFY( sz03 >= sz04 );

  sz03 = str02.size();
  str02.clear();  
  b01 = str02.empty(); 
  VERIFY( b01 == true );
  sz04 = str02.size();  
  VERIFY( sz03 >= sz04 );
}

// libstdc++/4548
// http://gcc.gnu.org/ml/libstdc++/2001-11/msg00150.html
void test02()
{
  bool test = true;

  std::string str01 = "twelve chars";
  // str01 becomes shared
  std::string str02 = str01;
  str01.reserve(1);
  VERIFY( str01.capacity() == 12 );
}

#if !__GXX_WEAK__
// Explicitly instantiate for systems with no COMDAT or weak support.
template 
  std::basic_string< A<B> >::size_type 
  std::basic_string< A<B> >::_Rep::_S_max_size;

template 
  A<B>
  std::basic_string< A<B> >::_Rep::_S_terminal;
#endif

int main()
{
  test01();
  test02();

  return 0;
}
