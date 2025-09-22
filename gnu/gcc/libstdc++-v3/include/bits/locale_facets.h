// Locale support -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

/** @file locale_facets.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

//
// ISO C++ 14882: 22.1  Locales
//

#ifndef _LOCALE_FACETS_H
#define _LOCALE_FACETS_H 1

#pragma GCC system_header

#include <ctime>	// For struct tm
#include <cwctype>	// For wctype_t
#include <bits/ctype_base.h>	
#include <iosfwd>
#include <bits/ios_base.h>  // For ios_base, ios_base::iostate
#include <streambuf>
#include <bits/cpp_type_traits.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // NB: Don't instantiate required wchar_t facets if no wchar_t support.
#ifdef _GLIBCXX_USE_WCHAR_T
# define  _GLIBCXX_NUM_FACETS 28
#else
# define  _GLIBCXX_NUM_FACETS 14
#endif

  // Convert string to numeric value of type _Tv and store results.
  // NB: This is specialized for all required types, there is no
  // generic definition.
  template<typename _Tv>
    void
    __convert_to_v(const char* __in, _Tv& __out, ios_base::iostate& __err,
		   const __c_locale& __cloc);

  // Explicit specializations for required types.
  template<>
    void
    __convert_to_v(const char*, float&, ios_base::iostate&,
		   const __c_locale&);

  template<>
    void
    __convert_to_v(const char*, double&, ios_base::iostate&,
		   const __c_locale&);

  template<>
    void
    __convert_to_v(const char*, long double&, ios_base::iostate&,
		   const __c_locale&);

  // NB: __pad is a struct, rather than a function, so it can be
  // partially-specialized.
  template<typename _CharT, typename _Traits>
    struct __pad
    {
      static void
      _S_pad(ios_base& __io, _CharT __fill, _CharT* __news,
	     const _CharT* __olds, const streamsize __newlen,
	     const streamsize __oldlen, const bool __num);
    };

  // Used by both numeric and monetary facets.
  // Inserts "group separator" characters into an array of characters.
  // It's recursive, one iteration per group.  It moves the characters
  // in the buffer this way: "xxxx12345" -> "12,345xxx".  Call this
  // only with __glen != 0.
  template<typename _CharT>
    _CharT*
    __add_grouping(_CharT* __s, _CharT __sep,
		   const char* __gbeg, size_t __gsize,
		   const _CharT* __first, const _CharT* __last);

  // This template permits specializing facet output code for
  // ostreambuf_iterator.  For ostreambuf_iterator, sputn is
  // significantly more efficient than incrementing iterators.
  template<typename _CharT>
    inline
    ostreambuf_iterator<_CharT>
    __write(ostreambuf_iterator<_CharT> __s, const _CharT* __ws, int __len)
    {
      __s._M_put(__ws, __len);
      return __s;
    }

  // This is the unspecialized form of the template.
  template<typename _CharT, typename _OutIter>
    inline
    _OutIter
    __write(_OutIter __s, const _CharT* __ws, int __len)
    {
      for (int __j = 0; __j < __len; __j++, ++__s)
	*__s = __ws[__j];
      return __s;
    }


  // 22.2.1.1  Template class ctype
  // Include host and configuration specific ctype enums for ctype_base.

  // Common base for ctype<_CharT>.
  /**
   *  @brief  Common base for ctype facet
   *
   *  This template class provides implementations of the public functions
   *  that forward to the protected virtual functions.
   *
   *  This template also provides abtract stubs for the protected virtual
   *  functions.
  */
  template<typename _CharT>
    class __ctype_abstract_base : public locale::facet, public ctype_base
    {
    public:
      // Types:
      /// Typedef for the template parameter
      typedef _CharT char_type;

      /**
       *  @brief  Test char_type classification.
       *
       *  This function finds a mask M for @a c and compares it to mask @a m.
       *  It does so by returning the value of ctype<char_type>::do_is().
       *
       *  @param c  The char_type to compare the mask of.
       *  @param m  The mask to compare against.
       *  @return  (M & m) != 0.
      */
      bool
      is(mask __m, char_type __c) const
      { return this->do_is(__m, __c); }

      /**
       *  @brief  Return a mask array.
       *
       *  This function finds the mask for each char_type in the range [lo,hi)
       *  and successively writes it to vec.  vec must have as many elements
       *  as the char array.  It does so by returning the value of
       *  ctype<char_type>::do_is().
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param vec  Pointer to an array of mask storage.
       *  @return  @a hi.
      */
      const char_type*
      is(const char_type *__lo, const char_type *__hi, mask *__vec) const
      { return this->do_is(__lo, __hi, __vec); }

      /**
       *  @brief  Find char_type matching a mask
       *
       *  This function searches for and returns the first char_type c in
       *  [lo,hi) for which is(m,c) is true.  It does so by returning
       *  ctype<char_type>::do_scan_is().
       *
       *  @param m  The mask to compare against.
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  Pointer to matching char_type if found, else @a hi.
      */
      const char_type*
      scan_is(mask __m, const char_type* __lo, const char_type* __hi) const
      { return this->do_scan_is(__m, __lo, __hi); }

      /**
       *  @brief  Find char_type not matching a mask
       *
       *  This function searches for and returns the first char_type c in
       *  [lo,hi) for which is(m,c) is false.  It does so by returning
       *  ctype<char_type>::do_scan_not().
       *
       *  @param m  The mask to compare against.
       *  @param lo  Pointer to first char in range.
       *  @param hi  Pointer to end of range.
       *  @return  Pointer to non-matching char if found, else @a hi.
      */
      const char_type*
      scan_not(mask __m, const char_type* __lo, const char_type* __hi) const
      { return this->do_scan_not(__m, __lo, __hi); }

      /**
       *  @brief  Convert to uppercase.
       *
       *  This function converts the argument to uppercase if possible.
       *  If not possible (for example, '2'), returns the argument.  It does
       *  so by returning ctype<char_type>::do_toupper().
       *
       *  @param c  The char_type to convert.
       *  @return  The uppercase char_type if convertible, else @a c.
      */
      char_type
      toupper(char_type __c) const
      { return this->do_toupper(__c); }

      /**
       *  @brief  Convert array to uppercase.
       *
       *  This function converts each char_type in the range [lo,hi) to
       *  uppercase if possible.  Other elements remain untouched.  It does so
       *  by returning ctype<char_type>:: do_toupper(lo, hi).
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  @a hi.
      */
      const char_type*
      toupper(char_type *__lo, const char_type* __hi) const
      { return this->do_toupper(__lo, __hi); }

      /**
       *  @brief  Convert to lowercase.
       *
       *  This function converts the argument to lowercase if possible.  If
       *  not possible (for example, '2'), returns the argument.  It does so
       *  by returning ctype<char_type>::do_tolower(c).
       *
       *  @param c  The char_type to convert.
       *  @return  The lowercase char_type if convertible, else @a c.
      */
      char_type
      tolower(char_type __c) const
      { return this->do_tolower(__c); }

      /**
       *  @brief  Convert array to lowercase.
       *
       *  This function converts each char_type in the range [lo,hi) to
       *  lowercase if possible.  Other elements remain untouched.  It does so
       *  by returning ctype<char_type>:: do_tolower(lo, hi).
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  @a hi.
      */
      const char_type*
      tolower(char_type* __lo, const char_type* __hi) const
      { return this->do_tolower(__lo, __hi); }

      /**
       *  @brief  Widen char to char_type
       *
       *  This function converts the char argument to char_type using the
       *  simplest reasonable transformation.  It does so by returning
       *  ctype<char_type>::do_widen(c).
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param c  The char to convert.
       *  @return  The converted char_type.
      */
      char_type
      widen(char __c) const
      { return this->do_widen(__c); }

      /**
       *  @brief  Widen array to char_type
       *
       *  This function converts each char in the input to char_type using the
       *  simplest reasonable transformation.  It does so by returning
       *  ctype<char_type>::do_widen(c).
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param to  Pointer to the destination array.
       *  @return  @a hi.
      */
      const char*
      widen(const char* __lo, const char* __hi, char_type* __to) const
      { return this->do_widen(__lo, __hi, __to); }

      /**
       *  @brief  Narrow char_type to char
       *
       *  This function converts the char_type to char using the simplest
       *  reasonable transformation.  If the conversion fails, dfault is
       *  returned instead.  It does so by returning
       *  ctype<char_type>::do_narrow(c).
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param c  The char_type to convert.
       *  @param dfault  Char to return if conversion fails.
       *  @return  The converted char.
      */
      char
      narrow(char_type __c, char __dfault) const
      { return this->do_narrow(__c, __dfault); }

      /**
       *  @brief  Narrow array to char array
       *
       *  This function converts each char_type in the input to char using the
       *  simplest reasonable transformation and writes the results to the
       *  destination array.  For any char_type in the input that cannot be
       *  converted, @a dfault is used instead.  It does so by returning
       *  ctype<char_type>::do_narrow(lo, hi, dfault, to).
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param dfault  Char to use if conversion fails.
       *  @param to  Pointer to the destination array.
       *  @return  @a hi.
      */
      const char_type*
      narrow(const char_type* __lo, const char_type* __hi,
	      char __dfault, char *__to) const
      { return this->do_narrow(__lo, __hi, __dfault, __to); }

    protected:
      explicit
      __ctype_abstract_base(size_t __refs = 0): facet(__refs) { }

      virtual
      ~__ctype_abstract_base() { }

      /**
       *  @brief  Test char_type classification.
       *
       *  This function finds a mask M for @a c and compares it to mask @a m.
       *
       *  do_is() is a hook for a derived facet to change the behavior of
       *  classifying.  do_is() must always return the same result for the
       *  same input.
       *
       *  @param c  The char_type to find the mask of.
       *  @param m  The mask to compare against.
       *  @return  (M & m) != 0.
      */
      virtual bool
      do_is(mask __m, char_type __c) const = 0;

      /**
       *  @brief  Return a mask array.
       *
       *  This function finds the mask for each char_type in the range [lo,hi)
       *  and successively writes it to vec.  vec must have as many elements
       *  as the input.
       *
       *  do_is() is a hook for a derived facet to change the behavior of
       *  classifying.  do_is() must always return the same result for the
       *  same input.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param vec  Pointer to an array of mask storage.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_is(const char_type* __lo, const char_type* __hi,
	    mask* __vec) const = 0;

      /**
       *  @brief  Find char_type matching mask
       *
       *  This function searches for and returns the first char_type c in
       *  [lo,hi) for which is(m,c) is true.
       *
       *  do_scan_is() is a hook for a derived facet to change the behavior of
       *  match searching.  do_is() must always return the same result for the
       *  same input.
       *
       *  @param m  The mask to compare against.
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  Pointer to a matching char_type if found, else @a hi.
      */
      virtual const char_type*
      do_scan_is(mask __m, const char_type* __lo,
		 const char_type* __hi) const = 0;

      /**
       *  @brief  Find char_type not matching mask
       *
       *  This function searches for and returns a pointer to the first
       *  char_type c of [lo,hi) for which is(m,c) is false.
       *
       *  do_scan_is() is a hook for a derived facet to change the behavior of
       *  match searching.  do_is() must always return the same result for the
       *  same input.
       *
       *  @param m  The mask to compare against.
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  Pointer to a non-matching char_type if found, else @a hi.
      */
      virtual const char_type*
      do_scan_not(mask __m, const char_type* __lo,
		  const char_type* __hi) const = 0;

      /**
       *  @brief  Convert to uppercase.
       *
       *  This virtual function converts the char_type argument to uppercase
       *  if possible.  If not possible (for example, '2'), returns the
       *  argument.
       *
       *  do_toupper() is a hook for a derived facet to change the behavior of
       *  uppercasing.  do_toupper() must always return the same result for
       *  the same input.
       *
       *  @param c  The char_type to convert.
       *  @return  The uppercase char_type if convertible, else @a c.
      */
      virtual char_type
      do_toupper(char_type) const = 0;

      /**
       *  @brief  Convert array to uppercase.
       *
       *  This virtual function converts each char_type in the range [lo,hi)
       *  to uppercase if possible.  Other elements remain untouched.
       *
       *  do_toupper() is a hook for a derived facet to change the behavior of
       *  uppercasing.  do_toupper() must always return the same result for
       *  the same input.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_toupper(char_type* __lo, const char_type* __hi) const = 0;

      /**
       *  @brief  Convert to lowercase.
       *
       *  This virtual function converts the argument to lowercase if
       *  possible.  If not possible (for example, '2'), returns the argument.
       *
       *  do_tolower() is a hook for a derived facet to change the behavior of
       *  lowercasing.  do_tolower() must always return the same result for
       *  the same input.
       *
       *  @param c  The char_type to convert.
       *  @return  The lowercase char_type if convertible, else @a c.
      */
      virtual char_type
      do_tolower(char_type) const = 0;

      /**
       *  @brief  Convert array to lowercase.
       *
       *  This virtual function converts each char_type in the range [lo,hi)
       *  to lowercase if possible.  Other elements remain untouched.
       *
       *  do_tolower() is a hook for a derived facet to change the behavior of
       *  lowercasing.  do_tolower() must always return the same result for
       *  the same input.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_tolower(char_type* __lo, const char_type* __hi) const = 0;

      /**
       *  @brief  Widen char
       *
       *  This virtual function converts the char to char_type using the
       *  simplest reasonable transformation.
       *
       *  do_widen() is a hook for a derived facet to change the behavior of
       *  widening.  do_widen() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param c  The char to convert.
       *  @return  The converted char_type
      */
      virtual char_type
      do_widen(char) const = 0;

      /**
       *  @brief  Widen char array
       *
       *  This function converts each char in the input to char_type using the
       *  simplest reasonable transformation.
       *
       *  do_widen() is a hook for a derived facet to change the behavior of
       *  widening.  do_widen() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param lo  Pointer to start range.
       *  @param hi  Pointer to end of range.
       *  @param to  Pointer to the destination array.
       *  @return  @a hi.
      */
      virtual const char*
      do_widen(const char* __lo, const char* __hi,
	       char_type* __dest) const = 0;

      /**
       *  @brief  Narrow char_type to char
       *
       *  This virtual function converts the argument to char using the
       *  simplest reasonable transformation.  If the conversion fails, dfault
       *  is returned instead.
       *
       *  do_narrow() is a hook for a derived facet to change the behavior of
       *  narrowing.  do_narrow() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param c  The char_type to convert.
       *  @param dfault  Char to return if conversion fails.
       *  @return  The converted char.
      */
      virtual char
      do_narrow(char_type, char __dfault) const = 0;

      /**
       *  @brief  Narrow char_type array to char
       *
       *  This virtual function converts each char_type in the range [lo,hi) to
       *  char using the simplest reasonable transformation and writes the
       *  results to the destination array.  For any element in the input that
       *  cannot be converted, @a dfault is used instead.
       *
       *  do_narrow() is a hook for a derived facet to change the behavior of
       *  narrowing.  do_narrow() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param dfault  Char to use if conversion fails.
       *  @param to  Pointer to the destination array.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_narrow(const char_type* __lo, const char_type* __hi,
		char __dfault, char* __dest) const = 0;
    };

  // NB: Generic, mostly useless implementation.
  /**
   *  @brief  Template ctype facet
   *
   *  This template class defines classification and conversion functions for
   *  character sets.  It wraps <cctype> functionality.  Ctype gets used by
   *  streams for many I/O operations.
   *
   *  This template provides the protected virtual functions the developer
   *  will have to replace in a derived class or specialization to make a
   *  working facet.  The public functions that access them are defined in
   *  __ctype_abstract_base, to allow for implementation flexibility.  See
   *  ctype<wchar_t> for an example.  The functions are documented in
   *  __ctype_abstract_base.
   *
   *  Note: implementations are provided for all the protected virtual
   *  functions, but will likely not be useful.
  */
  template<typename _CharT>
    class ctype : public __ctype_abstract_base<_CharT>
    {
    public:
      // Types:
      typedef _CharT			char_type;
      typedef typename __ctype_abstract_base<_CharT>::mask mask;

      /// The facet id for ctype<char_type>
      static locale::id			id;

      explicit
      ctype(size_t __refs = 0) : __ctype_abstract_base<_CharT>(__refs) { }

   protected:
      virtual
      ~ctype();

      virtual bool
      do_is(mask __m, char_type __c) const;

      virtual const char_type*
      do_is(const char_type* __lo, const char_type* __hi, mask* __vec) const;

      virtual const char_type*
      do_scan_is(mask __m, const char_type* __lo, const char_type* __hi) const;

      virtual const char_type*
      do_scan_not(mask __m, const char_type* __lo,
		  const char_type* __hi) const;

      virtual char_type
      do_toupper(char_type __c) const;

      virtual const char_type*
      do_toupper(char_type* __lo, const char_type* __hi) const;

      virtual char_type
      do_tolower(char_type __c) const;

      virtual const char_type*
      do_tolower(char_type* __lo, const char_type* __hi) const;

      virtual char_type
      do_widen(char __c) const;

      virtual const char*
      do_widen(const char* __lo, const char* __hi, char_type* __dest) const;

      virtual char
      do_narrow(char_type, char __dfault) const;

      virtual const char_type*
      do_narrow(const char_type* __lo, const char_type* __hi,
		char __dfault, char* __dest) const;
    };

  template<typename _CharT>
    locale::id ctype<_CharT>::id;

  // 22.2.1.3  ctype<char> specialization.
  /**
   *  @brief  The ctype<char> specialization.
   *
   *  This class defines classification and conversion functions for
   *  the char type.  It gets used by char streams for many I/O
   *  operations.  The char specialization provides a number of
   *  optimizations as well.
  */
  template<>
    class ctype<char> : public locale::facet, public ctype_base
    {
    public:
      // Types:
      /// Typedef for the template parameter char.
      typedef char		char_type;

    protected:
      // Data Members:
      __c_locale		_M_c_locale_ctype;
      bool			_M_del;
      __to_type			_M_toupper;
      __to_type			_M_tolower;
      const mask*		_M_table;
      mutable char		_M_widen_ok;
      mutable char		_M_widen[1 + static_cast<unsigned char>(-1)];
      mutable char		_M_narrow[1 + static_cast<unsigned char>(-1)];
      mutable char		_M_narrow_ok;	// 0 uninitialized, 1 init,
						// 2 memcpy can't be used

    public:
      /// The facet id for ctype<char>
      static locale::id        id;
      /// The size of the mask table.  It is SCHAR_MAX + 1.
      static const size_t      table_size = 1 + static_cast<unsigned char>(-1);

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param table If non-zero, table is used as the per-char mask.
       *               Else classic_table() is used.
       *  @param del   If true, passes ownership of table to this facet.
       *  @param refs  Passed to the base facet class.
      */
      explicit
      ctype(const mask* __table = 0, bool __del = false, size_t __refs = 0);

      /**
       *  @brief  Constructor performs static initialization.
       *
       *  This constructor is used to construct the initial C locale facet.
       *
       *  @param cloc  Handle to C locale data.
       *  @param table If non-zero, table is used as the per-char mask.
       *  @param del   If true, passes ownership of table to this facet.
       *  @param refs  Passed to the base facet class.
      */
      explicit
      ctype(__c_locale __cloc, const mask* __table = 0, bool __del = false,
	    size_t __refs = 0);

      /**
       *  @brief  Test char classification.
       *
       *  This function compares the mask table[c] to @a m.
       *
       *  @param c  The char to compare the mask of.
       *  @param m  The mask to compare against.
       *  @return  True if m & table[c] is true, false otherwise.
      */
      inline bool
      is(mask __m, char __c) const;

      /**
       *  @brief  Return a mask array.
       *
       *  This function finds the mask for each char in the range [lo, hi) and
       *  successively writes it to vec.  vec must have as many elements as
       *  the char array.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param vec  Pointer to an array of mask storage.
       *  @return  @a hi.
      */
      inline const char*
      is(const char* __lo, const char* __hi, mask* __vec) const;

      /**
       *  @brief  Find char matching a mask
       *
       *  This function searches for and returns the first char in [lo,hi) for
       *  which is(m,char) is true.
       *
       *  @param m  The mask to compare against.
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  Pointer to a matching char if found, else @a hi.
      */
      inline const char*
      scan_is(mask __m, const char* __lo, const char* __hi) const;

      /**
       *  @brief  Find char not matching a mask
       *
       *  This function searches for and returns a pointer to the first char
       *  in [lo,hi) for which is(m,char) is false.
       *
       *  @param m  The mask to compare against.
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  Pointer to a non-matching char if found, else @a hi.
      */
      inline const char*
      scan_not(mask __m, const char* __lo, const char* __hi) const;

      /**
       *  @brief  Convert to uppercase.
       *
       *  This function converts the char argument to uppercase if possible.
       *  If not possible (for example, '2'), returns the argument.
       *
       *  toupper() acts as if it returns ctype<char>::do_toupper(c).
       *  do_toupper() must always return the same result for the same input.
       *
       *  @param c  The char to convert.
       *  @return  The uppercase char if convertible, else @a c.
      */
      char_type
      toupper(char_type __c) const
      { return this->do_toupper(__c); }

      /**
       *  @brief  Convert array to uppercase.
       *
       *  This function converts each char in the range [lo,hi) to uppercase
       *  if possible.  Other chars remain untouched.
       *
       *  toupper() acts as if it returns ctype<char>:: do_toupper(lo, hi).
       *  do_toupper() must always return the same result for the same input.
       *
       *  @param lo  Pointer to first char in range.
       *  @param hi  Pointer to end of range.
       *  @return  @a hi.
      */
      const char_type*
      toupper(char_type *__lo, const char_type* __hi) const
      { return this->do_toupper(__lo, __hi); }

      /**
       *  @brief  Convert to lowercase.
       *
       *  This function converts the char argument to lowercase if possible.
       *  If not possible (for example, '2'), returns the argument.
       *
       *  tolower() acts as if it returns ctype<char>::do_tolower(c).
       *  do_tolower() must always return the same result for the same input.
       *
       *  @param c  The char to convert.
       *  @return  The lowercase char if convertible, else @a c.
      */
      char_type
      tolower(char_type __c) const
      { return this->do_tolower(__c); }

      /**
       *  @brief  Convert array to lowercase.
       *
       *  This function converts each char in the range [lo,hi) to lowercase
       *  if possible.  Other chars remain untouched.
       *
       *  tolower() acts as if it returns ctype<char>:: do_tolower(lo, hi).
       *  do_tolower() must always return the same result for the same input.
       *
       *  @param lo  Pointer to first char in range.
       *  @param hi  Pointer to end of range.
       *  @return  @a hi.
      */
      const char_type*
      tolower(char_type* __lo, const char_type* __hi) const
      { return this->do_tolower(__lo, __hi); }

      /**
       *  @brief  Widen char
       *
       *  This function converts the char to char_type using the simplest
       *  reasonable transformation.  For an underived ctype<char> facet, the
       *  argument will be returned unchanged.
       *
       *  This function works as if it returns ctype<char>::do_widen(c).
       *  do_widen() must always return the same result for the same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param c  The char to convert.
       *  @return  The converted character.
      */
      char_type
      widen(char __c) const
      {
	if (_M_widen_ok)
	  return _M_widen[static_cast<unsigned char>(__c)];
	this->_M_widen_init();
	return this->do_widen(__c);
      }

      /**
       *  @brief  Widen char array
       *
       *  This function converts each char in the input to char using the
       *  simplest reasonable transformation.  For an underived ctype<char>
       *  facet, the argument will be copied unchanged.
       *
       *  This function works as if it returns ctype<char>::do_widen(c).
       *  do_widen() must always return the same result for the same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param lo  Pointer to first char in range.
       *  @param hi  Pointer to end of range.
       *  @param to  Pointer to the destination array.
       *  @return  @a hi.
      */
      const char*
      widen(const char* __lo, const char* __hi, char_type* __to) const
      {
	if (_M_widen_ok == 1)
	  {
	    memcpy(__to, __lo, __hi - __lo);
	    return __hi;
	  }
	if (!_M_widen_ok)
	  _M_widen_init();
	return this->do_widen(__lo, __hi, __to);
      }

      /**
       *  @brief  Narrow char
       *
       *  This function converts the char to char using the simplest
       *  reasonable transformation.  If the conversion fails, dfault is
       *  returned instead.  For an underived ctype<char> facet, @a c
       *  will be returned unchanged.
       *
       *  This function works as if it returns ctype<char>::do_narrow(c).
       *  do_narrow() must always return the same result for the same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param c  The char to convert.
       *  @param dfault  Char to return if conversion fails.
       *  @return  The converted character.
      */
      char
      narrow(char_type __c, char __dfault) const
      {
	if (_M_narrow[static_cast<unsigned char>(__c)])
	  return _M_narrow[static_cast<unsigned char>(__c)];
	const char __t = do_narrow(__c, __dfault);
	if (__t != __dfault)
	  _M_narrow[static_cast<unsigned char>(__c)] = __t;
	return __t;
      }

      /**
       *  @brief  Narrow char array
       *
       *  This function converts each char in the input to char using the
       *  simplest reasonable transformation and writes the results to the
       *  destination array.  For any char in the input that cannot be
       *  converted, @a dfault is used instead.  For an underived ctype<char>
       *  facet, the argument will be copied unchanged.
       *
       *  This function works as if it returns ctype<char>::do_narrow(lo, hi,
       *  dfault, to).  do_narrow() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param dfault  Char to use if conversion fails.
       *  @param to  Pointer to the destination array.
       *  @return  @a hi.
      */
      const char_type*
      narrow(const char_type* __lo, const char_type* __hi,
	     char __dfault, char *__to) const
      {
	if (__builtin_expect(_M_narrow_ok == 1, true))
	  {
	    memcpy(__to, __lo, __hi - __lo);
	    return __hi;
	  }
	if (!_M_narrow_ok)
	  _M_narrow_init();
	return this->do_narrow(__lo, __hi, __dfault, __to);
      }

    protected:
      /// Returns a pointer to the mask table provided to the constructor, or
      /// the default from classic_table() if none was provided.
      const mask*
      table() const throw()
      { return _M_table; }

      /// Returns a pointer to the C locale mask table.
      static const mask*
      classic_table() throw();

      /**
       *  @brief  Destructor.
       *
       *  This function deletes table() if @a del was true in the
       *  constructor.
      */
      virtual
      ~ctype();

      /**
       *  @brief  Convert to uppercase.
       *
       *  This virtual function converts the char argument to uppercase if
       *  possible.  If not possible (for example, '2'), returns the argument.
       *
       *  do_toupper() is a hook for a derived facet to change the behavior of
       *  uppercasing.  do_toupper() must always return the same result for
       *  the same input.
       *
       *  @param c  The char to convert.
       *  @return  The uppercase char if convertible, else @a c.
      */
      virtual char_type
      do_toupper(char_type) const;

      /**
       *  @brief  Convert array to uppercase.
       *
       *  This virtual function converts each char in the range [lo,hi) to
       *  uppercase if possible.  Other chars remain untouched.
       *
       *  do_toupper() is a hook for a derived facet to change the behavior of
       *  uppercasing.  do_toupper() must always return the same result for
       *  the same input.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_toupper(char_type* __lo, const char_type* __hi) const;

      /**
       *  @brief  Convert to lowercase.
       *
       *  This virtual function converts the char argument to lowercase if
       *  possible.  If not possible (for example, '2'), returns the argument.
       *
       *  do_tolower() is a hook for a derived facet to change the behavior of
       *  lowercasing.  do_tolower() must always return the same result for
       *  the same input.
       *
       *  @param c  The char to convert.
       *  @return  The lowercase char if convertible, else @a c.
      */
      virtual char_type
      do_tolower(char_type) const;

      /**
       *  @brief  Convert array to lowercase.
       *
       *  This virtual function converts each char in the range [lo,hi) to
       *  lowercase if possible.  Other chars remain untouched.
       *
       *  do_tolower() is a hook for a derived facet to change the behavior of
       *  lowercasing.  do_tolower() must always return the same result for
       *  the same input.
       *
       *  @param lo  Pointer to first char in range.
       *  @param hi  Pointer to end of range.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_tolower(char_type* __lo, const char_type* __hi) const;

      /**
       *  @brief  Widen char
       *
       *  This virtual function converts the char to char using the simplest
       *  reasonable transformation.  For an underived ctype<char> facet, the
       *  argument will be returned unchanged.
       *
       *  do_widen() is a hook for a derived facet to change the behavior of
       *  widening.  do_widen() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param c  The char to convert.
       *  @return  The converted character.
      */
      virtual char_type
      do_widen(char __c) const
      { return __c; }

      /**
       *  @brief  Widen char array
       *
       *  This function converts each char in the range [lo,hi) to char using
       *  the simplest reasonable transformation.  For an underived
       *  ctype<char> facet, the argument will be copied unchanged.
       *
       *  do_widen() is a hook for a derived facet to change the behavior of
       *  widening.  do_widen() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param to  Pointer to the destination array.
       *  @return  @a hi.
      */
      virtual const char*
      do_widen(const char* __lo, const char* __hi, char_type* __dest) const
      {
	memcpy(__dest, __lo, __hi - __lo);
	return __hi;
      }

      /**
       *  @brief  Narrow char
       *
       *  This virtual function converts the char to char using the simplest
       *  reasonable transformation.  If the conversion fails, dfault is
       *  returned instead.  For an underived ctype<char> facet, @a c will be
       *  returned unchanged.
       *
       *  do_narrow() is a hook for a derived facet to change the behavior of
       *  narrowing.  do_narrow() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param c  The char to convert.
       *  @param dfault  Char to return if conversion fails.
       *  @return  The converted char.
      */
      virtual char
      do_narrow(char_type __c, char) const
      { return __c; }

      /**
       *  @brief  Narrow char array to char array
       *
       *  This virtual function converts each char in the range [lo,hi) to
       *  char using the simplest reasonable transformation and writes the
       *  results to the destination array.  For any char in the input that
       *  cannot be converted, @a dfault is used instead.  For an underived
       *  ctype<char> facet, the argument will be copied unchanged.
       *
       *  do_narrow() is a hook for a derived facet to change the behavior of
       *  narrowing.  do_narrow() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param dfault  Char to use if conversion fails.
       *  @param to  Pointer to the destination array.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_narrow(const char_type* __lo, const char_type* __hi,
		char, char* __dest) const
      {
	memcpy(__dest, __lo, __hi - __lo);
	return __hi;
      }

    private:

      void _M_widen_init() const
      {
	char __tmp[sizeof(_M_widen)];
	for (size_t __i = 0; __i < sizeof(_M_widen); ++__i)
	  __tmp[__i] = __i;
	do_widen(__tmp, __tmp + sizeof(__tmp), _M_widen);

	_M_widen_ok = 1;
	// Set _M_widen_ok to 2 if memcpy can't be used.
	if (memcmp(__tmp, _M_widen, sizeof(_M_widen)))
	  _M_widen_ok = 2;
      }

      // Fill in the narrowing cache and flag whether all values are
      // valid or not.  _M_narrow_ok is set to 2 if memcpy can't
      // be used.
      void _M_narrow_init() const
      {
	char __tmp[sizeof(_M_narrow)];
	for (size_t __i = 0; __i < sizeof(_M_narrow); ++__i)
	  __tmp[__i] = __i;
	do_narrow(__tmp, __tmp + sizeof(__tmp), 0, _M_narrow);

	_M_narrow_ok = 1;
	if (memcmp(__tmp, _M_narrow, sizeof(_M_narrow)))
	  _M_narrow_ok = 2;
	else
	  {
	    // Deal with the special case of zero: renarrow with a
	    // different default and compare.
	    char __c;
	    do_narrow(__tmp, __tmp + 1, 1, &__c);
	    if (__c == 1)
	      _M_narrow_ok = 2;
	  }
      }
    };

  template<>
    const ctype<char>&
    use_facet<ctype<char> >(const locale& __loc);

#ifdef _GLIBCXX_USE_WCHAR_T
  // 22.2.1.3  ctype<wchar_t> specialization
  /**
   *  @brief  The ctype<wchar_t> specialization.
   *
   *  This class defines classification and conversion functions for the
   *  wchar_t type.  It gets used by wchar_t streams for many I/O operations.
   *  The wchar_t specialization provides a number of optimizations as well.
   *
   *  ctype<wchar_t> inherits its public methods from
   *  __ctype_abstract_base<wchar_t>.
  */
  template<>
    class ctype<wchar_t> : public __ctype_abstract_base<wchar_t>
    {
    public:
      // Types:
      /// Typedef for the template parameter wchar_t.
      typedef wchar_t		char_type;
      typedef wctype_t		__wmask_type;

    protected:
      __c_locale		_M_c_locale_ctype;

      // Pre-computed narrowed and widened chars.
      bool                      _M_narrow_ok;
      char                      _M_narrow[128];
      wint_t                    _M_widen[1 + static_cast<unsigned char>(-1)];

      // Pre-computed elements for do_is.
      mask                      _M_bit[16];
      __wmask_type              _M_wmask[16];

    public:
      // Data Members:
      /// The facet id for ctype<wchar_t>
      static locale::id		id;

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param refs  Passed to the base facet class.
      */
      explicit
      ctype(size_t __refs = 0);

      /**
       *  @brief  Constructor performs static initialization.
       *
       *  This constructor is used to construct the initial C locale facet.
       *
       *  @param cloc  Handle to C locale data.
       *  @param refs  Passed to the base facet class.
      */
      explicit
      ctype(__c_locale __cloc, size_t __refs = 0);

    protected:
      __wmask_type
      _M_convert_to_wmask(const mask __m) const;

      /// Destructor
      virtual
      ~ctype();

      /**
       *  @brief  Test wchar_t classification.
       *
       *  This function finds a mask M for @a c and compares it to mask @a m.
       *
       *  do_is() is a hook for a derived facet to change the behavior of
       *  classifying.  do_is() must always return the same result for the
       *  same input.
       *
       *  @param c  The wchar_t to find the mask of.
       *  @param m  The mask to compare against.
       *  @return  (M & m) != 0.
      */
      virtual bool
      do_is(mask __m, char_type __c) const;

      /**
       *  @brief  Return a mask array.
       *
       *  This function finds the mask for each wchar_t in the range [lo,hi)
       *  and successively writes it to vec.  vec must have as many elements
       *  as the input.
       *
       *  do_is() is a hook for a derived facet to change the behavior of
       *  classifying.  do_is() must always return the same result for the
       *  same input.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param vec  Pointer to an array of mask storage.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_is(const char_type* __lo, const char_type* __hi, mask* __vec) const;

      /**
       *  @brief  Find wchar_t matching mask
       *
       *  This function searches for and returns the first wchar_t c in
       *  [lo,hi) for which is(m,c) is true.
       *
       *  do_scan_is() is a hook for a derived facet to change the behavior of
       *  match searching.  do_is() must always return the same result for the
       *  same input.
       *
       *  @param m  The mask to compare against.
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  Pointer to a matching wchar_t if found, else @a hi.
      */
      virtual const char_type*
      do_scan_is(mask __m, const char_type* __lo, const char_type* __hi) const;

      /**
       *  @brief  Find wchar_t not matching mask
       *
       *  This function searches for and returns a pointer to the first
       *  wchar_t c of [lo,hi) for which is(m,c) is false.
       *
       *  do_scan_is() is a hook for a derived facet to change the behavior of
       *  match searching.  do_is() must always return the same result for the
       *  same input.
       *
       *  @param m  The mask to compare against.
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  Pointer to a non-matching wchar_t if found, else @a hi.
      */
      virtual const char_type*
      do_scan_not(mask __m, const char_type* __lo,
		  const char_type* __hi) const;

      /**
       *  @brief  Convert to uppercase.
       *
       *  This virtual function converts the wchar_t argument to uppercase if
       *  possible.  If not possible (for example, '2'), returns the argument.
       *
       *  do_toupper() is a hook for a derived facet to change the behavior of
       *  uppercasing.  do_toupper() must always return the same result for
       *  the same input.
       *
       *  @param c  The wchar_t to convert.
       *  @return  The uppercase wchar_t if convertible, else @a c.
      */
      virtual char_type
      do_toupper(char_type) const;

      /**
       *  @brief  Convert array to uppercase.
       *
       *  This virtual function converts each wchar_t in the range [lo,hi) to
       *  uppercase if possible.  Other elements remain untouched.
       *
       *  do_toupper() is a hook for a derived facet to change the behavior of
       *  uppercasing.  do_toupper() must always return the same result for
       *  the same input.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_toupper(char_type* __lo, const char_type* __hi) const;

      /**
       *  @brief  Convert to lowercase.
       *
       *  This virtual function converts the argument to lowercase if
       *  possible.  If not possible (for example, '2'), returns the argument.
       *
       *  do_tolower() is a hook for a derived facet to change the behavior of
       *  lowercasing.  do_tolower() must always return the same result for
       *  the same input.
       *
       *  @param c  The wchar_t to convert.
       *  @return  The lowercase wchar_t if convertible, else @a c.
      */
      virtual char_type
      do_tolower(char_type) const;

      /**
       *  @brief  Convert array to lowercase.
       *
       *  This virtual function converts each wchar_t in the range [lo,hi) to
       *  lowercase if possible.  Other elements remain untouched.
       *
       *  do_tolower() is a hook for a derived facet to change the behavior of
       *  lowercasing.  do_tolower() must always return the same result for
       *  the same input.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_tolower(char_type* __lo, const char_type* __hi) const;

      /**
       *  @brief  Widen char to wchar_t
       *
       *  This virtual function converts the char to wchar_t using the
       *  simplest reasonable transformation.  For an underived ctype<wchar_t>
       *  facet, the argument will be cast to wchar_t.
       *
       *  do_widen() is a hook for a derived facet to change the behavior of
       *  widening.  do_widen() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param c  The char to convert.
       *  @return  The converted wchar_t.
      */
      virtual char_type
      do_widen(char) const;

      /**
       *  @brief  Widen char array to wchar_t array
       *
       *  This function converts each char in the input to wchar_t using the
       *  simplest reasonable transformation.  For an underived ctype<wchar_t>
       *  facet, the argument will be copied, casting each element to wchar_t.
       *
       *  do_widen() is a hook for a derived facet to change the behavior of
       *  widening.  do_widen() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param lo  Pointer to start range.
       *  @param hi  Pointer to end of range.
       *  @param to  Pointer to the destination array.
       *  @return  @a hi.
      */
      virtual const char*
      do_widen(const char* __lo, const char* __hi, char_type* __dest) const;

      /**
       *  @brief  Narrow wchar_t to char
       *
       *  This virtual function converts the argument to char using
       *  the simplest reasonable transformation.  If the conversion
       *  fails, dfault is returned instead.  For an underived
       *  ctype<wchar_t> facet, @a c will be cast to char and
       *  returned.
       *
       *  do_narrow() is a hook for a derived facet to change the
       *  behavior of narrowing.  do_narrow() must always return the
       *  same result for the same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param c  The wchar_t to convert.
       *  @param dfault  Char to return if conversion fails.
       *  @return  The converted char.
      */
      virtual char
      do_narrow(char_type, char __dfault) const;

      /**
       *  @brief  Narrow wchar_t array to char array
       *
       *  This virtual function converts each wchar_t in the range [lo,hi) to
       *  char using the simplest reasonable transformation and writes the
       *  results to the destination array.  For any wchar_t in the input that
       *  cannot be converted, @a dfault is used instead.  For an underived
       *  ctype<wchar_t> facet, the argument will be copied, casting each
       *  element to char.
       *
       *  do_narrow() is a hook for a derived facet to change the behavior of
       *  narrowing.  do_narrow() must always return the same result for the
       *  same input.
       *
       *  Note: this is not what you want for codepage conversions.  See
       *  codecvt for that.
       *
       *  @param lo  Pointer to start of range.
       *  @param hi  Pointer to end of range.
       *  @param dfault  Char to use if conversion fails.
       *  @param to  Pointer to the destination array.
       *  @return  @a hi.
      */
      virtual const char_type*
      do_narrow(const char_type* __lo, const char_type* __hi,
		char __dfault, char* __dest) const;

      // For use at construction time only.
      void
      _M_initialize_ctype();
    };

  template<>
    const ctype<wchar_t>&
    use_facet<ctype<wchar_t> >(const locale& __loc);
#endif //_GLIBCXX_USE_WCHAR_T

  /// @brief  class ctype_byname [22.2.1.2].
  template<typename _CharT>
    class ctype_byname : public ctype<_CharT>
    {
    public:
      typedef _CharT		char_type;

      explicit
      ctype_byname(const char* __s, size_t __refs = 0);

    protected:
      virtual
      ~ctype_byname() { };
    };

  /// 22.2.1.4  Class ctype_byname specializations.
  template<>
    ctype_byname<char>::ctype_byname(const char*, size_t refs);

  template<>
    ctype_byname<wchar_t>::ctype_byname(const char*, size_t refs);

_GLIBCXX_END_NAMESPACE

// Include host and configuration specific ctype inlines.
#include <bits/ctype_inline.h>

// 22.2.1.5  Template class codecvt
#include <bits/codecvt.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // 22.2.2  The numeric category.
  class __num_base
  {
  public:
    // NB: Code depends on the order of _S_atoms_out elements.
    // Below are the indices into _S_atoms_out.
    enum
      {
        _S_ominus,
        _S_oplus,
        _S_ox,
        _S_oX,
        _S_odigits,
        _S_odigits_end = _S_odigits + 16,
        _S_oudigits = _S_odigits_end,
        _S_oudigits_end = _S_oudigits + 16,
        _S_oe = _S_odigits + 14,  // For scientific notation, 'e'
        _S_oE = _S_oudigits + 14, // For scientific notation, 'E'
	_S_oend = _S_oudigits_end
      };

    // A list of valid numeric literals for output.  This array
    // contains chars that will be passed through the current locale's
    // ctype<_CharT>.widen() and then used to render numbers.
    // For the standard "C" locale, this is
    // "-+xX0123456789abcdef0123456789ABCDEF".
    static const char* _S_atoms_out;

    // String literal of acceptable (narrow) input, for num_get.
    // "-+xX0123456789abcdefABCDEF"
    static const char* _S_atoms_in;

    enum
    {
      _S_iminus,
      _S_iplus,
      _S_ix,
      _S_iX,
      _S_izero,
      _S_ie = _S_izero + 14,
      _S_iE = _S_izero + 20,
      _S_iend = 26
    };

    // num_put
    // Construct and return valid scanf format for floating point types.
    static void
    _S_format_float(const ios_base& __io, char* __fptr, char __mod);
  };

  template<typename _CharT>
    struct __numpunct_cache : public locale::facet
    {
      const char*			_M_grouping;
      size_t                            _M_grouping_size;
      bool				_M_use_grouping;
      const _CharT*			_M_truename;
      size_t                            _M_truename_size;
      const _CharT*			_M_falsename;
      size_t                            _M_falsename_size;
      _CharT				_M_decimal_point;
      _CharT				_M_thousands_sep;

      // A list of valid numeric literals for output: in the standard
      // "C" locale, this is "-+xX0123456789abcdef0123456789ABCDEF".
      // This array contains the chars after having been passed
      // through the current locale's ctype<_CharT>.widen().
      _CharT				_M_atoms_out[__num_base::_S_oend];

      // A list of valid numeric literals for input: in the standard
      // "C" locale, this is "-+xX0123456789abcdefABCDEF"
      // This array contains the chars after having been passed
      // through the current locale's ctype<_CharT>.widen().
      _CharT				_M_atoms_in[__num_base::_S_iend];

      bool				_M_allocated;

      __numpunct_cache(size_t __refs = 0) : facet(__refs),
      _M_grouping(NULL), _M_grouping_size(0), _M_use_grouping(false),
      _M_truename(NULL), _M_truename_size(0), _M_falsename(NULL),
      _M_falsename_size(0), _M_decimal_point(_CharT()),
      _M_thousands_sep(_CharT()), _M_allocated(false)
      { }

      ~__numpunct_cache();

      void
      _M_cache(const locale& __loc);

    private:
      __numpunct_cache&
      operator=(const __numpunct_cache&);
      
      explicit
      __numpunct_cache(const __numpunct_cache&);
    };

  template<typename _CharT>
    __numpunct_cache<_CharT>::~__numpunct_cache()
    {
      if (_M_allocated)
	{
	  delete [] _M_grouping;
	  delete [] _M_truename;
	  delete [] _M_falsename;
	}
    }

  /**
   *  @brief  Numpunct facet.
   *
   *  This facet stores several pieces of information related to printing and
   *  scanning numbers, such as the decimal point character.  It takes a
   *  template parameter specifying the char type.  The numpunct facet is
   *  used by streams for many I/O operations involving numbers.
   *
   *  The numpunct template uses protected virtual functions to provide the
   *  actual results.  The public accessors forward the call to the virtual
   *  functions.  These virtual functions are hooks for developers to
   *  implement the behavior they require from a numpunct facet.
  */
  template<typename _CharT>
    class numpunct : public locale::facet
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT			char_type;
      typedef basic_string<_CharT>	string_type;
      //@}
      typedef __numpunct_cache<_CharT>  __cache_type;

    protected:
      __cache_type*			_M_data;

    public:
      /// Numpunct facet id.
      static locale::id			id;

      /**
       *  @brief  Numpunct constructor.
       *
       *  @param  refs  Refcount to pass to the base class.
       */
      explicit
      numpunct(size_t __refs = 0) : facet(__refs), _M_data(NULL)
      { _M_initialize_numpunct(); }

      /**
       *  @brief  Internal constructor.  Not for general use.
       *
       *  This is a constructor for use by the library itself to set up the
       *  predefined locale facets.
       *
       *  @param  cache  __numpunct_cache object.
       *  @param  refs  Refcount to pass to the base class.
       */
      explicit
      numpunct(__cache_type* __cache, size_t __refs = 0)
      : facet(__refs), _M_data(__cache)
      { _M_initialize_numpunct(); }

      /**
       *  @brief  Internal constructor.  Not for general use.
       *
       *  This is a constructor for use by the library itself to set up new
       *  locales.
       *
       *  @param  cloc  The "C" locale.
       *  @param  refs  Refcount to pass to the base class.
       */
      explicit
      numpunct(__c_locale __cloc, size_t __refs = 0)
      : facet(__refs), _M_data(NULL)
      { _M_initialize_numpunct(__cloc); }

      /**
       *  @brief  Return decimal point character.
       *
       *  This function returns a char_type to use as a decimal point.  It
       *  does so by returning returning
       *  numpunct<char_type>::do_decimal_point().
       *
       *  @return  @a char_type representing a decimal point.
      */
      char_type
      decimal_point() const
      { return this->do_decimal_point(); }

      /**
       *  @brief  Return thousands separator character.
       *
       *  This function returns a char_type to use as a thousands
       *  separator.  It does so by returning returning
       *  numpunct<char_type>::do_thousands_sep().
       *
       *  @return  char_type representing a thousands separator.
      */
      char_type
      thousands_sep() const
      { return this->do_thousands_sep(); }

      /**
       *  @brief  Return grouping specification.
       *
       *  This function returns a string representing groupings for the
       *  integer part of a number.  Groupings indicate where thousands
       *  separators should be inserted in the integer part of a number.
       *
       *  Each char in the return string is interpret as an integer
       *  rather than a character.  These numbers represent the number
       *  of digits in a group.  The first char in the string
       *  represents the number of digits in the least significant
       *  group.  If a char is negative, it indicates an unlimited
       *  number of digits for the group.  If more chars from the
       *  string are required to group a number, the last char is used
       *  repeatedly.
       *
       *  For example, if the grouping() returns "\003\002" and is
       *  applied to the number 123456789, this corresponds to
       *  12,34,56,789.  Note that if the string was "32", this would
       *  put more than 50 digits into the least significant group if
       *  the character set is ASCII.
       *
       *  The string is returned by calling
       *  numpunct<char_type>::do_grouping().
       *
       *  @return  string representing grouping specification.
      */
      string
      grouping() const
      { return this->do_grouping(); }

      /**
       *  @brief  Return string representation of bool true.
       *
       *  This function returns a string_type containing the text
       *  representation for true bool variables.  It does so by calling
       *  numpunct<char_type>::do_truename().
       *
       *  @return  string_type representing printed form of true.
      */
      string_type
      truename() const
      { return this->do_truename(); }

      /**
       *  @brief  Return string representation of bool false.
       *
       *  This function returns a string_type containing the text
       *  representation for false bool variables.  It does so by calling
       *  numpunct<char_type>::do_falsename().
       *
       *  @return  string_type representing printed form of false.
      */
      string_type
      falsename() const
      { return this->do_falsename(); }

    protected:
      /// Destructor.
      virtual
      ~numpunct();

      /**
       *  @brief  Return decimal point character.
       *
       *  Returns a char_type to use as a decimal point.  This function is a
       *  hook for derived classes to change the value returned.
       *
       *  @return  @a char_type representing a decimal point.
      */
      virtual char_type
      do_decimal_point() const
      { return _M_data->_M_decimal_point; }

      /**
       *  @brief  Return thousands separator character.
       *
       *  Returns a char_type to use as a thousands separator.  This function
       *  is a hook for derived classes to change the value returned.
       *
       *  @return  @a char_type representing a thousands separator.
      */
      virtual char_type
      do_thousands_sep() const
      { return _M_data->_M_thousands_sep; }

      /**
       *  @brief  Return grouping specification.
       *
       *  Returns a string representing groupings for the integer part of a
       *  number.  This function is a hook for derived classes to change the
       *  value returned.  @see grouping() for details.
       *
       *  @return  String representing grouping specification.
      */
      virtual string
      do_grouping() const
      { return _M_data->_M_grouping; }

      /**
       *  @brief  Return string representation of bool true.
       *
       *  Returns a string_type containing the text representation for true
       *  bool variables.  This function is a hook for derived classes to
       *  change the value returned.
       *
       *  @return  string_type representing printed form of true.
      */
      virtual string_type
      do_truename() const
      { return _M_data->_M_truename; }

      /**
       *  @brief  Return string representation of bool false.
       *
       *  Returns a string_type containing the text representation for false
       *  bool variables.  This function is a hook for derived classes to
       *  change the value returned.
       *
       *  @return  string_type representing printed form of false.
      */
      virtual string_type
      do_falsename() const
      { return _M_data->_M_falsename; }

      // For use at construction time only.
      void
      _M_initialize_numpunct(__c_locale __cloc = NULL);
    };

  template<typename _CharT>
    locale::id numpunct<_CharT>::id;

  template<>
    numpunct<char>::~numpunct();

  template<>
    void
    numpunct<char>::_M_initialize_numpunct(__c_locale __cloc);

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    numpunct<wchar_t>::~numpunct();

  template<>
    void
    numpunct<wchar_t>::_M_initialize_numpunct(__c_locale __cloc);
#endif

  /// @brief  class numpunct_byname [22.2.3.2].
  template<typename _CharT>
    class numpunct_byname : public numpunct<_CharT>
    {
    public:
      typedef _CharT			char_type;
      typedef basic_string<_CharT>	string_type;

      explicit
      numpunct_byname(const char* __s, size_t __refs = 0)
      : numpunct<_CharT>(__refs)
      {
	if (std::strcmp(__s, "C") != 0 && std::strcmp(__s, "POSIX") != 0)
	  {
	    __c_locale __tmp;
	    this->_S_create_c_locale(__tmp, __s);
	    this->_M_initialize_numpunct(__tmp);
	    this->_S_destroy_c_locale(__tmp);
	  }
      }

    protected:
      virtual
      ~numpunct_byname() { }
    };

_GLIBCXX_BEGIN_LDBL_NAMESPACE
  /**
   *  @brief  Facet for parsing number strings.
   *
   *  This facet encapsulates the code to parse and return a number
   *  from a string.  It is used by the istream numeric extraction
   *  operators.
   *
   *  The num_get template uses protected virtual functions to provide the
   *  actual results.  The public accessors forward the call to the virtual
   *  functions.  These virtual functions are hooks for developers to
   *  implement the behavior they require from the num_get facet.
  */
  template<typename _CharT, typename _InIter>
    class num_get : public locale::facet
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT			char_type;
      typedef _InIter			iter_type;
      //@}

      /// Numpunct facet id.
      static locale::id			id;

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param refs  Passed to the base facet class.
      */
      explicit
      num_get(size_t __refs = 0) : facet(__refs) { }

      /**
       *  @brief  Numeric parsing.
       *
       *  Parses the input stream into the bool @a v.  It does so by calling
       *  num_get::do_get().
       *
       *  If ios_base::boolalpha is set, attempts to read
       *  ctype<CharT>::truename() or ctype<CharT>::falsename().  Sets
       *  @a v to true or false if successful.  Sets err to
       *  ios_base::failbit if reading the string fails.  Sets err to
       *  ios_base::eofbit if the stream is emptied.
       *
       *  If ios_base::boolalpha is not set, proceeds as with reading a long,
       *  except if the value is 1, sets @a v to true, if the value is 0, sets
       *  @a v to false, and otherwise set err to ios_base::failbit.
       *
       *  @param  in  Start of input stream.
       *  @param  end  End of input stream.
       *  @param  io  Source of locale and flags.
       *  @param  err  Error flags to set.
       *  @param  v  Value to format and insert.
       *  @return  Iterator after reading.
      */
      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, bool& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      //@{
      /**
       *  @brief  Numeric parsing.
       *
       *  Parses the input stream into the integral variable @a v.  It does so
       *  by calling num_get::do_get().
       *
       *  Parsing is affected by the flag settings in @a io.
       *
       *  The basic parse is affected by the value of io.flags() &
       *  ios_base::basefield.  If equal to ios_base::oct, parses like the
       *  scanf %o specifier.  Else if equal to ios_base::hex, parses like %X
       *  specifier.  Else if basefield equal to 0, parses like the %i
       *  specifier.  Otherwise, parses like %d for signed and %u for unsigned
       *  types.  The matching type length modifier is also used.
       *
       *  Digit grouping is intrepreted according to numpunct::grouping() and
       *  numpunct::thousands_sep().  If the pattern of digit groups isn't
       *  consistent, sets err to ios_base::failbit.
       *
       *  If parsing the string yields a valid value for @a v, @a v is set.
       *  Otherwise, sets err to ios_base::failbit and leaves @a v unaltered.
       *  Sets err to ios_base::eofbit if the stream is emptied.
       *
       *  @param  in  Start of input stream.
       *  @param  end  End of input stream.
       *  @param  io  Source of locale and flags.
       *  @param  err  Error flags to set.
       *  @param  v  Value to format and insert.
       *  @return  Iterator after reading.
      */
      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, long& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, unsigned short& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, unsigned int& __v)   const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, unsigned long& __v)  const
      { return this->do_get(__in, __end, __io, __err, __v); }

