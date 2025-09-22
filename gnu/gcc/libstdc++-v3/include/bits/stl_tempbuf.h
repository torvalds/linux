// Temporary buffer implementation -*- C++ -*-

// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006 
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

/*
 *
 * Copyright (c) 1994
 * Hewlett-Packard Company
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Hewlett-Packard Company makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 *
 * Copyright (c) 1996,1997
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

/** @file stl_tempbuf.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _TEMPBUF_H
#define _TEMPBUF_H 1

#include <memory>

_GLIBCXX_BEGIN_NAMESPACE(std)

  /**
   *  @if maint
   *  This class is used in two places: stl_algo.h and ext/memory,
   *  where it is wrapped as the temporary_buffer class.  See
   *  temporary_buffer docs for more notes.
   *  @endif
   */
  template<typename _ForwardIterator, typename _Tp>
    class _Temporary_buffer
    {
      // concept requirements
      __glibcxx_class_requires(_ForwardIterator, _ForwardIteratorConcept)

    public:
      typedef _Tp         value_type;
      typedef value_type* pointer;
      typedef pointer     iterator;
      typedef ptrdiff_t   size_type;

    protected:
      size_type  _M_original_len;
      size_type  _M_len;
      pointer    _M_buffer;

      void
      _M_initialize_buffer(const _Tp&, __true_type) { }

      void
      _M_initialize_buffer(const _Tp& __val, __false_type)
      { std::uninitialized_fill_n(_M_buffer, _M_len, __val); }

    public:
      /// As per Table mumble.
      size_type
      size() const
      { return _M_len; }

      /// Returns the size requested by the constructor; may be >size().
      size_type
      requested_size() const
      { return _M_original_len; }

      /// As per Table mumble.
      iterator
      begin()
      { return _M_buffer; }

      /// As per Table mumble.
      iterator
      end()
      { return _M_buffer + _M_len; }

      /**
       * Constructs a temporary buffer of a size somewhere between
       * zero and the size of the given range.
       */
      _Temporary_buffer(_ForwardIterator __first, _ForwardIterator __last);

      ~_Temporary_buffer()
      {
	std::_Destroy(_M_buffer, _M_buffer + _M_len);
	std::return_temporary_buffer(_M_buffer);
      }

    private:
      // Disable copy constructor and assignment operator.
      _Temporary_buffer(const _Temporary_buffer&);

      void
      operator=(const _Temporary_buffer&);
    };


  template<typename _ForwardIterator, typename _Tp>
    _Temporary_buffer<_ForwardIterator, _Tp>::
    _Temporary_buffer(_ForwardIterator __first, _ForwardIterator __last)
    : _M_original_len(std::distance(__first, __last)),
      _M_len(0), _M_buffer(0)
    {
      // Workaround for a __type_traits bug in the pre-7.3 compiler.
      typedef typename std::__is_scalar<_Tp>::__type _Trivial;

      try
	{
	  pair<pointer, size_type> __p(get_temporary_buffer<
				       value_type>(_M_original_len));
	  _M_buffer = __p.first;
	  _M_len = __p.second;
	  if (_M_len > 0)
	    _M_initialize_buffer(*__first, _Trivial());
	}
      catch(...)
	{
	  std::return_temporary_buffer(_M_buffer);
	  _M_buffer = 0;
	  _M_len = 0;
	  __throw_exception_again;
	}
    }

_GLIBCXX_END_NAMESPACE

#endif /* _TEMPBUF_H */

