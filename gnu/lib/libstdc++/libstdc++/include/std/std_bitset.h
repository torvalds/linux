// <bitset> -*- C++ -*-

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

/*
 * Copyright (c) 1998
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

/** @file bitset
 *  This is a Standard C++ Library header.  You should @c #include this header
 *  in your programs, rather than any of the "st[dl]_*.h" implementation files.
 */

#ifndef _GLIBCPP_BITSET_H
#define _GLIBCPP_BITSET_H

#pragma GCC system_header

#include <cstddef>     // for size_t
#include <cstring>     // for memset
#include <string>
#include <bits/functexcept.h>   // for invalid_argument, out_of_range,
                                // overflow_error
#include <ostream>     // for ostream (operator<<)
#include <istream>     // for istream (operator>>)


#define _GLIBCPP_BITSET_BITS_PER_WORD (CHAR_BIT*sizeof(unsigned long))
#define _GLIBCPP_BITSET_WORDS(__n) \
 ((__n) < 1 ? 0 : ((__n) + _GLIBCPP_BITSET_BITS_PER_WORD - 1)/_GLIBCPP_BITSET_BITS_PER_WORD)

namespace std
{
  extern unsigned char 	_S_bit_count[256];
  extern unsigned char 	_S_first_one[256];

  /**
   *  @if maint
   *  Base class, general case.  It is a class inveriant that _Nw will be
   *  nonnegative.
   *
   *  See documentation for bitset.
   *  @endif
  */
  template<size_t _Nw>
    struct _Base_bitset
    {
      typedef unsigned long _WordT;

      /// 0 is the least significant word.
      _WordT 		_M_w[_Nw];

      _Base_bitset() { _M_do_reset(); }
      _Base_bitset(unsigned long __val)
      {
	_M_do_reset();
	_M_w[0] = __val;
      }

      static size_t
      _S_whichword(size_t __pos )
      { return __pos / _GLIBCPP_BITSET_BITS_PER_WORD; }

      static size_t
      _S_whichbyte(size_t __pos )
      { return (__pos % _GLIBCPP_BITSET_BITS_PER_WORD) / CHAR_BIT; }

      static size_t
      _S_whichbit(size_t __pos )
      { return __pos % _GLIBCPP_BITSET_BITS_PER_WORD; }

      static _WordT
      _S_maskbit(size_t __pos )
      { return (static_cast<_WordT>(1)) << _S_whichbit(__pos); }

      _WordT&
      _M_getword(size_t __pos)
      { return _M_w[_S_whichword(__pos)]; }

      _WordT
      _M_getword(size_t __pos) const
      { return _M_w[_S_whichword(__pos)]; }

      _WordT&
      _M_hiword() { return _M_w[_Nw - 1]; }

      _WordT
      _M_hiword() const { return _M_w[_Nw - 1]; }

      void
      _M_do_and(const _Base_bitset<_Nw>& __x)
      {
	for (size_t __i = 0; __i < _Nw; __i++)
	  _M_w[__i] &= __x._M_w[__i];
      }

      void
      _M_do_or(const _Base_bitset<_Nw>& __x)
      {
	for (size_t __i = 0; __i < _Nw; __i++)
	  _M_w[__i] |= __x._M_w[__i];
      }

      void
      _M_do_xor(const _Base_bitset<_Nw>& __x)
      {
	for (size_t __i = 0; __i < _Nw; __i++)
	  _M_w[__i] ^= __x._M_w[__i];
      }

      void
      _M_do_left_shift(size_t __shift);

      void
      _M_do_right_shift(size_t __shift);

      void
      _M_do_flip()
      {
	for (size_t __i = 0; __i < _Nw; __i++)
	  _M_w[__i] = ~_M_w[__i];
      }

      void
      _M_do_set()
      {
	for (size_t __i = 0; __i < _Nw; __i++)
	  _M_w[__i] = ~static_cast<_WordT>(0);
      }

      void
      _M_do_reset() { memset(_M_w, 0, _Nw * sizeof(_WordT)); }

      bool
      _M_is_equal(const _Base_bitset<_Nw>& __x) const
      {
	for (size_t __i = 0; __i < _Nw; ++__i)
	  {
	    if (_M_w[__i] != __x._M_w[__i])
	      return false;
	  }
	return true;
      }

      bool
      _M_is_any() const
      {
	for (size_t __i = 0; __i < _Nw; __i++)
	  {
	    if (_M_w[__i] != static_cast<_WordT>(0))
	      return true;
	  }
	return false;
      }

