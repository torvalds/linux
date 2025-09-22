// -*- C++ -*-

// Copyright (C) 2005, 2006 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software
// Foundation; either version 2, or (at your option) any later
// version.

// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this library; see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330, Boston,
// MA 02111-1307, USA.

// As a special exception, you may use this file as part of a free
// software library without restriction.  Specifically, if other files
// instantiate templates or use macros or inline functions from this
// file, or you compile this file and link it with other files to
// produce an executable, this file does not by itself cause the
// resulting executable to be covered by the GNU General Public
// License.  This exception does not however invalidate any other
// reasons why the executable file might be covered by the GNU General
// Public License.

// Copyright (C) 2004 Ami Tavory and Vladimir Dreizin, IBM-HRL.

// Permission to use, copy, modify, sell, and distribute this software
// is hereby granted without fee, provided that the above copyright
// notice appears in all copies, and that both that copyright notice
// and this permission notice appear in supporting documentation. None
// of the above authors, nor IBM Haifa Research Laboratories, make any
// representation about the suitability of this software for any
// purpose. It is provided "as is" without express or implied
// warranty.

/**
 * @file const_iterator.hpp
 * Contains an iterator class returned by the table's const find and insert
 *     methods.
 */

#ifndef PB_DS_BINARY_HEAP_CONST_ITERATOR_HPP
#define PB_DS_BINARY_HEAP_CONST_ITERATOR_HPP

#include <ext/pb_ds/detail/binary_heap_/const_point_iterator.hpp>
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_CLASS_C_DEC \
    binary_heap_const_iterator_<Value_Type, Entry, Simple, Allocator>

#define PB_DS_BASE_C_DEC \
    binary_heap_const_point_iterator_<Value_Type, Entry, Simple, Allocator>

    // Const point-type iterator.
    template<typename Value_Type,
	     typename Entry,
	     bool Simple,
	     class Allocator>
    class binary_heap_const_iterator_ : public PB_DS_BASE_C_DEC
    {

    private:
      typedef typename PB_DS_BASE_C_DEC::entry_pointer entry_pointer;

      typedef PB_DS_BASE_C_DEC base_type;

    public:

      // Category.
      typedef std::forward_iterator_tag iterator_category;

      // Difference type.
      typedef typename Allocator::difference_type difference_type;

      // Iterator's value type.
      typedef typename base_type::value_type value_type;

      // Iterator's pointer type.
      typedef typename base_type::pointer pointer;

      // Iterator's const pointer type.
      typedef typename base_type::const_pointer const_pointer;

      // Iterator's reference type.
      typedef typename base_type::reference reference;

      // Iterator's const reference type.
      typedef typename base_type::const_reference const_reference;

    public:

      inline
      binary_heap_const_iterator_(entry_pointer p_e) : base_type(p_e)
      { }

      // Default constructor.
      inline
      binary_heap_const_iterator_()
      { }

      // Copy constructor.
      inline
      binary_heap_const_iterator_(const PB_DS_CLASS_C_DEC& other) : base_type(other)
      { }

      // Compares content to a different iterator object.
      inline bool
      operator==(const PB_DS_CLASS_C_DEC& other) const
      {
	return base_type::m_p_e == other.m_p_e;
      }

      // Compares content (negatively) to a different iterator object.
      inline bool
      operator!=(const PB_DS_CLASS_C_DEC& other) const
      {
	return base_type::m_p_e != other.m_p_e;
      }

      inline PB_DS_CLASS_C_DEC& 
      operator++()
      {
	_GLIBCXX_DEBUG_ASSERT(base_type::m_p_e != NULL);
	inc();
	return *this;
      }

      inline PB_DS_CLASS_C_DEC
      operator++(int)
      {
	PB_DS_CLASS_C_DEC ret_it(base_type::m_p_e);
	operator++();
	return ret_it;
      }

    private:
      void
      inc()
      { ++base_type::m_p_e; }
    };

#undef PB_DS_CLASS_C_DEC
#undef PB_DS_BASE_C_DEC
  } // namespace detail
} // namespace pb_ds

#endif 