#ifdef _GLIBCXX_USE_LONG_LONG
      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, long long& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, unsigned long long& __v)  const
      { return this->do_get(__in, __end, __io, __err, __v); }
#endif
      //@}

      //@{
      /**
       *  @brief  Numeric parsing.
       *
       *  Parses the input stream into the integral variable @a v.  It does so
       *  by calling num_get::do_get().
       *
       *  The input characters are parsed like the scanf %g specifier.  The
       *  matching type length modifier is also used.
       *
       *  The decimal point character used is numpunct::decimal_point().
       *  Digit grouping is intrepreted according to numpunct::grouping() and
       *  numpunct::thousands_sep().  If the pattern of digit groups isn't
       *  consistent, sets err to ios_base::failbit.
       *
       *  If parsing the string yields a valid value for @a v, @a v is set.
       *  Otherwise, sets err to ios_base::failbit and leaves @a v unaltered.
       *  Sets err to ios_base::eofbit if the stream is emptied.
       *
       *  @param  in  Start of input stream.
       *  @param  end  End of input stream.
       *  @param  io  Source of locale and flags.
       *  @param  err  Error flags to set.
       *  @param  v  Value to format and insert.
       *  @return  Iterator after reading.
      */
      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, float& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, double& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, long double& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }
      //@}

      /**
       *  @brief  Numeric parsing.
       *
       *  Parses the input stream into the pointer variable @a v.  It does so
       *  by calling num_get::do_get().
       *
       *  The input characters are parsed like the scanf %p specifier.
       *
       *  Digit grouping is intrepreted according to numpunct::grouping() and
       *  numpunct::thousands_sep().  If the pattern of digit groups isn't
       *  consistent, sets err to ios_base::failbit.
       *
       *  Note that the digit grouping effect for pointers is a bit ambiguous
       *  in the standard and shouldn't be relied on.  See DR 344.
       *
       *  If parsing the string yields a valid value for @a v, @a v is set.
       *  Otherwise, sets err to ios_base::failbit and leaves @a v unaltered.
       *  Sets err to ios_base::eofbit if the stream is emptied.
       *
       *  @param  in  Start of input stream.
       *  @param  end  End of input stream.
       *  @param  io  Source of locale and flags.
       *  @param  err  Error flags to set.
       *  @param  v  Value to format and insert.
       *  @return  Iterator after reading.
      */
      iter_type
      get(iter_type __in, iter_type __end, ios_base& __io,
	  ios_base::iostate& __err, void*& __v) const
      { return this->do_get(__in, __end, __io, __err, __v); }

    protected:
      /// Destructor.
      virtual ~num_get() { }

      iter_type
      _M_extract_float(iter_type, iter_type, ios_base&, ios_base::iostate&,
		       string& __xtrc) const;

      template<typename _ValueT>
        iter_type
        _M_extract_int(iter_type, iter_type, ios_base&, ios_base::iostate&,
		       _ValueT& __v) const;

      template<typename _CharT2>
      typename __gnu_cxx::__enable_if<__is_char<_CharT2>::__value, int>::__type
        _M_find(const _CharT2*, size_t __len, _CharT2 __c) const
        {
	  int __ret = -1;
	  if (__len <= 10)
	    {
	      if (__c >= _CharT2('0') && __c < _CharT2(_CharT2('0') + __len))
		__ret = __c - _CharT2('0');
	    }
	  else
	    {
	      if (__c >= _CharT2('0') && __c <= _CharT2('9'))
		__ret = __c - _CharT2('0');
	      else if (__c >= _CharT2('a') && __c <= _CharT2('f'))
		__ret = 10 + (__c - _CharT2('a'));
	      else if (__c >= _CharT2('A') && __c <= _CharT2('F'))
		__ret = 10 + (__c - _CharT2('A'));
	    }
	  return __ret;
	}

      template<typename _CharT2>
      typename __gnu_cxx::__enable_if<!__is_char<_CharT2>::__value, 
				      int>::__type
        _M_find(const _CharT2* __zero, size_t __len, _CharT2 __c) const
        {
	  int __ret = -1;
	  const char_type* __q = char_traits<_CharT2>::find(__zero, __len, __c);
	  if (__q)
	    {
	      __ret = __q - __zero;
	      if (__ret > 15)
		__ret -= 6;
	    }
	  return __ret;
	}

      //@{
      /**
       *  @brief  Numeric parsing.
       *
       *  Parses the input stream into the variable @a v.  This function is a
       *  hook for derived classes to change the value returned.  @see get()
       *  for more details.
       *
       *  @param  in  Start of input stream.
       *  @param  end  End of input stream.
       *  @param  io  Source of locale and flags.
       *  @param  err  Error flags to set.
       *  @param  v  Value to format and insert.
       *  @return  Iterator after reading.
      */
      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate&, bool&) const;


      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate&, long&) const;

      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	      unsigned short&) const;

      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	     unsigned int&) const;

      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	     unsigned long&) const;