      size_t
      _M_do_count() const
      {
	size_t __result = 0;
	const unsigned char* __byte_ptr = (const unsigned char*)_M_w;
	const unsigned char* __end_ptr = (const unsigned char*)(_M_w + _Nw);

	while ( __byte_ptr < __end_ptr )
	  {
	    __result += _S_bit_count[*__byte_ptr];
	    __byte_ptr++;
	  }
	return __result;
      }

      unsigned long
      _M_do_to_ulong() const;

      // find first "on" bit
      size_t
      _M_do_find_first(size_t __not_found) const;

      // find the next "on" bit that follows "prev"
      size_t
      _M_do_find_next(size_t __prev, size_t __not_found) const;
    };

  // Definitions of non-inline functions from _Base_bitset.
  template<size_t _Nw>
    void
    _Base_bitset<_Nw>::_M_do_left_shift(size_t __shift)
    {
      if (__builtin_expect(__shift != 0, 1))
	{
	  const size_t __wshift = __shift / _GLIBCPP_BITSET_BITS_PER_WORD;
	  const size_t __offset = __shift % _GLIBCPP_BITSET_BITS_PER_WORD;

	  if (__offset == 0)
	    for (size_t __n = _Nw - 1; __n >= __wshift; --__n)
	      _M_w[__n] = _M_w[__n - __wshift];
	  else
	    {
	      const size_t __sub_offset = _GLIBCPP_BITSET_BITS_PER_WORD - __offset;
	      for (size_t __n = _Nw - 1; __n > __wshift; --__n)
		_M_w[__n] = (_M_w[__n - __wshift] << __offset) |
		  (_M_w[__n - __wshift - 1] >> __sub_offset);
	      _M_w[__wshift] = _M_w[0] << __offset;
	    }

	  fill(_M_w + 0, _M_w + __wshift, static_cast<_WordT>(0));
	}
    }

  template<size_t _Nw>
    void
    _Base_bitset<_Nw>::_M_do_right_shift(size_t __shift)
    {
      if (__builtin_expect(__shift != 0, 1))
	{
	  const size_t __wshift = __shift / _GLIBCPP_BITSET_BITS_PER_WORD;
	  const size_t __offset = __shift % _GLIBCPP_BITSET_BITS_PER_WORD;
	  const size_t __limit = _Nw - __wshift - 1;

	  if (__offset == 0)
	    for (size_t __n = 0; __n <= __limit; ++__n)
	      _M_w[__n] = _M_w[__n + __wshift];
	  else
	    {
	      const size_t __sub_offset = _GLIBCPP_BITSET_BITS_PER_WORD - __offset;
	      for (size_t __n = 0; __n < __limit; ++__n)
		_M_w[__n] = (_M_w[__n + __wshift] >> __offset) |
		  (_M_w[__n + __wshift + 1] << __sub_offset);
	      _M_w[__limit] = _M_w[_Nw-1] >> __offset;
	    }

	  fill(_M_w + __limit + 1, _M_w + _Nw, static_cast<_WordT>(0));
	}
    }

  template<size_t _Nw>
    unsigned long
    _Base_bitset<_Nw>::_M_do_to_ulong() const
    {
      for (size_t __i = 1; __i < _Nw; ++__i)
	if (_M_w[__i])
	  __throw_overflow_error("bitset -- too large to fit in unsigned long");
      return _M_w[0];
    }

  template<size_t _Nw>
    size_t
    _Base_bitset<_Nw>::_M_do_find_first(size_t __not_found) const
    {
      for (size_t __i = 0; __i < _Nw; __i++ )
	{
	  _WordT __thisword = _M_w[__i];
	  if ( __thisword != static_cast<_WordT>(0) )
	    {
	      // find byte within word
	      for (size_t __j = 0; __j < sizeof(_WordT); __j++ )
		{
		  unsigned char __this_byte
		    = static_cast<unsigned char>(__thisword & (~(unsigned char)0));
		  if (__this_byte)
		    return __i*_GLIBCPP_BITSET_BITS_PER_WORD + __j*CHAR_BIT +
		      _S_first_one[__this_byte];

		  __thisword >>= CHAR_BIT;
		}
	    }
	}
      // not found, so return an indication of failure.
      return __not_found;
    }

