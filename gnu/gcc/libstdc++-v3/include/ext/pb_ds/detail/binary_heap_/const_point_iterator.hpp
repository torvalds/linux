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
 * @file const_point_iterator.hpp
 * Contains an iterator class returned by the table's const find and insert
 *     methods.
 */

#ifndef PB_DS_BINARY_HEAP_CONST_FIND_ITERATOR_HPP
#define PB_DS_BINARY_HEAP_CONST_FIND_ITERATOR_HPP

#include <ext/pb_ds/tag_and_trait.hpp>
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {
    // Const point-type iterator.
    template<typename Value_Type, typename Entry, bool Simple, 
	     typename Allocator>
    class binary_heap_const_point_iterator_
    {
    protected:
      typedef typename Allocator::template rebind<Entry>::other::pointer entry_pointer;

    public:
      // Category.
      typedef trivial_iterator_tag iterator_category;

      // Difference type.
      typedef trivial_iterator_difference_type difference_type;

      // Iterator's value type.
      typedef Value_Type value_type;

      // Iterator's pointer type.
      typedef typename Allocator::template rebind<value_type>::other::pointer
      pointer;

      // Iterator's const pointer type.
      typedef
      typename Allocator::template rebind<value_type>::other::const_pointer
      const_pointer;

      // Iterator's reference type.
      typedef
      typename Allocator::template rebind<value_type>::other::reference
      reference;

      // Iterator's const reference type.
      typedef
      typename Allocator::template rebind<value_type>::other::const_reference
      const_reference;

      inline
      binary_heap_const_point_iterator_(entry_pointer p_e) : m_p_e(p_e)
      { }

      // Default constructor.
      inline
      binary_heap_const_point_iterator_() : m_p_e(NULL) { }

      // Copy constructor.
      inline
      binary_heap_const_point_iterator_(const binary_heap_const_point_iterator_& other)
      : m_p_e(other.m_p_e)
      { }

      // Access.
      inline const_pointer
      operator->() const
      {
	_GLIBCXX_DEBUG_ASSERT(m_p_e != NULL);
	return to_ptr(integral_constant<int, Simple>());
      }

      // Access.
      inline const_reference
      operator*() const
      {
	_GLIBCXX_DEBUG_ASSERT(m_p_e != NULL);
	return *to_ptr(integral_constant<int, Simple>());
      }

      // Compares content to a different iterator object.
      inline bool
      operator==(const binary_heap_const_point_iterator_& other) const
      { return m_p_e == other.m_p_e; }

      // Compares content (negatively) to a different iterator object.
      inline bool
      operator!=(const binary_heap_const_point_iterator_& other) const
      { return m_p_e != other.m_p_e; }

    private:
      inline const_pointer
      to_ptr(true_type) const
      { return m_p_e; }

      inline const_pointer
      to_ptr(false_type) const
      { return *m_p_e; }

    public:
      entry_pointer m_p_e;
    };
  } // namespace detail
} // namespace pb_ds

#endif 