#ifdef _GLIBCXX_USE_LONG_LONG
      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	     long long&) const;

      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	     unsigned long long&) const;
#endif

      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	     float&) const;

      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	     double&) const;

      // XXX GLIBCXX_ABI Deprecated
#if defined _GLIBCXX_LONG_DOUBLE_COMPAT && defined __LONG_DOUBLE_128__
      virtual iter_type
      __do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	       double&) const;
#else
      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	     long double&) const;
#endif

      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	     void*&) const;

      // XXX GLIBCXX_ABI Deprecated
#if defined _GLIBCXX_LONG_DOUBLE_COMPAT && defined __LONG_DOUBLE_128__
      virtual iter_type
      do_get(iter_type, iter_type, ios_base&, ios_base::iostate& __err,
	     long double&) const;
#endif
      //@}
    };

  template<typename _CharT, typename _InIter>
    locale::id num_get<_CharT, _InIter>::id;


  /**
   *  @brief  Facet for converting numbers to strings.
   *
   *  This facet encapsulates the code to convert a number to a string.  It is
   *  used by the ostream numeric insertion operators.
   *
   *  The num_put template uses protected virtual functions to provide the
   *  actual results.  The public accessors forward the call to the virtual
   *  functions.  These virtual functions are hooks for developers to
   *  implement the behavior they require from the num_put facet.
  */
  template<typename _CharT, typename _OutIter>
    class num_put : public locale::facet
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT		char_type;
      typedef _OutIter		iter_type;
      //@}

      /// Numpunct facet id.
      static locale::id		id;

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param refs  Passed to the base facet class.
      */
      explicit
      num_put(size_t __refs = 0) : facet(__refs) { }

      /**
       *  @brief  Numeric formatting.
       *
       *  Formats the boolean @a v and inserts it into a stream.  It does so
       *  by calling num_put::do_put().
       *
       *  If ios_base::boolalpha is set, writes ctype<CharT>::truename() or
       *  ctype<CharT>::falsename().  Otherwise formats @a v as an int.
       *
       *  @param  s  Stream to write to.
       *  @param  io  Source of locale and flags.
       *  @param  fill  Char_type to use for filling.
       *  @param  v  Value to format and insert.
       *  @return  Iterator after writing.
      */
      iter_type
      put(iter_type __s, ios_base& __f, char_type __fill, bool __v) const
      { return this->do_put(__s, __f, __fill, __v); }

      //@{
      /**
       *  @brief  Numeric formatting.
       *
       *  Formats the integral value @a v and inserts it into a
       *  stream.  It does so by calling num_put::do_put().
       *
       *  Formatting is affected by the flag settings in @a io.
       *
       *  The basic format is affected by the value of io.flags() &
       *  ios_base::basefield.  If equal to ios_base::oct, formats like the
       *  printf %o specifier.  Else if equal to ios_base::hex, formats like
       *  %x or %X with ios_base::uppercase unset or set respectively.
       *  Otherwise, formats like %d, %ld, %lld for signed and %u, %lu, %llu
       *  for unsigned values.  Note that if both oct and hex are set, neither
       *  will take effect.
       *
       *  If ios_base::showpos is set, '+' is output before positive values.
       *  If ios_base::showbase is set, '0' precedes octal values (except 0)
       *  and '0[xX]' precedes hex values.
       *
       *  Thousands separators are inserted according to numpunct::grouping()
       *  and numpunct::thousands_sep().  The decimal point character used is
       *  numpunct::decimal_point().
       *
       *  If io.width() is non-zero, enough @a fill characters are inserted to
       *  make the result at least that wide.  If
       *  (io.flags() & ios_base::adjustfield) == ios_base::left, result is
       *  padded at the end.  If ios_base::internal, then padding occurs
       *  immediately after either a '+' or '-' or after '0x' or '0X'.
       *  Otherwise, padding occurs at the beginning.
       *
       *  @param  s  Stream to write to.
       *  @param  io  Source of locale and flags.
       *  @param  fill  Char_type to use for filling.
       *  @param  v  Value to format and insert.
       *  @return  Iterator after writing.
      */
      iter_type
      put(iter_type __s, ios_base& __f, char_type __fill, long __v) const
      { return this->do_put(__s, __f, __fill, __v); }

      iter_type
      put(iter_type __s, ios_base& __f, char_type __fill,
	  unsigned long __v) const
      { return this->do_put(__s, __f, __fill, __v); }

