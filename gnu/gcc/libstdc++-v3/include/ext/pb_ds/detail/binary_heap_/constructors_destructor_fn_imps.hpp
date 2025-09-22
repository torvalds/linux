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
 * @file constructors_destructor_fn_imps.hpp
 * Contains an implementation class for binary_heap_.
 */

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::entry_allocator
PB_DS_CLASS_C_DEC::s_entry_allocator;

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::value_allocator
PB_DS_CLASS_C_DEC::s_value_allocator;

PB_DS_CLASS_T_DEC
typename PB_DS_CLASS_C_DEC::no_throw_copies_t
PB_DS_CLASS_C_DEC::s_no_throw_copies_ind;

PB_DS_CLASS_T_DEC
template<typename It>
void
PB_DS_CLASS_C_DEC::
copy_from_range(It first_it, It last_it)
{
  while (first_it != last_it)
    {
      insert_value(*first_it, s_no_throw_copies_ind);
      ++first_it;
    }

  std::make_heap(m_a_entries, m_a_entries + m_size, static_cast<entry_cmp& >(*this));

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
binary_heap_() :
  m_size(0),
  m_actual_size(resize_policy::min_size),
  m_a_entries(s_entry_allocator.allocate(m_actual_size))
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
binary_heap_(const Cmp_Fn& r_cmp_fn) :
  entry_cmp(r_cmp_fn),
  m_size(0),
  m_actual_size(resize_policy::min_size),
  m_a_entries(s_entry_allocator.allocate(m_actual_size))
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
binary_heap_(const PB_DS_CLASS_C_DEC& other) :
  entry_cmp(other),
  resize_policy(other),
  m_size(0),
  m_actual_size(other.m_actual_size),
  m_a_entries(s_entry_allocator.allocate(m_actual_size))
{
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
  _GLIBCXX_DEBUG_ASSERT(m_a_entries != other.m_a_entries);

  const_iterator first_it = other.begin();
  const_iterator last_it = other.end();

  try
    {
      while (first_it != last_it)
        {
	  insert_value(*first_it, s_no_throw_copies_ind);
	  ++first_it;
        }
    }
  catch(...)
    {
      for (size_type i = 0; i < m_size; ++i)
	erase_at(m_a_entries, i, s_no_throw_copies_ind);

      s_entry_allocator.deallocate(m_a_entries, m_actual_size);
      __throw_exception_again;
    }
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
swap(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
  _GLIBCXX_DEBUG_ASSERT(m_a_entries != other.m_a_entries);

  value_swap(other);
  std::swap((entry_cmp& )(*this), (entry_cmp& )other);
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
value_swap(PB_DS_CLASS_C_DEC& other)
{
  std::swap(m_a_entries, other.m_a_entries);
  std::swap(m_size, other.m_size);
  std::swap(m_actual_size, other.m_actual_size);
  static_cast<resize_policy*>(this)->swap(other);
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
~binary_heap_()
{
  for (size_type i = 0; i < m_size; ++i)
    erase_at(m_a_entries, i, s_no_throw_copies_ind);
  s_entry_allocator.deallocate(m_a_entries, m_actual_size);
}