  template<size_t _Nw>
    size_t
    _Base_bitset<_Nw>::_M_do_find_next(size_t __prev, size_t __not_found) const
    {
      // make bound inclusive
      ++__prev;

      // check out of bounds
      if ( __prev >= _Nw * _GLIBCPP_BITSET_BITS_PER_WORD )
	return __not_found;

      // search first word
      size_t __i = _S_whichword(__prev);
      _WordT __thisword = _M_w[__i];

      // mask off bits below bound
      __thisword &= (~static_cast<_WordT>(0)) << _S_whichbit(__prev);

      if ( __thisword != static_cast<_WordT>(0) )
	{
	  // find byte within word
	  // get first byte into place
	  __thisword >>= _S_whichbyte(__prev) * CHAR_BIT;
	  for (size_t __j = _S_whichbyte(__prev); __j < sizeof(_WordT); __j++)
	    {
	      unsigned char __this_byte
		= static_cast<unsigned char>(__thisword & (~(unsigned char)0));
	      if ( __this_byte )
		return __i*_GLIBCPP_BITSET_BITS_PER_WORD + __j*CHAR_BIT +
		  _S_first_one[__this_byte];

	      __thisword >>= CHAR_BIT;
	    }
	}

      // check subsequent words
      __i++;
      for ( ; __i < _Nw; __i++ )
	{
	  __thisword = _M_w[__i];
	  if ( __thisword != static_cast<_WordT>(0) )
	    {
	      // find byte within word
	      for (size_t __j = 0; __j < sizeof(_WordT); __j++ )
		{
		  unsigned char __this_byte
		    = static_cast<unsigned char>(__thisword & (~(unsigned char)0));
		  if ( __this_byte )
		    return __i*_GLIBCPP_BITSET_BITS_PER_WORD + __j*CHAR_BIT +
		      _S_first_one[__this_byte];

		  __thisword >>= CHAR_BIT;
		}
	    }
	}
      // not found, so return an indication of failure.
      return __not_found;
    } // end _M_do_find_next


  /**
   *  @if maint
   *  Base class, specialization for a single word.
   *
   *  See documentation for bitset.
   *  @endif
  */
  template<>
    struct _Base_bitset<1>
    {
      typedef unsigned long _WordT;
      _WordT _M_w;

      _Base_bitset( void ) : _M_w(0) {}
      _Base_bitset(unsigned long __val) : _M_w(__val) {}

      static size_t
      _S_whichword(size_t __pos )
      { return __pos / _GLIBCPP_BITSET_BITS_PER_WORD; }

      static size_t
      _S_whichbyte(size_t __pos )
      { return (__pos % _GLIBCPP_BITSET_BITS_PER_WORD) / CHAR_BIT; }

      static size_t
      _S_whichbit(size_t __pos )
      {  return __pos % _GLIBCPP_BITSET_BITS_PER_WORD; }

      static _WordT
      _S_maskbit(size_t __pos )
      { return (static_cast<_WordT>(1)) << _S_whichbit(__pos); }

      _WordT&
      _M_getword(size_t) { return _M_w; }

      _WordT
      _M_getword(size_t) const { return _M_w; }

      _WordT&
      _M_hiword() { return _M_w; }

      _WordT
      _M_hiword() const { return _M_w; }

      void
      _M_do_and(const _Base_bitset<1>& __x) { _M_w &= __x._M_w; }

      void
      _M_do_or(const _Base_bitset<1>& __x)  { _M_w |= __x._M_w; }

      void
      _M_do_xor(const _Base_bitset<1>& __x) { _M_w ^= __x._M_w; }

      void
      _M_do_left_shift(size_t __shift) { _M_w <<= __shift; }

      void
      _M_do_right_shift(size_t __shift) { _M_w >>= __shift; }

      void
      _M_do_flip() { _M_w = ~_M_w; }

      void
      _M_do_set() { _M_w = ~static_cast<_WordT>(0); }

      void
      _M_do_reset() { _M_w = 0; }

      bool
      _M_is_equal(const _Base_bitset<1>& __x) const
      { return _M_w == __x._M_w; }

      bool
      _M_is_any() const { return _M_w != 0; }

      size_t
      _M_do_count() const
      {
	size_t __result = 0;
	const unsigned char* __byte_ptr = (const unsigned char*)&_M_w;
	const unsigned char* __end_ptr
	  = ((const unsigned char*)&_M_w)+sizeof(_M_w);
	while ( __byte_ptr < __end_ptr )
	  {
	    __result += _S_bit_count[*__byte_ptr];
	    __byte_ptr++;
	  }
	return __result;
      }

      unsigned long
      _M_do_to_ulong() const { return _M_w; }

      size_t
      _M_do_find_first(size_t __not_found) const;

      // find the next "on" bit that follows "prev"
      size_t
      _M_do_find_next(size_t __prev, size_t __not_found) const;
    };


  /**
   *  @if maint
   *  Base class, specialization for no storage (zero-length %bitset).
   *
   *  See documentation for bitset.
   *  @endif
  */
  template<>
    struct _Base_bitset<0>
    {
      typedef unsigned long _WordT;

      _Base_bitset() {}
      _Base_bitset(unsigned long) {}

      static size_t
      _S_whichword(size_t __pos )
      { return __pos / _GLIBCPP_BITSET_BITS_PER_WORD; }