#ifdef _GLIBCXX_USE_LONG_LONG
      iter_type
      put(iter_type __s, ios_base& __f, char_type __fill, long long __v) const
      { return this->do_put(__s, __f, __fill, __v); }

      iter_type
      put(iter_type __s, ios_base& __f, char_type __fill,
	  unsigned long long __v) const
      { return this->do_put(__s, __f, __fill, __v); }
#endif
      //@}

      //@{
      /**
       *  @brief  Numeric formatting.
       *
       *  Formats the floating point value @a v and inserts it into a stream.
       *  It does so by calling num_put::do_put().
       *
       *  Formatting is affected by the flag settings in @a io.
       *
       *  The basic format is affected by the value of io.flags() &
       *  ios_base::floatfield.  If equal to ios_base::fixed, formats like the
       *  printf %f specifier.  Else if equal to ios_base::scientific, formats
       *  like %e or %E with ios_base::uppercase unset or set respectively.
       *  Otherwise, formats like %g or %G depending on uppercase.  Note that
       *  if both fixed and scientific are set, the effect will also be like
       *  %g or %G.
       *
       *  The output precision is given by io.precision().  This precision is
       *  capped at numeric_limits::digits10 + 2 (different for double and
       *  long double).  The default precision is 6.
       *
       *  If ios_base::showpos is set, '+' is output before positive values.
       *  If ios_base::showpoint is set, a decimal point will always be
       *  output.
       *
       *  Thousands separators are inserted according to numpunct::grouping()
       *  and numpunct::thousands_sep().  The decimal point character used is
       *  numpunct::decimal_point().
       *
       *  If io.width() is non-zero, enough @a fill characters are inserted to
       *  make the result at least that wide.  If
       *  (io.flags() & ios_base::adjustfield) == ios_base::left, result is
       *  padded at the end.  If ios_base::internal, then padding occurs
       *  immediately after either a '+' or '-' or after '0x' or '0X'.
       *  Otherwise, padding occurs at the beginning.
       *
       *  @param  s  Stream to write to.
       *  @param  io  Source of locale and flags.
       *  @param  fill  Char_type to use for filling.
       *  @param  v  Value to format and insert.
       *  @return  Iterator after writing.
      */
      iter_type
      put(iter_type __s, ios_base& __f, char_type __fill, double __v) const
      { return this->do_put(__s, __f, __fill, __v); }

      iter_type
      put(iter_type __s, ios_base& __f, char_type __fill,
	  long double __v) const
      { return this->do_put(__s, __f, __fill, __v); }
      //@}

      /**
       *  @brief  Numeric formatting.
       *
       *  Formats the pointer value @a v and inserts it into a stream.  It
       *  does so by calling num_put::do_put().
       *
       *  This function formats @a v as an unsigned long with ios_base::hex
       *  and ios_base::showbase set.
       *
       *  @param  s  Stream to write to.
       *  @param  io  Source of locale and flags.
       *  @param  fill  Char_type to use for filling.
       *  @param  v  Value to format and insert.
       *  @return  Iterator after writing.
      */
      iter_type
      put(iter_type __s, ios_base& __f, char_type __fill,
	  const void* __v) const
      { return this->do_put(__s, __f, __fill, __v); }

    protected:
      template<typename _ValueT>
        iter_type
        _M_insert_float(iter_type, ios_base& __io, char_type __fill,
			char __mod, _ValueT __v) const;

      void
      _M_group_float(const char* __grouping, size_t __grouping_size,
		     char_type __sep, const char_type* __p, char_type* __new,
		     char_type* __cs, int& __len) const;

      template<typename _ValueT>
        iter_type
        _M_insert_int(iter_type, ios_base& __io, char_type __fill,
		      _ValueT __v) const;

      void
      _M_group_int(const char* __grouping, size_t __grouping_size,
		   char_type __sep, ios_base& __io, char_type* __new,
		   char_type* __cs, int& __len) const;

      void
      _M_pad(char_type __fill, streamsize __w, ios_base& __io,
	     char_type* __new, const char_type* __cs, int& __len) const;

      /// Destructor.
      virtual
      ~num_put() { };

      //@{
      /**
       *  @brief  Numeric formatting.
       *
       *  These functions do the work of formatting numeric values and
       *  inserting them into a stream. This function is a hook for derived
       *  classes to change the value returned.
       *
       *  @param  s  Stream to write to.
       *  @param  io  Source of locale and flags.
       *  @param  fill  Char_type to use for filling.
       *  @param  v  Value to format and insert.
       *  @return  Iterator after writing.
      */
      virtual iter_type
      do_put(iter_type, ios_base&, char_type __fill, bool __v) const;

      virtual iter_type
      do_put(iter_type, ios_base&, char_type __fill, long __v) const;

      virtual iter_type
      do_put(iter_type, ios_base&, char_type __fill, unsigned long) const;

