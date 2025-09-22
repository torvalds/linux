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
 * @file point_iterator.hpp
 * Contains an iterator class returned by the tables' find and insert
 *     methods.
 */

// Find type iterator.
class point_iterator_
{

public:

  // Category.
  typedef trivial_iterator_tag iterator_category;

  // Difference type.
  typedef trivial_iterator_difference_type difference_type;

  // Iterator's value type.
  typedef value_type_ value_type;

  // Iterator's pointer type.
  typedef pointer_ pointer;

  // Iterator's const pointer type.
  typedef const_pointer_ const_pointer;

  // Iterator's reference type.
  typedef reference_ reference;

  // Iterator's const reference type.
  typedef const_reference_ const_reference;

public:

  // Default constructor.
  inline
  point_iterator_()

    : m_p_value(NULL)
  { }

  // Copy constructor.
  inline
  point_iterator_(const point_iterator_& other)

    : m_p_value(other.m_p_value)
  { }

  // Access.
  inline pointer
  operator->() const
  {
    _GLIBCXX_DEBUG_ASSERT(m_p_value != NULL);

    return (m_p_value);
  }

  // Access.
  inline reference
  operator*() const
  {
    _GLIBCXX_DEBUG_ASSERT(m_p_value != NULL);

    return (*m_p_value);
  }

  // Compares content to a different iterator object.
  inline bool
  operator==(const point_iterator_& other) const
  {
    return (m_p_value == other.m_p_value);
  }

  // Compares content to a different iterator object.
  inline bool
  operator==(const const_point_iterator_& other) const
  {
    return (m_p_value == other.m_p_value);
  }

  // Compares content to a different iterator object.
  inline bool
  operator!=(const point_iterator_& other) const
  {
    return (m_p_value != other.m_p_value);
  }

  // Compares content (negatively) to a different iterator object.
  inline bool
  operator!=(const const_point_iterator_& other) const
  {
    return (m_p_value != other.m_p_value);
  }

  inline
  point_iterator_(pointer p_value) : m_p_value(p_value)
  { }

protected:
  friend class const_point_iterator_;

  friend class PB_DS_CLASS_C_DEC;

protected:
  pointer m_p_value;
};

