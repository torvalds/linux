// Compatibility symbols for previous versions -*- C++ -*-

// Copyright (C) 2005, 2006
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
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#include <bits/c++config.h>

#if defined(_GLIBCXX_SYMVER_GNU) && defined(PIC)
#define istreambuf_iterator istreambuf_iteratorXX
#define basic_fstream basic_fstreamXX
#define basic_ifstream basic_ifstreamXX
#define basic_ofstream basic_ofstreamXX
#define _M_copy(a, b, c) _M_copyXX(a, b, c)
#define _M_move(a, b, c) _M_moveXX(a, b, c)
#define _M_assign(a, b, c) _M_assignXX(a, b, c)
#define _M_disjunct(a) _M_disjunctXX(a)
#define _M_check_length(a, b, c) _M_check_lengthXX(a, b, c)
#define _M_set_length_and_sharable(a) _M_set_length_and_sharableXX(a)
#define ignore ignoreXX
#define eq eqXX
#define _List_node_base _List_node_baseXX
#endif

#include <string>
#include <istream>
#include <fstream>
#include <sstream>
#include <cmath>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // std::istream ignore explicit specializations.
  template<>
    basic_istream<char>&
    basic_istream<char>::
    ignore(streamsize __n)
    {
      if (__n == 1)
	return ignore();
      
      _M_gcount = 0;
      sentry __cerb(*this, true);
      if (__cerb && __n > 0)
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try
	    {
	      const int_type __eof = traits_type::eof();
	      __streambuf_type* __sb = this->rdbuf();
	      int_type __c = __sb->sgetc();

	      // See comment in istream.tcc.
	      bool __large_ignore = false;
	      while (true)
		{
		  while (_M_gcount < __n
			 && !traits_type::eq_int_type(__c, __eof))
		    {
		      streamsize __size = std::min(streamsize(__sb->egptr()
							      - __sb->gptr()),
					          streamsize(__n - _M_gcount));
		      if (__size > 1)
			{
			  __sb->gbump(__size);
			  _M_gcount += __size;
			  __c = __sb->sgetc();
			}
		      else
			{
			  ++_M_gcount;
			  __c = __sb->snextc();
			} 
		    }
		  if (__n == numeric_limits<streamsize>::max()
		      && !traits_type::eq_int_type(__c, __eof))
		    {
		      _M_gcount = numeric_limits<streamsize>::min();
		      __large_ignore = true;
		    }
		  else
		    break;
		}

	      if (__large_ignore)
		_M_gcount = numeric_limits<streamsize>::max();

	      if (traits_type::eq_int_type(__c, __eof))
		__err |= ios_base::eofbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    } 

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    basic_istream<wchar_t>&
    basic_istream<wchar_t>::
    ignore(streamsize __n)
    {
      if (__n == 1)
	return ignore();
      
      _M_gcount = 0;
      sentry __cerb(*this, true);
      if (__cerb && __n > 0)
	{
	  ios_base::iostate __err = ios_base::iostate(ios_base::goodbit);
	  try
	    {
	      const int_type __eof = traits_type::eof();
	      __streambuf_type* __sb = this->rdbuf();
	      int_type __c = __sb->sgetc();

	      bool __large_ignore = false;
	      while (true)
		{
		  while (_M_gcount < __n
			 && !traits_type::eq_int_type(__c, __eof))
		    {
		      streamsize __size = std::min(streamsize(__sb->egptr()
							      - __sb->gptr()),
						  streamsize(__n - _M_gcount));
		      if (__size > 1)
			{
			  __sb->gbump(__size);
			  _M_gcount += __size;
			  __c = __sb->sgetc();
			}
		      else
			{
			  ++_M_gcount;
			  __c = __sb->snextc();
			}
		    }
		  if (__n == numeric_limits<streamsize>::max()
		      && !traits_type::eq_int_type(__c, __eof))
		    {
		      _M_gcount = numeric_limits<streamsize>::min();
		      __large_ignore = true;
		    }
		  else
		    break;
		}

	      if (__large_ignore)
		_M_gcount = numeric_limits<streamsize>::max();

	      if (traits_type::eq_int_type(__c, __eof))
		__err |= ios_base::eofbit;
	    }
	  catch(...)
	    { this->_M_setstate(ios_base::badbit); }
	  if (__err)
	    this->setstate(__err);
	}
      return *this;
    }
#endif

_GLIBCXX_END_NAMESPACE


// NB: These symbols renames should go into the shared library only,
// and only those shared libraries that support versioning.
#if defined(_GLIBCXX_SYMVER_GNU) && defined(PIC)

/* gcc-3.4.4
_ZNSt19istreambuf_iteratorIcSt11char_traitsIcEEppEv
_ZNSt19istreambuf_iteratorIwSt11char_traitsIwEEppEv
 */

_GLIBCXX_BEGIN_NAMESPACE(std)

  template
    istreambuf_iterator<char>&
    istreambuf_iterator<char>::operator++();

#ifdef _GLIBCXX_USE_WCHAR_T
  template
    istreambuf_iterator<wchar_t>&
    istreambuf_iterator<wchar_t>::operator++();
#endif

_GLIBCXX_END_NAMESPACE


/* gcc-4.0.0
_ZNSs4_Rep26_M_set_length_and_sharableEj
_ZNSs7_M_copyEPcPKcj
_ZNSs7_M_moveEPcPKcj
_ZNSs9_M_assignEPcjc
_ZNKSs11_M_disjunctEPKc
_ZNKSs15_M_check_lengthEjjPKc
_ZNSbIwSt11char_traitsIwESaIwEE4_Rep26_M_set_length_and_sharableEj
_ZNSbIwSt11char_traitsIwESaIwEE7_M_copyEPwPKwj
_ZNSbIwSt11char_traitsIwESaIwEE7_M_moveEPwPKwj
_ZNSbIwSt11char_traitsIwESaIwEE9_M_assignEPwjw
_ZNKSbIwSt11char_traitsIwESaIwEE11_M_disjunctEPKw
_ZNKSbIwSt11char_traitsIwESaIwEE15_M_check_lengthEjjPKc

_ZNKSt13basic_fstreamIcSt11char_traitsIcEE7is_openEv
_ZNKSt13basic_fstreamIwSt11char_traitsIwEE7is_openEv
_ZNKSt14basic_ifstreamIcSt11char_traitsIcEE7is_openEv
_ZNKSt14basic_ifstreamIwSt11char_traitsIwEE7is_openEv
_ZNKSt14basic_ofstreamIcSt11char_traitsIcEE7is_openEv
_ZNKSt14basic_ofstreamIwSt11char_traitsIwEE7is_openEv

_ZNSi6ignoreEi
_ZNSi6ignoreEv
_ZNSt13basic_istreamIwSt11char_traitsIwEE6ignoreEi
_ZNSt13basic_istreamIwSt11char_traitsIwEE6ignoreEv

_ZNSt11char_traitsIcE2eqERKcS2_
_ZNSt11char_traitsIwE2eqERKwS2_
 */
_GLIBCXX_BEGIN_NAMESPACE(std)

  // std::char_traits is explicitly specialized
  bool (* __p1)(const char&, const char&) = &char_traits<char>::eq;

  // std::string
  template
    void
    basic_string<char>::_M_copy(char*, const char*, size_t);

  template
    void
    basic_string<char>::_M_move(char*, const char*, size_t);

  template
    void
    basic_string<char>::_M_assign(char*, size_t, char);

  template
    bool
    basic_string<char>::_M_disjunct(const char*) const;

  template
    void
    basic_string<char>::_M_check_length(size_t, size_t, const char*) const;

  template
    void
    basic_string<char>::_Rep::_M_set_length_and_sharable(size_t);


  // std::istream
  template
    basic_istream<char>&
    basic_istream<char>::ignore(); 

  template
    bool
    basic_fstream<char>::is_open() const;

  template
    bool
    basic_ifstream<char>::is_open() const;

  template
    bool
    basic_ofstream<char>::is_open() const;

#ifdef _GLIBCXX_USE_WCHAR_T
  bool (* __p2)(const wchar_t&, const wchar_t&) = &char_traits<wchar_t>::eq;

  // std::wstring
  template
    void
    basic_string<wchar_t>::_M_copy(wchar_t*, const wchar_t*, size_t);

  template
    void
    basic_string<wchar_t>::_M_move(wchar_t*, const wchar_t*, size_t);

  template
    void
    basic_string<wchar_t>::_M_assign(wchar_t*, size_t, wchar_t);

  template
    bool
    basic_string<wchar_t>::_M_disjunct(const wchar_t*) const;

  template
    void
    basic_string<wchar_t>::_M_check_length(size_t, size_t, 
					   const char*) const;

  template
    void
    basic_string<wchar_t>::_Rep::_M_set_length_and_sharable(size_t);

  template
    basic_istream<wchar_t>&
    basic_istream<wchar_t>::ignore(); 

  template
    bool
    basic_fstream<wchar_t>::is_open() const;

  template
    bool
    basic_ifstream<wchar_t>::is_open() const;

  template
    bool
    basic_ofstream<wchar_t>::is_open() const;
#endif

_GLIBCXX_END_NAMESPACE

// The rename syntax for default exported names is
//   asm (".symver name1,exportedname@GLIBCXX_3.4")
//   asm (".symver name2,exportedname@@GLIBCXX_3.4.5")
// In the future, GLIBCXX_ABI > 6 should remove all uses of
// _GLIBCXX_*_SYMVER macros in this file.

#define _GLIBCXX_3_4_SYMVER(XXname, name) \
   extern "C" void \
   _X##name() \
   __attribute__ ((alias(#XXname))); \
   asm (".symver " "_X" #name "," #name "@GLIBCXX_3.4");

#define _GLIBCXX_3_4_5_SYMVER(XXname, name) \
   extern "C" void \
   _Y##name() \
   __attribute__ ((alias(#XXname))); \
   asm (".symver " "_Y" #name  "," #name "@@GLIBCXX_3.4.5");

#define _GLIBCXX_ASM_SYMVER(cur, old, version) \
   asm (".symver " #cur "," #old "@@" #version);

#define _GLIBCXX_APPLY_SYMVER _GLIBCXX_3_4_SYMVER
#include <bits/compatibility.h>
#undef _GLIBCXX_APPLY_SYMVER

#define _GLIBCXX_APPLY_SYMVER _GLIBCXX_3_4_5_SYMVER
#include <bits/compatibility.h>
#undef _GLIBCXX_APPLY_SYMVER


/* gcc-3.4.0
_ZN10__gnu_norm15_List_node_base4hookEPS0_;
_ZN10__gnu_norm15_List_node_base4swapERS0_S1_;
_ZN10__gnu_norm15_List_node_base6unhookEv;
_ZN10__gnu_norm15_List_node_base7reverseEv;
_ZN10__gnu_norm15_List_node_base8transferEPS0_S1_;
*/
#include "list.cc"
_GLIBCXX_ASM_SYMVER(_ZNSt17_List_node_baseXX4hookEPS_, \
_ZN10__gnu_norm15_List_node_base4hookEPS0_, \
GLIBCXX_3.4)

_GLIBCXX_ASM_SYMVER(_ZNSt17_List_node_baseXX4swapERS_S0_, \
_ZN10__gnu_norm15_List_node_base4swapERS0_S1_, \
GLIBCXX_3.4)

_GLIBCXX_ASM_SYMVER(_ZNSt17_List_node_baseXX6unhookEv, \
_ZN10__gnu_norm15_List_node_base6unhookEv, \
GLIBCXX_3.4)

_GLIBCXX_ASM_SYMVER(_ZNSt17_List_node_baseXX7reverseEv, \
_ZN10__gnu_norm15_List_node_base7reverseEv, \
GLIBCXX_3.4)

_GLIBCXX_ASM_SYMVER(_ZNSt17_List_node_baseXX8transferEPS_S0_, \
_ZN10__gnu_norm15_List_node_base8transferEPS0_S1_, \
GLIBCXX_3.4)
#undef _List_node_base

// gcc-4.1.0
#ifdef _GLIBCXX_LONG_DOUBLE_COMPAT
#define _GLIBCXX_MATHL_WRAPPER(name, argdecl, args, ver) \
extern "C" double						\
__ ## name ## l_wrapper argdecl					\
{								\
  return name args;						\
}								\
asm (".symver __" #name "l_wrapper, " #name "l@" #ver)

#define _GLIBCXX_MATHL_WRAPPER1(name, ver) \
  _GLIBCXX_MATHL_WRAPPER (name, (double x), (x), ver)

#define _GLIBCXX_MATHL_WRAPPER2(name, ver) \
  _GLIBCXX_MATHL_WRAPPER (name, (double x, double y), (x, y), ver)

#ifdef _GLIBCXX_HAVE_ACOSL
_GLIBCXX_MATHL_WRAPPER1 (acos, GLIBCXX_3.4.3);
#endif
#ifdef _GLIBCXX_HAVE_ASINL
_GLIBCXX_MATHL_WRAPPER1 (asin, GLIBCXX_3.4.3);
#endif
#ifdef _GLIBCXX_HAVE_ATAN2L
_GLIBCXX_MATHL_WRAPPER2 (atan2, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_ATANL
_GLIBCXX_MATHL_WRAPPER1 (atan, GLIBCXX_3.4.3);
#endif
#ifdef _GLIBCXX_HAVE_CEILL
_GLIBCXX_MATHL_WRAPPER1 (ceil, GLIBCXX_3.4.3);
#endif
#ifdef _GLIBCXX_HAVE_COSHL
_GLIBCXX_MATHL_WRAPPER1 (cosh, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_COSL
_GLIBCXX_MATHL_WRAPPER1 (cos, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_EXPL
_GLIBCXX_MATHL_WRAPPER1 (exp, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_FLOORL
_GLIBCXX_MATHL_WRAPPER1 (floor, GLIBCXX_3.4.3);
#endif
#ifdef _GLIBCXX_HAVE_FMODL
_GLIBCXX_MATHL_WRAPPER2 (fmod, GLIBCXX_3.4.3);
#endif
#ifdef _GLIBCXX_HAVE_FREXPL
_GLIBCXX_MATHL_WRAPPER (frexp, (double x, int *y), (x, y), GLIBCXX_3.4.3);
#endif
#ifdef _GLIBCXX_HAVE_HYPOTL
_GLIBCXX_MATHL_WRAPPER2 (hypot, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_LDEXPL
_GLIBCXX_MATHL_WRAPPER (ldexp, (double x, int y), (x, y), GLIBCXX_3.4.3);
#endif
#ifdef _GLIBCXX_HAVE_LOG10L
_GLIBCXX_MATHL_WRAPPER1 (log10, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_LOGL
_GLIBCXX_MATHL_WRAPPER1 (log, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_MODFL
_GLIBCXX_MATHL_WRAPPER (modf, (double x, double *y), (x, y), GLIBCXX_3.4.3);
#endif
#ifdef _GLIBCXX_HAVE_POWL
_GLIBCXX_MATHL_WRAPPER2 (pow, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_SINHL
_GLIBCXX_MATHL_WRAPPER1 (sinh, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_SINL
_GLIBCXX_MATHL_WRAPPER1 (sin, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_SQRTL
_GLIBCXX_MATHL_WRAPPER1 (sqrt, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_TANHL
_GLIBCXX_MATHL_WRAPPER1 (tanh, GLIBCXX_3.4);
#endif
#ifdef _GLIBCXX_HAVE_TANL
_GLIBCXX_MATHL_WRAPPER1 (tan, GLIBCXX_3.4);
#endif
#endif // _GLIBCXX_LONG_DOUBLE_COMPAT

#endif

#ifdef _GLIBCXX_LONG_DOUBLE_COMPAT
extern void *_ZTVN10__cxxabiv123__fundamental_type_infoE[];
extern void *_ZTVN10__cxxabiv119__pointer_type_infoE[];
extern __attribute__((used, weak)) const char _ZTSe[2] = "e";
extern __attribute__((used, weak)) const char _ZTSPe[3] = "Pe";
extern __attribute__((used, weak)) const char _ZTSPKe[4] = "PKe";
extern __attribute__((used, weak)) const void *_ZTIe[2]
  = { (void *) &_ZTVN10__cxxabiv123__fundamental_type_infoE[2],
      (void *) _ZTSe };
extern __attribute__((used, weak)) const void *_ZTIPe[4]
  = { (void *) &_ZTVN10__cxxabiv119__pointer_type_infoE[2],
      (void *) _ZTSPe, (void *) 0L, (void *) _ZTIe };
extern __attribute__((used, weak)) const void *_ZTIPKe[4]
  = { (void *) &_ZTVN10__cxxabiv119__pointer_type_infoE[2],
      (void *) _ZTSPKe, (void *) 1L, (void *) _ZTIe };
#endif // _GLIBCXX_LONG_DOUBLE_COMPAT



#ifdef _GLIBCXX_SYMVER_DARWIN
#if (defined(__ppc__) || defined(__ppc64__)) && defined(PIC)
/* __eprintf shouldn't have been made visible from libstdc++, or
   anywhere, but on Mac OS X 10.4 it was defined in
   libstdc++.6.0.3.dylib; so on that platform we have to keep defining
   it to keep binary compatibility.  We can't just put the libgcc
   version in the export list, because that doesn't work; once a
   symbol is marked as hidden, it stays that way.  */

#include <cstdio>
#include <cstdlib>

using namespace std;

extern "C" void
__eprintf(const char *string, const char *expression,
	  unsigned int line, const char *filename)
{
  fprintf(stderr, string, expression, line, filename);
  fflush(stderr);
  abort();
}
#endif
#endif