#ifdef _GLIBCXX_USE_LONG_LONG
      virtual iter_type
      do_put(iter_type, ios_base&, char_type __fill, long long __v) const;

      virtual iter_type
      do_put(iter_type, ios_base&, char_type __fill, unsigned long long) const;
#endif

      virtual iter_type
      do_put(iter_type, ios_base&, char_type __fill, double __v) const;

      // XXX GLIBCXX_ABI Deprecated
#if defined _GLIBCXX_LONG_DOUBLE_COMPAT && defined __LONG_DOUBLE_128__
      virtual iter_type
      __do_put(iter_type, ios_base&, char_type __fill, double __v) const;
#else
      virtual iter_type
      do_put(iter_type, ios_base&, char_type __fill, long double __v) const;
#endif

      virtual iter_type
      do_put(iter_type, ios_base&, char_type __fill, const void* __v) const;

      // XXX GLIBCXX_ABI Deprecated
#if defined _GLIBCXX_LONG_DOUBLE_COMPAT && defined __LONG_DOUBLE_128__
      virtual iter_type
      do_put(iter_type, ios_base&, char_type __fill, long double __v) const;
#endif
      //@}
    };

  template <typename _CharT, typename _OutIter>
    locale::id num_put<_CharT, _OutIter>::id;

_GLIBCXX_END_LDBL_NAMESPACE

  /**
   *  @brief  Facet for localized string comparison.
   *
   *  This facet encapsulates the code to compare strings in a localized
   *  manner.
   *
   *  The collate template uses protected virtual functions to provide
   *  the actual results.  The public accessors forward the call to
   *  the virtual functions.  These virtual functions are hooks for
   *  developers to implement the behavior they require from the
   *  collate facet.
  */
  template<typename _CharT>
    class collate : public locale::facet
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT			char_type;
      typedef basic_string<_CharT>	string_type;
      //@}

    protected:
      // Underlying "C" library locale information saved from
      // initialization, needed by collate_byname as well.
      __c_locale			_M_c_locale_collate;

    public:
      /// Numpunct facet id.
      static locale::id			id;

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param refs  Passed to the base facet class.
      */
      explicit
      collate(size_t __refs = 0)
      : facet(__refs), _M_c_locale_collate(_S_get_c_locale())
      { }

      /**
       *  @brief  Internal constructor. Not for general use.
       *
       *  This is a constructor for use by the library itself to set up new
       *  locales.
       *
       *  @param cloc  The "C" locale.
       *  @param refs  Passed to the base facet class.
      */
      explicit
      collate(__c_locale __cloc, size_t __refs = 0)
      : facet(__refs), _M_c_locale_collate(_S_clone_c_locale(__cloc))
      { }

      /**
       *  @brief  Compare two strings.
       *
       *  This function compares two strings and returns the result by calling
       *  collate::do_compare().
       *
       *  @param lo1  Start of string 1.
       *  @param hi1  End of string 1.
       *  @param lo2  Start of string 2.
       *  @param hi2  End of string 2.
       *  @return  1 if string1 > string2, -1 if string1 < string2, else 0.
      */
      int
      compare(const _CharT* __lo1, const _CharT* __hi1,
	      const _CharT* __lo2, const _CharT* __hi2) const
      { return this->do_compare(__lo1, __hi1, __lo2, __hi2); }

      /**
       *  @brief  Transform string to comparable form.
       *
       *  This function is a wrapper for strxfrm functionality.  It takes the
       *  input string and returns a modified string that can be directly
       *  compared to other transformed strings.  In the "C" locale, this
       *  function just returns a copy of the input string.  In some other
       *  locales, it may replace two chars with one, change a char for
       *  another, etc.  It does so by returning collate::do_transform().
       *
       *  @param lo  Start of string.
       *  @param hi  End of string.
       *  @return  Transformed string_type.
      */
      string_type
      transform(const _CharT* __lo, const _CharT* __hi) const
      { return this->do_transform(__lo, __hi); }

      /**
       *  @brief  Return hash of a string.
       *
       *  This function computes and returns a hash on the input string.  It
       *  does so by returning collate::do_hash().
       *
       *  @param lo  Start of string.
       *  @param hi  End of string.
       *  @return  Hash value.
      */
      long
      hash(const _CharT* __lo, const _CharT* __hi) const
      { return this->do_hash(__lo, __hi); }

      // Used to abstract out _CharT bits in virtual member functions, below.
      int
      _M_compare(const _CharT*, const _CharT*) const;

      size_t
      _M_transform(_CharT*, const _CharT*, size_t) const;

  protected:
      /// Destructor.
      virtual
      ~collate()
      { _S_destroy_c_locale(_M_c_locale_collate); }

      /**
       *  @brief  Compare two strings.
       *
       *  This function is a hook for derived classes to change the value
       *  returned.  @see compare().
       *
       *  @param lo1  Start of string 1.
       *  @param hi1  End of string 1.
       *  @param lo2  Start of string 2.
       *  @param hi2  End of string 2.
       *  @return  1 if string1 > string2, -1 if string1 < string2, else 0.
      */
      virtual int
      do_compare(const _CharT* __lo1, const _CharT* __hi1,
		 const _CharT* __lo2, const _CharT* __hi2) const;

      /**
       *  @brief  Transform string to comparable form.
       *
       *  This function is a hook for derived classes to change the value
       *  returned.
       *
       *  @param lo1  Start of string 1.
       *  @param hi1  End of string 1.
       *  @param lo2  Start of string 2.
       *  @param hi2  End of string 2.
       *  @return  1 if string1 > string2, -1 if string1 < string2, else 0.
      */
      virtual string_type
      do_transform(const _CharT* __lo, const _CharT* __hi) const;

      /**
       *  @brief  Return hash of a string.
       *
       *  This function computes and returns a hash on the input string.  This
       *  function is a hook for derived classes to change the value returned.
       *
       *  @param lo  Start of string.
       *  @param hi  End of string.
       *  @return  Hash value.
      */
      virtual long
      do_hash(const _CharT* __lo, const _CharT* __hi) const;
    };

  template<typename _CharT>
    locale::id collate<_CharT>::id;

  // Specializations.
  template<>
    int
    collate<char>::_M_compare(const char*, const char*) const;

  template<>
    size_t
    collate<char>::_M_transform(char*, const char*, size_t) const;

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    int
    collate<wchar_t>::_M_compare(const wchar_t*, const wchar_t*) const;

  template<>
    size_t
    collate<wchar_t>::_M_transform(wchar_t*, const wchar_t*, size_t) const;
#endif

  /// @brief  class collate_byname [22.2.4.2].
  template<typename _CharT>
    class collate_byname : public collate<_CharT>
    {
    public:
      //@{
      /// Public typedefs
      typedef _CharT               char_type;
      typedef basic_string<_CharT> string_type;
      //@}

      explicit
      collate_byname(const char* __s, size_t __refs = 0)
      : collate<_CharT>(__refs)
      {
	if (std::strcmp(__s, "C") != 0 && std::strcmp(__s, "POSIX") != 0)
	  {
	    this->_S_destroy_c_locale(this->_M_c_locale_collate);
	    this->_S_create_c_locale(this->_M_c_locale_collate, __s);
	  }
      }

    protected:
      virtual
      ~collate_byname() { }
    };


  /**
   *  @brief  Time format ordering data.
   *
   *  This class provides an enum representing different orderings of day,
   *  month, and year.
  */
  class time_base
  {
  public:
    enum dateorder { no_order, dmy, mdy, ymd, ydm };
  };

  template<typename _CharT>
    struct __timepunct_cache : public locale::facet
    {
      // List of all known timezones, with GMT first.
      static const _CharT*		_S_timezones[14];

      const _CharT*			_M_date_format;
      const _CharT*			_M_date_era_format;
      const _CharT*			_M_time_format;
      const _CharT*			_M_time_era_format;
      const _CharT*			_M_date_time_format;
      const _CharT*			_M_date_time_era_format;
      const _CharT*			_M_am;
      const _CharT*			_M_pm;
      const _CharT*			_M_am_pm_format;

      // Day names, starting with "C"'s Sunday.
      const _CharT*			_M_day1;
      const _CharT*			_M_day2;
      const _CharT*			_M_day3;
      const _CharT*			_M_day4;
      const _CharT*			_M_day5;
      const _CharT*			_M_day6;
      const _CharT*			_M_day7;

      // Abbreviated day names, starting with "C"'s Sun.
      const _CharT*			_M_aday1;
      const _CharT*			_M_aday2;
      const _CharT*			_M_aday3;
      const _CharT*			_M_aday4;
      const _CharT*			_M_aday5;
      const _CharT*			_M_aday6;
      const _CharT*			_M_aday7;

      // Month names, starting with "C"'s January.
      const _CharT*			_M_month01;
      const _CharT*			_M_month02;
      const _CharT*			_M_month03;
      const _CharT*			_M_month04;
      const _CharT*			_M_month05;
      const _CharT*			_M_month06;
      const _CharT*			_M_month07;
      const _CharT*			_M_month08;
      const _CharT*			_M_month09;
      const _CharT*			_M_month10;
      const _CharT*			_M_month11;
      const _CharT*			_M_month12;

      // Abbreviated month names, starting with "C"'s Jan.
      const _CharT*			_M_amonth01;
      const _CharT*			_M_amonth02;
      const _CharT*			_M_amonth03;
      const _CharT*			_M_amonth04;
      const _CharT*			_M_amonth05;
      const _CharT*			_M_amonth06;
      const _CharT*			_M_amonth07;
      const _CharT*			_M_amonth08;
      const _CharT*			_M_amonth09;
      const _CharT*			_M_amonth10;
      const _CharT*			_M_amonth11;
      const _CharT*			_M_amonth12;

      bool				_M_allocated;

      __timepunct_cache(size_t __refs = 0) : facet(__refs),
      _M_date_format(NULL), _M_date_era_format(NULL), _M_time_format(NULL),
      _M_time_era_format(NULL), _M_date_time_format(NULL),
      _M_date_time_era_format(NULL), _M_am(NULL), _M_pm(NULL),
      _M_am_pm_format(NULL), _M_day1(NULL), _M_day2(NULL), _M_day3(NULL),
      _M_day4(NULL), _M_day5(NULL), _M_day6(NULL), _M_day7(NULL),
      _M_aday1(NULL), _M_aday2(NULL), _M_aday3(NULL), _M_aday4(NULL),
      _M_aday5(NULL), _M_aday6(NULL), _M_aday7(NULL), _M_month01(NULL),
      _M_month02(NULL), _M_month03(NULL), _M_month04(NULL), _M_month05(NULL),
      _M_month06(NULL), _M_month07(NULL), _M_month08(NULL), _M_month09(NULL),
      _M_month10(NULL), _M_month11(NULL), _M_month12(NULL), _M_amonth01(NULL),
      _M_amonth02(NULL), _M_amonth03(NULL), _M_amonth04(NULL),
      _M_amonth05(NULL), _M_amonth06(NULL), _M_amonth07(NULL),
      _M_amonth08(NULL), _M_amonth09(NULL), _M_amonth10(NULL),
      _M_amonth11(NULL), _M_amonth12(NULL), _M_allocated(false)
      { }

      ~__timepunct_cache();

      void
      _M_cache(const locale& __loc);

    private:
      __timepunct_cache&
      operator=(const __timepunct_cache&);
      
      explicit
      __timepunct_cache(const __timepunct_cache&);
    };

  template<typename _CharT>
    __timepunct_cache<_CharT>::~__timepunct_cache()
    {
      if (_M_allocated)
	{
	  // Unused.
	}
    }

  // Specializations.
  template<>
    const char*
    __timepunct_cache<char>::_S_timezones[14];

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    const wchar_t*
    __timepunct_cache<wchar_t>::_S_timezones[14];
#endif

  // Generic.
  template<typename _CharT>
    const _CharT* __timepunct_cache<_CharT>::_S_timezones[14];

  template<typename _CharT>
    class __timepunct : public locale::facet
    {
    public:
      // Types:
      typedef _CharT			__char_type;
      typedef basic_string<_CharT>	__string_type;
      typedef __timepunct_cache<_CharT>	__cache_type;

    protected:
      __cache_type*			_M_data;
      __c_locale			_M_c_locale_timepunct;
      const char*			_M_name_timepunct;

    public:
      /// Numpunct facet id.
      static locale::id			id;

      explicit
      __timepunct(size_t __refs = 0);

      explicit
      __timepunct(__cache_type* __cache, size_t __refs = 0);

      /**
       *  @brief  Internal constructor. Not for general use.
       *
       *  This is a constructor for use by the library itself to set up new
       *  locales.
       *
       *  @param cloc  The "C" locale.
       *  @param s  The name of a locale.
       *  @param refs  Passed to the base facet class.
      */
      explicit
      __timepunct(__c_locale __cloc, const char* __s, size_t __refs = 0);

      // FIXME: for error checking purposes _M_put should return the return
      // value of strftime/wcsftime.
      void
      _M_put(_CharT* __s, size_t __maxlen, const _CharT* __format,
	     const tm* __tm) const;

      void
      _M_date_formats(const _CharT** __date) const
      {
	// Always have default first.
	__date[0] = _M_data->_M_date_format;
	__date[1] = _M_data->_M_date_era_format;
      }

      void
      _M_time_formats(const _CharT** __time) const
      {
	// Always have default first.
	__time[0] = _M_data->_M_time_format;
	__time[1] = _M_data->_M_time_era_format;
      }

      void
      _M_date_time_formats(const _CharT** __dt) const
      {
	// Always have default first.
	__dt[0] = _M_data->_M_date_time_format;
	__dt[1] = _M_data->_M_date_time_era_format;
      }

      void
      _M_am_pm_format(const _CharT* __ampm) const
      { __ampm = _M_data->_M_am_pm_format; }

      void
      _M_am_pm(const _CharT** __ampm) const
      {
	__ampm[0] = _M_data->_M_am;
	__ampm[1] = _M_data->_M_pm;
      }

      void
      _M_days(const _CharT** __days) const
      {
	__days[0] = _M_data->_M_day1;
	__days[1] = _M_data->_M_day2;
	__days[2] = _M_data->_M_day3;
	__days[3] = _M_data->_M_day4;
	__days[4] = _M_data->_M_day5;
	__days[5] = _M_data->_M_day6;
	__days[6] = _M_data->_M_day7;
      }

      void
      _M_days_abbreviated(const _CharT** __days) const
      {
	__days[0] = _M_data->_M_aday1;
	__days[1] = _M_data->_M_aday2;
	__days[2] = _M_data->_M_aday3;
	__days[3] = _M_data->_M_aday4;
	__days[4] = _M_data->_M_aday5;
	__days[5] = _M_data->_M_aday6;
	__days[6] = _M_data->_M_aday7;
      }

      void
      _M_months(const _CharT** __months) const
      {
	__months[0] = _M_data->_M_month01;
	__months[1] = _M_data->_M_month02;
	__months[2] = _M_data->_M_month03;
	__months[3] = _M_data->_M_month04;
	__months[4] = _M_data->_M_month05;
	__months[5] = _M_data->_M_month06;
	__months[6] = _M_data->_M_month07;
	__months[7] = _M_data->_M_month08;
	__months[8] = _M_data->_M_month09;
	__months[9] = _M_data->_M_month10;
	__months[10] = _M_data->_M_month11;
	__months[11] = _M_data->_M_month12;
      }

      void
      _M_months_abbreviated(const _CharT** __months) const
      {
	__months[0] = _M_data->_M_amonth01;
	__months[1] = _M_data->_M_amonth02;
	__months[2] = _M_data->_M_amonth03;
	__months[3] = _M_data->_M_amonth04;
	__months[4] = _M_data->_M_amonth05;
	__months[5] = _M_data->_M_amonth06;
	__months[6] = _M_data->_M_amonth07;
	__months[7] = _M_data->_M_amonth08;
	__months[8] = _M_data->_M_amonth09;
	__months[9] = _M_data->_M_amonth10;
	__months[10] = _M_data->_M_amonth11;
	__months[11] = _M_data->_M_amonth12;
      }

    protected:
      virtual
      ~__timepunct();

      // For use at construction time only.
      void
      _M_initialize_timepunct(__c_locale __cloc = NULL);
    };

  template<typename _CharT>
    locale::id __timepunct<_CharT>::id;

  // Specializations.
  template<>
    void
    __timepunct<char>::_M_initialize_timepunct(__c_locale __cloc);

  template<>
    void
    __timepunct<char>::_M_put(char*, size_t, const char*, const tm*) const;

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    void
    __timepunct<wchar_t>::_M_initialize_timepunct(__c_locale __cloc);

  template<>
    void
    __timepunct<wchar_t>::_M_put(wchar_t*, size_t, const wchar_t*,
				 const tm*) const;