      static size_t
      _S_whichbyte(size_t __pos )
      { return (__pos % _GLIBCPP_BITSET_BITS_PER_WORD) / CHAR_BIT; }

      static size_t
      _S_whichbit(size_t __pos )
      {  return __pos % _GLIBCPP_BITSET_BITS_PER_WORD; }

      static _WordT
      _S_maskbit(size_t __pos )
      { return (static_cast<_WordT>(1)) << _S_whichbit(__pos); }

      // This would normally give access to the data.  The bounds-checking
      // in the bitset class will prevent the user from getting this far,
      // but (1) it must still return an lvalue to compile, and (2) the
      // user might call _Unchecked_set directly, in which case this /needs/
      // to fail.  Let's not penalize zero-length users unless they actually
      // make an unchecked call; all the memory ugliness is therefore
      // localized to this single should-never-get-this-far function.
      _WordT&
      _M_getword(size_t) const
      { __throw_out_of_range("bitset -- zero-length"); return *new _WordT; }

      _WordT
      _M_hiword() const { return 0; }

      void
      _M_do_and(const _Base_bitset<0>&) { }

      void
      _M_do_or(const _Base_bitset<0>&)  { }

      void
      _M_do_xor(const _Base_bitset<0>&) { }

      void
      _M_do_left_shift(size_t) { }

      void
      _M_do_right_shift(size_t) { }

      void
      _M_do_flip() { }

      void
      _M_do_set() { }

      void
      _M_do_reset() { }

      // Are all empty bitsets equal to each other?  Are they equal to
      // themselves?  How to compare a thing which has no state?  What is
      // the sound of one zero-length bitset clapping?
      bool
      _M_is_equal(const _Base_bitset<0>&) const { return true; }

      bool
      _M_is_any() const { return false; }

      size_t
      _M_do_count() const { return 0; }

      unsigned long
      _M_do_to_ulong() const { return 0; }

      // Normally "not found" is the size, but that could also be
      // misinterpreted as an index in this corner case.  Oh well.
      size_t
      _M_do_find_first(size_t) const { return 0; }

      size_t
      _M_do_find_next(size_t, size_t) const { return 0; }
    };


  // Helper class to zero out the unused high-order bits in the highest word.
  template<size_t _Extrabits>
    struct _Sanitize
    {
      static void _S_do_sanitize(unsigned long& __val)
      { __val &= ~((~static_cast<unsigned long>(0)) << _Extrabits); }
    };

  template<>
    struct _Sanitize<0>
    { static void _S_do_sanitize(unsigned long) { } };


  /**
   *  @brief  The %bitset class represents a @e fixed-size sequence of bits.
   *
   *  @ingroup Containers
   *
   *  (Note that %bitset does @e not meet the formal requirements of a
   *  <a href="tables.html#65">container</a>.  Mainly, it lacks iterators.)
   *
   *  The template argument, @a Nb, may be any non-negative number,
   *  specifying the number of bits (e.g., "0", "12", "1024*1024").
   *
   *  In the general unoptimized case, storage is allocated in word-sized
   *  blocks.  Let B be the number of bits in a word, then (Nb+(B-1))/B
   *  words will be used for storage.  B - Nb%B bits are unused.  (They are
   *  the high-order bits in the highest word.)  It is a class invariant
   *  that those unused bits are always zero.
   *
   *  If you think of %bitset as "a simple array of bits," be aware that
   *  your mental picture is reversed:  a %bitset behaves the same way as
   *  bits in integers do, with the bit at index 0 in the "least significant
   *  / right-hand" position, and the bit at index Nb-1 in the "most
   *  significant / left-hand" position.  Thus, unlike other containers, a
   *  %bitset's index "counts from right to left," to put it very loosely.
   *
   *  This behavior is preserved when translating to and from strings.  For
   *  example, the first line of the following program probably prints
   *  "b('a') is 0001100001" on a modern ASCII system.
   *
   *  @code
   *     #include <bitset>
   *     #include <iostream>
   *     #include <sstream>
   *
   *     using namespace std;
   *
   *     int main()
   *     {
   *         long         a = 'a';
   *         bitset<10>   b(a);
   *
   *         cout << "b('a') is " << b << endl;
   *
   *         ostringstream s;
   *         s << b;
   *         string  str = s.str();
   *         cout << "index 3 in the string is " << str[3] << " but\n"
   *              << "index 3 in the bitset is " << b[3] << endl;
   *     }
   *  @endcode
   *
   *  Also see http://gcc.gnu.org/onlinedocs/libstdc++/ext/sgiexts.html#ch23
   *  for a description of extensions.
   *
   *  @if maint
   *  Most of the actual code isn't contained in %bitset<> itself, but in the
   *  base class _Base_bitset.  The base class works with whole words, not with
   *  individual bits.  This allows us to specialize _Base_bitset for the
   *  important special case where the %bitset is only a single word.
   *
   *  Extra confusion can result due to the fact that the storage for
   *  _Base_bitset @e is a regular array, and is indexed as such.  This is
   *  carefully encapsulated.
   *  @endif
  */
  template<size_t _Nb>
    class bitset : private _Base_bitset<_GLIBCPP_BITSET_WORDS(_Nb)>
  {
  private:
    typedef _Base_bitset<_GLIBCPP_BITSET_WORDS(_Nb)> _Base;
    typedef unsigned long _WordT;

