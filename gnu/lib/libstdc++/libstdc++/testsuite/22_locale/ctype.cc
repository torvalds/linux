// 1999-08-24 bkoz

// Copyright (C) 2000, 1999 Free Software Foundation
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

// 22.2.1 The ctype category

// { dg-do compile }

// 1: Test that the locale headers are picking up the correct declaration
// of the internal type `ctype_base::mask'.
int mask ();

#include <locale>

// 2: Should be able to instantiate this for other types besides char, wchar_t
typedef std::ctype<char> cctype;

class gnu_ctype: public std::ctype<unsigned char> 
{ 
private:
  const cctype& _M_cctype;

public:
  explicit 
  gnu_ctype(size_t __refs = 0) 
  : std::ctype<unsigned char>(__refs), 
    _M_cctype(std::use_facet<cctype>(std::locale::classic())) 
  { }

  ~gnu_ctype();

protected:
  virtual bool 
  do_is(mask __m, char_type __c) const
  { return _M_cctype.is(__m, __c); }

  virtual const char_type*
  do_is(const char_type* __lo, const char_type* __hi, mask* __vec) const
  { 
    const char* __c = _M_cctype.is(reinterpret_cast<const char*>(__lo), 
				   reinterpret_cast<const char*>(__hi), __vec);
    return reinterpret_cast<const char_type*>(__c);
  }
  
  virtual const char_type*
  do_scan_is(mask __m, const char_type* __lo, const char_type* __hi) const
  {
    const char* __c = _M_cctype.scan_is(__m, 
					reinterpret_cast<const char*>(__lo), 
					reinterpret_cast<const char*>(__hi));
    return reinterpret_cast<const char_type*>(__c);
  }

  virtual const char_type*
  do_scan_not(mask __m, const char_type* __lo, const char_type* __hi) const
  {
    const char* __c = _M_cctype.scan_is(__m, 
					reinterpret_cast<const char*>(__lo), 
					reinterpret_cast<const char*>(__hi));
    return reinterpret_cast<const char_type*>(__c);
  }

  virtual char_type 
  do_toupper(char_type __c) const
  { return _M_cctype.toupper(__c); }

  virtual const char_type*
  do_toupper(char_type* __lo, const char_type* __hi) const
  {
    const char* __c = _M_cctype.toupper(reinterpret_cast<char*>(__lo), 
					reinterpret_cast<const char*>(__hi));
    return reinterpret_cast<const char_type*>(__c);
  }

  virtual char_type 
  do_tolower(char_type __c) const
  { return _M_cctype.tolower(__c); }

  virtual const char_type*
  do_tolower(char_type* __lo, const char_type* __hi) const
  {
    const char* __c = _M_cctype.toupper(reinterpret_cast<char*>(__lo), 
					reinterpret_cast<const char*>(__hi));
    return reinterpret_cast<const char_type*>(__c);
  }

  virtual char_type 
  do_widen(char __c) const
  { return _M_cctype.widen(__c); }

  virtual const char*
  do_widen(const char* __lo, const char* __hi, char_type* __dest) const
  {
    const char* __c = _M_cctype.widen(reinterpret_cast<const char*>(__lo), 
				      reinterpret_cast<const char*>(__hi),
				      reinterpret_cast<char*>(__dest));
    return __c;
  }

  virtual char 
  do_narrow(char_type __c, char __dfault) const
  { return _M_cctype.narrow(__c, __dfault); }

  virtual const char_type*
  do_narrow(const char_type* __lo, const char_type* __hi, char __dfault, 
	    char* __dest) const
  {
    const char* __c = _M_cctype.narrow(reinterpret_cast<const char*>(__lo), 
				       reinterpret_cast<const char*>(__hi),
				       __dfault,
				       reinterpret_cast<char*>(__dest));
    return reinterpret_cast<const char_type*>(__c);
  }

};

gnu_ctype::~gnu_ctype() { }

gnu_ctype facet01;

// 3: Sanity check ctype_base::mask bitmask requirements
void
test01()
{
  using namespace std;

  ctype_base::mask m01;
  ctype_base::mask m02;
  
  m01 = ctype_base::space;
  m02 = ctype_base::xdigit;

  m01 & m02;
  m01 | m02;
  m01 ^ m02;
  ~m01;
  m01 &= m02;
  m01 |= m02;
  m01 ^= m02;
}

class gnu_obj 
{ };

class gnu_ctype2: public std::ctype<gnu_obj> 
{ };

// libstdc++/3017
void test02()
{
  gnu_ctype2 obj;
}

int main() 
{ 
  test01();
  test02();
  return 0;
}