#endif

_GLIBCXX_END_NAMESPACE

  // Include host and configuration specific timepunct functions.
  #include <bits/time_members.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  /**
   *  @brief  Facet for parsing dates and times.
   *
   *  This facet encapsulates the code to parse and return a date or
   *  time from a string.  It is used by the istream numeric
   *  extraction operators.
   *
   *  The time_get template uses protected virtual functions to provide the
   *  actual results.  The public accessors forward the call to the virtual
   *  functions.  These virtual functions are hooks for developers to
   *  implement the behavior they require from the time_get facet.
  */
  template<typename _CharT, typename _InIter>
    class time_get : public locale::facet, public time_base
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT			char_type;
      typedef _InIter			iter_type;
      //@}
      typedef basic_string<_CharT>	__string_type;

      /// Numpunct facet id.
      static locale::id			id;

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param refs  Passed to the base facet class.
      */
      explicit
      time_get(size_t __refs = 0)
      : facet (__refs) { }

      /**
       *  @brief  Return preferred order of month, day, and year.
       *
       *  This function returns an enum from timebase::dateorder giving the
       *  preferred ordering if the format "x" given to time_put::put() only
       *  uses month, day, and year.  If the format "x" for the associated
       *  locale uses other fields, this function returns
       *  timebase::dateorder::noorder.
       *
       *  NOTE: The library always returns noorder at the moment.
       *
       *  @return  A member of timebase::dateorder.
      */
      dateorder
      date_order()  const
      { return this->do_date_order(); }

      /**
       *  @brief  Parse input time string.
       *
       *  This function parses a time according to the format "x" and puts the
       *  results into a user-supplied struct tm.  The result is returned by
       *  calling time_get::do_get_time().
       *
       *  If there is a valid time string according to format "x", @a tm will
       *  be filled in accordingly and the returned iterator will point to the
       *  first character beyond the time string.  If an error occurs before
       *  the end, err |= ios_base::failbit.  If parsing reads all the
       *  characters, err |= ios_base::eofbit.
       *
       *  @param  beg  Start of string to parse.
       *  @param  end  End of string to parse.
       *  @param  io  Source of the locale.
       *  @param  err  Error flags to set.
       *  @param  tm  Pointer to struct tm to fill in.
       *  @return  Iterator to first char beyond time string.
      */
      iter_type
      get_time(iter_type __beg, iter_type __end, ios_base& __io,
	       ios_base::iostate& __err, tm* __tm)  const
      { return this->do_get_time(__beg, __end, __io, __err, __tm); }

      /**
       *  @brief  Parse input date string.
       *
       *  This function parses a date according to the format "X" and puts the
       *  results into a user-supplied struct tm.  The result is returned by
       *  calling time_get::do_get_date().
       *
       *  If there is a valid date string according to format "X", @a tm will
       *  be filled in accordingly and the returned iterator will point to the
       *  first character beyond the date string.  If an error occurs before
       *  the end, err |= ios_base::failbit.  If parsing reads all the
       *  characters, err |= ios_base::eofbit.
       *
       *  @param  beg  Start of string to parse.
       *  @param  end  End of string to parse.
       *  @param  io  Source of the locale.
       *  @param  err  Error flags to set.
       *  @param  tm  Pointer to struct tm to fill in.
       *  @return  Iterator to first char beyond date string.
      */
      iter_type
      get_date(iter_type __beg, iter_type __end, ios_base& __io,
	       ios_base::iostate& __err, tm* __tm)  const
      { return this->do_get_date(__beg, __end, __io, __err, __tm); }

      /**
       *  @brief  Parse input weekday string.
       *
       *  This function parses a weekday name and puts the results into a
       *  user-supplied struct tm.  The result is returned by calling
       *  time_get::do_get_weekday().
       *
       *  Parsing starts by parsing an abbreviated weekday name.  If a valid
       *  abbreviation is followed by a character that would lead to the full
       *  weekday name, parsing continues until the full name is found or an
       *  error occurs.  Otherwise parsing finishes at the end of the
       *  abbreviated name.
       *
       *  If an error occurs before the end, err |= ios_base::failbit.  If
       *  parsing reads all the characters, err |= ios_base::eofbit.
       *
       *  @param  beg  Start of string to parse.
       *  @param  end  End of string to parse.
       *  @param  io  Source of the locale.
       *  @param  err  Error flags to set.
       *  @param  tm  Pointer to struct tm to fill in.
       *  @return  Iterator to first char beyond weekday name.
      */
      iter_type
      get_weekday(iter_type __beg, iter_type __end, ios_base& __io,
		  ios_base::iostate& __err, tm* __tm) const
      { return this->do_get_weekday(__beg, __end, __io, __err, __tm); }

      /**
       *  @brief  Parse input month string.
       *
       *  This function parses a month name and puts the results into a
       *  user-supplied struct tm.  The result is returned by calling
       *  time_get::do_get_monthname().
       *
       *  Parsing starts by parsing an abbreviated month name.  If a valid
       *  abbreviation is followed by a character that would lead to the full
       *  month name, parsing continues until the full name is found or an
       *  error occurs.  Otherwise parsing finishes at the end of the
       *  abbreviated name.
       *
       *  If an error occurs before the end, err |= ios_base::failbit.  If
       *  parsing reads all the characters, err |=
       *  ios_base::eofbit.
       *
       *  @param  beg  Start of string to parse.
       *  @param  end  End of string to parse.
       *  @param  io  Source of the locale.
       *  @param  err  Error flags to set.
       *  @param  tm  Pointer to struct tm to fill in.
       *  @return  Iterator to first char beyond month name.
      */
      iter_type
      get_monthname(iter_type __beg, iter_type __end, ios_base& __io,
		    ios_base::iostate& __err, tm* __tm) const
      { return this->do_get_monthname(__beg, __end, __io, __err, __tm); }

      /**
       *  @brief  Parse input year string.
       *
       *  This function reads up to 4 characters to parse a year string and
       *  puts the results into a user-supplied struct tm.  The result is
       *  returned by calling time_get::do_get_year().
       *
       *  4 consecutive digits are interpreted as a full year.  If there are
       *  exactly 2 consecutive digits, the library interprets this as the
       *  number of years since 1900.
       *
       *  If an error occurs before the end, err |= ios_base::failbit.  If
       *  parsing reads all the characters, err |= ios_base::eofbit.
       *
       *  @param  beg  Start of string to parse.
       *  @param  end  End of string to parse.
       *  @param  io  Source of the locale.
       *  @param  err  Error flags to set.
       *  @param  tm  Pointer to struct tm to fill in.
       *  @return  Iterator to first char beyond year.
      */
      iter_type
      get_year(iter_type __beg, iter_type __end, ios_base& __io,
	       ios_base::iostate& __err, tm* __tm) const
      { return this->do_get_year(__beg, __end, __io, __err, __tm); }

    protected:
      /// Destructor.
      virtual
      ~time_get() { }

      /**
       *  @brief  Return preferred order of month, day, and year.
       *
       *  This function returns an enum from timebase::dateorder giving the
       *  preferred ordering if the format "x" given to time_put::put() only
       *  uses month, day, and year.  This function is a hook for derived
       *  classes to change the value returned.
       *
       *  @return  A member of timebase::dateorder.
      */
      virtual dateorder
      do_date_order() const;

      /**
       *  @brief  Parse input time string.
       *
       *  This function parses a time according to the format "x" and puts the
       *  results into a user-supplied struct tm.  This function is a hook for
       *  derived classes to change the value returned.  @see get_time() for
       *  details.
       *
       *  @param  beg  Start of string to parse.
       *  @param  end  End of string to parse.
       *  @param  io  Source of the locale.
       *  @param  err  Error flags to set.
       *  @param  tm  Pointer to struct tm to fill in.
       *  @return  Iterator to first char beyond time string.
      */
      virtual iter_type
      do_get_time(iter_type __beg, iter_type __end, ios_base& __io,
		  ios_base::iostate& __err, tm* __tm) const;

      /**
       *  @brief  Parse input date string.
       *
       *  This function parses a date according to the format "X" and puts the
       *  results into a user-supplied struct tm.  This function is a hook for
       *  derived classes to change the value returned.  @see get_date() for
       *  details.
       *
       *  @param  beg  Start of string to parse.
       *  @param  end  End of string to parse.
       *  @param  io  Source of the locale.
       *  @param  err  Error flags to set.
       *  @param  tm  Pointer to struct tm to fill in.
       *  @return  Iterator to first char beyond date string.
      */
      virtual iter_type
      do_get_date(iter_type __beg, iter_type __end, ios_base& __io,
		  ios_base::iostate& __err, tm* __tm) const;

      /**
       *  @brief  Parse input weekday string.
       *
       *  This function parses a weekday name and puts the results into a
       *  user-supplied struct tm.  This function is a hook for derived
       *  classes to change the value returned.  @see get_weekday() for
       *  details.
       *
       *  @param  beg  Start of string to parse.
       *  @param  end  End of string to parse.
       *  @param  io  Source of the locale.
       *  @param  err  Error flags to set.
       *  @param  tm  Pointer to struct tm to fill in.
       *  @return  Iterator to first char beyond weekday name.
      */
      virtual iter_type
      do_get_weekday(iter_type __beg, iter_type __end, ios_base&,
		     ios_base::iostate& __err, tm* __tm) const;

      /**
       *  @brief  Parse input month string.
       *
       *  This function parses a month name and puts the results into a
       *  user-supplied struct tm.  This function is a hook for derived
       *  classes to change the value returned.  @see get_monthname() for
       *  details.
       *
       *  @param  beg  Start of string to parse.
       *  @param  end  End of string to parse.
       *  @param  io  Source of the locale.
       *  @param  err  Error flags to set.
       *  @param  tm  Pointer to struct tm to fill in.
       *  @return  Iterator to first char beyond month name.
      */
      virtual iter_type
      do_get_monthname(iter_type __beg, iter_type __end, ios_base&,
		       ios_base::iostate& __err, tm* __tm) const;

      /**
       *  @brief  Parse input year string.
       *
       *  This function reads up to 4 characters to parse a year string and
       *  puts the results into a user-supplied struct tm.  This function is a
       *  hook for derived classes to change the value returned.  @see
       *  get_year() for details.
       *
       *  @param  beg  Start of string to parse.
       *  @param  end  End of string to parse.
       *  @param  io  Source of the locale.
       *  @param  err  Error flags to set.
       *  @param  tm  Pointer to struct tm to fill in.
       *  @return  Iterator to first char beyond year.
      */
      virtual iter_type
      do_get_year(iter_type __beg, iter_type __end, ios_base& __io,
		  ios_base::iostate& __err, tm* __tm) const;

      // Extract numeric component of length __len.
      iter_type
      _M_extract_num(iter_type __beg, iter_type __end, int& __member,
		     int __min, int __max, size_t __len,
		     ios_base& __io, ios_base::iostate& __err) const;

      // Extract day or month name, or any unique array of string
      // literals in a const _CharT* array.
      iter_type
      _M_extract_name(iter_type __beg, iter_type __end, int& __member,
		      const _CharT** __names, size_t __indexlen,
		      ios_base& __io, ios_base::iostate& __err) const;

      // Extract on a component-by-component basis, via __format argument.
      iter_type
      _M_extract_via_format(iter_type __beg, iter_type __end, ios_base& __io,
			    ios_base::iostate& __err, tm* __tm,
			    const _CharT* __format) const;
    };

  template<typename _CharT, typename _InIter>
    locale::id time_get<_CharT, _InIter>::id;

  /// @brief  class time_get_byname [22.2.5.2].
  template<typename _CharT, typename _InIter>
    class time_get_byname : public time_get<_CharT, _InIter>
    {
    public:
      // Types:
      typedef _CharT			char_type;
      typedef _InIter			iter_type;

      explicit
      time_get_byname(const char*, size_t __refs = 0)
      : time_get<_CharT, _InIter>(__refs) { }

    protected:
      virtual
      ~time_get_byname() { }
    };

  /**
   *  @brief  Facet for outputting dates and times.
   *
   *  This facet encapsulates the code to format and output dates and times
   *  according to formats used by strftime().
   *
   *  The time_put template uses protected virtual functions to provide the
   *  actual results.  The public accessors forward the call to the virtual
   *  functions.  These virtual functions are hooks for developers to
   *  implement the behavior they require from the time_put facet.
  */
  template<typename _CharT, typename _OutIter>
    class time_put : public locale::facet
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT			char_type;
      typedef _OutIter			iter_type;
      //@}

      /// Numpunct facet id.
      static locale::id			id;

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param refs  Passed to the base facet class.
      */
      explicit
      time_put(size_t __refs = 0)
      : facet(__refs) { }

      /**
       *  @brief  Format and output a time or date.
       *
       *  This function formats the data in struct tm according to the
       *  provided format string.  The format string is interpreted as by
       *  strftime().
       *
       *  @param  s  The stream to write to.
       *  @param  io  Source of locale.
       *  @param  fill  char_type to use for padding.
       *  @param  tm  Struct tm with date and time info to format.
       *  @param  beg  Start of format string.
       *  @param  end  End of format string.
       *  @return  Iterator after writing.
       */
      iter_type
      put(iter_type __s, ios_base& __io, char_type __fill, const tm* __tm,
	  const _CharT* __beg, const _CharT* __end) const;

      /**
       *  @brief  Format and output a time or date.
       *
       *  This function formats the data in struct tm according to the
       *  provided format char and optional modifier.  The format and modifier
       *  are interpreted as by strftime().  It does so by returning
       *  time_put::do_put().
       *
       *  @param  s  The stream to write to.
       *  @param  io  Source of locale.
       *  @param  fill  char_type to use for padding.
       *  @param  tm  Struct tm with date and time info to format.
       *  @param  format  Format char.
       *  @param  mod  Optional modifier char.
       *  @return  Iterator after writing.
       */
      iter_type
      put(iter_type __s, ios_base& __io, char_type __fill,
	  const tm* __tm, char __format, char __mod = 0) const
      { return this->do_put(__s, __io, __fill, __tm, __format, __mod); }

    protected:
      /// Destructor.
      virtual
      ~time_put()
      { }

      /**
       *  @brief  Format and output a time or date.
       *
       *  This function formats the data in struct tm according to the
       *  provided format char and optional modifier.  This function is a hook
       *  for derived classes to change the value returned.  @see put() for
       *  more details.
       *
       *  @param  s  The stream to write to.
       *  @param  io  Source of locale.
       *  @param  fill  char_type to use for padding.
       *  @param  tm  Struct tm with date and time info to format.
       *  @param  format  Format char.
       *  @param  mod  Optional modifier char.
       *  @return  Iterator after writing.
       */
      virtual iter_type
      do_put(iter_type __s, ios_base& __io, char_type __fill, const tm* __tm,
	     char __format, char __mod) const;
    };

  template<typename _CharT, typename _OutIter>
    locale::id time_put<_CharT, _OutIter>::id;

  /// @brief  class time_put_byname [22.2.5.4].
  template<typename _CharT, typename _OutIter>
    class time_put_byname : public time_put<_CharT, _OutIter>
    {
    public:
      // Types:
      typedef _CharT			char_type;
      typedef _OutIter			iter_type;

      explicit
      time_put_byname(const char*, size_t __refs = 0)
      : time_put<_CharT, _OutIter>(__refs)
      { };

    protected:
      virtual
      ~time_put_byname() { }
    };


  /**
   *  @brief  Money format ordering data.
   *
   *  This class contains an ordered array of 4 fields to represent the
   *  pattern for formatting a money amount.  Each field may contain one entry
   *  from the part enum.  symbol, sign, and value must be present and the
   *  remaining field must contain either none or space.  @see
   *  moneypunct::pos_format() and moneypunct::neg_format() for details of how
   *  these fields are interpreted.
  */
  class money_base
  {
  public:
    enum part { none, space, symbol, sign, value };
    struct pattern { char field[4]; };

    static const pattern _S_default_pattern;

    enum
    {
      _S_minus,
      _S_zero,
      _S_end = 11
    };

    // String literal of acceptable (narrow) input/output, for
    // money_get/money_put. "-0123456789"
    static const char* _S_atoms;

    // Construct and return valid pattern consisting of some combination of:
    // space none symbol sign value
    static pattern
    _S_construct_pattern(char __precedes, char __space, char __posn);
  };

  template<typename _CharT, bool _Intl>
    struct __moneypunct_cache : public locale::facet
    {
      const char*			_M_grouping;
      size_t                            _M_grouping_size;
      bool				_M_use_grouping;
      _CharT				_M_decimal_point;
      _CharT				_M_thousands_sep;
      const _CharT*			_M_curr_symbol;
      size_t                            _M_curr_symbol_size;
      const _CharT*			_M_positive_sign;
      size_t                            _M_positive_sign_size;
      const _CharT*			_M_negative_sign;
      size_t                            _M_negative_sign_size;
      int				_M_frac_digits;
      money_base::pattern		_M_pos_format;
      money_base::pattern	        _M_neg_format;

      // A list of valid numeric literals for input and output: in the standard
      // "C" locale, this is "-0123456789". This array contains the chars after
      // having been passed through the current locale's ctype<_CharT>.widen().
      _CharT				_M_atoms[money_base::_S_end];

      bool				_M_allocated;

      __moneypunct_cache(size_t __refs = 0) : facet(__refs),
      _M_grouping(NULL), _M_grouping_size(0), _M_use_grouping(false),
      _M_decimal_point(_CharT()), _M_thousands_sep(_CharT()),
      _M_curr_symbol(NULL), _M_curr_symbol_size(0),
      _M_positive_sign(NULL), _M_positive_sign_size(0),
      _M_negative_sign(NULL), _M_negative_sign_size(0),
      _M_frac_digits(0),
      _M_pos_format(money_base::pattern()),
      _M_neg_format(money_base::pattern()), _M_allocated(false)
      { }

      ~__moneypunct_cache();

      void
      _M_cache(const locale& __loc);

    private:
      __moneypunct_cache&
      operator=(const __moneypunct_cache&);
      
      explicit
      __moneypunct_cache(const __moneypunct_cache&);
    };

  template<typename _CharT, bool _Intl>
    __moneypunct_cache<_CharT, _Intl>::~__moneypunct_cache()
    {
      if (_M_allocated)
	{
	  delete [] _M_grouping;
	  delete [] _M_curr_symbol;
	  delete [] _M_positive_sign;
	  delete [] _M_negative_sign;
	}
    }

  /**
   *  @brief  Facet for formatting data for money amounts.
   *
   *  This facet encapsulates the punctuation, grouping and other formatting
   *  features of money amount string representations.
  */
  template<typename _CharT, bool _Intl>
    class moneypunct : public locale::facet, public money_base
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT			char_type;
      typedef basic_string<_CharT>	string_type;
      //@}
      typedef __moneypunct_cache<_CharT, _Intl>     __cache_type;

    private:
      __cache_type*			_M_data;

    public:
      /// This value is provided by the standard, but no reason for its
      /// existence.
      static const bool			intl = _Intl;
      /// Numpunct facet id.
      static locale::id			id;

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param refs  Passed to the base facet class.
      */
      explicit
      moneypunct(size_t __refs = 0) : facet(__refs), _M_data(NULL)
      { _M_initialize_moneypunct(); }

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is an internal constructor.
       *
       *  @param cache  Cache for optimization.
       *  @param refs  Passed to the base facet class.
      */
      explicit
      moneypunct(__cache_type* __cache, size_t __refs = 0)
      : facet(__refs), _M_data(__cache)
      { _M_initialize_moneypunct(); }

      /**
       *  @brief  Internal constructor. Not for general use.
       *
       *  This is a constructor for use by the library itself to set up new
       *  locales.
       *
       *  @param cloc  The "C" locale.
       *  @param s  The name of a locale.
       *  @param refs  Passed to the base facet class.
      */
      explicit
      moneypunct(__c_locale __cloc, const char* __s, size_t __refs = 0)
      : facet(__refs), _M_data(NULL)
      { _M_initialize_moneypunct(__cloc, __s); }

      /**
       *  @brief  Return decimal point character.
       *
       *  This function returns a char_type to use as a decimal point.  It
       *  does so by returning returning
       *  moneypunct<char_type>::do_decimal_point().
       *
       *  @return  @a char_type representing a decimal point.
      */
      char_type
      decimal_point() const
      { return this->do_decimal_point(); }

      /**
       *  @brief  Return thousands separator character.
       *
       *  This function returns a char_type to use as a thousands
       *  separator.  It does so by returning returning
       *  moneypunct<char_type>::do_thousands_sep().
       *
       *  @return  char_type representing a thousands separator.
      */
      char_type
      thousands_sep() const
      { return this->do_thousands_sep(); }

      /**
       *  @brief  Return grouping specification.
       *
       *  This function returns a string representing groupings for the
       *  integer part of an amount.  Groupings indicate where thousands
       *  separators should be inserted.
       *
       *  Each char in the return string is interpret as an integer rather
       *  than a character.  These numbers represent the number of digits in a
       *  group.  The first char in the string represents the number of digits
       *  in the least significant group.  If a char is negative, it indicates
       *  an unlimited number of digits for the group.  If more chars from the
       *  string are required to group a number, the last char is used
       *  repeatedly.
       *
       *  For example, if the grouping() returns "\003\002" and is applied to
       *  the number 123456789, this corresponds to 12,34,56,789.  Note that
       *  if the string was "32", this would put more than 50 digits into the
       *  least significant group if the character set is ASCII.
       *
       *  The string is returned by calling
       *  moneypunct<char_type>::do_grouping().
       *
       *  @return  string representing grouping specification.
      */
      string
      grouping() const
      { return this->do_grouping(); }

      /**
       *  @brief  Return currency symbol string.
       *
       *  This function returns a string_type to use as a currency symbol.  It
       *  does so by returning returning
       *  moneypunct<char_type>::do_curr_symbol().
       *
       *  @return  @a string_type representing a currency symbol.
      */
      string_type
      curr_symbol() const
      { return this->do_curr_symbol(); }

      /**
       *  @brief  Return positive sign string.
       *
       *  This function returns a string_type to use as a sign for positive
       *  amounts.  It does so by returning returning
       *  moneypunct<char_type>::do_positive_sign().
       *
       *  If the return value contains more than one character, the first
       *  character appears in the position indicated by pos_format() and the
       *  remainder appear at the end of the formatted string.
       *
       *  @return  @a string_type representing a positive sign.
      */
      string_type
      positive_sign() const
      { return this->do_positive_sign(); }

      /**
       *  @brief  Return negative sign string.
       *
       *  This function returns a string_type to use as a sign for negative
       *  amounts.  It does so by returning returning
       *  moneypunct<char_type>::do_negative_sign().
       *
       *  If the return value contains more than one character, the first
       *  character appears in the position indicated by neg_format() and the
       *  remainder appear at the end of the formatted string.
       *
       *  @return  @a string_type representing a negative sign.
      */
      string_type
      negative_sign() const
      { return this->do_negative_sign(); }

      /**
       *  @brief  Return number of digits in fraction.
       *
       *  This function returns the exact number of digits that make up the
       *  fractional part of a money amount.  It does so by returning
       *  returning moneypunct<char_type>::do_frac_digits().
       *
       *  The fractional part of a money amount is optional.  But if it is
       *  present, there must be frac_digits() digits.
       *
       *  @return  Number of digits in amount fraction.
      */
      int
      frac_digits() const
      { return this->do_frac_digits(); }

      //@{
      /**
       *  @brief  Return pattern for money values.
       *
       *  This function returns a pattern describing the formatting of a
       *  positive or negative valued money amount.  It does so by returning
       *  returning moneypunct<char_type>::do_pos_format() or
       *  moneypunct<char_type>::do_neg_format().
       *
       *  The pattern has 4 fields describing the ordering of symbol, sign,
       *  value, and none or space.  There must be one of each in the pattern.
       *  The none and space enums may not appear in the first field and space
       *  may not appear in the final field.
       *
       *  The parts of a money string must appear in the order indicated by
       *  the fields of the pattern.  The symbol field indicates that the
       *  value of curr_symbol() may be present.  The sign field indicates
       *  that the value of positive_sign() or negative_sign() must be
       *  present.  The value field indicates that the absolute value of the
       *  money amount is present.  none indicates 0 or more whitespace
       *  characters, except at the end, where it permits no whitespace.
       *  space indicates that 1 or more whitespace characters must be
       *  present.
       *
       *  For example, for the US locale and pos_format() pattern
       *  {symbol,sign,value,none}, curr_symbol() == '$' positive_sign() ==
       *  '+', and value 10.01, and options set to force the symbol, the
       *  corresponding string is "$+10.01".
       *
       *  @return  Pattern for money values.
      */
      pattern
      pos_format() const
      { return this->do_pos_format(); }

      pattern
      neg_format() const
      { return this->do_neg_format(); }
      //@}

    protected:
      /// Destructor.
      virtual
      ~moneypunct();

      /**
       *  @brief  Return decimal point character.
       *
       *  Returns a char_type to use as a decimal point.  This function is a
       *  hook for derived classes to change the value returned.
       *
       *  @return  @a char_type representing a decimal point.
      */
      virtual char_type
      do_decimal_point() const
      { return _M_data->_M_decimal_point; }

      /**
       *  @brief  Return thousands separator character.
       *
       *  Returns a char_type to use as a thousands separator.  This function
       *  is a hook for derived classes to change the value returned.
       *
       *  @return  @a char_type representing a thousands separator.
      */
      virtual char_type
      do_thousands_sep() const
      { return _M_data->_M_thousands_sep; }

      /**
       *  @brief  Return grouping specification.
       *
       *  Returns a string representing groupings for the integer part of a
       *  number.  This function is a hook for derived classes to change the
       *  value returned.  @see grouping() for details.
       *
       *  @return  String representing grouping specification.
      */
      virtual string
      do_grouping() const
      { return _M_data->_M_grouping; }

      /**
       *  @brief  Return currency symbol string.
       *
       *  This function returns a string_type to use as a currency symbol.
       *  This function is a hook for derived classes to change the value
       *  returned.  @see curr_symbol() for details.
       *
       *  @return  @a string_type representing a currency symbol.
      */
      virtual string_type
      do_curr_symbol()   const
      { return _M_data->_M_curr_symbol; }

      /**
       *  @brief  Return positive sign string.
       *
       *  This function returns a string_type to use as a sign for positive
       *  amounts.  This function is a hook for derived classes to change the
       *  value returned.  @see positive_sign() for details.
       *
       *  @return  @a string_type representing a positive sign.
      */
      virtual string_type
      do_positive_sign() const
      { return _M_data->_M_positive_sign; }

      /**
       *  @brief  Return negative sign string.
       *
       *  This function returns a string_type to use as a sign for negative
       *  amounts.  This function is a hook for derived classes to change the
       *  value returned.  @see negative_sign() for details.
       *
       *  @return  @a string_type representing a negative sign.
      */
      virtual string_type
      do_negative_sign() const
      { return _M_data->_M_negative_sign; }

      /**
       *  @brief  Return number of digits in fraction.
       *
       *  This function returns the exact number of digits that make up the
       *  fractional part of a money amount.  This function is a hook for
       *  derived classes to change the value returned.  @see frac_digits()
       *  for details.
       *
       *  @return  Number of digits in amount fraction.
      */
      virtual int
      do_frac_digits() const
      { return _M_data->_M_frac_digits; }

      /**
       *  @brief  Return pattern for money values.
       *
       *  This function returns a pattern describing the formatting of a
       *  positive valued money amount.  This function is a hook for derived
       *  classes to change the value returned.  @see pos_format() for
       *  details.
       *
       *  @return  Pattern for money values.
      */
      virtual pattern
      do_pos_format() const
      { return _M_data->_M_pos_format; }

      /**
       *  @brief  Return pattern for money values.
       *
       *  This function returns a pattern describing the formatting of a
       *  negative valued money amount.  This function is a hook for derived
       *  classes to change the value returned.  @see neg_format() for
       *  details.
       *
       *  @return  Pattern for money values.
      */
      virtual pattern
      do_neg_format() const
      { return _M_data->_M_neg_format; }

      // For use at construction time only.
       void
       _M_initialize_moneypunct(__c_locale __cloc = NULL,
				const char* __name = NULL);
    };

  template<typename _CharT, bool _Intl>
    locale::id moneypunct<_CharT, _Intl>::id;

  template<typename _CharT, bool _Intl>
    const bool moneypunct<_CharT, _Intl>::intl;

  template<>
    moneypunct<char, true>::~moneypunct();

  template<>
    moneypunct<char, false>::~moneypunct();

  template<>
    void
    moneypunct<char, true>::_M_initialize_moneypunct(__c_locale, const char*);

  template<>
    void
    moneypunct<char, false>::_M_initialize_moneypunct(__c_locale, const char*);

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    moneypunct<wchar_t, true>::~moneypunct();

  template<>
    moneypunct<wchar_t, false>::~moneypunct();

  template<>
    void
    moneypunct<wchar_t, true>::_M_initialize_moneypunct(__c_locale,
							const char*);

  template<>
    void
    moneypunct<wchar_t, false>::_M_initialize_moneypunct(__c_locale,
							 const char*);