    void
    _M_do_sanitize()
    {
      _Sanitize<_Nb%_GLIBCPP_BITSET_BITS_PER_WORD>::
          _S_do_sanitize(this->_M_hiword());
    }

  public:
    /**
     *  This encapsulates the concept of a single bit.  An instance of this
     *  class is a proxy for an actual bit; this way the individual bit
     *  operations are done as faster word-size bitwise instructions.
     *
     *  Most users will never need to use this class directly; conversions
     *  to and from bool are automatic and should be transparent.  Overloaded
     *  operators help to preserve the illusion.
     *
     *  (On a typical system, this "bit %reference" is 64 times the size of
     *  an actual bit.  Ha.)
    */
    class reference
    {
      friend class bitset;

      _WordT *_M_wp;
      size_t _M_bpos;

      // left undefined
      reference();

    public:
      reference(bitset& __b, size_t __pos)
      {
	_M_wp = &__b._M_getword(__pos);
	_M_bpos = _Base::_S_whichbit(__pos);
      }

      ~reference() { }

      // for b[i] = __x;
      reference&
      operator=(bool __x)
      {
	if ( __x )
	  *_M_wp |= _Base::_S_maskbit(_M_bpos);
	else
	  *_M_wp &= ~_Base::_S_maskbit(_M_bpos);
	return *this;
      }

      // for b[i] = b[__j];
      reference&
      operator=(const reference& __j)
      {
	if ( (*(__j._M_wp) & _Base::_S_maskbit(__j._M_bpos)) )
	  *_M_wp |= _Base::_S_maskbit(_M_bpos);
	else
	  *_M_wp &= ~_Base::_S_maskbit(_M_bpos);
	return *this;
      }

      // flips the bit
      bool
      operator~() const
      { return (*(_M_wp) & _Base::_S_maskbit(_M_bpos)) == 0; }

      // for __x = b[i];
      operator bool() const
      { return (*(_M_wp) & _Base::_S_maskbit(_M_bpos)) != 0; }

      // for b[i].flip();
      reference&
      flip()
      {
	*_M_wp ^= _Base::_S_maskbit(_M_bpos);
	return *this;
      }
    };
    friend class reference;

    // 23.3.5.1 constructors:
    /// All bits set to zero.
    bitset() { }

    /// Initial bits bitwise-copied from a single word (others set to zero).
    bitset(unsigned long __val) : _Base(__val)
    { _M_do_sanitize(); }

    /**
     *  @brief  Use a subset of a string.
     *  @param  s  A string of '0' and '1' characters.
     *  @param  pos  Index of the first character in @a s to use; defaults
     *               to zero.
     *  @throw  std::out_of_range  If @a pos is bigger the size of @a s.
     *  @throw  std::invalid_argument  If a character appears in the string
     *                                 which is neither '0' nor '1'.
    */
    template<class _CharT, class _Traits, class _Alloc>
      explicit bitset(const basic_string<_CharT, _Traits, _Alloc>& __s,
		      size_t __pos = 0) : _Base()
      {
	if (__pos > __s.size())
	  __throw_out_of_range("bitset -- initial position is larger than "
	                       "the string itself");
	_M_copy_from_string(__s, __pos,
			    basic_string<_CharT, _Traits, _Alloc>::npos);
      }

    /**
     *  @brief  Use a subset of a string.
     *  @param  s  A string of '0' and '1' characters.
     *  @param  pos  Index of the first character in @a s to use.
     *  @param  n    The number of characters to copy.
     *  @throw  std::out_of_range  If @a pos is bigger the size of @a s.
     *  @throw  std::invalid_argument  If a character appears in the string
     *                                 which is neither '0' nor '1'.
    */
    template<class _CharT, class _Traits, class _Alloc>
      bitset(const basic_string<_CharT, _Traits, _Alloc>& __s,
	     size_t __pos, size_t __n) : _Base()
      {
	if (__pos > __s.size())
	  __throw_out_of_range("bitset -- initial position is larger than "
	                       "the string itself");
	_M_copy_from_string(__s, __pos, __n);
      }

