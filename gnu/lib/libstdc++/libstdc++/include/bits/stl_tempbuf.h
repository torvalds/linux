// Temporary buffer implementation -*- C++ -*-

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

#ifndef __GLIBCPP_INTERNAL_TEMPBUF_H
#define __GLIBCPP_INTERNAL_TEMPBUF_H

namespace std
{

/**
 *  @if maint
 *  This class is used in two places:  stl_algo.h and ext/memory, where it
 *  is wrapped as the temporary_buffer class.  See temporary_buffer docs for
 *  more notes.
 *  @endif
*/
template <class _ForwardIterator, class _Tp>
  class _Temporary_buffer
{
  // concept requirements
  __glibcpp_class_requires(_ForwardIterator, _ForwardIteratorConcept)

  ptrdiff_t  _M_original_len;
  ptrdiff_t  _M_len;
  _Tp*       _M_buffer;

  // this is basically get_temporary_buffer() all over again
  void _M_allocate_buffer() {
    _M_original_len = _M_len;
    _M_buffer = 0;

    if (_M_len > (ptrdiff_t)(INT_MAX / sizeof(_Tp)))
      _M_len = INT_MAX / sizeof(_Tp);

    while (_M_len > 0) {
      _M_buffer = (_Tp*) malloc(_M_len * sizeof(_Tp));
      if (_M_buffer)
        break;
      _M_len /= 2;
    }
  }

  void _M_initialize_buffer(const _Tp&, __true_type) {}
  void _M_initialize_buffer(const _Tp& val, __false_type) {
    uninitialized_fill_n(_M_buffer, _M_len, val);
  }

public:
  /// As per Table mumble.
  ptrdiff_t size() const { return _M_len; }
  /// Returns the size requested by the constructor; may be >size().
  ptrdiff_t requested_size() const { return _M_original_len; }
  /// As per Table mumble.
  _Tp* begin() { return _M_buffer; }
  /// As per Table mumble.
  _Tp* end() { return _M_buffer + _M_len; }

  _Temporary_buffer(_ForwardIterator __first, _ForwardIterator __last) {
    // Workaround for a __type_traits bug in the pre-7.3 compiler.
    typedef typename __type_traits<_Tp>::has_trivial_default_constructor
            _Trivial;

    try {
      _M_len = distance(__first, __last);
      _M_allocate_buffer();
      if (_M_len > 0)
        _M_initialize_buffer(*__first, _Trivial());
    }
    catch(...)
      { 
	free(_M_buffer); 
	_M_buffer = 0; 
	_M_len = 0;
	__throw_exception_again; 
      }
  }
 
  ~_Temporary_buffer() {  
    _Destroy(_M_buffer, _M_buffer + _M_len);
    free(_M_buffer);
  }

private:
  // Disable copy constructor and assignment operator.
  _Temporary_buffer(const _Temporary_buffer&) {}
  void operator=(const _Temporary_buffer&) {}
};
    
} // namespace std

#endif /* __GLIBCPP_INTERNAL_TEMPBUF_H */