#endif

  /// @brief  class moneypunct_byname [22.2.6.4].
  template<typename _CharT, bool _Intl>
    class moneypunct_byname : public moneypunct<_CharT, _Intl>
    {
    public:
      typedef _CharT			char_type;
      typedef basic_string<_CharT>	string_type;

      static const bool intl = _Intl;

      explicit
      moneypunct_byname(const char* __s, size_t __refs = 0)
      : moneypunct<_CharT, _Intl>(__refs)
      {
	if (std::strcmp(__s, "C") != 0 && std::strcmp(__s, "POSIX") != 0)
	  {
	    __c_locale __tmp;
	    this->_S_create_c_locale(__tmp, __s);
	    this->_M_initialize_moneypunct(__tmp);
	    this->_S_destroy_c_locale(__tmp);
	  }
      }

    protected:
      virtual
      ~moneypunct_byname() { }
    };

  template<typename _CharT, bool _Intl>
    const bool moneypunct_byname<_CharT, _Intl>::intl;

_GLIBCXX_BEGIN_LDBL_NAMESPACE
  /**
   *  @brief  Facet for parsing monetary amounts.
   *
   *  This facet encapsulates the code to parse and return a monetary
   *  amount from a string.
   *
   *  The money_get template uses protected virtual functions to
   *  provide the actual results.  The public accessors forward the
   *  call to the virtual functions.  These virtual functions are
   *  hooks for developers to implement the behavior they require from
   *  the money_get facet.
  */
  template<typename _CharT, typename _InIter>
    class money_get : public locale::facet
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT			char_type;
      typedef _InIter			iter_type;
      typedef basic_string<_CharT>	string_type;
      //@}

      /// Numpunct facet id.
      static locale::id			id;

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param refs  Passed to the base facet class.
      */
      explicit
      money_get(size_t __refs = 0) : facet(__refs) { }

      /**
       *  @brief  Read and parse a monetary value.
       *
       *  This function reads characters from @a s, interprets them as a
       *  monetary value according to moneypunct and ctype facets retrieved
       *  from io.getloc(), and returns the result in @a units as an integral
       *  value moneypunct::frac_digits() * the actual amount.  For example,
       *  the string $10.01 in a US locale would store 1001 in @a units.
       *
       *  Any characters not part of a valid money amount are not consumed.
       *
       *  If a money value cannot be parsed from the input stream, sets
       *  err=(err|io.failbit).  If the stream is consumed before finishing
       *  parsing,  sets err=(err|io.failbit|io.eofbit).  @a units is
       *  unchanged if parsing fails.
       *
       *  This function works by returning the result of do_get().
       *
       *  @param  s  Start of characters to parse.
       *  @param  end  End of characters to parse.
       *  @param  intl  Parameter to use_facet<moneypunct<CharT,intl> >.
       *  @param  io  Source of facets and io state.
       *  @param  err  Error field to set if parsing fails.
       *  @param  units  Place to store result of parsing.
       *  @return  Iterator referencing first character beyond valid money
       *	   amount.
       */
      iter_type
      get(iter_type __s, iter_type __end, bool __intl, ios_base& __io,
	  ios_base::iostate& __err, long double& __units) const
      { return this->do_get(__s, __end, __intl, __io, __err, __units); }

      /**
       *  @brief  Read and parse a monetary value.
       *
       *  This function reads characters from @a s, interprets them as a
       *  monetary value according to moneypunct and ctype facets retrieved
       *  from io.getloc(), and returns the result in @a digits.  For example,
       *  the string $10.01 in a US locale would store "1001" in @a digits.
       *
       *  Any characters not part of a valid money amount are not consumed.
       *
       *  If a money value cannot be parsed from the input stream, sets
       *  err=(err|io.failbit).  If the stream is consumed before finishing
       *  parsing,  sets err=(err|io.failbit|io.eofbit).
       *
       *  This function works by returning the result of do_get().
       *
       *  @param  s  Start of characters to parse.
       *  @param  end  End of characters to parse.
       *  @param  intl  Parameter to use_facet<moneypunct<CharT,intl> >.
       *  @param  io  Source of facets and io state.
       *  @param  err  Error field to set if parsing fails.
       *  @param  digits  Place to store result of parsing.
       *  @return  Iterator referencing first character beyond valid money
       *	   amount.
       */
      iter_type
      get(iter_type __s, iter_type __end, bool __intl, ios_base& __io,
	  ios_base::iostate& __err, string_type& __digits) const
      { return this->do_get(__s, __end, __intl, __io, __err, __digits); }

    protected:
      /// Destructor.
      virtual
      ~money_get() { }

      /**
       *  @brief  Read and parse a monetary value.
       *
       *  This function reads and parses characters representing a monetary
       *  value.  This function is a hook for derived classes to change the
       *  value returned.  @see get() for details.
       */
      // XXX GLIBCXX_ABI Deprecated