    // 23.3.5.2 bitset operations:
    //@{
    /**
     *  @brief  Operations on bitsets.
     *  @param  rhs  A same-sized bitset.
     *
     *  These should be self-explanatory.
    */
    bitset<_Nb>&
    operator&=(const bitset<_Nb>& __rhs)
    {
      this->_M_do_and(__rhs);
      return *this;
    }

    bitset<_Nb>&
    operator|=(const bitset<_Nb>& __rhs)
    {
      this->_M_do_or(__rhs);
      return *this;
    }

    bitset<_Nb>&
    operator^=(const bitset<_Nb>& __rhs)
    {
      this->_M_do_xor(__rhs);
      return *this;
    }
    //@}

    //@{
    /**
     *  @brief  Operations on bitsets.
     *  @param  pos  The number of places to shift.
     *
     *  These should be self-explanatory.
    */
    bitset<_Nb>&
    operator<<=(size_t __pos)
    {
      if (__builtin_expect(__pos < _Nb, 1))
        {
          this->_M_do_left_shift(__pos);
          this->_M_do_sanitize();
        }
      else
	this->_M_do_reset();
      return *this;
    }

    bitset<_Nb>&
    operator>>=(size_t __pos)
    {
      if (__builtin_expect(__pos < _Nb, 1))
        {
          this->_M_do_right_shift(__pos);
          this->_M_do_sanitize();
        }
      else
	this->_M_do_reset();
      return *this;
    }
    //@}

    //@{
    /**
     *  These versions of single-bit set, reset, flip, and test are
     *  extensions from the SGI version.  They do no range checking.
     *  @ingroup SGIextensions
    */
    bitset<_Nb>&
    _Unchecked_set(size_t __pos)
    {
      this->_M_getword(__pos) |= _Base::_S_maskbit(__pos);
      return *this;
    }

    bitset<_Nb>&
    _Unchecked_set(size_t __pos, int __val)
    {
      if (__val)
	this->_M_getword(__pos) |= _Base::_S_maskbit(__pos);
      else
	this->_M_getword(__pos) &= ~_Base::_S_maskbit(__pos);
      return *this;
    }

    bitset<_Nb>&
    _Unchecked_reset(size_t __pos)
    {
      this->_M_getword(__pos) &= ~_Base::_S_maskbit(__pos);
      return *this;
    }

    bitset<_Nb>&
    _Unchecked_flip(size_t __pos)
    {
      this->_M_getword(__pos) ^= _Base::_S_maskbit(__pos);
      return *this;
    }

    bool
    _Unchecked_test(size_t __pos) const
    {
      return (this->_M_getword(__pos) & _Base::_S_maskbit(__pos))
	!= static_cast<_WordT>(0);
    }
    //@}

    // Set, reset, and flip.
    /**
     *  @brief Sets every bit to true.
    */
    bitset<_Nb>&
    set()
    {
      this->_M_do_set();
      this->_M_do_sanitize();
      return *this;
    }

    /**
     *  @brief Sets a given bit to a particular value.
     *  @param  pos  The index of the bit.
     *  @param  val  Either true or false, defaults to true.
     *  @throw  std::out_of_range  If @a pos is bigger the size of the %set.
    */
    bitset<_Nb>&
    set(size_t __pos, bool __val = true)
    {
      if (__pos >= _Nb)
	__throw_out_of_range("bitset -- set() argument too large");
      return _Unchecked_set(__pos, __val);
    }

    /**
     *  @brief Sets every bit to false.
    */
    bitset<_Nb>&
    reset()
    {
      this->_M_do_reset();
      return *this;
    }

    /**
     *  @brief Sets a given bit to false.
     *  @param  pos  The index of the bit.
     *  @throw  std::out_of_range  If @a pos is bigger the size of the %set.
     *
     *  Same as writing @c set(pos,false).
    */
    bitset<_Nb>&
    reset(size_t __pos)
    {
      if (__pos >= _Nb)
	__throw_out_of_range("bitset -- reset() argument too large");
      return _Unchecked_reset(__pos);
    }

    /**
     *  @brief Toggles every bit to its opposite value.
    */
    bitset<_Nb>&
    flip()
    {
      this->_M_do_flip();
      this->_M_do_sanitize();
      return *this;
    }

    /**
     *  @brief Toggles a given bit to its opposite value.
     *  @param  pos  The index of the bit.
     *  @throw  std::out_of_range  If @a pos is bigger the size of the %set.
    */
    bitset<_Nb>&
    flip(size_t __pos)
    {
      if (__pos >= _Nb)
	__throw_out_of_range("bitset -- flip() argument too large");
      return _Unchecked_flip(__pos);
    }