#if defined _GLIBCXX_LONG_DOUBLE_COMPAT && defined __LONG_DOUBLE_128__
      virtual iter_type
      __do_get(iter_type __s, iter_type __end, bool __intl, ios_base& __io,
	       ios_base::iostate& __err, double& __units) const;
#else
      virtual iter_type
      do_get(iter_type __s, iter_type __end, bool __intl, ios_base& __io,
	     ios_base::iostate& __err, long double& __units) const;
#endif

      /**
       *  @brief  Read and parse a monetary value.
       *
       *  This function reads and parses characters representing a monetary
       *  value.  This function is a hook for derived classes to change the
       *  value returned.  @see get() for details.
       */
      virtual iter_type
      do_get(iter_type __s, iter_type __end, bool __intl, ios_base& __io,
	     ios_base::iostate& __err, string_type& __digits) const;

      // XXX GLIBCXX_ABI Deprecated
#if defined _GLIBCXX_LONG_DOUBLE_COMPAT && defined __LONG_DOUBLE_128__
      virtual iter_type
      do_get(iter_type __s, iter_type __end, bool __intl, ios_base& __io,
	     ios_base::iostate& __err, long double& __units) const;
#endif

      template<bool _Intl>
        iter_type
        _M_extract(iter_type __s, iter_type __end, ios_base& __io,
		   ios_base::iostate& __err, string& __digits) const;     
    };

  template<typename _CharT, typename _InIter>
    locale::id money_get<_CharT, _InIter>::id;

  /**
   *  @brief  Facet for outputting monetary amounts.
   *
   *  This facet encapsulates the code to format and output a monetary
   *  amount.
   *
   *  The money_put template uses protected virtual functions to
   *  provide the actual results.  The public accessors forward the
   *  call to the virtual functions.  These virtual functions are
   *  hooks for developers to implement the behavior they require from
   *  the money_put facet.
  */
  template<typename _CharT, typename _OutIter>
    class money_put : public locale::facet
    {
    public:
      //@{
      /// Public typedefs
      typedef _CharT			char_type;
      typedef _OutIter			iter_type;
      typedef basic_string<_CharT>	string_type;
      //@}

      /// Numpunct facet id.
      static locale::id			id;

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param refs  Passed to the base facet class.
      */
      explicit
      money_put(size_t __refs = 0) : facet(__refs) { }

      /**
       *  @brief  Format and output a monetary value.
       *
       *  This function formats @a units as a monetary value according to
       *  moneypunct and ctype facets retrieved from io.getloc(), and writes
       *  the resulting characters to @a s.  For example, the value 1001 in a
       *  US locale would write "$10.01" to @a s.
       *
       *  This function works by returning the result of do_put().
       *
       *  @param  s  The stream to write to.
       *  @param  intl  Parameter to use_facet<moneypunct<CharT,intl> >.
       *  @param  io  Source of facets and io state.
       *  @param  fill  char_type to use for padding.
       *  @param  units  Place to store result of parsing.
       *  @return  Iterator after writing.
       */
      iter_type
      put(iter_type __s, bool __intl, ios_base& __io,
	  char_type __fill, long double __units) const
      { return this->do_put(__s, __intl, __io, __fill, __units); }

      /**
       *  @brief  Format and output a monetary value.
       *
       *  This function formats @a digits as a monetary value according to
       *  moneypunct and ctype facets retrieved from io.getloc(), and writes
       *  the resulting characters to @a s.  For example, the string "1001" in
       *  a US locale would write "$10.01" to @a s.
       *
       *  This function works by returning the result of do_put().
       *
       *  @param  s  The stream to write to.
       *  @param  intl  Parameter to use_facet<moneypunct<CharT,intl> >.
       *  @param  io  Source of facets and io state.
       *  @param  fill  char_type to use for padding.
       *  @param  units  Place to store result of parsing.
       *  @return  Iterator after writing.
       */
      iter_type
      put(iter_type __s, bool __intl, ios_base& __io,
	  char_type __fill, const string_type& __digits) const
      { return this->do_put(__s, __intl, __io, __fill, __digits); }

    protected:
      /// Destructor.
      virtual
      ~money_put() { }

      /**
       *  @brief  Format and output a monetary value.
       *
       *  This function formats @a units as a monetary value according to
       *  moneypunct and ctype facets retrieved from io.getloc(), and writes
       *  the resulting characters to @a s.  For example, the value 1001 in a
       *  US locale would write "$10.01" to @a s.
       *
       *  This function is a hook for derived classes to change the value
       *  returned.  @see put().
       *
       *  @param  s  The stream to write to.
       *  @param  intl  Parameter to use_facet<moneypunct<CharT,intl> >.
       *  @param  io  Source of facets and io state.
       *  @param  fill  char_type to use for padding.
       *  @param  units  Place to store result of parsing.
       *  @return  Iterator after writing.
       */
      // XXX GLIBCXX_ABI Deprecated
#if defined _GLIBCXX_LONG_DOUBLE_COMPAT && defined __LONG_DOUBLE_128__
      virtual iter_type
      __do_put(iter_type __s, bool __intl, ios_base& __io, char_type __fill,
	       double __units) const;
#else
      virtual iter_type
      do_put(iter_type __s, bool __intl, ios_base& __io, char_type __fill,
	     long double __units) const;
#endif

      /**
       *  @brief  Format and output a monetary value.
       *
       *  This function formats @a digits as a monetary value according to
       *  moneypunct and ctype facets retrieved from io.getloc(), and writes
       *  the resulting characters to @a s.  For example, the string "1001" in
       *  a US locale would write "$10.01" to @a s.
       *
       *  This function is a hook for derived classes to change the value
       *  returned.  @see put().
       *
       *  @param  s  The stream to write to.
       *  @param  intl  Parameter to use_facet<moneypunct<CharT,intl> >.
       *  @param  io  Source of facets and io state.
       *  @param  fill  char_type to use for padding.
       *  @param  units  Place to store result of parsing.
       *  @return  Iterator after writing.
       */
      virtual iter_type
      do_put(iter_type __s, bool __intl, ios_base& __io, char_type __fill,
	     const string_type& __digits) const;

      // XXX GLIBCXX_ABI Deprecated
#if defined _GLIBCXX_LONG_DOUBLE_COMPAT && defined __LONG_DOUBLE_128__
      virtual iter_type
      do_put(iter_type __s, bool __intl, ios_base& __io, char_type __fill,
	     long double __units) const;
#endif

      template<bool _Intl>
        iter_type
        _M_insert(iter_type __s, ios_base& __io, char_type __fill,
		  const string_type& __digits) const;
    };

  template<typename _CharT, typename _OutIter>
    locale::id money_put<_CharT, _OutIter>::id;

_GLIBCXX_END_LDBL_NAMESPACE

  /**
   *  @brief  Messages facet base class providing catalog typedef.
   */
  struct messages_base
  {
    typedef int catalog;
  };

  /**
   *  @brief  Facet for handling message catalogs
   *
   *  This facet encapsulates the code to retrieve messages from
   *  message catalogs.  The only thing defined by the standard for this facet
   *  is the interface.  All underlying functionality is
   *  implementation-defined.
   *
   *  This library currently implements 3 versions of the message facet.  The
   *  first version (gnu) is a wrapper around gettext, provided by libintl.
   *  The second version (ieee) is a wrapper around catgets.  The final
   *  version (default) does no actual translation.  These implementations are
   *  only provided for char and wchar_t instantiations.
   *
   *  The messages template uses protected virtual functions to
   *  provide the actual results.  The public accessors forward the
   *  call to the virtual functions.  These virtual functions are
   *  hooks for developers to implement the behavior they require from
   *  the messages facet.
  */
  template<typename _CharT>
    class messages : public locale::facet, public messages_base
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT			char_type;
      typedef basic_string<_CharT>	string_type;
      //@}

    protected:
      // Underlying "C" library locale information saved from
      // initialization, needed by messages_byname as well.
      __c_locale			_M_c_locale_messages;
      const char*			_M_name_messages;

    public:
      /// Numpunct facet id.
      static locale::id			id;

      /**
       *  @brief  Constructor performs initialization.
       *
       *  This is the constructor provided by the standard.
       *
       *  @param refs  Passed to the base facet class.
      */
      explicit
      messages(size_t __refs = 0);

      // Non-standard.
      /**
       *  @brief  Internal constructor.  Not for general use.
       *
       *  This is a constructor for use by the library itself to set up new
       *  locales.
       *
       *  @param  cloc  The "C" locale.
       *  @param  s  The name of a locale.
       *  @param  refs  Refcount to pass to the base class.
       */
      explicit
      messages(__c_locale __cloc, const char* __s, size_t __refs = 0);

      /*
       *  @brief  Open a message catalog.
       *
       *  This function opens and returns a handle to a message catalog by
       *  returning do_open(s, loc).
       *
       *  @param  s  The catalog to open.
       *  @param  loc  Locale to use for character set conversions.
       *  @return  Handle to the catalog or value < 0 if open fails.
      */
      catalog
      open(const basic_string<char>& __s, const locale& __loc) const
      { return this->do_open(__s, __loc); }

      // Non-standard and unorthodox, yet effective.
      /*
       *  @brief  Open a message catalog.
       *
       *  This non-standard function opens and returns a handle to a message
       *  catalog by returning do_open(s, loc).  The third argument provides a
       *  message catalog root directory for gnu gettext and is ignored
       *  otherwise.
       *
       *  @param  s  The catalog to open.
       *  @param  loc  Locale to use for character set conversions.
       *  @param  dir  Message catalog root directory.
       *  @return  Handle to the catalog or value < 0 if open fails.
      */
      catalog
      open(const basic_string<char>&, const locale&, const char*) const;

      /*
       *  @brief  Look up a string in a message catalog.
       *
       *  This function retrieves and returns a message from a catalog by
       *  returning do_get(c, set, msgid, s).
       *
       *  For gnu, @a set and @a msgid are ignored.  Returns gettext(s).
       *  For default, returns s. For ieee, returns catgets(c,set,msgid,s).
       *
       *  @param  c  The catalog to access.
       *  @param  set  Implementation-defined.
       *  @param  msgid  Implementation-defined.
       *  @param  s  Default return value if retrieval fails.
       *  @return  Retrieved message or @a s if get fails.
      */
      string_type
      get(catalog __c, int __set, int __msgid, const string_type& __s) const
      { return this->do_get(__c, __set, __msgid, __s); }

      /*
       *  @brief  Close a message catalog.
       *
       *  Closes catalog @a c by calling do_close(c).
       *
       *  @param  c  The catalog to close.
      */
      void
      close(catalog __c) const
      { return this->do_close(__c); }

    protected:
      /// Destructor.
      virtual
      ~messages();

      /*
       *  @brief  Open a message catalog.
       *
       *  This function opens and returns a handle to a message catalog in an
       *  implementation-defined manner.  This function is a hook for derived
       *  classes to change the value returned.
       *
       *  @param  s  The catalog to open.
       *  @param  loc  Locale to use for character set conversions.
       *  @return  Handle to the opened catalog, value < 0 if open failed.
      */
      virtual catalog
      do_open(const basic_string<char>&, const locale&) const;

      /*
       *  @brief  Look up a string in a message catalog.
       *
       *  This function retrieves and returns a message from a catalog in an
       *  implementation-defined manner.  This function is a hook for derived
       *  classes to change the value returned.
       *
       *  For gnu, @a set and @a msgid are ignored.  Returns gettext(s).
       *  For default, returns s. For ieee, returns catgets(c,set,msgid,s).
       *
       *  @param  c  The catalog to access.
       *  @param  set  Implementation-defined.
       *  @param  msgid  Implementation-defined.
       *  @param  s  Default return value if retrieval fails.
       *  @return  Retrieved message or @a s if get fails.
      */
      virtual string_type
      do_get(catalog, int, int, const string_type& __dfault) const;

      /*
       *  @brief  Close a message catalog.
       *
       *  @param  c  The catalog to close.
      */
      virtual void
      do_close(catalog) const;

      // Returns a locale and codeset-converted string, given a char* message.
      char*
      _M_convert_to_char(const string_type& __msg) const
      {
	// XXX
	return reinterpret_cast<char*>(const_cast<_CharT*>(__msg.c_str()));
      }

      // Returns a locale and codeset-converted string, given a char* message.
      string_type
      _M_convert_from_char(char*) const
      {
#if 0
	// Length of message string without terminating null.
	size_t __len = char_traits<char>::length(__msg) - 1;

	// "everybody can easily convert the string using
	// mbsrtowcs/wcsrtombs or with iconv()"

	// Convert char* to _CharT in locale used to open catalog.
	// XXX need additional template parameter on messages class for this..
	// typedef typename codecvt<char, _CharT, _StateT> __codecvt_type;
	typedef typename codecvt<char, _CharT, mbstate_t> __codecvt_type;

	__codecvt_type::state_type __state;
	// XXX may need to initialize state.
	//initialize_state(__state._M_init());

	char* __from_next;
	// XXX what size for this string?
	_CharT* __to = static_cast<_CharT*>(__builtin_alloca(__len + 1));
	const __codecvt_type& __cvt = use_facet<__codecvt_type>(_M_locale_conv);
	__cvt.out(__state, __msg, __msg + __len, __from_next,
		  __to, __to + __len + 1, __to_next);
	return string_type(__to);
#endif
#if 0
	typedef ctype<_CharT> __ctype_type;
	// const __ctype_type& __cvt = use_facet<__ctype_type>(_M_locale_msg);
	const __ctype_type& __cvt = use_facet<__ctype_type>(locale());
	// XXX Again, proper length of converted string an issue here.
	// For now, assume the converted length is not larger.
	_CharT* __dest = static_cast<_CharT*>(__builtin_alloca(__len + 1));
	__cvt.widen(__msg, __msg + __len, __dest);
	return basic_string<_CharT>(__dest);
#endif
	return string_type();
      }
     };

  template<typename _CharT>
    locale::id messages<_CharT>::id;

  // Specializations for required instantiations.
  template<>
    string
    messages<char>::do_get(catalog, int, int, const string&) const;

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    wstring
    messages<wchar_t>::do_get(catalog, int, int, const wstring&) const;
#endif

   /// @brief class messages_byname [22.2.7.2].
   template<typename _CharT>
    class messages_byname : public messages<_CharT>
    {
    public:
      typedef _CharT			char_type;
      typedef basic_string<_CharT>	string_type;

      explicit
      messages_byname(const char* __s, size_t __refs = 0);

    protected:
      virtual
      ~messages_byname()
      { }
    };

_GLIBCXX_END_NAMESPACE

  // Include host and configuration specific messages functions.
  #include <bits/messages_members.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // Subclause convenience interfaces, inlines.
  // NB: These are inline because, when used in a loop, some compilers
  // can hoist the body out of the loop; then it's just as fast as the
  // C is*() function.

  /// Convenience interface to ctype.is(ctype_base::space, __c).
  template<typename _CharT>
    inline bool
    isspace(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::space, __c); }

  /// Convenience interface to ctype.is(ctype_base::print, __c).
  template<typename _CharT>
    inline bool
    isprint(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::print, __c); }

  /// Convenience interface to ctype.is(ctype_base::cntrl, __c).
  template<typename _CharT>
    inline bool
    iscntrl(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::cntrl, __c); }

  /// Convenience interface to ctype.is(ctype_base::upper, __c).
  template<typename _CharT>
    inline bool
    isupper(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::upper, __c); }

  /// Convenience interface to ctype.is(ctype_base::lower, __c).
  template<typename _CharT>
    inline bool 
    islower(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::lower, __c); }

  /// Convenience interface to ctype.is(ctype_base::alpha, __c).
  template<typename _CharT>
    inline bool
    isalpha(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::alpha, __c); }

  /// Convenience interface to ctype.is(ctype_base::digit, __c).
  template<typename _CharT>
    inline bool
    isdigit(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::digit, __c); }

  /// Convenience interface to ctype.is(ctype_base::punct, __c).
  template<typename _CharT>
    inline bool
    ispunct(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::punct, __c); }

  /// Convenience interface to ctype.is(ctype_base::xdigit, __c).
  template<typename _CharT>
    inline bool
    isxdigit(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::xdigit, __c); }

  /// Convenience interface to ctype.is(ctype_base::alnum, __c).
  template<typename _CharT>
    inline bool
    isalnum(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::alnum, __c); }

  /// Convenience interface to ctype.is(ctype_base::graph, __c).
  template<typename _CharT>
    inline bool
    isgraph(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).is(ctype_base::graph, __c); }

  /// Convenience interface to ctype.toupper(__c).
  template<typename _CharT>
    inline _CharT
    toupper(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).toupper(__c); }

  /// Convenience interface to ctype.tolower(__c).
  template<typename _CharT>
    inline _CharT
    tolower(_CharT __c, const locale& __loc)
    { return use_facet<ctype<_CharT> >(__loc).tolower(__c); }

_GLIBCXX_END_NAMESPACE

#endif