    /// See the no-argument flip().
    bitset<_Nb>
    operator~() const { return bitset<_Nb>(*this).flip(); }

    //@{
    /**
     *  @brief  Array-indexing support.
     *  @param  pos  Index into the %bitset.
     *  @return  A bool for a 'const %bitset'.  For non-const bitsets, an
     *           instance of the reference proxy class.
     *  @note  These operators do no range checking and throw no exceptions,
     *         as required by DR 11 to the standard.
     *
     *  @if maint
     *  _GLIBCPP_RESOLVE_LIB_DEFECTS Note that this implementation already
     *  resolves DR 11 (items 1 and 2), but does not do the range-checking
     *  required by that DR's resolution.  -pme
     *  The DR has since been changed:  range-checking is a precondition
     *  (users' responsibility), and these functions must not throw.  -pme
     *  @endif
    */
    reference
    operator[](size_t __pos) { return reference(*this,__pos); }

    bool
    operator[](size_t __pos) const { return _Unchecked_test(__pos); }
    //@}

    /**
     *  @brief Retuns a numerical interpretation of the %bitset.
     *  @return  The integral equivalent of the bits.
     *  @throw  std::overflow_error  If there are too many bits to be
     *                               represented in an @c unsigned @c long.
    */
    unsigned long
    to_ulong() const { return this->_M_do_to_ulong(); }

    /**
     *  @brief Retuns a character interpretation of the %bitset.
     *  @return  The string equivalent of the bits.
     *
     *  Note the ordering of the bits:  decreasing character positions
     *  correspond to increasing bit positions (see the main class notes for
     *  an example).
     *
     *  Also note that you must specify the string's template parameters
     *  explicitly.  Given a bitset @c bs and a string @s:
     *  @code
     *     s = bs.to_string<char,char_traits<char>,allocator<char> >();
     *  @endcode
    */
    template<class _CharT, class _Traits, class _Alloc>
      basic_string<_CharT, _Traits, _Alloc>
      to_string() const
      {
	basic_string<_CharT, _Traits, _Alloc> __result;
	_M_copy_to_string(__result);
	return __result;
      }

    // Helper functions for string operations.
    template<class _CharT, class _Traits, class _Alloc>
      void
      _M_copy_from_string(const basic_string<_CharT,_Traits,_Alloc>& __s,
                          size_t, size_t);

    template<class _CharT, class _Traits, class _Alloc>
      void
      _M_copy_to_string(basic_string<_CharT,_Traits,_Alloc>&) const;

    /// Returns the number of bits which are set.
    size_t
    count() const { return this->_M_do_count(); }

    /// Returns the total number of bits.
    size_t
    size() const { return _Nb; }

    //@{
    /// These comparisons for equality/inequality are, well, @e bitwise.
    bool
    operator==(const bitset<_Nb>& __rhs) const
    { return this->_M_is_equal(__rhs); }

    bool
    operator!=(const bitset<_Nb>& __rhs) const
    { return !this->_M_is_equal(__rhs); }
    //@}

    /**
     *  @brief Tests the value of a bit.
     *  @param  pos  The index of a bit.
     *  @return  The value at @a pos.
     *  @throw  std::out_of_range  If @a pos is bigger the size of the %set.
    */
    bool
    test(size_t __pos) const
    {
      if (__pos >= _Nb)
	__throw_out_of_range("bitset -- test() argument too large");
      return _Unchecked_test(__pos);
    }

    /**
     *  @brief Tests whether any of the bits are on.
     *  @return  True if at least one bit is set.
    */
    bool
    any() const { return this->_M_is_any(); }

    /**
     *  @brief Tests whether any of the bits are on.
     *  @return  True if none of the bits are set.
    */
    bool
    none() const { return !this->_M_is_any(); }

    //@{
    /// Self-explanatory.
    bitset<_Nb>
    operator<<(size_t __pos) const
    { return bitset<_Nb>(*this) <<= __pos; }

    bitset<_Nb>
    operator>>(size_t __pos) const
    { return bitset<_Nb>(*this) >>= __pos; }
    //@}

    /**
     *  @brief  Finds the index of the first "on" bit.
     *  @return  The index of the first bit set, or size() if not found.
     *  @ingroup SGIextensions
     *  @sa  _Find_next
    */
    size_t
    _Find_first() const
    { return this->_M_do_find_first(_Nb); }

    /**
     *  @brief  Finds the index of the next "on" bit after prev.
     *  @return  The index of the next bit set, or size() if not found.
     *  @param  prev  Where to start searching.
     *  @ingroup SGIextensions
     *  @sa  _Find_first
    */
    size_t
    _Find_next(size_t __prev ) const
    { return this->_M_do_find_next(__prev, _Nb); }
  };

  // Definitions of non-inline member functions.
  template<size_t _Nb>
    template<class _CharT, class _Traits, class _Alloc>
    void
    bitset<_Nb>::_M_copy_from_string(const basic_string<_CharT,_Traits,_Alloc>& __s, size_t __pos, size_t __n)
    {
      reset();
      const size_t __nbits = min(_Nb, min(__n, __s.size() - __pos));
      for (size_t __i = 0; __i < __nbits; ++__i)
	{
	  switch(__s[__pos + __nbits - __i - 1])
	    {
	    case '0':
	      break;
	    case '1':
	      set(__i);
	      break;
	    default:
	      __throw_invalid_argument("bitset -- string contains characters "
	                               "which are neither 0 nor 1");
	    }
	}
    }

  template<size_t _Nb>
    template<class _CharT, class _Traits, class _Alloc>
    void
    bitset<_Nb>::_M_copy_to_string(basic_string<_CharT, _Traits, _Alloc>& __s) const
    {
      __s.assign(_Nb, '0');
      for (size_t __i = 0; __i < _Nb; ++__i)
	if (_Unchecked_test(__i))
	  __s[_Nb - 1 - __i] = '1';
    }

  // 23.3.5.3 bitset operations:
  //@{
  /**
   *  @brief  Global bitwise operations on bitsets.
   *  @param  x  A bitset.
   *  @param  y  A bitset of the same size as @a x.
   *  @return  A new bitset.
   *
   *  These should be self-explanatory.
  */
  template<size_t _Nb>
    inline bitset<_Nb>
    operator&(const bitset<_Nb>& __x, const bitset<_Nb>& __y)
    {
      bitset<_Nb> __result(__x);
      __result &= __y;
      return __result;
    }

  template<size_t _Nb>
    inline bitset<_Nb>
    operator|(const bitset<_Nb>& __x, const bitset<_Nb>& __y)
    {
      bitset<_Nb> __result(__x);
      __result |= __y;
      return __result;
    }

  template <size_t _Nb>
    inline bitset<_Nb>
    operator^(const bitset<_Nb>& __x, const bitset<_Nb>& __y)
    {
      bitset<_Nb> __result(__x);
      __result ^= __y;
      return __result;
    }
  //@}

  //@{
  /**
   *  @brief Global I/O operators for bitsets.
   *
   *  Direct I/O between streams and bitsets is supported.  Output is
   *  straightforward.  Input will skip whitespace, only accept '0' and '1'
   *  characters, and will only extract as many digits as the %bitset will
   *  hold.
  */
  template<class _CharT, class _Traits, size_t _Nb>
    basic_istream<_CharT, _Traits>&
    operator>>(basic_istream<_CharT, _Traits>& __is, bitset<_Nb>& __x)
    {
      typedef typename _Traits::char_type char_type;
      basic_string<_CharT, _Traits> __tmp;
      __tmp.reserve(_Nb);

      // Skip whitespace
      typename basic_istream<_CharT, _Traits>::sentry __sentry(__is);
      if (__sentry)
	{
	  ios_base::iostate  __state = ios_base::goodbit;
	  basic_streambuf<_CharT, _Traits>* __buf = __is.rdbuf();
	  for (size_t __i = 0; __i < _Nb; ++__i)
	    {
	      static typename _Traits::int_type __eof = _Traits::eof();

	      typename _Traits::int_type __c1 = __buf->sbumpc();
	      if (_Traits::eq_int_type(__c1, __eof))
		{
		  __state |= ios_base::eofbit;
		  break;
		}
	      else
		{
		  char_type __c2 = _Traits::to_char_type(__c1);
		  char_type __c  = __is.narrow(__c2, '*');

		  if (__c == '0' || __c == '1')
		    __tmp.push_back(__c);
		  else if (_Traits::eq_int_type(__buf->sputbackc(__c2), __eof))
		    {
		      __state |= ios_base::failbit;
		      break;
		    }
		}
	    }

	  if (__tmp.empty() && !_Nb)
	    __state |= ios_base::failbit;
	  else
	    __x._M_copy_from_string(__tmp, static_cast<size_t>(0), _Nb);

	  if (__state != ios_base::goodbit)
	    __is.setstate(__state);    // may throw an exception
	}

      return __is;
    }

  template <class _CharT, class _Traits, size_t _Nb>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __os, const bitset<_Nb>& __x)
    {
      basic_string<_CharT, _Traits> __tmp;
      __x._M_copy_to_string(__tmp);
      return __os << __tmp;
    }
  //@}
} // namespace std

#undef _GLIBCPP_BITSET_WORDS

#endif /* _GLIBCPP_BITSET_H */
